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

#define MAX_TALLY 1000

struct tally
{
    int32_t api;
    int32_t op;
    float min;
    float max;
    float sum;
    int count;
};

int main(int argc, char **argv)
{
    int ret = -1;
    FILE* infile;
    struct tally* tally_array;
    char tmp_buf[512];
    int tally_count = 0;
    int measure_num;
    int server;
    int api;
    int op;
    long long value;
    float start;
    float end;
    float len;
    int line = 1;
    int i;

    if(argc != 2)
	usage(argc, argv);

    infile = fopen(argv[1], "r");
    if(!infile)
    {
	perror("fopen");
	return(-1);
    }

    tally_array = (struct tally*)malloc(MAX_TALLY*sizeof(struct tally));
    if(!tally_array)
    {
	perror("malloc");
	return(-1);
    }
    memset(tally_array, 0, MAX_TALLY*sizeof(struct tally));

    printf("# (api_op) (count) (ave) (min) (max)\n");

    /* pull in all of the data */
    while(fgets(tmp_buf, 512, infile))
    {
	/* skip comments */
	if(tmp_buf[0] == '#')
	    continue;

	/* read in data line */
	ret = sscanf(tmp_buf, "%d\t%d\t%d_%d\t%lld\t%f\t%f\t%f\n",
	    &measure_num,
	    &server,
	    &api,
	    &op,
	    &value,
	    &start,
	    &end,
	    &len);
	if(ret != 8)
	{
	    fprintf(stderr, "Parse error, data line %d.\n", (line));
	    return(-1);
	}
	line++;

	/* look for a tally for this type of op */
	for(i=0; i<tally_count; i++)
	{
	    if(tally_array[i].op == op && tally_array[i].api == api)
		break;
	}
	if(i >= tally_count)
	{
	    /* no match, start a new tally */
	    tally_array[i].op = op;
	    tally_array[i].api = api;
	    tally_count++;
	}

	/* add into statistics */
	if(tally_array[i].min == 0 || tally_array[i].min > len)
	    tally_array[i].min = len;
	if(tally_array[i].max == 0 || tally_array[i].max < len)
	    tally_array[i].max = len;
	tally_array[i].sum += len;
	tally_array[i].count++;
    }

    /* print out results */
    for(i=0; i<tally_count; i++)
    {
	printf("%d_%d\t%d\t%f\t%f\t%f\n",
	    tally_array[i].api,
	    tally_array[i].op,
	    tally_array[i].count,
	    (tally_array[i].sum / (float)tally_array[i].count),
	    tally_array[i].min,
	    tally_array[i].max);
    }

    return(0);
}

static void usage(int argc, char** argv)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage  : %s <parsed event log>\n",
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

