


/*
 * This is an example of a client program that uses the BMI library
 * for communications.
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <bmi.h>
#include <gossip.h>
#include <pvfs2-req-proto.h>
#include <print-struct.h>

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
	struct PVFS_server_req* my_req = NULL;
	struct PVFS_server_resp* my_ack = NULL;
	int ret = -1;
	bmi_addr_t server_addr;
	bmi_op_id_t client_ops[2];
	int outcount = 0;
	bmi_error_code_t error_code;
	bmi_size_t actual_size;
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

	/* allocate a buffer for the initial request and ack */
	my_req = (struct PVFS_server_req*)BMI_memalloc(server_addr, 
		sizeof(struct PVFS_server_req), BMI_SEND);
	my_ack = (struct PVFS_server_resp*)BMI_memalloc(server_addr, 
		sizeof(struct PVFS_server_resp), BMI_RECV);
	if(!my_req || !my_ack){
		fprintf(stderr, "BMI_memalloc failed.\n");
		return(-1);
	}

	/* setup setattr request */
	my_req->op = PVFS_SERV_SETATTR;
	my_req->credentials.uid = 0;
	my_req->credentials.gid = 0;
	my_req->u.setattr.handle = user_opts->bucket;
	my_req->u.setattr.fs_id = 9;
	my_req->u.setattr.attr.owner = 100;
	my_req->u.setattr.attr.group = 100;
	my_req->u.setattr.attr.perms = 0x755;
	my_req->u.setattr.attr.atime = time(NULL);
	my_req->u.setattr.attr.ctime = time(NULL);
	my_req->u.setattr.attr.mtime = time(NULL);
	my_req->u.setattr.attr.objtype = 1;
	my_req->u.setattr.attr.mask = PVFS_ATTR_COMMON_ALL; 

	printf("Sending SETATTR for Handle %lld\n",(long long)user_opts->bucket);
	display_pvfs_structure(my_req,1);


	/* send the initial request on its way */
	ret = BMI_post_sendunexpected(&(client_ops[1]), server_addr, my_req, 
		sizeof(struct PVFS_server_req), BMI_PRE_ALLOC, 0, NULL, context);
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
	ret = BMI_post_recv(&(client_ops[0]), server_addr, my_ack, 
		sizeof(struct PVFS_server_resp), &actual_size, BMI_PRE_ALLOC, 0, 
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
		
	/* look at the ack */
	if(my_ack->op != PVFS_SERV_SETATTR)
	{
		printf("ERROR: received ack of wrong type (%d)\n", (int)my_ack->op);
	}
	if(my_ack->status != 0)
	{
		printf("ERROR: server returned status: %d\n",
			(int)my_ack->status);
	}
	printf("=== Recv'd ===\n");
	display_pvfs_structure(my_ack,2);

	/* free up memory buffers */
	BMI_memfree(server_addr, my_req, sizeof(struct PVFS_server_req), 
		BMI_SEND);
	BMI_memfree(server_addr, my_ack, sizeof(struct PVFS_server_resp), 
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

