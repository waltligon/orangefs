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

#include <pvfs-distribution.h>
#include <simple-stripe.h>

#define SEGMAX 32
#define BYTEMAX 250

main(int argc, char **argv)
{
	struct PVFS_Dist *d;
	struct PVFS_Dist *d2;
	int d_size;
	void *buffer;
	void *buffer2;

	/* grab a distribution */
	d = PVFS_Dist_create("simple_stripe");
	PINT_Dist_dump(d);

	/* encode the distribution */
	d_size = PINT_DIST_PACK_SIZE(d);
	buffer = malloc(d_size);
	PINT_Dist_encode(buffer, d);

	/* decode the distribution */
	d2 = (struct PVFS_Dist *)malloc(d_size);
	memcpy(buffer2, buffer, d_size);
	PINT_Dist_decode(d2, buffer);
	PINT_Dist_dump(d2);
}
