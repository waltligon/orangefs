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
	free(msg->buffer_list[0]);
	free(msg->buffer_list);
	free(msg->size_list);
	//free(msg);
}
