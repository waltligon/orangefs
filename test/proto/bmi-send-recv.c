/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <bmi.h>
#include <pvfs2-req-proto.h>
#include <gossip.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <PINT-reqproto-encode.h>
#include <bmi-send-recv.h>

#define RET_CHECK(__name) if(ret != 0) {printf(__name);exit(-1);}

int send_msg(bmi_op_id_t i, 
		bmi_addr_t s, 
		void *msg, 
		int size, 
		bmi_flag_t f, 
		bmi_msg_tag_t t, 
		void *in_test_user_ptr)
{
	int ret;
	int outcount;
	bmi_error_code_t error_code;
	bmi_size_t actual_size;
	void *u2 = NULL;

	ret = BMI_post_sendunexpected(&(i), 
			s, 
			msg,
			size,
			f,
			t,
			in_test_user_ptr);

	if(ret == 0)
	{
		/* turning this into a blocking call for testing :) */
		/* check for completion of request */
		do
		{
			ret = BMI_wait(i, &outcount, &error_code, &actual_size,
					&u2);
		} while(ret == 0 && outcount == 0);

		if(ret < 0 || error_code != 0)
		{
			fprintf(stderr, "Request send failed.\n");
			if(ret<0)
			{
				errno = -ret;
				perror("BMI_wait");
			}
			return(-1);
		}

		if(in_test_user_ptr != u2)
		{
			fprintf(stderr, "1st ptr failure.\n");
			return(-1);
		}
		u2 = NULL;
	}
	return(0);
}

int recv_msg(bmi_op_id_t i,
				bmi_addr_t a,
				void *buffer,
				bmi_size_t s,
				bmi_size_t *as,
				bmi_flag_t f,
				bmi_msg_tag_t m,
				void *in_test_user_ptr)
{
	int ret;
	int outcount;
	bmi_error_code_t error_code;
	bmi_size_t actual_size;
	void *out_test_user_ptr = NULL;

	ret = BMI_post_recv(&(i), 
								a,
								buffer, 
								s, 
								as, 
								f, 
								m, 
								in_test_user_ptr);
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
			ret = BMI_wait(i, &outcount, &error_code,
				&actual_size, &out_test_user_ptr);
		} while(ret == 0 && outcount == 0);

		if(ret < 0 || error_code != 0)
		{
			fprintf(stderr, "Ack recv failed.\n");
			return(-1);
		}
		if(in_test_user_ptr != out_test_user_ptr)
		{
			fprintf(stderr, "2nd ptr failure.\n");
		}
		out_test_user_ptr = NULL;
	}
	else
	{
		if(actual_size != s)
		{
			printf("Short recv.\n");
			return(-1);
		}
	}
	*as = actual_size;
	return(0);
}


