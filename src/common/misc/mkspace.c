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

static char *dir_ent_string = "dir_ent";
static char *root_handle_string = "root_handle";


#define mkspace_print(v, format, f...)              \
do {                                                \
 if (v == PVFS2_MKSPACE_GOSSIP_VERBOSE)             \
   gossip_debug(GOSSIP_SERVER_DEBUG, format, ##f);  \
 else if (v == PVFS2_MKSPACE_STDERR_VERBOSE)        \
   fprintf(stderr,format, ##f);                     \
} while(0)


int pvfs2_mkspace(
    char *storage_space,
    char *collection,
    TROVE_coll_id coll_id,
    TROVE_handle root_handle,
    char *handle_ranges,
    int create_collection_only,
    int verbose)
{
    int ret = - 1, count = 0;
    TROVE_op_id op_id;
    TROVE_handle new_root_handle, ent_handle;
    TROVE_ds_state state;
    TROVE_keyval_s key, val;
    char *method_name = NULL;
    TROVE_ds_attributes_s attr;
    TROVE_handle_extent cur_extent;
    TROVE_handle_extent_array extent_array;
    TROVE_context_id trove_context = -1;

    mkspace_print(verbose,"Storage space: %s\n",storage_space);
    mkspace_print(verbose,"Collection   : %s\n",collection);
    mkspace_print(verbose,"ID           : %d\n",coll_id);
    mkspace_print(verbose,"Root Handle  : %Lu\n",Lu(root_handle));
    mkspace_print(verbose,"Handle Ranges: %s\n",handle_ranges);

    new_root_handle = (TROVE_handle)root_handle;

    /*
      if we're only creating a collection inside an existing
      storage space, we need to assume that it exists already
    */
    if (!create_collection_only)
    {
        /*
          try to initialize; fails if storage space isn't there, which
          is exactly what we're expecting in this case.
        */
        ret = trove_initialize(storage_space, 0, &method_name, 0);
        if (ret > -1)
        {
            gossip_err("error: storage space %s already "
                       "exists; aborting!\n",storage_space);
            return -1;
        }

        ret = trove_storage_create(storage_space, NULL, &op_id);
        if (ret != 1)
        {
            gossip_err("error: storage create failed; aborting!\n");
            return -1;
        }
    }

    /* now that the storage space exists, initialize trove properly */
    ret = trove_initialize(storage_space, 0, &method_name, 0);
    if (ret < 0)
    {
	gossip_err("error: trove initialize failed; aborting!\n");
	return -1;
    }

    mkspace_print(verbose,"info: created storage space '%s'.\n",
                  storage_space);

    /* try to look up collection used to store file system */
    ret = trove_collection_lookup(collection, &coll_id, NULL, &op_id);
    if (ret != -1)
    {
	mkspace_print(verbose, "warning: collection lookup succeeded "
                      "before it should; aborting!\n");
	trove_finalize();
	return -1;
    }

    /* create the collection for the fs */
    ret = trove_collection_create(collection, coll_id, NULL, &op_id);
    if (ret != 1)
    {
	mkspace_print(verbose,"error: collection create failed for "
                      "collection '%s'.\n",collection);
	return -1;
    }

    /* make sure a collection lookup succeeds */
    ret = trove_collection_lookup(collection, &coll_id, NULL, &op_id);
    if (ret != 1)
    {
	mkspace_print(verbose,"error: collection lookup failed for "
                      "collection '%s' after create.\n",collection);
	return -1;
    }

    if (verbose)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG,
                     "info: created collection '%s'.\n",collection);
    }

    ret = trove_open_context(coll_id, &trove_context);
    if (ret < 0)
    {
        mkspace_print(verbose,"trove_open_context() failure.\n");
        return -1;
    }

    /*
      we have a three-step process for starting trove:
      initialize, collection_lookup, collection_setinfo
    */
    ret = trove_collection_setinfo(coll_id,
                                   trove_context,
                                   TROVE_COLLECTION_HANDLE_RANGES,
                                   handle_ranges);
    if (ret < 0)
    {
	mkspace_print(verbose, "Error adding handle ranges\n");
	return -1;
    }

    /*
      if a root_handle is specified, 1) create a dataspace to
      hold the root directory 2) create the dspace for dir
      entries, 3) set attributes on the dspace
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
            mkspace_print(verbose,
                          "dspace create (for root dir) failed.\n");
            return -1;
        }

        if (verbose)
        {
            mkspace_print(verbose,"info: created root directory "
                          "with handle %Lu.\n", Lu(new_root_handle));
        }

        /* set collection attribute for root handle */
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

        /* set root directory dspace attributes */
        memset(&attr, 0, sizeof(TROVE_ds_attributes_s));
        attr.uid = getuid();
        attr.gid = getgid();
        attr.mode = 0777;
	attr.atime = time(NULL);
	attr.ctime = time(NULL);
	attr.mtime = time(NULL);
        attr.type = PVFS_TYPE_DIRECTORY;

        ret = trove_dspace_setattr(
            coll_id, new_root_handle, &attr, TROVE_SYNC, NULL,
            trove_context, &op_id);

        while (ret == 0)
        {
            ret = trove_dspace_test(coll_id, op_id, trove_context,
                                    &count, NULL, NULL, &state,
                                    TROVE_DEFAULT_TEST_TIMEOUT);
        }

        if (ret < 0)
        {
            gossip_err("error: dspace setattr for root handle "
                       "attributes failed; aborting!\n");
            return -1;
        }

        /*
          create a dataspace to hold directory entries; a handle value
          of TROVE_HANDLE_NULL leaves the allocation to trove.
        */
        cur_extent.first = cur_extent.last = TROVE_HANDLE_NULL;
        extent_array.extent_count = 1;
        extent_array.extent_array = &cur_extent;

        ret = trove_dspace_create(
            coll_id, &extent_array, &ent_handle, PVFS_TYPE_DIRDATA, NULL,
            TROVE_SYNC, NULL, trove_context, &op_id);

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
            mkspace_print(verbose,"info: created dspace for dirents "
                          "with handle %Lu.\n", Lu(ent_handle));
        }

        key.buffer    = dir_ent_string;
        key.buffer_sz = strlen(dir_ent_string) + 1;
        val.buffer    = &ent_handle;
        val.buffer_sz = sizeof(TROVE_handle);

        ret = trove_keyval_write(
            coll_id, new_root_handle, &key, &val, TROVE_SYNC, 0, NULL,
            trove_context, &op_id);

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
            mkspace_print(verbose, "info: wrote attributes for "
                          "root directory.\n");
        }
    }

    if (trove_context != -1)
    {
        trove_close_context(coll_id, trove_context);
    }
    trove_finalize();

    if (verbose)
    {
        mkspace_print(verbose, "collection created:\n"
                      "\troot handle = %Lu, coll id = %d, "
                      "root string = \"%s\"\n",
                      Lu(root_handle), coll_id, root_handle_string);
    }
    return 0;
}

int pvfs2_rmspace(
    char *storage_space,
    char *collection,
    TROVE_coll_id coll_id,
    int remove_collection_only,
    int verbose)
{
    int ret = -1;
    char *method_name = NULL;
    TROVE_op_id op_id;
    static int trove_is_initialized = 0;

    /* try to initialize; fails if storage space isn't there? */
    if (!trove_is_initialized)
    {
        ret = trove_initialize(storage_space, 0, &method_name, 0);
        if (ret == -1)
        {
            gossip_err("error: storage space %s does not "
                       "exist; aborting!\n", storage_space);
            return -1;
        }
        trove_is_initialized = 1;
    }

    if (verbose)
    {
        mkspace_print(verbose, "Attempting to remove collection %s\n",
                      collection);
    }

    ret = trove_collection_remove(collection, NULL, &op_id);
    if (verbose)
    {
        mkspace_print(
            verbose, "PVFS2 Collection %s removed %s\n", collection,
            ((ret != -1) ? "successfully" : "with errors"));
    }

    if (!remove_collection_only)
    {
        ret = trove_storage_remove(storage_space, NULL, &op_id);
        if (verbose)
        {
            gossip_debug(GOSSIP_SERVER_DEBUG,
                         "PVFS2 Storage Space %s removed %s\n",
                         storage_space,
                         ((ret != -1) ? "successfully" : "with errors"));
        }

        /*
          we should be doing a trove finalize here, but for now
          we can't because it will fail horribly during the sync/close
          calls to files that we've just removed.
     
          an extra flag to finalize, or a static var in the dbpf-mgmt
          methods could resolve this.
        */
/*         trove_finalize(); */
        trove_is_initialized = 0;
    }
    return ret;
}
