


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
#include <test_bmi.h>
#include <gossip.h>

/**************************************************************
 * Data structures 
 */

/* A little structure to hold program options, either defaults or
 * specified on the command line 
 */
struct options{
	char* hostid;       /* host identifier */
	int message_size;   /* message size */
};


/**************************************************************
 * Internal utility functions
 */

static struct options* parse_args(int argc, char* argv[]);


/**************************************************************/

int main(int argc, char **argv)	{

	struct options* user_opts = NULL;
	struct server_request* my_req = NULL;
	struct server_ack* my_ack = NULL;
	int ret = -1;
	bmi_addr_t server_addr;
	bmi_op_id_t client_ops[2];
	int outcount = 0;
	bmi_error_code_t error_code;
	void* send_buffer = NULL;
	bmi_size_t actual_size;

	/* grab any command line options */
	user_opts = parse_args(argc, argv);
	if(!user_opts){
		return(-1);
	}

	/* set debugging stuff */
	gossip_enable_stderr();
	gossip_set_debug_mask(0, BMI_DEBUG_ALL);

	/* initialize local interface */
	ret = BMI_initialize("bmi_gm", NULL, 0);
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
		goto client_exit;
	}

	/* allocate a buffer for the initial request and ack */
	my_req = (struct server_request*)BMI_memalloc(server_addr, 
		sizeof(struct server_request), BMI_SEND_BUFFER);
	my_ack = (struct server_ack*)BMI_memalloc(server_addr, 
		sizeof(struct server_ack), BMI_RECV_BUFFER);
	if(!my_req || !my_ack){
		fprintf(stderr, "BMI_memalloc failed.\n");
		goto client_exit;
	}

	my_req->size = user_opts->message_size;

	/* send the initial request on its way */
	ret = BMI_post_sendunexpected(&(client_ops[1]), server_addr, my_req, 
		sizeof(struct server_request), BMI_PRE_ALLOC, 0, NULL);
	if(ret < 0)
	{
		errno = -ret;
		perror("BMI_post_send");
		goto client_exit;
	}
	if(ret == 0)
	{
		/* turning this into a blocking call for testing :) */
		/* check for completion of request */
		do
		{
			ret = BMI_wait(client_ops[1], &outcount, &error_code, 
				&actual_size, NULL);
		} while(ret == 0 && outcount == 0);

		if(ret < 0 || error_code != 0)
		{
			fprintf(stderr, "Request send failed.\n");
			if(ret<0)
			{
				errno = -ret;
				perror("BMI_wait");
			}
			goto client_exit;
		}
	}

	/* post a recv for the server acknowledgement */
	ret = BMI_post_recv(&(client_ops[0]), server_addr, my_ack, 
		sizeof(struct server_ack), &actual_size, BMI_PRE_ALLOC, 0, NULL);
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
			ret = BMI_wait(client_ops[0], &outcount, &error_code, 
				&actual_size, NULL);
		} while(ret == 0 && outcount == 0);

		if(ret < 0 || error_code != 0)
		{
			fprintf(stderr, "Ack recv failed.\n");
			return(-1);
		}
	}
	else
	{
		if(actual_size != sizeof(struct server_ack))
		{
			printf("Short recv.\n");
			return(-1);
		}
	}
		
	/* look at the ack */
	if(my_ack->status != 0){
		fprintf(stderr, "Request denied.\n");
		return(-1);
	}

	/* create a buffer to send */
	send_buffer = BMI_memalloc(server_addr,
		user_opts->message_size, BMI_SEND_BUFFER);
	if(!send_buffer){
		fprintf(stderr, "BMI_memalloc.\n");
		return(-1);
	}
	sprintf((char*)send_buffer, "Hello World.\n");

	/* send the data payload on its way */
	ret = BMI_post_send(&(client_ops[0]), server_addr, send_buffer, 
		user_opts->message_size, BMI_PRE_ALLOC, 0, NULL);
	if(ret < 0)
	{
		errno = -ret;
		perror("BMI_post_send");
		return(-1);
	}
	if(ret == 0)
	{
		/* turning this into a blocking call for testing :) */
		/* check for completion of data payload send */
		do
		{
			ret = BMI_wait(client_ops[0], &outcount, &error_code, &actual_size, NULL);
		} while(ret == 0 && outcount == 0);

		if(ret < 0 || error_code != 0)
		{
			fprintf(stderr, "Data payload send failed.\n");
			return(-1);
		}
	}

	/* free up the message buffers */
	BMI_memfree(server_addr, send_buffer, user_opts->message_size, 
		BMI_SEND_BUFFER);
	BMI_memfree(server_addr, my_req, sizeof(struct server_request), 
		BMI_SEND_BUFFER);
	BMI_memfree(server_addr, my_ack, sizeof(struct server_ack), 
		BMI_RECV_BUFFER);

client_exit:

	/* shutdown the local interface */
	ret = BMI_finalize();
	if(ret < 0){
		errno = -ret;
		perror("BMI_finalize");
		return(-1);
	}

	/* turn off debugging stuff */
	gossip_disable();

	return(0);
}


static struct options* parse_args(int argc, char* argv[]){

	/* getopt stuff */
	extern char* optarg;
	extern int optind, opterr, optopt;
	char flags[] = "h:r:s:c:";
	char one_opt = ' ';

	struct options* tmp_opts = NULL;
	int len = -1;
	int ret = -1;

	/* create storage for the command line options */
	tmp_opts = (struct options*)malloc(sizeof(struct options));
	if(!tmp_opts){
		goto parse_args_error;
	}

	/* fill in defaults (except for hostid) */
	tmp_opts->message_size = 10000;

	/* look at command line arguments */
	while((one_opt = getopt(argc, argv, flags)) != EOF){
		switch(one_opt){
			case('h'):
				len = (strlen(optarg)) + 1;
				if((tmp_opts->hostid = (char*)malloc(len))==NULL){
					goto parse_args_error;
				}
				memcpy(tmp_opts->hostid, optarg, len);
				break;
			case('s'):
				ret = sscanf(optarg, "%d", &tmp_opts->message_size);
				if(ret < 1){
					goto parse_args_error;
				}
				if(tmp_opts->message_size < 32)
				{
					fprintf(stderr, "Please pick a larger message size!.\n");
					goto parse_args_error;
				}
				break;
			default:
				break;
		}
	}
	
	/* if we didn't get a host argument, fill in a default: */
	len = (strlen(DEFAULT_HOSTID_GM)) + 1;
	if((tmp_opts->hostid = (char*)malloc(len))==NULL){
		goto parse_args_error;
	}
	memcpy(tmp_opts->hostid, DEFAULT_HOSTID_GM, len);

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

