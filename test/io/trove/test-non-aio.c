/*
 * Copyright  Acxiom Corporation, 2006
 *
 * See COPYING in top-level directory.
 */

#define _XOPEN_SOURCE 500

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "quicklist.h"
#include "pint-util.h"

struct options
{
    int total_ops;
    long long size;
    char* filename;
    int sync;
};

static int parse_args(int argc, char **argv, struct options* opts);
static void usage(void);

static float time_sum = 0;
static float time_max = 0;

#define IO_SIZE (256*1024)

int main(int argc, char **argv)	 
{
    int ret;
    struct options opts;
    int i = 0;
    int my_fd = -1;
    char* my_buffer = NULL;
    long long cur_offset = 0;
    PINT_time_marker start;
    PINT_time_marker end;
    PINT_time_marker total_start;
    PINT_time_marker total_end;
    double wtime, utime, stime;
    double total_wtime, total_utime, total_stime;

    ret = parse_args(argc, argv, &opts);
    if (ret < 0) 
    {
	fprintf(stderr, "Error: argument parsing failed.\n");
        usage();
	return -1;
    }

    printf("file size: %lld\n", opts.size);
    printf("total ops: %d\n", opts.total_ops);
    printf("filename: %s\n", opts.filename);

    /* open file */
    if(opts.sync)
    {
        my_fd = open(opts.filename, (O_RDWR|O_CREAT|O_TRUNC|O_SYNC), (S_IRUSR|S_IWUSR));
    }
    else
    {
        my_fd = open(opts.filename, (O_RDWR|O_CREAT|O_TRUNC), (S_IRUSR|S_IWUSR));
    }
    if(my_fd < 0)
    {
        perror("open");
        return(-1);
    }

    /* allocate buffer to write */
    my_buffer = malloc(IO_SIZE);

    PINT_time_mark(&total_start);
    for(i = 0; i<opts.total_ops; i++)
    {

        PINT_time_mark(&start);
        
        ret = pwrite(my_fd, my_buffer, IO_SIZE, cur_offset);
        if(ret < 0)
        {
            perror("write");
            return(-1);
        }
        PINT_time_mark(&end);

        PINT_time_diff(start, end, &wtime, &utime, &stime);

        time_sum += wtime;
        if(wtime > time_max)
        {
            time_max = wtime;
        }

        printf("write count: %d, elapsed time: %f seconds\n", i,
            wtime);
        cur_offset += IO_SIZE;
        if(cur_offset > opts.size)
        {
            cur_offset = 0;
        }
    }
    PINT_time_mark(&total_end);

    close(my_fd);
    ret = unlink(opts.filename);
    if(ret < 0)
    {
        perror("unlink");
        return(-1);
    }

    PINT_time_diff(total_start, total_end, &total_wtime, &total_utime,
        &total_stime);

    printf("TEST COMPLETE.\n");
    printf("Maximum service time: %f seconds\n", time_max);
    printf("Average service time: %f seconds\n", (time_sum/opts.total_ops));
    printf("Total time: %f seconds\n", total_wtime);

    return(0);
}

static int parse_args(int argc, char **argv, struct options* opts)
{
    int c;
    int ret;

    memset(opts, 0, sizeof(struct options));
    opts->total_ops = 100;
    opts->size = 1024*1024*1024;

    while ((c = getopt(argc, argv, "t:s:hf:y")) != EOF) {
	switch (c) {
	    case 't': /* total operations */
                ret = sscanf(optarg, "%d", &opts->total_ops);
                if(ret != 1)
                {
                    return(-1);
                }
		break;
	    case 's': /* size of file */
                ret = sscanf(optarg, "%lld",
                    &opts->size);
                if(ret != 1)
                {
                    return(-1);
                }
		break;
            case 'f':
                opts->filename = (char*)malloc(strlen(optarg)+1);
                assert(opts->filename);
                strcpy(opts->filename, optarg);
                break;
	    case 'h': /* help */
                usage();
                return(0);
		break;
            case 'y': /* help */
                opts->sync = 1;
		break;
	    case '?':
	    default:
		return -1;
	}
    }
    if(!opts->filename)
    {
        fprintf(stderr, "Error: must specify filename with -f.\n");
        return(-1);
    }
    return 0;
}

static void usage(void)
{
    printf("USAGE: test-aio [options]\n");
    printf("  -t: total number of operations\n");
    printf("  -s: overall size of file\n");
    printf("  -h: display usage information\n");
    printf("  -y: open file in O_SYNC mode\n");
    printf("  -f: file name\n");
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

