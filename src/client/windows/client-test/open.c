/* Copyright (C) 2011
   Client test - open file functions */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "open.h"

void open_file_cleanup(char *file_name)
{
    _unlink(file_name);
}

/* open file w/specified mode */
int open_file_int(char *file_name, char *mode)
{
    FILE *f;
    int code = 0;

    f = fopen(file_name, mode);
    if (f)
        fclose(f);
    else
        code = errno;

    return code;
}

/* different subtests for file modes */
int open_file(global_options *options, int fatal)
{
    int code;
    char *file_name;

    file_name = randfile(options->root_dir);

    /* write mode */
    code = open_file_int(file_name, "w");
    
    report_result(options,
                  "open_file",
                  "w_mode",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  code);

    if (code != 0 && fatal)
    {
        open_file_cleanup(file_name);
        free(file_name);
        return CODE_FATAL;
    }

    /* read mode */
    code = open_file_int(file_name, "r");
    
    report_result(options,
                  "open_file",
                  "r_mode",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  code);

    if (code != 0 && fatal)
    {
        open_file_cleanup(file_name);
        free(file_name);
        return CODE_FATAL;
    }

    /* append mode */
    code = open_file_int(file_name, "a");
    
    report_result(options,
                  "open_file",
                  "a_mode",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  code);

    if (code != 0 && fatal)
    {
        open_file_cleanup(file_name);
        free(file_name);
        return CODE_FATAL;
    }

    /* read/write mode */
    code = open_file_int(file_name, "w+");
    
    report_result(options,
                  "open_file",
                  "w+_mode",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  code);

    if (code != 0 && fatal)
    {
        open_file_cleanup(file_name);
        free(file_name);
        return CODE_FATAL;
    }

    open_file_cleanup(file_name);

    free(file_name);

    return 0;
}
