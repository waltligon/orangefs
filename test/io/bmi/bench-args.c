/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <getopt.h>
#include "gossip.h"
#include <mpi.h>
#include "bmi.h"
#include "bench-args.h"

int bench_args(
    struct bench_options *user_opts,
    int argc,
    char **argv)
{
    char flags[] = "L:pm:t:l:s:r";
    int one_opt = ' ';
    int got_method = 0;

    int ret = -1;

    /* fill in defaults */
    user_opts->message_len = 100;
    user_opts->total_len = 1000;
    user_opts->flags = 0;
    user_opts->method_name[0] = '\0';
    user_opts->num_servers = 1;
    user_opts->list_io_factor = 1;

    /* look at command line arguments */
    while ((one_opt = getopt(argc, argv, flags)) != EOF)
    {
	switch (one_opt)
	{
	case ('s'):
	    ret = sscanf(optarg, "%d", &user_opts->num_servers);
	    if (ret < 1)
	    {
		return -1;
	    }
	    break;
	case ('l'):
	    ret = sscanf(optarg, "%d", &user_opts->message_len);
	    if (ret < 1)
	    {
		return -1;
	    }
	    break;
	case ('L'):
	    ret = sscanf(optarg, "%d", &user_opts->list_io_factor);
	    if (ret < 1)
	    {
		return -1;
	    }
	    break;
	case ('t'):
	    ret = sscanf(optarg, "%d", &user_opts->total_len);
	    if (ret < 1)
	    {
		return -1;
	    }
	    break;
	case ('m'):
	    got_method = 1;
	    ret = sscanf(optarg, "%s", user_opts->method_name);
	    if (ret < 1)
	    {
		return -1;
	    }
	    break;
	case ('p'):
	    user_opts->flags |= BMI_ALLOCATE_MEMORY;
	    break;
	case ('r'):
	    user_opts->flags |= REUSE_BUFFERS;
	    break;
	default:
	    break;
	}
    }

    if (!got_method)
    {
	printf("Must specify method!\n");
	return (-1);
    }

    return (0);
}

void bench_args_dump(
    struct bench_options *opts)
{
    printf("Options:\n");
    if (opts->flags & REUSE_BUFFERS)
    {
	printf("Reuse buffers.\n");
    }
    if (opts->flags & BMI_ALLOCATE_MEMORY)
    {
	printf("BMI allocate memory.\n");
    }
    printf("message length: %d\n", opts->message_len);
    printf("total length: %d\n", opts->total_len);
    printf("number of servers: %d\n", opts->num_servers);
    printf("method name: %s\n", opts->method_name);
    printf("count of each list io message: %d\n", opts->list_io_factor);

    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
