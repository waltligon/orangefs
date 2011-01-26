/* Copyright (C) 2011 Omnibond, LLC
   Configuration file functions */

#include <Windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "client-service.h"
#include "config.h"

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

int get_config(PORANGEFS_OPTIONS options)
{
    FILE *config_file;
    char line[256], copy[256], *token, *p;

    config_file = open_config_file();
    if (config_file == NULL)
        /* do not return an error -- config file is not required */
        return 0;

    /* parse options from the file */
    while (!feof(config_file))
    {
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
                p = line + strlen(token);
                while (*p && (*p == ' ' || *p == '\t'))
                    p++;
                if (*p)
                    strncpy(options->mount_point, p, MAX_PATH);
            }
            else if (!stricmp(token, "-threads") ||
                     !stricmp(token, "threads"))
            {
                p = line + strlen(token);
                while (*p && (*p == ' ' || *p == '\t'))
                    p++;
                if (*p)
                    options->threads = atoi(p);
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

    return 0;
}

void close_config_file(FILE *f)
{
    fclose(f);
}
