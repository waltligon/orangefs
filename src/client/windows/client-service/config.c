/*
 * (C) 2010-2022 Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 */
   
/*
 * Configuration file functions 
 */

#include <Windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <openssl/err.h>
#include <openssl/pem.h>

#include "pvfs2-types.h"
#include "security-util.h"

#include "client-service.h"
#include "config.h"
#include "user-cache.h"
#include "cred.h"

extern struct qhash_table user_cache;

QLIST_HEAD(user_list);

#define LDAP_SCOPE_ONELEVEL    0x01
#define LDAP_SCOPE_SUBTREE     0x02

#define EAT_WS(str)    while (*str && (*str == ' ' || \
                              *str == '\t')) \
                           str++

#define MAX_ARGS    8

#define KEYWORD_ERR_UNEXPECTED    -1
#define KEYWORD_ERR_INVALID_ARGS  -2

#define ERROR_MSG_LEN            255

/* keyword callbacks */
#define KEYWORD_CB(__name)    int keyword_cb_##__name(PORANGEFS_OPTIONS options, \
                                                      const char *keyword, \
                                                      char **args, \
                                                      char *error_msg)

static KEYWORD_CB(mount);
static KEYWORD_CB(threads);
static KEYWORD_CB(user_mode);
static KEYWORD_CB(user);
static KEYWORD_CB(perms);
static KEYWORD_CB(write_time);
static KEYWORD_CB(debug);
static KEYWORD_CB(security_mode);
static KEYWORD_CB(key_file);
static KEYWORD_CB(security_timeout);
static KEYWORD_CB(cert_security);
static KEYWORD_CB(ldap);

/* keyword processing callback definitions */
CONFIG_KEYWORD_DEF config_keyword_defs[] = 
{
    { "mount", 1, 1, keyword_cb_mount },
    { "threads", 1, 1, keyword_cb_threads },
    { "user-mode", 1, 1, keyword_cb_user_mode },
    { "user", 2, 2, keyword_cb_user },
    { "new-file-perms", 1, 1, keyword_cb_perms },
    { "new-dir-perms", 1, 1, keyword_cb_perms },
    { "disable-update-write-time", 0, 0, keyword_cb_write_time },
    { "debug", 0, 1, keyword_cb_debug },
    { "debug-stderr", 0, 0, keyword_cb_debug },
    { "debug-file", 1, 1, keyword_cb_debug },
    { "security-mode", 1, 1, keyword_cb_security_mode },
    { "key-file", 1, 1, keyword_cb_key_file },
    { "security-timeout", 1, 1, keyword_cb_security_timeout },
    { "cert-mode", 1, 1, keyword_cb_cert_security },
    { "ca-file", 1, 1, keyword_cb_cert_security },
    { "ca-path", 1, 1, keyword_cb_cert_security },
    { "cert-dir-prefix", 1, 1, keyword_cb_cert_security },
    { "cert-file", 1, 1, keyword_cb_cert_security },
    { "ldap-host", 1, 1, keyword_cb_ldap },
    { "ldap-bind-dn", 1, 1, keyword_cb_ldap },
    { "ldap-bind-password", 1, 1, keyword_cb_ldap },
    { "ldap-search-root", 1, 1, keyword_cb_ldap },
    { "ldap-search-scope", 1, 1, keyword_cb_ldap },
    { "ldap-search-class", 1, 1, keyword_cb_ldap },
    { "ldap-naming-attr", 1, 1, keyword_cb_ldap },
    { "ldap-uid-attr", 1, 1, keyword_cb_ldap },
    { "ldap-gid-attr", 1, 1, keyword_cb_ldap },
    { NULL, 0, 0, NULL }
};

/* get the directory where exe resides
   module_dir should be MAX_PATH */
static DWORD get_module_dir(char *module_dir)
{
    char *p;

    /* get exe path */
    if (!GetModuleFileName(NULL, module_dir, MAX_PATH))
        return GetLastError();

    /* remove exe file name */
    p = strrchr(module_dir, '\\');  
    if (p)
        *p = '\0';
  
    return 0;
}

static FILE *open_config_file(char *error_msg, 
                              unsigned int error_msg_len)
{
    FILE *f = NULL;
    char *file_name = NULL, module_dir[MAX_PATH];
    DWORD ret = 0, malloc_flag = FALSE;

    /* environment variable overrides */
    file_name = getenv("ORANGEFS_CONFIG_FILE");
    if (file_name == NULL)
        file_name = getenv("PVFS2_CONFIG_FILE");
    if (file_name == NULL)
    {
        /* look for file in exe directory */
        ret = get_module_dir(module_dir);
        if (ret == 0)
        {
            if ((file_name = (char*)malloc(MAX_PATH)) == NULL)
            {
                _snprintf(error_msg, error_msg_len, "Fatal: open_config_file: out of memory\n");
                return NULL;
            }
            malloc_flag = TRUE;

            ZeroMemory(file_name, MAX_PATH);
            _snprintf(file_name, MAX_PATH, "%s\\orangefs.cfg", module_dir);
            file_name[MAX_PATH-1] = '\0';
        }
        else
        {
            _snprintf(error_msg, error_msg_len, "Fatal: GetModuleFileName failed: %u\n", ret);
            return NULL;
        }
    }

    /* open config file */
    f = fopen(file_name, "r");
    if (f == NULL)
        _snprintf(error_msg, error_msg_len, "Fatal: could not open configuration file %s\n", 
            file_name == NULL ? "(null)" : file_name);

    if (malloc_flag)
        free(file_name);

    return f;
}

static void close_config_file(FILE *f)
{
    fclose(f);
}

/* Return null-terminated array of keyword arguments.
   Note: strings aren't freed on error as the program will exit. */
static char** get_args(char* args, char *keyword, int min_args, int max_args, char* error_msg)
{
    int i = 0, alloc_flag = 0, c = 0, count = 0,
        truncated = 0;
    char quote_flag = 0;
    char** out_args;
    char* p, *cur_arg = NULL;

    /* NOTE: up to MAX_ARGS args are read to detect config file errors */

    /* allocate MAX_ARGS array + 1 for null ptr */
    out_args = (char**)malloc(sizeof(char*) * (MAX_ARGS + 1));
    if (!out_args) {
        _snprintf(error_msg, ERROR_MSG_LEN, "out of memory");
        return NULL;
    }

    ZeroMemory(out_args, sizeof(char*) * (MAX_ARGS + 1));

    /* parse args, splitting on whitespace or quote marks */
    p = args;
    while (*p) {
        EAT_WS(p);

        if (!(*p)) {
            break;
        }

        if (*p == '"' || *p == '\'') {
            quote_flag = *p;
            p++;
        }

        /* get current argument */
        while (*p && ((quote_flag && *p != quote_flag) || (!quote_flag && (*p != ' ' && *p != '\t')))) {
            if (!alloc_flag) {
                out_args[i] = (char*)malloc(STR_BUF_LEN);
                if (!out_args[i]) {
                    return NULL;
                }
                ZeroMemory(out_args[i], STR_BUF_LEN);
                cur_arg = out_args[i++];
                c = 0;
                alloc_flag = 1;
            }
            cur_arg[c++] = *p++;
            if (c == STR_BUF_LEN-1) {
                truncated = 1;
                break;
            }
        }

        if (truncated) {
            _snprintf(error_msg, ERROR_MSG_LEN, "Fatal: configuration file - option '%s' "
                "value is too long", keyword);
            return NULL;
        }

        /* move to next argument */
        if (*p && *p == quote_flag) {
            p++;
        }
        alloc_flag = quote_flag = 0;
        if (++count == MAX_ARGS) {
            break;
        }
    }

    if (count < min_args) {
        _snprintf(error_msg, ERROR_MSG_LEN, "Fatal: configuration file - option '%s' "
            " is missing argument(s)", keyword);
        return NULL;
    }
    else if (count > max_args) {
        _snprintf(error_msg, ERROR_MSG_LEN, "Fatal: configuration file - option '%s' "
            " has too many arguments", keyword);
        return NULL;
    }

    return out_args;
}

static void free_args(char** args) {
    char* cur_arg; 

    if (!args) {
        return;
    }

    while ((cur_arg = *args++)) {
        free(cur_arg);
    }

    return;
}

/* The string 'args' is parsed into the null-terminated array iargs.
   An error is returned if there is an invalid number of arguments. */
static KEYWORD_CB(mount)
{
    strncpy(options->mount_point, args[0], MAX_PATH);

    return 0;
}

static KEYWORD_CB(threads)
{
    options->threads = atoi(args[0]);
   
    return 0;
}

static KEYWORD_CB(user_mode)
{
    if (!stricmp(args[0], "list"))
    {
        options->user_mode = USER_MODE_LIST;
    }
    else if (!stricmp(args[0], "certificate"))
    {
        options->user_mode = USER_MODE_CERT;
    }
    else if (!stricmp(args[0], "ldap"))
    {
        options->user_mode = USER_MODE_LDAP;
    }
    else if (!stricmp(args[0], "server"))
    {
        options->user_mode = USER_MODE_SERVER;
    }
    else
    {
        _snprintf(error_msg, ERROR_MSG_LEN, "Fatal: configuration file - '%s' option"
            " must be \"list\", \"certificate\", \"ldap\" or \"server\"", keyword);
        return KEYWORD_ERR_INVALID_ARGS;
    }

    return 0;
}

static KEYWORD_CB(user)
{
    char *user_name, *uid_gid, *p;
    char uidbuf[16], gidbuf[16];
    PVFS_uid uid;
    PVFS_gid gid;
    int i, ret = 0, quote_flag = 0;
    PCONFIG_USER_ENTRY user_entry;
    
    if (options->user_mode != USER_MODE_LIST)
    {
        _snprintf(error_msg, ERROR_MSG_LEN, "%s option: must be in list mode", 
            keyword);
        return KEYWORD_ERR_UNEXPECTED;
    }

    user_name = args[0];
    uid_gid = args[1];
    
    if (uid_gid)
    {
        uidbuf[0] = gidbuf[0] = '\0';
        i = 0;
        p = uid_gid;
        while (*p && *p != ':' && i < 15)
        {
            if (isdigit(*p))
            {
                uidbuf[i++] = *p++;
            }
            else 
            {
                ret = -1;
                break;
            }
        }
        uidbuf[i] = '\0';
        if (ret == 0)
        {
            if (*p == ':')
                p++;
            i = 0;
            while(*p && i < 15)
            {
                if (isdigit(*p))
                {
                    gidbuf[i++] = *p++;
                }
                else 
                {
                    ret = -1;
                    break;
                }
            }
            gidbuf[i] = '\0';
        }
    }
    else
    {
        ret = -1;
    }
    
    if (ret == 0)
        ret = !(strlen(uidbuf) > 0 && strlen(gidbuf) > 0);

    if (ret == 0)
    {
        /* get numeric uid/gid, checking for error */
        uid = atoi(uidbuf);
        if (uid == 0 && strcmp(uidbuf, "0"))
        {
            return -1;
        }
        gid = atoi(gidbuf);
        if (gid == 0 && strcmp(gidbuf, "0"))
        {
            return -1;
        }

        /* set up user entry for list */
        user_entry = (PCONFIG_USER_ENTRY) malloc(sizeof(CONFIG_USER_ENTRY));
        if (user_entry != NULL)
        {
            strncpy(user_entry->user_name, user_name, STR_BUF_LEN);
            user_entry->user_name[STR_BUF_LEN-1] = '\0';

            user_entry->uid = uid;
            user_entry->gid = gid;

            /* insert entry */
            qlist_add_tail(&user_entry->link, &user_list);
        }
        else {
            ret = -1;
        }
    }

    return ret;
}

static KEYWORD_CB(perms)
{
    char *endptr = NULL;
    long mask;

    mask = strtol(args[0], &endptr, 8);
    if (!mask)
    {
        _snprintf(error_msg, ERROR_MSG_LEN,
                  "Fatal: configuration file - '%s' option: parse error - value "
                  "must be nonzero octal integer\n", keyword);
        return KEYWORD_ERR_INVALID_ARGS;
    }

    if (!stricmp(keyword, "new-file-perms"))
    {
        options->new_file_perms = (unsigned int) mask;
    }
    else
    {
        options->new_dir_perms = (unsigned int) mask;
    }

    return 0;
}

static KEYWORD_CB(write_time)
{
    options->disable_update_write_time = TRUE;

    return 0;
}

static KEYWORD_CB(debug)
{
    if (!stricmp(keyword, "debug"))
    {
        options->debug = TRUE;
        /* optional debug mask */
        if (args[0])
        {
            strncpy(options->debug_mask, args[0], STR_BUF_LEN);
            options->debug_mask[STR_BUF_LEN-1] = '\0';
        }
        else
        {
            /* just debug Windows client */
            strcpy(options->debug_mask, "win_client");
        }
    }
    else if (!stricmp(keyword, "debug-stderr"))
    {
        options->debug_stderr = options->debug = TRUE;
    }
    else if (!stricmp(keyword, "debug-file"))
    {
        /* copy in file path (else use default) */
        if (args[0])
        {
            strncpy(options->debug_file, args[0], MAX_PATH - 2);
            options->debug_file[MAX_PATH-2] = '\0';

            options->debug_file_flag = TRUE;
        }
    }

    return 0;
}

static KEYWORD_CB(security_mode)
{
    if (!stricmp(args[0], "default"))
    {
        options->security_mode = SECURITY_MODE_DEFAULT;
    }
    else if (!stricmp(args[0], "key"))
    {
        options->security_mode = SECURITY_MODE_KEY;
    }
    else if (!stricmp(args[0], "certificate"))
    {
        options->security_mode = SECURITY_MODE_CERT;
    }
    else
    {
        _snprintf(error_msg, ERROR_MSG_LEN, "Fatal: configuration file - '%s' option"
            " must be \"default\", \"key\" or \"certificate\"", keyword);

        return KEYWORD_ERR_INVALID_ARGS;
    }

    return 0;
}

static KEYWORD_CB(key_file)
{
    FILE *f;
    char errbuf[256];

    strncpy(options->key_file, args[0], MAX_PATH - 2);
    options->key_file[MAX_PATH-2] = '\0';

    /* cache private key in key mode */
    if (options->security_mode == SECURITY_MODE_KEY)
    {
        f = fopen(options->key_file, "r");
        if (f == NULL)
        {
            strerror_s(errbuf, sizeof(errbuf), errno);
            _snprintf(error_msg, ERROR_MSG_LEN, "Fatal: configuration file - '%s' option:"
                " could not open file %s: %s (%d)", keyword, options->key_file, errbuf, errno);

            return KEYWORD_ERR_INVALID_ARGS;
        }
    
        options->private_key = PEM_read_PrivateKey(f, NULL, NULL, NULL);
        if (options->private_key == NULL) {
            report_error("Error loading private key: ", -PVFS_ESECURITY);
        }

        fclose(f);
    }

    return 0;
}

static KEYWORD_CB(security_timeout)
{
    int timeout; 

    timeout = atoi(args[0]);
    if (timeout > 0)
    {
        options->security_timeout = timeout;
    }
    else
    {
        _snprintf(error_msg, ERROR_MSG_LEN, "Fatal: configuration file - '%s' option "
            " must be a positive integer", keyword);

        return KEYWORD_ERR_INVALID_ARGS;
    }

    return 0;
}

static KEYWORD_CB(cert_security)
{
    if (!stricmp(keyword, "ca-file") || !stricmp(keyword, "ca-path"))
    {
        strncpy(options->ca_file, args[0], MAX_PATH - 2);
        options->ca_file[MAX_PATH-2] = '\0';
    }
    else if (!stricmp(keyword, "cert-dir-prefix") || !stricmp(keyword, "cert-file"))
    {
        strncpy(options->cert_dir_prefix, args[0], MAX_PATH - 2);
        options->cert_dir_prefix[MAX_PATH-2] = '\0';
    }

    return 0;
}

static KEYWORD_CB(ldap)
{
    char temp[STR_BUF_LEN], *token;
    int ret = -1;

    if (!stricmp(keyword, "ldap-host"))
    {
        /* parse string of form ldap[s]://host[:port] */      
        strncpy(temp, args[0], STR_BUF_LEN);
        temp[STR_BUF_LEN - 1] = '\0';
        token = strtok(temp, ":/");
        if (token != NULL)
        {
            if (!stricmp(token, "ldap"))
            {
                options->ldap.secure = 0;
            }
            else if (!stricmp(token, "ldaps"))
            {
                options->ldap.secure = 1;
            }
            else
            {
                goto keyword_ldap_cb_exit;
            }
        }
        else
        {
            goto keyword_ldap_cb_exit;
        }
        
        token = strtok(NULL, ":/");
        if (token != NULL && strlen(token) > 0)
        {
            strcpy(options->ldap.host, token);
        }
        else
        {
            goto keyword_ldap_cb_exit;
        }

        token = strtok(NULL, ":");
        if (token != NULL && strlen(token) > 0)
            options->ldap.port = atoi(token);

        ret = 0;
    }
    else if (!stricmp(keyword, "ldap-bind-dn"))
    {
        /* the dn of the user used to bind to the ldap host */
        strncpy(options->ldap.bind_dn, args[0], sizeof(options->ldap.bind_dn));
        options->ldap.bind_dn[sizeof(options->ldap.bind_dn)-1] = '\0';

        ret = 0;
    }
    else if (!stricmp(keyword, "ldap-bind-password"))
    {
        /* TODO: file option */
        /* the password of the binding user */        
        strncpy(options->ldap.bind_password, args[0], 32);
        options->ldap.bind_password[31] = '\0';

        ret = 0;
    }
    else if (!stricmp(keyword, "ldap-search-root"))
    {
        /* dn of the object from which to start the search */
        strncpy(options->ldap.search_root, args[0], sizeof(options->ldap.search_root));
        options->ldap.search_root[sizeof(options->ldap.search_root)-1] = '\0';

        ret = 0;
    }
    else if (!stricmp(keyword, "ldap-search-scope"))
    {
        /* scope of search: onelevel or subtree */
        strncpy(temp, args[0], 32);
        temp[31] = '\0';

        if (!stricmp(temp, "onelevel"))
        {
            options->ldap.search_scope = LDAP_SCOPE_ONELEVEL;
        }
        else if (!stricmp(temp, "subtree"))
        {
            options->ldap.search_scope = LDAP_SCOPE_SUBTREE;
        }
        else
        {
            _snprintf(error_msg, ERROR_MSG_LEN, "Fatal: configuration file - '%s' option"
                " must be \"onelevel\" or \"subtree\"", keyword);
            ret = KEYWORD_ERR_INVALID_ARGS;
            goto keyword_ldap_cb_exit;
        }

        ret = 0;
    }
    else if (!stricmp(keyword, "ldap-search-class"))
    {
        strncpy(options->ldap.search_class, args[0], 32);
        options->ldap.search_class[31] = '\0';

        ret = 0;
    }
    else if (!stricmp(keyword, "ldap-naming-attr"))
    {
        strncpy(options->ldap.naming_attr, args[0], 32);
        options->ldap.naming_attr[31] = '\0';

        ret = 0;
    }
    else if (!stricmp(keyword, "ldap-uid-attr"))
    {
        strncpy(options->ldap.uid_attr, args[0], 32);
        options->ldap.uid_attr[31] = '\0';

        ret = 0;
    }
    else if (!stricmp(keyword, "ldap-gid-attr"))
    {
        strncpy(options->ldap.gid_attr, args[0], 32);
        options->ldap.gid_attr[31] = '\0';

        ret = 0;
    }

keyword_ldap_cb_exit:

    return ret;
}

/* pick the first available drive, starting with E: 
   drive must be able to store two chars (e.g. "F:") */
char *get_default_mount_point(char *mount_point)
{
    DWORD mask, drive_bit;
    char drive_char;

    mount_point[0] = '\0';
    mask = GetLogicalDrives();
    if (mask != 0)
    {
        /* scan for an available drive (0 bit) */
        drive_bit = 1 << 4; /* E: */
        drive_char = 'E';
        while (drive_char <= 'Z' &&
               (mask & drive_bit))
        {
            drive_bit <<= 1;
            drive_char++;
        }
        if (drive_char <= 'Z')
        {
            mount_point[0] = drive_char;
            mount_point[1] = ':';
            mount_point[2] = '\0';
        }
    }

    /* will return empty string on error/no available drives
       the program will either set a user-specified drive or 
       abort later */

    return mount_point;
}

void set_defaults(PORANGEFS_OPTIONS options)
{
    char module_dir[MAX_PATH], mount_point[16];


    /* default CA and debug file paths */
    if (get_module_dir(module_dir) == 0)
    {
        strcpy(options->ca_file, module_dir);
        strcat(options->ca_file, "\\CA\\cacert.pem");

        strcpy(options->debug_file, module_dir);
        strcat(options->debug_file, "\\orangefs.log");
    }

    /* new file/dir permissions
     *   0755 = rwxr-xr-x 
     */
    options->new_file_perms = options->new_dir_perms = 0755; 

    /* default LDAP options */
    options->ldap.search_scope = LDAP_SCOPE_ONELEVEL;
    strcpy(options->ldap.search_class, "user");
    strcpy(options->ldap.naming_attr, "sAMAccountName");
    strcpy(options->ldap.uid_attr, "uidNumber");
    strcpy(options->ldap.gid_attr, "gidNumber");

    /* default mount point */
    strcpy(options->mount_point, get_default_mount_point(mount_point));

}

int get_config(PORANGEFS_OPTIONS options,
               char *error_msg,
               unsigned int error_msg_len)
{
    FILE *config_file;
    PCONFIG_KEYWORD_DEF keyword_def;
    char line[STR_BUF_LEN+8], *pline, keyword[STR_BUF_LEN], args[STR_BUF_LEN];
    int line_num, ret = 0, i, debug_file_flag = FALSE;

    config_file = open_config_file(error_msg, error_msg_len);
    if (config_file == NULL)
        /* config file is required */
        return -1;

    set_defaults(options);

    /* initialize user list */
    /*INIT_QLIST_HEAD(&user_list);*/

    /* parse options from the file */
    line_num = 1;
    while (!feof(config_file))
    {
        ZeroMemory(line, sizeof(line));
        fgets(line, sizeof(line), config_file);
        line[sizeof(line)-1] = '\0';
        /* check for line that exceeds maximum length */
        if (strlen(line) > STR_BUF_LEN) {
            _snprintf(error_msg, ERROR_MSG_LEN, "Fatal: configuration file - line %d is too long",
                line_num);
            return -1;
        }

        /* remove \n */        
        if (strlen(line) > 0 && line[strlen(line)-1] == '\n')
            line[strlen(line)-1] = '\0';
        
        /* check line -- # used for comments */
        if (strlen(line) > 0)
        {
            /* skip whitespace and comments */
            pline = line;
            EAT_WS(pline);
            if (!(*pline) || *pline == '#')
                continue;

            /* get keyword */
            i = 0;
            while (*pline && (*pline != ' ' || *pline == '\t') &&
                (i < sizeof(keyword)-1))
            {
                keyword[i++] = *pline++;
            }
            keyword[i] = '\0';

            /* get arguments */
            EAT_WS(pline);
            i = 0;
            while (*pline && (i < sizeof(args)-1))
            {
                args[i++] = *pline++;
            }
            args[i] = '\0';
            
            memset(error_msg, 0, error_msg_len);

            /* locate and call keyword callback */
            i = 0;
            ret = KEYWORD_ERR_UNEXPECTED;
            keyword_def = &config_keyword_defs[i];
            while (keyword_def->keyword != NULL)
            {
                if (!stricmp(keyword_def->keyword, keyword)) {
                    /* parse argments into array */
                    char** arglist = get_args(args, keyword, keyword_def->min_args, 
                        keyword_def->max_args, error_msg);
                    if (arglist == NULL) {
                        /* exit - error msg already set */
                        ret = -1;
                        goto get_config_exit;
                    }
                    /* call callback */
                    ret = keyword_def->keyword_cb(options, keyword, arglist, error_msg);
                    break;
                }
                keyword_def = &config_keyword_defs[++i];
            }

            if (ret != 0)
            {
                if (ret == KEYWORD_ERR_UNEXPECTED)
                {
                    _snprintf(error_msg, error_msg_len, "Fatal: configuration file - "
                        "could not process line %s", line);
                }
                close_config_file(config_file);
                return ret;
            }            
        }
        line_num++;
    } /* feof */

    if (options->user_mode == USER_MODE_NONE && 
        options->security_mode == SECURITY_MODE_CERT)
    {
        options->user_mode = USER_MODE_SERVER;
    }

    if (options->user_mode == USER_MODE_NONE)
    {
        _snprintf(error_msg, error_msg_len, 
            "Fatal: configuration file - "
            "must specify user-mode (list, certificate, ldap or server)");
        ret = -1;
        goto get_config_exit;
    }

    if (options->security_mode == SECURITY_MODE_CERT &&
        options->user_mode != USER_MODE_SERVER)
    {
        _snprintf(error_msg, error_msg_len,
            "Fatal: configuration file - "
            "user-mode must be server if security-mode is certificate");
        ret = -1;
        goto get_config_exit;
    }

    if (options->user_mode == USER_MODE_LDAP &&
        (strlen(options->ldap.host) == 0 ||
         strlen(options->ldap.search_root) == 0))
    {        
        _snprintf(error_msg, error_msg_len, 
            "Fatal: configuration file - "
            "missing ldap option: ldap-host, or ldap-search-root");
        ret = -1;
    }

    /* gossip can only print to either a file or stderr */
    if (options->debug_stderr && options->debug_file_flag)
    {
        _snprintf(error_msg, error_msg_len, 
            "Fatal: configuration file - "
            "cannot specify both debug-stderr and debug-file");
        ret = -1;
    }

    if (options->user_mode == USER_MODE_LDAP &&
        options->ldap.port == 0)
        options->ldap.port = options->ldap.secure ? 636 : 389;

get_config_exit:

    close_config_file(config_file);

    return ret;
}

/* add users from list to cache 
 * NOTE: goptions must have been initialized
 */
int add_users(PORANGEFS_OPTIONS options,
              char *error_msg,
              unsigned int error_msg_len)
{
    int ret = 0, add_flag = 0;
    struct qlist_head *iterator = NULL, *scratch = NULL;
    PCONFIG_USER_ENTRY user_entry;
    PVFS_credential cred;

    if (options->user_mode == USER_MODE_LIST)
    {
        /* add users from list to cache */
        qlist_for_each_safe(iterator, scratch, &user_list)
        {
            user_entry = qlist_entry(iterator, CONFIG_USER_ENTRY, link);
            if (user_entry == NULL)
            {
                _snprintf(error_msg, error_msg_len, "Fatal: initialization - "
                    "internal error");
                ret = -1;
                break;
            }

            /* create credential */
            ret = init_credential(user_entry->uid, &user_entry->gid, 1, NULL, NULL, &cred);
            if (ret != 0)
            {
                _snprintf(error_msg, error_msg_len, "Fatal: initialization - "
                    "could not init credential for %s", user_entry->user_name);
                break;
            }

            /* add user to cache with no expiration */
            ret = add_cache_user(user_entry->user_name, &cred, NULL);
            if (ret != 0)
            {
                _snprintf(error_msg, error_msg_len, "Fatal: initialization - "
                    "could not add %s to cache", user_entry->user_name);
                break;
            }

            add_flag = 1;

            /* free list entry and credential fields */
            free(user_entry);

            PINT_cleanup_credential(&cred);
        }
    }

    if (options->user_mode == USER_MODE_LIST && ret == 0 && add_flag == 0)
    {
        _snprintf(error_msg, error_msg_len, "Fatal: initialization - no users");
        ret = -1;
    }

    return ret;
}
