/*
 * This is an example of a client program that uses the BMI library
 * for communications.
 */

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

#define DEFAULT_FILESYSTEM_NAME               "fs-foo"
#define DEFAULT_HOSTID          "tcp://localhost:3334"
#define DEFAULT_METHOD                       "bmi_tcp"

/**************************************************************
 * Data structures 
 */

/* A little structure to hold program options, either defaults or
 * specified on the command line 
 */
struct options{
	char* hostid;       /* host identifier */
	char* method;       /* bmi method to use */
};


/**************************************************************
 * Internal utility functions
 */

static struct options* parse_args(int argc, char* argv[]);


/**************************************************************/

int main(int argc, char **argv)	{

	struct options* user_opts = NULL;
	struct PVFS_server_req *server_req = NULL;
	struct PVFS_server_resp *server_resp = NULL;
	int ret = -1;
	bmi_addr_t server_addr;
	bmi_op_id_t client_ops[2];
	int outcount = 0;
	bmi_error_code_t error_code;
	bmi_size_t actual_size;
	struct PINT_encoded_msg encoded_msg;
	struct PINT_decoded_msg decoded_msg;
	bmi_context_id context;

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

	ret = BMI_open_context(&context);
	if(ret < 0)
	{
		errno = -ret;
		perror("BMI_open_context");
		return(-1);
	}

	/* get a bmi_addr for the server */
	ret = BMI_addr_lookup(&server_addr, user_opts->hostid);
	if(ret < 0){
		errno = -ret;
		perror("BMI_addr_lookup");
		return(-1);
	}

	server_req = (struct PVFS_server_req*)BMI_memalloc(server_addr, 
		sizeof(struct PVFS_server_req), BMI_SEND);
	server_resp = (struct PVFS_server_resp*)BMI_memalloc(server_addr, 
		sizeof(struct PVFS_server_resp)+8192, BMI_RECV);
	if(!server_req || !server_resp){
		fprintf(stderr, "BMI_memalloc failed.\n");
		return(-1);
	}

	/* setup create request */
	server_req->op = PVFS_SERV_GETCONFIG;
	server_req->credentials.uid = 0;
	server_req->credentials.gid = 0;
	/* TODO: fill below fields in with the correct values */
	server_req->credentials.perms = PVFS_U_WRITE | PVFS_U_READ;
	server_req->u.getconfig.config_buf_size = 8192;
	server_req->rsize = sizeof(struct PVFS_server_req);

	display_pvfs_structure(server_req,1);
	ret = PINT_encode(server_req,PINT_ENCODE_REQ,&encoded_msg,server_addr,0);
	printf("\n---encoded_msg (server_req) ---\n");
	display_pvfs_structure(encoded_msg.buffer_list[0],1);

	/* send the initial request on its way */
	ret = BMI_post_sendunexpected(&(client_ops[1]), server_addr, encoded_msg.buffer_list[0], 
		encoded_msg.total_size, BMI_PRE_ALLOC, 0, NULL, context);
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
			fprintf(stderr, "Request send failed.\n");
			if(ret<0)
			{
				errno = -ret;
				perror("BMI_test");
			}
			return(-1);
		}
	}

	/* post a recv for the server acknowledgement */
	ret = BMI_post_recv(&(client_ops[0]), server_addr, server_resp, 
		sizeof(struct PVFS_server_resp)+8192, &actual_size, BMI_PRE_ALLOC, 0, 
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
			fprintf(stderr, "Ack recv failed.\n");
			fprintf(stderr, "Ret: %d, E_code: %d\n",ret,error_code);
			return(-1);
		}
	}
	else
	{
		if(actual_size != sizeof(struct PVFS_server_resp))
		{
			printf("Short recv.\n");
			return(-1);
		}
	}

	ret = PINT_decode(server_resp,PINT_ENCODE_RESP,
                          &decoded_msg,server_addr,actual_size,NULL);
	printf("Decoded response size: %d\n",(int)actual_size);
	printf("\n---decoded_msg (server_resp) ---\n");
	display_pvfs_structure(decoded_msg.buffer,0);
	BMI_memfree(server_addr, server_resp, sizeof(struct PVFS_server_resp)+8192, 
		BMI_RECV);
	server_resp = decoded_msg.buffer;
	if(server_resp->op != PVFS_SERV_GETCONFIG)
	{
		printf("ERROR: received ack of wrong type (%d)\n", (int)server_resp->op);
	}
	if(server_resp->status != 0)
	{
		printf("ERROR: server returned status: %d\n",
			(int)server_resp->status);
	}

	/* free up memory buffers */
	BMI_memfree(server_addr, server_req, sizeof(struct PVFS_server_req), 
		BMI_SEND);
	BMI_memfree(server_addr, server_resp, sizeof(struct PVFS_server_resp)+8192, 
		BMI_RECV);

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

	free(user_opts->hostid);
	free(user_opts->method);
	free(user_opts);
	return(0);
}


static struct options* parse_args(int argc, char* argv[]){

	/* getopt stuff */
	extern char* optarg;
	extern int optind, opterr, optopt;
	char flags[] = "h:m:";
	char one_opt = ' ';

	struct options* tmp_opts = NULL;

	tmp_opts = (struct options*)malloc(sizeof(struct options));
	if(!tmp_opts){
		goto parse_args_error;
	}
	memset(tmp_opts, 0, sizeof(struct options));
	tmp_opts->hostid = strdup(DEFAULT_HOSTID);
	tmp_opts->method = strdup(DEFAULT_METHOD);
	if (!tmp_opts->method || !tmp_opts->hostid)
	{
		goto parse_args_error;
	}

	/* look at command line arguments */
	while((one_opt = getopt(argc, argv, flags)) != EOF){
		switch(one_opt){
			case('h'):
				free(tmp_opts->hostid);
				tmp_opts->hostid = strdup(optarg);
				break;
			case('m'):
				free(tmp_opts->method);
				tmp_opts->method = strdup(optarg);
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

