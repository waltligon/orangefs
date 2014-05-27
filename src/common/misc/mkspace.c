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
#include <sys/stat.h>

#include "pvfs2-internal.h"
#include "pvfs2-attr.h"
#include "trove.h"
#include "mkspace.h"
#include "pvfs2-debug.h"
#include "gossip.h"
#include "server-config.h"
#include "str-utils.h"
#include "extent-utils.h"
#include "pvfs2-util.h"
#include "pint-util.h"
#include "pint-event.h"
#include "dist-dir-utils.h"

/*
static char *lost_and_found_string = "lost+found";
*/

static TROVE_handle s_used_handles[4] =
{
    TROVE_HANDLE_NULL, TROVE_HANDLE_NULL,
    TROVE_HANDLE_NULL, TROVE_HANDLE_NULL
};

#define mkspace_print(v, format, f...)              \
do {                                                \
 if (v == PVFS2_MKSPACE_GOSSIP_VERBOSE)             \
   gossip_debug(GOSSIP_SERVER_DEBUG, format, ##f);  \
 else if (v == PVFS2_MKSPACE_STDERR_VERBOSE)        \
   fprintf(stderr,format, ##f);                     \
} while(0)

#if 0
static int handle_is_excluded(
    TROVE_handle handle, TROVE_handle *handles_to_exclude,
    int num_handles_to_exclude)
{
    int excluded = 0;

    while((num_handles_to_exclude - 1) > -1)
    {
        if (handle == handles_to_exclude[num_handles_to_exclude-1])
        {
            excluded = 1;
            break;
        }
        num_handles_to_exclude--;
    }
    return excluded;
}

static void get_handle_extent_from_ranges(
    char *handle_ranges, TROVE_handle_extent *out_extent,
    TROVE_handle *handles_to_exclude, int num_handles_to_exclude)
{
    PINT_llist *cur = NULL;
    TROVE_handle_extent *tmp_extent = NULL;
    PINT_llist *extent_list = NULL;

    if (handle_ranges && out_extent)
    {
        out_extent->first = TROVE_HANDLE_NULL;
        out_extent->last = TROVE_HANDLE_NULL;

        extent_list = PINT_create_extent_list(handle_ranges);
        if (extent_list)
        {
            cur = extent_list;
            while(cur)
            {
                tmp_extent = PINT_llist_head(cur);
                if (!tmp_extent)
                {
                    break;
                }

                /*
                  allow any handle range in this list that can allow
                  at least the single handle allocation to pass.  a
                  range of 1 is ok, so long as it's not a handle that
                  was previously allocated (i.e. in the specified
                  excluded list)
                */
                if (((tmp_extent->last - tmp_extent->first) > 0) ||
                    ((tmp_extent->last > 0) &&
                     (tmp_extent->last == tmp_extent->first) &&
                     !handle_is_excluded(
                         tmp_extent->last, handles_to_exclude,
                         num_handles_to_exclude)))
                {
                    out_extent->first = tmp_extent->first;
                    out_extent->last = tmp_extent->last;
                    break;
                }
                cur = PINT_llist_next(cur);
            }
            PINT_release_extent_list(extent_list);
        }
    }
}
#endif

int pvfs2_mkspace(char *data_path,
                  char *meta_path,
                  char *collection,
                  TROVE_coll_id coll_id,
                  TROVE_handle root_handle,
                  char *meta_handle_ranges,
                  char *data_handle_ranges,
                  int create_collection_only,
                  int verbose)
{
    int ret = - 1, count = 0;
    TROVE_op_id op_id;
    TROVE_ds_state state;
    TROVE_keyval_s key, val;
    TROVE_keyval_s *key_a = NULL, *val_a = NULL;
    TROVE_ds_attributes_s attr;
    TROVE_handle_extent cur_extent;
    TROVE_handle_extent_array extent_array;
    TROVE_context_id trove_context = -1;
    char *merged_handle_ranges = NULL;
    TROVE_handle new_root_handle = TROVE_HANDLE_NULL;
    struct stat root_stat;
    struct stat meta_stat;
    struct stat data_stat;

    mkspace_print(verbose,"Data storage space     : %s\n",data_path);
    mkspace_print(verbose,"Metadata storage space : %s\n", meta_path);
    mkspace_print(verbose,"Collection   : %s\n",collection);
    mkspace_print(verbose,"ID           : %d\n",coll_id);
    mkspace_print(verbose,"Root Handle  : %llu\n",llu(root_handle));
    mkspace_print(verbose,"Meta Handles : %s\n",
                  (meta_handle_ranges && strlen(meta_handle_ranges) ?
                   meta_handle_ranges : "NONE"));
    mkspace_print(verbose,"Data Handles : %s\n",
                  (data_handle_ranges && strlen(data_handle_ranges) ?
                   data_handle_ranges : "NONE"));

    new_root_handle = root_handle;


    /* init stat buffers */
    memset(&root_stat, 0, sizeof(root_stat));
    memset(&meta_stat, 0, sizeof(meta_stat));

    /* call stat on root, meta, and data paths */
    stat("/", &root_stat);
    stat(meta_path, &meta_stat);

    /* see if the metadata path is located on the root device */
    if (meta_stat.st_dev == root_stat.st_dev)
    {
        mkspace_print(PVFS2_MKSPACE_STDERR_VERBOSE,
                      "*** WARNING *** *** WARNING *** *** WARNING ***\n");
        mkspace_print(PVFS2_MKSPACE_STDERR_VERBOSE,
                      "*The MetadataStorageSpace path %s appears\n"
                      "      to be on the root device.\n", meta_path);
        mkspace_print(PVFS2_MKSPACE_STDERR_VERBOSE,
                      "*It is recommended that the meta data be\n"
                      "      stored on a dedicated partition.\n");
        mkspace_print(PVFS2_MKSPACE_STDERR_VERBOSE,
                      "*If you have a dedicated partition setup,\n"
                      "      please be sure it is mounted.\n\n");   
    }

    if (!create_collection_only)
    {
        memset(&data_stat, 0, sizeof(data_stat));
        stat(data_path, &data_stat);
        /* see if the data path is located on the root device */
        if (data_stat.st_dev == root_stat.st_dev)
        {
            mkspace_print(PVFS2_MKSPACE_STDERR_VERBOSE,
                          "*** WARNING *** *** WARNING *** *** WARNING ***\n");
            mkspace_print(PVFS2_MKSPACE_STDERR_VERBOSE,
                          "*The DataStorageSpace path %s appears\n"
                          "      to be on the root device.\n", data_path);
            mkspace_print(PVFS2_MKSPACE_STDERR_VERBOSE,
                          "*It is recommended that the data be\n"
                          "      stored on a dedicated partition.\n");
            mkspace_print(PVFS2_MKSPACE_STDERR_VERBOSE,
                          "*If you have a dedicated partition setup,\n"
                          "      please be sure it is mounted.\n\n");   
        }
    }

    /*
     * if we're only creating a collection inside an existing
     * storage space, we need to assume that it exists already
     */
    if (!create_collection_only)
    {
        /*
          try to initialize; fails if storage space isn't there, which
          is exactly what we're expecting in this case.
        */
        ret = trove_initialize(TROVE_METHOD_DBPF, 
			       NULL, 
			       data_path,
			       meta_path,
			       0);
        if (ret > -1)
        {
            gossip_err("error: storage space %s or %s already "
                       "exists; aborting!\n",data_path,meta_path);
            return -1;
        }

        ret = trove_storage_create(TROVE_METHOD_DBPF, data_path, meta_path, NULL, &op_id);
        if (ret != 1)
        {
            gossip_err("error: storage create failed; aborting!\n");
            return -1;
        }
    }

    /* now that the storage space exists, initialize trove properly */
    ret = trove_initialize(TROVE_METHOD_DBPF,
                           NULL, 
	                   data_path,
                           meta_path,
                           0);
    if (ret < 0)
    {
	gossip_err("error: trove initialize failed; aborting!\n");
	return -1;
    }

    mkspace_print(verbose,"info: created data storage space '%s'.\n",
                  data_path);
    mkspace_print(verbose,"info: created metadata storage space '%s'.\n",
                  meta_path);

    /* try to look up collection used to store file system */
    ret = trove_collection_lookup(TROVE_METHOD_DBPF,
                                  collection,
                                  &coll_id,
                                  NULL,
                                  &op_id);
    if (ret == 1)
    {
	mkspace_print(verbose, "warning: collection lookup succeeded "
                      "before it should; aborting!\n");
	trove_finalize(TROVE_METHOD_DBPF);
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
    ret = trove_collection_lookup(
	TROVE_METHOD_DBPF, collection, &coll_id, NULL, &op_id);
    if (ret != 1)
    {
	mkspace_print(verbose,"error: collection lookup failed for "
                      "collection '%s' after create.\n",collection);
	return -1;
    }

    mkspace_print(verbose, "info: created collection '%s'.\n",collection);

    ret = trove_open_context(coll_id, &trove_context);
    if (ret < 0)
    {
        mkspace_print(verbose,"trove_open_context() failure.\n");
        return -1;
    }

    /* merge the specified ranges to pass to the handle allocator */
    if ((meta_handle_ranges && strlen(meta_handle_ranges)) &&
        (data_handle_ranges && strlen(data_handle_ranges)))
    {
        merged_handle_ranges = PINT_merge_handle_range_strs(
            meta_handle_ranges, data_handle_ranges);
    }
    else if (meta_handle_ranges && strlen(meta_handle_ranges))
    {
        merged_handle_ranges = strdup(meta_handle_ranges);
    }
    else if (data_handle_ranges && strlen(data_handle_ranges))
    {
        merged_handle_ranges = strdup(data_handle_ranges);
    }

    if (!merged_handle_ranges)
    {
        gossip_err("Failed to merge the handle range!  Format invalid\n");
        return -1;
    }

    /*
      set the trove handle ranges; this initializes the handle
      allocator with the ranges we were told to use
    */ 
    ret = trove_collection_setinfo(coll_id,
                                   trove_context,
                                   TROVE_COLLECTION_HANDLE_RANGES,
                                   merged_handle_ranges);

    if (ret < 0)
    {
	mkspace_print(verbose, "Error adding handle ranges: %s\n",
                      merged_handle_ranges);
        free(merged_handle_ranges);
	return -1;
    }

    mkspace_print(verbose, "info: set handle ranges to %s\n",
                  merged_handle_ranges);

    free(merged_handle_ranges);
 

    /*
      if a root_handle is specified, 1) create a dataspace to hold the
      root directory  2) set attributes on the dspace

      The dirdata objects and the lost+found directory are created in pvfs2-server.c
    */
    if (new_root_handle != TROVE_HANDLE_NULL)
    {
        cur_extent.first = cur_extent.last = new_root_handle;
        extent_array.extent_count = 1;
        extent_array.extent_array = &cur_extent;

        ret = trove_dspace_create(coll_id,
                                  &extent_array,
                                  &new_root_handle,
                                  PVFS_TYPE_DIRECTORY,
                                  NULL,
                                  (TROVE_SYNC | TROVE_FORCE_REQUESTED_HANDLE),
                                  NULL,
                                  trove_context,
                                  &op_id,
                                  NULL);

        while (ret == 0)
        {
            ret = trove_dspace_test(coll_id,
                                    op_id,
                                    trove_context,
                                    &count,
                                    NULL,
                                    NULL,
                                    &state,
                                    TROVE_DEFAULT_TEST_TIMEOUT);
        }

        if ((ret != 1) && (state != 0))
        {
            mkspace_print(verbose,
                          "dspace create (for root dir) failed.\n");
            return -1;
        }

        mkspace_print(verbose,"info: created root directory "
                      "with handle %llu.\n", llu(new_root_handle));
        s_used_handles[0] = new_root_handle;

        /* set collection attribute for root handle */
        key.buffer = ROOT_HANDLE_KEYSTR;
        key.buffer_sz = ROOT_HANDLE_KEYLEN;
        val.buffer = &new_root_handle;
        val.buffer_sz = sizeof(new_root_handle);
        ret = trove_collection_seteattr(coll_id,
                                        &key,
                                        &val,
                                        0,
                                        NULL,
                                        trove_context,
                                        &op_id);
        while (ret == 0)
        {
            ret = trove_dspace_test(coll_id,
                                    op_id,
                                    trove_context,
                                    &count,
                                    NULL,
                                    NULL,
                                    &state,
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
        attr.type = PVFS_TYPE_DIRECTORY;
	attr.atime = attr.ctime = PINT_util_get_current_time();
        attr.mtime = PINT_util_mktime_version(attr.ctime);

        ret = trove_dspace_setattr(coll_id,
                                   new_root_handle,
                                   &attr,
                                   TROVE_SYNC,
                                   NULL,
            trove_context, &op_id, NULL);

        while (ret == 0)
        {
            ret = trove_dspace_test(coll_id,
                                    op_id,
                                    trove_context,
                                    &count,
                                    NULL,
                                    NULL,
                                    &state,
                                    TROVE_DEFAULT_TEST_TIMEOUT);
        }

        if (ret < 0)
        {
            gossip_err("error: dspace setattr for root handle "
                       "attributes failed; aborting!\n");
            return -1;
        }

        /* The creation of dirdata objects for the root directory is 
         * moved to pvfs2_server.c to setup distributed directory struct.
         * lost+found directory is also created there. 
         */

    }
    	
    if (trove_context != -1)
    {
        trove_close_context(coll_id, trove_context);
    }
    trove_finalize(TROVE_METHOD_DBPF);

    mkspace_print(verbose, "collection created:\n"
                  "\troot handle = %llu, coll id = %d, "
                  "root string = \"%s\"\n",
                  llu(root_handle), coll_id, ROOT_HANDLE_KEYSTR);

    /* free space */
    if (key_a)
    {
        free(key_a);
    }
    if (val_a)
    {
        free(val_a);
    }

    return 0;
}

int pvfs2_rmspace(
    char *data_path,
    char *meta_path,
    char *collection,
    TROVE_coll_id coll_id,
    int remove_collection_only,
    int verbose)
{
    int ret = -1;
    TROVE_op_id op_id;
    static int trove_is_initialized = 0;

    /* try to initialize; fails if storage space isn't there */
    if (!trove_is_initialized)
    {
        ret = trove_initialize(
	    TROVE_METHOD_DBPF, NULL,
	    data_path, meta_path, 0);
        if (ret == -1)
        {
            gossip_err("error: storage space %s or %s does not "
                       "exist; aborting!\n", data_path, meta_path);
            return -1;
        }
        trove_is_initialized = 1;
    }

    mkspace_print(verbose, "Attempting to remove collection %s\n",
                  collection);

    ret = trove_collection_remove(
	TROVE_METHOD_DBPF, collection, NULL, &op_id);
    mkspace_print(
        verbose, "PVFS2 Collection %s removed %s\n", collection,
        (((ret == 1) || (ret == -TROVE_ENOENT)) ? "successfully" :
         "with errors"));

    if (!remove_collection_only)
    {
        ret = trove_storage_remove(TROVE_METHOD_DBPF, data_path, meta_path, 
				   NULL, &op_id);
	/*
	 * it is a bit weird to do a trove_finaliz() prior to blowing away
	 * the storage space, but this allows the __db files of the DB env
	 * to be blown away for the rmdir() to work correctly!
	 */
	trove_finalize(TROVE_METHOD_DBPF);
        mkspace_print(
            verbose, "PVFS2 Storage Space %s and %s removed %s\n",
            data_path, meta_path, (((ret == 1) || (ret == -TROVE_ENOENT)) ?
                            "successfully" : "with errors"));

        trove_is_initialized = 0;
    }
    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
