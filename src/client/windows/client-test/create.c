/* Copyright (C) 2011 Omnibond, LLC
   Client -- creation tests */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <direct.h>

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

    report_result(options, "create-dir", RESULT_SUCCESS, 0, OPER_EQUAL, code);

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
    if (root_dir_int[strlen(root_dir_int)-1] == '\\')
        root_dir_int[strlen(root_dir_int)-1] = '\0';

    /* remove directories back to front */
    while (stricmp(root_dir_int, path))
    {
        slash = strrchr(path, '\\');
        if (slash)
            *slash = '\0';

        _rmdir(path);
    }

    free(root_dir_int);

}

#define MAX_SIZE    254

int create_subdir_int(global_options *options, int fatal, int max_size)
{
    int rem_size = max_size, dir_size, i, 
        code = 0, dir_num;
    FILE *ftab;
    char line[256], *token, *fs_root = NULL, 
         path[MAX_SIZE*2], dir[9];

    if (options->tab_file == NULL)
    {
        report_error(options, "create_subdir: missing -tabfile option\n");
        return -87; /* invalid parameter */
    }

    ftab = fopen(options->tab_file, "r");
    if (ftab == NULL)
    {
        report_error(options, "create_subdir: could not open tabfile\n");
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
        report_error(options, "create_subdir: could not parse tabfile\n");
        fclose(ftab);
        return 1;
    }
    
    fclose(ftab);

    /* remove the length of the root dir from the max path size */
    rem_size -= strlen(fs_root);

    /* copy root into path */
    strcpy(path, options->root_dir);
    /* rem_size -= strlen(options->root_dir); */
    /* add backslash if necessary */
    if (strlen(options->root_dir) && 
        options->root_dir[strlen(options->root_dir)-1] != '\\')
    {
        strcat(path, "\\");
        /* rem_size--; */
    }

    /* note--local root dir is not included in the path size */
    dir_num = 1;
    while (rem_size > 0 && code == 0)
    {
        /* generate subdir */
        dir_size = rem_size > 8 ? 8 : rem_size;
        for (i = 0; i < dir_size-1; i++)
            dir[i] = randchar();
        dir[dir_size-1] = '\\';
        dir[dir_size] = '\0';

        /* append the path */
        strcat(path, dir);

        /* create the sub-directory */
        _mkdir(path);
        code = errno;

        rem_size -= dir_size;
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
       report_result(options, "create-subdir", RESULT_SUCCESS, 0, OPER_EQUAL, code);
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
    code = create_subdir_int(options, fatal, MAX_SIZE+1);
    if (code >= 0)
    {
       report_result(options, "create-dir-toolong", RESULT_FAILURE, 2, OPER_EQUAL, code);
       if (code != 2 && fatal)  /* expected result is 2 */
           return CODE_FATAL;
    }
    else 
        /* technical error */
        return code;

    return 0;    
}
