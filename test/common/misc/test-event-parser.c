/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>

#include "pvfs2.h"
#include "pvfs2-mgmt.h"
#include "pvfs2-event.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

static void usage(int argc, char** argv);

struct data_point
{
    int processed;
    int server;
    int api;
    int op;
    long long value;
    long long id;
    int flags;
    long sec;
    long usec;
};

int main(int argc, char **argv)
{
    int ret = -1;
    FILE* infile;
    struct data_point* data_array;
    int array_size = 8000;
    int cur_index = 0;
    int run_index = 0;
    char tmp_buf[512];
    double tmp_time = 0.0f;
    double tmp_time2 = 0.0f;
    double first_time = 0.0f;
    double last_end_time = 0.0f;
    double total_empty = 0.0f;
    int counter = 0;

    if(argc != 2)
	usage(argc, argv);

    infile = fopen(argv[1], "r");
    if(!infile)
    {
	perror("fopen");
	return(-1);
    }

    data_array = (struct data_point*)malloc(array_size*sizeof(struct data_point));
    if(!data_array)
    {
	perror("malloc");
	return(-1);
    }
    memset(data_array, 0, array_size*sizeof(struct data_point));

    printf("# (measurement #) (server) (api_op) (size/value) (start) (end) (length)\n");

    /* pull in all of the data */
    while(fgets(tmp_buf, 512, infile))
    {
	/* skip comments */
	if(tmp_buf[0] == '#')
	    continue;

	if(cur_index >= array_size)
	{
	    fprintf(stderr, "Overflow.\n");
	    return(-1);
	}

	/* read in data line */
	ret = sscanf(tmp_buf, "%d %d %d %Ld %Ld %d %ld %ld\n",
	    &data_array[cur_index].server,
	    &data_array[cur_index].api,
	    &data_array[cur_index].op,
	    &data_array[cur_index].value,
	    &data_array[cur_index].id,
	    &data_array[cur_index].flags,
	    &data_array[cur_index].sec,
	    &data_array[cur_index].usec);
	if(ret != 8)
	{
	    fprintf(stderr, "Parse error, data line %d.\n", (cur_index+1));
	    return(-1);
	}
	cur_index++;
    }
    array_size = cur_index;

    /* iterate through it and turn into something plottable */
    for(cur_index=0; cur_index < array_size; cur_index++)
    {
	if(data_array[cur_index].processed)
	    continue;
	if(data_array[cur_index].flags != PVFS_EVENT_FLAG_START)
	{
	    data_array[cur_index].processed = 1;
	    continue;
	}

	if(first_time == 0)
	{
	    first_time = (double)data_array[cur_index].sec + 
		(double)data_array[cur_index].usec / 1000000;
	}
	
	/* found a starting time; lets look for the matching end time */
	for(run_index=cur_index; run_index < array_size; run_index++)
	{
	    if(
		!data_array[run_index].processed &&
		data_array[run_index].flags == PVFS_EVENT_FLAG_END &&
		data_array[run_index].server == data_array[cur_index].server &&
		data_array[run_index].api == data_array[cur_index].api &&
		data_array[run_index].op == data_array[cur_index].op &&
		data_array[run_index].id == data_array[cur_index].id)
	    {
		/* printf("match.\n"); */
		printf("%d\t", counter);
		counter++;
		data_array[run_index].processed = 1;
		printf("%d\t%d_%d\t", data_array[cur_index].server,
		    data_array[cur_index].api,
		    data_array[cur_index].op);
		if(data_array[cur_index].value > 0)
		    printf("%Ld\t", data_array[cur_index].value);
		else
		    printf("%Ld\t", data_array[run_index].value);
		tmp_time = (double)data_array[cur_index].sec + 
		    (double)data_array[cur_index].usec / 1000000;
		tmp_time = tmp_time - first_time;
		/* tmp_time is start time, counted since first event */
		printf("%f\t", tmp_time);

		if(last_end_time != 0 && last_end_time < tmp_time)
		{
#if 0
		    printf("empty: %f to %f, len %f\n", last_end_time, tmp_time,
			(tmp_time-last_end_time));
#endif
		    total_empty += (tmp_time-last_end_time);
		}
		else
		{
#if 0
		    printf("\n overlap\n");
#endif
		}

		/* again; end time */
		tmp_time2 = (double)data_array[run_index].sec + 
		    (double)data_array[run_index].usec / 1000000;
		tmp_time2 = tmp_time2 - first_time;
		/* tmp_time2 is end time, counted since first event */
		printf("%f\t%f\n", tmp_time2, (tmp_time2-tmp_time));

		if(tmp_time2 > last_end_time)
		    last_end_time = tmp_time2;


		break;
	    }
	}
	if(run_index == array_size)
	{
	    /* printf("lost end time.\n"); */
	}
    }

    printf("#total empty: %f\n", (total_empty));
    printf("#total time: %f\n", tmp_time);
    printf("#percentage idle: %f\n", (100*total_empty/tmp_time));

    free(data_array);
    fclose(infile);

#if 0
    printf("# (server number) (api) (operation) (value) (id) (flags) (sec) (usec)\n");
    for(i=0; i<io_server_count; i++)
    {
	for(j=0; j<EVENT_DEPTH; j++)
	{
	    if((event_matrix[i][j].flags & PVFS_EVENT_FLAG_INVALID) == 0)
	    {
		printf("%d %d %d %Ld %Ld %d %d %d\n", 
		    i, 
		    (int)event_matrix[i][j].api,
		    (int)event_matrix[i][j].operation,
		    (long long)event_matrix[i][j].value,
		    (long long)event_matrix[i][j].id,
		    (int)event_matrix[i][j].flags,
		    (int)event_matrix[i][j].tv_sec,
		    (int)event_matrix[i][j].tv_usec);
	    }
	}
    }
#endif

    return(ret);
}

static void usage(int argc, char** argv)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage  : %s <event log>\n",
	argv[0]);
    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

