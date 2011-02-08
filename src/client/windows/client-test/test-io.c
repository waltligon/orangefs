/* Copyright (C) 2011 Omnibond, LLC
   Client test - IO functions */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "test-support.h"
#include "test-io.h"
#include "timer.h"

int io_file_int(char *file_name, char *mode, char *buffer, size_t size)
{
    FILE *f;
    int real_size, code = 0;

    f = fopen(file_name, mode);
    if (!f)
        return errno;

    if (!strcmp(mode, "r"))
        real_size = fread(buffer, 1, size, f);
    else /* "w" or "a" */
        real_size = fwrite(buffer, 1, size, f);

    if (size != real_size)
        code = errno;

    fclose(f);

    return code;
}

int io_file(global_options *options, int fatal)
{
    char *file_name, *buffer = NULL, *copy = NULL;
    int i, j, code, code_flag;
    size_t sizes[] = {4*1024, 100*1024, 1024*1024};
    char *perftests[] = {"io_file_write_4kb", "io_file_read_4kb", "io_file_write_100kb", 
                        "io_file_read_100kb", "io_file_write_1mb", "io_file_read_1mb"};
    char *subtests[] = {"4kb", "100kb", "1mb"};
    unsigned long long start;
    double elapsed;
    
    for (i = 0; i < 3; i++)
    {
        code_flag = 0;

        /* allocate buffer */
        buffer = (char *) malloc(sizes[i]);

        /* fill buffer */
        for (j = 0; j < sizes[i]; j++)
            buffer[j] = (char) j % 256;

        file_name = randfile(options->root_dir);

        start = timer_start();
        code = io_file_int(file_name, "w", buffer, sizes[i]);
        elapsed = timer_elapsed(start);

        if (code != 0)
            goto io_file_exit;

        report_perf(options,
                    "io_file",
                    perftests[i*2],
                    elapsed,
                    "%3.3fs");

        /* copy the buffer */
        copy = (char *) malloc(sizes[i]);
        memcpy(copy, buffer, sizes[i]);

        start = timer_start();
        code = io_file_int(file_name, "r", buffer, sizes[i]);
        elapsed = timer_elapsed(start);

        if (code != 0)
            goto io_file_exit;

        report_perf(options,
                    "io_file",
                    perftests[i*2+1],
                    elapsed,
                    "%3.3fs");

        /* compare buffers */
        code = memcmp(copy, buffer, sizes[i]);
        code_flag = 1;

        report_result(options,
                      "io_file",
                      subtests[i],
                      RESULT_SUCCESS,
                      0,
                      OPER_EQUAL,
                      code);

        free(file_name); file_name = NULL;
        free(buffer); buffer = NULL;
        free(copy); copy = NULL;

    }

io_file_exit:

    if (file_name)
        free(file_name);
    if (buffer)
        free(buffer);
    if (copy)
        free(copy);

    if (!code_flag)
    {
        /* todo: report_error */
        return code;
    }

    return 0;
}