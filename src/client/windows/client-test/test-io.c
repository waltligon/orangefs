/* Copyright (C) Omnibond, LLC
   Client test - IO functions */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef WIN32
#include <Windows.h>
#include <process.h>
#include <direct.h>
#else
#include <pthread.h>
#endif
#include <errno.h>

#include "test-support.h"
#include "test-io.h"
#include "timer.h"
#include "thread.h"

#define BUF_MAX_SIZE    1024*1024   /* 1 MB */

/*
const char* local_time(const char *out_time) {
    SYSTEMTIME localtime;

    GetLocalTime(&localtime);

    sprintf(out_time, "%04d-%02d-%02d %02d:%02d:%02d.%03d", localtime.wYear,
            localtime.wMonth, localtime.wDay, localtime.wHour, localtime.wMinute,
            localtime.wSecond, localtime.wMilliseconds);
    return out_time;
}
*/

void io_file_cleanup(char *file_name)
{
    _unlink(file_name);
}

int io_file_int(char *file_name, char *mode, char *buffer, size_t size)
{
    FILE *f;
    int code = 0, imode, i = 0;
    size_t real_size, total = 0, buf_size;
    /* const char timestr[64]; */
    unsigned __int64 subtimer;
    double elapsed;

    f = fopen(file_name, mode);
    if (!f)
        return errno;

    /* compute buf_size */
    buf_size = (size > BUF_MAX_SIZE) ? BUF_MAX_SIZE : size;

    /* set mode */
    imode = !strcmp(mode, "rb") ? 0 : 1;

    while ((total < size) && !feof(f))
    {
        if (imode == 0) /* "rb" */
        {
            /* printf("  [%s] read start %llu of %llu\n", local_time(timestr), buf_size, total); */
            subtimer = timer_start();
            real_size = fread(buffer, 1, buf_size, f);
            elapsed = timer_elapsed(subtimer);
            /* printf("  [%s] read complete %llu of %llu (%llu)\n", local_time(timestr), buf_size, total, real_size); */
            
        }
        else /* "wb" or "ab" */
        {
            /* printf("  [%s] write start %llu of %llu\n", local_time(timestr), buf_size, total); */
            subtimer = timer_start();
            real_size = fwrite(buffer, 1, buf_size, f);
            elapsed = timer_elapsed(subtimer);
            /* printf("  [%s] write complete %llu of %llu (%llu)\n", local_time(timestr), buf_size, total, real_size); */
        }

        if (real_size == 0)
        {
            code = errno;
            break;
        }
        total += real_size;

        /* debugging */
        /* printf("  %s %llu of %llu #%u: %llu (%7.3fs)\n", (imode == 0) ? "read" : "write", buf_size,
               size, i++, total, elapsed); */
    }

    fclose(f);

    return code;
}


#define NUM_SIZES    5

int io_file(global_options *options, int fatal)
{
    char *file_name, *buffer = NULL, *copy = NULL;
    int i, j, code, code_flag;
    size_t sizes[] = {4*1024, 100*1024, 1024*1024, 100*1024*1024, 1024*1024*1024};
    size_t buf_size;
    char *perftests[] = {"io_file_write_4kb", "io_file_read_4kb", 
                         "io_file_write_100kb", "io_file_read_100kb", 
                         "io_file_write_1mb", "io_file_read_1mb",
                         "io_file_write_100mb", "io_file_read_100mb",
                         "io_file_write_1gb", "io_file_read_1gb"};
    char *subtests[] = {"4kb", "100kb", "1mb", "100mb", "1gb"};
#ifdef WIN32
    unsigned __int64 start;
#else
    struct timeval start;
#endif
    double elapsed;
    
    for (i = 0; i < NUM_SIZES; i++)
    {
        code_flag = 0;

        /* compute buffer size */
        buf_size = (sizes[i] > BUF_MAX_SIZE) ? BUF_MAX_SIZE : sizes[i];

        /* allocate buffer */
        buffer = (char *) malloc(buf_size);

        /* fill buffer */
        for (j = 0; (unsigned) j < buf_size; j++)
            buffer[j] = (char) j % 256;

        file_name = randfile(options->root_dir);

#ifdef WIN32
        start = timer_start();
#else
        timer_start(&start);
#endif
        code = io_file_int(file_name, "wb", buffer, sizes[i]);
#ifdef WIN32
        elapsed = timer_elapsed(start);
#else
        elapsed = timer_elapsed(&start);
#endif

        if (code != 0)
            goto io_file_exit;

        report_perf(options,
                    "io_file",
                    perftests[i*2],
                    elapsed,
                    "%3.3fs");

        /* copy the buffer */
        copy = (char *) malloc(buf_size);
        memcpy(copy, buffer, buf_size);

#ifdef WIN32
        start = timer_start();
#else
        timer_start(&start);
#endif
        code = io_file_int(file_name, "rb", buffer, sizes[i]);

#ifdef WIN32
        elapsed = timer_elapsed(start);
#else
        elapsed = timer_elapsed(&start);
#endif

        if (code != 0)
            goto io_file_exit;

        report_perf(options,
                    "io_file",
                    perftests[i*2+1],
                    elapsed,
                    "%3.3fs");

        /* compare buffers */
        code = memcmp(copy, buffer, buf_size);
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
    int i, size = 4 * 1024, code;
    size_t total = 0;
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

#define THREAD_COUNT   10
#define FILE_COUNT     100

void io_file_mt_cleanup(global_options *options)
{
    char *dir_name, *file_name;
    int i, j;

    for (i = 0; i < THREAD_COUNT; i++)
    {
        dir_name = (char *) malloc(strlen(options->root_dir) + 16);
        sprintf(dir_name, "%smt%02d", options->root_dir, i);

        for (j = 0; j < FILE_COUNT; j++)
        {
            file_name = (char *) malloc(strlen(dir_name) + 16);
            sprintf(file_name, "%s%cmt%04d.tst", dir_name, SLASH_CHAR, j);

            _unlink(file_name);

            free(file_name);
        }

        _rmdir(dir_name);

        free(dir_name);
    }
}

#ifdef WIN32
unsigned __stdcall io_file_mt_thread(void *pargs)
{
    thread_args *args = (thread_args *) pargs;
    char *dir_name, *file_name, buf[4096];
    int i;
    unsigned __int64 start;
    double elapsed;
    unsigned int total = 0;
    FILE *f;

    /* create directory */
    dir_name = (char *) malloc(strlen(args->options->root_dir) + 16);
    sprintf(dir_name, "%smt%02d", args->options->root_dir, args->threadi);
    _mkdir(dir_name);

    for (i = 0; i < 4096; i++)
        buf[i] = i % 256;

    /* create 1000 files and write 4KB to them */
    for (i = 0; i < FILE_COUNT; i++)
    {
        file_name = (char *) malloc(strlen(dir_name) + 16);
        sprintf(file_name, "%s%cmt%04d.tst", dir_name, SLASH_CHAR, i);
        start = timer_start();
        f = fopen(file_name, "wb");
        if (f)
        {
            fwrite(buf, 1, 4096, f);
            fclose(f);
        }
        elapsed = timer_elapsed(start);
        total += (unsigned int) (elapsed * 1000000.0);

        free(file_name);
    }

    free(dir_name);

    return total;
}

int io_file_mt(global_options *options, int fatal)
{
    thread_args *args;
    uintptr_t *hthreads;
    int threadi, ret, i, code = 0;
    unsigned int counter, total;
    unsigned long long start;
    double elapsed;

    /* allocate args */
    args = (thread_args *) malloc(sizeof(thread_args) * THREAD_COUNT);

    /* allocate thread array */
    hthreads = (uintptr_t *) malloc(sizeof(uintptr_t) * THREAD_COUNT);

    /* spawn threads */    
    start = timer_start();
    for (threadi = 0; threadi < THREAD_COUNT; threadi++)
    {
        args[threadi].options = options;
        args[threadi].threadi = threadi;

        hthreads[threadi] = _beginthreadex(NULL, 0, io_file_mt_thread, &(args[threadi]), 0, NULL);
        if (hthreads[threadi] == 0)
        {
            /* TODO */
            code = -1;
            break;
        }
    }

    /* wait for threads to complete */
    if (code == 0)
    {
        ret = thread_wait_multiple(THREAD_COUNT, hthreads, 1, THREAD_WAIT_INFINITE);
        elapsed = timer_elapsed(start);

        if (ret >= THREAD_WAIT_SIGNALED && ret < THREAD_COUNT)
        {
            /* sum the exit codes */
            for (i = 0, total = 0; i < THREAD_COUNT; i++)
            {
                /* exit code is time in microseconds */
                if (get_thread_exit_code(hthreads[i], &counter))
                {
                    total += counter;
                }
                else 
                {
                    /* TODO */
                    code = -1;
                    break;
                }
            }
        }
        else 
        {
            code = ret * -1;
        }
    }

    if (code == 0)
    {
        report_perf(options,
                    "io-file-mt",
                    "file-io",
                    (double) total / 1000000,
                    "%3.3f sec");

        report_perf(options,
                    "io-file-mt",
                    "total",
                    elapsed,
                    "%3.3f sec");
    }
    else
    {
        /* TODO */
        report_error(options,
                     "io_file_mt: failed");
    }

    io_file_mt_cleanup(options);

    free(hthreads);
    free(args);

    return code;
}
#else
/* Linux version */
void *io_file_mt_thread(void *pargs)
{
    thread_args *args = (thread_args *) pargs;
    char *dir_name, *file_name, buf[4096];
    int i;
    struct timeval start;
    double elapsed;
    unsigned int *total;
    FILE *f;

    total = (unsigned int *) malloc(sizeof(unsigned int));
    *total = 0;

    /* create directory */
    dir_name = (char *) malloc(strlen(args->options->root_dir) + 16);
    sprintf(dir_name, "%smt%02d", args->options->root_dir, args->threadi);
    _mkdir(dir_name);

    for (i = 0; i < 4096; i++)
        buf[i] = i % 256;

    /* create 1000 files and write 4KB to them */
    for (i = 0; i < FILE_COUNT; i++)
    {
        file_name = (char *) malloc(strlen(dir_name) + 16);
        sprintf(file_name, "%s%cmt%04d.tst", dir_name, SLASH_CHAR, i);
        timer_start(&start);
        f = fopen(file_name, "wb");
        if (f)
        {
            fwrite(buf, 1, 4096, f);
            fclose(f);
        }
        elapsed = timer_elapsed(&start);
        *total += (unsigned int) (elapsed * 1000000.0);

        free(file_name);
    }

    free(dir_name);

    pthread_exit(total);

    return total;
}

int io_file_mt(global_options *options, int fatal)
{
    thread_args *args;
    pthread_t *hthreads;
    int threadi, ret, i, code = 0;
    unsigned int total;
    void *counter;
    struct timeval start;
    double elapsed;

    /* allocate args */
    args = (thread_args *) malloc(sizeof(thread_args) * THREAD_COUNT);

    /* allocate thread array */
    hthreads = (pthread_t *) malloc(sizeof(pthread_t) * THREAD_COUNT);

    /* spawn threads */    
    timer_start(&start);
    for (threadi = 0; threadi < THREAD_COUNT; threadi++)
    {
        args[threadi].options = options;
        args[threadi].threadi = threadi;

        code = pthread_create(&hthreads[threadi], NULL, io_file_mt_thread, &args[threadi]);
        if (code != 0)
            break;
    }

    /* wait for threads to complete */
    if (code == 0)
    {
        for (i = 0; i < THREAD_COUNT && code == 0; i++)
        {
            /* return value is time in microseconds */
            code = pthread_join(hthreads[i], &counter);            
            total += *((unsigned int *) counter);

            free(counter);
        }

        elapsed = timer_elapsed(&start);
    }

    if (code == 0)
    {
        report_perf(options,
                    "io-file-mt",
                    "file-io",
                    (double) total / 1000000,
                    "%3.3f sec");

        report_perf(options,
                    "io-file-mt",
                    "total",
                    elapsed,
                    "%3.3f sec");
    }
    else
    {
        /* TODO */
        report_error(options,
                     "io_file_mt: thread join failed");
    }

    io_file_mt_cleanup(options);

    free(hthreads);
    free(args);

    return code;
}
#endif