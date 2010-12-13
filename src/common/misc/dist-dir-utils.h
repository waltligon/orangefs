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

int PINT_init_dist_dir_state(
		PVFS_dist_dir_attr *dist_dir_attr, 
		PVFS_dist_dir_bitmap *bitmap_ptr,
		const int num_servers, 
		const int server_no, 
		const int pre_dsg_num_server);
int PINT_is_dist_dir_bucket_active(
		const PVFS_dist_dir_attr *dist_dir_attr_p, 
		const PVFS_dist_dir_bitmap bitmap,
		const int server_no); 
int PINT_find_dist_dir_bucket(
		const PVFS_dist_dir_hash_type hash, 
		const PVFS_dist_dir_attr *const dist_dir_attr,
		const PVFS_dist_dir_bitmap bitmap);
int PINT_find_dist_dir_split_node(
		const PVFS_dist_dir_attr *const dist_dir_attr, 
		const PVFS_dist_dir_bitmap bitmap);
int PINT_update_dist_dir_bitmap_from_bitmap(
		PVFS_dist_dir_attr *to_dir_attr, 
		PVFS_dist_dir_bitmap to_dir_bitmap,
		const PVFS_dist_dir_attr *from_dir_attr, 
		const PVFS_dist_dir_bitmap from_dir_bitmap);
PVFS_dist_dir_hash_type PINT_encrypt_dirdata(const char *const name);
int PINT_dist_dir_set_serverno(const int server_no, 
	PVFS_dist_dir_attr *ddattr, 
	PVFS_dist_dir_bitmap ddbitmap);

#define PINT_dist_dir_attr_copyto(to_attr, from_attr) \
do { \
	to_attr.tree_height = from_attr.tree_height; \
	to_attr.num_servers = from_attr.num_servers; \
	to_attr.bitmap_size = from_attr.bitmap_size; \
	to_attr.split_size = from_attr.split_size; \
	to_attr.server_no = from_attr.server_no; \
	to_attr.branch_level = from_attr.branch_level; \
} while(0)
	


#endif /* __DIST_DIR_UTILS_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
