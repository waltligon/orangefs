/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * Changes by Acxiom Corporation to add relative path support to
 * PVFS_util_resolve(),
 * Copyright � Acxiom Corporation, 2005
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <libgen.h>

#define __PINT_REQPROTO_ENCODE_FUNCS_C
#include "pvfs2-config.h"
#include "pvfs2-internal.h"
#include "pvfs2-sysint.h"
#include "pvfs2-util.h"
#include "pvfs2-debug.h"
#include "gossip.h"
#include "pvfs2-attr.h"
#include "pvfs2-types-debug.h"
#include "str-utils.h"
#include "gen-locks.h"
#include "realpath.h"
#include "pint-sysint-utils.h"
#include "pint-util.h"
#include "security-util.h"
#include "pvfs-path.h"

#if defined(ENABLE_SECURITY_CERT) || defined(ENABLE_SECURITY_KEY)
#define ENABLE_SECURITY_MODE
#endif

#ifndef ENABLE_SECURITY_MODE
#include <pwd.h>
#include <grp.h>
#ifndef HAVE_GETGROUPLIST
#include "getugroups.h"
#endif
#endif

#ifdef HAVE_MNTENT_H

#include <mntent.h>
#define PINT_fstab_t FILE
#define PINT_fstab_entry_t struct mntent
#define PINT_fstab_open(_fstab, _fname) (_fstab) = setmntent(_fname, "r")
#define PINT_fstab_close(_tab) endmntent(_tab)
#define PINT_fstab_next_entry(_tab) getmntent(_tab)
#define PINT_fstab_entry_destroy(_entry) _entry = NULL
#define PINT_fstab_entry_hasopt(_entry, _opt) hasmntopt(_entry, _opt)

#define PINT_FSTAB_NAME(_entry) (_entry)->mnt_fsname
#define PINT_FSTAB_PATH(_entry) (_entry)->mnt_dir
#define PINT_FSTAB_TYPE(_entry) (_entry)->mnt_type
#define PINT_FSTAB_OPTS(_entry) (_entry)->mnt_opts

#elif HAVE_FSTAB_H

#include <fstab.h>
#define PINT_fstab_t FILE
#define PINT_fstab_entry_t struct fstab
#define PINT_fstab_open(_fstab, _fname) _fstab = fopen(_fname, "r")
#define PINT_fstab_close(_tab) fclose(_tab)
#define PINT_fstab_next_entry(_tab) PINT_util_my_get_next_fsent(_tab)
#define PINT_fstab_entry_destroy(_entry) PINT_util_fsent_destroy(_entry)
#define PINT_fstab_entry_hasopt(_entry, _opt) strstr((_entry)->fs_mntops, _opt)

#define PINT_FSTAB_NAME(_entry) (_entry)->fs_spec
#define PINT_FSTAB_PATH(_entry) (_entry)->fs_file
#define PINT_FSTAB_TYPE(_entry) (_entry)->fs_vfstype
#define PINT_FSTAB_OPTS(_entry) (_entry)->fs_mntops

#define DEFINE_MY_GET_NEXT_FSENT
#define PVFS_MY_GET_NEXT_FSENT_MAX_LINESIZE 2048
static struct fstab * PINT_util_my_get_next_fsent(PINT_fstab_t * tab);
static void PINT_util_fsent_destroy(PINT_fstab_entry_t * entry);

#else

#error OS does not have mntent.h or fstab.h.  
#error Add your own fstab parser macros to fix.

#endif

#define PVFS2_MAX_INVALID_MNTENTS                     256
#define PVFS2_MAX_TABFILES                              8
#define PVFS2_DYNAMIC_TAB_INDEX  (PVFS2_MAX_TABFILES - 1)
#define PVFS2_DYNAMIC_TAB_NAME              "<DynamicTab>"

static PVFS_util_tab s_stat_tab_array[PVFS2_MAX_TABFILES];
static int s_stat_tab_count = 0;
static gen_mutex_t s_stat_tab_mutex = GEN_MUTEX_INITIALIZER;

static int parse_flowproto_string(const char *input,
                                  enum PVFS_flowproto_type *flowproto);

static int parse_encoding_string(const char *cp,
                                 enum PVFS_encoding_type *et);

static int parse_num_dfiles_string(const char* cp, int* num_dfiles);

#ifndef ENABLE_SECURITY_MODE
static int PINT_is_idnum(const char *str);

static int PINT_gen_unsigned_credential(const char *user, const char *group,
                                        unsigned int timeout, PVFS_credential *cred);
#endif

/**
 * PVFS_util_gen_mntent
 */
struct PVFS_sys_mntent* PVFS_util_gen_mntent(char* config_server,
                                             char* fs_name)
{
    struct PVFS_sys_mntent* tmp_ent = NULL;

    tmp_ent = (struct PVFS_sys_mntent*)malloc(sizeof(struct
        PVFS_sys_mntent));
    if(!tmp_ent)
    {
        return(NULL);
    }
    memset(tmp_ent, 0, sizeof(struct PVFS_sys_mntent));

    tmp_ent->num_pvfs_config_servers = 1;
    tmp_ent->pvfs_config_servers = (char**)malloc(sizeof(char*));
    if(!tmp_ent->pvfs_config_servers)
    {
        free(tmp_ent);
        return(NULL);
    }

    tmp_ent->pvfs_config_servers[0] = strdup(config_server);
    if(!tmp_ent->pvfs_config_servers[0])
    {
        free(tmp_ent->pvfs_config_servers);
        free(tmp_ent);
        return(NULL);
    }

    tmp_ent->pvfs_fs_name = strdup(fs_name);
    if(!tmp_ent->pvfs_fs_name)
    {
        free(tmp_ent->pvfs_config_servers[0]);
        free(tmp_ent->pvfs_config_servers);
        free(tmp_ent);
        return(NULL);
    }

    tmp_ent->flowproto = FLOWPROTO_DEFAULT;
    tmp_ent->encoding = PVFS2_ENCODING_DEFAULT;

    return(tmp_ent);
}

void PVFS_util_gen_mntent_release(struct PVFS_sys_mntent* mntent)
{
    free(mntent->pvfs_config_servers[0]);
    free(mntent->pvfs_config_servers);
    free(mntent->pvfs_fs_name);
    mntent->pvfs_fs_name = NULL;
    free(mntent);
    return;
}

/* PVFS_util_gen_credential_defaults
 * 
 * Generate a signed credential for the current user, with a 
 * default timeout.
 */
int PVFS_util_gen_credential_defaults(PVFS_credential *cred)
{
    return PVFS_util_gen_credential(NULL, NULL,
                                    PVFS2_DEFAULT_CREDENTIAL_TIMEOUT,
                                    NULL, NULL,
                                    cred);
}

#ifdef ENABLE_SECURITY_MODE
void debug_gencred(char *args[])
{
    char **p = args, *str;
    size_t slen = 0;

    while (*p)
    {
        slen += strlen(*p++) + 1;
    }

    str = calloc(1, slen + 1);
    if (str == NULL)
    {
        return;
    }

    p = args;
    while (*p)
    {
        strcat(str, *p++);
        strcat(str, " ");
    }
    
    gossip_debug(GOSSIP_SECURITY_DEBUG, "Executing pvfs2-gencred: %s\n", str);

    free(str);
}

/* PVFS_util_gen_credential
 * 
 * Generate signed credential object using external app pvfs2-gencred.
 *
 * user - string representation of numeric uid
 * group - string representation of numeric gid
 * timeout - in seconds; value of 0 will result in default (1 hour)
 * keypath - path to client private key file
 * certpath - path to client certificate file
 * cred - the credential object
 */
int PVFS_util_gen_credential(const char *user, const char *group,
    unsigned int timeout, const char *keypath, const char *certpath,
    PVFS_credential *cred)
{
    struct sigaction newsa, oldsa;
    pid_t pid;
    int filedes[2], errordes[2];
    int ret;

    if (!keypath && getenv("PVFS2KEY_FILE"))
    {
        keypath = getenv("PVFS2KEY_FILE");
    }

#ifdef ENABLE_SECURITY_CERT
    if (!certpath && getenv("PVFS2CERT_FILE"))
    {
        certpath = getenv("PVFS2CERT_FILE");
    }
#endif

    memset(&newsa, 0, sizeof(newsa));
    newsa.sa_handler = SIG_DFL;
    sigaction(SIGCHLD, &newsa, &oldsa);

    /* pipe to read credential from stdout of pvfs2-gencred */
    ret = pipe(filedes);
    if (ret == -1)
    {
        return -PVFS_errno_to_error(errno);
    }
    /* pipe to read any error messages from stderr of pvfs2-gencred */
    ret = pipe(errordes);
    if (ret == -1)
    {
        return -PVFS_errno_to_error(errno);
    }

    pid = fork();
    if (pid == 0)
    {
        char *args[7];
        char **ptr = args;
        char timearg[16];

        close(STDERR_FILENO);
        dup(errordes[1]);
        close(STDOUT_FILENO);
        dup(filedes[1]);
        close(STDIN_FILENO);

        *ptr++ = BINDIR"/pvfs2-gencred";

        if (user)
        {
            *ptr++ = "-u";
            *ptr++ = (char *) user;
        }

        if (group)
        {
            *ptr++ = "-g";
            *ptr++ = (char *) group;
        }

        if (timeout != 0 && 
            timeout != PVFS2_DEFAULT_CREDENTIAL_TIMEOUT)
        {
           snprintf(timearg, sizeof(timearg), "%u", timeout);
           *ptr++ = "-t";
           *ptr++ = timearg;
        }

        if (keypath)
        {
            *ptr++ = "-k";
            *ptr++ = (char *) keypath;
        }
#ifdef ENABLE_SECURITY_CERT
        if (certpath)
        {
            *ptr++ = "-c";
            *ptr++ = (char *) certpath;
        }
#endif
        *ptr++ = NULL;

        debug_gencred(args);

        execv(BINDIR"/pvfs2-gencred", args);

        _exit(100);
    }
    else if (pid == -1)
    {
        close(filedes[1]);
        close(errordes[1]);
        ret = -PVFS_errno_to_error(errno);
    }
    else
    {
        char buf[sizeof(PVFS_credential)+extra_size_PVFS_credential],
             ebuf[512];
        ssize_t total = 0, etotal = 0;
        ssize_t cnt, ecnt;

        /* close write end so we get EOF when child exits */
        close(errordes[1]);
        close(filedes[1]);

        /* read credential */
        do
        {
            do
            {
                cnt = read(filedes[0], buf+total, (sizeof(buf) - total));
            } while (cnt == -1 && errno == EINTR);
            total += cnt;
        } while (cnt > 0);
        
        if (cnt == -1)
        {
            ret = -PVFS_errno_to_error(errno);
        }
        else
        {
            int rc;

            waitpid(pid, &rc, 0);
            if (WIFEXITED(rc) && !WEXITSTATUS(rc))
            {
                char *ptr = buf;
                PVFS_credential tmp;

                decode_PVFS_credential(&ptr, &tmp);
                ret = PINT_copy_credential(&tmp, cred);

                /* on the stack */
                tmp.issuer = NULL;
                PINT_cleanup_credential(&tmp);
            }
            else if (WIFEXITED(rc))
            {                
                /* error code from pvfs2_gencred */
                ret = -PVFS_errno_to_error(WEXITSTATUS(rc));
            }
            else
            {
                /* catch-all error */
                ret = -PVFS_EINVAL;
            }

            /* read errors and warnings */
            do
            {
                do
                {
                    ecnt = read(errordes[0], ebuf+etotal,
                                (sizeof(ebuf) - etotal));
                } while (ecnt == -1 && errno == EINTR);
                etotal += ecnt;
            } while (ecnt > 0 && etotal < sizeof(ebuf));
            /* null terminate */
            ebuf[(etotal < sizeof(ebuf)) ? etotal : sizeof(ebuf)-1] = '\0';

            /* print errors */
            if (etotal > 0)
            {
                char *line, *saveptr;

                line = strtok_r(ebuf, "\n", &saveptr);
                while (line != NULL && strlen(line) > 0)
                {
                    gossip_err("%s\n", line);
                    line = strtok_r(NULL, "\n", &saveptr);
                }
            }
        }
    }

    close(filedes[0]);
    close(errordes[0]);
    sigaction(SIGCHLD, &oldsa, NULL);

    return ret;
}
#else /* ENABLE_SECURITY_MODE */

/* This macro will call a user info function, e.g. getpwuid_r, and 
   increase the buffer size if needed, to a maximum of 64 x size. */
#define GET_PWD_INFO(func, id, ptr, buf, size, result) \
    do { \
        int retry = 0; \
        unsigned long bsize = size; \
        do { \
            ret = func(id, ptr, buf, bsize, result); \
            if (ret == ERANGE) \
            { \
                free(buf); \
                bsize *= 8; \
                buf = calloc(1, bsize); \
                if (buf == NULL) \
                { \
                    return -PVFS_ENOMEM; \
                } \
            } \
        } while (ret == ERANGE && retry++ < 2); \
        if (ret == ERANGE) \
        { \
            gossip_err("%s error: buffer too small (%lu)\n", #func, \
                        bsize); \
        } \
    } while (0)

/* return 1 if string is a uid/gid number */
static int PINT_is_idnum(const char *str)
{
    char *pstr, *endptr;
    unsigned long id;

    /* NULL or blank values */
    if (str == NULL || *str == '\0')
    {
        return 0;
    }

    for (pstr = (char *) str; *pstr; pstr++)
    {
        if (!isdigit(*pstr))
        {
            return 0;
        }
    }

    /* check whether value is in range */
    errno = 0;
    id = strtoul(str, &endptr, 10);
    if (errno != 0 || id > PVFS_UID_MAX)
    {
        return 0;
    }

    return 1;
}

/* PINT_gen_unsigned_credential 
 *
 * Generate unsigned credential in-process instead of calling pvfs2_gencred. 
 * For use when robust security is disabled.
 */
static int PINT_gen_unsigned_credential(const char *user, const char *group,
                                        unsigned int timeout, PVFS_credential *cred)
{
    uid_t uid;
    gid_t gid;
    unsigned long bufsize;
    char *endptr, hostname[HOST_NAME_MAX];
    struct passwd pwd;
    struct passwd *presult = NULL;
    char *pwdbuf;
    gid_t groups[PVFS_REQ_LIMIT_GROUPS];
    int ngroups, ret, i;

    bufsize = 8192;

    pwdbuf = (char *) malloc(bufsize);
    if (pwdbuf == NULL)
    {
        return -PVFS_ENOMEM;
    }

    /* get info for specified user or calling user */
    if (user != NULL)
    {
        if (PINT_is_idnum(user))
        {
            uid = (uid_t) strtoul(user, &endptr, 10);
            GET_PWD_INFO(getpwuid_r, (uid_t) uid, &pwd, pwdbuf, bufsize, 
                         &presult);
            if (presult == NULL)
            {
               gossip_err("Warning: uid %u lookup error: %d (0 = not "
                          "found)\n", uid, ret);               
            }
        }        
        else
        {
            gossip_err("Error: uid %s is not a number\n", user);
            free(pwdbuf);
            return -PVFS_EINVAL;
        }
    }
    else
    {
        uid = getuid();
        GET_PWD_INFO(getpwuid_r, (uid_t) uid, &pwd, pwdbuf, bufsize, &presult);
        if (presult == NULL)
        {
            gossip_err("Warning: uid %u lookup error: %d (0 = not found)\n",
                       uid, ret);
        }
    }

    /* get info for specified or calling user's group 
       note... group need not necessarily exist in /etc/group */
    if (group != NULL)
    {
        if (PINT_is_idnum(group))
        {
            gid = (gid_t) strtoul(group, &endptr, 10);
        }
        else
        {
            gossip_err("Error: gid %s is not a number\n", group);
            free(pwdbuf);
            return -PVFS_EINVAL;
        }
    }
    else
    {
        gid = getgid();
    }

    if (presult == NULL)
    {
        goto pvfs2_util_default_group;
    }

    /* get user group list */
#ifdef HAVE_GETGROUPLIST

    ngroups = sizeof(groups)/sizeof(*groups);
    ret = getgrouplist(pwd.pw_name, gid, groups, &ngroups);
    if (ret == -1)
    {
        gossip_err("Warning: unable to get group list for user %s - using "
                   "default\n", pwd.pw_name);
        goto pvfs2_util_default_group;
    }
    /* some implementations put specified gid at end of the list */
    if (groups[0] != gid && groups[ngroups-1] == gid)
    {        
        groups[ngroups-1] = groups[0];
        groups[0] = gid;
    }

#else /* !HAVE_GETGROUPLIST */

    ngroups = sizeof(groups)/sizeof(*groups);
    ngroups = getugroups(ngroups, groups, pwd.pw_name, gid);
    if (ngroups == -1)
    {
        gossip_err("Warning: unable to get group list for user %s - using "
                   "default\n", pwd.pw_name);
        goto pvfs2_util_default_group;
    }

#endif /* HAVE_GETGROUPLIST */

    goto pvfs2_util_gen_cred;

pvfs2_util_default_group:
    /* just use specified or current group */
    ngroups = 1;
    groups[0] = gid;

pvfs2_util_gen_cred:

    /* Note: without security enabled any user can generate a 
       credential for another user. */

    /* fill in credential struct */
    cred->userid = (PVFS_uid) uid;
    cred->num_groups = (uint32_t) ngroups;
    cred->group_array = calloc(ngroups, sizeof(PVFS_gid));
    if (cred->group_array == NULL)
    {        
        free(pwdbuf);
        return -PVFS_ENOMEM;
    }
    for (i = 0; i < ngroups; i++)
    {
        cred->group_array[i] = (PVFS_gid) groups[i];
    }

    /* insert an issuer and a null signature */
    cred->issuer = calloc(PVFS_REQ_LIMIT_ISSUER+1, 1);
    if (cred->issuer == NULL)
    {
        free(pwdbuf);
        return -PVFS_ENOMEM;
    }

    /* issuer field for clients is prefixed with "C:" */
    cred->issuer[0] = 'C';
    cred->issuer[1] = ':';
    gethostname(hostname, HOST_NAME_MAX);
    hostname[sizeof(hostname)-1] = '\0';
    strncpy(cred->issuer+2, hostname, PVFS_REQ_LIMIT_ISSUER-2);

    cred->timeout = PINT_util_get_current_time() + 
        (timeout != 0 ? timeout : PVFS2_DEFAULT_CREDENTIAL_TIMEOUT);

    cred->sig_size = 0;
    cred->signature = NULL;

    cred->certificate.buf_size = 0;
    cred->certificate.buf = NULL;

    free(pwdbuf);

    return 0;
}

/*
 * This function generates an unsigned credential for use when
 * robust security is disabled.
 */
int PVFS_util_gen_credential(const char *user, const char *group,
    unsigned int timeout, const char *keypath, const char *certpath,
    PVFS_credential *cred)
{
    if (cred == NULL)
    {
        gossip_lerr("PVFS_util_gen_credential: credential is null\n");
        return -PVFS_EINVAL;
    }

    memset(cred, 0, sizeof(*cred));

    return PINT_gen_unsigned_credential(user, group, timeout, cred);
}
#endif /* ENABLE_SECURITY_MODE */

#define PINT_REFRESH_CREDENTIAL_TIME    3
/*
 * This function checks to see if the credential is still valid
 * and is not about to time out - if so then it does nothing,
 * otherwise it calls PVFS_util_gen_credential_defaults to make a
 * fresh one. Call this before running any system call.
 */
int PVFS_util_refresh_credential(PVFS_credential *cred)
{
    int ret;

    /* check if the credential is about to expire */
    if (PINT_util_get_current_time() <= 
        cred->timeout - PINT_REFRESH_CREDENTIAL_TIME)
    {
        ret = 0;
    }
    else
    {
        PINT_cleanup_credential(cred);
        ret = PVFS_util_gen_credential_defaults(cred);
    }

    return ret;
}

int PVFS_util_get_umask(void)
{
    static int mask = 0, set = 0;

    if (set == 0)
    {
        mask = (int)umask(0);
        umask(mask);
        set = 1;
    }
    return mask;
}

int PVFS_util_copy_sys_attr(
    PVFS_sys_attr *dest_attr, PVFS_sys_attr *src_attr)
{
    int ret = -PVFS_EINVAL;

    if (src_attr && dest_attr)
    {
        dest_attr->owner = src_attr->owner;
        dest_attr->group = src_attr->group;
        dest_attr->perms = src_attr->perms;
        dest_attr->atime = src_attr->atime;
        dest_attr->mtime = src_attr->mtime;
        dest_attr->ctime = src_attr->ctime;
        dest_attr->dfile_count = src_attr->dfile_count;
        dest_attr->distr_dir_servers_initial = src_attr->distr_dir_servers_initial;
        dest_attr->distr_dir_servers_max = src_attr->distr_dir_servers_max;
        dest_attr->distr_dir_split_size = src_attr->distr_dir_split_size;
        dest_attr->objtype = src_attr->objtype;
        dest_attr->mask = src_attr->mask;
        dest_attr->flags = src_attr->flags;

        if (src_attr->mask & PVFS_ATTR_SYS_SIZE)
        {
            dest_attr->size = src_attr->size;
        }

        if((src_attr->mask & PVFS_ATTR_SYS_LNK_TARGET) &&
            src_attr->link_target)
        {
            dest_attr->link_target = strdup(src_attr->link_target);
            if (!dest_attr->link_target)
            {
                ret = -PVFS_ENOMEM;
                return ret;
            }
        }
        else if ((src_attr->mask & PVFS_ATTR_SYS_DIR_HINT))
        {
            if (src_attr->dist_name)
            {
                dest_attr->dist_name = strdup(src_attr->dist_name);
                if (dest_attr->dist_name == NULL)
                {
                    ret = -PVFS_ENOMEM;
                    return ret;
                }
            }
            if (src_attr->dist_params)
            {
                dest_attr->dist_params = strdup(src_attr->dist_params);
                if (dest_attr->dist_params == NULL)
                {
                    free(dest_attr->dist_name);
                    ret = -PVFS_ENOMEM;
                    return ret;
                }
            }
        }
        ret = 0;
    }
    return ret;
}

void PVFS_util_release_sys_attr(PVFS_sys_attr *attr)
{
    if (attr)
    {
        if ((attr->mask & PVFS_ATTR_SYS_TYPE) &&
            (attr->objtype == PVFS_TYPE_SYMLINK) && attr->link_target)
        {
            free(attr->link_target);
            attr->link_target = NULL;
        }
        else if ((attr->mask & PVFS_ATTR_SYS_DIR_HINT) &&
            (attr->objtype == PVFS_TYPE_DIRECTORY))
        {
            if (attr->dist_name)
                free(attr->dist_name);
            if (attr->dist_params)
                free(attr->dist_params);
            attr->dist_name = NULL;
            attr->dist_params = NULL;
        }
    }
}

/* PVFS_util_parse_pvfstab()
 *
 * parses either the file pointed to by the PVFS2TAB_FILE env
 * variable, or /etc/fstab, or /etc/pvfs2tab or ./pvfs2tab to extract
 * pvfs2 mount entries.
 * 
 * NOTE: if tabfile argument is given at runtime to specify which
 * tabfile to use, then that will be the _only_ file searched for
 * pvfs2 entries.
 *
 * example entry:
 * tcp://localhost:3334/pvfs2-fs /mnt/pvfs2 pvfs2 defaults 0 0
 *
 * returns const pointer to internal tab structure on success, NULL on
 * failure
 */
const PVFS_util_tab *PVFS_util_parse_pvfstab(
                                     const char *tabfile)
{
    PINT_fstab_t *mnt_fp = NULL;
    int file_count = 5;
    /* NOTE: mtab should be last for clean error logic below */
    const char *file_list[5] = { NULL,
                                 "/etc/fstab",
                                 "/etc/pvfs2tab",
                                 "pvfs2tab",
                                 "/etc/mtab" };
    const char *targetfile = NULL;
    PINT_fstab_entry_t *tmp_ent;
    int i, j;
    int ret = -1;
    int tmp_mntent_count = 0;
    PVFS_util_tab *current_tab = NULL;
    char *epenv, *tmp;

    if((epenv = getenv("PVFS2EP")) != NULL)
    {
        struct PVFS_sys_mntent *mntent;

        current_tab = &s_stat_tab_array[0];
        current_tab->mntent_array = malloc(sizeof(struct PVFS_sys_mntent));

        mntent = &current_tab->mntent_array[0];
        strcpy(current_tab->tabfile_name, "PVFSEP");
        current_tab->mntent_count = 1;

        mntent->pvfs_config_servers = malloc(sizeof(char *));
        mntent->pvfs_config_servers[0] = strdup(index(epenv, '=') + 1);
        mntent->num_pvfs_config_servers = 1;
        mntent->the_pvfs_config_server = mntent->pvfs_config_servers[0];
        mntent->pvfs_fs_name = strdup(rindex(mntent->the_pvfs_config_server,
                                             '/'));
        mntent->pvfs_fs_name++;
        mntent->flowproto = FLOWPROTO_DEFAULT;
        mntent->encoding = PVFS2_ENCODING_DEFAULT;
        mntent->mnt_dir = strdup(epenv);
        tmp = index(mntent->mnt_dir, '=');
        *tmp = 0;
        mntent->mnt_opts = strdup("rw");
        mntent->fs_id = PVFS_FS_ID_NULL;
        return &s_stat_tab_array[0];
    }

    if (tabfile != NULL)
    {
        /*
         * caller wants us to look in a specific location for the
         * tabfile
         */
        file_list[0] = tabfile;
        file_count = 1;
    }
    else
    {
        /*
         * search the system and env vars for tab files;
         * first check for environment variable override
         */
        file_list[0] = getenv("PVFS2TAB_FILE");
    }

    gen_mutex_lock(&s_stat_tab_mutex);

    /* start by checking list of files we have already parsed */
    for (i = 0; i < s_stat_tab_count; i++)
    {
        for (j = 0; j < file_count; j++)
        {
            if (file_list[j] &&
                !strcmp(file_list[j], s_stat_tab_array[i].tabfile_name))
            {
                /* already done */
                gen_mutex_unlock(&s_stat_tab_mutex);
                return (&s_stat_tab_array[i]);
            }
        }
    }

    assert(s_stat_tab_count < PVFS2_DYNAMIC_TAB_INDEX);

    /* scan our prioritized list of tab files in order, stop when we
     * find one that has at least one pvfs2 entry
     */
    for (i = 0; (i < file_count && !targetfile); i++)
    {
        if(file_list[i])
        {
            PINT_fstab_open(mnt_fp, file_list[i]);
            if (mnt_fp)
            {
                while ((tmp_ent = PINT_fstab_next_entry(mnt_fp)))
                {
                    if(!(PINT_FSTAB_NAME(tmp_ent)) || 
                       !(strncmp(PINT_FSTAB_NAME(tmp_ent), "#", 1)))
                    {
                        PINT_fstab_entry_destroy(tmp_ent);
                        continue;
                    }

                    if (strcmp(PINT_FSTAB_TYPE(tmp_ent), "pvfs2") == 0)
                    {
                        targetfile = file_list[i];
                        tmp_mntent_count++;
                    }

                    PINT_fstab_entry_destroy(tmp_ent);
                }
                PINT_fstab_close(mnt_fp);
            }
        }
    }

    if (!targetfile)
    {
        gossip_err("Error: could not find any pvfs2 tabfile entries.\n");
        gossip_err("Error: tried the following tabfiles:\n");
        for (i = 0; i < file_count; i++)
        {
            gossip_err("       %s\n", file_list[i]);
        }
        gen_mutex_unlock(&s_stat_tab_mutex);
        return (NULL);
    }
    gossip_debug(GOSSIP_CLIENT_DEBUG,
                 "Using pvfs2 tab file: %s\n", targetfile);

    /* allocate array of entries */
    current_tab = &s_stat_tab_array[s_stat_tab_count];

    current_tab->mntent_array = (struct PVFS_sys_mntent *)malloc(
                (tmp_mntent_count * sizeof(struct PVFS_sys_mntent)));

    if (!current_tab->mntent_array)
    {
        gen_mutex_unlock(&s_stat_tab_mutex);
        return (NULL);
    }
    memset(current_tab->mntent_array,
           0,
           (tmp_mntent_count * sizeof(struct PVFS_sys_mntent)));

    for (i = 0; i < tmp_mntent_count; i++)
    {
        current_tab->mntent_array[i].fs_id = PVFS_FS_ID_NULL;
    }
    current_tab->mntent_count = tmp_mntent_count;

    /* reopen our chosen fstab file */
    PINT_fstab_open(mnt_fp, targetfile);

    /* scan through looking for every pvfs2 entry */
    i = 0;
    while ((tmp_ent = PINT_fstab_next_entry(mnt_fp)))
    {
       if(!(PINT_FSTAB_NAME(tmp_ent)) ||
          !(strncmp(PINT_FSTAB_NAME(tmp_ent), "#", 1)))
       {
           PINT_fstab_entry_destroy(tmp_ent);
           continue;
       }

       if ((PINT_FSTAB_TYPE(tmp_ent) != NULL) &&
           (strncmp(PINT_FSTAB_TYPE(tmp_ent), "pvfs2", 5) == 0))
       {
           struct PVFS_sys_mntent *me = &current_tab->mntent_array[i];
           char *cp;
           int cur_server;
	   char *rewrite_pointer;

	   /* Entries in mtab may be prefixed by a process name and '#' */
	   /* If detected, remove prefix.  */
	   for(rewrite_pointer = cp = PINT_FSTAB_NAME(tmp_ent);
               *cp;
               cp++ , rewrite_pointer++)
           {
		if (*cp == '#')
                {
		    rewrite_pointer = PINT_FSTAB_NAME(tmp_ent) - 1;
		    continue;
		}
		if (rewrite_pointer == cp)
                {
                    continue;
                }
		*rewrite_pointer = *cp;
	    }
	    *rewrite_pointer = '\0';
	       
            /* Enable integrity checks by default */
            me->integrity_check = 1;
            /* comma-separated list of ways to contact a config server */
            me->num_pvfs_config_servers = 1;
            for (cp = PINT_FSTAB_NAME(tmp_ent); *cp; cp++)
            {
                if (*cp == ',')
                {
                    ++me->num_pvfs_config_servers;
                }
            }

            /* allocate room for our copies of the strings */
            me->pvfs_config_servers = malloc(me->num_pvfs_config_servers
                                           * sizeof(*me->pvfs_config_servers));
            if (!me->pvfs_config_servers)
            {
                goto error_exit;
            }
            memset(me->pvfs_config_servers,
                   0,
                   me->num_pvfs_config_servers *
                            sizeof(*me->pvfs_config_servers));

            me->mnt_dir = malloc(strlen(PINT_FSTAB_PATH(tmp_ent)) + 1);
            me->mnt_opts = malloc(strlen(PINT_FSTAB_OPTS(tmp_ent)) + 1);

            /* bail if any mallocs failed */
            if (!me->mnt_dir || !me->mnt_opts)
            {
                goto error_exit;
            }

            /* parse server list and make sure fsname is same */
            cp = PINT_FSTAB_NAME(tmp_ent);

            cur_server = 0;
            for (;;)
            {
                char *tok;
                int slashcount;
                char *slash;
                char *last_slash;

                tok = strsep(&cp, ",");
                if (!tok)
                {
                    break;
                }

                slash = tok;
                slashcount = 0;
                while ((slash = index(slash, '/')))
                {
                    slash++;
                    slashcount++;
                }
                if (slashcount != 3)
                {
                    /* if we are looking at the mtab, then just silently
                     * treat this error as if we didn't find an entry at
                     * all; they may have mounted using an odd syntax on a
                     * 2.4 kernel system
                     */
                    if(!strcmp(targetfile, "/etc/mtab"))
                    {
                        gossip_err("Error: could not find any pvfs2 tabfile entries.\n");
                        gossip_err("Error: tried the following tabfiles:\n");
                        for (j = 0; j < file_count; j++)
                        {
                            gossip_err("       %s\n", file_list[j]);
                        }
                        goto error_exit;
                    }
                    else
                    {
                        gossip_err("Error: invalid tab file entry: %s\n",
                                    PINT_FSTAB_NAME(tmp_ent));
                        gossip_err("Error: offending tab file: %s\n",
                                    targetfile);
                        goto error_exit;
                    }
                }

                /* find a reference point in the string */
                /* find the last slash in the string */
                /* should be between server name and fs_name */
                last_slash = rindex(tok, '/');

                /* config server and fs name are a special case, take one 
                 * string and split it in half on "/" delimiter
                 */
                *last_slash = '\0';
                me->pvfs_config_servers[cur_server] = strdup(tok);
                if (!me->pvfs_config_servers[cur_server])
                {
                    goto error_exit;
                }

                ++last_slash;

                /* first time through the loop save the fs_name in me
                 * From then on make sure the fs_name in the entry is
                 * the same as the first one
                 */
                if (cur_server == 0)
                {
                    me->pvfs_fs_name = strdup(last_slash);
                    if (!me->pvfs_fs_name)
                    {
                        goto error_exit;
                    }
                }
                else
                {
                    if (strcmp(last_slash, me->pvfs_fs_name) != 0)
                    {
                        gossip_lerr(
                          "Error: different fs names in server addresses: %s\n",
                          PINT_FSTAB_NAME(tmp_ent));
                        goto error_exit;
                    }
                }
                ++cur_server;
            }

            /* make our own copy of parameters of interest */
            /* mnt_dir and mnt_opts are verbatim copies */
            strcpy(current_tab->mntent_array[i].mnt_dir,
                   PINT_FSTAB_PATH(tmp_ent));

            strcpy(current_tab->mntent_array[i].mnt_opts,
                   PINT_FSTAB_OPTS(tmp_ent));

            /* find out if a particular flow protocol was specified */
            if ((PINT_fstab_entry_hasopt(tmp_ent, "flowproto")))
            {
                ret = parse_flowproto_string(PINT_FSTAB_OPTS(tmp_ent),
                                             &(current_tab->
                                               mntent_array[i].flowproto));
                if (ret < 0)
                {
                    goto error_exit;
                }
            }
            else
            {
                current_tab->mntent_array[i].flowproto =
                                        FLOWPROTO_DEFAULT;
            }

            /* pick an encoding to use with the server */
            current_tab->mntent_array[i].encoding = PVFS2_ENCODING_DEFAULT;
            cp = PINT_fstab_entry_hasopt(tmp_ent, "encoding");
            if (cp)
            {
                ret = parse_encoding_string(
                                    cp,
                                    &current_tab->mntent_array[i].encoding);
                if (ret < 0)
                {
                    goto error_exit;
                }
            }

            /* find out if a particular num dfiles was specified */
            current_tab->mntent_array[i].default_num_dfiles = 0;
            cp = PINT_fstab_entry_hasopt(tmp_ent, "num_dfiles");
            if (cp)
            {
                ret = parse_num_dfiles_string(
                    cp,
                    &(current_tab->mntent_array[i].default_num_dfiles));

                if (ret < 0)
                {
                    goto error_exit;
                }
            }

            /* increment number of pvfs records found */
            i++;

            PINT_fstab_entry_destroy(tmp_ent);
        }
    }
    s_stat_tab_count++;
    strcpy(s_stat_tab_array[s_stat_tab_count - 1].tabfile_name, targetfile);
    PINT_fstab_close(mnt_fp);
    gen_mutex_unlock(&s_stat_tab_mutex);

    return (&s_stat_tab_array[s_stat_tab_count - 1]);

error_exit:
    for (; i > -1; i--)
    {
        struct PVFS_sys_mntent *me = &current_tab->mntent_array[i];

        if (me->pvfs_config_servers)
        {
            int j;
            for (j = 0; j < me->num_pvfs_config_servers; j++)
            {
                if (me->pvfs_config_servers[j])
                {
                    free(me->pvfs_config_servers[j]);
                }
            }
            free(me->pvfs_config_servers);
            me->pvfs_config_servers = NULL;
            me->num_pvfs_config_servers = 0;
        }

        if (me->mnt_dir)
        {
            free(me->mnt_dir);
            me->mnt_dir = NULL;
        }

        if (me->mnt_opts)
        {
            free(me->mnt_opts);
            me->mnt_opts = NULL;
        }

        if (me->pvfs_fs_name)
        {
            free(me->pvfs_fs_name);
            me->pvfs_fs_name = NULL;
        }
    }
    PINT_fstab_close(mnt_fp);
    gen_mutex_unlock(&s_stat_tab_mutex);
    return (NULL);
}

/* PVFS_util_get_default_fsid()
 *
 * fills in the fs identifier for the first active file system that
 * the library knows about.  Useful for test programs or admin tools
 * that need default file system to access if the user has not
 * specified one
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PVFS_util_get_default_fsid(PVFS_fs_id* out_fs_id)
{
    int i = 0, j = 0;

    gen_mutex_lock(&s_stat_tab_mutex);

    for(i = 0; i < s_stat_tab_count; i++)
    {
        for(j = 0; j < s_stat_tab_array[i].mntent_count; j++)
        {
            *out_fs_id = s_stat_tab_array[i].mntent_array[j].fs_id;
            if(*out_fs_id != PVFS_FS_ID_NULL)
            {
                gen_mutex_unlock(&s_stat_tab_mutex);
                return(0);
            }
        }
    }

    /* check the dynamic tab area if we haven't found an fs yet */
    for(j = 0; j < s_stat_tab_array[
            PVFS2_DYNAMIC_TAB_INDEX].mntent_count; j++)
    {
        *out_fs_id = s_stat_tab_array[
            PVFS2_DYNAMIC_TAB_INDEX].mntent_array[j].fs_id;
        if(*out_fs_id != PVFS_FS_ID_NULL)
        {
            gen_mutex_unlock(&s_stat_tab_mutex);
            return(0);
        }
    }

    gen_mutex_unlock(&s_stat_tab_mutex);
    return(-PVFS_ENOENT);
}

/*
 * PVFS_util_add_dynamic_mntent()
 *
 * dynamically add mount information to our internally managed mount
 * tables (used for quick fs resolution using PVFS_util_resolve).
 * dynamic mnt entries can only be added to a particular dynamic
 * region of our book keeping, so they're the exception, not the rule.
 *
 * returns 0 on success, -PVFS_error on failure, and 1 if the mount
 * entry already exists as a parsed entry (not dynamic)
 */
int PVFS_util_add_dynamic_mntent(struct PVFS_sys_mntent *mntent)
{
    int i = 0, j = 0, new_index = 0;
    int ret = -PVFS_EINVAL;
    struct PVFS_sys_mntent *current_mnt = NULL;
    struct PVFS_sys_mntent *tmp_mnt_array = NULL;

    if (mntent)
    {
        gen_mutex_lock(&s_stat_tab_mutex);

        /*
          we exhaustively scan to be sure this mnt entry doesn't exist
          anywhere in our book keeping; first scan the parsed regions
        */
        for(i = 0; i < s_stat_tab_count; i++)
        {
            for(j = 0; j < s_stat_tab_array[i].mntent_count; j++)
            {
                current_mnt = &(s_stat_tab_array[i].mntent_array[j]);

                if (current_mnt->fs_id == mntent->fs_id)
                {
                    /*
                      no need to add the dynamic mount information
                      because the file system already exists as a
                      parsed mount entry
                    */
                    gen_mutex_unlock(&s_stat_tab_mutex);
                    return 1;
                }
            }
        }

#if 0
        /* check the dynamic region if we haven't found a match yet */
        for(j = 0; j < s_stat_tab_array[
                PVFS2_DYNAMIC_TAB_INDEX].mntent_count; j++)
        {
            current_mnt = &(s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].
                            mntent_array[j]);

            if ((current_mnt->fs_id == mntent->fs_id) &&
                (strcmp(current_mnt->pvfs_config_servers[0],
                        mntent->pvfs_config_servers[0]) != 0))
            {
                gossip_err("Error: FS with id %d is already mounted using"
                           " a different config server.\n", (int)mntent->fs_id); 
                gossip_err("Error: This could indicate that a duplicate fsid"
                           " is being used.\n");
                gossip_err("Error: Please check your server configuration.\n");
                gen_mutex_unlock(&s_stat_tab_mutex);
                return -PVFS_ENXIO;
            }
        }
#endif

        /* copy the mntent to our table in the dynamic tab area */
        new_index = s_stat_tab_array[
            PVFS2_DYNAMIC_TAB_INDEX].mntent_count;

        if (new_index == 0)
        {
            /* allocate and initialize the dynamic tab object */
            s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].mntent_array =
                (struct PVFS_sys_mntent *)malloc(
                    sizeof(struct PVFS_sys_mntent));
            if (!s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].mntent_array)
            {
                gen_mutex_unlock(&s_stat_tab_mutex);
                return -PVFS_ENOMEM;
            }
            /* should this be PVFS_PATH_MAX? - WBL */
            /* need to find def of this field (tabfile_name) first */
            strncpy(s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].tabfile_name,
                    PVFS2_DYNAMIC_TAB_NAME, PVFS_NAME_MAX);
        }
        else
        {
            /* we need to re-alloc this guy to add a new array entry */
            tmp_mnt_array = (struct PVFS_sys_mntent *)malloc(
                ((new_index + 1) * sizeof(struct PVFS_sys_mntent)));
            if (!tmp_mnt_array)
            {
                gen_mutex_unlock(&s_stat_tab_mutex);
                return -PVFS_ENOMEM;
            }

            /*
              copy all mntent entries into the new array, freeing the
              original entries
            */
            for(i = 0; i < new_index; i++)
            {
                current_mnt = &s_stat_tab_array[
                    PVFS2_DYNAMIC_TAB_INDEX].mntent_array[i];
                PVFS_util_copy_mntent(&tmp_mnt_array[i], current_mnt);
                PVFS_util_free_mntent(current_mnt);
            }

            /* finally, swap the mntent arrays */
            free(s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].mntent_array);
            s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].mntent_array =
                tmp_mnt_array;
        }

        gossip_debug(GOSSIP_CLIENT_DEBUG, "* Adding new dynamic mount "
                     "point %s [%d,%d]\n", mntent->mnt_dir,
                     PVFS2_DYNAMIC_TAB_INDEX, new_index);

        current_mnt = &s_stat_tab_array[
            PVFS2_DYNAMIC_TAB_INDEX].mntent_array[new_index];

        ret = PVFS_util_copy_mntent(current_mnt, mntent);

        s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].mntent_count++;

        gen_mutex_unlock(&s_stat_tab_mutex);
    }
    return ret;
}

/*
 * PVFS_util_remove_internal_mntent()
 *
 * dynamically remove mount information from our internally managed
 * mount tables.
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PVFS_util_remove_internal_mntent(
    struct PVFS_sys_mntent *mntent)
{
    int i = 0, j = 0, new_count = 0, found = 0, found_index = 0;
    int ret = -PVFS_EINVAL;
    struct PVFS_sys_mntent *current_mnt = NULL;
    struct PVFS_sys_mntent *tmp_mnt_array = NULL;

    if (mntent)
    {
        gen_mutex_lock(&s_stat_tab_mutex);

        /*
          we exhaustively scan to be sure this mnt entry *does* exist
          somewhere in our book keeping
        */
        for(i = 0; i < s_stat_tab_count; i++)
        {
            for(j = 0; j < s_stat_tab_array[i].mntent_count; j++)
            {
                current_mnt = &(s_stat_tab_array[i].mntent_array[j]);
                if ((current_mnt->fs_id == mntent->fs_id)
                    && (strcmp(current_mnt->mnt_dir, mntent->mnt_dir) == 0))
                {
                    found_index = i;
                    found = 1;
                    goto mntent_found;
                }
            }
        }

        /* check the dynamic region if we haven't found a match yet */
        for(j = 0; j < s_stat_tab_array[
                PVFS2_DYNAMIC_TAB_INDEX].mntent_count; j++)
        {
            current_mnt = &(s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].
                            mntent_array[j]);

            if (current_mnt->fs_id == mntent->fs_id)
            {
                found_index = PVFS2_DYNAMIC_TAB_INDEX;
                found = 1;
                goto mntent_found;
            }
        }

      mntent_found:
        if (!found)
        {
            gen_mutex_unlock(&s_stat_tab_mutex);
            return -PVFS_EINVAL;
        }

        gossip_debug(GOSSIP_CLIENT_DEBUG, "* Removing mount "
                     "point %s [%d,%d]\n", current_mnt->mnt_dir,
                     found_index, j);

        /* remove the mntent from our table in the found tab area */
        if ((s_stat_tab_array[found_index].mntent_count - 1) > 0)
        {
            /*
              this is 1 minus the old count since there will be 1 less
              mnt entries after this call
            */
            new_count = s_stat_tab_array[found_index].mntent_count - 1;

            /* we need to re-alloc this guy to remove the array entry */
            tmp_mnt_array = (struct PVFS_sys_mntent *)malloc(
                (new_count * sizeof(struct PVFS_sys_mntent)));
            if (!tmp_mnt_array)
            {
                gen_mutex_unlock(&s_stat_tab_mutex);
                return -PVFS_ENOMEM;
            }

            /*
              copy all mntent entries into the new array, freeing the
              original entries -- and skipping the one that we're
              trying to remove
            */
            for(i = 0, new_count = 0;
                i < s_stat_tab_array[found_index].mntent_count; i++)
            {
                current_mnt = &s_stat_tab_array[found_index].mntent_array[i];

                if ((current_mnt->fs_id == mntent->fs_id)
                    && (strcmp(current_mnt->mnt_dir, mntent->mnt_dir) == 0))
                {
                    PVFS_util_free_mntent(current_mnt);
                    continue;
                }
                PVFS_util_copy_mntent(
                            &tmp_mnt_array[new_count++], current_mnt);
                PVFS_util_free_mntent(current_mnt);
            }

            /* finally, swap the mntent arrays */
            free(s_stat_tab_array[found_index].mntent_array);
            s_stat_tab_array[found_index].mntent_array = tmp_mnt_array;

            s_stat_tab_array[found_index].mntent_count--;
            ret = 0;
        }
        else
        {
            /*
              special case: we're removing the last mnt entry in the
              array here.  since this is the case, we also free the
              array since we know it's now empty.
            */
            PVFS_util_free_mntent(
                        &s_stat_tab_array[found_index].mntent_array[0]);
            free(s_stat_tab_array[found_index].mntent_array);
            s_stat_tab_array[found_index].mntent_array = NULL;
            s_stat_tab_array[found_index].mntent_count = 0;
            ret = 0;
        }
        gen_mutex_unlock(&s_stat_tab_mutex);
    }
    return ret;
}

/*
 * PVFS_util_get_mntent_copy()
 *
 * Given a pointer to a valid mount entry, out_mntent, copy the contents of
 * the mount entry  for fs_id into out_mntent.
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PVFS_util_get_mntent_copy(PVFS_fs_id fs_id,
                              struct PVFS_sys_mntent *out_mntent)
{
    int i = 0;

    /* Search for mntent by fsid */
    gen_mutex_lock(&s_stat_tab_mutex);
    for(i = 0; i < s_stat_tab_count; i++)
    {
        int j;
        for(j = 0; j < s_stat_tab_array[i].mntent_count; j++)
        {
            struct PVFS_sys_mntent* mnt_iter;
            mnt_iter = &(s_stat_tab_array[i].mntent_array[j]);

            if (mnt_iter->fs_id == fs_id)
            {
                PVFS_util_copy_mntent(out_mntent, mnt_iter);
                gen_mutex_unlock(&s_stat_tab_mutex);
                return 0;
            }
        }
    }

    /* check the dynamic region if we haven't found a match yet */
    for (i = 0; i < s_stat_tab_array[
             PVFS2_DYNAMIC_TAB_INDEX].mntent_count; i++)
    {
        struct PVFS_sys_mntent *mnt_iter;
        mnt_iter = &(s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].
                     mntent_array[i]);

        if (mnt_iter->fs_id == fs_id)
        {
            PVFS_util_copy_mntent(out_mntent, mnt_iter);
            gen_mutex_unlock(&s_stat_tab_mutex);
            return 0;
        }
    }

    gen_mutex_unlock(&s_stat_tab_mutex);
    return -PVFS_EINVAL;
}

/* PVFS_util_resolve()
 *
 * This function is quasi-obsolete.  It should still work and for now
 * it is called by the pvfs2-xxx utils.  Most everything else calls
 * PVFS_util_resolve_absolute directly and uses the new PVFS_path
 * structure to keep up with paths and their modifications/expansions
 *
 * given a local path of a file that resides on a pvfs2 volume,
 * determine what the fsid and fs relative path is.  
 *
 * returns 0 on succees, -PVFS_error on failure
 */
int PVFS_util_resolve(
    const char* local_path,
    PVFS_fs_id* out_fs_id,
    char* out_fs_path,
    int out_fs_path_max)
{
    int ret = -1;
    char* tmp_path = NULL;
    char* parent_path = NULL;
    int base_len = 0;
    PVFS_path_t *Ppath;
    int ppath_local = 0;

    if(strlen(local_path) > (PVFS_PATH_MAX-1))
    {
        gossip_err("Error: PVFS_util_resolve() input path too long.\n");
        return(-PVFS_ENAMETOOLONG);
    }

    Ppath = PVFS_path_from_expanded((char *)local_path);
    if (!VALID_PATH_MAGIC(Ppath))
    {
        /* this allocates a PVFS_path that must be freed before return */
        Ppath = PVFS_new_path(local_path);
        local_path = Ppath->expanded_path;
        ppath_local = 1; /* this Ppath created locally */
    }

    if (!(PATH_QUALIFIED(Ppath) || PATH_EXPANDED(Ppath)))
    {
        /* this saves a copy of the original path and returns */
        /* a pointer to the modified path */
        /* this will not allocated PVFS_path) */
        local_path = PVFS_qualify_path(local_path);
    }

    /* the most common case first; just try to resolve the path that we
     * were given
     */
    ret = PVFS_util_resolve_absolute(local_path);
    if(ret == 0)
    {
        /* done */
        strncpy(out_fs_path, Ppath->pvfs_path, out_fs_path_max);
        *out_fs_id = Ppath->fs_id;
        if (ppath_local)
        {
            PVFS_free_path(Ppath);
        }
        return(0);
    }
    if(ret == -PVFS_ENOENT)
    {
        /* if the path wasn't found, try canonicalizing the path in case it
         * refers to a relative path on a mounted volume or contains symlinks
         */
        tmp_path = (char*)malloc(PVFS_PATH_MAX * sizeof(char));
        if(!tmp_path)
        {
            return(-PVFS_ENOMEM);
        }
        memset(tmp_path, 0, PVFS_PATH_MAX * sizeof(char));
        ret = PINT_realpath(local_path, tmp_path, (PVFS_PATH_MAX - 1));
        if(ret == -PVFS_EINVAL)
        {
            /* one more try; canonicalize the parent in case this function
             * is called before object creation; the basename
             * doesn't yet exist but we still need to find the PVFS volume
             */
            parent_path = (char *)malloc(PVFS_PATH_MAX * sizeof(char));
            if(!parent_path)
            {
                free(tmp_path);
                return(-PVFS_ENOMEM);
            }
            /* find size of basename so we can reserve space for it */
            /* note: basename() and dirname() modify args, thus the strcpy */
            strcpy(parent_path, local_path);
            base_len = strlen(basename(parent_path));
            strcpy(parent_path, local_path);
            ret = PINT_realpath(dirname(parent_path), tmp_path,
                               (PVFS_PATH_MAX - base_len - 2));
            if(ret < 0)
            {
                free(tmp_path);
                free(parent_path);
                /* last chance failed; this is not a valid pvfs2 path */
                return(-PVFS_ENOENT);
            }
            /* glue the basename back on */
            strcpy(parent_path, local_path);
            strcat(tmp_path, "/");
            strcat(tmp_path, basename(parent_path));
            free(parent_path);
        }
        else if(ret < 0)
        {
            /* first canonicalize failed; this is not a valid pvfs2 path */
            free(tmp_path);
            return(-PVFS_ENOENT);
        }

        ret = PVFS_util_resolve_absolute(tmp_path);
        if (!ret)
        {
            /* done */
            strncpy(out_fs_path, Ppath->pvfs_path, out_fs_path_max);
            *out_fs_id = Ppath->fs_id;
            if (ppath_local)
            {
                PVFS_free_path(Ppath);
            }
        }
        free(tmp_path);

        /* fall through and preserve "ret" to be returned */
    }

    return(ret);
}


/* PVFS_util_init_defaults()
 *
 * performs the standard set of initialization steps for the system
 * interface, mostly just a wrapper function
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PVFS_util_init_defaults(void)
{
    int ret = -1, i = 0, j = 0, found_one = 0;
    int failed_indices[PVFS2_MAX_INVALID_MNTENTS] = {0};

    /* first set up our malloc */
    init_glibc_malloc();

    /* use standard system tab files */
    const PVFS_util_tab* tab = PVFS_util_parse_pvfstab(NULL);
    if (!tab)
    {
        gossip_err(
            "Error: failed to find any pvfs2 file systems in the "
            "standard system tab files.\n");
        return(-PVFS_ENOENT);
    }

    /* initialize pvfs system interface */
    ret = PVFS_sys_initialize(GOSSIP_NO_DEBUG);
    if (ret < 0)
    {
        return(ret);
    }

    /* add in any file systems we found in the fstab */
    for(i = 0; i < tab->mntent_count; i++)
    {
        ret = PVFS_sys_fs_add(&tab->mntent_array[i]);
        if (ret == 0)
        {
            found_one = 1;
        }
        else
        {
            if (ret == -PVFS_EEXIST)
            {
                /* this mount already exists so count it as found */
                found_one = 1;
                continue;
            }
            failed_indices[j++] = i;

            if (j > (PVFS2_MAX_INVALID_MNTENTS - 1))
            {
                gossip_err("*** Failed to initialize %d file systems "
                           "from tab file %s.\n ** If this is a valid "
                           "tabfile, please remove invalid entries.\n",
                           PVFS2_MAX_INVALID_MNTENTS,
                           tab->tabfile_name);
                gossip_err("Continuing execution without remaining "
                           "mount entries\n");
                
                break;
            }
        }
    }

    /* remove any mount entries that couldn't be added here */
    for(i = 0; i < PVFS2_MAX_INVALID_MNTENTS; i++)
    {
        if (failed_indices[i])
        {
            PVFS_util_remove_internal_mntent(
                          &tab->mntent_array[failed_indices[i]]);
        }
        else
        {
            break;
        }
    }

    if (found_one)
    {
        return 0;
    }

    gossip_err("ERROR: could not initialize any file systems "
               "in %s.\n", tab->tabfile_name);

    PVFS_sys_finalize();
    return -PVFS_ENODEV;
}

/*********************/
/* normal size units */
/*********************/
#define KILOBYTE                1024
#define MEGABYTE   (1024 * KILOBYTE)
#define GIGABYTE   (1024 * MEGABYTE)
#define TERABYTE   (1024llu * GIGABYTE)
#define PETABYTE   (1024llu * TERABYTE)
#define EXABYTE    (1024llu * PETABYTE)
#define ZETTABYTE  (1024llu * EXABYTE)
#define YOTTABYTE  (1024llu * ZETTABYTE)

/*****************/
/* si size units */
/*****************/
#define SI_KILOBYTE                   1000
#define SI_MEGABYTE   (1000 * SI_KILOBYTE)
#define SI_GIGABYTE   (1000 * SI_MEGABYTE)
#define SI_TERABYTE  (1000llu * SI_GIGABYTE)
#define SI_PETABYTE  (1000llu * SI_TERABYTE)
#define SI_EXABYTE   (1000llu * SI_PETABYTE)
#define SI_ZETTABYTE (1000llu * SI_EXABYTE)
#define SI_YOTTABYTE (1000llu * SI_ZETTABYTE)

#if SIZEOF_LONG_INT == 8
#define NUM_SIZES                  5
#else
#define NUM_SIZES                  4
#endif

static PVFS_size PINT_s_size_table[NUM_SIZES] =
{
    /*YOTTABYTE, ZETTABYTE, EXABYTE, */
#if SIZEOF_LONG_INT == 8
    PETABYTE,
    TERABYTE,
#endif
    GIGABYTE, MEGABYTE, KILOBYTE
};

static PVFS_size PINT_s_si_size_table[NUM_SIZES] =
{
    /*SI_YOTTABYTE, SI_ZETTABYTE, SI_EXABYTE, */
#if SIZEOF_LONG_INT == 8
    SI_PETABYTE, SI_TERABYTE,
#endif
    SI_GIGABYTE, SI_MEGABYTE, SI_KILOBYTE
};

static const char *PINT_s_str_size_table[NUM_SIZES] =
{
    /*"Y", "Z", "E", */
#if SIZEOF_LONG_INT == 8
    "P","T",
#endif
    "G", "M", "K"
};

/*
 * PVFS_util_make_size_human_readable
 *
 * converts a size value to a human readable string format
 *
 * size         - numeric size of file
 * out_str      - nicely formatted string, like "3.4M"
 *                  (caller must allocate this string)
 * max_out_len  - maximum lenght of out_str
 * use_si_units - use units of 1000, not 1024
 */
void PVFS_util_make_size_human_readable(
    PVFS_size size,
    char *out_str,
    int max_out_len,
    int use_si_units)
{
    int i = 0;
    double tmp = 0.0f;
    PVFS_size *size_table =
        (use_si_units? PINT_s_si_size_table : PINT_s_size_table);

    if (out_str)
    {
        for (i = 0; i < NUM_SIZES; i++)
        {
            tmp = (double)size;
            if ((PVFS_size) (tmp / size_table[i]) > 0)
            {
                tmp = (tmp / size_table[i]);
                break;
            }
        }
        if (i == NUM_SIZES)
        {
            snprintf(out_str, 16, "%lld", lld(size));
        }
        else
        {
            snprintf(out_str, max_out_len, "%.1f%s",
                     tmp, PINT_s_str_size_table[i]);
        }
    }
}

/* parse_flowproto_string()
 *
 * looks in the mount options string for a flowprotocol specifier and 
 * sets the flowproto type accordingly
 *
 * returns 0 on success, -PVFS_error on failure
 */
static int parse_flowproto_string(
    const char *input,
    enum PVFS_flowproto_type *flowproto)
{
    int ret = 0;
    char *start = NULL;
    char flow[256];
    char *comma = NULL;

    start = strstr(input, "flowproto");
    /* we must find a match if this function is being called... */
    assert(start);

    /* scan out the option */
    ret = sscanf(start, "flowproto = %255s ,", flow);
    if (ret != 1)
    {
        gossip_err("Error: malformed flowproto option in tab file.\n");
        return (-PVFS_EINVAL);
    }

    /* chop it off at any trailing comma */
    comma = index(flow, ',');
    if (comma)
    {
        comma[0] = '\0';
    }

    if (!strcmp(flow, "dump_offsets"))
    {
        *flowproto = FLOWPROTO_DUMP_OFFSETS;
    }
    else if (!strcmp(flow, "bmi_cache"))
    {
        *flowproto = FLOWPROTO_BMI_CACHE;
    }
    else if (!strcmp(flow, "multiqueue"))
    {
        *flowproto = FLOWPROTO_MULTIQUEUE;
    }
    else
    {
        gossip_err("Error: unrecognized flowproto option: %s\n", flow);
        return (-PVFS_EINVAL);
    }
    return 0;
}

void PVFS_util_free_mntent(
    struct PVFS_sys_mntent *mntent)
{
    if (mntent)
    {
        if (mntent->pvfs_config_servers)
        {
            int j;
            for (j=0; j<mntent->num_pvfs_config_servers; j++)
            {            
                if (mntent->pvfs_config_servers[j])
                {
                    if (mntent->pvfs_config_servers[j] == 
                        mntent->the_pvfs_config_server)
                    {
                        /* don't free further down */
                        mntent->the_pvfs_config_server = NULL;
                    }
                    free(mntent->pvfs_config_servers[j]);
                }
            }
            free(mntent->pvfs_config_servers);
            mntent->pvfs_config_servers = NULL;
            mntent->num_pvfs_config_servers = 0;
        }
        if (mntent->pvfs_fs_name)
        {
            free(mntent->pvfs_fs_name);
            mntent->pvfs_fs_name = NULL;
        }
        if (mntent->mnt_dir)
        {
            free(mntent->mnt_dir);
            mntent->mnt_dir = NULL;
        }
        if (mntent->mnt_opts)
        {
            free(mntent->mnt_opts);
            mntent->mnt_opts = NULL;
        }
        if (mntent->the_pvfs_config_server)           
        {
            free(mntent->the_pvfs_config_server);
            mntent->the_pvfs_config_server = NULL;
        }

        mntent->flowproto = 0;
        mntent->encoding = 0;
        mntent->fs_id = PVFS_FS_ID_NULL;
    }    
}

int PVFS_util_copy_mntent(
    struct PVFS_sys_mntent *dest_mntent,
    struct PVFS_sys_mntent *src_mntent)
{
    int ret = -PVFS_EINVAL, i = 0;

    if (dest_mntent && src_mntent)
    {
        memset(dest_mntent, 0, sizeof(struct PVFS_sys_mntent));

        dest_mntent->num_pvfs_config_servers =
            src_mntent->num_pvfs_config_servers;

        dest_mntent->pvfs_config_servers =
            malloc(dest_mntent->num_pvfs_config_servers *
                   sizeof(*dest_mntent->pvfs_config_servers));
        if (!dest_mntent)
        {
            return -PVFS_ENOMEM;
        }

        memset(dest_mntent->pvfs_config_servers, 0,
               dest_mntent->num_pvfs_config_servers *
               sizeof(*dest_mntent->pvfs_config_servers));

        for(i = 0; i < dest_mntent->num_pvfs_config_servers; i++)
        {
            dest_mntent->pvfs_config_servers[i] =
                strdup(src_mntent->pvfs_config_servers[i]);
            if (!dest_mntent->pvfs_config_servers[i])
            {
                ret = -PVFS_ENOMEM;
                goto error_exit;
            }
        }

        dest_mntent->the_pvfs_config_server = 
            strdup(src_mntent->the_pvfs_config_server);
        if (!dest_mntent->the_pvfs_config_server)
        {
            ret = -PVFS_ENOMEM;
            goto error_exit;
        }

        dest_mntent->pvfs_fs_name = strdup(src_mntent->pvfs_fs_name);
        if (!dest_mntent->pvfs_fs_name)
        {
            ret = -PVFS_ENOMEM;
            goto error_exit;
        }

        if (src_mntent->mnt_dir)
        {
            dest_mntent->mnt_dir = strdup(src_mntent->mnt_dir);
            if (!dest_mntent->mnt_dir)
            {
                ret = -PVFS_ENOMEM;
                goto error_exit;
            }
        }

        if (src_mntent->mnt_opts)
        {
            dest_mntent->mnt_opts = strdup(src_mntent->mnt_opts);
            if (!dest_mntent->mnt_opts)
            {
                ret = -PVFS_ENOMEM;
                goto error_exit;
            }
        }

        dest_mntent->flowproto = src_mntent->flowproto;
        dest_mntent->encoding = src_mntent->encoding;
        dest_mntent->fs_id = src_mntent->fs_id;
        dest_mntent->default_num_dfiles = src_mntent->default_num_dfiles;
    }
    return 0;

  error_exit:

    for(i = 0; i < dest_mntent->num_pvfs_config_servers; i++)
    {
        if (dest_mntent->pvfs_config_servers[i])
        {
            free(dest_mntent->pvfs_config_servers[i]);
            dest_mntent->pvfs_config_servers[i] = NULL;
        }
    }

    if (dest_mntent->pvfs_config_servers)
    {
        free(dest_mntent->pvfs_config_servers);
        dest_mntent->pvfs_config_servers = NULL;
    }

    if (dest_mntent->pvfs_fs_name)
    {
        free(dest_mntent->pvfs_fs_name);
        dest_mntent->pvfs_fs_name = NULL;
    }

    if (dest_mntent->mnt_dir)
    {
        free(dest_mntent->mnt_dir);
        dest_mntent->mnt_dir = NULL;
    }

    if (dest_mntent->mnt_opts)
    {
        free(dest_mntent->mnt_opts);
        dest_mntent->mnt_opts = NULL;
    }
    return ret;
}

/*
 * Pull out the wire encoding specified as a mount option in the tab
 * file.
 *
 * Input string is not modified; result goes into et.
 *
 * Returns 0 if all okay.
 */
static int parse_encoding_string(
    const char *cp,
    enum PVFS_encoding_type *et)
{
    int i = 0;
    const char *cq = NULL;

    struct
    {
        const char *name;
        enum PVFS_encoding_type val;
    } enc_str[] =
        { { "default", PVFS2_ENCODING_DEFAULT },
          { "defaults", PVFS2_ENCODING_DEFAULT },
          { "direct", ENCODING_DIRECT },
          { "le_bfield", ENCODING_LE_BFIELD },
          { "xdr", ENCODING_XDR } };

    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: input is %s\n",
                 __func__, cp);
    cp += strlen("encoding");
    for (; isspace(*cp); cp++);        /* optional spaces */
    if (*cp != '=')
    {
        gossip_err("Error: %s: malformed encoding option in tab file.\n",
                   __func__);
        return -PVFS_EINVAL;
    }
    for (++cp; isspace(*cp); cp++);        /* optional spaces */
    for (cq = cp; *cq && *cq != ','; cq++);/* find option end */

    for (i = 0; i < sizeof(enc_str) / sizeof(enc_str[0]); i++)
    {
        int n = strlen(enc_str[i].name);
        if (cq - cp > n)
            n = cq - cp;
        if (!strncmp(enc_str[i].name, cp, n))
        {
            *et = enc_str[i].val;
            return 0;
        }
    }
    gossip_err("Error: %s: unknown encoding type in tab file.\n",
            __func__);
    return -PVFS_EINVAL;
}

/* PINT_release_pvfstab()
 *
 * frees up any resources associated with previously parsed tabfiles
 *
 * no return value
 */
void PINT_release_pvfstab(void)
{
    int i, j;

    gen_mutex_lock(&s_stat_tab_mutex);
    for(i = 0; i < s_stat_tab_count; i++)
    {
        for (j = 0; j < s_stat_tab_array[i].mntent_count; j++)
        {
            if (s_stat_tab_array[i].mntent_array[j].fs_id !=
                PVFS_FS_ID_NULL)
            {
                PVFS_util_free_mntent(
                    &s_stat_tab_array[i].mntent_array[j]);
            }
        }
        free(s_stat_tab_array[i].mntent_array);
    }
    s_stat_tab_count = 0;

    for (j = 0; j < s_stat_tab_array[
             PVFS2_DYNAMIC_TAB_INDEX].mntent_count; j++)
    {
        if (s_stat_tab_array[
                PVFS2_DYNAMIC_TAB_INDEX].mntent_array[j].fs_id !=
            PVFS_FS_ID_NULL)
        {
            PVFS_util_free_mntent(
                &s_stat_tab_array[
                    PVFS2_DYNAMIC_TAB_INDEX].mntent_array[j]);
        }
    }
    if (s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].mntent_array)
    {
        free(s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].mntent_array);
    }

    gen_mutex_unlock(&s_stat_tab_mutex);
}

uint32_t PVFS_util_sys_to_object_attr_mask(
    uint32_t sys_attrmask)
{

    /*
      adjust parameters as necessary; what's happening here
      is that we're converting sys_attr masks to obj_attr masks
      before passing the getattr request to the server.
    */
    uint32_t attrmask = 0;
    if (sys_attrmask & PVFS_ATTR_SYS_SIZE)
    {
        /* need datafile handles and distribution in order to get 
         * datafile handles and know what function to call to get
         * the file size.
         */
        attrmask |= (PVFS_ATTR_META_ALL | PVFS_ATTR_DATA_SIZE);
    }

    if (sys_attrmask & PVFS_ATTR_SYS_DFILE_COUNT)
    {
        attrmask |= (PVFS_ATTR_META_DFILES | PVFS_ATTR_META_MIRROR_DFILES);
    }
    if (sys_attrmask & PVFS_ATTR_SYS_MIRROR_COPIES_COUNT)
    {
        attrmask |= PVFS_ATTR_META_MIRROR_DFILES;
    }

    if (sys_attrmask & PVFS_ATTR_SYS_DIRENT_COUNT)
    {
        attrmask |= PVFS_ATTR_DIR_DIRENT_COUNT;
    }

    if (sys_attrmask & PVFS_ATTR_SYS_DIR_HINT)
    {
        attrmask |= PVFS_ATTR_DIR_HINT;
    }

    if (sys_attrmask & PVFS_ATTR_SYS_DISTDIR_ATTR)
    {
        attrmask |= PVFS_ATTR_DISTDIR_ATTR;
    }

    if (sys_attrmask & PVFS_ATTR_SYS_LNK_TARGET)
    {
        attrmask |= PVFS_ATTR_SYMLNK_TARGET;
    }

    /* we need the distribution in order to calculate block size */
    if (sys_attrmask & PVFS_ATTR_SYS_BLKSIZE)
    {
        attrmask |= PVFS_ATTR_META_DIST;
    }

    if (sys_attrmask & PVFS_ATTR_SYS_CAPABILITY)
    {
        attrmask |= PVFS_ATTR_CAPABILITY;
    }

    if(sys_attrmask & PVFS_ATTR_SYS_UID)
        attrmask |= PVFS_ATTR_COMMON_UID;
    if(sys_attrmask & PVFS_ATTR_SYS_GID)
        attrmask |= PVFS_ATTR_COMMON_GID;
    if(sys_attrmask & PVFS_ATTR_SYS_PERM)
        attrmask |= PVFS_ATTR_COMMON_PERM;
    if(sys_attrmask & PVFS_ATTR_SYS_ATIME)
        attrmask |= PVFS_ATTR_COMMON_ATIME;
    if(sys_attrmask & PVFS_ATTR_SYS_CTIME)
        attrmask |= PVFS_ATTR_COMMON_CTIME;
    if(sys_attrmask & PVFS_ATTR_SYS_MTIME)
        attrmask |= PVFS_ATTR_COMMON_MTIME;
    if(sys_attrmask & PVFS_ATTR_SYS_TYPE)
        attrmask |= PVFS_ATTR_COMMON_TYPE;
    if(sys_attrmask & PVFS_ATTR_SYS_ATIME_SET)
        attrmask |= PVFS_ATTR_COMMON_ATIME_SET;
    if(sys_attrmask & PVFS_ATTR_SYS_MTIME_SET)
        attrmask |= PVFS_ATTR_COMMON_MTIME_SET;

    gossip_debug(GOSSIP_GETATTR_DEBUG,
                 "attrmask being passed to server: \n");
    PINT_attrmask_print(GOSSIP_GETATTR_DEBUG, attrmask);

    return attrmask;
}

uint32_t PVFS_util_object_to_sys_attr_mask( 
    uint32_t obj_mask)
{
    int sys_mask = 0;

    if (obj_mask & PVFS_ATTR_COMMON_UID)
    {
        sys_mask |= PVFS_ATTR_SYS_UID;
    }
    if (obj_mask & PVFS_ATTR_COMMON_GID)
    {
        sys_mask |= PVFS_ATTR_SYS_GID;
    }
    if (obj_mask & PVFS_ATTR_COMMON_PERM)
    {
        sys_mask |= PVFS_ATTR_SYS_PERM;
    }
    if (obj_mask & PVFS_ATTR_COMMON_ATIME)
    {
        sys_mask |= PVFS_ATTR_SYS_ATIME;
    }
    if (obj_mask & PVFS_ATTR_COMMON_CTIME)
    {
        sys_mask |= PVFS_ATTR_SYS_CTIME;
    }
    if (obj_mask & PVFS_ATTR_COMMON_MTIME)
    {
        sys_mask |= PVFS_ATTR_SYS_MTIME;
    }
    if (obj_mask & PVFS_ATTR_COMMON_TYPE)
    {
        sys_mask |= PVFS_ATTR_SYS_TYPE;
    }
    if (obj_mask & PVFS_ATTR_DATA_SIZE)
    {
        sys_mask |= PVFS_ATTR_SYS_SIZE;
    }
    if (obj_mask & PVFS_ATTR_SYMLNK_TARGET)
    {
        sys_mask |= PVFS_ATTR_SYS_LNK_TARGET;
    }
    if (obj_mask & PVFS_ATTR_DIR_DIRENT_COUNT)
    {
        sys_mask |= PVFS_ATTR_SYS_DIRENT_COUNT;
    }
    if (obj_mask & PVFS_ATTR_META_DFILES)
    {
        sys_mask |= PVFS_ATTR_SYS_DFILE_COUNT;
    }
    if (obj_mask & PVFS_ATTR_META_MIRROR_DFILES)
    {
        sys_mask |= PVFS_ATTR_SYS_MIRROR_COPIES_COUNT;
    }
    if (obj_mask & PVFS_ATTR_META_DIST)
    {
        sys_mask |= PVFS_ATTR_SYS_BLKSIZE;
    }
    if (obj_mask & PVFS_ATTR_DIR_HINT)
    {
        sys_mask |= PVFS_ATTR_SYS_DIR_HINT;
    }
    if (obj_mask & PVFS_ATTR_CAPABILITY)
    {
        sys_mask |= PVFS_ATTR_SYS_CAPABILITY;
    }
    if (obj_mask & PVFS_ATTR_DISTDIR_ATTR)
    {
        sys_mask |= PVFS_ATTR_SYS_DISTDIR_ATTR;
    }

    /* NOTE: the PVFS_ATTR_META_UNSTUFFED is intentionally not exposed
     * outside of the system interface
     */
    return sys_mask;
}

/*
 * Pull out the wire encoding specified as a mount option in the tab
 * file.
 *
 * Input string is not modified; result goes into et.
 *
 * Returns 0 if all okay.
 */
static int parse_num_dfiles_string(const char* cp, int* num_dfiles)
{
    int parsed_value = 0;
    char* end_ptr = NULL;

    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: input is %s\n",
                 __func__, cp);
    
    cp += strlen("num_dfiles");

    /* Skip optional spacing */
    for (; isspace(*cp); cp++);
    
    if (*cp != '=')
    {
        gossip_err("Error: %s: malformed num_dfiles option in tab file.\n",
                   __func__);
        return -PVFS_EINVAL;
    }
    
    /* Skip optional spacing */
    for (++cp; isspace(*cp); cp++);

    parsed_value = strtol(cp, &end_ptr, 10);

    /* If a numerica value was found, continue
       else, report an error */
    if (end_ptr != cp)
    {
        *num_dfiles = parsed_value;
    }
    else
    {
        gossip_err("Error: %s: malformed num_dfiles option in tab file.\n",
                   __func__);
        return -PVFS_EINVAL;
    }

    return 0;
}

/* PVFS_util_resolve_absolute()
 *
 * given a local path of a file that may reside on a pvfs2 volume,
 * determine what the fsid and fs relative path is. Makes no attempt
 * to canonicalize the path.
 *
 * result is returned through the PVFS_path type now
 *
 * returns 0 on succees, -PVFS_error on failure
 */
int PVFS_util_resolve_absolute(const char* local_path)
{
    int i = 0, j = 0;
    int ret = -PVFS_EINVAL;
    PVFS_path_t *Ppath;

    gen_mutex_lock(&s_stat_tab_mutex);

    Ppath = PVFS_path_from_expanded((char *)local_path);
    if (!VALID_PATH_MAGIC(Ppath))
    {
        gen_mutex_unlock(&s_stat_tab_mutex);
        return ret;
    }

    if (!(PATH_QUALIFIED(Ppath) || PATH_EXPANDED(Ppath)))
    {
        gossip_err("Error: PVFS_util_resolve_absolute only"
                   " works on qualified or expanded paths\n");
        gen_mutex_unlock(&s_stat_tab_mutex);
        return ret;
    }

    CLEAR_RESOLVED(Ppath);

    for(i=0; i < s_stat_tab_count; i++)
    {
        for(j = 0; j < s_stat_tab_array[i].mntent_count; j++)
        {
            ret = PINT_remove_dir_prefix(
                        local_path, 
                        s_stat_tab_array[i].mntent_array[j].mnt_dir);
            if(ret == 0)
            {
                Ppath->fs_id = s_stat_tab_array[i].mntent_array[j].fs_id;
                if(Ppath->fs_id == PVFS_FS_ID_NULL)
                {
                    gossip_err("Error: %s resides on a PVFS2 file system "
                    "that has not yet been initialized.\n", local_path);

                    gen_mutex_unlock(&s_stat_tab_mutex);
                    return(-PVFS_ENXIO);
                }
                SET_RESOLVED(Ppath);
                gen_mutex_unlock(&s_stat_tab_mutex);
                return(0);
            }
        }
    }

    /* check the dynamic tab area if we haven't resolved anything yet */
    for(j = 0; j < s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].mntent_count; j++)
    {
        ret = PINT_remove_dir_prefix(
                    local_path, s_stat_tab_array[
                    PVFS2_DYNAMIC_TAB_INDEX].mntent_array[j].mnt_dir);
        if (ret == 0)
        {
            Ppath->fs_id = s_stat_tab_array[
                        PVFS2_DYNAMIC_TAB_INDEX].mntent_array[j].fs_id;
            if(Ppath->fs_id == PVFS_FS_ID_NULL)
            {
                gossip_err("Error: %s resides on a PVFS2 file system "
                           "that has not yet been initialized.\n",
                           local_path);

                gen_mutex_unlock(&s_stat_tab_mutex);
                return(-PVFS_ENXIO);
            }
            SET_RESOLVED(Ppath);
            gen_mutex_unlock(&s_stat_tab_mutex);
            return(0);
        }
    }

    gen_mutex_unlock(&s_stat_tab_mutex);
    return(-PVFS_ENOENT);
}

#ifdef DEFINE_MY_GET_NEXT_FSENT

static struct fstab *PINT_util_my_get_next_fsent(PINT_fstab_t *tab)
{
    char linestr[PVFS_MY_GET_NEXT_FSENT_MAX_LINESIZE];
    int linelen = 0;
    char * strtok_ctx;
    char * nexttok; 
    PINT_fstab_entry_t * fsentry;
    if(!fgets(linestr, PVFS_MY_GET_NEXT_FSENT_MAX_LINESIZE, tab))
    {
        return NULL;
    }

    fsentry = malloc(sizeof(PINT_fstab_entry_t));
    if (!fsentry)
    {
        return NULL;
    }
    memset(fsentry, 0, sizeof(PINT_fstab_entry_t));

    linelen = strlen(linestr);
    if (linestr[linelen - 1] == '\n')
    {
        linestr[linelen - 1] = 0;
    }

    /* get the path string */
    nexttok = strtok_r(linestr, " \t", &strtok_ctx);
    if(!nexttok)
    {
        goto exit;
    }
    fsentry->fs_spec = strdup(nexttok);

    
    /* get the mount point */

    nexttok = strtok_r(NULL, " \t", &strtok_ctx);
    if(!nexttok)
    {
        goto exit;
    }
    fsentry->fs_file = strdup(nexttok);

    /* get the fs type */
    nexttok = strtok_r(NULL, " \t", &strtok_ctx);
    if(!nexttok)
    {
        goto exit;
    }
    fsentry->fs_vfstype = strdup(nexttok);

    /* get the mount opts */
    nexttok = strtok_r(NULL, " \t", &strtok_ctx);
    if(!nexttok)
    {
        goto exit;
    }
    fsentry->fs_mntops = strdup(nexttok);

 exit:
    return fsentry;
}

static void PINT_util_fsent_destroy(PINT_fstab_entry_t * entry)
{
    if(entry)
    {
        if(entry->fs_spec)
        {
            free(entry->fs_spec);
        }

        if(entry->fs_file)
        {
            free(entry->fs_file);
        }
        
        if(entry->fs_vfstype)
        {
            free(entry->fs_vfstype);
        }

        if(entry->fs_mntops)
        {
            free(entry->fs_mntops);
        }

        if(entry->fs_type)
        {
            free(entry->fs_type);
        }
    
        free(entry);
    }
}
#endif /* DEFINE_MY_GET_NEXT_FSENT */

int32_t PVFS_util_translate_mode(int mode, int suid)
{
    int ret = 0, i = 0;
#define NUM_MODES 11
    static int modes[NUM_MODES] =
    {
        S_IXOTH, S_IWOTH, S_IROTH,
        S_IXGRP, S_IWGRP, S_IRGRP,
        S_IXUSR, S_IWUSR, S_IRUSR,
        S_ISGID, S_ISUID
    };
    static int pvfs2_modes[NUM_MODES] =
    {
        PVFS_O_EXECUTE, PVFS_O_WRITE, PVFS_O_READ,
        PVFS_G_EXECUTE, PVFS_G_WRITE, PVFS_G_READ,
        PVFS_U_EXECUTE, PVFS_U_WRITE, PVFS_U_READ,
        PVFS_G_SGID,    PVFS_U_SUID
    };

    for(i = 0; i < NUM_MODES; i++)
    {
        if (mode & modes[i])
        {
            ret |= pvfs2_modes[i];
        }
    }
    if (suid == 0 && (ret & PVFS_U_SUID))
    {
         ret &= ~PVFS_U_SUID;
    }
    return ret;
#undef NUM_MODES
}

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
