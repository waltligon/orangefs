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

#include "pint-distribution.h"
#include "pint-dist-utils.h"
#include "pvfs2-request.h"
#include "pint-request.h"
#include "pvfs2-dist-simple-stripe.h"

#define NMBLK1 1
#define NMBLK2 1

int PINT_req_overlap(PVFS_offset r1offset, struct PINT_Request *r1,
		      PVFS_offset r2offset, struct PINT_Request *r2);

int main(int argc, char **argv)
{
	PINT_Request *r1, *r1a;
	PVFS_offset displacement1[NMBLK1];
	int32_t blocklength1[NMBLK1];
	PINT_Request *r2, *r2a;
	PVFS_offset displacement2[NMBLK2];
	int32_t blocklength2[NMBLK2];
	int retval;

	/* Turn on debugging */
	gossip_enable_stderr();
	gossip_set_debug_mask(1,GOSSIP_REQUEST_DEBUG);


	/* two identical indexed of size 1 */
	blocklength1[0] = 10*1024; /* 10K */
	displacement1[0] = 0;
	PVFS_Request_indexed(NMBLK1, blocklength1, displacement1, PVFS_BYTE, &r1);

	blocklength2[0] = 10*1024; /* 10K */
	displacement2[0] = 0;
	PVFS_Request_indexed(NMBLK2, blocklength2, displacement2, PVFS_BYTE, &r2);

	retval = PINT_req_overlap(0, r1, 0, r2);
	printf("Return value is %d should be 1\n", retval);

	/* two different indexed of size 1 */
	blocklength1[0] = 10*1024; /* 10K */
	displacement1[0] = 0;
	PVFS_Request_indexed(NMBLK1, blocklength1, displacement1, PVFS_BYTE, &r1);

	blocklength2[0] = 10*1024; /* 10K */
	displacement2[0] = 12*1024;
	PVFS_Request_indexed(NMBLK2, blocklength2, displacement2, PVFS_BYTE, &r2);

	retval = PINT_req_overlap(0, r1, 0, r2);
	printf("Return value is %d should be 0\n", retval);

	/* two different indexed of size 1 */
	blocklength1[0] = 10*1024; /* 10K */
	displacement1[0] = 12*1024;
	PVFS_Request_indexed(NMBLK1, blocklength1, displacement1, PVFS_BYTE, &r1);

	blocklength2[0] = 10*1024; /* 10K */
	displacement2[0] = 1*1024;
	PVFS_Request_indexed(NMBLK2, blocklength2, displacement2, PVFS_BYTE, &r2);

	retval = PINT_req_overlap(0, r1, 0, r2);
	printf("Return value is %d should be 0\n", retval);

	/* two different indexed of size 1 */
	blocklength1[0] = 10*1024; /* 10K */
	displacement1[0] = 1*1024;
	PVFS_Request_indexed(NMBLK1, blocklength1, displacement1, PVFS_BYTE, &r1);

	blocklength2[0] = 4*1024; /* 10K */
	displacement2[0] = 3*1024;
	PVFS_Request_indexed(NMBLK2, blocklength2, displacement2, PVFS_BYTE, &r2);

	retval = PINT_req_overlap(0, r1, 0, r2);
	printf("Return value is %d should be 1\n", retval);

	/* two identical vector of size 1000 */
	PVFS_Request_vector(1000, 100, 200, PVFS_BYTE, &r1);

	PVFS_Request_vector(1000, 100, 200, PVFS_BYTE, &r2);

	retval = PINT_req_overlap(0, r1, 0, r2);
	printf("Return value is %d should be 1\n", retval);

	/* two different vector of size 1000 */
	PVFS_Request_vector(1000, 200, 300, PVFS_BYTE, &r1);

	PVFS_Request_vector(500, 100, 200, PVFS_BYTE, &r2);

	retval = PINT_req_overlap(0, r1, 0, r2);
	printf("Return value is %d should be 1\n", retval);

	/* two different vector of size 1000 */
	PVFS_Request_vector(1000, 200, 300, PVFS_BYTE, &r1);

	PVFS_Request_vector(500, 100, 200, PVFS_BYTE, &r2);

	retval = PINT_req_overlap(0, r1, 800, r2);
	printf("Return value is %d should be 1\n", retval);

	/* two different vector of size 1000 */
	PVFS_Request_vector(1000, 200, 300, PVFS_BYTE, &r1);

	PVFS_Request_vector(500, 100, 200, PVFS_BYTE, &r2);

	retval = PINT_req_overlap(800, r1, 0, r2);
	printf("Return value is %d should be 1\n", retval);

	/* two different vector of size 1000 */
	PVFS_Request_vector(1000, 175, 300, PVFS_BYTE, &r1);

	PVFS_Request_vector(1000, 75, 300, PVFS_BYTE, &r2);

	retval = PINT_req_overlap(0, r1, 200, r2);
	printf("Return value is %d should be 0\n", retval);

	/* two different vector of size 1000 */
	PVFS_Request_vector(1000, 200, 300, PVFS_BYTE, &r1);

	PVFS_Request_vector(1000, 100, 300, PVFS_BYTE, &r2);

	retval = PINT_req_overlap(0, r1, 200, r2);
	printf("Return value is %d should be 0\n", retval);

	exit(0);
}
