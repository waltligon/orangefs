/*
 * (C) 2002 Clemson University and The University of Chicago.
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_DISTRIBUTION_H
#define __PINT_DISTRIBUTION_H

#include "pvfs2-types.h"
#include "pvfs-distribution.h"

/* PVFS Distribution Processing Stuff */

#define PINT_DIST_TABLE_SZ 8
#define PINT_DIST_NAME_SZ 15

#ifdef DIST_MODULE

/* these functions must be defined by a distribution module */

void init_module(void);
void cleanup_module(void);

/* these functions can be called by a distribution module */

int PVFS_register_distribution(PINT_dist *d_p);
int PVFS_unregister_distribution(char *dist_name);

#endif

/* fill in missing items in dist struct */
int PINT_Dist_lookup(PINT_dist *dist);

/* pack dist struct for sending over wire */
void PINT_Dist_encode(void *buffer, PINT_dist *dist);

/* unpack dist struct after receiving from wire */
void PINT_Dist_decode(PINT_dist *dist, void *buffer);

#endif /* __PINT_DISTRIBUTION_H */
