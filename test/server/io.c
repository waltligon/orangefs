#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <bmi.h>
#include <gossip.h>
#include <pvfs2-req-proto.h>
#include <print-struct.h>
#include <PINT-reqproto-encode.h>


/* TODO: update this as we go
 *
 * this is a test program for playing around with I/O operations on the
 * pvfs2 server.  It doesn't work yet- just a hack for development
 * purposes.
 *
 * -PHIL
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
	int bucket;
};


/**************************************************************
 * Internal utility functions
 */

static struct options* parse_args(int argc, char* argv[]);


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
	struct PINT_encoded_msg encoded3;
	struct PINT_decoded_msg decoded3;
	struct PVFS_server_req_s my_req;
	struct PVFS_server_resp_s* io_dec_ack;
	struct PVFS_server_resp_s* create_dec_ack;
	struct PVFS_server_resp_s* remove_dec_ack;
	void* my_ack, *create_ack, *remove_ack;
	int my_ack_size, create_ack_size, remove_ack_size;
	
	/**************************************************
	 * general setup 
	 */

	/* figure out how big of an ack to post */
	my_ack_size = PINT_get_encoded_generic_ack_sz(0, PVFS_SERV_IO);
	create_ack_size = PINT_get_encoded_generic_ack_sz(0,
		PVFS_SERV_CREATE);
	remove_ack_size = PINT_get_encoded_generic_ack_sz(0,
		PVFS_SERV_REMOVE);

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

	/* grab any command line options */
	user_opts = parse_args(argc, argv);
	if(!user_opts){
		return(-1);
	}

	/* set debugging stuff */
	gossip_enable_stderr();
	gossip_set_debug_mask(0, 0);

	/* initialize local interface */
	ret = BMI_initialize(user_opts->method, NULL, 0);
	if(ret < 0){
		errno = -ret;
		perror("BMI_initialize");
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
	my_req.rsize = sizeof(struct PVFS_server_req_s);
	my_req.credentials.uid = 0;
	my_req.credentials.gid = 0;
	my_req.credentials.perms = U_WRITE | U_READ;  

	/* create specific fields */
	my_req.u.create.bucket = 4095;
	my_req.u.create.handle_mask = 0;
	my_req.u.create.fs_id = 9;
	my_req.u.create.object_type = ATTR_DATA;

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
		encoded1.buffer_flag, 
		0, 
		NULL);
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
				NULL, 10);
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
	PINT_encode_release(&encoded1, PINT_ENCODE_REQ, 0);

	/* post a recv for the server acknowledgement */
	ret = BMI_post_recv(&(client_ops[0]), server_addr, create_ack, 
		create_ack_size, &actual_size, BMI_EXT_ALLOC, 0, 
		NULL);
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
				&actual_size, NULL, 10);
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
		actual_size,
		NULL);
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

	/* setup create request */
	my_req.op = PVFS_SERV_IO;
	my_req.rsize = sizeof(struct PVFS_server_req_s);
	my_req.credentials.uid = 0;
	my_req.credentials.gid = 0;
	my_req.credentials.perms = U_WRITE | U_READ;  

	/* io specific fields */
	my_req.u.io.fs_id = 9;
	my_req.u.io.handle = user_opts->bucket;

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
		encoded2.buffer_flag,
		0,
		NULL);
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
				NULL, 10);
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
	PINT_encode_release(&encoded2, PINT_ENCODE_REQ, 0);

	/* post a recv for the server acknowledgement */
	ret = BMI_post_recv(&(client_ops[0]), server_addr, my_ack, 
		my_ack_size, &actual_size, BMI_EXT_ALLOC, 0, 
		NULL);
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
				&actual_size, NULL, 10);
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
		actual_size,
		NULL);
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
	
	/**************************************************
	 * general cleanup  
	 */

	/* release the decoded buffers */
	PINT_decode_release(&decoded2, PINT_ENCODE_RESP, 0);

	/* shutdown the local interface */
	ret = BMI_finalize();
	if(ret < 0){
		errno = -ret;
		perror("BMI_finalize");
		return(-1);
	}

	/* turn off debugging stuff */
	gossip_disable();

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
	char one_opt = ' ';

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
	tmp_opts->bucket=4095;

	/* look at command line arguments */
	while((one_opt = getopt(argc, argv, flags)) != EOF){
		switch(one_opt){
			case('l'):
				tmp_opts->bucket = atoi(optarg);
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

