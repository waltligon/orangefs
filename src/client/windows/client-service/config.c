/* Copyright (C) 2011 Omnibond, LLC
   Configuration file functions */

#include <Windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "client-service.h"
#include "config.h"
#include "user-cache.h"

extern struct qhash_table user_cache;

static FILE *open_config_file(char *error_msg, 
                              unsigned int error_msg_len)
{
    FILE *f = NULL;
    char *file_name = NULL, exe_path[MAX_PATH], *p;
    DWORD ret = 0, malloc_flag = FALSE;

    /* environment variable overrides */
    if (!(file_name = getenv("ORANGEFS_CONFIG_FILE")))
    {
        /* look for file in exe directory */
        ret = GetModuleFileName(NULL, exe_path, MAX_PATH);
        if (ret)
        {
            p = strrchr(exe_path, '\\');
            if (p)
                *p = '\0';

            file_name = (char *) malloc(MAX_PATH);
            malloc_flag = TRUE;
            strncpy(file_name, exe_path, MAX_PATH-14);
            strcat(file_name, "\\orangefs.cfg");

            ret = 0;
        }
        else
        {
            ret = GetLastError();
            _snprintf(error_msg, error_msg_len, "GetModuleFileName failed: %u\n", ret);
        }
    }

    /* open config file */
    if (ret == 0)
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
int parse_user()
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
                    ret = 1;
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
                        ret = 1;
                        break;
                    }
                }
                gid[i] = '\0';
            }
        }
        else
        {
            ret = 1;
        }
    }
    else
    {
        ret = 1;
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

int get_config(PORANGEFS_OPTIONS options,
               char *error_msg,
               unsigned int error_msg_len)
{
    FILE *config_file;
    char line[256], copy[256], *token;
    int ret = 0;

    config_file = open_config_file(error_msg, error_msg_len);
    if (config_file == NULL)
        /* config file is required */
        return 1;

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
                /*
                p = line + strlen(token);
                while (*p && (*p == ' ' || *p == '\t'))
                    p++;
                if (*p)
                */
                token = strtok(NULL, " \t");
                strncpy(options->mount_point, token, MAX_PATH);
            }
            else if (!stricmp(token, "threads"))
            {
                /*
                p = line + strlen(token);
                while (*p && (*p == ' ' || *p == '\t'))
                    p++;
                if (*p)
                */
                token = strtok(NULL, " \t");
                options->threads = atoi(token);
            }
            else if (!stricmp(token, "user-mode"))
            {
                token = strtok(NULL, " \t");
                if (token == NULL)
                {
                    _snprintf(error_msg, error_msg_len, "user-mode option must be list, certificate, or ldap\n");                    
                    ret = 1;
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
                    _snprintf(error_msg, error_msg_len, "user-mode option must be list, certificate, or ldap\n");
                    ret = 1;
                    goto get_config_exit;
                }
            }
            else if (!stricmp(token, "user")) 
            {
                if (options->user_mode == USER_MODE_NONE)
                {
                    _snprintf(error_msg, error_msg_len, "user option: specify 'user-mode list' above user option\n");
                    ret = 1;
                    goto get_config_exit;
                }
                else if (options->user_mode != USER_MODE_LIST)
                {
                    _snprintf(error_msg, error_msg_len, "user option: not legal with current user mode\n");
                    ret = 1;
                    goto get_config_exit;
                }

                if (parse_user() != 0)
                {
                    _snprintf(error_msg, error_msg_len, "user option: parse error\n");
                    ret = 1;
                    goto get_config_exit;
                }
            }            
            else if (!stricmp(token, "cert-dir-prefix"))
            {
                if (strlen(line) > 16)
                {
                    strncpy(options->cert_dir_prefix, line + 16, MAX_PATH-2);
                    options->cert_dir_prefix[MAX_PATH-2] = '\0';
                    if (options->cert_dir_prefix[strlen(options->cert_dir_prefix)-1] != '\\')
                        strcat(options->cert_dir_prefix, "\\");
                }
                else
                {
                    _snprintf(error_msg, error_msg_len, "cert-dir-prefix option: parse error\n");
                    ret = 1;
                    goto get_config_exit;
                }
            }
            else if (!stricmp(token, "ca-path"))
            {
                if (strlen(line) > 8)
                {
                    strncpy(options->ca_path, line + 8, MAX_PATH-2);
                    options->ca_path[MAX_PATH-2] = '\0';
                }
                else
                {
                    _snprintf(error_msg, error_msg_len, "ca-path option: parse error\n");
                    ret = 1;
                    goto get_config_exit;
                }
            }
            else if (!stricmp(token, "debug"))
            {
                options->debug = TRUE;
            }            
            else
                _snprintf(error_msg, error_msg_len, "Unknown option %s\n", token);
        }
    }

    if (options->user_mode == USER_MODE_NONE)
    {
        _snprintf(error_msg, error_msg_len, "Must specify user-mode (list, certificate or ldap)\n");
        ret = 1;
        goto get_config_exit;
    }

    if (options->user_mode == USER_MODE_CERT &&
        strlen(options->ca_path) == 0)
    {
        _snprintf(error_msg, error_msg_len, "Must specify ca-path with certificate mode\n");
        ret = 1;
    }

get_config_exit:

    close_config_file(config_file);

    return ret;
}
