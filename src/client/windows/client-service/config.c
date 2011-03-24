/* Copyright (C) 2011 Omnibond, LLC
   Configuration file functions */

#include <Windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "client-service.h"
#include "config.h"

extern struct qlist_head user_list;

FILE *open_config_file()
{
    FILE *f = NULL;
    char *file_name, exe_path[MAX_PATH], *p;
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
            strcpy(file_name, exe_path);
            strcat(file_name, "\\orangefs.cfg");

            ret = 0;
        }
        else
        {
            ret = GetLastError();
            fprintf(stderr, "GetModuleFileName failed: %u\n", ret);
        }
    }

    /* open config file */
    if (ret == 0)
        f = fopen(file_name, "r");

    if (malloc_flag)
        free(file_name);

    return f;
}

void close_config_file(FILE *f)
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
    PORANGEFS_USER puser;

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
        /* add user to linked list */
        puser = (PORANGEFS_USER) malloc(sizeof(ORANGEFS_USER));
        strncpy(puser->user_name, user_name, 256);
        puser->uid = atoi(uid);
        puser->gid = atoi(gid);

        qlist_add(&puser->list_link, &user_list);
    }

    return ret;
}

int get_config(PORANGEFS_OPTIONS options)
{
    FILE *config_file;
    char line[256], copy[256], *token;

    config_file = open_config_file();
    if (config_file == NULL)
        /* do not return an error -- config file is not required */
        return 0;

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

            if (!stricmp(token, "-mount") ||
                !stricmp(token, "mount"))
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
            else if (!stricmp(token, "-threads") ||
                     !stricmp(token, "threads"))
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
            else if (!stricmp(token, "-user") ||
                     !stricmp(token, "user")) 
            {
                if (parse_user() != 0)
                {
                    fprintf(stderr, "-user option: parse error\n");
                    close_config_file(config_file);
                    return 1;
                }
            }
#ifndef _DEBUG
            /* debug already enabled for debug builds */
            else if (!stricmp(token, "-debug") ||
                     !stricmp(token, "debug"))
            {
                options->debug = TRUE;
            }            
#endif
            else
                fprintf(stderr, "Unknown option %s\n", token);
        }
    }

    close_config_file(config_file);

    return 0;
}
