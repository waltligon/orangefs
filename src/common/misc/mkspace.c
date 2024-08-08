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
#include "pvfs2-server.h"
#include "str-utils.h"
#include "extent-utils.h"
#include "pvfs2-util.h"
#include "pint-util.h"
#include "pint-event.h"
#include "dist-dir-utils.h"
#include "server-config-mgr.h"

#define DEFAULT_ROOTDIR_META_SID_COUNT root_sid_count
#define DEFAULT_ROOTDIR_TREE_HEIGHT    0
#define DEFAULT_ROOTDIR_DIRDATA_MIN    1
#define DEFAULT_ROOTDIR_DIRDATA_MAX    1
#define DEFAULT_ROOTDIR_DIRDATA_COUNT  1
#define DEFAULT_ROOTDIR_BITMAP_SIZE    1
#define DEFAULT_ROOTDIR_SPLIT_SIZE  4096
#define DEFAULT_ROOTDIR_BRANCH_LEVEL   1
/* Note: server_no is set below where attribs are initialized */
/* -1 indicates no hint present */
#define DEFAULT_ROOTDIR_HINT_DIST_NAME_LEN 0
#define DEFAULT_ROOTDIR_HINT_DIST_PARAMS_LEN 0
#define DEFAULT_ROOTDIR_HINT_DFILE_COUNT 0
#define DEFAULT_ROOTDIR_HINT_DFILE_SID_COUNT 0
#define DEFAULT_ROOTDIR_HINT_LAYOUT_ALGORITHM PVFS_SYS_LAYOUT_NULL
#define DEFAULT_ROOTDIR_HINT_LAYOUT_LIST_CNT 0
#define DEFAULT_ROOTDIR_HINT_DIRDATA_MIN 0
#define DEFAULT_ROOTDIR_HINT_DIRDATA_MAX 0
#define DEFAULT_ROOTDIR_HINT_SPLIT_SIZE 0
#define DEFAULT_ROOTDIR_HINT_DIR_LAYOUT_ALGORITHM PVFS_SYS_LAYOUT_NULL
#define DEFAULT_ROOTDIR_HINT_DIR_LAYOUT_LIST_CNT 0


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
    int count = 0;  /* used in trove_dspace_test */
    int rec_count = 0;  /* used in trove_keyval_write_list */
    TROVE_ds_state state;
    TROVE_keyval_s key, val;
    TROVE_keyval_s *key_a = NULL, *val_a = NULL;
    TROVE_ds_attributes_s ds_attr;
    TROVE_handle new_root_handle = root_handle;
    TROVE_handle new_root_dirdata_handle = root_dirdata_handle;
    PVFS_dist_dir_bitmap_basetype bitmap[1];
    PVFS_ID *dirdata_handles = NULL;
    PVFS_ID *dirdata_parent_handles = NULL;
    struct stat root_stat;
    struct stat meta_stat;
    struct stat data_stat;
    struct stat config_stat;

    struct server_configuration_s *config = PINT_server_config_mgr_get_config();

    static PVFS_OID ROOT_DIR_PARENT_AND_SID[2] = {{{0,0,0,0,0,0,0,0, /* NULL OID, SID */
                                                    0,0,0,0,0,0,0,0}},
                                                  {{0,0,0,0,0,0,0,0,
                                                    0,0,0,0,0,0,0,0}}};

    mkspace_print(verbose, "%s:\n", __func__);
    mkspace_print(verbose, "Data storage space     : %s\n", data_path);
    mkspace_print(verbose, "Metadata storage space : %s\n", meta_path);
    mkspace_print(verbose, "Config storage space   : %s\n", config_path);
    mkspace_print(verbose, "Collection             : %s\n", collection);
    mkspace_print(verbose, "Collection ID          : %d\n", coll_id);
    mkspace_print(verbose, "Root Handle            : %s\n",
                  PVFS_OID_str(&root_handle));
    mkspace_print(verbose, "Root Dirdata Handle    : %s\n",
                  PVFS_OID_str(&root_dirdata_handle));
    mkspace_print(verbose, "Root SID Count         : %d\n", root_sid_count);


    /* init stat buffers */
    memset(&root_stat, 0, sizeof(root_stat));
    memset(&meta_stat, 0, sizeof(meta_stat));
    memset(&data_stat, 0, sizeof(meta_stat));
    memset(&config_stat, 0, sizeof(config_stat));

    /* call stat on root, meta, and data paths */
    stat("/", &root_stat);
    stat(data_path, &data_stat);
    stat(meta_path, &meta_stat);
    stat(config_path, &config_stat);

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
     * storage space, we assume that it exists already so we skip this
     */
    if (!create_collection_only)
    {
        /*
         * try to initialize; fails if storage space isn't there, which
         * is exactly what we're expecting in this case.
         */
        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "%s: calling trove_initialize. Paths NOT expected\n", __func__);
        ret = trove_initialize(TROVE_METHOD_DBPF, 
			       NULL, 
			       data_path,
			       meta_path,
			       config_path,
			       0);

        // SUCCESS
        if (ret > -1)
        {
            gossip_err("%s: error: storage space %s or %s or %s already exists; "
                       "aborting!\n", __func__, data_path, meta_path, config_path);
            // ERROR
            return -1;
        }

        /* set the config pointer and filesystem pointer inside of trove */
        ret = trove_collection_set_fs_config(coll_id, config);
        // ERROR
        if (ret < 0)
        {
            char emsg[256];
            PVFS_strerror_r(ret, emsg, 256);
            gossip_err("%s: trove_collection_set_fs_config(1) failed: %d(%s)\n",
                       __func__, ret, emsg);
        }

        gossip_debug(GOSSIP_TROVE_DEBUG,"%s: calling trove_storage_create\n", __func__);
        ret = trove_storage_create(TROVE_METHOD_DBPF,
                                   data_path,
                                   meta_path,
                                   config_path,
                                   NULL,
                                   &op_id);
        // not SUCCESS?
        if (ret != 1)
        {
            char emsg[256];
            PVFS_strerror_r(ret, emsg, 256);
            gossip_err("%s: trove_storage_create failed: %d(%s)\n", __func__, ret, emsg);
            // ERROR
            return -1;
        }
    }

    /* now that the storage space exists, initialize trove properly */
    gossip_debug(GOSSIP_TROVE_DEBUG,
                 "%s: calling trove_initialize. Paths expected.\n", __func__);
    ret = trove_initialize(TROVE_METHOD_DBPF,
                           NULL, 
	                   data_path,
                           meta_path,
                           config_path,
                           0);

    // less than SUCCESS, ERROR?
    if (ret < 0)
    {
        char emsg[256];
        PVFS_strerror_r(ret, emsg, 256);
	gossip_err("%s: trove_initialize failed: %d(%s)\n", __func__, ret, emsg);
        // ERROR
	return -1;
    }

    mkspace_print(verbose,
                  "%s: created data storage space '%s'.\n", __func__,
                  data_path);
    mkspace_print(verbose,
                  "%s: created metadata storage space '%s'.\n", __func__,
                  meta_path);
    mkspace_print(verbose,
                  "%s: created config storage space '%s'.\n", __func__,
                  config_path);

    /* set the config pointer and filesystem pointer inside of trove */
    ret = trove_collection_set_fs_config(coll_id, config);
    // lt SUCCESS == ERROR?
    if (ret < 0) // NOT SURE ABOUT 0
    {
        char emsg[256];
        strerror_r(ret, emsg, 256);
	gossip_err("%s: trove_collection_set_fs_config(2) failed: %d(%s)\n",
                   __func__, ret, emsg);
        // ERROR
	return -1;
    }

    gossip_debug(GOSSIP_TROVE_DEBUG,
                 "%s: trove_collection_lookup should fail with ENOENT\n",
                 __func__);
    /* try to look up collection used to store file system - should fail */
    ret = trove_collection_lookup(TROVE_METHOD_DBPF,
                                  collection,
                                  &coll_id,
                                  NULL,
                                  &op_id);
    // SUCCESS or BUSY?
    if (ret == 1)
    {
        char emsg[256];
        PVFS_strerror_r(ret, emsg, 256);
	mkspace_print(verbose,
                      "%s: warning: trove_collection_lookup succeeded "
                      "before it should; %d(%s) aborting!\n", __func__, ret, emsg);
	trove_finalize(TROVE_METHOD_DBPF);
        // ERROR
	return -1;
    }

    gossip_debug(GOSSIP_TROVE_DEBUG,
                 "%s: calling trove_collection_create to set up collection\n",
                 __func__);
    /* create the collection for the fs */
    ret = trove_collection_create(collection, coll_id, NULL, &op_id);
    // not SUCCESS
    if (ret != 1)
    {
        char emsg[256];
        PVFS_strerror_r(ret, emsg, 256);
	mkspace_print(verbose,
                      "%s: error: trove_collection_create failed for collection"
                      " '%s' %d(%s).\n", __func__, collection, ret, emsg);
        // ERROR
	return -1;
    }

    /* try to look up collection used to store file system - should succeed */
    gossip_debug(GOSSIP_TROVE_DEBUG,
                 "%s: BEGIN trove_collection_lookup SHOULD SUCCEED\n", __func__);
    ret = trove_collection_lookup(TROVE_METHOD_DBPF,
                                  collection,
                                  &coll_id,
                                  NULL,
                                  &op_id);
    // not SUCCESS
    if (ret != 1)
    {
	gossip_debug(GOSSIP_TROVE_DEBUG,
                     "%s: ERROR: trove_collection_lookup FAILED for "
                     "collection '%s' after create.\n", __func__,
                     collection);
        // ERROR
	return -1;
    }
    else
    {
    gossip_debug(GOSSIP_TROVE_DEBUG,
                 "%s: END trove_collection_lookup SHOULD SUCCEED\n", __func__);
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: INFO: created collection '%s'.\n",
                  __func__, collection);

    ret = trove_open_context(coll_id, &trove_context);
    if (ret < 0)
    {
        mkspace_print(verbose,"%s: trove_open_context() failure.\n", __func__);
        return -1;
    }

    /*
     * if a root_handle is specified, 1) create a dir dspace to hold the
     * root directory 2) create the dirdata dspace for dir entries, 3) set
     * attributes on both dspaces
     */
    if (PVFS_OID_NE(&new_root_handle, &TROVE_HANDLE_NULL))
    {
        /* new_root_handle is not NULL */
        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "%s: CREATE root handle in collection\n", __func__);

        /* we could eliminate this extra record and write it in the
         * primary collection record - maybe later
         */
    
        /********************************************/
        /* set Collection attribute for root handle */

        key.buffer = ROOT_HANDLE_KEYSTR;
        key.buffer_sz = ROOT_HANDLE_KEYLEN;
        val.buffer = &new_root_handle;
        val.buffer_sz = sizeof(new_root_handle);

        /* for these operations the following nonstandard return codes
         * are ret == 1 is successful, 0 is not complete, <0 error 
         */
        ret = trove_collection_seteattr(coll_id,
                                        &key,
                                        &val,
                                        0,
                                        NULL,
                                        trove_context,
                                        &op_id);
        // lt 0 == ERROR?
        if (ret < 0)
        {
            gossip_err("%s: error: collection_seteattr (root handle) failed; "
                       "aborting!\n", __func__);
            // ERROR
            return -1;
        }

        // BUSY
        while (ret == 0)
        {
            count = 0;
            gossip_debug(GOSSIP_TROVE_DEBUG,
                         "%s: calling dspace_test op_id=%ld\n", __func__, op_id);
            ret = trove_dspace_test(coll_id,
                                    op_id,
                                    trove_context,
                                    &count,
                                    NULL,
                                    NULL,
                                    &state,
                                    TROVE_DEFAULT_TEST_TIMEOUT);
        }
        /* ret == 1 is successful, 0 is not complete (BUSY), <0 error */

        // ERROR?
        if (ret < 0)
        {
            gossip_err("%s: error: dspace_test failed; aborting!\n",
                       __func__);
            // ERROR;
            return -1;
        }

        /*********************************/
        /* Create Root Dir dspace record */

        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "%s: CREATE root dir in dspace\n", __func__);

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

        // BUSY
        while (ret == 0)
        {
            count = 0;
            ret = trove_dspace_test(coll_id,
                                    op_id,
                                    trove_context,
                                    &count,
                                    NULL,
                                    NULL,
                                    &state,
                                    TROVE_DEFAULT_TEST_TIMEOUT);
        }

        // (ret <= ERROR && state != UNINITIALIZED)
        if ((ret != 1) && (state != 0))
        {
            gossip_err("%s: dspace create (for root dir) failed.\n", __func__);
            // ERROR;
            return -1;
        }

        /* if new_root_handle is not eq to root_handle we have a problem
         */
        if(PVFS_OID_NE(&root_handle, &new_root_handle))
        {
            gossip_err("%s: Trove did not use handle passed in for root\n",
                       __func__);
            // ERROR
            return -1;
        }

        mkspace_print(verbose,
                      "%s: info: created root directory "
                      "with handle %s.\n", __func__,
                      PVFS_OID_str(&new_root_handle));

        /********************************/
        /* create Dirdata dspace record */

        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "%s: CREATE root dirdata in dspace\n", __func__);
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

        // BUSY
        while (ret == 0)
        {
            count = 0;
            ret = trove_dspace_test(coll_id,
                                    op_id,
                                    trove_context,
                                    &count,
                                    NULL,
                                    NULL,
                                    &state,
                                    TROVE_DEFAULT_TEST_TIMEOUT);
        }

        // (ret <= ERROR && state != UNINITIALIZED)
        if (ret != 1 && state != 0)
        {
            gossip_err("%s: dspace create (for root dirdata) failed.\n",
                       __func__);
            // ERROR;
            return -1;
        }

        /* if new_root_dirdata_handle is not eq to root_dirdata_handle
         * we have a problem
         */
        if(PVFS_OID_NE(&root_dirdata_handle, &new_root_dirdata_handle))
        {
            gossip_err("%s: Trove did not use handle passed for root dirdata\n",
                       __func__);
            // ERROR;
            return -1;
        }

        /**********************************/
        /* set Root Dir dspace attributes */

        memset(&ds_attr, 0, sizeof(TROVE_ds_attributes_s));
        ds_attr.type = PVFS_TYPE_DIRECTORY;
        /* fs_id and handle filled in by call */
        ds_attr.uid = getuid();
        ds_attr.gid = getgid();
        ds_attr.mode = 0777;
	ds_attr.mtime = ds_attr.ntime = ds_attr.atime = ds_attr.ctime = 
                        PINT_util_get_current_time();
        /* attr.mtime = PINT_util_mktime_version(attr.ntime); */
        ds_attr.meta_sid_count =                    DEFAULT_ROOTDIR_META_SID_COUNT;
        ds_attr.u.directory.dirent_count =          0;
        ds_attr.u.directory.tree_height =           DEFAULT_ROOTDIR_TREE_HEIGHT;
        ds_attr.u.directory.dirdata_min =           DEFAULT_ROOTDIR_DIRDATA_MIN;
        ds_attr.u.directory.dirdata_max =           DEFAULT_ROOTDIR_DIRDATA_MAX;
        ds_attr.u.directory.dirdata_count =         DEFAULT_ROOTDIR_DIRDATA_COUNT;
        ds_attr.u.directory.bitmap_size =           DEFAULT_ROOTDIR_BITMAP_SIZE;
        ds_attr.u.directory.split_size =            DEFAULT_ROOTDIR_SPLIT_SIZE;
        /*attr.u.directory.server_no =                -1;*/
        ds_attr.u.directory.branch_level =          DEFAULT_ROOTDIR_BRANCH_LEVEL;
        /* FILE HINTS */
        ds_attr.u.directory.hint_dist_name_len =    DEFAULT_ROOTDIR_HINT_DIST_NAME_LEN;
        ds_attr.u.directory.hint_dist_params_len =  DEFAULT_ROOTDIR_HINT_DIST_PARAMS_LEN;
        ds_attr.u.directory.hint_dfile_count =      DEFAULT_ROOTDIR_HINT_DFILE_COUNT;
        ds_attr.u.directory.hint_dfile_sid_count =  DEFAULT_ROOTDIR_HINT_DFILE_SID_COUNT;
        ds_attr.u.directory.hint_layout_algorithm = DEFAULT_ROOTDIR_HINT_LAYOUT_ALGORITHM;
        ds_attr.u.directory.hint_layout_list_cnt =  DEFAULT_ROOTDIR_HINT_LAYOUT_LIST_CNT;
        /* DIR HINTS */
        ds_attr.u.directory.hint_dirdata_min =      DEFAULT_ROOTDIR_HINT_DIRDATA_MIN;
        ds_attr.u.directory.hint_dirdata_max =      DEFAULT_ROOTDIR_HINT_DIRDATA_MAX;
        ds_attr.u.directory.hint_split_size =       DEFAULT_ROOTDIR_HINT_SPLIT_SIZE;
        ds_attr.u.directory.hint_dir_layout_algorithm = DEFAULT_ROOTDIR_HINT_DIR_LAYOUT_ALGORITHM;
        ds_attr.u.directory.hint_dir_layout_list_cnt = DEFAULT_ROOTDIR_HINT_DIR_LAYOUT_LIST_CNT;

        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: setting root dir attributes\n",
                     __func__);
        ret = trove_dspace_setattr(coll_id,
                                   root_handle,
                                   &ds_attr,
                                   TROVE_SYNC,
                                   NULL,
                                   trove_context,
                                   &op_id,
                                   NULL);

        // BUSY
        while (ret == 0)
        {
            count = 0;
            ret = trove_dspace_test(coll_id,
                                    op_id,
                                    trove_context,
                                    &count,
                                    NULL,
                                    NULL,
                                    &state,
                                    TROVE_DEFAULT_TEST_TIMEOUT);
        }

        // ERROR
        if (ret < 0)
        {
            gossip_err("%s: error: dspace setattr for root dir "
                       "attributes failed; aborting!\n", __func__);
            // ERROR
            return -1;
        }

        /***********************************/
        /* write Dirdata dspace attributes */
        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "%s: setting root dirdata attributes\n", __func__);

        memset(&ds_attr, 0, sizeof(TROVE_ds_attributes_s));
        /* fs_id and handle filled in by call */
        ds_attr.type = PVFS_TYPE_DIRDATA;
	ds_attr.mtime = ds_attr.ntime = ds_attr.atime = ds_attr.ctime =
                        PINT_util_get_current_time();
        /* attr.mtime = PINT_util_mktime_version(attr.ntime); */
        ds_attr.meta_sid_count =          DEFAULT_ROOTDIR_META_SID_COUNT;
        ds_attr.u.dirdata.dirent_count =  0;
        ds_attr.u.dirdata.tree_height =   DEFAULT_ROOTDIR_TREE_HEIGHT;
        ds_attr.u.dirdata.dirdata_count = DEFAULT_ROOTDIR_DIRDATA_COUNT;
        ds_attr.u.dirdata.bitmap_size =   DEFAULT_ROOTDIR_BITMAP_SIZE;
        ds_attr.u.dirdata.split_size =    DEFAULT_ROOTDIR_SPLIT_SIZE;
        ds_attr.u.dirdata.server_no =     0;
        ds_attr.u.dirdata.branch_level =  DEFAULT_ROOTDIR_BRANCH_LEVEL;

        ret = trove_dspace_setattr(coll_id,
                                   root_dirdata_handle,
                                   &ds_attr,
                                   TROVE_SYNC,
                                   NULL,
                                   trove_context,
                                   &op_id,
                                   NULL);

        // BUSY
        while (ret == 0)
        {
            count = 0;
            ret = trove_dspace_test(coll_id,
                                    op_id,
                                    trove_context,
                                    &count,
                                    NULL,
                                    NULL,
                                    &state,
                                    TROVE_DEFAULT_TEST_TIMEOUT);
        }

        // ERROR
        if (ret <= -1)
        {
            gossip_err("%s: error: dspace setattr for root handle "
                       "attributes failed; aborting!\n", __func__);
            // ERROR;
            return -1;
        }

        /************************************/
        /* write Root Dir keyval attributes */
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: setting root dir kvals\n",
                     __func__);

        /* total 3 keyvals,
         * PVFS_DIRDATA_BITMAP, PVFS_DIRDATA_HANDLES, OBJECT_PARENT
         */
        rec_count = 3;

        /* malloc arrays and buffers - will be freed at the end */
        /* key_a */
        key_a = malloc(sizeof(PVFS_ds_keyval) * rec_count);
        if(!key_a)
        {
            // ERROR
            return -1;
        }
        ZEROMEM(key_a, sizeof(PVFS_ds_keyval) * rec_count);

        /* val_a */
        val_a = malloc(sizeof(PVFS_ds_keyval) * rec_count);
        if(!val_a)
        {
            free(key_a);
            // ERROR
            return -1;
        }
        ZEROMEM(val_a, sizeof(PVFS_ds_keyval) * rec_count);

        /* dirdata_handles */
        dirdata_handles = (PVFS_ID *)malloc((root_sid_count + 1) *
                                            sizeof(PVFS_ID));
        if(!dirdata_handles)
        {
            free(key_a);
            free(val_a);
            // ERROR
            return -1;
        }
        ZEROMEM(dirdata_handles, ((root_sid_count + 1) * sizeof(PVFS_ID)));

        /* I guesss this is actually dirdata parent OID and N SIDs */
        /* parent_handles */
        dirdata_parent_handles = (PVFS_ID *)malloc((root_sid_count + 1) *
                                            sizeof(PVFS_ID));
        if(!dirdata_parent_handles)
        {
            free(key_a);
            free(val_a);
            // ERROR
            return -1;
        }
        ZEROMEM(dirdata_parent_handles, ((root_sid_count + 1) * sizeof(PVFS_ID)));

        /* set up query keys */
        /* bitmap */
        key_a[0].buffer = DIST_DIRDATA_BITMAP_KEYSTR;
        key_a[0].buffer_sz = DIST_DIRDATA_BITMAP_KEYLEN;

        bitmap[0] = 1;
        val_a[0].buffer = bitmap;
        val_a[0].buffer_sz = 1 * sizeof(PVFS_dist_dir_bitmap_basetype);

        /* dirdata handles */
        key_a[1].buffer = DIST_DIRDATA_HANDLES_KEYSTR;
        key_a[1].buffer_sz = DIST_DIRDATA_HANDLES_KEYLEN;

        /* We use the root_sid_array for the dirdata sids because we assume
         * each copy of the root dir also has a copy of the root dirdata
         */
        dirdata_handles[0].oid = root_dirdata_handle;
        for (i = 0; i < root_sid_count; i++)
        {
            dirdata_handles[i + 1].sid = root_sid_array[i];
        }
        val_a[1].buffer = dirdata_handles;
        val_a[1].buffer_sz = OSASZ(1, root_sid_count);
        //val_a[1].buffer_sz = sizeof(PVFS_OID) +
        //                     ((root_sid_count) * sizeof(PVFS_SID));

        /* parent of root directory is NULL */
        key_a[2].buffer = OBJECT_PARENT_KEYSTR;
        key_a[2].buffer_sz = OBJECT_PARENT_KEYLEN;

        val_a[2].buffer = &ROOT_DIR_PARENT_AND_SID;
        val_a[2].buffer_sz = OSASZ(1, 1);

        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: writing root dir kvals to %s\n",
                     __func__, PVFS_OID_str(&root_handle));

        /* write root dir data to keyval database */
        ret = trove_keyval_write_list(coll_id,
                                      root_handle,
                                      key_a,
                                      val_a,
                                      rec_count,
                                      0,    /* flags */
                                      NULL, /* vtag */
                                      NULL, /* user ptr */
                                      trove_context,
                                      &op_id,
                                      NULL);

        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "%s: returning from trove_keyval_write_list\n",
                     __func__);

        // BUSY
        while (ret == 0)
        {
            gossip_debug(GOSSIP_TROVE_DEBUG,
                         "%s: waiting for kvals to write\n", __func__);
            count = 0;
            ret = trove_dspace_test(coll_id,
                                    op_id,
                                    trove_context,
                                    &count,
                                    NULL,
                                    NULL,
                                    &state,
                                    TROVE_DEFAULT_TEST_TIMEOUT);
        }

        // ((ret <= ERROR) && (state != UNINITIALIZED))
        if ((ret != 1) && (state != 0))
        {
            gossip_err("%s: keyval write_list (for root dirdata) failed.\n",
                       __func__);
            // ERROR;
            return -1;
        }

        /***********************************/
        /* write Dirdata keyval attributes */
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: setting root dirdata kvals\n",
                     __func__);

        free(key_a);
        free(val_a);
        free(dirdata_handles);
        free(dirdata_parent_handles);

        /* total 3 keyvals,
         * PVFS_DIRDATA_BITMAP, PVFS_DIRDATA_HANDLES, OBJECT_PARENT
         */
        rec_count = 3;

        /* malloc arrays and buffers - will be freed at the end */
        /* key_a */
        key_a = malloc(sizeof(PVFS_ds_keyval) * rec_count);
        if(!key_a)
        {
            // ERROR
            return -1;
        }
        ZEROMEM(key_a, sizeof(PVFS_ds_keyval) * rec_count);

        /* val_a */
        val_a = malloc(sizeof(PVFS_ds_keyval) * rec_count);
        if(!val_a)
        {
            free(key_a);
            // ERROR
            return -1;
        }
        ZEROMEM(val_a, sizeof(PVFS_ds_keyval) * rec_count);

        /* dirdata_handles */
        dirdata_handles = (PVFS_ID *)malloc((root_sid_count + 1) *
                                            sizeof(PVFS_ID));
        if(!dirdata_handles)
        {
            free(key_a);
            free(val_a);
            // ERROR
            return -1;
        }
        ZEROMEM(dirdata_handles, ((root_sid_count + 1) * sizeof(PVFS_ID)));

        /* I guesss this is actually dirdata parent OID and N SIDs */
        /* parent_handles */
        dirdata_parent_handles = (PVFS_ID *)malloc((root_sid_count + 1) *
                                            sizeof(PVFS_ID));
        if(!dirdata_parent_handles)
        {
            free(key_a);
            free(val_a);
            // ERROR
            return -1;
        }
        ZEROMEM(dirdata_parent_handles, ((root_sid_count + 1) * sizeof(PVFS_ID)));

        /* set up query keys */
        /* bitmap */
        key_a[0].buffer = DIST_DIRDATA_BITMAP_KEYSTR;
        key_a[0].buffer_sz = DIST_DIRDATA_BITMAP_KEYLEN;

        bitmap[0] = 1;
        val_a[0].buffer = bitmap;
        val_a[0].buffer_sz = 1 * sizeof(PVFS_dist_dir_bitmap_basetype);

        /* dirdata handles */
        key_a[1].buffer = DIST_DIRDATA_HANDLES_KEYSTR;
        key_a[1].buffer_sz = DIST_DIRDATA_HANDLES_KEYLEN;

        /* We use the root_sid_array for the dirdata sids because we assume
         * each copy of the root dir also has a copy of the root dirdata
         */
        dirdata_handles[0].oid = root_dirdata_handle;
        for (i = 0; i < root_sid_count; i++)
        {
            dirdata_handles[i + 1].sid = root_sid_array[i];
        }
        val_a[1].buffer = dirdata_handles;
        val_a[1].buffer_sz = OSASZ(1, root_sid_count);

        /* Dirdata has the same attributes as it needs to perform
         * collective extensible hashing with other dirdata records
         */

        /* BUT, reusing buffers is dangerous!  Asking for errors! */

        /* only change query key 2 */
        key_a[2].buffer_sz = OBJECT_PARENT_KEYLEN;
        key_a[2].buffer = OBJECT_PARENT_KEYSTR;
        
        dirdata_parent_handles[0].oid = root_handle;
        for (i = 0; i < root_sid_count; i++)
        {
            /* this validates comment above */
            dirdata_parent_handles[i + 1].sid = root_sid_array[i];
        }
        val_a[2].buffer = dirdata_parent_handles;
        val_a[2].buffer_sz = OSASZ(1, root_sid_count);

        op_id = PVFS_OP_NULL;

        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: writing root dirdata kvals to %s\n",
                     __func__, PVFS_OID_str(&root_dirdata_handle));

        /* write data to keyval database */
        ret = trove_keyval_write_list(coll_id,
                                      root_dirdata_handle,
                                      key_a,
                                      val_a,
                                      rec_count,
                                      0,    /* flags */
                                      NULL, /* vtag */
                                      NULL, /* user ptr */
                                      trove_context,
                                      &op_id,
                                      NULL);

        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "%s: returning from trove_keyval_write_list\n",
                     __func__);

        // BUSY
        while (ret == 0)
        {
            gossip_debug(GOSSIP_TROVE_DEBUG,
                         "%s: waiting for kvals to write\n", __func__);
            count = 0;
            ret = trove_dspace_test(coll_id,
                                    op_id,
                                    trove_context,
                                    &count,
                                    NULL,
                                    NULL,
                                    &state,
                                    TROVE_DEFAULT_TEST_TIMEOUT);
        }

        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "%s: finished kval wait for write loop\n", __func__);

        // ((ret <= ERROR) && (state != UNINITIALIZED))
        if ((ret != 1) && (state != 0))
        {
            gossip_err("%s: keyval write_list (for root dirdata) failed.\n",
                       __func__);
            // ERROR
            return -1;
        }

        /* Flush keyval database - just for safety sake */
        /* using BDB this should flush the entire cache */
        /* we will flush again when we clear the collection */
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: Flushing DB\n", __func__);

        op_id = PVFS_OP_NULL;
        ret = trove_keyval_flush(coll_id,
                                 TROVE_HANDLE_NULL,
                                 0,    /* flags */
                                 NULL, /* user ptr */
                                 trove_context,
                                 &op_id,
                                 NULL);

        // BUSY
        while (ret == 0)
        {
            count = 0;
            ret = trove_dspace_test(coll_id,
                                    op_id,
                                    trove_context,
                                    &count,
                                    NULL,
                                    NULL,
                                    &state,
                                    TROVE_DEFAULT_TEST_TIMEOUT);
        }

        // ((ret <= ERROR) && (state != UNINITIALIZED))
        if ((ret != 1) && (state != 0))
        {
            gossip_err("%s: keyval flush (for root dirdata) failed.\n",
                       __func__);
            // ERROR;
            return -1;
        }
    
        /*********************/
        /* create lost+found */
        /* NOT DONE YET */

    }
    
    /* flush and close file system databases */
    trove_collection_clear(TROVE_METHOD_DBPF, coll_id);

    /* close context */
    if (trove_context != -1)
    {
        trove_close_context(coll_id, trove_context);
    }

    /* shut down trove */
    trove_finalize(TROVE_METHOD_DBPF);

    mkspace_print(verbose, "%s: collection created:\n"
                  "\troot handle = %s, coll id = %d, "
                  "root string = \"%s\"\n",
                  __func__,
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
    if (dirdata_parent_handles)
    {
        free(dirdata_parent_handles);
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
