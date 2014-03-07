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
#include "pvfs2-server.h"
#include "str-utils.h"
#include "extent-utils.h"
#include "pvfs2-util.h"
#include "pvfs2-internal.h"
#include "pint-util.h"
#include "pint-event.h"
#include "dist-dir-utils.h"

/*
static char *lost_and_found_string = "lost+found";
*/

#define mkspace_print(v, format, f...)                   \
do {                                                     \
    if (v == PVFS2_MKSPACE_GOSSIP_VERBOSE)               \
        gossip_debug(GOSSIP_SERVER_DEBUG, format, ##f);  \
    else if (v == PVFS2_MKSPACE_STDERR_VERBOSE)          \
        fprintf(stderr,format, ##f);                     \
} while(0)

/* V3 NEXT remove this func and all references to it */
#if 0
static int handle_is_excluded(
    TROVE_handle handle, TROVE_handle *handles_to_exclude,
    int num_handles_to_exclude)
{
    int excluded = 0;

    while((num_handles_to_exclude - 1) > -1)
    {
        if (!PVFS_OID_cmp(&handle, &handles_to_exclude[num_handles_to_exclude-1]))
        {
            excluded = 1;
            break;
        }
        num_handles_to_exclude--;
    }
    return excluded;
}
#endif

/* V3 NEXT remove this func and all refersnce to it */
#if 0
static void get_handle_extent_from_ranges(char *handle_ranges,
                                          TROVE_handle_extent *out_extent,
                                          TROVE_handle *handles_to_exclude,
                                          int num_handles_to_exclude)
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
/* this range stuff is going away anyway so for now I'm just making
 * this if true - WBL
 */
#if 0
                if (((tmp_extent->last - tmp_extent->first) > 0) ||
                    ((tmp_extent->last > 0) &&
                     (tmp_extent->last == tmp_extent->first) &&
                     !handle_is_excluded(
                         tmp_extent->last, handles_to_exclude,
                         num_handles_to_exclude)))
#endif
if (1)
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

/** pvfs2_mkspace
 *
 * Procedure is as follows:
 *   - First create the storage space using Trove, check for conditions
 *     like already exists
 *   - Create the collection (file system record)
 *   - Set attributes on the collection (with Root Handle)
 *   - If we are supposed to create the root dir then
 *      - create the dspace for the dir
 *      - set attributes on the dspace
 *      - add attributes to keyval for
 *            dirdata handle
 *            dirdata count
 *            dirdata attributes
 *      - create a dspace for the dirdata
 *      - set attributes on the dirdata
 */

int pvfs2_mkspace(char *data_path,
                  char *meta_path,
                  char *config_path,
                  char *collection,
                  TROVE_coll_id coll_id,
                  TROVE_handle root_handle,
                  TROVE_handle root_dirdata_handle,
                  PVFS_SID *root_sid_array,
                  int root_sid_count,
                  int create_collection_only,
                  int verbose)
{
    int ret = - 1;
    int i;
    TROVE_op_id op_id;
    TROVE_context_id trove_context = -1;
    int count = 0;
    TROVE_ds_state state;
    TROVE_keyval_s key, val;
    TROVE_keyval_s *key_a = NULL, *val_a = NULL;
    TROVE_ds_attributes_s attr;
    TROVE_handle new_root_handle = root_handle;
    TROVE_handle new_root_dirdata_handle = root_dirdata_handle;
    PVFS_dist_dir_bitmap_basetype bitmap[1];
    PVFS_ID *dirdata_handles = NULL;

    mkspace_print(verbose, "Data storage space     : %s\n", data_path);
    mkspace_print(verbose, "Metadata storage space : %s\n", meta_path);
    mkspace_print(verbose, "Collection             : %s\n", collection);
    mkspace_print(verbose, "ID                     : %d\n", coll_id);
    mkspace_print(verbose, "Root Handle            : %s\n",
                  PVFS_OID_str(&root_handle));
    mkspace_print(verbose, "Root Dirdata Handle    : %s\n",
                  PVFS_OID_str(&root_dirdata_handle));

    /*
     * if we're only creating a collection inside an existing
     * storage space, we need to assume that it exists already
     */
    if (!create_collection_only)
    {
        /*
         * try to initialize; fails if storage space isn't there, which
         * is exactly what we're expecting in this case.
         */
        ret = trove_initialize(TROVE_METHOD_DBPF, 
			       NULL, 
			       data_path,
			       meta_path,
			       config_path,
			       0);
        if (ret > -1)
        {
            gossip_err("error: storage space %s or %s already "
                       "exists; aborting!\n",data_path,meta_path);
            return -1;
        }

        ret = trove_storage_create(TROVE_METHOD_DBPF,
                                   data_path,
                                   meta_path,
                                   config_path,
                                   NULL,
                                   &op_id);
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
                           config_path,
                           0);
    if (ret < 0)
    {
	gossip_err("error: trove initialize failed; aborting!\n");
	return -1;
    }

    mkspace_print(verbose,
                  "info: created data storage space '%s'.\n",
                  data_path);
    mkspace_print(verbose,
                  "info: created metadata storage space '%s'.\n",
                  meta_path);
    mkspace_print(verbose,
                  "info: created config storage space '%s'.\n",
                  config_path);

    /* try to look up collection used to store file system */
    ret = trove_collection_lookup(TROVE_METHOD_DBPF,
                                  collection,
                                  &coll_id,
                                  NULL,
                                  &op_id);
    if (ret == 1)
    {
	mkspace_print(verbose,
                      "warning: collection lookup succeeded "
                      "before it should; aborting!\n");
	trove_finalize(TROVE_METHOD_DBPF);
	return -1;
    }

    /* create the collection for the fs */
    ret = trove_collection_create(collection, coll_id, NULL, &op_id);
    if (ret != 1)
    {
	mkspace_print(verbose,
                      "error: collection create failed for "
                      "collection '%s'.\n",
                      collection);
	return -1;
    }

    /* make sure a collection lookup succeeds */
    ret = trove_collection_lookup(TROVE_METHOD_DBPF,
                                  collection,
                                  &coll_id,
                                  NULL,
                                  &op_id);
    if (ret != 1)
    {
	mkspace_print(verbose,
                      "error: collection lookup failed for "
                      "collection '%s' after create.\n",
                      collection);
	return -1;
    }

    mkspace_print(verbose, "info: created collection '%s'.\n",collection);

    ret = trove_open_context(coll_id, &trove_context);
    if (ret < 0)
    {
        mkspace_print(verbose,"trove_open_context() failure.\n");
        return -1;
    }

    /*
     * if a root_handle is specified, 1) create a dataspace to hold the
     * root directory 2) create the dspace for dir entries, 3) set
     * attributes on the dspace
     */
    if (PVFS_OID_cmp(&new_root_handle, &TROVE_HANDLE_NULL))
    {
        /* new_root_handle is not NULL */

        /* we could eliminate this extra record and write it in the
         * primary collection record - maybe later
         */
    
        /********************************************/
        /* set Collection attribute for root handle */

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

        /*********************************/
        /* Create Root Dir dspace record */

        new_root_handle = root_handle;

        ret = trove_dspace_create(coll_id, 
                                  root_handle,      /* in */
                                  &new_root_handle, /* out */
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

        /* if new_root_handle is not eq to root_handle we have a problem
         */
        if(!PVFS_OID_cmp(&root_handle, &new_root_handle))
        {
            gossip_err("Trove did not use handle passed in for root\n");
            return -1;
        }

        mkspace_print(verbose,
                      "info: created root directory "
                      "with handle %s.\n",
                      PVFS_OID_str(&new_root_handle));

        /********************************/
        /* create Dirdata dspace record */

        new_root_dirdata_handle = root_dirdata_handle;

        ret = trove_dspace_create(coll_id, 
                                  root_dirdata_handle,      /* in */
                                  &new_root_dirdata_handle, /* out */
                                  PVFS_TYPE_DIRDATA,
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
                          "dspace create (for root dirdata) failed.\n");
            return -1;
        }

        /* if new_root_handle is not eq to root_handle we have a problem
         */
        if(!PVFS_OID_cmp(&root_dirdata_handle, &new_root_dirdata_handle))
        {
            gossip_err("Trove did not use handle passed in for root dirdata\n");
            return -1;
        }

        /**********************************/
        /* set Root Dir dspace attributes */

        memset(&attr, 0, sizeof(TROVE_ds_attributes_s));
        attr.type = PVFS_TYPE_DIRECTORY;
        /* fs_id and handle filled in by call */
        attr.uid = getuid();
        attr.gid = getgid();
        attr.mode = 0777;
	attr.atime = attr.ctime = PINT_util_get_current_time();
        attr.mtime = PINT_util_mktime_version(attr.ctime);
        attr.u.directory.tree_height = 1;
        attr.u.directory.dirdata_count = 1;
        attr.u.directory.sid_count = root_sid_count;
        attr.u.directory.bitmap_size = 1;
        attr.u.directory.split_size = 4096;
        attr.u.directory.server_no = 0;
        attr.u.directory.branch_level = 1;

        ret = trove_dspace_setattr(coll_id,
                                   root_handle,
                                   &attr,
                                   TROVE_SYNC,
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

        if (ret < 0)
        {
            gossip_err("error: dspace setattr for root handle "
                       "attributes failed; aborting!\n");
            return -1;
        }

        /***********************************/
        /* write Dirdata dspace attributes */

        attr.type = PVFS_TYPE_DIRDATA;

        ret = trove_dspace_setattr(coll_id,
                                   root_dirdata_handle,
                                   &attr,
                                   TROVE_SYNC,
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

        if (ret < 0)
        {
            gossip_err("error: dspace setattr for root handle "
                       "attributes failed; aborting!\n");
            return -1;
        }

        /************************************/
        /* write Root Dir keyval attributes */

        /* total 2 keyvals,
         * PVFS_DIRDATA_BITMAP, PVFS_DIRDATA_HANDLES
         */
        count = 2;

        key_a = malloc(sizeof(PVFS_ds_keyval) * count);
        if(!key_a)
        {
            return -1;
        }
        memset(key_a, 0, sizeof(PVFS_ds_keyval) * count);

        val_a = malloc(sizeof(PVFS_ds_keyval) * count);
        if(!val_a)
        {
            free(key_a);
            return -1;
        }
        memset(val_a, 0, sizeof(PVFS_ds_keyval) * count);

        dirdata_handles = (PVFS_ID *)malloc((root_sid_count + 1) *
                                            sizeof(PVFS_ID));
        if(!dirdata_handles)
        {
            free(key_a);
            free(val_a);
            return -1;
        }
        memset(val_a, 0, ((root_sid_count + 1) * sizeof(PVFS_ID)));

        key_a[0].buffer = DIST_DIRDATA_BITMAP_KEYSTR;
        key_a[0].buffer_sz = DIST_DIRDATA_BITMAP_KEYLEN;

        bitmap[0] = 1;
        val_a[0].buffer = bitmap;
        val_a[0].buffer_sz = 1 * sizeof(PVFS_dist_dir_bitmap_basetype);

        key_a[1].buffer = DIST_DIRDATA_HANDLES_KEYSTR;
        key_a[1].buffer_sz = DIST_DIRDATA_HANDLES_KEYLEN;

        dirdata_handles[0].oid = root_dirdata_handle;
        for (i = 0; i < root_sid_count; i++)
        {
            dirdata_handles[i + 1].sid = root_sid_array[i];
        }
        val_a[1].buffer = dirdata_handles;
        val_a[1].buffer_sz = (root_sid_count + 1) * sizeof(PVFS_SID);

        ret = trove_keyval_write_list(coll_id,
                                      root_handle,
                                      key_a,
                                      val_a,
                                      count,
                                      0,    /* flags */
                                      NULL, /* vtag */
                                      NULL, /* user ptr */
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
                          "dspace create (for root dirdata) failed.\n");
            return -1;
        }

        /***********************************/
        /* write Dirdata keyval attributes */

        ret = trove_keyval_write_list(coll_id,
                                      root_dirdata_handle,
                                      key_a,
                                      val_a,
                                      count,
                                      0,    /* flags */
                                      NULL, /* vtag */
                                      NULL, /* user ptr */
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
                          "dspace create (for root dirdata) failed.\n");
            return -1;
        }
    
        /*********************/
        /* create lost+found */

    }
    	
    if (trove_context != -1)
    {
        trove_close_context(coll_id, trove_context);
    }
    trove_finalize(TROVE_METHOD_DBPF);

    mkspace_print(verbose, "collection created:\n"
                  "\troot handle = %s, coll id = %d, "
                  "root string = \"%s\"\n",
                  PVFS_OID_str(&root_handle),
                  coll_id,
                  ROOT_HANDLE_KEYSTR);

    /* free space */
    if (key_a)
    {
        free(key_a);
    }
    if (val_a)
    {
        free(val_a);
    }
    if (dirdata_handles)
    {
        free(dirdata_handles);
    }

    return 0;
}

int pvfs2_rmspace(char *data_path,
                  char *meta_path,
                  char *config_path,
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
        ret = trove_initialize(TROVE_METHOD_DBPF,
                               NULL,
	                       data_path,
                               meta_path,
                               config_path,
                               0);
        if (ret == -1)
        {
            gossip_err("error: storage space %s, %s or %s does not "
                       "exist; aborting!\n",
                       data_path,
                       meta_path,
                       config_path);
            return -1;
        }
        trove_is_initialized = 1;
    }

    mkspace_print(verbose,
                  "Attempting to remove collection %s\n",
                  collection);

    ret = trove_collection_remove(TROVE_METHOD_DBPF, collection, NULL, &op_id);
    mkspace_print(verbose,
                  "PVFS2 Collection %s removed %s\n",
                  collection,
                  (((ret == 1) || (ret == -TROVE_ENOENT)) ?
                                "successfully" : "with errors"));

    if (!remove_collection_only)
    {
        ret = trove_storage_remove(TROVE_METHOD_DBPF,
                                   data_path,
                                   meta_path, 
                                   config_path, 
				   NULL,
                                   &op_id);
	/*
	 * it is a bit weird to do a trove_finaliz() prior to blowing away
	 * the storage space, but this allows the __db files of the DB env
	 * to be blown away for the rmdir() to work correctly!
	 */
	trove_finalize(TROVE_METHOD_DBPF);
        mkspace_print(verbose,
                      "PVFS2 Storage Space %s, %s and %s removed %s\n",
                      data_path,
                      meta_path,
                      config_path,
                      (((ret == 1) || (ret == -TROVE_ENOENT)) ?
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
