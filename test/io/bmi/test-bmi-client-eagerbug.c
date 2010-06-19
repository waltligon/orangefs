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

#include "pvfs2.h"
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
    int message_size;		/* message size */
};


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
    void *send_buffer = NULL;
    bmi_op_id_t client_ops[2];
    int outcount = 0;
    bmi_error_code_t error_code;
    void *in_test_user_ptr = &server_addr;
    void *out_test_user_ptr = NULL;
    bmi_size_t actual_size;
    bmi_context_id context;
    char method[24], *cp;
    int len;
    char testeagerbuf1[] = "aaaccc";

    /* grab any command line options */
    user_opts = parse_args(argc, argv);
    if (!user_opts)
    {
	return (-1);
    }

    /* set debugging stuff */
    gossip_enable_stderr();
    /* gossip_set_debug_mask(1, GOSSIP_BMI_DEBUG_ALL); */

    /* convert address to bmi method type by prefixing bmi_ */
    cp = strchr(user_opts->hostid, ':');
    if (!cp)
        return 1;
    len = cp - user_opts->hostid;
    strcpy(method, "bmi_");
    strncpy(method + 4, user_opts->hostid, len);
    method[4+len] = '\0';

    /* initialize local interface */
    ret = BMI_initialize(NULL, NULL, 0);
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

    my_req->size = user_opts->message_size;

    /* send the initial request on its way */
    ret = BMI_post_sendunexpected(&(client_ops[1]), server_addr, my_req,
				  sizeof(struct server_request), BMI_PRE_ALLOC,
				  0, in_test_user_ptr, context, NULL);
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

	out_test_user_ptr = NULL;
    }

    /* post a recv for the server acknowledgement */
    ret = BMI_post_recv(&(client_ops[0]), server_addr, my_ack,
			sizeof(struct server_ack), &actual_size, BMI_PRE_ALLOC,
			0, in_test_user_ptr, context, NULL);
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

    /* create a buffer to send */
    send_buffer = BMI_memalloc(server_addr, user_opts->message_size, BMI_SEND);
    if (!send_buffer)
    {
	fprintf(stderr, "BMI_memalloc.\n");
	return (-1);
    }

    /* send the data payload on its way */
    /* NOTE: intentionally sending short message here, testing
     * ability to match eager send with rend. receive
     */
    ret = BMI_post_send(&(client_ops[0]), server_addr, send_buffer,
			15000, BMI_PRE_ALLOC, 0, in_test_user_ptr, context, NULL);
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

    /* final send to test eager bug *****************************/
    ret = BMI_post_send(&(client_ops[0]), server_addr, testeagerbuf1,
			6, BMI_EXT_ALLOC, 1, NULL, context, NULL);
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
			   &actual_size, NULL, 10, context);
	} while (ret == 0 && outcount == 0);

	if (ret < 0 || error_code != 0)
	{
	    fprintf(stderr, "Data payload send failed.\n");
	    return (-1);
	}
    }

    /* let the server get ahead of us */
    sleep(10);

    ret = BMI_post_send(&(client_ops[0]), server_addr, testeagerbuf1,
			6, BMI_EXT_ALLOC, 1, NULL, context, NULL);
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
			   &actual_size, NULL, 10, context);
	} while (ret == 0 && outcount == 0);

	if (ret < 0 || error_code != 0)
	{
	    fprintf(stderr, "Data payload send failed.\n");
	    return (-1);
	}
    }

    /* free up the message buffers */
    BMI_memfree(server_addr, send_buffer, user_opts->message_size, BMI_SEND);
    BMI_memfree(server_addr, my_req, sizeof(struct server_request), BMI_SEND);
    BMI_memfree(server_addr, my_ack, sizeof(struct server_ack), BMI_RECV);

    /* try out rev lookup */
    /* printf("rev_lookup() output: %s\n", BMI_addr_rev_lookup(server_addr)); */

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
    const char flags[] = "h:s:";
    int one_opt = 0;

    struct options *tmp_opts = NULL;
    int len = -1;
    int ret = -1;

    /* create storage for the command line options */
    tmp_opts = malloc(sizeof(struct options));
    if (!tmp_opts)
    {
	goto parse_args_error;
    }

    /* fill in defaults (except for hostid) */
    tmp_opts->hostid = NULL;
    tmp_opts->message_size = 32000;

    /* look at command line arguments */
    while ((one_opt = getopt(argc, argv, flags)) != EOF)
    {
	switch (one_opt)
	{
	case ('h'):
	    len = (strlen(optarg)) + 1;
	    if ((tmp_opts->hostid = malloc(len)) == NULL)
	    {
		goto parse_args_error;
	    }
	    memcpy(tmp_opts->hostid, optarg, len);
	    break;
	case ('s'):
	    ret = sscanf(optarg, "%d", &tmp_opts->message_size);
	    if (ret < 1)
	    {
		goto parse_args_error;
	    }
	    break;
	default:
	    break;
	}
    }

    /* if we didn't get a host argument, fill in a default: */
    if (tmp_opts->hostid == NULL) {
        len = (strlen(DEFAULT_HOSTID)) + 1;
        if ((tmp_opts->hostid = malloc(len)) == NULL)
        {
            goto parse_args_error;
        }
        memcpy(tmp_opts->hostid, DEFAULT_HOSTID, len);
    }

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
