/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#ifndef __DIST_DIR_UTILS_H
#define __DIST_DIR_UTILS_H

#include "pvfs2-types.h"


/* changes to original prototype
 * 1. separate attr and bitmap structure
 * 2. add bitmap_size field and the size is indicating the number of uint32_t blocks,
 *    simplifies the bitmap update procedure
 * 3. change type and function names
 */


/* decode bit-mapped array 32-bit ints */
/* 5 is log2(32bits) - selects 32 bit word */
/* 0x1f masks lower 5 bits - selects bit in word */

/* the bitnum should start from 0 */

#define SET_BIT(i_bitfield, bitnum) \
	do { i_bitfield[(bitnum) >> 5] |= (1 << ((bitnum) & 0x1f));} while (0)

#define CRL_BIT(i_bitfield, bitnum) \
	do { i_bitfield[(bitnum) >> 5] &= ~(1 << ((bitnum) & 0x1f));} while (0)

#define TST_BIT(i_bitfield, bitnum) \
	( i_bitfield[(bitnum) >> 5] & (1 << ((bitnum) & 0x1f)) )


/* dummy value of split size for now */
#define PVFS_DIST_DIR_MAX_ENTRIES 16000


static double my_log2(const double n);
static int dist_dir_calc_branch_level(
		const PVFS_dist_dir_attr *dist_dir_attr,
		const PVFS_dist_dir_bitmap bitmap);
int PINT_init_dist_dir_state(
		PVFS_dist_dir_attr *dist_dir_attr, PVFS_dist_dir_bitmap *bitmap_ptr,
		int num_servers, int server_no, int pre_dsg_num_server);
int PINT_find_dist_dir_bucket(
		PVFS_dist_dir_hash_type hash, PVFS_dist_dir_attr *dist_dir_attr,
		PVFS_dist_dir_bitmap bitmap);
int PINT_find_dist_dir_split_node(
		PVFS_dist_dir_attr *dist_dir_attr, PVFS_dist_dir_bitmap bitmap);
int PINT_update_dist_dir_bitmap_from_bitmap(
		PVFS_dist_dir_attr *to_dir_attr, PVFS_dist_dir_bitmap to_dir_bitmap,
		const PVFS_dist_dir_attr *from_dir_attr, const PVFS_dist_dir_bitmap from_dir_bitmap);
PVFS_dist_dir_hash_type PINT_encrypt_dirdata(const char *const name);




#endif /* __DIST_DIR_UTILS_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
