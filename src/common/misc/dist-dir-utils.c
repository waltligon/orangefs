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

#include "pvfs2-internal.h"
#include "dist-dir-utils.h"
#include "md5.h"
#include "bmi-byteswap.h"


/****************************
 * helper functions
 * **************************/

/* calculate log2 of number */
static double my_log2(const double n)
{
	return log(n) / log(2.0);
}

/* calculate branch_level for a server from a bitmap */
static int dist_dir_calc_branch_level(
		const PVFS_dist_dir_attr *dist_dir_attr, 
		const PVFS_dist_dir_bitmap bitmap)
{
	int level = 0;
	int server_no, num_servers;

	server_no = dist_dir_attr->server_no;
	num_servers = dist_dir_attr->num_servers;

	/* meta handle copy (server_no = -1) shall not reach here */
	assert(server_no >= 0 && server_no < num_servers);
	assert(bitmap != NULL);

	if(!(TST_BIT(bitmap, server_no)))
	{
		return -1; /* not an active server */
	}

	/* get the number of bits above which all bits are zero */
	while( server_no >> level )
	{
		level++;
	}

	/* until no splitting node is set */
	while( TST_BIT(bitmap, server_no + (1l << level)) )
	{
		level++;
	}

	return level;
}

/*****************************
 * main operation functions
 * ***************************/

/* init dir state function, set all parameters
 * server_no <- -1..(num_servers-1)
 * pre_dsg_num_server: pre-set a number of servers, used for known large directory. default value can be 1.
 */

int PINT_init_dist_dir_state(
		PVFS_dist_dir_attr *dist_dir_attr, 
		PVFS_dist_dir_bitmap *bitmap_ptr,
		const int num_servers, 
		const int server_no, 
		int pre_dsg_num_server,
                const int split_size)
{
	int i;
        double cval;

	assert(dist_dir_attr != NULL);

	/* -1 <= server_no < num_servers && 0 < pre_dsg_num_server <= num_servers */
	assert(	(num_servers > 0) &&
	 		(server_no >= -1) && /* metadata handle has server_no = -1 */
			(server_no < num_servers));

	if ((pre_dsg_num_server <= 0) ||
		(pre_dsg_num_server > num_servers))
	{
		pre_dsg_num_server = num_servers;	
	}

	dist_dir_attr->num_servers = num_servers;
	dist_dir_attr->server_no = server_no;
	/* tree_height start from 0 */
	cval = ceil(my_log2((double)num_servers)); 
	dist_dir_attr->tree_height = (int)cval;

	/* increase bitmap_size if 2^tree_height > 32
	 * bitmap has at least 2^tree_height bits, that is, the number of leaves of a full tree. */
	if( (1l << dist_dir_attr->tree_height) >
		(sizeof(PVFS_dist_dir_bitmap_basetype) * 8))
	{
		dist_dir_attr->bitmap_size = ((1l << dist_dir_attr->tree_height) >> 5);
	}
	else
	{
		dist_dir_attr->bitmap_size = 1;
	}

	*bitmap_ptr = calloc(dist_dir_attr->bitmap_size,
                sizeof(PVFS_dist_dir_bitmap_basetype));
	if ((*bitmap_ptr) == NULL)
	{
		return -1;
	}

	for(i = pre_dsg_num_server-1; i >= 0; i--)
	{
		SET_BIT((*bitmap_ptr), i);
	}

	if(server_no > -1) /* an dirdata server */
	{
		dist_dir_attr->branch_level = dist_dir_calc_branch_level(dist_dir_attr, *bitmap_ptr);
	}
	else /* a meta server */
	{
		dist_dir_attr->branch_level = -1;
	}

	/* set split size */
	dist_dir_attr->split_size = split_size;
	return 0;
}

/* functions to test whether a dirdata server is active or not. 
 * will return 0 if server_no is out of bound or server is inactive.
 */
int PINT_is_dist_dir_bucket_active(
		const PVFS_dist_dir_attr *dist_dir_attr_p, 
		const PVFS_dist_dir_bitmap bitmap,
		const int server_no)
{
    if((server_no < 0) ||
            (server_no >= dist_dir_attr_p->num_servers))
    {
        return 0;
    }

    if(TST_BIT(bitmap, server_no))
    {
        return 1;
    }
    else
    {
        return 0;
    }

}

/*
 * client uses this function to find the server that should hold
 * a new dir entry, based on the current tree
 * hash can be any value, whose rightmost several bits will be examined.
 */
int PINT_find_dist_dir_bucket(
		const PVFS_dist_dir_hash_type hash, 
		const PVFS_dist_dir_attr *const dist_dir_attr,
		const PVFS_dist_dir_bitmap bitmap)
{
	PVFS_dist_dir_hash_type node_val;
	int level;

	assert(dist_dir_attr != NULL);
	assert(bitmap != NULL);

	level = dist_dir_attr->tree_height;

	if(level < 0)
	{
		return -1;
	}

	for( node_val = hash & ((1l << level) - 1); /* use the rightmost 'tree_height' bits */
		 (level >= 0) && !(TST_BIT(bitmap, node_val)); /* test if node_val bit is set */
		 node_val &= ((1l << (--level)) - 1) ); /* if it's not, use less bits of the hash value */

	return (int)node_val; /* assume tree_height < 32 */
}


/*
 * this code return the index of the new node when a bucket is to be split
 */
int PINT_find_dist_dir_split_node(
		PVFS_dist_dir_attr *const dist_dir_attr, 
		PVFS_dist_dir_bitmap bitmap)
{
	int new_node_val;
	int branch_level;

	assert((dist_dir_attr != NULL) && 
		   (dist_dir_attr->server_no > -1) &&
		   (bitmap != NULL));

	branch_level = dist_dir_attr->branch_level;

	/* meta server or inactive dirdata server should not come here */
	assert(branch_level > -1);

	/* if it reaches the maximum tree height */
	if (branch_level >= dist_dir_attr->tree_height)
	{
		return -1;
	}

	/* calculate new node value */
	new_node_val = dist_dir_attr->server_no 
				+ (1l << branch_level);
	if(new_node_val >= dist_dir_attr->num_servers)
	{
		return -1;
	}

	/* new node must be unset, otherwise branch_level is messed up */
	assert(!TST_BIT(bitmap, new_node_val));
        SET_BIT(bitmap, new_node_val);
        dist_dir_attr->branch_level = dist_dir_calc_branch_level(dist_dir_attr, bitmap);

	return new_node_val;
}

/*
 * update current bitmap tree and re-calculate branch_level
 */
int PINT_update_dist_dir_bitmap_from_bitmap(
		PVFS_dist_dir_attr *to_dir_attr, 
		PVFS_dist_dir_bitmap to_dir_bitmap,
		const PVFS_dist_dir_attr *from_dir_attr, 
		const PVFS_dist_dir_bitmap from_dir_bitmap)
{
	int i;

	assert((to_dir_attr != NULL) && (from_dir_attr != NULL));
	assert((to_dir_bitmap != NULL) && (from_dir_bitmap != NULL));

	if( (to_dir_attr->num_servers != from_dir_attr->num_servers) ||
		(to_dir_attr->server_no == from_dir_attr->server_no))
	{
		return -1; /* not in the same tree or update itself */
	}

	/* bitmap is with a type of (uint32_t *)
	 */
	for(i = to_dir_attr->bitmap_size - 1;
		i >= 0; i--) 
	{
		to_dir_bitmap[i] |= from_dir_bitmap[i];
	}

	/* update branch level */
	if(to_dir_attr->server_no > -1) /* dirdata server */
	{
	to_dir_attr->branch_level = dist_dir_calc_branch_level(to_dir_attr, to_dir_bitmap);
	}

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
        return bmitoh64(*hash_val);
}



/* set server_no field and update branch_level if necessary */
int PINT_dist_dir_set_serverno(const int server_no, 
	PVFS_dist_dir_attr *ddattr, 
	PVFS_dist_dir_bitmap ddbitmap)
{
	assert(ddattr != NULL && 
			ddbitmap != NULL &&
			server_no >= -1 &&
			server_no < ddattr->num_servers);


	ddattr->server_no = server_no;
	
	ddattr->branch_level = dist_dir_calc_branch_level(ddattr, ddbitmap);

        return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
