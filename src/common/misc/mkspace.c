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

#include "pvfs2-attr.h"
#include "trove.h"
#include "mkspace.h"
#include "pvfs2-debug.h"
#include "gossip.h"


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
