/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "bmi.h"
#include "pvfs2-req-proto.h"
#include "gossip.h"
#include <stdlib.h>
#include <errno.h>
#include "PINT-reqproto-encode.h"
#include "PINT-reqproto-module.h"

void do_encode_rel(
    struct PINT_encoded_msg *msg,
    enum PINT_encode_msg_type input_type)
{
    int i;

    if (msg->buffer_type == BMI_PRE_ALLOC)
    {

	for (i = 0; i < msg->list_count; i++)
	{
	    if (msg->buffer_list[i])
	    {
		BMI_memfree(msg->dest,
			    msg->buffer_list[i], msg->size_list[i], BMI_SEND);
	    }
	}
    }
    free(msg->buffer_list);
    free(msg->size_list);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
