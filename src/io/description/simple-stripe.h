/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

#ifndef __SIMPLE_STRIPE_H
#define __SIMPLE_STRIPE_H

#include <stdlib.h>
#include <stdio.h>
#include <pvfs2-types.h>
#define DIST_MODULE 1
#include <pint-distribution.h>
#include <pvfs-distribution.h>

/* simple striping parameters */
struct PVFS_Dist_params {
	PVFS_size strip_size;
};

#endif /* __SIMPLE_STRIPE_H */
