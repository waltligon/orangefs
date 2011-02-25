/* Copyright (C) 2011 
   Client tests -- find files functions */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef WIN32
#include <io.h>
#else
#include <dirent.h>
#include <fnmatch.h>
#endif
#include <errno.h>

#include "test-support.h"
#include "find.h"

#ifdef WIN32
int find_files(global_options *options, int fatal)
{
    char *file_names[10];
    int code, i, j;
    struct _finddata_t fileinfo;
    intptr_t findptr;

    /* create 10 files */
    for (i = 0; i < 10; i++)
    {
        file_names[i] = randfile(options->root_dir);
        if ((code = quick_create(file_names[i])) != 0)
        {
            for (j = 0; j <= i; j++)
            {
                _unlink(file_names[j]);
                free(file_names[j]);
            }
            
            return code;
        }
    }

    /* find the files */    
    for (i = 0, findptr = 0; i < 10 && findptr != -1; i++)
    {
        findptr = _findfirst(file_names[i], &fileinfo);
        if (findptr != -1)
            _findclose(findptr);
    }

    code = (findptr != -1) ? 0 : errno;

    report_result(options,
                  "find-files",
                  "main",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  code);

    for (i = 0; i < 10; i++)
    {
        _unlink(file_names[i]);
        free(file_names[i]);
    }

    if (code != 0 && fatal)
        return CODE_FATAL;

    return 0;
}

int find_files_pattern(global_options *options, int fatal)
{
    char *file_names[10], *file_name, *pattern;
    int code = 0, i, j, mark[10], found, ret;
    struct _finddata_t fileinfo;
    intptr_t findptr;

    /* create 10 files */
    for (i = 0; i < 10; i++)
    {
        file_names[i] = (char *) malloc(strlen(options->root_dir) + 9);
        sprintf(file_names[i], "%sxyz%05d", options->root_dir, rand());
        if ((code = quick_create(file_names[i])) != 0)
        {
            for (j = 0; j <= i; j++)
            {
                _unlink(file_names[j]);
                free(file_names[j]);
            }
            
            return code;
        }
        mark[i] = 0;
    }

    /* find the files with the * wildcard */
    pattern = (char *) malloc(strlen(options->root_dir) + 5);
    sprintf(pattern, "%sxyz*", options->root_dir);

    ret = findptr = _findfirst(pattern, &fileinfo);   
    while (ret != -1)
    {
        /* search file list for file */
        for (i = 0; i < 10; i++)
        {
            file_name = strrchr(file_names[i], SLASH_CHAR) + 1;
            if (!_stricmp(file_name, fileinfo.name))
            {
                mark[i] = 1;
                break;
            }
        }

        ret = _findnext(findptr, &fileinfo);
    }

    if (errno == ENOENT)
    {
        /* all found? */
        for (i = 0, found = 1; i < 10 && found; i++)
            found = found && mark[i];

        code = !found;

        report_result(options,
                      "find-files-pattern",
                      "pattern-*",
                      RESULT_SUCCESS,
                      0,
                      OPER_EQUAL,
                      code);

        _findclose(findptr);
    }
    else
    {
        _findclose(findptr);

        free(pattern);
        /* technical error */
        for (i = 0; i < 10; i++)
        {
            _unlink(file_names[i]);
            free(file_names[i]);
        }

        return errno;
    }

    /* find by ? pattern */
    free(pattern);
    pattern = (char *) malloc(strlen(options->root_dir) + 9);
    sprintf(pattern, "%sxyz?????", options->root_dir);

    ret = findptr = _findfirst(pattern, &fileinfo);   
    while (ret != -1)
    {
        /* search file list for file */
        for (i = 0; i < 10; i++)
        {
            file_name = strrchr(file_names[i], SLASH_CHAR) + 1;
            if (!_stricmp(file_name, fileinfo.name))
            {
                mark[i] = 1;
                break;
            }
        }

        ret = _findnext(findptr, &fileinfo);
    }

    if (errno == ENOENT)
    {
        /* all found? */
        for (i = 0, found = 1; i < 10 && found; i++)
            found = found && mark[i];

        code = !found;

        report_result(options,
                      "find-files-pattern",
                      "pattern-?",
                      RESULT_SUCCESS,
                      0,
                      OPER_EQUAL,
                      code);

        _findclose(findptr);
    }
    else
    {
        _findclose(findptr);

        free(pattern);
        /* technical error */
        for (i = 0; i < 10; i++)
        {
            _unlink(file_names[i]);
            free(file_names[i]);
        }

        return errno;
    }


    /* cleanup */
    free(pattern);
    for (i = 0; i < 10; i++)
    {
        _unlink(file_names[i]);
        free(file_names[i]);
    }

    if (code != 0 && fatal)
        return CODE_FATAL;

    return 0;     
}
#else
/* return first file in dir matching pattern */
int find_file(DIR *dir, char *pattern, char *path, size_t path_size)
{
    struct dirent *entry;
    int ret = -1;

    path[0] = '\0';

    entry = readdir(dir);
    while (entry) {
        ret = fnmatch(pattern, entry->d_name, FNM_PATHNAME);
        if (ret == 0)
        {
            strncpy(path, entry->d_name, path_size);
            return ret;
        }
        if (ret != FNM_NOMATCH)
            break;

        entry = readdir(dir);

    }

    return ret;
}

int find_files(global_options *options, int fatal)
{
    char *file_names[10], match_file[256];
    int code, i, j;
    DIR *dir;
    struct dirent *entry;

    /* create 10 files */
    for (i = 0; i < 10; i++)
    {
        file_names[i] = randfile(options->root_dir);
        if ((code = quick_create(file_names[i])) != 0)
        {
            for (j = 0; j <= i; j++)
            {
                _unlink(file_names[j]);
                free(file_names[j]);
            }
            
            return code;
        }
    }

    /* find the files */    
    dir = opendir(options->root_dir);
    if (dir != NULL) {
        for (i = 0; i < 10 && code == 0; i++)
        {
            code = find_file(dir, file_names[i], match_file, 256);
            seekdir(dir, 0);
        }
        closedir(dir);
    }
    else
    {
        code = errno;
    }

    report_result(options,
                  "find-files",
                  "main",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  code);

    for (i = 0; i < 10; i++)
    {
        _unlink(file_names[i]);
        free(file_names[i]);
    }

    if (code != 0 && fatal)
        return CODE_FATAL;

    return 0;
}

int find_files_pattern(global_options *options, int fatal)
{
    char *file_names[10], *file_name, *pattern, match_file[256];
    int code = 0, i, j, mark[10], found, ret;
    DIR *dir;

    /* create 10 files */
    for (i = 0; i < 10; i++)
    {
        file_names[i] = (char *) malloc(strlen(options->root_dir) + 9);
        sprintf(file_names[i], "%sxyz%05d", options->root_dir, rand());
        if ((code = quick_create(file_names[i])) != 0)
        {
            for (j = 0; j <= i; j++)
            {
                _unlink(file_names[j]);
                free(file_names[j]);
            }
            
            return code;
        }
        mark[i] = 0;
    }

    /* find the files with the * wildcard */
    pattern = (char *) malloc(strlen(options->root_dir) + 5);
    sprintf(pattern, "%sxyz*", options->root_dir);

    dir = opendir(options->root_dir);
    if (dir != NULL)
    {
        ret = find_file(dir, pattern, match_file, 256);
        while (ret == 0)
        {
            /* search file list for file */
            for (i = 0; i < 10; i++)
            {                
                if (!_stricmp(file_names[i], match_file))
                {
                    mark[i] = 1;
                    break;
                }
            }

            ret = find_file(dir, pattern, match_file, 256);
        }

        if (ret != FNM_NOMATCH)
        {
            report_error(options, "find-files-pattern: search error (1)\n");
        }

        closedir(dir);
    }
    else
    {
        report_error(options, "find-files-pattern: could not open directory (1)\n");
        return 2;
    }

    /* all found? */
    for (i = 0, found = 1; i < 10 && found; i++)
        found = found && mark[i];

    code = !found;

    report_result(options,
                  "find-files-pattern",
                  "pattern-*",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  code);

    /* find by ? pattern */
    free(pattern);
    pattern = (char *) malloc(strlen(options->root_dir) + 9);
    sprintf(pattern, "%sxyz?????", options->root_dir);

    dir = opendir(options->root_dir);
    if (dir != NULL)
    {
        ret = find_file(dir, pattern, match_file, 256);
        while (ret == 0)
        {
            /* search file list for file */
            for (i = 0; i < 10; i++)
            {                
                if (!_stricmp(file_names[i], match_file))
                {
                    mark[i] = 1;
                    break;
                }
            }

            ret = find_file(dir, pattern, match_file, 256);
        }

        closedir(dir);

        if (ret != FNM_NOMATCH)
        {
            report_error(options, "find-files-pattern: search error (2)\n");
        }
    }
    else
    {
        report_error(options, "find-files-pattern: could not open directory (2)\n");
        return 2;
    }

    /* all found? */
    for (i = 0, found = 1; i < 10 && found; i++)
         found = found && mark[i];

    code = !found;

    report_result(options,
                  "find-files-pattern",
                  "pattern-?",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  code);

    /* cleanup */
    free(pattern);
    for (i = 0; i < 10; i++)
    {
        _unlink(file_names[i]);
        free(file_names[i]);
    }

    if (code != 0 && fatal)
        return CODE_FATAL;

    return 0;     
}

#endif