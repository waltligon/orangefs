/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

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

int result1 [] = {
	0, 65536,
	196608, 65536,
	393216, 65536,
	589824, 65536,
	786432, 65536,
	983040, 65536,
	1179648, 65536,
	1376256, 65536,
	1572864, 65536,
	1769472, 65536,
	1966080, 65536,
	2162688, 65536,
	2359296, 65536,
	2555904, 65536,
	2752512, 65536,
	2949120, 65536,
	-1
};

int result2 [] = {
	3145728, 65536,
	3342336, 65536,
	3538944, 65536,
	3735552, 65536,
	3932160, 65536,
	4128768, 65536,
	4325376, 65536,
	4521984, 65536,
	4718592, 65536,
	4915200, 65536,
	5111808, 65536,
	5308416, 65536,
	5505024, 65536,
	5701632, 65536,
	5898240, 65536,
	6094848, 65536,
	-1
};

int result3 [] = {
	6291456, 65536,
	6488064, 65536,
	6684672, 65536,
	6881280, 65536,
	7077888, 65536,
	7274496, 65536,
	7471104, 65536,
	7667712, 65536,
	7864320, 65536,
	8060928, 65536,
	8257536, 65536,
	8454144, 65536,
	8650752, 65536,
	8847360, 65536,
	9043968, 65536,
	9240576, 65536,
	-1
};

int result4 [] = {
	9437184, 65536,
	9633792, 65536,
	9830400, 65536,
	10027008, 65536,
	10223616, 65536,
	-1
};

void prtres(int *result)
{  
	int *p = result;
	printf("Result should be:\n");
	while (*p != -1)
	{
	   printf("\t%d\t%d\n",*p, *(p+1));
	   p+=2; 
	}
}   

int main(int argc, char **argv)
{
	int i;
	PINT_Request *r1;
	PINT_Request *r2;
	PINT_Request_state *rs1;
	PINT_Request_state *rs2;
	PINT_Request_file_data rf1;
	PINT_Request_file_data rf2;
	PINT_Request_result seg1;

	/* PVFS_Process_request arguments */
	int retval;

	int32_t blocklength = 10*1024*1024; /* 10M */

	/* set up two requests, both at offset 0 */
	PVFS_size displacement = 0;  /* first at offset zero */
	PVFS_Request_indexed(1, &blocklength, &displacement, PVFS_BYTE, &r1);
	PVFS_Request_indexed(1, &blocklength, &displacement, PVFS_BYTE, &r2);

	/* set up two request states */
	rs1 = PINT_New_request_state(r1);
	rs2 = PINT_New_request_state(r2);

	/* set up file data for first request */
	PINT_dist_initialize();
	rf1.server_nr = 0;
	rf1.server_ct = 3;
	rf1.fsize = 8454144;
	rf1.dist = PINT_dist_create("simple_stripe");
	rf1.extend_flag = 0;
	PINT_dist_lookup(rf1.dist);

	/* file data for second request is the same, except the file
	 * will have grown by 10M 
	 */
	rf2.server_nr = 0;
	rf2.server_ct = 3;
	rf2.fsize = 8454144;
	rf2.dist = PINT_dist_create("simple_stripe");
	rf2.extend_flag = 0;
	PINT_dist_lookup(rf2.dist);

	/* set up result struct */
	seg1.offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.bytemax = BYTEMAX;
	seg1.segmax = SEGMAX;
	seg1.bytes = 0;
	seg1.segs = 0;

   /* Turn on debugging */
	// gossip_enable_stderr(); 
	// gossip_set_debug_mask(1,REQUEST_DEBUG); 

	printf("\n************************************\n");
	printf("Two requests 10M each contiguous from offset 0 server 0 of 3\n");
	printf("Simple stripe, default stripe size (64K)\n");
	printf("First in SERVER mode, file size 8454144, no extend flag\n");
	printf("\n************************************\n");

	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		retval = PINT_Process_request(rs1, NULL, &rf1, &seg1, PINT_SERVER);

		if(retval >= 0)
		{
			printf("results of PINT_Process_request(PINT_SERVER):\n");
			printf("%d segments with %lld bytes\n", seg1.segs, Ld(seg1.bytes));
			for(i=0; i<seg1.segs; i++)
			{
				printf("  segment %d: offset: %d size: %d\n",
					i, (int)seg1.offset_array[i], (int)seg1.size_array[i]);
			}
		}

	} while(!PINT_REQUEST_DONE(rs1) && retval >= 0);
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_Process_request() failure.\n");
		return(-1);
	}
	if(PINT_REQUEST_DONE(rs1))
	{
		printf("**** first request done.\n");
		printf("Result should be:\n\t0\t3538944\n");
	}

	printf("\n************************************\n");
	printf("Second in CLIENT mode, file size 8454144, no extend flag\n");
	printf("\n************************************\n");
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		retval = PINT_Process_request(rs2, NULL, &rf2, &seg1, PINT_CLIENT);

		if(retval >= 0)
		{
			printf("results of PINT_Process_request(PINT_CLIENT):\n");
			printf("%d segments with %lld bytes\n", seg1.segs, Ld(seg1.bytes));
			for(i=0; i<seg1.segs; i++)
			{
				printf("  segment %d: offset: %d size: %d\n",
					i, (int)seg1.offset_array[i], (int)seg1.size_array[i]);
			}
		}

	} while(!PINT_REQUEST_DONE(rs2) && retval >= 0);
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_Process_request() failure.\n");
		return(-1);
	}
	if(PINT_REQUEST_DONE(rs2))
	{
		printf("**** second request done.\n");
		prtres(result1);
		prtres(result2);
		prtres(result3);
		prtres(result4);
	}

	return 0;
}
