/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_BUCKET_H
#define __PINT_BUCKET_H

#include <errno.h>
#include <string.h>
#include <assert.h>

#include "pvfs2-types.h"
#include "pvfs2-attr.h"
#include "pint-sysint.h"
#include "bmi.h"
#include "gossip.h"
#include "dotconf.h"
#include "trove.h"
#include "server-config.h"
#include "llist.h"
#include "quickhash.h"
#include "extent-utils.h"
#include "pint-bucket.h"

/* Configuration Management Data Structure */
fsconfig_array server_config;
extern struct server_configuration_s g_server_config;

static char HACK_server_name[] = "tcp://localhost:3334";
static PVFS_handle HACK_handle_mask = 0;
static PVFS_handle HACK_bucket = 0;

static struct qhash_table *s_fsid_config_cache_table = NULL;

/* these are based on code from src/server/request-scheduler.c */
static int hash_fsid(void *fsid, int table_size);
static int hash_fsid_compare(void *key, struct qlist_head *link);


/* PINT_bucket_initialize()
 *
 * initializes the bucket interface
 *
 * returns 0 on success, -errno on failure
 */
int PINT_bucket_initialize(void)
{
    int ret = -EINVAL;
    struct llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs = NULL;

    if (!g_server_config.file_systems)
    {
        return ret;
    }

    s_fsid_config_cache_table = qhash_init(
        hash_fsid_compare,hash_fsid,67);
    if (!s_fsid_config_cache_table)
    {
        return (-ENOMEM);
    }

    /*
      we can do this here...reserving the load_mapping
      call for dynamic addition.  is this a problem?
    */
    cur = g_server_config.file_systems;
    while(cur)
    {
        cur_fs = llist_head(cur);
        if (!cur_fs)
        {
            break;
        }
        ret = PINT_handle_load_mapping(cur_fs);
        if (ret)
        {
            return ret;
        }
        cur = llist_next(cur);
    }
    return(0);
}

/* PINT_bucket_finalize()
 *
 * shuts down the bucket interface and releases any resources associated
 * with it.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_bucket_finalize(void)
{
	/* FIXME: iterate through hashtable and free each element */
	gossip_lerr("Warning: PINT_bucket_finalize leaking memory.\n");
	qhash_finalize(s_fsid_config_cache_table);
	return(0);
}

/* PINT_handle_load_mapping()
 *
 * loads a new mapping of servers to handle into this interface.
 * This function may be called multiple times in order to add new file
 * system information at run time.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_handle_load_mapping(struct filesystem_configuration_s *fs)
{
    int ret = -EINVAL;
    struct llist *cur = NULL;
    struct host_handle_mapping_s *cur_mapping = NULL;
    struct config_fs_cache_s *cur_config_fs_cache = NULL;
    struct bmi_host_extent_table_s *cur_host_extent_table = NULL;

    if (fs)
    {
        cur_config_fs_cache = (struct config_fs_cache_s *)malloc(
            sizeof(struct config_fs_cache_s));
        assert(cur_config_fs_cache);

        cur_config_fs_cache->fs = fs;
        cur_config_fs_cache->bmi_host_extent_tables = llist_new();
        assert(cur_config_fs_cache->bmi_host_extent_tables);

        cur = fs->handle_ranges;
        while(cur)
        {
            cur_mapping = llist_head(cur);
            if (!cur_mapping)
            {
                break;
            }
            assert(cur_mapping->host_alias);
            assert(cur_mapping->handle_range);

            cur_host_extent_table = (bmi_host_extent_table_s *)malloc(
                sizeof(bmi_host_extent_table_s));
            if (!cur_host_extent_table)
            {
                ret = -ENOMEM;
                break;
            }
            cur_host_extent_table->bmi_address =
                PINT_server_config_get_host_addr_ptr(
                    &g_server_config,cur_mapping->host_alias);
            assert(cur_host_extent_table->bmi_address);

            cur_host_extent_table->extent_list =
                PINT_create_extent_list(cur_mapping->handle_range);
            if (!cur_host_extent_table->extent_list)
            {
                free(cur_host_extent_table);
                ret = -ENOMEM;
                break;
            }
            /*
              add this host to extent list mapping to
              config cache object's host extent table
            */
            ret = llist_add_to_tail(
                cur_config_fs_cache->bmi_host_extent_tables,
                cur_host_extent_table);
            assert(ret == 0);

            cur = llist_next(cur);
        }

        /*
          add config cache object to the hash table
          that maps fsid to a config_fs_cache_s
        */
        if (ret == 0)
        {
            qhash_add(s_fsid_config_cache_table,&(fs->coll_id),
                      &(cur_config_fs_cache->hash_link));
        }
    }
    return ret;
}


/* PINT_bucket_get_next_meta()
 *
 * returns the address, bucket, and handle mask of the next server 
 * that should be used to store a new piece of metadata.  This 
 * function is responsible for fairly distributing the metadata 
 * storage responsibility to all servers.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_bucket_get_next_meta(
	PVFS_fs_id fsid,
	bmi_addr_t* meta_addr,
	PVFS_handle* bucket,
	PVFS_handle* handle_mask)
{
	int ret = -1;

#if 0
	/* make sure that they asked for something sane */
	if (!PINT_server_config_is_valid_collection_id(
                &g_server_config,(TROVE_coll_id)fsid))
	{
		gossip_lerr("PINT_bucket_get_next_meta() called with invalid fsid.\n");
		return(-EINVAL);
	}
#endif
	ret = BMI_addr_lookup(meta_addr, HACK_server_name);
	if(ret < 0)
	{
		return(ret);
	}

	*handle_mask = HACK_handle_mask;
	*bucket = HACK_bucket;

	return(0);
}

/* PINT_bucket_get_next_io()
 *
 * returns the address, bucket, and handle mask of a set of servers that
 * should be used to store new pieces of file data.  This function is
 * responsible for evenly distributing the file data storage load to all
 * servers.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_bucket_get_next_io(
	PVFS_fs_id fsid,
	int num_servers,
	bmi_addr_t* io_addr_array,
	PVFS_handle* bucket_array,
	PVFS_handle* handle_mask)
{
	int i = 0;
	int ret = -1;

#if 0
	/* make sure that they asked for something sane */
	if (!PINT_server_config_is_valid_collection_id(
                &g_server_config,(TROVE_coll_id)fsid))
	{
		gossip_lerr("PINT_bucket_get_next_io() called with invalid fsid.\n");
		return(-EINVAL);
	}
#endif
	/* NOTE: for now, we assume that if the caller asks for more servers
	 * than we have available, we should just duplicate servers in the
	 * list.  The caller can use get_num_io to find out how many servers
	 * there are if they want to match up.
	 */
	
	ret = BMI_addr_lookup(&(io_addr_array[0]), HACK_server_name);
	if(ret < 0)
	{
		return(ret);
	}

	/* copy the same address to every server position if the user wanted
	 * more than one i/o server
	 */
	for(i=1; i<num_servers; i++)
	{
		io_addr_array[i] = io_addr_array[0];
	}

	/* fill in the same bucket in every slot as well */
	for(i=0; i<num_servers; i++)
	{
		bucket_array[i] = HACK_bucket;
	}

	*handle_mask = HACK_handle_mask;

	return(0);
}


/* PINT_bucket_map_to_server()
 *
 * maps from a bucket and fsid to a server address
 *
 * returns 0 on success to -errno on failure
 */
int PINT_bucket_map_to_server(
	bmi_addr_t* server_addr,
	PVFS_handle handle,
	PVFS_fs_id fsid)
{
    int ret = -EINVAL;
    char *bmi_server_addr = (char *)0;
    struct llist *cur = NULL;
    struct bmi_host_extent_table_s *cur_host_extent_table = NULL;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;

    assert(s_fsid_config_cache_table);

    hash_link = qhash_search(s_fsid_config_cache_table,&(fsid));
    if (hash_link)
    {
        cur_config_cache =
            qlist_entry(hash_link, struct config_fs_cache_s,
                        hash_link);
        assert(cur_config_cache);
        assert(cur_config_cache->fs);
        assert(cur_config_cache->bmi_host_extent_tables);

        cur = cur_config_cache->bmi_host_extent_tables;
        while(cur)
        {
            cur_host_extent_table = llist_head(cur);
            if (!cur_host_extent_table)
            {
                break;
            }
            assert(cur_host_extent_table->bmi_address);
            assert(cur_host_extent_table->extent_list);

            if (PINT_handle_in_extent_list(
                    cur_host_extent_table->extent_list, handle))
            {
                bmi_server_addr = cur_host_extent_table->bmi_address;
                ret = 0;
                break;
            }
            cur = llist_next(cur);
        }
    }
    return (((ret == 0) && bmi_server_addr) ?
            BMI_addr_lookup(server_addr, bmi_server_addr) : ret);
}


/* PINT_bucket_map_from_server()
 *
 * maps from a server to an array of buckets (bounded by inout_count)
 * that it controls.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_bucket_map_from_server(
	char* server_name,
	int* inout_count, 
	PVFS_handle* bucket_array,
	PVFS_handle* handle_mask)
{
#if 0
	if(strcmp(server_name, HACK_server_name) != 0)
	{
		return(-EINVAL);
	}
#endif

	if(*inout_count < 1)
	{
		return(-EINVAL);
	}

	*inout_count = 1;
	bucket_array[0] = HACK_bucket;
	*handle_mask = HACK_handle_mask;

	return(0);
}

/* PINT_bucket_get_num_meta()
 *
 * discovers the number of metadata servers available for a given file
 * system
 *
 * returns 0 on success, -errno on failure
 */
int PINT_bucket_get_num_meta(PVFS_fs_id fsid, int *num_meta)
{
    int ret = -EINVAL;
    struct llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs = NULL;

    /* make sure the specified fs_id is sane */
    if (!PINT_server_config_is_valid_collection_id(
            &g_server_config,(TROVE_coll_id)fsid))
    {
        gossip_lerr("PINT_bucket_get_num_meta() called with invalid fs_id.\n");
        return(-EINVAL);
    }

    cur = g_server_config.file_systems;
    while(cur)
    {
        cur_fs = llist_head(cur);
        if (!cur_fs)
        {
            break;
        }
        if (fsid == (PVFS_fs_id)cur_fs->coll_id)
        {
            if (cur_fs->meta_server_list)
            {
                *num_meta = llist_count(cur_fs->meta_server_list);
                ret = 0;
            }
            break;
        }
        cur = llist_next(cur);
    }
    return ret;
}

/* PINT_bucket_get_num_io()
 *
 * discovers the number of io servers available for a given file system
 *
 * returns 0 on success, -errno on failure
 */
int PINT_bucket_get_num_io(PVFS_fs_id fsid, int *num_io)
{
    int ret = -EINVAL;
    struct llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs = NULL;

    /* make sure the specified fs_id is sane */
    if (!PINT_server_config_is_valid_collection_id(
            &g_server_config,(TROVE_coll_id)fsid))
    {
        gossip_lerr("PINT_bucket_get_num_meta() called with invalid fs_id.\n");
        return(-EINVAL);
    }

    cur = g_server_config.file_systems;
    while(cur)
    {
        cur_fs = llist_head(cur);
        if (!cur_fs)
        {
            break;
        }
        if (fsid == (PVFS_fs_id)cur_fs->coll_id)
        {
            if (cur_fs->data_server_list)
            {
                *num_io = llist_count(cur_fs->data_server_list);
                ret = 0;
            }
            break;
        }
        cur = llist_next(cur);
    }
    return ret;
}

/* PINT_bucket_get_server_name()
 *
 * discovers the string (BMI url) name of a server that controls the
 * specified bucket.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_bucket_get_server_name(
	char* server_name,
	int max_server_name_len,
	PVFS_handle handle,
	PVFS_fs_id fsid)
{
    int ret = -EINVAL;

    /* make sure the specified fs_id is sane */
    if (!PINT_server_config_is_valid_collection_id(
            &g_server_config,(TROVE_coll_id)fsid))
    {
        gossip_lerr("PINT_bucket_get_server_name() called with invalid fs_id.\n");
        return(-EINVAL);
    }

    /*
      hash from fsid to struct
      
    */

    memcpy(server_name,HACK_server_name,max_server_name_len);
    server_name[max_server_name_len] = '\0';
    ret = 0;

    return ret;
}

/* PINT_bucket_get_root_handle()
 *
 * return the root handle of any filesystem
 *
 * returns 0 on success -errno on failure
 *
 */
int PINT_bucket_get_root_handle(
	PVFS_fs_id fsid,
	PVFS_handle *fh_root)
{
    int ret = -EINVAL;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;

    if (fh_root)
    {
        hash_link = qhash_search(s_fsid_config_cache_table,&(fsid));
        if (hash_link)
        {
            cur_config_cache =
                qlist_entry(hash_link, struct config_fs_cache_s,
                            hash_link);
            assert(cur_config_cache);
            assert(cur_config_cache->fs);

            *fh_root = (PVFS_handle)cur_config_cache->fs->root_handle;
            ret = 0;
        }
    }
    return ret;
}

/* hash_fsid()
 *
 * hash function for fsids added to table
 *
 * returns integer offset into table
 */
static int hash_fsid(void *fsid, int table_size)
{
    /* TODO: update this later with a better hash function,
     * depending on what fsids look like, for now just modding
     *
     */
    unsigned long tmp = 0;
    PVFS_fs_id *real_fsid = (PVFS_fs_id *)fsid;

    tmp += (*(real_fsid));
    tmp = tmp%table_size;

    return ((int)tmp);
}

/* hash_fsid_compare()
 *
 * performs a comparison of a hash table entro to a given key
 * (used for searching)
 *
 * returns 1 if match found, 0 otherwise
 */
static int hash_fsid_compare(void *key, struct qlist_head *link)
{
    config_fs_cache_s *fs_info = NULL;
    PVFS_fs_id *real_fsid = (PVFS_fs_id *)key;

    fs_info = qlist_entry(link, config_fs_cache_s, hash_link);
    if((PVFS_fs_id)fs_info->fs->coll_id == *real_fsid)
    {
        return(1);
    }

    return(0);
}

#endif
