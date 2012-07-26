/*
 * (C) 2010-2011 Clemson University and Omnibond LLC
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

#include "client-service.h"
#include "config.h"
#include "user-cache.h"

extern struct qhash_table user_cache;

#define LDAP_SCOPE_ONELEVEL    0x01
#define LDAP_SCOPE_SUBTREE     0x02

#define EAT_WS(str)    while (*str && (*str == ' ' || \
                              *str == '\t')) \
                           str++

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
            file_name = (char *) malloc(MAX_PATH);
            malloc_flag = TRUE;
            strncpy(file_name, module_dir, MAX_PATH-14);
            strcat(file_name, "\\orangefs.cfg");            
        }
        else
        {
            _snprintf(error_msg, error_msg_len, "GetModuleFileName failed: %u\n", ret);
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

/* parse line in format: <[domain\]user name> <uid>:<gid> */
static int parse_user()
{
    char *token, *p;
    char user_name[256];
    char uid[16], gid[16];
    int i, ret = 0;
    PVFS_credentials credentials;

    /* assume current string being parsed */
    token = strtok(NULL, " \t");

    if (token)
    {
        /* copy user name */
        strncpy(user_name, token, 256);

        token = strtok(NULL, " \t");
        if (token)
        {
            uid[0] = gid[0] = '\0';
            i = 0;
            p = token;
            while (*p && *p != ':' && i < 15)
            {
                if (isdigit(*p))
                {
                    uid[i++] = *p++;
                }
                else 
                {
                    ret = -1;
                    break;
                }
            }
            uid[i] = '\0';
            if (ret == 0)
            {
                if (*p == ':')
                    p++;
                i = 0;
                while(*p && i < 15)
                {
                    if (isdigit(*p))
                    {
                        gid[i++] = *p++;
                    }
                    else 
                    {
                        ret = -1;
                        break;
                    }
                }
                gid[i] = '\0';
            }
        }
        else
        {
            ret = -1;
        }
    }
    else
    {
        ret = -1;
    }

    if (ret == 0)
        ret = !(strlen(uid) > 0 && strlen(gid) > 0);

    if (ret == 0)
    {
        /* add user to cache with no expiration */
        credentials.uid = atoi(uid);
        credentials.gid = atoi(gid);
        
        add_user(user_name, &credentials, NULL);
    }

    return ret;
}

static int parse_ldap_option(PORANGEFS_OPTIONS options,
                      char *line,
                      char *option,
                      char *error_msg,
                      unsigned int error_msg_len)
{
    char *p, temp[256], *token;
    int ret = -1;

    error_msg[0] = '\0';

    if (options->user_mode != USER_MODE_LDAP)
    {
        strncpy(error_msg, "Configuration file (fatal): "
            "Specify \"user-mode ldap\" before other ldap options", 
            error_msg_len);
        goto parse_ldap_option_exit;
    }

    if (!stricmp(option, "ldap-host"))
    {
        /* parse string of form ldap[s]://host[:port] */
        p = line + strlen(option);
        EAT_WS(p);
       
        strncpy(temp, p, 256);
        token = strtok(temp, ":/");
        if (token != NULL)        
            if (!stricmp(token, "ldap"))
                options->ldap.secure = 0;
            else if (!stricmp(token, "ldaps"))
                options->ldap.secure = 1;
            else
                goto parse_ldap_option_exit;        
        else 
            goto parse_ldap_option_exit;
        
        token = strtok(NULL, ":/");
        if (token != NULL && strlen(token) > 0)
            strcpy(options->ldap.host, token);
        else
            goto parse_ldap_option_exit;

        token = strtok(NULL, ":");
        if (token != NULL && strlen(token) > 0)
            options->ldap.port = atoi(token);

        ret = 0;
    }
    else if (!stricmp(option, "ldap-bind-dn"))
    {
        /* the dn of the user used to bind to the ldap host */
        p = line + strlen(option);
        EAT_WS(p);

        strncpy(options->ldap.bind_dn, p, 256);
        options->ldap.bind_dn[255] = '\0';

        ret = strlen(p) > 0 ? 0 : -1;
    }
    else if (!stricmp(option, "ldap-bind-password"))
    {
        /* the password of the binding user */
        p = line + strlen(option);
        EAT_WS(p);

        strncpy(options->ldap.bind_password, p, 32);
        options->ldap.bind_password[31] = '\0';

        ret = strlen(p) > 0 ? 0 : -1;
    }
    else if (!stricmp(option, "ldap-search-root"))
    {
        /* dn of the object from which to start the search */
        p = line + strlen(option);
        EAT_WS(p);

        strncpy(options->ldap.search_root, p, 256);
        options->ldap.search_root[255] = '\0';

        ret = strlen(p) > 0 ? 0 : -1;
    }
    else if (!stricmp(option, "ldap-search-scope"))
    {
        /* scope of search: onelevel or subtree */
        p = line + strlen(option);
        EAT_WS(p);

        strncpy(temp, p, 32);
        temp[31] = '\0';

        if (!stricmp(temp, "onelevel"))
            options->ldap.search_scope = LDAP_SCOPE_ONELEVEL;
        else if (!stricmp(temp, "subtree"))
            options->ldap.search_scope = LDAP_SCOPE_SUBTREE;
        else
        {
            strncpy(error_msg, "Configuration file (fatal): "
                "ldap-search-scope must be onelevel or subtree", error_msg_len);
            goto parse_ldap_option_exit;
        }

        ret = 0;
    }
    else if (!stricmp(option, "ldap-search-class"))
    {
        p = line + strlen(option);
        EAT_WS(p);

        strncpy(options->ldap.search_class, p, 32);
        options->ldap.search_class[31] = '\0';

        ret = strlen(p) > 0 ? 0 : -1;
    }
    else if (!stricmp(option, "ldap-naming-attr"))
    {
        p = line + strlen(option);
        EAT_WS(p);

        strncpy(options->ldap.naming_attr, p, 32);
        options->ldap.naming_attr[31] = '\0';

        ret = strlen(p) > 0 ? 0 : -1;
    }
    else if (!stricmp(option, "ldap-uid-attr"))
    {
        p = line + strlen(option);
        EAT_WS(p);

        strncpy(options->ldap.uid_attr, p, 32);
        options->ldap.uid_attr[31] = '\0';

        ret = strlen(p) > 0 ? 0 : -1;
    }
    else if (!stricmp(option, "ldap-gid-attr"))
    {
        p = line + strlen(option);
        EAT_WS(p);

        strncpy(options->ldap.gid_attr, p, 32);
        options->ldap.gid_attr[31] = '\0';

        ret = strlen(p) > 0 ? 0 : -1;
    }
    else
    {
        _snprintf(error_msg, error_msg_len, "Configuration file (fatal): "
            "Unknown option %s", option);
    }

parse_ldap_option_exit:

    if (ret != 0 && strlen(error_msg) == 0)
        _snprintf(error_msg, error_msg_len, "Configuration file (fatal): "
            "Could not parse option %s", option);

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
        strcpy(options->ca_path, module_dir);
        strcat(options->ca_path, "\\CA\\cacert.pem");

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
    char line[256], copy[256], *token, *p, *endptr;
    int ret = 0, debug_file_flag = FALSE;
    long mask;

    config_file = open_config_file(error_msg, error_msg_len);
    if (config_file == NULL)
        /* config file is required */
        return -1;

    set_defaults(options);

    /* parse options from the file */
    while (!feof(config_file))
    {
        line[0] = '\0';
        fgets(line, 256, config_file);

        /* remove \n */        
        if (strlen(line) > 0 && line[strlen(line)-1] == '\n')
            line[strlen(line)-1] = '\0';
        
        /* check line -- # used for comments */
        if (strlen(line) > 0 && line[0] != '#')
        {
            /* make a copy */
            strncpy(copy, line, 256);
            /* parse line */
            token = strtok(copy, " \t");
            if (token == NULL)
                continue;

            if (!stricmp(token, "mount"))
            {
                /* copy the remaining portion of the line 
                   as the mount point */
                token = strtok(NULL, " \t");
                strncpy(options->mount_point, token, MAX_PATH);
            }
            else if (!stricmp(token, "threads"))
            {
                token = strtok(NULL, " \t");
                options->threads = atoi(token);
            }
            else if (!stricmp(token, "user-mode"))
            {
                token = strtok(NULL, " \t");
                if (token == NULL)
                {
                    _snprintf(error_msg, error_msg_len, 
                        "Configuration file (fatal): "
                        "user-mode option must be list, certificate, "
                        "or ldap");
                    ret = -1;
                    goto get_config_exit;
                }
                if (!stricmp(token, "list"))
                {
                    options->user_mode = USER_MODE_LIST;
                }
                else if (!stricmp(token, "certificate"))
                {
                    options->user_mode = USER_MODE_CERT;
                }
                else if (!stricmp(token, "ldap"))
                {
                    options->user_mode = USER_MODE_LDAP;
                }
                else
                {
                    _snprintf(error_msg, error_msg_len,
                        "Configuration file (fatal): "
                        "user-mode option must be list, certificate, "
                        "or ldap");
                    ret = -1;
                    goto get_config_exit;
                }
            }
            else if (!stricmp(token, "user")) 
            {
                if (options->user_mode == USER_MODE_NONE)
                {
                    _snprintf(error_msg, error_msg_len, 
                        "Configuration file (fatal): "
                        "user option: specify 'user-mode list' above user "
                        "option");
                    ret = -1;
                    goto get_config_exit;
                }
                else if (options->user_mode != USER_MODE_LIST)
                {
                    _snprintf(error_msg, error_msg_len, 
                        "Configuration file (fatal): "
                        "user option: not legal with current user mode");
                    ret = -1;
                    goto get_config_exit;
                }

                if (parse_user() != 0)
                {
                    _snprintf(error_msg, error_msg_len, 
                        "Configuration file (fatal): "
                        "user option: parse error");
                    ret = -1;
                    goto get_config_exit;
                }
            }            
            else if (!stricmp(token, "cert-dir-prefix"))
            {
                p = line + strlen(token);
                EAT_WS(p);
                if (strlen(p) > 0)
                {
                    strncpy(options->cert_dir_prefix, p, MAX_PATH-2);
                    options->cert_dir_prefix[MAX_PATH-2] = '\0';
                    if (options->cert_dir_prefix[strlen(options->cert_dir_prefix)-1] != '\\')
                        strcat(options->cert_dir_prefix, "\\");
                }
                else
                {
                    _snprintf(error_msg, error_msg_len, 
                        "Configuration file (fatal): "
                        "cert-dir-prefix option: parse error");
                    ret = -1;
                    goto get_config_exit;
                }
            }
            else if (!stricmp(token, "ca-path"))
            {
                p = line + strlen(token);
                EAT_WS(p);
                if (strlen(p) > 0)
                {
                    strncpy(options->ca_path, p, MAX_PATH-2);
                    options->ca_path[MAX_PATH-2] = '\0';
                }
                else
                {
                    _snprintf(error_msg, error_msg_len, 
                        "Configuration file (fatal): "
                        "ca-path option: parse error\n");
                    ret = -1;
                    goto get_config_exit;
                }
            }
            else if (!stricmp(token, "new-file-perms") ||
                     !stricmp(token, "new-dir-perms"))
            {
                p = line + strlen(token);
                EAT_WS(p);
                /* get mask in octal format */
                mask = strtol(p, &endptr, 8);
                if (!mask || *endptr != '\0')
                {
                    _snprintf(error_msg, error_msg_len,
                        "Configuration file (fatal): "
                        "%s option: parse error - value must be "
                        "nonzero octal integer\n", token);
                    ret = -1;
                    goto get_config_exit;
                }
                if (!stricmp(token, "new-file-perms"))
                {
                    options->new_file_perms = (unsigned int) mask;
                }
                else
                {
                    options->new_dir_perms = (unsigned int) mask;
                }
            }
            else if (!stricmp(token, "debug"))
            {
                options->debug = TRUE;
                /* rest of line gives optional debug mask */
                p = line + strlen(token);
                EAT_WS(p);
                if (strlen(p) > 0)
                {
                    strncpy(options->debug_mask, p, 256);
                    options->debug_mask[255] = '\0';
                }
                else
                {
                    /* just debug Windows client */
                    strcpy(options->debug_mask, "win_client");
                }
            }
            else if (!stricmp(token, "debug-stderr"))
            {
                options->debug_stderr = options->debug = TRUE;
            }
            else if (!stricmp(token, "debug-file"))
            {
                debug_file_flag = TRUE;
                /* path to debug file */
                p = line + strlen(token);
                EAT_WS(p);
                if (strlen(p) > 0)
                {
                    strncpy(options->debug_file, p, MAX_PATH-2);
                    options->debug_file[MAX_PATH-2] = '\0';
                }
            }
            else if (!strnicmp(token, "ldap", 4))
            {
                ret = parse_ldap_option(options, line, token, error_msg, 
                    error_msg_len);
                if (ret != 0)
                    goto get_config_exit;
            }
            else
            {
                _snprintf(error_msg, error_msg_len, 
                    "Configuration file (fatal): "
                    "Unknown option %s", token);
                ret = -1;
                goto get_config_exit;
            }
        }
    }

    if (options->user_mode == USER_MODE_NONE)
    {
        _snprintf(error_msg, error_msg_len, 
            "Configuration file (fatal): "
            "Must specify user-mode (list, certificate or ldap)");
        ret = -1;
        goto get_config_exit;
    }

    if (options->user_mode == USER_MODE_LDAP &&
        (strlen(options->ldap.host) == 0 ||
         strlen(options->ldap.search_root) == 0))
    {        
        _snprintf(error_msg, error_msg_len, 
            "Configuration file (fatal): "
            "Missing ldap option: ldap-host, or ldap-search-root");
        ret = -1;
    }

    /* gossip can only print to either a file or stderr */
    if (options->debug_stderr && debug_file_flag)
    {
        _snprintf(error_msg, error_msg_len, 
            "Configuration file (fatal): "
            "Cannot specify both debug-stderr and debug-file");
        ret = -1;
    }

    if (options->user_mode == USER_MODE_LDAP &&
        options->ldap.port == 0)
        options->ldap.port = options->ldap.secure ? 636 : 389;

get_config_exit:

    close_config_file(config_file);

    return ret;
}
