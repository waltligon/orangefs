/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <pvfs2-types.h>
#include <gossip.h>
#include <pvfs2-debug.h>
#include <pvfs-distribution.h>
#include <pvfs2-request.h>
#include <pint-request.h>
#include <simple-stripe.h>

#define SEGMAX 16
#define BYTEMAX (1024*1024)

void Dump_request(PVFS_Request req);

int main(int argc, char **argv)
{
	PINT_Request *r_dec;
	int ret = -1;
	int fd = -1;
	PINT_Request_state *rs1;
	PINT_Request_file_data rf1;
	PINT_Request_result seg1;
	int retval;

	gossip_enable_stderr();
	gossip_set_debug_mask(0,0);

	fd = open("enc_tmp.dat", O_RDONLY);
	if(fd < 0)
	{
	    perror("open");
	    return(-1);
	}

	/* just make this kindof big, I dunno how large the type is */
	r_dec = (PINT_Request*)malloc(16*1024);
	assert(r_dec);

	ret = read(fd, r_dec, (16*1024));
	if(ret < 0)
	{
	    perror("read");
	    return(-1);
	}
	
	ret = PINT_Request_decode(r_dec);
	if(ret < 0)
	{
		fprintf(stderr, "PINT_Request_decode() failure.\n");
		return(-1);
	}
	fprintf(stderr, "decode returns %d\n", ret);

	/* set up request state */
	rs1 = PINT_New_request_state(r_dec);

	/* set up file data for request */
	rf1.server_nr = 0;
	rf1.server_ct = 1;
	rf1.fsize = 508;
	rf1.dist = PVFS_Dist_create("simple_stripe");
	rf1.extend_flag = 1;
	PINT_Dist_lookup(rf1.dist);

	/* set up result struct */
	seg1.offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.bytemax = BYTEMAX;
	seg1.segmax = SEGMAX;
	seg1.bytes = 0;
	seg1.segs = 0;
		
	printf("\n************************************\n");
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		retval = PINT_Process_request(rs1, NULL, &rf1, &seg1, PINT_CKSIZE_MODIFY_OFFSET);

		if(retval >= 0)
		{
			printf("results of PINT_Process_request():\n");
			printf("%d segments with %lld bytes\n", seg1.segs, Ld(seg1.bytes));
#if 0
			for(i=0; i<seg1.segs; i++)
			{
				printf("  segment %d: offset: %d size: %d\n",
					i, (int)seg1.offset_array[i], (int)seg1.size_array[i]);
			}
#endif
		}

	} while(!PINT_REQUEST_DONE(rs1) && retval >= 0);
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_Process_request() failure.\n");
		return(-1);
	}
	if(PINT_REQUEST_DONE(rs1))
	{
		printf("**** request done.\n");
	}

	close(fd);

	/* do another one */

	fd = open("enc_tmp2.dat", O_RDONLY);
	if(fd < 0)
	{
	    perror("open");
	    return(-1);
	}

	/* just make this kindof big, I dunno how large the type is */
	r_dec = (PINT_Request*)malloc(16*1024);
	assert(r_dec);

	ret = read(fd, r_dec, (16*1024));
	if(ret < 0)
	{
	    perror("read");
	    return(-1);
	}
	
	ret = PINT_Request_decode(r_dec);
	if(ret < 0)
	{
		fprintf(stderr, "PINT_Request_decode() failure.\n");
		return(-1);
	}
	fprintf(stderr, "decode returns %d\n", ret);

	/* set up request state */
	rs1 = PINT_New_request_state(r_dec);

	/* set up file data for request */
	rf1.server_nr = 0;
	rf1.server_ct = 1;
	rf1.fsize = 508;
	rf1.dist = PVFS_Dist_create("simple_stripe");
	rf1.extend_flag = 1;
	PINT_Dist_lookup(rf1.dist);

	/* set up result struct */
	seg1.offset_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.size_array = (int64_t *)malloc(SEGMAX * sizeof(int64_t));
	seg1.bytemax = BYTEMAX;
	seg1.segmax = SEGMAX;
	seg1.bytes = 0;
	seg1.segs = 0;
		
	printf("\n************************************\n");
	do
	{
		seg1.bytes = 0;
		seg1.segs = 0;

		/* process request */
		retval = PINT_Process_request(rs1, NULL, &rf1, &seg1, PINT_CKSIZE_MODIFY_OFFSET);

		if(retval >= 0)
		{
			printf("results of PINT_Process_request():\n");
			printf("%d segments with %lld bytes\n", seg1.segs, Ld(seg1.bytes));
#if 0
			for(i=0; i<seg1.segs; i++)
			{
				printf("  segment %d: offset: %d size: %d\n",
					i, (int)seg1.offset_array[i], (int)seg1.size_array[i]);
			}
#endif
		}

	} while(!PINT_REQUEST_DONE(rs1) && retval >= 0);
	
	if(retval < 0)
	{
		fprintf(stderr, "Error: PINT_Process_request() failure.\n");
		return(-1);
	}
	if(PINT_REQUEST_DONE(rs1))
	{
		printf("**** request done.\n");
	}

	close(fd);


	return 0;
}

void Dump_request(PVFS_Request req)
{
	fprintf(stderr,"**********************\n");
	fprintf(stderr,"address:\t%p\n",req);
	fprintf(stderr,"offset:\t\t%d\n",(int)req->offset);
	fprintf(stderr,"num_ereqs:\t%d\n",(int)req->num_ereqs);
	fprintf(stderr,"num_blocks:\t%d\n",(int)req->num_blocks);
	fprintf(stderr,"stride:\t\t%d\n",(int)req->stride);
	fprintf(stderr,"ub:\t\t%d\n",(int)req->ub);
	fprintf(stderr,"lb:\t\t%d\n",(int)req->lb);
	fprintf(stderr,"agg_size:\t%d\n",(int)req->aggregate_size);
	fprintf(stderr,"num_chunk:\t%d\n",(int)req->num_contig_chunks);
	fprintf(stderr,"depth:\t\t%d\n",(int)req->depth);
	fprintf(stderr,"num_nest:\t%d\n",(int)req->num_nested_req);
	fprintf(stderr,"commit:\t\t%d\n",(int)req->committed);
	fprintf(stderr,"refcount:\t\t%d\n",(int)req->refcount);
	fprintf(stderr,"ereq:\t\t%p\n",req->ereq);
	fprintf(stderr,"sreq:\t\t%p\n",req->sreq);
	fprintf(stderr,"**********************\n");
}

