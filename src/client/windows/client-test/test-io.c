/* Copyright (C) 2011 Omnibond, LLC
   Client test - IO functions */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <process.h>
#include <direct.h>

#include "test-support.h"
#include "test-io.h"
#include "timer.h"

void io_file_cleanup(char *file_name)
{
    _unlink(file_name);
}

int io_file_int(char *file_name, char *mode, char *buffer, size_t size)
{
    FILE *f;
    int real_size, code = 0;
    size_t total = 0;

    f = fopen(file_name, mode);
    if (!f)
        return errno;

    while ((total < size) && !feof(f))
    {
        if (!strcmp(mode, "rb"))
            real_size = fread(&(buffer[total]), 1, size - total, f);
        else /* "wb" or "ab" */
            real_size = fwrite(&(buffer[total]), 1, size - total, f);
        if (real_size == 0 && errno != 0)
            break;
        total += real_size;
    }

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
    unsigned __int64 start;
    double elapsed;
    
    for (i = 0; i < 3; i++)
    {
        code_flag = 0;

        /* allocate buffer */
        buffer = (char *) malloc(sizes[i]);

        /* fill buffer */
        for (j = 0; (unsigned) j < sizes[i]; j++)
            buffer[j] = (char) j % 256;

        file_name = randfile(options->root_dir);

        start = timer_start();
        code = io_file_int(file_name, "wb", buffer, sizes[i]);
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
        code = io_file_int(file_name, "rb", buffer, sizes[i]);
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

        io_file_cleanup(file_name);

        free(file_name); file_name = NULL;
        free(buffer); buffer = NULL;
        free(copy); copy = NULL;

    }

io_file_exit:

    if (file_name)
    {
        io_file_cleanup(file_name);
        free(file_name);
    }
    if (buffer)
        free(buffer);
    if (copy)
        free(copy);

    if (!code_flag)
    {
        report_error(options,
            "io_file: I/O error\n");
        return code;
    }

    return 0;
}

int flush_file(global_options *options, int fatal)
{
    char *file_name;
    FILE *f, *f2;
    int i, size = 4 * 1024, total = 0,
        code;
    char *buffer, *copy;

    file_name = randfile(options->root_dir);

    f = fopen(file_name, "wb");
    if (!f)
    {
        free(file_name);
        return -1;
    }

    /* write bytes to file */
    buffer = (char *) malloc(size);
    for (i = 0; i < size; i++)
    {
        buffer[i] = i % 256;
    }
    while (total < size)
    {
        total += fwrite(buffer, 1, size, f);
    }

    /* flush the file */
    code = fflush(f);

    report_result(options,
                  "flush-file",
                  "flush-call",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  code);

    /* open another pointer to the file */
    f2 = fopen(file_name, "rb");
    if (!f2)
    {
        free(file_name);
        return -1;
    }

    /* compare data */
    copy = (char *) malloc(size);
    memcpy(copy, buffer, size);
    total = 0;
    while (total < size)
    {
        total += fread(buffer, 1, size, f2);
    }

    code = memcmp(copy, buffer, size);

    report_result(options,
                  "flush-file",
                  "flush-compare",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  code);

    fclose(f2);
    fclose(f);

    _unlink(file_name);

    free(copy);
    free(buffer);
    free(file_name);

    return 0;
}

typedef struct
{
    int threadi;
    global_options *options;
} thread_args;

void __cdecl io_file_mt_thread(void *pargs)
{
    thread_args *args = (thread_args *) pargs;
    char *dir_name, *file_name;
    int i;

    /* create directory */
    dir_name = (char *) malloc(strlen(args->options->root_dir) + 16);
    sprintf(dir_name, "%smt%02d", args->options->root_dir, args->threadi);
    _mkdir(dir_name);

    /* create 1000 files and write 4KB to them */
    for (i = 0; i < 1000; i++)
    {
        file_name = (char *) malloc(strlen(dir_name) + 16);
        sprintf(file_name, "%s\\mt%04d.tst", dir_name, i);

    }
}

int io_file_mt(global_options *options, int fatal)
{
    thread_args args;
    int code;

    /* spawn 10 threads */
    args.options = options;
    for (args.threadi = 0; args.threadi < 10; args.threadi++)
    {
        _beginthread(io_file_mt_thread, 0, &args);
    }


}