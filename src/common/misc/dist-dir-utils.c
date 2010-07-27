/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "dist-dir-utils.h"
#include "md5.h"


/****************************
 * helper functions
 * **************************/

/* calculate log2 of number */
static double my_log2(const double n)
{
	return log(n) / log(2);
}

/* calculate branch_level for a server from a bitmap */
static int dist_dir_calc_branch_level(
		const PVFS_dist_dir_attr *dist_dir_attr, const PVFS_dist_dir_bitmap bitmap)
{
	int level = 0;
	int server_no, num_servers;

	server_no = dist_dir_attr->server_no;
	num_servers = dist_dir_attr->num_servers;

	assert(server_no >= 0 && server_no < num_servers);

	if(!(TST_BIT(bitmap, server_no)))
		return -1; /* not an active server */

	/* get the number of bits above which all bits are zero */
	while( server_no >> level )
		level++;

	/* until no splitting node is set */
	while( TST_BIT(bitmap, server_no + (1l << level)) )
		level++;

	return level;
}

/*****************************
 * main operation functions
 * ***************************/

/* init dir state function, set all parameters
 * server_no <- 0..(num_servers-1)
 * pre_dsg_num_server: pre-set a number of servers, used for known large directory. default value can be 1.
 */

int PINT_init_dist_dir_state(
		PVFS_dist_dir_attr *dist_dir_attr, PVFS_dist_dir_bitmap *bitmap_ptr,
		int num_servers, int server_no, int pre_dsg_num_server)
{
	int i;

	assert(dist_dir_attr != NULL);

	/* 0 <= server_no < num_servers && 0 < pre_dsg_num_server <= num_servers */
	if( (num_servers < 1) ||
		(server_no < -1) || /* metadata handle has server_no = -1 */
		(server_no >= num_servers) ||
		(pre_dsg_num_server <= 0) ||
		(pre_dsg_num_server > num_servers) )
		return -1;

	dist_dir_attr->num_servers = num_servers;
	dist_dir_attr->server_no = server_no;
	dist_dir_attr->tree_height = (int)ceil(my_log2((double)num_servers)); /* tree_height start from 0 */

	/* increase bitmap_size if 2^tree_height > 32
	 * bitmap has at least 2^tree_height bits, that is, the number of leaves of a full tree. */
	if( (1l << dist_dir_attr->tree_height) >
		(sizeof(PVFS_dist_dir_bitmap_basetype) * 8))
		dist_dir_attr->bitmap_size = ((1l << dist_dir_attr->tree_height) >> 5);
	else
		dist_dir_attr->bitmap_size = 1;

	*bitmap_ptr = (PVFS_dist_dir_bitmap) malloc(
			dist_dir_attr->bitmap_size * sizeof(PVFS_dist_dir_bitmap_basetype));
	if ((*bitmap_ptr) == NULL)
		return -1;

	memset((*bitmap_ptr), 0,
			dist_dir_attr->bitmap_size * sizeof(PVFS_dist_dir_bitmap_basetype));

	for(i = pre_dsg_num_server-1; i >= 0; i--)
		SET_BIT((*bitmap_ptr), i);

	dist_dir_attr->branch_level = dist_dir_calc_branch_level(dist_dir_attr, *bitmap_ptr);

	/* set split size */
	dist_dir_attr->split_size = PVFS_DIST_DIR_MAX_ENTRIES;
	return 0;
}


/*
 * client uses this function to find the server that should hold
 * a new dir entry, based on the current tree
 * hash can be any value, whose rightmost several bits will be examined.
 */
int PINT_find_dist_dir_bucket(
		PVFS_dist_dir_hash_type hash, PVFS_dist_dir_attr *dist_dir_attr,
		PVFS_dist_dir_bitmap bitmap)
{
	PVFS_dist_dir_hash_type node_val;
	int level;

	assert(dist_dir_attr != NULL);

	level = dist_dir_attr->tree_height;

	if(level < 0)
		return -1;

	for( node_val = hash & ((1l << level) - 1); /* use the rightmost 'tree_height' bits */
		 (level >= 0) && !(TST_BIT(bitmap, node_val)); /* test if node_val bit is set */
		 node_val &= ((1l << (--level)) - 1) ); /* if it's not, use less bits of the hash value */

	return (int)node_val; /* assume tree_height < 32 */
}


/*
 * this code return the index of the new node when a bucket is to be split
 */
int PINT_find_dist_dir_split_node(
		PVFS_dist_dir_attr *dist_dir_attr, PVFS_dist_dir_bitmap bitmap)
{
	int new_node_val;

	assert(dist_dir_attr != NULL);

	/* if it reaches the maximum tree height */
	if (dist_dir_attr->branch_level >= dist_dir_attr->tree_height)
		return -1;

	/* calculate new node value */
	new_node_val = dist_dir_attr->server_no + (1l << dist_dir_attr->branch_level);
	if(new_node_val >= dist_dir_attr->num_servers)
		return -1;

	return new_node_val;
}

/*
 * update current bitmap tree and re-calculate branch_level
 */
int PINT_update_dist_dir_bitmap_from_bitmap(
		PVFS_dist_dir_attr *to_dir_attr, PVFS_dist_dir_bitmap to_dir_bitmap,
		const PVFS_dist_dir_attr *from_dir_attr, const PVFS_dist_dir_bitmap from_dir_bitmap)
{
	int i;

	assert((to_dir_attr != NULL) && (from_dir_attr != NULL));

	if( (to_dir_attr->num_servers != from_dir_attr->num_servers) ||
		(to_dir_attr->server_no == from_dir_attr->server_no))
		return -1; /* not in the same tree or update itself */

	/* bitmap is with a type of (uint32_t *)
	 */
	for(i = to_dir_attr->bitmap_size - 1;
			i >= 0; i--) {
		to_dir_bitmap[i] |= from_dir_bitmap[i];
	}

	/* update branch level */
	to_dir_attr->branch_level = dist_dir_calc_branch_level(to_dir_attr, to_dir_bitmap);

	/* anything else to update? */

	return 0;
}


/** a md5 encrypt function
 * MD5 encryption returns a 128bit value, the hash value takes the last 64bit
 * and save as an uint64_t
 *
 * ?? later, may add an option to be able to use other hash algorithm?
 */
PVFS_dist_dir_hash_type PINT_encrypt_dirdata(const char *const name)
{
	PVFS_dist_dir_hash_type *hash_val;
	md5_state_t state;
	md5_byte_t digest[16];

	md5_init(&state);
	md5_append(&state, (const md5_byte_t *)name, strlen(name));
	md5_finish(&state, digest);

	hash_val = (PVFS_dist_dir_hash_type *)(digest + 8);
	return *hash_val;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
