#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "bmi.h"
#include "gossip.h"
#include "pvfs2-req-proto.h"
#include "print-struct.h"
#include "PINT-reqproto-encode.h"
#include "pvfs2-request.h"
#include "pint-request.h"
#include "pvfs-distribution.h"
#include "pint-distribution.h"
#include "flow.h"


/*
 * This is a test program for playing around with I/O operations on the
 * pvfs2 server.  If executed without any command line arguments, it
 * will create a new datafile, write something into it, then delete the
 * file.
 *
 */

/**************************************************************
 * Data structures 
 */

/* A little structure to hold program options, either defaults or
 * specified on the command line 
 */
struct options{
	char* hostid;       /* host identifier */
	char* method;       /* bmi method to use */
	int handle;
};


/**************************************************************
 * Internal utility functions
 */

static struct options* parse_args(int argc, char* argv[]);
static int block_on_flow(flow_descriptor* flow_d, FLOW_context_id
	flow_context);


/**************************************************************/

int main(int argc, char **argv)	{

	struct options* user_opts = NULL;
	int ret = -1;
	bmi_addr_t server_addr;
	bmi_op_id_t client_ops[2];
	int outcount = 0;
	bmi_error_code_t error_code;
	bmi_size_t actual_size;
	struct PINT_encoded_msg encoded1;
	struct PINT_decoded_msg decoded1;
	struct PINT_encoded_msg encoded2;
	struct PINT_decoded_msg decoded2;
	struct PVFS_server_req my_req;
	struct PVFS_server_resp* io_dec_ack;
	struct PVFS_server_resp* create_dec_ack;
	void* my_ack, *create_ack, *remove_ack;
	int my_ack_size, create_ack_size, remove_ack_size;
	PVFS_size io_size = 10 * 1024 * 1024;
	void* memory_buffer = NULL;
	flow_descriptor* flow_d = NULL;
	struct PVFS_server_resp* remove_dec_ack;
	struct PINT_encoded_msg encoded3;
	struct PINT_decoded_msg decoded3;
	bmi_context_id context;
	FLOW_context_id flow_context;
        PVFS_handle_extent cur_extent;

	/**************************************************
	 * general setup 
	 */

	/* figure out how big of an ack to post */
	my_ack_size = PINT_encode_calc_max_size(PINT_ENCODE_RESP, 
	    PVFS_SERV_IO, PINT_ENC_DIRECT);
	create_ack_size = PINT_encode_calc_max_size(PINT_ENCODE_RESP, 
	    PVFS_SERV_CREATE, PINT_ENC_DIRECT);
	remove_ack_size = PINT_encode_calc_max_size(PINT_ENCODE_RESP, 
	    PVFS_SERV_REMOVE, PINT_ENC_DIRECT);

	/* create storage for all of the acks */
	my_ack = malloc(my_ack_size);
	if(!my_ack)
		return(-errno);
	create_ack = malloc(create_ack_size);
	if(!create_ack)
		return(-errno);
	remove_ack = malloc(remove_ack_size);
	if(!remove_ack)
		return(-errno);

	/* create a memory buffer to write from */
	memory_buffer = malloc(io_size);
	if(!memory_buffer)
	{
		fprintf(stderr, "Error: malloc() failure.\n");
		return(-1);
	}
	memset(memory_buffer, 0, io_size);

	/* grab any command line options */
	user_opts = parse_args(argc, argv);
	if(!user_opts){
		return(-1);
	}

	/* set debugging stuff */
	gossip_enable_stderr();
	gossip_set_debug_mask(0, 0);

	ret = PINT_encode_initialize();
	if(ret < 0)
	{
	    fprintf(stderr, "PINT_encode_initialize failure.\n");
	    return(-1);
	}

	/* initialize local interface */
	ret = BMI_initialize(user_opts->method, NULL, 0);
	if(ret < 0){
		fprintf(stderr, "Error: BMI_initialize() failure.\n");
		return(-1);
	}

	ret = BMI_open_context(&context);
	if(ret < 0)
	{
		fprintf(stderr, "BMI_open_context() failure.\n");
		return(-1);
	}

	/* intialize flow interface */
	ret = PINT_flow_initialize("flowproto_bmi_trove", 0);
	if(ret < 0)
	{
		fprintf(stderr, "Error: flow_initialize() failure.\n");
		return(-1);
	}

	ret = PINT_flow_open_context(&flow_context);
	if(ret < 0)
	{
		fprintf(stderr, "PINT_flow_open_context() failure.\n");
		return(-1);
	}


	/* get a bmi_addr for the server */
	ret = BMI_addr_lookup(&server_addr, user_opts->hostid);
	if(ret < 0){
		errno = -ret;
		perror("BMI_addr_lookup");
		return(-1);
	}

	/**************************************************
	 * create request (create a data file to operate on) 
	 */
	my_req.op = PVFS_SERV_CREATE;
	my_req.credentials.uid = 0;
	my_req.credentials.gid = 0;

	/* create specific fields */
        cur_extent.first = cur_extent.last = 4095;
        my_req.u.create.handle_extent_array.extent_count = 1;
        my_req.u.create.handle_extent_array.extent_array = &cur_extent;
	my_req.u.create.fs_id = 9;
	my_req.u.create.object_type = PVFS_TYPE_DATAFILE;

	ret = PINT_encode(&my_req,PINT_ENCODE_REQ,&encoded1,server_addr,0);
	if(ret < 0)
	{
		fprintf(stderr, "Error: PINT_encode failure.\n");
		return(-1);
	}

	/* send the request on its way */
	ret = BMI_post_sendunexpected_list(
		&(client_ops[1]), 
		encoded1.dest,
		encoded1.buffer_list, 
		encoded1.size_list,
		encoded1.list_count,
		encoded1.total_size, 
		encoded1.buffer_type, 
		0, 
		NULL,
		context);
	if(ret < 0)
	{
		errno = -ret;
		perror("BMI_post_send");
		return(-1);
	}
	if(ret == 0)
	{
		/* turning this into a blocking call for testing :) */
		/* check for completion of request */
		do
		{
			ret = BMI_test(client_ops[1], &outcount, &error_code, &actual_size,
				NULL, 10, context);
		} while(ret == 0 && outcount == 0);

		if(ret < 0 || error_code != 0)
		{
			fprintf(stderr, "Error: request send failed.\n");
			if(ret<0)
			{
				errno = -ret;
				perror("BMI_test");
			}
			return(-1);
		}
	}

	/* release the encoded message */
	PINT_encode_release(&encoded1, PINT_ENCODE_REQ);

	/* post a recv for the server acknowledgement */
	ret = BMI_post_recv(&(client_ops[0]), server_addr, create_ack, 
		create_ack_size, &actual_size, BMI_EXT_ALLOC, 0, 
		NULL, context);
	if(ret < 0)
	{
		errno = -ret;
		perror("BMI_post_recv");
		return(-1);
	}
	if(ret == 0)
	{
		/* turning this into a blocking call for testing :) */
		/* check for completion of ack recv */
		do
		{
			ret = BMI_test(client_ops[0], &outcount, &error_code,
				&actual_size, NULL, 10, context);
		} while(ret == 0 && outcount == 0);

		if(ret < 0 || error_code != 0)
		{
			fprintf(stderr, "Error: ack recv.\n");
			fprintf(stderr, "   ret: %d, error code: %d\n",ret,error_code);
			return(-1);
		}
	}
	else
	{
		if(actual_size != create_ack_size)
		{
			printf("Error: short recv.\n");
			return(-1);
		}
	}
				
	/* look at the ack */
	ret = PINT_decode(
		create_ack,
		PINT_ENCODE_RESP,
		&decoded1,
		server_addr,
		actual_size);
	if(ret < 0)
	{
		fprintf(stderr, "Error: PINT_decode() failure.\n");
		return(-1);
	}

	create_dec_ack = decoded1.buffer;
	if(create_dec_ack->op != PVFS_SERV_CREATE)
	{
		fprintf(stderr, "ERROR: received ack of wrong type (%d)\n", 
			(int)create_dec_ack->op);
		return(-1);
	}
	if(create_dec_ack->status != 0)
	{
		fprintf(stderr, "ERROR: server returned status: %d\n",
			(int)create_dec_ack->status);
		return(-1);
	}
	


	/**************************************************
	 * io request  
	 */

	/* request */
	my_req.op = PVFS_SERV_IO;
	my_req.credentials.uid = 0;
	my_req.credentials.gid = 0;

	/* io specific fields */
	my_req.u.io.fs_id = 9;
	my_req.u.io.handle = create_dec_ack->u.create.handle;
	my_req.u.io.iod_num = 0;
	my_req.u.io.iod_count = 1;
	my_req.u.io.io_type = PVFS_IO_WRITE;
	my_req.u.io.io_dist = PVFS_Dist_create("default_dist");
	if(!my_req.u.io.io_dist)
	{
		fprintf(stderr, "Error: failed to create dist.\n");
		return(-1);
	}
	ret = PINT_Dist_lookup(my_req.u.io.io_dist);
	if(ret != 0)
	{
		fprintf(stderr, "Error: failed to lookup dist.\n");
		return(-1);
	}
	ret = PVFS_Request_contiguous(io_size, PVFS_BYTE,
		&(my_req.u.io.file_req));
	if(ret < 0)
	{
		fprintf(stderr, "Error: PVFS_Request_contiguous() failure.\n");
		return(-1);
	}

	ret = PINT_encode(&my_req,PINT_ENCODE_REQ,&encoded2,server_addr,0);
	if(ret < 0)
	{
		fprintf(stderr, "Error: PINT_encode failure.\n");
		return(-1);
	}

	/* send the request on its way */
	ret = BMI_post_sendunexpected_list(
		&(client_ops[1]), 
		encoded2.dest,
		encoded2.buffer_list,
		encoded2.size_list,
		encoded2.list_count,
		encoded2.total_size,
		encoded2.buffer_type,
		0,
		NULL,
		context);
	if(ret < 0)
	{
		errno = -ret;
		perror("BMI_post_send");
		return(-1);
	}
	if(ret == 0)
	{
		/* turning this into a blocking call for testing :) */
		/* check for completion of request */
		do
		{
			ret = BMI_test(client_ops[1], &outcount, &error_code, &actual_size,
				NULL, 10, context);
		} while(ret == 0 && outcount == 0);

		if(ret < 0 || error_code != 0)
		{
			fprintf(stderr, "Error: request send failed.\n");
			if(ret<0)
			{
				errno = -ret;
				perror("BMI_test");
			}
			return(-1);
		}
	}

	/* release the encoded message */
	PINT_encode_release(&encoded2, PINT_ENCODE_REQ);

	/* post a recv for the server acknowledgement */
	ret = BMI_post_recv(&(client_ops[0]), server_addr, my_ack, 
		my_ack_size, &actual_size, BMI_EXT_ALLOC, 0, 
		NULL, context);
	if(ret < 0)
	{
		errno = -ret;
		perror("BMI_post_recv");
		return(-1);
	}
	if(ret == 0)
	{
		/* turning this into a blocking call for testing :) */
		/* check for completion of ack recv */
		do
		{
			ret = BMI_test(client_ops[0], &outcount, &error_code,
				&actual_size, NULL, 10, context);
		} while(ret == 0 && outcount == 0);

		if(ret < 0 || error_code != 0)
		{
			fprintf(stderr, "Error: ack recv failed.\n");
			fprintf(stderr, "   ret: %d, error code: %d\n",ret,error_code);
			return(-1);
		}
	}
	else
	{
		if(actual_size != my_ack_size)
		{
			printf("Short recv.\n");
			return(-1);
		}
	}
		
	/* look at the ack */
	ret = PINT_decode(
		my_ack,
		PINT_ENCODE_RESP,
		&decoded2,
		server_addr,
		actual_size);
	if(ret < 0)
	{
		fprintf(stderr, "Error: PINT_decode() failure.\n");
		return(-1);
	}

	io_dec_ack = decoded2.buffer;
	if(io_dec_ack->op != PVFS_SERV_IO)
	{
		fprintf(stderr, "ERROR: received ack of wrong type (%d)\n", 
			(int)io_dec_ack->op);
		return(-1);
	}
	if(io_dec_ack->status != 0)
	{
		fprintf(stderr, "ERROR: server returned status: %d\n",
			(int)io_dec_ack->status);
		return(-1);
	}
	
	printf("Datafile size before I/O: %d\n",
		(int)io_dec_ack->u.io.bstream_size);

	/**************************************************
	 * flow 
	 */

	flow_d = PINT_flow_alloc();
	if(!flow_d)
	{
		fprintf(stderr, "Error: PINT_flow_alloc() failure.\n");
		return(-1);
	}

	flow_d->file_data.fsize = io_dec_ack->u.io.bstream_size;
	flow_d->file_data.iod_num = 0;
	flow_d->file_data.iod_count = 1;
	flow_d->file_data.dist = my_req.u.io.io_dist;

	flow_d->file_req = my_req.u.io.file_req;
	flow_d->tag = 0;
	flow_d->user_ptr = NULL;

	/* the following section assumes we are doing a write */
	flow_d->file_data.extend_flag = 1;
	flow_d->src.endpoint_id = MEM_ENDPOINT;
	flow_d->src.u.mem.buffer = memory_buffer;
	flow_d->dest.endpoint_id = BMI_ENDPOINT;
	flow_d->dest.u.bmi.address = server_addr;

	ret = block_on_flow(flow_d, flow_context);
	if(ret < 0)
	{
		return(-1);
	}

	printf("Amount of data written: %d\n",
		(int)flow_d->total_transfered);

	PINT_flow_free(flow_d);

	/**************************************************
	 * remove request  
	 */

	my_req.op = PVFS_SERV_REMOVE;
	my_req.credentials.uid = 0;
	my_req.credentials.gid = 0;

	/* remove specific fields */
	my_req.u.remove.fs_id = 9;
	my_req.u.remove.handle = create_dec_ack->u.create.handle;

	ret = PINT_encode(&my_req,PINT_ENCODE_REQ,&encoded3,server_addr,0);
	if(ret < 0)
	{
		fprintf(stderr, "Error: PINT_encode failure.\n");
		return(-1);
	}

	/* send the request on its way */
	ret = BMI_post_sendunexpected_list(
		&(client_ops[1]), 
		encoded3.dest,
		encoded3.buffer_list, 
		encoded3.size_list,
		encoded3.list_count,
		encoded3.total_size, 
		encoded3.buffer_type, 
		0, 
		NULL,
		context);
	if(ret < 0)
	{
		errno = -ret;
		perror("BMI_post_send");
		return(-1);
	}
	if(ret == 0)
	{
		/* turning this into a blocking call for testing :) */
		/* check for completion of request */
		do
		{
			ret = BMI_test(client_ops[1], &outcount, &error_code, &actual_size,
				NULL, 10, context);
		} while(ret == 0 && outcount == 0);

		if(ret < 0 || error_code != 0)
		{
			fprintf(stderr, "Error: request send failed.\n");
			if(ret<0)
			{
				errno = -ret;
				perror("BMI_test");
			}
			return(-1);
		}
	}

	/* release the encoded message */
	PINT_encode_release(&encoded3, PINT_ENCODE_REQ);

	/* post a recv for the server acknowledgement */
	ret = BMI_post_recv(&(client_ops[0]), server_addr, remove_ack, 
		remove_ack_size, &actual_size, BMI_EXT_ALLOC, 0, 
		NULL, context);
	if(ret < 0)
	{
		errno = -ret;
		perror("BMI_post_recv");
		return(-1);
	}
	if(ret == 0)
	{
		/* turning this into a blocking call for testing :) */
		/* check for completion of ack recv */
		do
		{
			ret = BMI_test(client_ops[0], &outcount, &error_code,
				&actual_size, NULL, 10, context);
		} while(ret == 0 && outcount == 0);

		if(ret < 0 || error_code != 0)
		{
			fprintf(stderr, "Error: ack recv.\n");
			fprintf(stderr, "   ret: %d, error code: %d\n",ret,error_code);
			return(-1);
		}
	}
	else
	{
		if(actual_size != remove_ack_size)
		{
			printf("Error: short recv.\n");
			return(-1);
		}
	}
				
	/* look at the ack */
	ret = PINT_decode(
		remove_ack,
		PINT_ENCODE_RESP,
		&decoded3,
		server_addr,
		actual_size);
	if(ret < 0)
	{
		fprintf(stderr, "Error: PINT_decode() failure.\n");
		return(-1);
	}

	remove_dec_ack = decoded3.buffer;
	if(remove_dec_ack->op != PVFS_SERV_REMOVE)
	{
		fprintf(stderr, "ERROR: received ack of wrong type (%d)\n", 
			(int)remove_dec_ack->op);
		return(-1);
	}
	if(remove_dec_ack->status != 0)
	{
		fprintf(stderr, "ERROR: server returned status: %d\n",
			(int)remove_dec_ack->status);
		return(-1);
	}

	/**************************************************
	 * general cleanup  
	 */

	/* release the decoded buffers */
	PINT_decode_release(&decoded1, PINT_ENCODE_RESP);
	PINT_decode_release(&decoded2, PINT_ENCODE_RESP);
	PINT_decode_release(&decoded3, PINT_ENCODE_RESP);

	PINT_flow_close_context(flow_context);
	PINT_flow_finalize();

	/* shutdown the local interface */
	BMI_close_context(context);
	ret = BMI_finalize();
	if(ret < 0){
		errno = -ret;
		perror("BMI_finalize");
		return(-1);
	}

	/* turn off debugging stuff */
	gossip_disable();

	PINT_encode_finalize();

	free(memory_buffer);
	free(my_ack);
	free(create_ack);
	free(remove_ack);
	free(user_opts->hostid);
	free(user_opts->method);
	free(user_opts);

	return(0);
}


static struct options* parse_args(int argc, char* argv[]){

	/* getopt stuff */
	extern char* optarg;
	extern int optind, opterr, optopt;
	char flags[] = "h:m:l:";
	int one_opt = 0;

	struct options* tmp_opts = NULL;
	int len = -1;
	char default_hostid[] = "tcp://localhost:3334";
	char default_method[] = "bmi_tcp";

	/* create storage for the command line options */
	tmp_opts = (struct options*)malloc(sizeof(struct options));
	if(!tmp_opts){
		goto parse_args_error;
	}
	memset(tmp_opts, 0, sizeof(struct options));
	tmp_opts->hostid = (char*)malloc(strlen(default_hostid) + 1);
	tmp_opts->method = (char*)malloc(strlen(default_method) + 1);
	if(!tmp_opts->method || !tmp_opts->hostid)
	{
		goto parse_args_error;
	}

	/* fill in defaults */
	memcpy(tmp_opts->hostid, default_hostid, strlen(default_hostid) + 1);
	memcpy(tmp_opts->method, default_method, strlen(default_method) + 1);
	tmp_opts->handle=4095;

	/* look at command line arguments */
	while((one_opt = getopt(argc, argv, flags)) != EOF){
		switch(one_opt){
			case('l'):
				tmp_opts->handle = atoi(optarg);
				break;
			case('h'):
				len = (strlen(optarg)) + 1;
				free(tmp_opts->hostid);
				if((tmp_opts->hostid = (char*)malloc(len))==NULL){
					goto parse_args_error;
				}
				memcpy(tmp_opts->hostid, optarg, len);
				break;
			case('m'):
				len = (strlen(optarg)) + 1;
				free(tmp_opts->method);
				if((tmp_opts->method = (char*)malloc(len))==NULL){
					goto parse_args_error;
				}
				memcpy(tmp_opts->method, optarg, len);
				break;
			default:
				break;
		}
	}

	return(tmp_opts);

parse_args_error:

	/* if an error occurs, just free everything and return NULL */
	if(tmp_opts){
		if(tmp_opts->hostid){
			free(tmp_opts->hostid);
		}
		free(tmp_opts);
	}
	return(NULL);
}


static int block_on_flow(flow_descriptor* flow_d, FLOW_context_id
	flow_context)
{
	int ret = -1;
	int count = 0;
	int index = 5;

	ret = PINT_flow_post(flow_d, flow_context);
	if(ret == 1)
	{
		return(0);
	}
	if(ret < 0)
	{
		fprintf(stderr, "Error: flow_post() failure.\n");
		return(ret);
	}

	do
	{
		ret = PINT_flow_testsome(1, &flow_d, &count, &index, 10,
			flow_context);
	}while(ret == 0 && count == 0);
	if(ret < 0)
	{
		fprintf(stderr, "Error: flow_test() failure.\n");
		return(ret);
	}
	if(flow_d->state != FLOW_COMPLETE)
	{
		fprintf(stderr, "Error: flow finished in error state: %d\n",
		flow_d->state);
		return(-1);
	}

	return(0);
}
