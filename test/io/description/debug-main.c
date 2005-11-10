/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pvfs2-types.h>
#include <gossip.h>
#include <pvfs2-debug.h>

#include <pint-distribution.h>
#include <pint-dist-utils.h>
#include <pvfs2-request.h>
#include <pint-request.h>

#define SEGMAX 16
#define BYTEMAX (4*1024*1024)

int longflag = 0;
int gossipflag = 0;

extern int request_debug(void);

int main(int argc, char **argv)
{
	int c;
	while ((c = getopt(argc, argv, "lg")) != -1)
	{
		switch (c)
		{
			case 'l' : longflag++;
				break;
			case 'g' : gossipflag++;
				break;
			default :
				break;
		}
	}
	return request_debug();
}

void prtval(long long val, char *s)
{
	if (!longflag)
		return;
	printf("%s: ",s);
	printf("%lld\n",val);
}

void cmpval(long long val, long long exp)
{
	if (val != exp)
		printf("TEST FAILED! <<=============================\n");
	else
		printf("TEST SUCCEEDED!\n");
}

void prtseg(PINT_Request_result *seg, char *s)
{
	int i;
	if (!longflag)
		return;
	printf("%s\n",s);
	printf("%d segments with %lld bytes\n", seg->segs, lld(seg->bytes));
	for(i=0; i<seg->segs && i<seg->segmax; i++)
	{
		printf("  segment %d: offset: %lld size: %lld\n",
				i, lld(seg->offset_array[i]), lld(seg->size_array[i]));
	}
}

void cmpseg(PINT_Request_result *seg, PINT_Request_result *exp)
{
	int i;
	if (seg->segs != exp->segs || seg->bytes != exp->bytes)
	{
		printf("TEST FAILED! <<=============================\n");
		return;
	}
	for(i=0; i<seg->segs && i<seg->segmax; i++)
	{
		if (seg->offset_array[i] != exp->offset_array[i] ||
				seg->size_array[i] != exp->size_array[i])
		{
			printf("TEST FAILED! <<=============================\n");
			return;
		}
	}
	printf("TEST SUCCEEDED!\n");
}
