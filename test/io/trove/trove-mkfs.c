/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <trove.h>
#include <trove-test.h>

char storage_space[SSPACE_SIZE] = "/tmp/storage-space-foo";
char file_system[FS_SIZE] = "fs-foo";
char admin_info[FS_SIZE] = "admin-foo";

extern char *optarg;

int parse_args(int argc, char **argv);

int main(int argc, char **argv)
{
    int ret, count;
    TROVE_op_id op_id;
    TROVE_coll_id coll_id, coll_id_admin;
    TROVE_handle root_handle;
    TROVE_ds_state state;
    TROVE_keyval_s key, val;
    char *method_name;

    char root_handle_string[] = ROOT_HANDLE_STRING;

    ret = parse_args(argc, argv);
    if (ret < 0) {
	fprintf(stderr, "argument parsing failed.\n");
	return -1;
    }

    /* try to initialize; fails if storage space isn't there? */
    ret = trove_initialize(storage_space, 0, &method_name, 0);
    if (ret < 0) {
	fprintf(stderr, "warning: initialize failed.  trying to create storage space.\n");

	/* create the storage space */
	/* Q: what good is the op_id here if we have to match on coll_id in test fn? */
	ret = trove_storage_create(storage_space, NULL, &op_id);
	if (ret < 0) {
	    fprintf(stderr, "storage create failed.\n");
	    return -1;
	}

	/* second try at initialize, in case it failed first try. */
	ret = trove_initialize(storage_space, 0, &method_name, 0);
	if (ret < 0) {
	    fprintf(stderr, "initialized failed second time.\n");
	    return -1;
	}
    }

    /* try to look up collection used to store file system */
    ret = trove_collection_lookup(file_system, &coll_id, NULL, &op_id);
    if (ret != -1) {
	fprintf(stderr, "collection lookup succeeded before it should.\n");
	return -1;
    }

    /* try to look up collection used to store admin information */
    ret = trove_collection_lookup(admin_info, &coll_id_admin, NULL, &op_id);
    if (ret != -1) {
	    fprintf(stderr, "admin collection lookup succeeded before it should.\n");
	    return -1;
    }
    /* create the collection for the fs */
    /* Q: why do i get to pick the coll id?  so i can make it the same across nodes? */
    ret = trove_collection_create(file_system, FS_COLL_ID, NULL, &op_id);
    if (ret < 0) {
	fprintf(stderr, "collection create (fs) failed.\n");
	return -1;
    }

    /* create the collection for the admin data */
    ret = trove_collection_create(admin_info, ADMIN_COLL_ID, NULL, &op_id);
    if (ret < 0 ) {
	    fprintf(stderr, "collection create (admin) failed.\n");
	    return -1;
    }
    
    /* lookup collection.  this is redundant because we just gave it a coll. id to use,
     * but it's a good test i guess...
     */
    /* NOTE: can't test on this because we still don't know a coll_id */
    ret = trove_collection_lookup(file_system, &coll_id, NULL, &op_id);
    if (ret < 0) {
	fprintf(stderr, "collection lookup failed.\n");
	return -1;
    }

    ret = trove_collection_lookup(admin_info, &coll_id_admin, NULL, &op_id);
    if (ret < 0 ) {
	    fprintf(stderr, "collection lookup (admin) failed.\n");
	    return -1;
    }

    /* create a dataspace to hold the root directory */
    /* Q: what should the bitmask be? */
    /* Q: where are we going to define the dspace types? -- trove-test.h for now. */
    root_handle = 7;
    ret = trove_dspace_create(coll_id,
			      &root_handle,
			      0xffffffff,
			      TROVE_TEST_DIR,
			      NULL,
			      NULL,
			      &op_id);
    while (ret == 0) trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
    if (ret < 0) {
	fprintf(stderr, "dspace create (for root dir) failed.\n");
	return -1;
    }

    /* add attribute to collection for root handle */
    /* NOTE: should be using the data_sz field, but it doesn't exist yet. */
    /* NOTE: put ROOT_HANDLE_STRING in trove-test.h; not sure where it should be. */
    key.buffer = root_handle_string;
    key.buffer_sz = strlen(root_handle_string) + 1;
    val.buffer = &root_handle;
    val.buffer_sz = sizeof(root_handle);
    ret = trove_collection_seteattr(coll_id, &key, &val, 0, NULL, &op_id);
    while (ret == 0) trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
    if (ret < 0) {
	fprintf(stderr, "collection seteattr (for root handle) failed.\n");
	return -1;
    }

    /* add attribute to collection for last used handle ??? */
    
    return 0;
}

int parse_args(int argc, char **argv)
{
    int c;

    while ((c = getopt(argc, argv, "s:c:")) != EOF) {
	switch (c) {
	    case 's':
		strncpy(storage_space, optarg, SSPACE_SIZE);
		break;
	    case 'c': /* collection */
		strncpy(file_system, optarg, FS_SIZE);
		break;
	    case '?':
	    default:
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
 */
