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
typedef struct PVFS_Dist_params PVFS_Dist_params;
#else
typedef void PVFS_Dist_params;
#endif

/* this struct is used to define a distribution to PVFS */

typedef struct PVFS_Dist_methods {
	PVFS_offset (*logical_to_physical_offset) (PVFS_Dist_params *dparam,
			uint32_t iod_num, uint32_t iod_count,
			PVFS_offset logical_offset);
	PVFS_offset (*physical_to_logical_offset) (PVFS_Dist_params *dparam,
			uint32_t iod_num, uint32_t iod_count,
			PVFS_offset physical_offset);
	PVFS_offset (*next_mapped_offset) (PVFS_Dist_params *dparam,
			uint32_t iod_num, uint32_t iod_count,
			PVFS_offset logical_offset);
	PVFS_size (*contiguous_length) (PVFS_Dist_params *dparam,
			uint32_t iod_num, uint32_t iod_count,
			PVFS_offset physical_offset);
	PVFS_size (*logical_file_size) (PVFS_Dist_params *dparam,
			uint32_t iod_count, PVFS_size *psizes);
	void (*encode) (PVFS_Dist_params *dparam, void *buffer);
	void (*decode) (PVFS_Dist_params *dparam, void *buffer);
} PVFS_Dist_methods;

#ifdef MODULE

/* these functions must be defined by a distribution module */

void init_module(void);
void cleanup_module(void);

/* these functions can be called by a distribution module */

int PVFS_register_distribution(struct PVFS_Dist *d_p);
int PVFS_unregister_distribution(char *dist_name);

#endif

/* fill in missing items in dist struct */
int PINT_Dist_lookup(PVFS_Dist *dist);

/* pack dist struct for sending over wire */
void PINT_Dist_encode(void *buffer, PVFS_Dist *dist);

/* unpack dist struct after receiving from wire */
void PINT_Dist_decode(PVFS_Dist *dist, void *buffer);

#endif /* __PINT_DISTRIBUTION_H */
