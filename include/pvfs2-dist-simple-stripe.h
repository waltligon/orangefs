/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

#ifndef __PVFS_DIST_SIMPLE_STRIPE_H
#define __PVFS_DIST_SIMPLE_STRIPE_H

/* Identifier to use when looking up this distribution */
#define PVFS_DIST_SIMPLE_STRIPE_NAME "simple_stripe"
#define PVFS_DIST_SIMPLE_STRIPE_NAME_SIZE 14

/* simple stripe distribution parameters */
struct PVFS_simple_stripe_params_s {
	PVFS_size strip_size;
};
typedef struct PVFS_simple_stripe_params_s PVFS_simple_stripe_params;

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
