/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "pvfs2-attr.h"
#include "trove.h"
#include "mkspace.h"


int pvfs2_mkspace(
    char *storage_space,
    char *collection,
    TROVE_coll_id coll_id,
    int root_handle,
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

    if (verbose)
    {
        fprintf(stderr,"Storage space: %s\n",storage_space);
        fprintf(stderr,"Collection   : %s\n",collection);
        fprintf(stderr,"Collection ID: %d\n",coll_id);
        fprintf(stderr,"Root Handle  : %d\n",root_handle);
        fprintf(stderr,"Handle Ranges: %s\n",handle_ranges);
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
            fprintf(stderr,"error: storage space %s already "
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
            fprintf(stderr,"error: storage create failed; aborting!\n");
            return -1;
        }
    }

    /* second try at initialize, in case it failed first try. */
    ret = trove_initialize(storage_space, 0, &method_name, 0);
    if (ret < 0)
    {
	fprintf(stderr,"error: trove initialize failed; aborting!\n");
	return -1;
    }

    if (verbose)
    {
        fprintf(stderr,"info: created storage space '%s'.\n",
                storage_space);
    }

    /* try to look up collection used to store file system */
    ret = trove_collection_lookup(collection, &coll_id, NULL, &op_id);
    if (ret != -1)
    {
	fprintf(stderr, "error: collection lookup succeeded before it "
                "should; aborting!\n");
	trove_finalize();
	return -1;
    }

    /* create the collection for the fs */
    ret = trove_collection_create(collection, coll_id, NULL, &op_id);
    if (ret != 1)
    {
	fprintf(stderr,"error: collection create failed for "
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
	fprintf(stderr,"error: collection lookup failed for "
                "collection '%s' after create.\n",collection);
	return -1;
    }

    if (verbose)
    {
        fprintf(stderr,"info: created collection '%s'.\n",collection);
    }

    /* we have a three-step process for starting trove:
     * initialize, collection_lookup, collection_setinfo */
    ret = trove_collection_setinfo(coll_id,
                                   TROVE_COLLECTION_HANDLE_RANGES,
                                   handle_ranges);
    if (ret < 0)
    {
	fprintf(stderr, "Error adding handle ranges\n");
	return -1;
    }

    /*
      if a root_handle is specified, 1) create a dataspace to
      hold the root directory 2) create the dspace for dir
      entries, 3) set attributes on the dspace
    
      Q: where are we going to define the dspace types? --
      trove-test.h for now.
    */
    if (new_root_handle)
    {
        ret = trove_dspace_create(coll_id,
                                  &new_root_handle,
                                  PVFS_TYPE_DIRECTORY,
                                  NULL,
                                  TROVE_SYNC,
                                  NULL,
                                  &op_id);
        while (ret == 0)
        {
            ret = trove_dspace_test(coll_id, op_id, &count,
                                    NULL, NULL, &state);
        }
        if (ret != 1 && state != 0)
        {
            fprintf(stderr, "dspace create (for root dir) failed.\n");
            return -1;
        }

        if (verbose)
        {
            fprintf(stderr,"info: created root directory with handle "
                    "0x%x.\n",(int)new_root_handle);
        }

        /*
          add attribute to collection for root handle
          NOTE: should be using the data_sz field, but it doesn't exist yet.
        */
        key.buffer = root_handle_string;
        key.buffer_sz = strlen(root_handle_string) + 1;
        val.buffer = &new_root_handle;
        val.buffer_sz = sizeof(new_root_handle);
        ret = trove_collection_seteattr(coll_id, &key,
                                        &val, 0, NULL, &op_id);
        while (ret == 0)
        {
            ret = trove_dspace_test(coll_id, op_id,
                                    &count, NULL, NULL, &state);
        }
        if (ret < 0)
        {
            fprintf(stderr,"error: collection seteattr (for root "
                    "handle) failed; aborting!\n");
            return -1;
        }

        memset(&attr, 0, sizeof(attr));
        attr.owner    = 100;
        attr.group    = 100;
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
                                 &op_id);
        while (ret == 0)
        {
            ret = trove_dspace_test(coll_id, op_id,
                                    &count, NULL, NULL, &state);
        }
        if (ret < 0)
        {
            fprintf(stderr,"error: keyval write for root handle "
                    "attributes failed; aborting!\n");
            return -1;
        }

        /* create dataspace to hold directory entries */
        ent_handle = new_root_handle - 1; /* just put something in here */
        ret = trove_dspace_create(coll_id,
                                  &ent_handle,
                                  PVFS_TYPE_DIRDATA,
                                  NULL,
                                  TROVE_SYNC,
                                  NULL,
                                  &op_id);
        while (ret == 0)
        {
            ret = trove_dspace_test(coll_id, op_id,
                                    &count, NULL, NULL, &state);
        }
        if (ret != 1 && state != 0)
        {
            fprintf(stderr, "dspace create (for dirent storage) failed.\n");
            return -1;
        }

        if (verbose)
        {
            fprintf(stderr,"info: created dspace for dirents with handle "
                    "0x%x.\n",(int) ent_handle);
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
                                 &op_id);
        while (ret == 0)
        {
            ret = trove_dspace_test(coll_id, op_id,
                                    &count, NULL, NULL, &state);
        }
        if (ret < 0)
        {
            fprintf(stderr,"error: keyval write for handle used to "
                    "store dirents failed; aborting!\n");
            return -1;
        }

        if (verbose)
        {
            fprintf(stderr,"info: wrote attributes for root directory.\n");
        }
    }

    trove_finalize();

    if (verbose)
    {
        fprintf(stderr,
                "info: collection created (root handle = %d, coll "
                "id = %d, root string = %s).\n",root_handle,
                (int)coll_id,root_handle_string);
    }
    return 0;
}
