/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <bmi.h>
#include <pvfs2-req-proto.h>
#include <gossip.h>
#include <stdlib.h>
#include <errno.h>
#include <PINT-reqproto-encode.h>
#include <PINT-reqproto-module.h>

ENCODE_REL_HEAD(do_encode_rel)
{
	int i;
	
	if(msg->buffer_flag == BMI_PRE_ALLOC)
	{

		for (i=0;i<msg->list_count;i++)
		{
			if(msg->buffer_list[i])
			{
				BMI_memfree(msg->dest,
						msg->buffer_list[i],
						msg->size_list[i],
						BMI_SEND);
			}
		}
	}
	free(msg->buffer_list);
	free(msg->size_list);
}
