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

#include "trove.h"
#include "trove-test.h"

char storage_space[SSPACE_SIZE] = "/tmp/trove-test-space";
char file_system[FS_SIZE] = "fs-foo";
char path_to_file[PATH_SIZE] = "/bar";
TROVE_handle requested_file_handle = 4095;

int file_count = 500;

int parse_args(int argc, char **argv);

int main(int argc, char **argv)
{
    int ret, count, i, myuid, mygid;
    TROVE_op_id op_id;
    TROVE_coll_id coll_id;
    TROVE_handle file_handle, parent_handle;
    TROVE_ds_state state;
    TROVE_keyval_s key, val;
    TROVE_ds_attributes_s s_attr;
    char *method_name, *file_name;
    char path_name[PATH_SIZE];
    time_t mytime;
    TROVE_extent cur_extent;
    TROVE_handle_extent_array extent_array;
    TROVE_context_id trove_context = -1;

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

    ret = trove_open_context(coll_id, &trove_context);
    if (ret < 0)
    {
        fprintf(stderr, "trove_open_context failed\n");
        return -1;
    }

    myuid = getuid();
    mygid = getgid();
    mytime = time(NULL);

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
    
    for (i=0; i < file_count; i++) {
	char tmp_file_name[PATH_SIZE];
	file_handle = 0;

        cur_extent.first = cur_extent.last = requested_file_handle;
        extent_array.extent_count = 1;
        extent_array.extent_array = &cur_extent;
	ret = trove_dspace_create(coll_id,
                                  &extent_array,
				  &file_handle,
				  TROVE_TEST_FILE,
				  NULL,
				  TROVE_FORCE_REQUESTED_HANDLE,
				  NULL,
                                  trove_context,
				  &op_id);
	while (ret == 0) ret = trove_dspace_test(
            coll_id, op_id, trove_context, &count, NULL, NULL, &state,
            TROVE_DEFAULT_TEST_TIMEOUT);
	if (ret < 0) {
	    fprintf(stderr, "dspace create failed.\n");
	    return -1;
	}

	s_attr.fs_id  = coll_id; /* for now */
	s_attr.handle = file_handle;
	s_attr.type   = TROVE_TEST_FILE; /* shouldn't need to fill this one in. */
	s_attr.uid    = myuid;
	s_attr.gid    = mygid;
	s_attr.mode   = 0755;
	s_attr.ctime  = mytime;
	count = 1;

	ret = trove_dspace_setattr(coll_id,
				   file_handle,
				   &s_attr,
				   0 /* flags */,
				   NULL /* user ptr */,
                                   trove_context,
				   &op_id);
	while (ret == 0) ret = trove_dspace_test(
            coll_id, op_id, trove_context, &count, NULL, NULL, &state,
            TROVE_DEFAULT_TEST_TIMEOUT);
	if (ret < 0) return -1;

	/* add new file name/handle pair to parent directory */
	snprintf(tmp_file_name, PATH_SIZE, "%s/file%d", path_name, i);
	key.buffer = tmp_file_name;
	key.buffer_sz = strlen(tmp_file_name) + 1;
	val.buffer = &file_handle;
	val.buffer_sz = sizeof(file_handle);

	ret = trove_keyval_write(coll_id, parent_handle, &key, &val,
                                 0, NULL, NULL, trove_context, &op_id);
	while (ret == 0) ret = trove_dspace_test(
            coll_id, op_id, trove_context, &count, NULL, NULL, &state,
            TROVE_DEFAULT_TEST_TIMEOUT);
	if (ret < 0) {
	    fprintf(stderr, "keyval write failed.\n");
	    return -1;
	}
    }
    
    trove_close_context(coll_id, trove_context);
    trove_finalize();

    return 0;
}

int parse_args(int argc, char **argv)
{
    int c;

    while ((c = getopt(argc, argv, "s:c:p:n:")) != EOF) {
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
	    case 'n':
		file_count = atoi(optarg);
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
 * vim: ts=8 sts=4 sw=4 expandtab
 */
