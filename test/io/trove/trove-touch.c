/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <getopt.h>

#include <trove.h>
#include "trove-test.h"

char storage_space[SSPACE_SIZE] = "/tmp/trove-test-space";
char file_system[FS_SIZE] = "fs-foo";
char path_to_file[PATH_SIZE] = "/bar";
TROVE_handle requested_file_handle = 4095;

int parse_args(int argc, char **argv);

int main(int argc, char **argv)
{
    int ret, count, i;
    TROVE_op_id op_id;
    TROVE_coll_id coll_id;
    TROVE_handle file_handle, parent_handle;
    TROVE_ds_state state;
    TROVE_keyval_s key, val;
    TROVE_ds_attributes_s s_attr;
    char *method_name, *file_name;
    char path_name[PATH_SIZE];
    TROVE_extent cur_extent;
    TROVE_handle_extent_array extent_array;

    ret = parse_args(argc, argv);
    if (ret < 0) {
	fprintf(stderr, "argument parsing failed.\n");
	return -1;
    }

    ret = trove_initialize(storage_space, 0, &method_name, 0);
    if (ret < 0) {
	fprintf(stderr, "initialize failed.\n");
	return -1;
    }

    /* try to look up collection used to store file system */
    ret = trove_collection_lookup(file_system, &coll_id, NULL, &op_id);
    if (ret < 0) {
	fprintf(stderr, "collection lookup failed.\n");
	return -1;
    }

    /* find the parent directory name */
    strcpy(path_name, path_to_file);
    for (i=strlen(path_name); i >= 0; i--) {
	if (path_name[i] != '/') path_name[i] = '\0';
	else break;
    }
    file_name = path_to_file + strlen(path_name);
#if 0
    printf("path is %s\n", path_name);
    printf("file is %s\n", file_name);
#endif

    /* find the parent directory handle */
    ret = path_lookup(coll_id, path_name, &parent_handle);
    if (ret < 0) {
	return -1;
    }

    /* TODO: verify that this is in fact a directory! */
    
    /* Q: how do I know what handle to use for the new file? */
    file_handle = 0;

    cur_extent.first = cur_extent.last = requested_file_handle;
    extent_array.extent_count = 1;
    extent_array.extent_array = &cur_extent;
    ret = trove_dspace_create(coll_id,
                              &extent_array,
			      &file_handle,
			      TROVE_TEST_FILE,
			      NULL,
			      0 /* flags */,
			      NULL,
			      &op_id);
    while (ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
    if (ret < 0) {
	fprintf(stderr, "dspace create failed.\n");
	return -1;
    }

    /* TODO: set attributes of file? */
    s_attr.fs_id  = coll_id; /* for now */
    s_attr.handle = file_handle;
    s_attr.type   = TROVE_TEST_FILE; /* shouldn't need to fill this one in. */
    s_attr.uid    = getuid();
    s_attr.gid    = getgid();
    s_attr.mode   = 0755;
    s_attr.ctime  = time(NULL);
    count = 1;

    ret = trove_dspace_setattr(coll_id,
			       file_handle,
			       &s_attr,
			       TROVE_SYNC,
			       NULL,
			       &op_id);
    while (ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
    if (ret < 0) return -1;

    /* add new file name/handle pair to parent directory */
    key.buffer = file_name;
    key.buffer_sz = strlen(file_name) + 1;
    val.buffer = &file_handle;
    val.buffer_sz = sizeof(file_handle);

    ret = trove_keyval_write(coll_id, parent_handle, &key, &val, 0, NULL, NULL, &op_id);
    while (ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
    if (ret < 0) {
	fprintf(stderr, "keyval write failed.\n");
	return -1;
    }
    trove_finalize();

    printf("created file %s (handle = %d)\n", file_name, (int) file_handle);

    return 0;
}

int parse_args(int argc, char **argv)
{
    int c;

    while ((c = getopt(argc, argv, "s:c:p:h:")) != EOF) {
	switch (c) {
	    case 's':
		strncpy(storage_space, optarg, SSPACE_SIZE);
		break;
	    case 'c': /* collection */
		strncpy(file_system, optarg, FS_SIZE);
		break;
	    case 'p':
		strncpy(path_to_file, optarg, PATH_SIZE);
		break;
	    case 'h':
		requested_file_handle = atoll(optarg);
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
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
