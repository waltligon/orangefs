/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>

#include "pvfs2-types.h"
#include "pvfs2-attr.h"
#include "pint-sysint-utils.h"
#include "bmi.h"
#include "trove.h"
#include "server-config.h"
#include "quickhash.h"
#include "extent-utils.h"
#include "pint-cached-config.h"
#include "pvfs2-internal.h"

/* really old linux distributions (jazz's RHEL 3) don't have this(!?) */
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 64
#endif

struct handle_lookup_entry
{
    PVFS_handle_extent extent;
    char* server_name;
    PVFS_BMI_addr_t server_addr;
};

struct config_fs_cache_s
{
    struct qlist_head hash_link;
    struct filesystem_configuration_s *fs;

    /* index into fs->meta_handle_ranges obj (see server-config.h) */
    PINT_llist *meta_server_cursor;

    /* index into fs->data_handle_ranges obj (see server-config.h) */
    PINT_llist *data_server_cursor;

    /*
      the following fields are used to cache arrays of unique physical
      server addresses, of particular use to the mgmt interface
    */
    phys_server_desc_s* io_server_array;
    int io_server_count;
    phys_server_desc_s* meta_server_array;
    int meta_server_count;
    phys_server_desc_s* server_array;
    int server_count;

    struct handle_lookup_entry* handle_lookup_table;
    int handle_lookup_table_size;
};

struct qhash_table *PINT_fsid_config_cache_table = NULL;

/* these are based on code from src/server/request-scheduler.c */
static int hash_fsid(
    void *fsid, int table_size);
static int hash_fsid_compare(
    void *key, struct qlist_head *link);

static int cache_server_array(PVFS_fs_id fsid);
static int handle_lookup_entry_compare(const void *p1, const void *p2);
static const struct handle_lookup_entry* find_handle_lookup_entry(
    PVFS_handle handle, PVFS_fs_id fsid);
static int load_handle_lookup_table(
    struct config_fs_cache_s *cur_config_fs_cache);

static int meta_randomized = 0;
static int io_randomized = 0;

/* PINT_cached_config_initialize()
 *
 * initializes the cached_config interface
 *
 * returns 0 on success, -errno on failure
 */
int PINT_cached_config_initialize(void)
{
    struct timeval tv;
    unsigned int seed = 0;
    char hostname[HOST_NAME_MAX];
    int ret;
    int i;
    int hostnamelen;

    if (!PINT_fsid_config_cache_table)
    {
        PINT_fsid_config_cache_table =
            qhash_init(hash_fsid_compare,hash_fsid,11);
    }

    /* include time, pid, and hostname in random seed in order to help avoid
     * collisions on object placement when many clients are launched 
     * concurrently 
     */
    gettimeofday(&tv, NULL);
    seed += tv.tv_sec;
    seed += tv.tv_usec;

    seed += getpid();

    ret = gethostname(hostname, HOST_NAME_MAX);
    if(ret == 0)
    {
        hostnamelen = strlen(hostname);
        for(i=0; i<hostnamelen; i++)
        {
            seed += (hostname[hostnamelen - i - 1] + i*256);
        }
    }
    
    srand(seed);

    return (PINT_fsid_config_cache_table ? 0 : -PVFS_ENOMEM);
}

/* PINT_cached_config_finalize()
 *
 * shuts down the cached_config interface and releases any resources
 * associated with it.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_cached_config_finalize(void)
{
    int i = 0;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;

    /* if we haven't been initialized yet, just return success */
    if (!PINT_fsid_config_cache_table)
    {
        return 0;
    }

    /*
      this is an exhaustive and slow iterate.  speed this up if
      'finalize' is something that will be done frequently.
    */
    for (i = 0; i < PINT_fsid_config_cache_table->table_size; i++)
    {
        do
        {
            hash_link = qhash_search_and_remove_at_index(
                PINT_fsid_config_cache_table, i);
            if (hash_link)
            {
                cur_config_cache = qlist_entry(
                    hash_link, struct config_fs_cache_s, hash_link);

                assert(cur_config_cache);
                assert(cur_config_cache->fs);

                /* fs object is freed by PINT_config_release */
                cur_config_cache->fs = NULL;
                /* if the 'cached server arrays' are used, free them */
                if (cur_config_cache->io_server_count &&
                    cur_config_cache->io_server_array)
                {
                    free(cur_config_cache->io_server_array);
                    cur_config_cache->io_server_array = NULL;
                }

                if (cur_config_cache->meta_server_count &&
                    cur_config_cache->meta_server_array)
                {
                    free(cur_config_cache->meta_server_array);
                    cur_config_cache->meta_server_array = NULL;
                }

                if (cur_config_cache->server_count &&
                    cur_config_cache->server_array)
                {
                    free(cur_config_cache->server_array);
                    cur_config_cache->server_array = NULL;
                }

                free(cur_config_cache->handle_lookup_table);

                free(cur_config_cache);
            }
        } while(hash_link);
    }
    qhash_finalize(PINT_fsid_config_cache_table);
    PINT_fsid_config_cache_table = NULL;

    return 0;
}

int PINT_cached_config_reinitialize(
    struct server_configuration_s *config)
{
    int ret = -PVFS_EINVAL;
    PINT_llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs = NULL;

    PINT_cached_config_finalize();

    ret = PINT_cached_config_initialize();
    if (ret == 0)
    {
        cur = config->file_systems;
        while(cur)
        {
            cur_fs = PINT_llist_head(cur);
            if (!cur_fs)
            {
                break;
            }

            ret = PINT_cached_config_handle_load_mapping(cur_fs);
            if (ret)
            {
                break;
            }
            cur = PINT_llist_next(cur);
        }
    }
    return 0;
}

/* PINT_cached_config_handle_load_mapping()
 *
 * loads a new mapping of servers to handle into this interface.  This
 * function may be called multiple times in order to add new file
 * system information at run time.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_cached_config_handle_load_mapping(
    struct filesystem_configuration_s *fs)
{
    struct config_fs_cache_s *cur_config_fs_cache = NULL;
    int ret;

    if (fs)
    {
        cur_config_fs_cache = (struct config_fs_cache_s *)
            malloc(sizeof(struct config_fs_cache_s));
        assert(cur_config_fs_cache);
        memset(cur_config_fs_cache, 0, sizeof(struct config_fs_cache_s));

        cur_config_fs_cache->fs = (struct filesystem_configuration_s *)fs;

        cur_config_fs_cache->meta_server_cursor =
            cur_config_fs_cache->fs->meta_handle_ranges;
        cur_config_fs_cache->data_server_cursor =
            cur_config_fs_cache->fs->data_handle_ranges;

        /* populate table used to speed up mapping of handle values 
         * to servers
         */ 
        ret = load_handle_lookup_table(cur_config_fs_cache); 
        if(ret < 0)
        {
            free(cur_config_fs_cache);
            gossip_err("Error: failed to load handle lookup table.\n");
            return(ret);
        }

        qhash_add(PINT_fsid_config_cache_table,
                  &(cur_config_fs_cache->fs->coll_id),
                  &(cur_config_fs_cache->hash_link));
    }

    return 0;
}

static struct host_handle_mapping_s *
PINT_cached_config_find_server(PINT_llist *handle_ranges, const char *addr)
{
    host_handle_mapping_s *cur_mapping;
    PINT_llist *server_cursor = handle_ranges;

    cur_mapping = PINT_llist_head(server_cursor);
    while(cur_mapping &&
          strcmp(cur_mapping->alias_mapping->bmi_address, addr))
    {
        server_cursor = PINT_llist_next(server_cursor);
        cur_mapping = PINT_llist_head(server_cursor);
    }
    return cur_mapping;
}

/* PINT_cached_config_get_server()
 *
 * Find the extent array for a specified server.
 * This array MUST NOT be freed by the caller, nor cached for
 * later use.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_cached_config_get_server(
    PVFS_fs_id fsid,
    const char* host,
    PVFS_ds_type type,
    PVFS_handle_extent_array *ext_array)
{
    struct host_handle_mapping_s *cur_mapping = NULL;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;
    PINT_llist* server_cursor;

    if (!ext_array)
    {
        return(-PVFS_EINVAL);
    }

    if(type != PINT_SERVER_TYPE_META && type != PINT_SERVER_TYPE_IO)
    {
        return(-PVFS_EINVAL);
    }

    hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
    if (!hash_link)
    {
        return(-PVFS_EINVAL);
    }

    cur_config_cache = qlist_entry(
       hash_link, struct config_fs_cache_s, hash_link);

    assert(cur_config_cache);
    assert(cur_config_cache->fs);

    if(type == PINT_SERVER_TYPE_META)
    {
        server_cursor = 
            cur_config_cache->fs->meta_handle_ranges;
    }
    else
    {
        server_cursor = 
            cur_config_cache->fs->data_handle_ranges;
    }

    cur_mapping = PINT_cached_config_find_server(server_cursor, host);
    /* didn't find the server */
    if(!cur_mapping)
    {
        return(-PVFS_ENOENT);
    }

    ext_array->extent_count =
        cur_mapping->handle_extent_array.extent_count;
    ext_array->extent_array =
        cur_mapping->handle_extent_array.extent_array;

    return(0);
}


/* PINT_cached_config_get_next_meta()
 *
 * returns the bmi address of a random server that should be used to
 * store a new piece of metadata.  This function is responsible for
 * fairly distributing the metadata storage responsibility to all
 * servers.
 *
 * in addition, a handle range is returned as an array of extents that
 * match the meta handle range configured for the returned meta
 * server.  This array MUST NOT be freed by the caller, nor cached for
 * later use.
 * NOTE: address resolution is skipped if meta_addr is set to NULL
 *
 * returns 0 on success, -errno on failure
 */
int PINT_cached_config_get_next_meta(
    PVFS_fs_id fsid,
    PVFS_BMI_addr_t *meta_addr,
    PVFS_handle_extent_array *ext_array)
{
    int ret = -PVFS_EINVAL, jitter = 0, num_meta_servers = 0;
    char *meta_server_bmi_str = NULL;
    struct host_handle_mapping_s *cur_mapping = NULL;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;

    if (ext_array)
    {
        hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
        if (hash_link)
        {
            cur_config_cache = qlist_entry(
                hash_link, struct config_fs_cache_s, hash_link);

            assert(cur_config_cache);
            assert(cur_config_cache->fs);
            assert(cur_config_cache->meta_server_cursor);

            num_meta_servers = PINT_llist_count(
                cur_config_cache->fs->meta_handle_ranges);

            /* pick random starting point, then round robin */
            if(!meta_randomized)
            {
                jitter = (rand() % num_meta_servers);
                meta_randomized = 1;
            }
            else
            {
                /* we let the jitter loop below increment the cursor by one */ 
                jitter = 0;
            }
            while(jitter-- > -1)
            {
                cur_mapping = PINT_llist_head(
                    cur_config_cache->meta_server_cursor);
                if (!cur_mapping)
                {
                    cur_config_cache->meta_server_cursor =
                        cur_config_cache->fs->meta_handle_ranges;
                    cur_mapping = PINT_llist_head(
                        cur_config_cache->meta_server_cursor);
                    assert(cur_mapping);
                }
                cur_config_cache->meta_server_cursor = PINT_llist_next(
                    cur_config_cache->meta_server_cursor);
            }
            meta_server_bmi_str = cur_mapping->alias_mapping->bmi_address;

            ext_array->extent_count =
                cur_mapping->handle_extent_array.extent_count;
            ext_array->extent_array =
                cur_mapping->handle_extent_array.extent_array;

	    if (meta_addr != NULL)
	    {
		ret = BMI_addr_lookup(meta_addr,meta_server_bmi_str);
	    }
	    else
	    {
		ret = 0;
	    }
        }
    }
    return ret;
}

static int PINT_cached_config_get_extents(
    PVFS_fs_id fsid,
    PVFS_BMI_addr_t *addr,
    PVFS_handle_extent_array *handle_extents)
{
    struct qhash_head *hash_link;
    struct PINT_llist *server_list;
    PVFS_BMI_addr_t tmp_addr;
    struct config_fs_cache_s *cur_config_cache = NULL;
    struct host_handle_mapping_s *cur_mapping = NULL;
    int num_io_servers, ret;

    hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
    if(!hash_link)
    {
        gossip_err("Failed to find a file system matching fsid: %d\n", fsid);
        return -PVFS_EINVAL;
    }

    cur_config_cache = qlist_entry(
        hash_link, struct config_fs_cache_s, hash_link);

    assert(cur_config_cache);
    assert(cur_config_cache->fs);

    server_list = cur_config_cache->fs->data_handle_ranges;
    num_io_servers = PINT_llist_count(server_list);

    while(!PINT_llist_empty(server_list))
    {
        cur_mapping = PINT_llist_head(server_list);
        assert(cur_mapping);
        server_list = PINT_llist_next(server_list);

        ret = BMI_addr_lookup(
            &tmp_addr, cur_mapping->alias_mapping->bmi_address);
        if(ret < 0)
        {
            return ret;
        }

        if(tmp_addr == *addr)
        {
            handle_extents->extent_count =
                cur_mapping->handle_extent_array.extent_count;
            handle_extents->extent_array =
                cur_mapping->handle_extent_array.extent_array;

            return 0;
        }
    }
    return -PVFS_ENOENT;
}

int PINT_cached_config_map_servers(
    PVFS_fs_id fsid,
    int *inout_num_datafiles,
    PVFS_sys_layout *layout,
    PVFS_BMI_addr_t *addr_array,
    PVFS_handle_extent_array *handle_extent_array)
{
    struct qhash_head *hash_link;
    struct PINT_llist *server_list;
    struct host_handle_mapping_s *cur_mapping = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;
    int num_io_servers, i, ret;
    int start_index = -1;
    int index;
    int random_attempts;

    assert(inout_num_datafiles);

    hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
    if(!hash_link)
    {
        gossip_err("Failed to find a file system matching fsid: %d\n", fsid);
        return -PVFS_EINVAL;
    }

    cur_config_cache = qlist_entry(
        hash_link, struct config_fs_cache_s, hash_link);

    assert(cur_config_cache);
    assert(cur_config_cache->fs);

    server_list = cur_config_cache->fs->data_handle_ranges;
    num_io_servers = PINT_llist_count(server_list);

    switch(layout->algorithm)
    {
        case PVFS_SYS_LAYOUT_LIST:

            if(*inout_num_datafiles < layout->server_list.count)
            {
                gossip_err("The specified datafile layout is larger"
                           " than the number of requested datafiles\n");
                return -PVFS_EINVAL;
            }

            *inout_num_datafiles = layout->server_list.count;
            for(i = 0; i < layout->server_list.count; ++i)
            {
                if(handle_extent_array)
                {
                    ret = PINT_cached_config_get_extents(
                        fsid,
                        &layout->server_list.servers[i],
                        &handle_extent_array[i]);
                    if(ret < 0)
                    {
                        gossip_err("The address specified in the datafile "
                                   "layout is invalid\n");
                        return ret;
                    }
                }

                addr_array[i] = layout->server_list.servers[i];
            }
            break;

        case PVFS_SYS_LAYOUT_NONE:
            start_index = 0;
            /* fall through */

        case PVFS_SYS_LAYOUT_ROUND_ROBIN:

            if(num_io_servers < *inout_num_datafiles)
            {
                *inout_num_datafiles = num_io_servers;
            }

            if(start_index == -1)
            {
                start_index = rand() % *inout_num_datafiles;
            }

            for(i = 0; i < *inout_num_datafiles; ++i)
            {
                cur_mapping = PINT_llist_head(server_list);
                assert(cur_mapping);
                server_list = PINT_llist_next(server_list);

                index = (i + start_index) % *inout_num_datafiles;
                ret = BMI_addr_lookup(
                    &addr_array[index],
                    cur_mapping->alias_mapping->bmi_address);
                if (ret)
                {
                    return ret;
                }

                if(handle_extent_array)
                {
                    handle_extent_array[index].extent_count =
                        cur_mapping->handle_extent_array.extent_count;
                    handle_extent_array[index].extent_array =
                        cur_mapping->handle_extent_array.extent_array;
                }
            }
            break;

        case PVFS_SYS_LAYOUT_RANDOM:
            /* this layout randomizes the order but still uses each server
             * only once
             */

            /* limit this layout to a number of datafiles no greater than
             * the number of servers
             */
            if(num_io_servers < *inout_num_datafiles)
            {
                *inout_num_datafiles = num_io_servers;
            }

            /* init all the addrs to 0, so we know whether we've set an
             * address at a particular index or not
             */
            memset(addr_array, 0, (*inout_num_datafiles)*sizeof(*addr_array));

            for(i = 0; i < *inout_num_datafiles; ++i)
            {
                /* go through server list in order */
                cur_mapping = PINT_llist_head(server_list);
                assert(cur_mapping);
                server_list = PINT_llist_next(server_list);

                /* select random index into caller's list */
                index = rand() % *inout_num_datafiles;
                random_attempts = 1;

                /* if we have already filled that index, try another random
                 * index 
                 */ 
                while(addr_array[index] != 0 && random_attempts < 6)
                {
                    index = rand() % *inout_num_datafiles;
                    random_attempts++;
                }

                /* if we exhausted a max number of randomization attempts,
                 * then just go linearly through list
                 */
                while(addr_array[index] != 0)
                {
                    index = (index + 1) % *inout_num_datafiles;
                }

                /* found an unused index */
                ret = BMI_addr_lookup(
                    &addr_array[index],
                    cur_mapping->alias_mapping->bmi_address);
                if (ret)
                {
                    return ret;
                }

                if(handle_extent_array)
                {
                    handle_extent_array[index].extent_count =
                        cur_mapping->handle_extent_array.extent_count;
                    handle_extent_array[index].extent_array =
                        cur_mapping->handle_extent_array.extent_array;
                }
            }
            break;
        default:
            gossip_err("Unknown datafile mapping algorithm\n");
            return -PVFS_EINVAL;
    }
    return 0;
}

/* PINT_cached_config_get_next_io()
 *
 * returns the address of a set of servers that should be used to
 * store new pieces of file data.  This function is responsible for
 * evenly distributing the file data storage load to all servers.
 *
 * NOTE: if io_addr_array is NULL, then don't resolve addresses
 *
 * returns 0 on success, -errno on failure
 */
int PINT_cached_config_get_next_io(
    PVFS_fs_id fsid,
    int num_servers,
    PVFS_BMI_addr_t *io_addr_array,
    PVFS_handle_extent_array *io_handle_extent_array)
{
    int ret = -PVFS_EINVAL, i = 0;
    char *data_server_bmi_str = (char *)0;
    struct host_handle_mapping_s *cur_mapping = NULL;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;
    int jitter = 0, num_io_servers = 0;
    PINT_llist* old_data_server_cursor = NULL;

    if (num_servers && io_handle_extent_array)
    {
        hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
        if (hash_link)
        {
            cur_config_cache = qlist_entry(
                hash_link, struct config_fs_cache_s, hash_link);

            assert(cur_config_cache);
            assert(cur_config_cache->fs);

            num_io_servers = PINT_llist_count(
                cur_config_cache->fs->data_handle_ranges);


            /* pick random starting point, then round robin */
            if(!io_randomized)
            {
                jitter = (rand() % num_io_servers);
                io_randomized = 1;
            }
            else
            {
                jitter = 0;
            }
            while(jitter-- > -1)
            {
                cur_mapping = PINT_llist_head(
                    cur_config_cache->data_server_cursor);
                if (!cur_mapping)
                {
                    cur_config_cache->data_server_cursor =
                        cur_config_cache->fs->data_handle_ranges;
                    cur_mapping = PINT_llist_head(
                        cur_config_cache->data_server_cursor);
                    assert(cur_mapping);
                }
                cur_config_cache->data_server_cursor = PINT_llist_next(
                    cur_config_cache->data_server_cursor);
            }
            old_data_server_cursor = cur_config_cache->data_server_cursor;

            while(num_servers)
            {
                assert(cur_config_cache->data_server_cursor);

                cur_mapping = PINT_llist_head(
                    cur_config_cache->data_server_cursor);
                if (!cur_mapping)
                {
                    cur_config_cache->data_server_cursor =
                        cur_config_cache->fs->data_handle_ranges;
                    continue;
                }
                cur_config_cache->data_server_cursor = PINT_llist_next(
                    cur_config_cache->data_server_cursor);

                data_server_bmi_str = cur_mapping->alias_mapping->bmi_address;

		if (io_addr_array != NULL)
		{
		    ret = BMI_addr_lookup(
                        io_addr_array,data_server_bmi_str);
		    if (ret)
		    {
			break;
		    }
		}

                io_handle_extent_array[i].extent_count =
                    cur_mapping->handle_extent_array.extent_count;
                io_handle_extent_array[i].extent_array =
                    cur_mapping->handle_extent_array.extent_array;

                i++;
                num_servers--;
		if(io_addr_array != NULL)
		    io_addr_array++;
            }
            ret = ((num_servers == 0) ? 0 : ret);
            /* reset data server cursor to point to the old cursor; the
             * jitter on the next iteration will increment it by one
             */
            cur_config_cache->data_server_cursor = old_data_server_cursor;
        }
    }
    return ret;
}

/* PINT_cached_config_map_addr()
 *
 * takes an opaque server address and returns the server type and
 * address string for that server
 *
 * returns pointer to string on success, NULL on failure
 */
const char *PINT_cached_config_map_addr(
    PVFS_fs_id fsid,
    PVFS_BMI_addr_t addr,
    int *server_type)
{
    int ret = -PVFS_EINVAL, i = 0;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;

    hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
    if (!hash_link)
    {
        return NULL;
    }
    cur_config_cache = qlist_entry(
        hash_link, struct config_fs_cache_s, hash_link);
    assert(cur_config_cache);
    assert(cur_config_cache->fs);

    ret = cache_server_array(fsid);
    if (ret < 0)
    {
        return NULL;
    }

    /* run through general server list for a match */
    for(i = 0; i < cur_config_cache->server_count; i++)
    {
        if (cur_config_cache->server_array[i].addr == addr)
        {
            if(server_type)
            {
                *server_type = cur_config_cache->server_array[i].server_type;
            }
            return (cur_config_cache->server_array[i].addr_string);
        }
    }
    return NULL;
}


/* PINT_cached_config_check_type()
 *
 * Retrieves the server type flags for a specified BMI addr string
 *
 * returns 0 on success, -errno on failure
 */
int PINT_cached_config_check_type(
    PVFS_fs_id fsid,
    const char *server_addr_str,
    int* server_type)
{
    int ret = -PVFS_EINVAL, i = 0;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;

    hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
    if (!hash_link)
    {
        return(-PVFS_EINVAL);
    }
    cur_config_cache = qlist_entry(
        hash_link, struct config_fs_cache_s, hash_link);
    assert(cur_config_cache);
    assert(cur_config_cache->fs);

    ret = cache_server_array(fsid);
    if (ret < 0)
    {
        return(ret);
    }

    /* run through general server list for a match */
    for(i = 0; i < cur_config_cache->server_count; i++)
    {
        if (!(strcmp(cur_config_cache->server_array[i].addr_string,
           server_addr_str)))
        {
            *server_type = cur_config_cache->server_array[i].server_type;
            return(0);
        }
    }
    return(-PVFS_EINVAL);
}


/* PINT_cached_config_count_servers()
 *
 * counts the number of physical servers of the specified type
 *
 * returns 0 on success, -errno on failure
 */
int PINT_cached_config_count_servers(
    PVFS_fs_id fsid, 
    int server_type,
    int *count)
{
    int ret = -PVFS_EINVAL;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;

    if (!server_type)
    {
        return ret;
    }

    hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
    if (!hash_link)
    {
        return ret;
    }
    cur_config_cache = qlist_entry(
        hash_link, struct config_fs_cache_s, hash_link);

    assert(cur_config_cache);
    assert(cur_config_cache->fs);

    ret = cache_server_array(fsid);
    if (ret == 0)
    {
        if (server_type == PINT_SERVER_TYPE_META)
        {
            *count = cur_config_cache->meta_server_count;
            ret = 0;
        }
        else if (server_type == PINT_SERVER_TYPE_IO)
        {
            *count = cur_config_cache->io_server_count;
            ret = 0;
        }
        else if (server_type == PINT_SERVER_TYPE_ALL)
        {
            *count = cur_config_cache->server_count;
            ret = 0;
        }
    }
    return ret;
}

/* PINT_cached_config_get_server_array()
 *
 * fills in an array of addresses corresponding to each server of the
 * type specified by "server_type" (meta,io,or both).  If
 * inout_count_p is not large enough to accomodate array, then an
 * error is returned.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_cached_config_get_server_array(
    PVFS_fs_id fsid,
    int server_type,
    PVFS_BMI_addr_t *addr_array,
    int *inout_count_p)
{
    int ret = -PVFS_EINVAL, i = 0;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;

    if (!inout_count_p || !*inout_count_p ||
        !addr_array || !server_type)
    {
        return ret;
    }

    hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
    if (!hash_link)
    {
        return ret;
    }
    cur_config_cache = qlist_entry(
        hash_link, struct config_fs_cache_s, hash_link);

    assert(cur_config_cache);
    assert(cur_config_cache->fs);

    ret = cache_server_array(fsid);
    if (ret < 0)
    {
        return ret;
    }

    /* at this point, we should have the data that we need cached up,
     * just copy out
     */
    if (server_type == PINT_SERVER_TYPE_META)
    {
        if (*inout_count_p < cur_config_cache->meta_server_count)
        {
            return -PVFS_EMSGSIZE;
        }

        for(i = 0; i < cur_config_cache->meta_server_count; i++)
        {
            addr_array[i] = cur_config_cache->meta_server_array[i].addr;
        }

        *inout_count_p = cur_config_cache->meta_server_count;
        return 0;
    }
    else if (server_type == PINT_SERVER_TYPE_IO)
    {
        if (*inout_count_p < cur_config_cache->io_server_count)
        {
            return -PVFS_EMSGSIZE;
        }

        for(i = 0; i < cur_config_cache->io_server_count; i++)
        {
            addr_array[i] = cur_config_cache->io_server_array[i].addr;
        }

        *inout_count_p = cur_config_cache->io_server_count;
        return 0;
    }
    else if (server_type == PINT_SERVER_TYPE_ALL)
    {
        if (*inout_count_p < cur_config_cache->server_count)
        {
            return -PVFS_EMSGSIZE;
        }

        for(i = 0; i < cur_config_cache->server_count; i++)
        {
            addr_array[i] =
                cur_config_cache->server_array[i].addr;
        }

        *inout_count_p = cur_config_cache->server_count;
        return 0;
    }
    return ret;
}

/* PINT_cached_config_map_to_server()
 *
 * maps from a handle and fsid to a server address
 *
 * returns 0 on success to -errno on failure
 */
int PINT_cached_config_map_to_server(
    PVFS_BMI_addr_t *server_addr,
    PVFS_handle handle,
    PVFS_fs_id fs_id)
{
    const struct handle_lookup_entry* tmp_entry;

    tmp_entry = find_handle_lookup_entry(handle, fs_id);

    if(!tmp_entry)
    {
        gossip_err("Error: failed to find handle %llu in fs configuration.\n",
            llu(handle));
        return(-PVFS_EINVAL);
    }

    *server_addr = tmp_entry->server_addr;
    return(0);
}

/* PINT_cached_config_get_num_dfiles()
 *
 * Returns 0 if the number of dfiles has been successfully set
 *
 * Sets the number of dfiles to a distribution approved the value.  Clients
 * may pass in num_dfiles_requested as a hint, if no hint is given, the server
 * configuration is checked to find a hint there.  The distribution will
 * choose a correct number of dfiles even if no hint is set.
 */
int PINT_cached_config_get_num_dfiles(
    PVFS_fs_id fsid,
    PINT_dist *dist,
    int num_dfiles_requested,
    int *num_dfiles)
{
    int rc;
    int num_io_servers;
    
    /* If the dfile request is zero, check to see if the config has that
       setting */
    if (0 == num_dfiles_requested)
    {
        struct qlist_head *hash_link = NULL;
        struct config_fs_cache_s *cur_config_cache = NULL;
        
        /* Locate the filesystem configuration for this fs id */
        hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
        if (hash_link)
        {
            cur_config_cache = qlist_entry(
                hash_link, struct config_fs_cache_s, hash_link);
            assert(cur_config_cache);
            assert(cur_config_cache->fs);
            num_dfiles_requested = cur_config_cache->fs->default_num_dfiles;
        }
    }

    /* Determine the number of I/O servers available */
    rc = PINT_cached_config_get_num_io(fsid, &num_io_servers);
    if(rc < 0)
    {
        return(rc);
    }
    
    /* Allow the distribution to apply its hint to the number of
       dfiles requested and the number of I/O servers available */
    *num_dfiles = dist->methods->get_num_dfiles(dist->params,
                                                num_io_servers,
                                                num_dfiles_requested);
    if(*num_dfiles < 1)
    {
        gossip_err("Error: distribution failure for %d servers and %d requested datafiles.\n", num_io_servers, num_dfiles_requested);
        return(-PVFS_EINVAL);
    }

    return 0;
}

/* PINT_cached_config_get_num_meta()
 *
 * discovers the number of metadata servers available for a given file
 * system
 *
 * returns 0 on success, -errno on failure
 */
int PINT_cached_config_get_num_meta(
    PVFS_fs_id fsid,
    int *num_meta)
{
    int ret = -PVFS_EINVAL;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;

    if (num_meta)
    {
        hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
        if (hash_link)
        {
            cur_config_cache = qlist_entry(
                hash_link, struct config_fs_cache_s, hash_link);

            assert(cur_config_cache);
            assert(cur_config_cache->fs);
            assert(cur_config_cache->fs->meta_handle_ranges);

            *num_meta = PINT_llist_count(
                cur_config_cache->fs->meta_handle_ranges);
            ret = 0;
        }
    }
    return ret;
}

/* PINT_cached_config_get_num_io()
 *
 * discovers the number of io servers available for a given file
 * system
 *
 * returns 0 on success, -errno on failure
 */
int PINT_cached_config_get_num_io(PVFS_fs_id fsid, int *num_io)
{
    int ret = -PVFS_EINVAL;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;

    if (num_io)
    {
        hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
        if (hash_link)
        {
            cur_config_cache = qlist_entry(
                hash_link, struct config_fs_cache_s, hash_link);

            assert(cur_config_cache);
            assert(cur_config_cache->fs);
            assert(cur_config_cache->fs->data_handle_ranges);

            *num_io = PINT_llist_count(
                cur_config_cache->fs->data_handle_ranges);
            ret = 0;
        }
    }
    return ret;
}

/* PINT_cached_config_get_server_handle_count()
 *
 * counts the number of handles associated with a given server
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_cached_config_get_server_handle_count(
    const char *server_addr_str,
    PVFS_fs_id fs_id,
    uint64_t *handle_count)
{
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;
    struct host_handle_mapping_s *server_mapping = NULL;

    *handle_count = 0;

    assert(PINT_fsid_config_cache_table);

    /* for each fs find the right server */
    hash_link = qhash_search(PINT_fsid_config_cache_table,&(fs_id));
    if (hash_link)
    {
        cur_config_cache = qlist_entry(
            hash_link, struct config_fs_cache_s, hash_link);

        assert(cur_config_cache);
        assert(cur_config_cache->fs);

        server_mapping = PINT_cached_config_find_server(
            cur_config_cache->fs->meta_handle_ranges, server_addr_str);
        if(server_mapping)
        {
            *handle_count += PINT_extent_array_count_total(
                &server_mapping->handle_extent_array);
        }

        server_mapping = PINT_cached_config_find_server(
            cur_config_cache->fs->data_handle_ranges, server_addr_str);
        if(server_mapping)
        {
            *handle_count += PINT_extent_array_count_total(
                &server_mapping->handle_extent_array);
        }
    }
    return 0;
}

/* PINT_cached_config_get_server_name()
 *
 * discovers the string (BMI url) name of a server that controls the
 * specified cached_config.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_cached_config_get_server_name(
    char *server_name,
    int max_server_name_len,
    PVFS_handle handle,
    PVFS_fs_id fsid)
{
    const struct handle_lookup_entry* tmp_entry;

    tmp_entry = find_handle_lookup_entry(handle, fsid);

    if(!tmp_entry)
    {
        gossip_err("Error: failed to find handle %llu in fs configuration.\n",
            llu(handle));
        return(-PVFS_EINVAL);
    }

    strncpy(server_name, tmp_entry->server_name, max_server_name_len);
    return(0);
}

/* PINT_cached_config_get_root_handle()
 *
 * return the root handle of any valid filesystem
 *
 * returns 0 on success -errno on failure
 *
 */
int PINT_cached_config_get_root_handle(
    PVFS_fs_id fsid,
    PVFS_handle *fh_root)
{
    int ret = -PVFS_EINVAL;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;

    if (fh_root)
    {
        hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
        if (hash_link)
        {
            cur_config_cache = qlist_entry(
                hash_link, struct config_fs_cache_s, hash_link);

            assert(cur_config_cache);
            assert(cur_config_cache->fs);

            *fh_root = (PVFS_handle)cur_config_cache->fs->root_handle;
            ret = 0;
        }
    }
    return ret;
}

int PINT_cached_config_get_handle_timeout(
    PVFS_fs_id fsid,
    struct timeval *timeout)
{
    int ret = -PVFS_EINVAL;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;

    hash_link = qhash_search(PINT_fsid_config_cache_table, &(fsid));
    if(hash_link)
    {
        cur_config_cache = qlist_entry(
            hash_link, struct config_fs_cache_s, hash_link);

        assert(cur_config_cache);
        assert(cur_config_cache->fs);

        timeout->tv_sec = 
            cur_config_cache->fs->handle_recycle_timeout_sec.tv_sec;
        timeout->tv_usec =
            cur_config_cache->fs->handle_recycle_timeout_sec.tv_usec;
        ret = 0;
    }
    return ret;
}

int PINT_cached_config_get_server_list(
    PVFS_fs_id fs_id,
    PINT_dist *dist,
    int num_dfiles_req,
    PVFS_sys_layout *layout,
    const char ***server_names,
    int *server_count)
{
    int num_io_servers, ret, i;
    PVFS_BMI_addr_t *server_addrs;
    const char **servers;

    /* find the server list from the layout */
    ret = PINT_cached_config_get_num_dfiles(
        fs_id,
        dist,
        num_dfiles_req,
        &num_io_servers);
    if (ret < 0)
    {
        gossip_err("Failed to get number of data servers\n");
        return ret;
    }

    if(num_io_servers > PVFS_REQ_LIMIT_DFILE_COUNT)
    {
        num_io_servers = PVFS_REQ_LIMIT_DFILE_COUNT;
        gossip_err("Warning: reducing number of data "
                     "files to PVFS_REQ_LIMIT_DFILE_COUNT\n");
    }

    server_addrs = malloc(sizeof(*server_addrs) * num_io_servers);
    if(!server_addrs)
    {
        gossip_err("Failed to allocate server address list\n");
        return -PVFS_ENOMEM;
    }

    ret = PINT_cached_config_map_servers(
        fs_id,
        &num_io_servers,
        layout,
        server_addrs,
        NULL);
    if(ret != 0)
    {
        gossip_err("Failed to get IO server addrs from layout\n");
        return ret;
    }

    servers = malloc(sizeof(*servers) * num_io_servers);
    if(!servers)
    {
        gossip_err("Failed to allocate server address list\n");
        free(server_addrs);
        return -PVFS_ENOMEM;
    }

    for(i = 0; i < num_io_servers; ++i)
    {
        servers[i] = BMI_addr_rev_lookup(server_addrs[i]);
    }
    free(server_addrs);

    *server_count = num_io_servers;
    *server_names = servers;

    return 0;
}

/* cache_server_array()
 *
 * verifies that the arrays of physical server addresses have been
 * cached in the configuration structs
 *
 * returns 0 on success, -errno on failure
 */
static int cache_server_array(
    PVFS_fs_id fsid)
{
    int ret = -PVFS_EINVAL, i = 0, j = 0;
    char *server_bmi_str = NULL;
    struct host_handle_mapping_s *cur_mapping = NULL;
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;
    PINT_llist *tmp_server = NULL;
    PVFS_BMI_addr_t tmp_bmi_addr;
    int dup_flag = 0;
    int current = 0;
    int array_index = 0, array_index2 = 0;

    hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
    if (!hash_link)
    {
        return ret;
    }
    cur_config_cache = qlist_entry(
        hash_link, struct config_fs_cache_s, hash_link);

    assert(cur_config_cache);
    assert(cur_config_cache->fs);

    /* first check to see if we have the array information cached */
    if (cur_config_cache->server_count < 1)
    {
        /* we need to fill in this stuff in our config cache */
        cur_config_cache->server_count = 0;
        cur_config_cache->meta_server_count = 0;
        cur_config_cache->io_server_count = 0;

        /* iterate through lists to come up with an upper bound for
         * the size of the arrays that we need
         */
        tmp_server = cur_config_cache->fs->meta_handle_ranges;
        while ((cur_mapping = PINT_llist_head(tmp_server)))
        {
            tmp_server = PINT_llist_next(tmp_server);
            cur_config_cache->meta_server_count++;
            cur_config_cache->server_count++;
        }
        tmp_server = cur_config_cache->fs->data_handle_ranges;
        while ((cur_mapping = PINT_llist_head(tmp_server)))
        {
            tmp_server = PINT_llist_next(tmp_server);
            cur_config_cache->io_server_count++;
            cur_config_cache->server_count++;
        }

        cur_config_cache->meta_server_array = (phys_server_desc_s*)malloc(
            (cur_config_cache->meta_server_count *
             sizeof(phys_server_desc_s)));
        cur_config_cache->io_server_array = (phys_server_desc_s*)malloc(
            (cur_config_cache->io_server_count*
             sizeof(phys_server_desc_s)));
        cur_config_cache->server_array = (phys_server_desc_s*)malloc(
            (cur_config_cache->server_count*
             sizeof(phys_server_desc_s)));

        if ((cur_config_cache->meta_server_array == NULL) ||
            (cur_config_cache->io_server_array == NULL) ||
            (cur_config_cache->server_array == NULL))
        {
            ret = -PVFS_ENOMEM;
            goto cleanup_allocations;
        }
        memset(cur_config_cache->server_array, 0, 
               (cur_config_cache->server_count *
                sizeof(phys_server_desc_s)));

        /* reset counts until we find out how many physical servers
         * are actually present
         */
        cur_config_cache->server_count = 0;
        cur_config_cache->meta_server_count = 0;
        cur_config_cache->io_server_count = 0;

        for(i = 0; i < 2; i++)
        {
            if (i == 0)
            {
                tmp_server = cur_config_cache->fs->meta_handle_ranges;
                current = PINT_SERVER_TYPE_META;
            }
            else
            {
                tmp_server = cur_config_cache->fs->data_handle_ranges;
                current = PINT_SERVER_TYPE_IO;
            }
            while ((cur_mapping = PINT_llist_head(tmp_server)))
            {
                tmp_server = PINT_llist_next(tmp_server);
                server_bmi_str = cur_mapping->alias_mapping->bmi_address;

                ret = BMI_addr_lookup(&tmp_bmi_addr,server_bmi_str);
                if (ret < 0)
                {
                    return(ret);
                }

                /* see if we have already listed this BMI address */
                dup_flag = 0;
                for (j=0; j < array_index; j++)
                {
                    if (cur_config_cache->server_array[j].addr ==
                        tmp_bmi_addr)
                    {
                        cur_config_cache->server_array[j].server_type 
                            |= current;
                        dup_flag = 1;
                        break;
                    }
                }
                
                if (!dup_flag)
                {
                    cur_config_cache->server_array[array_index].addr =
                        tmp_bmi_addr;
                    cur_config_cache->server_array[
                        array_index].addr_string = server_bmi_str;
                    cur_config_cache->server_array[
                        array_index].server_type = current;
                    array_index++;
                    cur_config_cache->server_count = array_index;
                }
            }
        }

        /* now build meta and I/O arrays based on generic server list */
        array_index = 0;
        array_index2 = 0;
        for(i = 0; i < cur_config_cache->server_count; i++)
        {
            if (cur_config_cache->server_array[i].server_type &
                PINT_SERVER_TYPE_META)
            {
                cur_config_cache->meta_server_array[array_index] = 
                    cur_config_cache->server_array[i];
                array_index++;
            }
            if (cur_config_cache->server_array[i].server_type &
                PINT_SERVER_TYPE_IO)
            {
                cur_config_cache->io_server_array[array_index2] = 
                    cur_config_cache->server_array[i];
                array_index2++;
            }
        }
        cur_config_cache->meta_server_count = array_index;
        cur_config_cache->io_server_count = array_index2;
    }
    return 0;

  cleanup_allocations:
    if (cur_config_cache->meta_server_array)
    {
        free(cur_config_cache->meta_server_array);
    }
    if (cur_config_cache->io_server_array)
    {
        free(cur_config_cache->io_server_array);
    }
    if (cur_config_cache->server_array)
    {
        free(cur_config_cache->server_array);
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
    unsigned long tmp = 0;
    PVFS_fs_id *real_fsid = (PVFS_fs_id *)fsid;

    assert(PINT_fsid_config_cache_table);

    tmp += (*(real_fsid));
    tmp = tmp%table_size;

    return ((int) tmp);
}

/* hash_fsid_compare()
 *
 * performs a comparison of a hash table entro to a given key (used
 * for searching)
 *
 * returns 1 if match found, 0 otherwise
 */
static int hash_fsid_compare(void *key, struct qlist_head *link)
{
    struct config_fs_cache_s *fs_info = NULL;
    PVFS_fs_id *real_fsid = (PVFS_fs_id *)key;

    assert(PINT_fsid_config_cache_table);

    fs_info = qlist_entry(link, struct config_fs_cache_s, hash_link);
    if ((PVFS_fs_id)fs_info->fs->coll_id == *real_fsid)
    {
        return 1;
    }
    return 0;
}

/* handle_lookup_entry_compare()
 *  *
 *   * comparison function used by qsort()
 *    */
static int handle_lookup_entry_compare(const void *p1, const void *p2)
{
    const struct handle_lookup_entry* e1  = p1;
    const struct handle_lookup_entry* e2  = p2;

    if(e1->extent.first < e2->extent.first)
        return(-1);
    if(e1->extent.first > e2->extent.first)
        return(1);

    return(0);
}

/* find_handle_lookup_entry()
 *
 * searches sorted table for extent that contains the specified handle
 *
 * returns pointer to table entry on success, NULL on failure
 */
static const struct handle_lookup_entry* find_handle_lookup_entry(
    PVFS_handle handle, PVFS_fs_id fsid)
{
    struct qlist_head *hash_link = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;
    int high, low, mid;
    int table_index;

    assert(PINT_fsid_config_cache_table);

    hash_link = qhash_search(PINT_fsid_config_cache_table,&(fsid));
    if(!hash_link)
    {
        return(NULL);
    }

    cur_config_cache = qlist_entry(
        hash_link, struct config_fs_cache_s, hash_link);

    assert(cur_config_cache);
    assert(cur_config_cache->fs);

    /* iterative binary search through handle lookup table to find the 
     * extent that this handle falls into 
     */
    low = 0;
    high = cur_config_cache->handle_lookup_table_size;
    while (low < high) 
    {
        mid = (low + high)/2;
        if (cur_config_cache->handle_lookup_table[mid].extent.first < handle)
            low = mid + 1; 
        else
            high = mid;
    }
    if ((low < cur_config_cache->handle_lookup_table_size) && 
        (cur_config_cache->handle_lookup_table[low].extent.first == handle))
    {
        /* we happened to locate the first handle in a range */
        table_index = low;
    }
    else
    {
        /* this handle must fall into the previous range if any */
        table_index = low-1;
    }

    /* confirm match */
    if(PINT_handle_in_extent(
        &cur_config_cache->handle_lookup_table[table_index].extent,
        handle))
    {
        return(&cur_config_cache->handle_lookup_table[table_index]);
    }

    /* no match */
    return(NULL);
}

/* load_handle_lookup_table()
 *
 * iterates through extents for all servers and constructs a table sorted by
 * the first handle in each extent.  This table can then be searched with a
 * binary algorithm to map handles to servers.  Table includes extent,
 * server name, and server's resolved bmi address.
 *
 * returns 0 on success, -PVFS_error on failure
 */
static int load_handle_lookup_table(
    struct config_fs_cache_s *cur_config_fs_cache)
{
    int ret = -PVFS_EINVAL;
    host_handle_mapping_s *cur_mapping = NULL;
    int count = 0;
    int table_offset = 0;
    PINT_llist* server_cursor;
    int i;
    int j;
    PINT_llist* range_list[2] = 
    {
        cur_config_fs_cache->fs->meta_handle_ranges,
        cur_config_fs_cache->fs->data_handle_ranges
    };

    /* count total number of extents */
    /* loop through both meta and data ranges */
    for(j=0; j<2; j++)
    {
        server_cursor = range_list[j];
        cur_mapping = PINT_llist_head(server_cursor);
        while(cur_mapping)
        {
            /* each server may have multiple extents */
            for(i=0; i<cur_mapping->handle_extent_array.extent_count; i++)
            {
                count += 1;
            }
            server_cursor = PINT_llist_next(server_cursor);
            cur_mapping = PINT_llist_head(server_cursor);
        }
    }

    /* allocate a table to hold all extents for faster searching */
    if(cur_config_fs_cache->handle_lookup_table)
    {
        free(cur_config_fs_cache->handle_lookup_table);
    }
    cur_config_fs_cache->handle_lookup_table = 
        malloc(sizeof(*cur_config_fs_cache->handle_lookup_table) * count);
    if(!cur_config_fs_cache->handle_lookup_table)
    {
        return(-PVFS_ENOMEM);
    }
    cur_config_fs_cache->handle_lookup_table_size = count;

    /* populate table */
    /* loop through both meta and data ranges */
    for(j=0; j<2; j++)
    {
        server_cursor = range_list[j];
        cur_mapping = PINT_llist_head(server_cursor);
        while(cur_mapping)
        {
            for(i=0; i<cur_mapping->handle_extent_array.extent_count; i++)
            {
                cur_config_fs_cache->handle_lookup_table[table_offset].extent 
                    = cur_mapping->handle_extent_array.extent_array[i];
                cur_config_fs_cache->handle_lookup_table[table_offset].server_name
                    = cur_mapping->alias_mapping->bmi_address;
                ret = BMI_addr_lookup(
                    &cur_config_fs_cache->handle_lookup_table[table_offset].server_addr,                 
                    cur_config_fs_cache->handle_lookup_table[table_offset].server_name);
                if(ret < 0)
                {
                    free(cur_config_fs_cache->handle_lookup_table);
                    gossip_err("Error: failed to resolve address of server: %s\n",
                        cur_config_fs_cache->handle_lookup_table[table_offset].server_name);
                    return(ret);
                }
                table_offset++;
            }
            server_cursor = PINT_llist_next(server_cursor);
            cur_mapping = PINT_llist_head(server_cursor);
        }
    }

    /* sort table */
    qsort(cur_config_fs_cache->handle_lookup_table, table_offset, 
        sizeof(*cur_config_fs_cache->handle_lookup_table),
        handle_lookup_entry_compare);

    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
