/*
 * Copyright 2010-2014 Clemson University and The University of Chicago.
 *
 * See COPYING in top-level directory.
 *
 * pvfs2-gencred returns a credential object to a calling program by writing 
 * it in binary form to stdout, which cannot be a tty. Normally the credential
 * is signed by the client private key (if in key-based security mode) or user
 * private key (if in certificate-based security mode). However, an unsigned
 * credential will be returned if an error occurs. Unsigned credentials can be
 * used only for operations that require no permissions (like statfs).
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <ctype.h>

/* FIXME: obtaining HOST_NAME_MAX is platform specific and should be handled more generally */
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 64
#endif

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#define __PINT_REQPROTO_ENCODE_FUNCS_C

#include "pvfs2-config.h"

/* avoid using PINT_malloc etc. */
#define PVFS_MALLOC_REDEF_OVERRIDE
#include "pvfs2-types.h"

#undef PVFS_MALLOC_REDEF
#include "src/proto/pvfs2-req-proto.h"

#include "pvfs2-util.h"

#include "src/common/security/getugroups.h"

typedef struct {
    const char *user;
    const char *group;
    int timeout;
    const char *keypath;
#ifdef ENABLE_SECURITY_CERT
    const char *certpath;
#endif
} options_t;

char cert_keypath[PATH_MAX];

#define PVFS2_GENCRED_MSG   "pvfs2-gencred: "

/* print error msg and jump to specified label
   if err is nonzero */
#define CHECK_ERROR(err, label, fmt, f...) \
    do { \
        if ((err)) \
        { \
            fprintf(stderr, PVFS2_GENCRED_MSG fmt, ##f); \
            goto label; \
        } \
    } while (0)

/* print error msg and jump to specified label
   if cond is false */
#define CHECK_ERROR_BOOL(cond, label, fmt, f...) \
    do { \
        if (!(cond)) \
        { \
            fprintf(stderr, PVFS2_GENCRED_MSG fmt, ##f); \
            goto label; \
        } \
    } while (0)

/* print error msg and jump to specified label
   if ptr is NULL */
#define CHECK_NULL(ptr, label, fmt, f...) \
    do { \
        if ((ptr) == NULL) \
        { \
            fprintf(stderr, PVFS2_GENCRED_MSG fmt, ##f); \
            goto label; \
        } \
    } while (0)

/* print error msg and jump to specified label
   if fileptr is NULL */
#define CHECK_FILE(fileptr, err, label, filepath) \
    do { \
        char errmsg[512]; \
        if ((fileptr) == NULL) \
        { \
            fprintf(stderr, PVFS2_GENCRED_MSG "%s: %s\n", \
                    filepath, strerror_r(err, errmsg, sizeof(errmsg))); \
            goto label; \
        } \
    } while (0)

/* print warning msg if ptr is NULL */
#define WARNING_NULL(ptr, fmt, f...) \
    do { \
        if ((ptr) == NULL) \
        { \
            fprintf(stderr, PVFS2_GENCRED_MSG fmt, ##f); \
        } \
    } while (0)

/* print warning msg */
#define WARNING(fmt, f...)      WARNING_NULL(NULL, fmt, ##f)

#define FATAL(label, fmt, f...) CHECK_NULL(NULL, label, fmt, ##f)

/* return 1 if string is a uid/gid number */
int is_idnum(const char *str)
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

static void usage(void)
{
#ifdef ENABLE_SECURITY_CERT
    puts("usage: pvfs2-gencred [-u uid] [-g gid] [-t timeout] "
         "[-k keyfile] [-c certfile]");
#else
    puts("usage: pvfs2-gencred [-u uid] [-g gid] [-t timeout] "
         "[-k keyfile]");
#endif
}

static int safe_write(int fd,
                      const void *buf,
                      size_t nbyte)
{
    const char *cbuf = (const char *)buf;
    ssize_t total;
    ssize_t cnt;

    for (total = 0; total < nbyte; total += cnt)
    {
        do cnt = write(fd, cbuf+total, (nbyte - total));
        while (cnt == -1 && errno == EINTR);
        if (cnt == -1)
        {
            return errno;
        }
    }

    return 0;
}

static int parse_options(int argc,
                         char **argv,
                         options_t *opts)
{
    int ch;
#ifdef ENABLE_SECURITY_CERT
    const char optstr[] = "?u:g:t:k:c:";
#else
    const char optstr[] = "?u:g:t:k:";
#endif
    
    while ((ch = getopt(argc, argv, optstr)) != -1)
    {
        switch(ch)
        {
            case 'u':
                opts->user = optarg;
                break;
            case 'g':
                opts->group = optarg;
                break;
            case 't':
                opts->timeout = strtol(optarg, NULL, 10);
                if (opts->timeout <= 0)
                {
                    fprintf(stderr, PVFS2_GENCRED_MSG "%s: illegal timeout "
                            "(%s)\n", argv[0], optarg);
                    usage();
                    return EINVAL;
                }
                break;
            case 'k':
                opts->keypath = optarg;
                break;
#ifdef ENABLE_SECURITY_CERT
            case 'c':
                opts->certpath = optarg;
                break;
#endif
            case '?':
            default:
                usage();
                exit(1);
        }
    }
    
    return EXIT_SUCCESS;
}

#ifdef ENABLE_SECURITY_CERT
static char *get_certificate_keypath(const struct passwd *pwd)
{
    const char def_keyfile[] = "/.pvfs2-cert-key.pem";

    if (pwd != NULL)
    {
        /* construct certificate private key file path */
        strncpy(cert_keypath, pwd->pw_dir, PATH_MAX);
        cert_keypath[PATH_MAX-1] = '\0';
        CHECK_ERROR_BOOL(
            ((strlen(cert_keypath) + strlen(def_keyfile)) < (PATH_MAX - 1)),
            get_keypath_error, "warning: key path too long\n");
        strcat(cert_keypath, def_keyfile);
        goto get_keypath_exit;
    }

get_keypath_error:
    return NULL;

get_keypath_exit:
    return cert_keypath;
}
#endif

static int create_credential(const struct passwd *pwd,
                             const gid_t *groups,
                             int ngroups,
#ifdef ENABLE_SECURITY_CERT
                             const char *certpath,
#endif
                             PVFS_credential *cred)
{
    char hostname[HOST_NAME_MAX+1];
    char *issuer;
    int ret;
#ifdef ENABLE_SECURITY_CERT
    const char def_certfile[] = "/.pvfs2-cert.pem";
    char def_certpath[PATH_MAX];
    int err;
    FILE *f = NULL;
    X509 *cert = NULL;
    BIO *bio_mem = NULL;
    char *cert_buf;    
#else
    int i;
#endif

    memset(cred, 0, sizeof(*cred));

    issuer = calloc(PVFS_REQ_LIMIT_ISSUER+1, 1);
    CHECK_NULL(issuer, create_error, "error: out of memory\n");

    /* issuer field for clients is prefixed with "C:" */
    issuer[0] = 'C';
    issuer[1] = ':';
    gethostname(hostname, HOST_NAME_MAX);
    hostname[sizeof(hostname)-1] = '\0';
    strncpy(issuer+2, hostname, PVFS_REQ_LIMIT_ISSUER-2);

#ifdef ENABLE_SECURITY_CERT
    /* in cert mode, the uid/gids must be determined by
       server mapping */
    cred->userid = PVFS_UID_MAX;
    cred->num_groups = 1;
    cred->group_array = calloc(1, sizeof(PVFS_gid));
    CHECK_NULL(cred->group_array, create_error, "error: out of memory\n");
    cred->group_array[0] = PVFS_GID_MAX;
#else
    if (pwd != NULL)
    {
        cred->userid = (PVFS_uid) pwd->pw_uid;
    }
    else
    {
        cred->userid = (PVFS_uid) getuid();
    }
    cred->num_groups = (uint32_t)ngroups;
    cred->group_array = calloc(ngroups, sizeof(PVFS_gid));
    CHECK_NULL(cred->group_array, create_error, "error: out of memory\n");
    for (i = 0; i < ngroups; i++)
    {
        cred->group_array[i] = (PVFS_gid)groups[i];
    }
#endif /* ENABLE_SECURITY_CERT */

    cred->issuer = issuer;

#ifdef ENABLE_SECURITY_CERT
    /* use certpath or look for cert in home dir */
    if (certpath == NULL)
    {
        if (pwd != NULL)
        {
            strncpy(def_certpath, pwd->pw_dir, PATH_MAX);
            def_certpath[PATH_MAX-1] = '\0';
            CHECK_ERROR_BOOL(
                (strlen(def_certpath) + strlen(def_certfile)) < (PATH_MAX - 1),
                create_error, "error: path to certificate too long\n");
            
            strncat(def_certpath, def_certfile, sizeof(def_certfile));

            certpath = def_certpath;
        }
    }

    /* open certificate path */
    CHECK_NULL(certpath, create_error, "warning: no cert. file specified\n");

    f = fopen(certpath, "r");
    err = errno;
    CHECK_FILE(f, err, create_error, certpath);

    /* read certificate */
    cert = PEM_read_X509(f, NULL, NULL, NULL);

    fclose(f);

    /* check cert */
    CHECK_NULL(cert, create_error, "security error:\n");

    /* write cert to memory */
    bio_mem = BIO_new(BIO_s_mem());
    CHECK_NULL(bio_mem, create_error, "security error:\n");

    /* write cert to mem BIO */
    CHECK_ERROR_BOOL((i2d_X509_bio(bio_mem, cert) > 0), create_error,
                     "security error:\n");

    /* get cert data */
    cred->certificate.buf_size = BIO_get_mem_data(bio_mem, &cert_buf);
    CHECK_ERROR_BOOL((cert_buf != NULL && cred->certificate.buf_size > 0), 
                     create_error, "security error:\n");

    /* copy cert data */
    cred->certificate.buf = (PVFS_cert_data) malloc(cred->certificate.buf_size);
    CHECK_NULL(cred->certificate.buf, create_error, "error: out of memory\n");

    memcpy(cred->certificate.buf, cert_buf, cred->certificate.buf_size);

    ret = EXIT_SUCCESS;

    goto create_exit;

create_error:
    ERR_print_errors_fp(stderr);

    /* set a blank certificate */
    if (cred->certificate.buf != NULL)
    {
        free(cred->certificate.buf);
        cred->certificate.buf = NULL;
    }

    cred->certificate.buf_size = 0;

    ret = EINVAL;

create_exit:
    if (bio_mem != NULL)
    {
        BIO_free(bio_mem);
    }
    if (cert != NULL)
    {
        X509_free(cert);
    }
#else  /* !ENABLE_SECURITY_CERT */
    ret = EXIT_SUCCESS;
    goto create_exit;

create_error: 
    /* currently only error caught for label */
    ret = ENOMEM;  

create_exit:
    cred->certificate.buf_size = 0;
    cred->certificate.buf = NULL;
#endif

    return ret;
}

static int sign_credential(PVFS_credential *cred,
                           time_t timeout,
                           const char *keypath)
{
    FILE *keyfile = NULL;
    int keyfile_open = 0;
    struct stat stats;
    EVP_PKEY *privkey = NULL;
    const EVP_MD *md = NULL;
    EVP_MD_CTX mdctx = {0}, emptyctx = {0};
    int err, ret;

    /* set timeout */
    cred->timeout = (PVFS_time)(time(NULL) + timeout);

    keyfile = fopen(keypath, "rb");    
    err = errno;
    CHECK_FILE(keyfile, err, sign_error, keypath);

    keyfile_open = 1;
    ret = fstat(fileno(keyfile), &stats);
    CHECK_ERROR_BOOL((ret != -1), sign_error, "error: could not stat key file "
                     " (%s)\n", keypath);

    if (stats.st_mode & (S_IROTH | S_IWOTH))
    {
        fprintf(stderr, "warning: insecure permissions on key file (%s)\n",
                keypath);
    }

    privkey = PEM_read_PrivateKey(keyfile, NULL, NULL, NULL);

    fclose(keyfile);
    keyfile_open = 0;

    CHECK_NULL(privkey, sign_error, "security error:\n");

    cred->sig_size = EVP_PKEY_size(privkey);
    cred->signature = malloc(cred->sig_size);
    CHECK_NULL(cred->signature, sign_error, "error: out of memory\n");

    md = EVP_sha1();
    EVP_MD_CTX_init(&mdctx);

    /* sign credential; an unsigned credential will result 
       if errors occur */

    ret = EVP_SignInit_ex(&mdctx, md, NULL);
    ret &= EVP_SignUpdate(&mdctx, &cred->userid, sizeof(PVFS_uid));
    ret &= EVP_SignUpdate(&mdctx, &cred->num_groups, sizeof(uint32_t));
    if (cred->num_groups)
    {
        ret &= EVP_SignUpdate(&mdctx, cred->group_array, 
            cred->num_groups * sizeof(PVFS_gid));
    }
    if (cred->issuer)
    {
        ret &= EVP_SignUpdate(&mdctx, cred->issuer, 
            strlen(cred->issuer) * sizeof(char));
    }
    ret &= EVP_SignUpdate(&mdctx, &cred->timeout, sizeof(PVFS_time));
    CHECK_ERROR_BOOL(ret, sign_error, "security_error:\n");

    ret = EVP_SignFinal(&mdctx, cred->signature, &cred->sig_size, privkey);
    CHECK_ERROR_BOOL(ret, sign_error, "security_error:\n");

    ret = EXIT_SUCCESS;

    goto sign_exit;

sign_error:

    ERR_print_errors_fp(stderr);

    if (keyfile_open)
    {
        fclose(keyfile);
    }

    /* remove signature */
    if (cred->signature != NULL)
    {
        free(cred->signature);
        cred->signature = NULL;
    }
    cred->sig_size = 0;

sign_exit: 

    if (memcmp(&mdctx, &emptyctx, sizeof(mdctx)) != 0)
    {
        EVP_MD_CTX_cleanup(&mdctx);
    }

    if (privkey != NULL)
    {
        EVP_PKEY_free(privkey);
    }

    return ret;
}

static int write_credential(const PVFS_credential *cred)
{
    char buf[sizeof(PVFS_credential)+extra_size_PVFS_credential] = { 0 };
    char *pptr = buf;
    int ret;

    CHECK_ERROR_BOOL(!isatty(STDOUT_FILENO), write_error, "error: stdout is a tty\n");

    encode_PVFS_credential(&pptr, cred);
    ret = safe_write(STDOUT_FILENO, buf, sizeof(buf));
    CHECK_ERROR(ret, write_error, "error: cannot write credential\n");

    goto write_exit;

write_error:
    ret = EIO;

write_exit:
    
    return ret;
}

/* return 0 if current uid is allowed to generate a credential for 
   the specified uid; return 1 if not */
int allowed(PVFS_uid curr_uid, PVFS_gid curr_gid, 
            PVFS_uid cred_uid, PVFS_gid cred_gid)
{
    char *filepath;
    struct stat st;
    FILE *f;
    size_t len, bufsize = 8192;
    char user[128], *service_data_buf;
    struct passwd service_pwd, *result;
    /* get user information */
    int retry = 0, ret = 0;

    /* root can generate a credential for anyone */
    if (curr_uid == 0 && curr_gid == 0)
    {
        return 0;
    }

    /* anyone can generate a credential for themselves */
    if (curr_uid == cred_uid && curr_gid == cred_gid)
    {
        return 0;
    }

    /* service file path */
    filepath = getenv("PVFS2_SERVICEFILE");
    if (filepath == NULL)
    {
        filepath = PVFS2_DEFAULT_CREDENTIAL_SERVICE_USERS;
    }

    /* Parse users out of the service user file if root owns it. */
    if (stat(filepath, &st) == 0)
    {
        CHECK_ERROR_BOOL((st.st_uid == 0 && st.st_gid == 0), allowed_exit,
            "service file %s must be owned by root:root\n", filepath);
    }
    else
    {
        CHECK_ERROR_BOOL(errno == ENOENT, allowed_exit, "service file %s "
                         "error\n", filepath);

        return 1;
    }

    f = fopen(filepath, "r");
    CHECK_FILE(f, errno, allowed_exit, filepath);

    /* read users, one per line, out of the service users file */
    while (!feof(f))
    {
        fgets(user, sizeof(user), f);
        len = strlen(user);
        /* remove trailing CR */
        if (len > 0 && user[len-1] == '\n')
        {
            user[--len] = '\0';
        }
        /* blank line */
        if (len == 0)
        {
            continue;
        }

        /* Call getpwnam_r to avoid trouble with
           previous call to getpwnam. Increase buffer up
           to 512K for adequate size. */
        bufsize = 8192;
        retry = 0;
        do 
        {
            ret = getpwnam_r(user, &service_pwd, service_data_buf,
                             bufsize, &result);
            if (ret == ERANGE)
            {
                free(service_data_buf);
                bufsize *= 8;
                service_data_buf = (char *) malloc(bufsize);
                CHECK_NULL(service_data_buf, allowed_exit, "out of memory\n");
            }
        } while (ret == ERANGE && retry++ < 2);

        if (result != NULL)
        {
            if (service_pwd.pw_uid == curr_uid)
            {
                fclose(f);
                free(service_data_buf);
                return 0;
            }
        }
        else
        {
            if (ret == 0)
            {
                fprintf(stderr, PVFS2_GENCRED_MSG "warning: service user %s "
                    "not found\n", user);
            }
            else
            {
                fprintf(stderr, PVFS2_GENCRED_MSG "warning: service user %s "
                    "error (%d)\n", user, ret);
            }            
        }
    }

    free(service_data_buf);

    fclose(f);

allowed_exit:

    return 1;
}

int main(int argc, char **argv)
{
    options_t opts;
    /* set up uid/gid info */
    PVFS_uid curr_uid = getuid(), cred_uid = PVFS_UID_MAX;
    PVFS_gid curr_gid = getgid(), cred_gid = PVFS_GID_MAX;
    const struct passwd *pwd = NULL;    
    gid_t groups[PVFS_REQ_LIMIT_GROUPS];
    int ngroups, sign_flag = 0;
    PVFS_credential credential;
    int ret = EXIT_SUCCESS;

#if HAVE_GETGROUPLIST
#   ifdef HAVE_GETGROUPLIST_INT
       int groups_int[PVFS_REQ_LIMIT_GROUPS];
       int i;
#   endif
#endif

    memset(&opts, 0, sizeof(opts));
    
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();

    ret = parse_options(argc, argv, &opts);
    CHECK_ERROR(ret, main_fatal, "fatal: could not parse arguments\n");

    /* we will retrieve the information for the user 
       if not found, they will still receive an unsigned 
       credential for use with non-secure operations like
       statfs */

    /* check for numeric uid on the command line */
    if (opts.user)
    {
        if (is_idnum(opts.user))
        {
            char *endptr;

            cred_uid = (PVFS_uid) strtoul(opts.user, &endptr, 10);
            pwd = getpwuid((uid_t) cred_uid);
        }
        else
        {
            FATAL(main_fatal, "fatal: uid %s is not a number\n", opts.user);
        }      
    }
    else
    {
        /* get info for current user */
        cred_uid = curr_uid;
        pwd = getpwuid(cred_uid);

        WARNING_NULL(pwd, "warning: current user %d not found\n", curr_uid);
    }

    /* check for numeric gid on the cmd line */
    if (opts.group)
    {        
        if (is_idnum(opts.group))
        {
            char *endptr;

            cred_gid = (PVFS_gid) strtoul(opts.group, &endptr, 10);
        }
        else
        {
            FATAL(main_fatal, "gid %s is not a number\n", opts.group);
        }
    }
    else
    {
        /* get info for current user */
        cred_gid = curr_gid;
    }

    /* sanity check */
    CHECK_ERROR_BOOL(cred_uid != PVFS_UID_MAX && cred_gid != PVFS_GID_MAX,
                     main_fatal, "fatal: could not determine credential "
                     "uid/gid\n");

    /* check whether current user is allowed to generate credential 
       for specified user */
    CHECK_ERROR((allowed(curr_uid, curr_gid, cred_uid, cred_gid)), main_error,
                "warning: no permission to generate credential for user "
                "%d:%d\n", cred_uid, cred_gid);

    /* if we couldn't find user, skip group list step; a signed cred is 
       still allowed */
    if (pwd == NULL)
    {        
        goto main_default_group;
    }

#ifdef HAVE_GETGROUPLIST

    ngroups = sizeof(groups) / sizeof(*groups);

#ifdef HAVE_GETGROUPLIST_INT
    /* The returned list of groups in groups_int is a list of signed integers; 
     * however, gid_t is defined as an unsigned 32-bit integer. This checks 
     * whether any returned groups are negative. */
    ret = getgrouplist(pwd->pw_name, cred_gid, groups_int, &ngroups);
    CHECK_ERROR_BOOL((ret != -1), main_default_group, "warning: unable to get "
                     "group list for user %s\n", pwd->pw_name);

    for (i=0; i<ngroups; i++)
    {
        CHECK_ERROR_BOOL((groups_int[i] >= 0), main_default_group, "warning: "
            "unable to convert group value (%d) for user %s\n", groups_int[i],
            pwd->pw_name);
        groups[i] = (gid_t) groups_int[i];
    } 
    
#else
    ret = getgrouplist(pwd->pw_name, cred_gid, groups, &ngroups);
    CHECK_ERROR_BOOL((ret != -1), main_default_group, "warning: unable to "
        "get group list for user %s\n", pwd->pw_name);
#endif
    /* primary gid may be in first or last position */
    if (groups[0] != cred_gid)
    {
        CHECK_ERROR_BOOL((groups[ngroups-1] != cred_gid),
            main_default_group, "warning: cannot determine primary group\n");

        groups[ngroups-1] = groups[0];
        groups[0] = cred_gid;
    }
#else /* !HAVE_GETGROUPLIST */

    ngroups = sizeof(groups) / sizeof(*groups);
    ngroups = getugroups(ngroups, groups, pwd->pw_name, cred_gid);
    CHECK_ERROR_BOOL((ngroups != -1), main_default_group, "warning: unable to "
        "get group list for user %s: %s\n", pwd->pw_name, strerror(errno));

#endif /* HAVE_GETGROUPLIST */

    /* everything checked out OK, so we will sign the credential */
    sign_flag = 1;

    goto main_create;

/* if we jump to main_default_group or main_error, we'll use a group array
   with just the primary group. if permission is valid, the credential will
   still be signed. */
main_default_group:
    sign_flag = 1;

main_error:
    ngroups = 1;
    groups[0] = getgid();

main_create:

    /* note: pwd is NULL if we're going unsigned */
    ret = create_credential(pwd, 
                            groups, 
                            ngroups, 
#ifdef ENABLE_SECURITY_CERT
                            opts.certpath,
#endif
                            &credential);

    /* something went wrong during creation--go unsigned */
    if (ret != EXIT_SUCCESS)
    {
        sign_flag = 0;
    }

    if (sign_flag)
    {

#ifdef ENABLE_SECURITY_CERT
        if (opts.keypath == NULL)
        {
            opts.keypath = get_certificate_keypath(pwd);
        }        
#endif
        /* sign the credential */
        ret = sign_credential(&credential,
                              (opts.timeout ? (time_t) opts.timeout :
                                  PVFS2_DEFAULT_CREDENTIAL_TIMEOUT),
                              (opts.keypath ? opts.keypath : 
                                  PVFS2_DEFAULT_CREDENTIAL_KEYPATH));

        /* this will mean we got an unsigned cred back--we'll log a warning */
        if (ret != EXIT_SUCCESS)
        {
            sign_flag = 0;
        }
    }

    /* log our unsigned warning */
    if (!sign_flag)
    {
        WARNING("warning: using unsigned credential; secure "
                "operations will fail\n");
    }

    /* write the credential to stdout; function will log errors */
    ret = write_credential(&credential);

    free(credential.issuer);
    free(credential.group_array);
#ifdef ENABLE_SECURITY_CERT
    free(credential.certificate.buf);
#endif

    goto main_exit;

main_fatal: 
    ret = EINVAL;

main_exit:

    ERR_free_strings();
    EVP_cleanup();

    return ret;
}

