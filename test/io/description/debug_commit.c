/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

#include <stdlib.h>
#include <stdio.h>
#include <pvfs2-types.h>
#include <gossip.h>
#include <pvfs2-debug.h>

#include "pint-distribution.h"
#include "pint-dist-utils.h"
#include "pvfs2-request.h"
#include "pint-request.h"
#include "pvfs2-dist-simple-stripe.h"

int main(int argc, char **argv)
{
	PINT_Request *r1;
	static int32_t blocklength[3] = {100, 22, 45};
	static PVFS_size displacement[3] = {10, 200, 300};

	/* Turn on debugging */
	gossip_enable_stderr();
	gossip_set_debug_mask(1,GOSSIP_REQUEST_DEBUG);


	/* set up two requests */
	PVFS_Request_hindexed(3, blocklength, displacement, PVFS_BYTE, &r1);

	PVFS_Request_commit(&r1);

	PINT_Dump_packed_request(r1);

	PVFS_Request_free(&r1);

	return 0;
}
