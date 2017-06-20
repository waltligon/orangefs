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

void PINT_Request_dump(PVFS_Request req)
{
	printf("Request Dump:\n");
	printf("\toffset: %lld\n",req->offset);
	printf("\tlb: %lld\n",req->lb);
	printf("\tub: %lld\n",req->ub);
	printf("\textent: %lld\n",req->ub - req->lb);
	printf("\tagg size: %lld\n",req->aggregate_size);
	printf("\tcontig chunks: %d\n",req->num_contig_chunks);
	printf("\n\n");
}

int main(int argc, char **argv)
{
	PVFS_Request r1a;
	PVFS_Request r1;
	PVFS_Request r2a;
	PVFS_Request r2;
	PVFS_Request r3a;
	PVFS_Request r3;
	PVFS_Request r4a;
	PVFS_Request r4;
	PVFS_Request r5a;
	PVFS_Request r5b;
	PVFS_Request r5;

	// PVFS_Process_request arguments
	int32_t blocklength [] = {24};
	PVFS_offset displacement [] = {1024};

   // Turn on debugging
	// gossip_enable_stderr();
	// gossip_set_debug_mask(1,REQUEST_DEBUG);

	PVFS_Request_resized(PVFS_INT, 0, -16, &r1a);
	PVFS_Request_hindexed(1, blocklength, displacement, r1a, &r1);
	PINT_Request_dump(r1);

	PVFS_Request_resized(PVFS_INT, 0, 16, &r2a);
	PINT_Request_dump(r2a);
	PVFS_Request_hindexed(1, blocklength, displacement, r2a, &r2);
	PINT_Request_dump(r2);

	PVFS_Request_resized(PVFS_INT, 12, 16, &r3a);
	PINT_Request_dump(r3a);
	PVFS_Request_hindexed(1, blocklength, displacement, r3a, &r3);
	PINT_Request_dump(r3);

	PVFS_Request_resized(PVFS_INT, 12, 4, &r4a);
	PINT_Request_dump(r4a);
	PVFS_Request_hindexed(1, blocklength, displacement, r4a, &r4);
	PINT_Request_dump(r4);

	PVFS_Request_vector(2, 1, 2, PVFS_INT, &r5a);
	PINT_Request_dump(r5a);
	PVFS_Request_resized(r5a, 0, 8, &r5b);
	PINT_Request_dump(r5b);
	PVFS_Request_hindexed(1, blocklength, displacement, r5b, &r5);
	PINT_Request_dump(r5);

	exit(0);
}
