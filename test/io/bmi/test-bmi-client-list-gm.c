/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */




/*
 * This is an example of a client program that uses the BMI library
 * for communications.
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "bmi.h"
#include "test-bmi.h"
#include "gossip.h"

/**************************************************************
 * Data structures 
 */

/* A little structure to hold program options, either defaults or
 * specified on the command line 
 */
struct options
{
    char *hostid;		/* host identifier */
};

#if 0
/* for testing eager mode */
#define MSG1_SIZE 1024
#define MSG2_SIZE 2048
#define MSG3_SIZE 3072
#else
/* for testing rendezvous mode */
#define MSG1_SIZE (1024*8)
#define MSG2_SIZE (2048*8)
#define MSG3_SIZE (3072*8)
#endif
/**************************************************************
 * Internal utility functions
 */

static struct options *parse_args(
    int argc,
    char *argv[]);


/**************************************************************/

int main(
    int argc,
    char **argv)
{

    struct options *user_opts = NULL;
    struct server_request *my_req = NULL;
    struct server_ack *my_ack = NULL;
    int ret = -1;
    PVFS_BMI_addr_t server_addr;
    void *send_buffer1 = NULL;
    void *send_buffer2 = NULL;
    void *send_buffer3 = NULL;
    bmi_op_id_t client_ops[2];
    int outcount = 0;
    bmi_error_code_t error_code;
    void *in_test_user_ptr = &server_addr;
    void *out_test_user_ptr = NULL;
    bmi_size_t actual_size;
    void *buffer_list[3];
    bmi_size_t size_list[3];
    int i = 0;
    int last = 0;
    bmi_context_id context;

    /* grab any command line options */
    user_opts = parse_args(argc, argv);
    if (!user_opts)
    {
	return (-1);
    }

    /* set debugging stuff */
    gossip_enable_stderr();
    gossip_set_debug_mask(1, GOSSIP_BMI_DEBUG_ALL);

    /* initialize local interface */
    ret = BMI_initialize("bmi_gm", NULL, 0);
    if (ret < 0)
    {
	errno = -ret;
	perror("BMI_initialize");
	return (-1);
    }

    ret = BMI_open_context(&context);
    if (ret < 0)
    {
	errno = -ret;
	perror("BMI_open_context()");
	return (-1);
    }

    /* get a bmi_addr for the server */
    ret = BMI_addr_lookup(&server_addr, user_opts->hostid);
    if (ret < 0)
    {
	errno = -ret;
	perror("BMI_addr_lookup");
	return (-1);
    }

    /* allocate a buffer for the initial request and ack */
    my_req = (struct server_request *) BMI_memalloc(server_addr,
						    sizeof(struct
							   server_request),
						    BMI_SEND);
    my_ack =
	(struct server_ack *) BMI_memalloc(server_addr,
					   sizeof(struct server_ack), BMI_RECV);
    if (!my_req || !my_ack)
    {
	fprintf(stderr, "BMI_memalloc failed.\n");
	return (-1);
    }

    my_req->size = MSG1_SIZE + MSG2_SIZE + MSG3_SIZE;

    /* send the initial request on its way */
    buffer_list[0] = my_req;
    size_list[0] = sizeof(struct server_request);
    ret = BMI_post_sendunexpected_list(&(client_ops[1]), server_addr,
				       (const void **) buffer_list, size_list,
				       1, size_list[0], BMI_PRE_ALLOC, 0,
				       in_test_user_ptr, context);
    if (ret < 0)
    {
	errno = -ret;
	perror("BMI_post_send");
	return (-1);
    }
    if (ret == 0)
    {
	/* turning this into a blocking call for testing :) */
	/* check for completion of request */
	do
	{
	    ret = BMI_test(client_ops[1], &outcount, &error_code, &actual_size,
			   &out_test_user_ptr, 10, context);
	} while (ret == 0 && outcount == 0);

	if (ret < 0 || error_code != 0)
	{
	    fprintf(stderr, "Request send failed.\n");
	    if (ret < 0)
	    {
		errno = -ret;
		perror("BMI_test");
	    }
	    return (-1);
	}

	if (in_test_user_ptr != out_test_user_ptr)
	{
	    fprintf(stderr, "1st ptr failure.\n");
	}
	else
	{
	    fprintf(stderr, "1st ptr success.\n");
	}
	out_test_user_ptr = NULL;
    }

    /* post a recv for the server acknowledgement */
    buffer_list[0] = my_ack;
    size_list[0] = sizeof(struct server_ack);

    ret = BMI_post_recv_list(&(client_ops[0]), server_addr, buffer_list,
			     size_list, 1, sizeof(struct server_ack),
			     &actual_size, BMI_PRE_ALLOC, 0, in_test_user_ptr,
			     context);
    if (ret < 0)
    {
	errno = -ret;
	perror("BMI_post_recv");
	return (-1);
    }
    if (ret == 0)
    {
	/* turning this into a blocking call for testing :) */
	/* check for completion of ack recv */
	do
	{
	    ret = BMI_test(client_ops[0], &outcount, &error_code,
			   &actual_size, &out_test_user_ptr, 10, context);
	} while (ret == 0 && outcount == 0);

	if (ret < 0 || error_code != 0)
	{
	    fprintf(stderr, "Ack recv failed.\n");
	    return (-1);
	}
	if (in_test_user_ptr != out_test_user_ptr)
	{
	    fprintf(stderr, "2nd ptr failure.\n");
	}
	else
	{
	    fprintf(stderr, "2nd ptr success.\n");
	}
	out_test_user_ptr = NULL;
    }
    else
    {
	if (actual_size != sizeof(struct server_ack))
	{
	    printf("Short recv.\n");
	    return (-1);
	}
    }

    /* look at the ack */
    if (my_ack->status != 0)
    {
	fprintf(stderr, "Request denied.\n");
	return (-1);
    }

    /* create 3 buffers to send */
    send_buffer1 = BMI_memalloc(server_addr, MSG1_SIZE, BMI_SEND);
    send_buffer2 = BMI_memalloc(server_addr, MSG2_SIZE, BMI_SEND);
    send_buffer3 = BMI_memalloc(server_addr, MSG3_SIZE, BMI_SEND);
    if (!send_buffer1 || !send_buffer2 || !send_buffer3)
    {
	fprintf(stderr, "BMI_memalloc.\n");
	return (-1);
    }
    buffer_list[0] = send_buffer1;
    buffer_list[1] = send_buffer2;
    buffer_list[2] = send_buffer3;
    size_list[0] = MSG1_SIZE;
    size_list[1] = MSG2_SIZE;
    size_list[2] = MSG3_SIZE;

    for (i = 0; i < (MSG1_SIZE / sizeof(int)); i++)
    {
	((int *) send_buffer1)[i] = i;
    }
    last = i;
    for (i = last; i < (last + (MSG2_SIZE / sizeof(int))); i++)
    {
	((int *) send_buffer2)[i - last] = i;
    }
    last = i;
    for (i = last; i < (last + (MSG3_SIZE / sizeof(int))); i++)
    {
	((int *) send_buffer3)[i - last] = i;
    }

    /* send the data payload on its way */
    ret = BMI_post_send_list(&(client_ops[0]), server_addr,
			     (const void **) buffer_list, size_list, 3,
			     (MSG1_SIZE + MSG2_SIZE + MSG3_SIZE),
			     BMI_PRE_ALLOC, 0, in_test_user_ptr, context);
    if (ret < 0)
    {
	errno = -ret;
	perror("BMI_post_send");
	return (-1);
    }
    if (ret == 0)
    {
	/* turning this into a blocking call for testing :) */
	/* check for completion of data payload send */
	do
	{
	    ret = BMI_test(client_ops[0], &outcount, &error_code,
			   &actual_size, &out_test_user_ptr, 10, context);
	} while (ret == 0 && outcount == 0);

	if (ret < 0 || error_code != 0)
	{
	    fprintf(stderr, "Data payload send failed.\n");
	    return (-1);
	}
	if (in_test_user_ptr != out_test_user_ptr)
	{
	    fprintf(stderr, "3rd ptr failure.\n");
	}
	else
	{
	    fprintf(stderr, "3rd ptr success.\n");
	}
	out_test_user_ptr = NULL;
    }

    /* free up the message buffers */
    BMI_memfree(server_addr, send_buffer1, MSG1_SIZE, BMI_SEND);
    BMI_memfree(server_addr, send_buffer2, MSG2_SIZE, BMI_SEND);
    BMI_memfree(server_addr, send_buffer3, MSG3_SIZE, BMI_SEND);
    BMI_memfree(server_addr, my_req, sizeof(struct server_request), BMI_SEND);
    BMI_memfree(server_addr, my_ack, sizeof(struct server_ack), BMI_RECV);

    /* shutdown the local interface */
    BMI_close_context(context);
    ret = BMI_finalize();
    if (ret < 0)
    {
	errno = -ret;
	perror("BMI_finalize");
	return (-1);
    }

    /* turn off debugging stuff */
    gossip_disable();

    free(user_opts->hostid);
    free(user_opts);

    return (0);
}


static struct options *parse_args(
    int argc,
    char *argv[])
{

    /* getopt stuff */
    extern char *optarg;
    char flags[] = "h:r:s:c:";
    int one_opt = 0;

    struct options *tmp_opts = NULL;
    int len = -1;

    /* create storage for the command line options */
    tmp_opts = (struct options *) malloc(sizeof(struct options));
    if (!tmp_opts)
    {
	goto parse_args_error;
    }

    /* look at command line arguments */
    while ((one_opt = getopt(argc, argv, flags)) != EOF)
    {
	switch (one_opt)
	{
	case ('h'):
	    len = (strlen(optarg)) + 1;
	    if ((tmp_opts->hostid = (char *) malloc(len)) == NULL)
	    {
		goto parse_args_error;
	    }
	    memcpy(tmp_opts->hostid, optarg, len);
	    break;
	default:
	    break;
	}
    }

    /* if we didn't get a host argument, fill in a default: */
    len = (strlen(DEFAULT_HOSTID_GM)) + 1;
    if ((tmp_opts->hostid = (char *) malloc(len)) == NULL)
    {
	goto parse_args_error;
    }
    memcpy(tmp_opts->hostid, DEFAULT_HOSTID_GM, len);

    return (tmp_opts);

  parse_args_error:

    /* if an error occurs, just free everything and return NULL */
    if (tmp_opts)
    {
	if (tmp_opts->hostid)
	{
	    free(tmp_opts->hostid);
	}
	free(tmp_opts);
    }
    return (NULL);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
