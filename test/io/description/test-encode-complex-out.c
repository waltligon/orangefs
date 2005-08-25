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
#include <pint-distribution.h>
#include <pint-dist-utils.h>
#include <pvfs2-request.h>
#include <pint-request.h>

#define SEGMAX 16
#define BYTEMAX (1024*1024)

void Dump_request(PVFS_Request req);

int main(int argc, char **argv)
{
	PINT_Request *r;
	PINT_Request *r1;
	PINT_Request *r_enc;
	int ret = -1;
	int pack_size = 0;
	int fd = -1;
	PINT_Request_state *rs1;
	PINT_request_file_data rf1;
	PINT_Request_result seg1;
	int retval;
	int i;
	int count = 64;
	int32_t* len_array;
	PVFS_offset* disp_array;

	len_array = (int32_t*)malloc(count*sizeof(int32_t));
	assert(len_array);
	disp_array = (PVFS_offset*)malloc(count*sizeof(PVFS_offset));
	assert(disp_array);

	for(i=0; i<count; i++)
	{
	    len_array[i] = 4;
	    disp_array[i] = i*8;
	}

	fd = open("enc_tmp.dat", O_WRONLY|O_CREAT|O_TRUNC, S_IRWXU);
	if(fd < 0)
	{
	    perror("open");
	    return(-1);
	}
	
	r = PVFS_BYTE;

	PVFS_Request_hindexed(64, len_array, disp_array, r, &r1);

	/* set up request state */
	rs1 = PINT_new_request_state(r1);

	/* set up file data for request */
	PINT_dist_initialize(NULL);
	rf1.server_nr = 0;
	rf1.server_ct = 4;
	rf1.fsize = 6000;
	rf1.dist = PINT_dist_create("simple_stripe");
	rf1.extend_flag = 0;
	PINT_dist_lookup(rf1.dist);

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
		retval = PINT_process_request(rs1, NULL, &rf1, &seg1, PINT_CLIENT);

		if(retval >= 0)
		{
			printf("results of PINT_process_request():\n");
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
		fprintf(stderr, "Error: PINT_process_request() failure.\n");
		return(-1);
	}
	if(PINT_REQUEST_DONE(rs1))
	{
		printf("**** request done.\n");
	}


	

	/* allocate a new request and pack the original one into it */
	pack_size = PINT_REQUEST_PACK_SIZE(r1);
	fprintf(stderr, "pack size is %d\n",pack_size);
	r_enc = (PINT_Request*)malloc(pack_size);
	assert(r_enc != NULL);

	ret = PINT_request_commit(r_enc, r1);
	if(ret < 0)
	{
		fprintf(stderr, "PINT_Request_commit() failure.\n");
		return(-1);
	}
	fprintf(stderr, "commit returns %d\n", ret);
	ret = PINT_request_encode(r_enc);
	if(ret < 0)
	{
		fprintf(stderr, "PINT_Request_encode() failure.\n");
		return(-1);
	}
	fprintf(stderr, "encode returns %d\n", ret);

	ret = write(fd, r_enc, pack_size);
	if(ret < 0)
	{
	    perror("write");
	    return(-1);
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

