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
#include <mpi.h>

#ifndef __BENCH_ARGS_H
#define __BENCH_ARGS_H

struct bench_options
{
	int list_io_factor;
	int flags;
	int message_len;
	int total_len;
	int num_servers;
	char method_name[256];
};

enum
{
	BMI_ALLOCATE_MEMORY = 1,
	REUSE_BUFFERS = 2
};

int bench_args(struct bench_options* user_opts, int argc, char** argv);
void bench_args_dump(struct bench_options* opts);

#endif /* __BENCH_ARGS_H */
