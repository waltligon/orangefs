/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>

#include "pvfs2-attr.h"
#include "trove.h"
#include "mkspace.h"
#include "pvfs2-debug.h"
#include "gossip.h"
#include "server-config.h"
#include "extent-utils.h"

static int is_root_handle_in_my_range(
    struct server_configuration_s *config_s,
    struct filesystem_configuration_s *fs);

int pvfs2_mkspace(
    char *storage_space,
    char *collection,
    TROVE_coll_id coll_id,
    TROVE_handle root_handle,
    char *handle_ranges,
    int create_collection_only,
    int verbose)
{
    int ret, count;
    TROVE_op_id op_id;
    TROVE_handle new_root_handle, ent_handle;
    TROVE_ds_state state;
    TROVE_keyval_s key, val;
    char *method_name;
    char metastring[] = "metadata";
    char entstring[]  = "dir_ent";
    struct PVFS_object_attr attr; /* from proto/pvfs2-attr.h */
    static char root_handle_string[PATH_MAX] = "root_handle";
    PVFS_handle_extent cur_extent;
    PVFS_handle_extent_array extent_array;
    TROVE_context_id trove_context = -1;

    if (verbose)
    {
        gossip_debug(SERVER_DEBUG,"Storage space: %s\n",storage_space);
        gossip_debug(SERVER_DEBUG,"Collection   : %s\n",collection);
        gossip_debug(SERVER_DEBUG,"Collection ID: %d\n",coll_id);
        gossip_debug(SERVER_DEBUG,"Root Handle  : %Ld\n",root_handle);
        gossip_debug(SERVER_DEBUG,"Handle Ranges: %s\n",handle_ranges);
    }

    new_root_handle = (PVFS_handle)root_handle;

    /*
      if we're only creating a collection inside an existing
      storage space, we need to assume that it exists already
    */
    if (!create_collection_only)
    {
        /* try to initialize; fails if storage space isn't there? */
        ret = trove_initialize(storage_space, 0, &method_name, 0);
        if (ret > -1)
        {
            gossip_err("error: storage space %s already "
                       "exists; aborting!\n",storage_space);
            return -1;
        }

        /*
          create the storage space
          
          Q: what good is the op_id here if we have to match
          on coll_id in test fn?
        */
        ret = trove_storage_create(storage_space, NULL, &op_id);
        if (ret != 1)
        {
            gossip_err("error: storage create failed; aborting!\n");
            return -1;
        }
    }

    /* second try at initialize, in case it failed first try. */
    ret = trove_initialize(storage_space, 0, &method_name, 0);
    if (ret < 0)
    {
	gossip_err("error: trove initialize failed; aborting!\n");
	return -1;
    }

    if (verbose)
    {
        gossip_debug(SERVER_DEBUG,"info: created storage space '%s'.\n",
                storage_space);
    }

    /* try to look up collection used to store file system */
    ret = trove_collection_lookup(collection, &coll_id, NULL, &op_id);
    if (ret != -1)
    {
	gossip_debug(SERVER_DEBUG, "error: collection lookup succeeded before it "
                "should; aborting!\n");
	trove_finalize();
	return -1;
    }

    /* create the collection for the fs */
    ret = trove_collection_create(collection, coll_id, NULL, &op_id);
    if (ret != 1)
    {
	gossip_debug(SERVER_DEBUG,"error: collection create failed for "
                "collection '%s'.\n",collection);
	return -1;
    }

    /*
      lookup collection.  this is redundant because we just gave
      it a coll. id to use, but it's a good test i guess...

      NOTE: can't test on this because we still don't know a coll_id
    */
    ret = trove_collection_lookup(collection, &coll_id, NULL, &op_id);
    if (ret != 1)
    {
	gossip_debug(SERVER_DEBUG,"error: collection lookup failed for "
                "collection '%s' after create.\n",collection);
	return -1;
    }

    if (verbose)
    {
        gossip_debug(SERVER_DEBUG,"info: created collection '%s'.\n",collection);
    }

    ret = trove_open_context(coll_id, &trove_context);
    if (ret < 0)
    {
        gossip_debug(SERVER_DEBUG,"trove_open_context() failure.\n");
        return -1;
    }

    /* we have a three-step process for starting trove:
     * initialize, collection_lookup, collection_setinfo */
    ret = trove_collection_setinfo(coll_id,
                                   trove_context,
                                   TROVE_COLLECTION_HANDLE_RANGES,
                                   handle_ranges);
    if (ret < 0)
    {
	gossip_debug(SERVER_DEBUG, "Error adding handle ranges\n");
	return -1;
    }

    /*
      if a root_handle is specified, 1) create a dataspace to
      hold the root directory 2) create the dspace for dir
      entries, 3) set attributes on the dspace

      Q: where are we going to define the dspace types? --
      trove-test.h for now.
    */
    if (new_root_handle != (TROVE_handle)0)
    {
        cur_extent.first = cur_extent.last = new_root_handle;
        extent_array.extent_count = 1;
        extent_array.extent_array = &cur_extent;

        ret = trove_dspace_create(
            coll_id,
            &extent_array,
            &new_root_handle,
            PVFS_TYPE_DIRECTORY,
            NULL,
            (TROVE_SYNC | TROVE_FORCE_REQUESTED_HANDLE),
            NULL,
            trove_context,
            &op_id);

        while (ret == 0)
        {
            ret = trove_dspace_test(coll_id, op_id, trove_context,
                                    &count, NULL, NULL, &state,
                                    TROVE_DEFAULT_TEST_TIMEOUT);
        }
        if (ret != 1 && state != 0)
        {
            gossip_debug(SERVER_DEBUG,
                         "dspace create (for root dir) failed.\n");
            return -1;
        }

        if (verbose)
        {
            gossip_debug(SERVER_DEBUG,"info: created root directory "
                         "with handle %Ld.\n", new_root_handle);
        }

        /*
          add attribute to collection for root handle
          NOTE: should be using the data_sz field, but it doesn't exist yet.
        */
        key.buffer = root_handle_string;
        key.buffer_sz = strlen(root_handle_string) + 1;
        val.buffer = &new_root_handle;
        val.buffer_sz = sizeof(new_root_handle);
        ret = trove_collection_seteattr(coll_id, &key, &val, 0, 
                                        NULL, trove_context, &op_id);
        while (ret == 0)
        {
            ret = trove_dspace_test(coll_id, op_id, trove_context,
                                    &count, NULL, NULL, &state,
                                    TROVE_DEFAULT_TEST_TIMEOUT);
        }
        if (ret < 0)
        {
            gossip_err("error: collection seteattr (for root handle) "
                       "failed; aborting!\n");
            return -1;
        }

        memset(&attr, 0, sizeof(attr));
        attr.owner    = getuid();
        attr.group    = getgid();
        attr.perms    = 0777;
	attr.atime    = time(NULL);
	attr.ctime    = time(NULL);
	attr.mtime    = time(NULL);
        attr.objtype  = PVFS_TYPE_DIRECTORY;
	attr.mask     = PVFS_ATTR_COMMON_ALL;

        key.buffer    = metastring;
        key.buffer_sz = strlen(metastring) + 1;
        val.buffer    = &attr;
        val.buffer_sz = sizeof(attr);

        ret = trove_keyval_write(coll_id,
                                 new_root_handle,
                                 &key,
                                 &val,
                                 TROVE_SYNC,
                                 0 /* vtag */,
                                 NULL /* user ptr */,
                                 trove_context,
                                 &op_id);
        while (ret == 0)
        {
            ret = trove_dspace_test(coll_id, op_id, trove_context,
                                    &count, NULL, NULL, &state,
                                    TROVE_DEFAULT_TEST_TIMEOUT);
        }
        if (ret < 0)
        {
            gossip_err("error: keyval write for root handle attributes "
                       "failed; aborting!\n");
            return -1;
        }

        /*
          create dataspace to hold directory entries; a
          value of zero leaves the allocation to trove.
        */
        cur_extent.first = cur_extent.last = 0;
        extent_array.extent_count = 1;
        extent_array.extent_array = &cur_extent;

        ret = trove_dspace_create(coll_id,
                                  &extent_array,
                                  &ent_handle,
                                  PVFS_TYPE_DIRDATA,
                                  NULL,
                                  TROVE_SYNC,
                                  NULL,
                                  trove_context,
                                  &op_id);
        while (ret == 0)
        {
            ret = trove_dspace_test(coll_id, op_id, trove_context,
                                    &count, NULL, NULL, &state,
                                    TROVE_DEFAULT_TEST_TIMEOUT);
        }
        if (ret != 1 && state != 0)
        {
            gossip_err("dspace create (for dirent storage) failed.\n");
            return -1;
        }

        if (verbose)
        {
            gossip_debug(SERVER_DEBUG,"info: created dspace for dirents "
                         "with handle %Ld.\n", ent_handle);
        }

        key.buffer    = entstring;
        key.buffer_sz = strlen(entstring) + 1;
        val.buffer    = &ent_handle;
        val.buffer_sz = sizeof(TROVE_handle);

        ret = trove_keyval_write(coll_id,
                                 new_root_handle,
                                 &key,
                                 &val,
                                 TROVE_SYNC,
                                 0 /* vtag */,
                                 NULL /* user ptr */,
                                 trove_context,
                                 &op_id);
        while (ret == 0)
        {
            ret = trove_dspace_test(coll_id, op_id, trove_context,
                                    &count, NULL, NULL, &state,
                                    TROVE_DEFAULT_TEST_TIMEOUT);
        }
        if (ret < 0)
        {
            gossip_err("error: keyval write for handle used to store "
                       "dirents failed; aborting!\n");
            return -1;
        }

        if (verbose)
        {
            gossip_debug(SERVER_DEBUG,
                         "info: wrote attributes for root directory.\n");
        }
    }

    if (trove_context != -1)
    {
        trove_close_context(coll_id, trove_context);
    }
    trove_finalize();

    if (verbose)
    {
        gossip_debug(SERVER_DEBUG,
                     "info: collection created (root handle = %Ld, coll "
                     "id = %d, root string = %s).\n",root_handle,
                     (int)coll_id, root_handle_string);
    }
    return 0;
}


/*
  create a storage space based on configuration settings object
  with the particular host settings local to the caller
*/
int PINT_config_pvfs2_mkspace(
    struct server_configuration_s *config)
{
    int ret = 1;
    PVFS_handle root_handle = 0;
    int create_collection_only = 0;
    struct llist *cur = NULL;
    char *cur_handle_range = (char *)0;
    filesystem_configuration_s *cur_fs = NULL;

    if (config)
    {
        cur = config->file_systems;
        while(cur)
        {
            cur_fs = llist_head(cur);
            if (!cur_fs)
            {
                break;
            }

            cur_handle_range =
                PINT_config_get_meta_handle_range_str(
                    config, cur_fs);
            if (!cur_handle_range)
            {
                gossip_err("Invalid configuration handle range\n");
                break;
            }

            /*
              check if root handle is in our handle range for this fs.
              if it is, we're responsible for creating it on disk when
              creating the storage space
            */
            root_handle = (is_root_handle_in_my_range(config,cur_fs) ?
                           cur_fs->root_handle : PVFS_HANDLE_NULL);

            /*
              for the first fs/collection we encounter, create
              the storage space if it doesn't exist.
            */
            gossip_debug(SERVER_DEBUG,"\n*****************************\n");
            gossip_debug(SERVER_DEBUG,"Creating new PVFS2 %s\n",
                    (create_collection_only ? "collection" :
                     "storage space"));
            ret = pvfs2_mkspace(config->storage_path,
                                cur_fs->file_system_name,
                                cur_fs->coll_id,
                                root_handle,
                                cur_handle_range,
                                create_collection_only,
                                1);
            gossip_debug(SERVER_DEBUG,"\n*****************************\n");

            /*
              now that the storage space is created, set the
              create_collection_only variable so that subsequent
              calls to pvfs2_mkspace will not fail when it finds
              that the storage space already exists; this causes
              pvfs2_mkspace to only add the collection to the
              already existing storage space.
            */
            create_collection_only = 1;

            cur = llist_next(cur);
        }
    }
    return ret;
}

static int is_root_handle_in_my_range(
    struct server_configuration_s *config,
    struct filesystem_configuration_s *fs)
{
    int ret = 0;
    struct llist *cur = NULL;
    struct llist *extent_list = NULL;
    char *cur_host_id = (char *)0;
    host_handle_mapping_s *cur_h_mapping = NULL;

    if (config)
    {
        /*
          check if the root handle is within one of the
          specified meta host's handle ranges for this fs;
          a root handle can't exist in a data handle range!
        */
        cur = fs->meta_handle_ranges;
        while(cur)
        {
            cur_h_mapping = llist_head(cur);
            if (!cur_h_mapping)
            {
                break;
            }
            assert(cur_h_mapping->alias_mapping);
            assert(cur_h_mapping->alias_mapping->host_alias);
            assert(cur_h_mapping->alias_mapping->bmi_address);
            assert(cur_h_mapping->handle_range);

            cur_host_id = cur_h_mapping->alias_mapping->bmi_address;
            if (!cur_host_id)
            {
                gossip_err("Invalid host ID for alias %s.\n",
                           cur_h_mapping->alias_mapping->host_alias);
                break;
            }

            /* only check if this is *our* range */
            if (strcmp(config->host_id,cur_host_id) == 0)
            {
                extent_list = PINT_create_extent_list(
                    cur_h_mapping->handle_range);
                if (!extent_list)
                {
                    gossip_err("Failed to create extent list.\n");
                    break;
                }

                ret = PINT_handle_in_extent_list(
                    extent_list,fs->root_handle);
                PINT_release_extent_list(extent_list);
                if (ret == 1)
                {
                    break;
                }
            }
            cur = llist_next(cur);
        }
    }
    return ret;
}
