/* Copyright (C) 2011 Omnibond, LLC
   Client -- creation tests */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef WIN32
#include <direct.h>
#endif
#include <errno.h>
#include "test-support.h"

void create_dir_cleanup(char *dir)
{
    _rmdir(dir);
}

char randchar()
{
    return rand() % 26 + 'a';
}

int create_dir(global_options *options, int fatal)
{
    int code;
    char *dir;

    /* create a directory in the root dir */
    dir = randdir(options->root_dir);
    _mkdir(dir);
    code = errno;

    report_result(options, "create-dir", "main", RESULT_SUCCESS, 0, OPER_EQUAL, code);

    create_dir_cleanup(dir);

    free(dir);

    if (code != 0 && fatal)
        return CODE_FATAL;

    return 0;
}

void create_subdir_cleanup(char *root_dir, char *path)
{
    char *root_dir_int, *slash;

    /* copy root_dir without trailing backslash */
    root_dir_int = _strdup(root_dir);
    if (root_dir_int[strlen(root_dir_int)-1] == SLASH_CHAR)
        root_dir_int[strlen(root_dir_int)-1] = '\0';

    if (!_stricmp(root_dir_int, path))
        return;

    /* remove directories back to front */
    do 
    {
        _rmdir(path);

        slash = strrchr(path, SLASH_CHAR);
        if (slash)
            *slash = '\0';

    } while  (_stricmp(root_dir_int, path));

    free(root_dir_int);

}

#define MAX_SIZE    248
#define DIR_LEN       8

/* Windows _mkdir (CreateDirectoryW) has a limit of 248 characters.
   Create a number of subdirs up to the limit */
int create_subdir_int(global_options *options, int fatal, int max_size)
{
    int i, dir_count, code = 0;
    char path[MAX_SIZE*2], dir[9];

    /* number of dirs that will fit, accounting for size of root_dir 
       (deduct 1 size from max_size for the terminating null) */
    dir_count = ((max_size - 1) - (int) strlen(options->root_dir)) / DIR_LEN;

    /* copy root into path */
    strcpy(path, options->root_dir);
    
    for (i = 0; i < dir_count && code == 0; i++)
    {
        /* generate subdir */        
        sprintf(dir, "dir%04d\\", i);

        /* append the dir name */
        strcat(path, dir);

        /* create the sub-directory */
        if (0 != _mkdir(path))
        {
            code = errno;
        }
    }

    create_subdir_cleanup(options->root_dir, path);

    return code;
}

int create_subdir(global_options *options, int fatal)
{
    int code;
    code = create_subdir_int(options, fatal, MAX_SIZE);
    if (code >= 0)
    {
       report_result(options, "create-subdir", "main", RESULT_SUCCESS, 0, OPER_EQUAL, code);
       if (code != 0 && fatal)
           return CODE_FATAL;
    }
    else 
        /* technical error */
        return code;

    return 0;
}

int create_dir_toolong(global_options *options, int fatal)
{
    int code;
    
    code = create_subdir_int(options, fatal, MAX_SIZE*2);
    if (code >= 0)
    {
       report_result(options, "create-dir-toolong", "main", RESULT_FAILURE, 2, OPER_EQUAL, code);
       if (code != 2 && fatal)  /* expected result is 2 */
           return CODE_FATAL;
    }
    else 
        /* technical error */
        return code;

    return 0;    
}

void create_files_cleanup(char *path)
{
    
    _unlink(path);

}

int create_file_int(char *path)
{
    int code;
    FILE *f;

    /* create a 0 byte file */
    f = fopen(path, "w");
    code = f ? 0 : errno;
    if (f)
        fclose(f);

    create_files_cleanup(path);

    return code;

}

int create_files(global_options *options, int fatal)
{
    int code;
    char *path, *dir;

    /* attempt single file creation */
    path = randfile(options->root_dir);

    code = create_file_int(path);

    free(path);

    report_result(options,
                  "create-files",
                  "root-file",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  code);

    if (code != 0 && fatal)
        return CODE_FATAL;

    /* create file in subdir */
    
    dir = randdir(options->root_dir);
    code = _mkdir(dir);
    if (code != 0)
    {
        free(dir);
        /* technical error */
        report_error(options, "create-files: could not create directory");
        return -errno;
    }

    path = randfile(dir);

    code = create_file_int(path);

    free(path);

    /* remove subdir */
    _rmdir(dir);
    free(dir);

    report_result(options,
                  "create-files",
                  "subdir-file",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  code);

    if (code != 0 && fatal)
        return CODE_FATAL;

    return 0;    
}

#define MAX_FILE_NAME 260

int create_file_long_int(global_options *options, int max_size)
{
    int code, rem_size, i;
    FILE *ftab;
    char line[256], *token, *fs_root = NULL, 
         path[MAX_FILE_NAME*2];

    if (options->tab_file == NULL)
    {
        report_error(options, "create_file_long: missing -tabfile option");
        return -87; /* invalid parameter */
    }

    ftab = fopen(options->tab_file, "r");
    if (ftab == NULL)
    {
        report_error(options, "create_file_long: could not open tabfile");
        return -87; /* file not found */
    }

    /* TODO: get line for specified file system, for now just read first line */
    fgets(line, 256, ftab);
    token = strtok(line, " ");
    if (token)
    {
        fs_root = strtok(NULL, " ");
    }
    if (token == NULL || fs_root == NULL)
    {
        report_error(options, "create_file_long: could not parse tabfile");
        fclose(ftab);
        return 1;
    }
    
    fclose(ftab);

    rem_size = max_size - (int) strlen(fs_root);

    strcpy(path, options->root_dir);

    for (i = (int) strlen(path); i < rem_size; i++)
        path[i] = randchar();
    path[i] = '\0';

    code = create_file_int(path);

    return code;

}

/* create a file with the maximum length file name */
int create_file_long(global_options *options, int fatal)
{
    int code;

    code = create_file_long_int(options, MAX_FILE_NAME);

    report_result(options,
                  "create_file_long",
                  "main",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  code);

    if (code != 0 && fatal)
        return CODE_FATAL;

    return 0;
}

/* attempt to create a file over the maximum length file name */
int create_file_toolong(global_options *options, int fatal)
{
    int code;

    code = create_file_long_int(options, MAX_FILE_NAME*2-1);

    report_result(options,
                  "create_file_toolong",
                  "main",
                  RESULT_FAILURE,
                  2,
                  OPER_EQUAL,
                  code);

    if (code != 2 && fatal)
        return CODE_FATAL;

    return 0;
}

/* create 1000 0-byte files */
int create_files_many(global_options *options, int fatal)
{
    int code = 0, i;
    char *path;

    for (i = 0; i < 1000 && code == 0; i++)
    {
        path = randfile(options->root_dir);
        code = create_file_int(path);
        free(path);
    }

    report_result(options,
                  "create_files_many",
                  "main",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  code);

    if (code != 0 && fatal)
        return CODE_FATAL;

    return 0;
}