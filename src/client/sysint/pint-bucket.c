/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <string.h>
#include <assert.h>

#include "pvfs2-types.h"
#include "pvfs2-attr.h"
#include "pint-sysint-utils.h"
#include "bmi.h"
#include "gossip.h"
#include "dotconf.h"
#include "trove.h"
#include "server-config.h"
#include "llist.h"
#include "quickhash.h"
#include "extent-utils.h"
#include "pint-bucket.h"

struct qhash_table *PINT_fsid_config_cache_table = NULL;

/* these are based on code from src/server/request-scheduler.c */
static int hash_fsid(void *fsid, int table_size);
static int hash_fsid_compare(void *key, struct qlist_head *link);

static void free_host_extent_table(void *ptr);


/* PINT_bucket_initialize()
 *
 * initializes the bucket interface
 *
 * returns 0 on success, -errno on failure
 */
int PINT_bucket_initialize(void)
{
    if (!PINT_fsid_config_cache_table)
    {
        PINT_fsid_config_cache_table =
            qhash_init(hash_fsid_compare,hash_fsid,67);
    }
    return (PINT_fsid_config_cache_table ? 0 : -ENOMEM);
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
    for (i = 0; i < PINT_fsid_config_cache_table->table_size; i++)
    {
        hash_link = qhash_search(PINT_fsid_config_cache_table,&(i));
        if (hash_link)
        {
            cur_config_cache =
                qlist_entry(hash_link, struct config_fs_cache_s,
                            hash_link);
            assert(cur_config_cache);
            assert(cur_config_cache->fs);
            assert(cur_config_cache->bmi_host_extent_tables);

            /* fs object is freed by PINT_config_release */
            cur_config_cache->fs = NULL;
            PINT_llist_free(cur_config_cache->bmi_host_extent_tables,
                       free_host_extent_table);
        }
    }
    qhash_finalize(PINT_fsid_config_cache_table);
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
int PINT_handle_load_mapping(
    struct server_configuration_s *config,
    struct filesystem_configuration_s *fs)
{
    int ret = -EINVAL;
    PINT_llist *cur = NULL;
    struct host_handle_mapping_s *cur_mapping = NULL;
    struct config_fs_cache_s *cur_config_fs_cache = NULL;
    struct bmi_host_extent_table_s *cur_host_extent_table = NULL;

    if (config && fs)
    {
        cur_config_fs_cache = (struct config_fs_cache_s *)
            malloc(sizeof(struct config_fs_cache_s));
        assert(cur_config_fs_cache);

        cur_config_fs_cache->fs = (struct filesystem_configuration_s *)fs;
        cur_config_fs_cache->meta_server_cursor = NULL;
        cur_config_fs_cache->data_server_cursor = NULL;
        cur_config_fs_cache->bmi_host_extent_tables = PINT_llist_new();
        assert(cur_config_fs_cache->bmi_host_extent_tables);

        /*
          map all meta and data handle ranges to the extent list, if any.
          map_handle_range_to_extent_list is a macro defined in
          pint-bucket.h for convenience only.
        */
        map_handle_range_to_extent_list(
            cur_config_fs_cache->fs->meta_handle_ranges);

        map_handle_range_to_extent_list(
            cur_config_fs_cache->fs->data_handle_ranges);

        /*
          add config cache object to the hash table
          that maps fsid to a config_fs_cache_s
        */
        if (ret == 0)
        {
            cur_config_fs_cache->meta_server_cursor =
                cur_config_fs_cache->fs->meta_handle_ranges;
            cur_config_fs_cache->data_server_cursor =
                cur_config_fs_cache->fs->data_handle_ranges;

            qhash_add(PINT_fsid_config_cache_table,
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
 * in addition, a handle range is returned as an array of extents
 * that match the meta handle range configured for the returned
 * meta server.  This array MUST NOT be freed by the caller, nor
 * cached for later use.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_bucket_get_next_meta(
    struct server_configuration_s *config,
    PVFS_fs_id fsid,
    bmi_addr_t *meta_addr,
    PVFS_handle_extent_array *meta_handle_extent_array)
{
    int ret = -EINVAL;
    char *meta_server_bmi_str = NULL;
    struct host_handle_mapping_s *cur_mapping = NULL;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;

    if (config && meta_addr && meta_handle_extent_array)
    {
        hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
        if (hash_link)
        {
            cur_config_cache =
                qlist_entry(hash_link, struct config_fs_cache_s,
                            hash_link);
            assert(cur_config_cache);
            assert(cur_config_cache->fs);
            assert(cur_config_cache->meta_server_cursor);

            cur_mapping =
                PINT_llist_head(cur_config_cache->meta_server_cursor);
            if (!cur_mapping)
            {
                cur_config_cache->meta_server_cursor =
                    cur_config_cache->fs->meta_handle_ranges;
                cur_mapping =
                    PINT_llist_head(cur_config_cache->meta_server_cursor);
                assert(cur_mapping);
            }
            cur_config_cache->meta_server_cursor =
                PINT_llist_next(cur_config_cache->meta_server_cursor);

            meta_server_bmi_str = PINT_config_get_host_addr_ptr(
                config,cur_mapping->alias_mapping->host_alias);

            meta_handle_extent_array->extent_count =
                cur_mapping->handle_extent_array.extent_count;
            meta_handle_extent_array->extent_array =
                cur_mapping->handle_extent_array.extent_array;

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
    struct server_configuration_s *config,
    PVFS_fs_id fsid,
    int num_servers,
    bmi_addr_t *io_addr_array,
    PVFS_handle_extent_array *io_handle_extent_array)
{
    int ret = -EINVAL, i = 0;
    char *data_server_bmi_str = (char *)0;
    struct host_handle_mapping_s *cur_mapping = NULL;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;

    if (config && num_servers && io_addr_array && io_handle_extent_array)
    {
        hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
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

                cur_mapping =
                    PINT_llist_head(cur_config_cache->data_server_cursor);
                if (!cur_mapping)
                {
                    cur_config_cache->data_server_cursor =
                        cur_config_cache->fs->data_handle_ranges;
                    continue;
                }
                cur_config_cache->data_server_cursor =
                    PINT_llist_next(cur_config_cache->data_server_cursor);

                data_server_bmi_str = PINT_config_get_host_addr_ptr(
                    config,cur_mapping->alias_mapping->host_alias);

                ret = BMI_addr_lookup(io_addr_array,data_server_bmi_str);
                if (ret)
                {
                    break;
                }

                io_handle_extent_array[i].extent_count =
                    cur_mapping->handle_extent_array.extent_count;
                io_handle_extent_array[i].extent_array =
                    cur_mapping->handle_extent_array.extent_array;

                i++;
                io_addr_array++;
                num_servers--;
            }
            ret = ((num_servers == 0) ? 0 : ret);
        }
    }
    return ret;
}


/* PINT_bucket_get_physical()
 *
 * returns the BMI addresses of all of the specified types of servers 
 * for a given file system (up to incount servers), without any duplicates 
 * (ie, servers with multiple handle ranges show up just once)
 *
 * NOTE: get_num_io() and get_num_meta() can be used to get an upper bound 
 * on the number of servers in the file system to use for the incount argument
 *
 * returns 0 on success, -errno on failure
 */
int PINT_bucket_get_physical(
    struct server_configuration_s *config,
    PVFS_fs_id fsid,
    int incount,
    int* outcount,
    struct PINT_bucket_server_info* info_array,
    int server_type)
{
    int ret = -EINVAL;
    char *server_bmi_str = (char *)0;
    struct host_handle_mapping_s *cur_mapping = NULL;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;
    PINT_llist* tmp_server = NULL;
    bmi_addr_t tmp_bmi_addr;
    int dup_flag = 0;
    int i;
    int current = 0;

    *outcount = 0;

    if (!(config && incount && info_array && server_type))
    {
	return(-EINVAL);
    }

    hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
    if (hash_link)
    {
	cur_config_cache =
	    qlist_entry(hash_link, struct config_fs_cache_s,
			hash_link);
	assert(cur_config_cache);
	assert(cur_config_cache->fs);

	while(server_type)
	{
	    if(server_type & PINT_BUCKET_IO)
	    {
		tmp_server = cur_config_cache->fs->data_handle_ranges;
		server_type -= PINT_BUCKET_IO;
		current = PINT_BUCKET_IO;
	    }
	    else if(server_type & PINT_BUCKET_META)
	    {
		tmp_server = cur_config_cache->fs->meta_handle_ranges;
		server_type -= PINT_BUCKET_META;
		current = PINT_BUCKET_META;
	    }
	    else
	    {
		return(-EINVAL);
	    }
	    assert(tmp_server);

	    while(*outcount < incount)
	    {
		cur_mapping =
		    PINT_llist_head(tmp_server);
		if (!cur_mapping)
		{
		    /* we hit the end of the list */
		    break;
		}
		tmp_server = PINT_llist_next(tmp_server);

		server_bmi_str = PINT_config_get_host_addr_ptr(
		    config,cur_mapping->alias_mapping->host_alias);

		ret = BMI_addr_lookup(&tmp_bmi_addr,server_bmi_str);
		if (ret < 0)
		{
		    return(ret);
		}

		/* see if we have already listed this BMI address */
		dup_flag = 0;
		for(i=0; i<*outcount; i++)
		{
		    if(info_array[i].addr == tmp_bmi_addr)
		    {
			dup_flag = 1;
			info_array[i].server_type |= current;
		    }
		}
		
		if(!dup_flag)
		{
		    info_array[*outcount].addr = tmp_bmi_addr;
		    info_array[*outcount].addr_string = server_bmi_str;
		    info_array[*outcount].server_type = current;
		    (*outcount)++;
		}
	    }
	}
    }
    return 0;
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
    char bmi_server_addr[PVFS_MAX_SERVER_ADDR_LEN] = {0};

    ret = PINT_bucket_get_server_name(bmi_server_addr,
                                      PVFS_MAX_SERVER_ADDR_LEN,handle,fsid);

    return (!ret ? BMI_addr_lookup(server_addr, bmi_server_addr) : ret);
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
        hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
        if (hash_link)
        {
            cur_config_cache =
                qlist_entry(hash_link, struct config_fs_cache_s,
                            hash_link);
            assert(cur_config_cache);
            assert(cur_config_cache->fs);
            assert(cur_config_cache->fs->meta_handle_ranges);

            *num_meta =
                PINT_llist_count(cur_config_cache->fs->meta_handle_ranges);
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
        hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
        if (hash_link)
        {
            cur_config_cache =
                qlist_entry(hash_link, struct config_fs_cache_s,
                            hash_link);
            assert(cur_config_cache);
            assert(cur_config_cache->fs);
            assert(cur_config_cache->fs->data_handle_ranges);

            *num_io = PINT_llist_count(cur_config_cache->fs->data_handle_ranges);
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
    PINT_llist *cur = NULL;
    struct bmi_host_extent_table_s *cur_host_extent_table = NULL;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;

    assert(PINT_fsid_config_cache_table);

    hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
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
            cur_host_extent_table = PINT_llist_head(cur);
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
            cur = PINT_llist_next(cur);
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
        hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
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

/* PINT_bucket_build_virt_server_list()
 *
 * allocates a string containing a space delimited list of pvfs2 server
 * addresses for the given fsid.  server_type controls whether listing is
 * of meta servers or I/O servers.  Caller must free allocated string.
 *
 * returns pointer to string on success, NULL on failure
 */
char* PINT_bucket_build_virt_server_list(
    struct server_configuration_s *config,
    PVFS_fs_id fsid,
    int server_type)
{
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;
    PINT_llist* tmp_server = NULL;
    char* ret_str = NULL;
    int ret_str_len = 0;
    char* server_bmi_str;
    int count = 0;
    struct host_handle_mapping_s *cur_mapping = NULL;

    hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
    if(!hash_link)
    {
	return(NULL);
    }
    cur_config_cache = qlist_entry(hash_link, struct config_fs_cache_s,
	hash_link);
    assert(cur_config_cache);
    assert(cur_config_cache->fs);

    if(server_type == PINT_BUCKET_META)
    {
	tmp_server = cur_config_cache->fs->meta_handle_ranges;
    }
    else if(server_type == PINT_BUCKET_IO)
    {
	tmp_server = cur_config_cache->fs->data_handle_ranges;
    }
    else
    {
	return(NULL);
    }
    assert(tmp_server);

    /* first, run through the list to figure out how long our string
     * must be
     */
    while((cur_mapping = PINT_llist_head(tmp_server)))
    {
	tmp_server = PINT_llist_next(tmp_server);
	server_bmi_str = PINT_config_get_host_addr_ptr(
	    config,cur_mapping->alias_mapping->host_alias);
	count++;
	ret_str_len += strlen(server_bmi_str) + 1;
    }
    ret_str_len++;

    if(ret_str_len <= 1)
	return(NULL);

    ret_str = (char*)malloc(ret_str_len*sizeof(char));	
    if(!ret_str)
	return(NULL);
    memset(ret_str, 0, ret_str_len*sizeof(char));

    if(server_type == PINT_BUCKET_META)
    {
	tmp_server = cur_config_cache->fs->meta_handle_ranges;
    }
    else if(server_type == PINT_BUCKET_IO)
    {
	tmp_server = cur_config_cache->fs->data_handle_ranges;
    }

    /* now cycle through again, this time filling in string */
    while((cur_mapping = PINT_llist_head(tmp_server)))
    {
	tmp_server = PINT_llist_next(tmp_server);
	server_bmi_str = PINT_config_get_host_addr_ptr(
	    config,cur_mapping->alias_mapping->host_alias);
	count--;
	strcat(ret_str, server_bmi_str);
	if(count)
	    strcat(ret_str, " ");
    }
 
    return(ret_str);
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

static void free_host_extent_table(void *ptr)
{
    struct bmi_host_extent_table_s *cur_host_extent_table =
        (struct bmi_host_extent_table_s *)ptr;

    assert(cur_host_extent_table);
    assert(cur_host_extent_table->bmi_address);
    assert(cur_host_extent_table->extent_list);

    /*
      NOTE: cur_host_extent_table->bmi_address is a ptr
      into a server_configuration_s->host_aliases object.
      it is properly freed by PINT_config_release
    */
    cur_host_extent_table->bmi_address = (char *)0;
    PINT_release_extent_list(cur_host_extent_table->extent_list);
    free(cur_host_extent_table);
}
