/* Copyright (C) 2011 Omnibond, LLC
   Client tests -- file info functions */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#include "test-support.h"
#include "info.h"

int file_time(global_options *options, int fatal)
{
    time_t t, diff1, diff2, diff3;
    char *file_name;
    int code;
    struct _stat buf;

    t = time(NULL);

    /* create file */
    file_name = randfile(options->root_dir);
    if ((code = quick_create(file_name)) != 0)
    {
        free(file_name);
        return code;
    }

    /* get times */
    if (_stat(file_name, &buf) != 0)
    {
        free(file_name);
        return errno;
    }

    diff1 = buf.st_atime - t;
    if (diff1 < 0) diff1 *= -1;
    diff2 = buf.st_ctime - t;
    if (diff2 < 0) diff2 *= -1;
    diff3 = buf.st_mtime - t;
    if (diff3 < 0) diff3 *= -1;

    code = !((diff1 < 15) && (diff2 < 15) && (diff3 < 15));
    
    report_result(options, 
                  "file-time",
                  "main",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  0);

    free(file_name);

    if (code != 0 && fatal)
        return CODE_FATAL;

    return 0;
}
