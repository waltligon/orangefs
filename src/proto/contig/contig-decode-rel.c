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

void do_decode_rel(
    struct PINT_decoded_msg *msg,
    enum PINT_encode_msg_type input_type)
{
    if (msg->buffer)
	free(msg->buffer);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
