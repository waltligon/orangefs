/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
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

/**************************************************************/

int main(
    int argc,
    char **argv)
{
    char in_buf, out_buf;
    int ret = -1;
    PVFS_BMI_addr_t server_addr;
    bmi_op_id_t client_ops[2];
    int outcount = 0;
    bmi_error_code_t error_code;
    bmi_size_t actual_size;
    bmi_context_id context;

    /* set debugging stuff */
    gossip_enable_stderr();
    gossip_set_debug_mask(1, GOSSIP_BMI_DEBUG_ALL);

    /* initialize local interface */
    ret = BMI_initialize("bmi_tcp", "tcp://localhost:3381", BMI_INIT_SERVER);
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
    ret = BMI_addr_lookup(&server_addr, "tcp://localhost:3380");
    if (ret < 0)
    {
	errno = -ret;
	perror("BMI_addr_lookup");
	return (-1);
    }

    /* sleep to let the other instance come up */
    sleep(5);

    /* send the initial request on its way */
    ret = BMI_post_send(&(client_ops[1]), server_addr, &out_buf,
				  1, BMI_EXT_ALLOC,
				  0, NULL, context, NULL);
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
			   NULL, 10, context);
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
    }

    /* post a recv for the server acknowledgement */
    ret = BMI_post_recv(&(client_ops[0]), server_addr, &in_buf,
			1, &actual_size, BMI_EXT_ALLOC,
			0, NULL, context, NULL);
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
			   &actual_size, NULL, 10, context);
	} while (ret == 0 && outcount == 0);

	if (ret < 0 || error_code != 0)
	{
	    fprintf(stderr, "Ack recv failed.\n");
	    return (-1);
	}
    }

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

    return (0);
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
