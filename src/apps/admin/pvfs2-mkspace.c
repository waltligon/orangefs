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

#include "pvfs2-attr.h"
#include "trove.h"

static char storage_space[PATH_MAX] = "/tmp/pvfs2-test-space";
static char collection[PATH_MAX] = "fs-foo";
static int verbose = 0;
static int new_coll_id = 9;
static int new_root_handle = 7;

static int parse_args(int argc, char **argv);

int main(int argc, char **argv)
{
    int ret, count;
    TROVE_op_id op_id;
    TROVE_coll_id coll_id;
    TROVE_handle root_handle, ent_handle;
    TROVE_ds_state state;
    TROVE_keyval_s key, val;
    char *method_name;
    char metastring[] = "metadata";
    char entstring[]  = "dir_ent";
    struct PVFS_object_attr attr; /* from proto/pvfs2-attr.h */

    char root_handle_string[] = "root_handle"; /* TODO: DEFINE ELSEWHERE? */

    ret = parse_args(argc, argv);
    if (ret < 0) {
	fprintf(stderr,
		"%s: error: argument parsing failed; aborting!\n",
		argv[0]);
	return -1;
    }

    /* try to initialize; fails if storage space isn't there? */
    ret = trove_initialize(storage_space, 0, &method_name, 0);
    if (ret >= 0) {
	fprintf(stderr,
		"%s: error: storage space %s already exists; aborting!\n",
		argv[0],
		storage_space);
	return -1;
    }

    /* create the storage space */
    /* Q: what good is the op_id here if we have to match on coll_id in test fn? */
    ret = trove_storage_create(storage_space, NULL, &op_id);
    if (ret != 1) {
	fprintf(stderr,
		"%s: error: storage create failed; aborting!\n",
		argv[0]);
	return -1;
    }

    /* second try at initialize, in case it failed first try. */
    ret = trove_initialize(storage_space, 0, &method_name, 0);
    if (ret < 0) {
	fprintf(stderr,
		"%s: error: trove initialize failed; aborting!\n",
		argv[0]);
	return -1;
    }

    if (verbose) fprintf(stderr,
			 "%s: info: created storage space '%s'.\n",
			 argv[0],
			 storage_space);

    /* try to look up collection used to store file system */
    ret = trove_collection_lookup(collection, &coll_id, NULL, &op_id);
    if (ret != -1) {
	fprintf(stderr, "%s: error: collection lookup succeeded before it should; aborting!\n", argv[0]);
	trove_finalize();
	return -1;
    }

    /* create the collection for the fs */
    ret = trove_collection_create(collection, new_coll_id, NULL, &op_id);
    if (ret != 1) {
	fprintf(stderr,
		"%s: error: collection create failed for collection '%s'.\n",
		argv[0],
		collection);
	return -1;
    }

    /* lookup collection.  this is redundant because we just gave it a coll. id to use,
     * but it's a good test i guess...
     */
    /* NOTE: can't test on this because we still don't know a coll_id */
    ret = trove_collection_lookup(collection, &coll_id, NULL, &op_id);
    if (ret != 1) {
	fprintf(stderr,
		"%s: error: collection lookup failed for collection '%s' after create.\n",
		argv[0],
		collection);
	return -1;
    }

    if (verbose) fprintf(stderr,
			 "%s: info: created collection '%s'.\n",
			 argv[0],
			 collection);

    /* create a dataspace to hold the root directory */
    /* Q: what should the bitmask be? */
    /* Q: where are we going to define the dspace types? -- trove-test.h for now. */
    root_handle = new_root_handle;
    ret = trove_dspace_create(coll_id,
			      &root_handle,
			      0xffffffff,
			      PVFS_TYPE_DIRECTORY,
			      NULL,
			      TROVE_SYNC,
			      NULL,
			      &op_id);
    while (ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
    if (ret != 1 && state != 0) {
	fprintf(stderr, "dspace create (for root dir) failed.\n");
	return -1;
    }

    if (verbose) fprintf(stderr,
			 "%s: info: created root directory with handle 0x%x.\n",
			 argv[0],
			 (int) root_handle);

    /* add attribute to collection for root handle */
    /* NOTE: should be using the data_sz field, but it doesn't exist yet. */
    key.buffer = root_handle_string;
    key.buffer_sz = strlen(root_handle_string) + 1;
    val.buffer = &root_handle;
    val.buffer_sz = sizeof(root_handle);
    ret = trove_collection_seteattr(coll_id, &key, &val, 0, NULL, &op_id);
    while (ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
    if (ret < 0) {
	fprintf(stderr,
		"%s: error: collection seteattr (for root handle) failed; aborting!\n",
		argv[0]);
	return -1;
    }

    memset(&attr, 0, sizeof(attr));
    attr.owner    = 100;
    attr.group    = 100;
    attr.perms    = 0777;
    attr.objtype  = PVFS_TYPE_DIRECTORY;

    key.buffer    = metastring;
    key.buffer_sz = strlen(metastring) + 1;
    val.buffer    = &attr;
    val.buffer_sz = sizeof(attr);

    ret = trove_keyval_write(coll_id,
			     root_handle,
			     &key,
			     &val,
			     TROVE_SYNC,
			     0 /* vtag */,
			     NULL /* user ptr */,
			     &op_id);
    while (ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
    if (ret < 0) {
	fprintf(stderr,
		"%s: error: keyval write for root handle attributes failed; aborting!\n",
		argv[0]);
	return -1;
    }

    /* create dataspace to hold directory entries */
    ent_handle = root_handle - 1; /* just put something in here */
    ret = trove_dspace_create(coll_id,
			      &ent_handle,
			      0xffffffff,
			      PVFS_TYPE_DIRDATA,
			      NULL,
			      TROVE_SYNC,
			      NULL,
			      &op_id);
    while (ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
    if (ret != 1 && state != 0) {
	fprintf(stderr, "dspace create (for dirent storage) failed.\n");
	return -1;
    }

    if (verbose) fprintf(stderr,
			 "%s: info: created dspace for dirents with handle 0x%x.\n",
			 argv[0],
			 (int) ent_handle);

    key.buffer    = entstring;
    key.buffer_sz = strlen(entstring) + 1;
    val.buffer    = &ent_handle;
    val.buffer_sz = sizeof(PVFS_handle);

    ret = trove_keyval_write(coll_id,
			     root_handle,
			     &key,
			     &val,
			     TROVE_SYNC,
			     0 /* vtag */,
			     NULL /* user ptr */,
			     &op_id);
    while (ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
    if (ret < 0) {
	fprintf(stderr,
		"%s: error: keyval write for handle used to store dirents failed; aborting!\n",
		argv[0]);
	return -1;
    }

    if (verbose) fprintf(stderr,
			 "%s: info: wrote attributes for root directory.\n",
			 argv[0]);

    trove_finalize();

    if (verbose) fprintf(stderr,
			 "%s: info: collection created (root handle = %d, coll id = %d, root string = %s).\n",
			 argv[0],
			 (int) root_handle,
			 (int) coll_id,
			 root_handle_string);

    return 0;
}

static int parse_args(int argc, char **argv)
{
    int c;

    while ((c = getopt(argc, argv, "s:c:i:r:v")) != EOF) {
	switch (c) {
	    case 's':
		strncpy(storage_space, optarg, PATH_MAX);
		break;
	    case 'c': /* collection */
		strncpy(collection, optarg, PATH_MAX);
		break;
	    case 'i':
		new_coll_id = atoi(optarg);
	    case 'r':
		new_root_handle = atoi(optarg);
	    case 'v':
		verbose = 1;
		break;
	    case '?':
	    default:
		fprintf(stderr, "%s: error: unrecognized option '%c'.\n", argv[0], c);
		fprintf(stderr,
			"usage: %s [-s storage_space] [-c collection_name] [-i coll_id] [-r root_handle] [-v]\n",
			argv[0]);
		fprintf(stderr, "\tdefault storage space is '/tmp/pvfs2-test-space'.\n");
		fprintf(stderr, "\tdefault initial collection is 'fs-foo'.\n");
		fprintf(stderr, "\tdefault collection id is '9'.\n");
		fprintf(stderr, "\tdefault root handle is '7'.\n");
		fprintf(stderr, "\t'-v' turns on verbose output.\n");
		return -1;
	}
    }
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
