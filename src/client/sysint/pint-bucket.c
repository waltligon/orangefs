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

static PVFS_handle HACK_handle_mask = 0;
static PVFS_handle HACK_bucket = 0;

static struct qhash_table *s_fsid_config_cache_table = NULL;

/* these are based on code from src/server/request-scheduler.c */
static int hash_fsid(void *fsid, int table_size);
static int hash_fsid_compare(void *key, struct qlist_head *link);

static void free_host_extent_table(void *ptr);

/*
  FIXME:
  there's a header file problem that needs to be resolved...
  for now, I'm forward declaring functions to fix warnings.
  this is a sin.
*/
int PINT_bucket_get_server_name(char* server_name, int max_server_name_len,
                                PVFS_handle bucket, PVFS_fs_id fsid);
int PINT_handle_load_mapping(void *fs);


/* PINT_bucket_initialize()
 *
 * initializes the bucket interface
 *
 * returns 0 on success, -errno on failure
 */
int PINT_bucket_initialize(void)
{
    int ret = -EINVAL;

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
    llist_doall(g_server_config.file_systems, PINT_handle_load_mapping);
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
    int i;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;

    /*
      this is an exhaustive and slow iterate.  speed this up
      if 'finalize' is something that will be done frequently.
    */
    for (i = 0; i < s_fsid_config_cache_table->table_size; i++)
    {
        hash_link = qhash_search(s_fsid_config_cache_table,&(i));
        if (hash_link)
        {
            cur_config_cache =
                qlist_entry(hash_link, struct config_fs_cache_s,
                            hash_link);
            assert(cur_config_cache);
            assert(cur_config_cache->fs);
            assert(cur_config_cache->bmi_host_extent_tables);

            /* fs object is freed by PINT_server_config_release */
            cur_config_cache->fs = NULL;
            llist_free(cur_config_cache->bmi_host_extent_tables,
                       free_host_extent_table);
        }
    }
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
int PINT_handle_load_mapping(void *fs)
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

        cur_config_fs_cache->fs = (struct filesystem_configuration_s *)fs;
        cur_config_fs_cache->meta_server_cursor = NULL;
        cur_config_fs_cache->data_server_cursor = NULL;
        cur_config_fs_cache->bmi_host_extent_tables = llist_new();
        assert(cur_config_fs_cache->bmi_host_extent_tables);

        cur = cur_config_fs_cache->fs->handle_ranges;
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
            cur_config_fs_cache->meta_server_cursor =
                cur_config_fs_cache->fs->meta_server_list;
            cur_config_fs_cache->data_server_cursor =
                cur_config_fs_cache->fs->data_server_list;

            qhash_add(s_fsid_config_cache_table,
                      &(cur_config_fs_cache->fs->coll_id),
                      &(cur_config_fs_cache->hash_link));
        }
    }
    return ret;
}


/* PINT_bucket_get_next_meta()
 *
 * returns the bmi address of the next server
 * that should be used to store a new piece of metadata.  This 
 * function is responsible for fairly distributing the metadata 
 * storage responsibility to all servers.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_bucket_get_next_meta(
    PVFS_fs_id fsid,
    bmi_addr_t *meta_addr)
{
    int ret = -EINVAL;
    char *meta_server_bmi_str = (char *)0;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;

    if (meta_addr)
    {
        hash_link = qhash_search(s_fsid_config_cache_table,&(fsid));
        if (hash_link)
        {
            cur_config_cache =
                qlist_entry(hash_link, struct config_fs_cache_s,
                            hash_link);
            assert(cur_config_cache);
            assert(cur_config_cache->fs);
            assert(cur_config_cache->meta_server_cursor);

            meta_server_bmi_str =
                llist_head(cur_config_cache->meta_server_cursor);
            if (!meta_server_bmi_str)
            {
                cur_config_cache->meta_server_cursor =
                    cur_config_cache->fs->meta_server_list;
                meta_server_bmi_str =
                    llist_head(cur_config_cache->meta_server_cursor);
                assert(meta_server_bmi_str);
            }
            cur_config_cache->meta_server_cursor =
                llist_next(cur_config_cache->meta_server_cursor);

            meta_server_bmi_str = PINT_server_config_get_host_addr_ptr(
                &g_server_config,meta_server_bmi_str);

            ret = BMI_addr_lookup(meta_addr,meta_server_bmi_str);
        }
    }
    return ret;
}

/* PINT_bucket_get_next_io()
 *
 * returns the address of a set of servers that
 * should be used to store new pieces of file data.  This function is
 * responsible for evenly distributing the file data storage load to all
 * servers.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_bucket_get_next_io(
    PVFS_fs_id fsid,
    int num_servers,
    bmi_addr_t *io_addr_array)
{
    int ret = -EINVAL;
    char *data_server_bmi_str = (char *)0;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;

    if (num_servers && io_addr_array)
    {
        hash_link = qhash_search(s_fsid_config_cache_table,&(fsid));
        if (hash_link)
        {
            cur_config_cache =
                qlist_entry(hash_link, struct config_fs_cache_s,
                            hash_link);
            assert(cur_config_cache);
            assert(cur_config_cache->fs);

            while(num_servers)
            {
                assert(cur_config_cache->data_server_cursor);

                data_server_bmi_str =
                    llist_head(cur_config_cache->data_server_cursor);
                if (!data_server_bmi_str)
                {
                    cur_config_cache->data_server_cursor =
                        cur_config_cache->fs->data_server_list;
                    continue;
                }
                cur_config_cache->data_server_cursor =
                    llist_next(cur_config_cache->data_server_cursor);

                data_server_bmi_str = PINT_server_config_get_host_addr_ptr(
                    &g_server_config,data_server_bmi_str);

                ret = BMI_addr_lookup(io_addr_array,data_server_bmi_str);
                if (ret)
                {
                    break;
                }
                io_addr_array++;
                num_servers--;
            }
            ret = ((num_servers == 0) ? 0 : ret);
        }
    }
    return ret;
}


/* PINT_bucket_map_to_server()
 *
 * maps from a handle and fsid to a server address
 *
 * returns 0 on success to -errno on failure
 */
int PINT_bucket_map_to_server(
	bmi_addr_t* server_addr,
	PVFS_handle handle,
	PVFS_fs_id fsid)
{
    int ret = -EINVAL;
    char bmi_server_addr[1024] = {0};
    /*
      FIXME: is there a limits.h file that defines the max length
      of a legal bmi_server URL?  If so, replace '1024' with it
    */
    ret = PINT_bucket_get_server_name(bmi_server_addr,1024,handle,fsid);
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
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;

    if (num_meta)
    {
        hash_link = qhash_search(s_fsid_config_cache_table,&(fsid));
        if (hash_link)
        {
            cur_config_cache =
                qlist_entry(hash_link, struct config_fs_cache_s,
                            hash_link);
            assert(cur_config_cache);
            assert(cur_config_cache->fs);

            *num_meta = llist_count(cur_config_cache->fs->meta_server_list);
            ret = 0;
        }
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
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;

    if (num_io)
    {
        hash_link = qhash_search(s_fsid_config_cache_table,&(fsid));
        if (hash_link)
        {
            cur_config_cache =
                qlist_entry(hash_link, struct config_fs_cache_s,
                            hash_link);
            assert(cur_config_cache);
            assert(cur_config_cache->fs);

            *num_io = llist_count(cur_config_cache->fs->data_server_list);
            ret = 0;
        }
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
                strncpy(server_name,cur_host_extent_table->bmi_address,
                        max_server_name_len);
                ret = 0;
                break;
            }
            cur = llist_next(cur);
        }
    }
    return ret;
}

/* PINT_bucket_get_root_handle()
 *
 * return the root handle of any valid filesystem
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

static void free_extent_list(void *ptr)
{
    struct llist *extent_list = (struct llist *)ptr;
    if (extent_list)
    {
        PINT_release_extent_list(extent_list);
    }
}

static void free_host_extent_table(void *ptr)
{
    struct bmi_host_extent_table_s *cur_host_extent_table =
        (struct bmi_host_extent_table_s *)ptr;

    assert(cur_host_extent_table);
    assert(cur_host_extent_table->bmi_address);
    assert(cur_host_extent_table->extent_list);

    /*
      NOTE: cur_host_extent_table->bmi_address is a ptr
      into a g_server_config->host_aliases object.
      it is properly freed by PINT_server_config_release
    */
    cur_host_extent_table->bmi_address = (char *)0;
    llist_free(cur_host_extent_table->extent_list,
               free_extent_list);
    free(cur_host_extent_table);
}

#endif
