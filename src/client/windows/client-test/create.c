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

int create_dir(op_options *options, int fatal)
{
    int code;
    char *dir;

    /* create a directory in the root dir */
    dir = randdir(options->root_dir);
    _mkdir(dir);
    code = errno;

    report_result(options, "create-dir", RESULT_SUCCESS, 0, OPER_EQUAL, errno);

    create_dir_cleanup(dir);

    free(dir);

    if (code != 0 && fatal)
        return CODE_FATAL;

    return 0;
}

#define MAX_SIZE    256

int create_subdir(op_options *options, int fatal)
{
    int rem_size = MAX_SIZE;

    rem_size -= strlen(options->root_dir);

    while (rem_size > 0)
    {


    }

    
}