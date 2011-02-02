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

int create_dir(op_options *options, int fatal)
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

#define MAX_SIZE    256

int create_subdir(op_options *options, int fatal)
{
    int rem_size = MAX_SIZE, dir_size, i, 
        code = 0;
    char path[MAX_SIZE*2], dir[9];

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

    /* note--root dir is not included in the path size */

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

    report_result(options, "create-subdir", RESULT_SUCCESS, 0, OPER_EQUAL, code);

    if (code != 0 && fatal)
        return CODE_FATAL;

    return 0;
}