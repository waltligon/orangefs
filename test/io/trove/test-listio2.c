/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* this is a simple test harness that operates on top of the flow
 * interface 
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <malloc.h>

#include "trove.h"

#define ROOT_HANDLE_STRING "root_handle"

int path_lookup(TROVE_coll_id coll_id,
                TROVE_context_id trove_context,
                char *path, TROVE_handle *out_handle_p);

#define MB 1024*1024

enum {
    TEST_SIZE = 20*MB,
    SSPACE_SIZE = 64,
    FS_SIZE = 64,
    PATH_SIZE = 256,
    FS_COLL_ID = 9,
    ADMIN_COLL_ID = 10
};

enum {
    TROVE_TEST_DIR  = 1,
    TROVE_TEST_FILE = 2,
    TROVE_TEST_BSTREAM = 3
};

char storage_space[SSPACE_SIZE] = "/tmp/trove-test-space";
char file_system[FS_SIZE] = "fs-foo";
char path_to_file[PATH_SIZE] = "/bar";
TROVE_handle requested_file_handle = 4095;

int main(int argc, char **argv)	
{
	int ret = -1;
	int count;
	char *mybuffer, *verify_buffer;
	int i;
	char path_name[PATH_SIZE];
	TROVE_op_id op_id;
	TROVE_coll_id coll_id;
	TROVE_handle file_handle, parent_handle;
	TROVE_ds_state state;
	char *method_name, *file_name;
	TROVE_keyval_s key, val;
        TROVE_context_id trove_context = -1;

	char *mem_offset_array[4] = {0};
	char *target_mem_offset_array[4] = {0};
	TROVE_size mem_size_array[4] = { 5*MB, 5*MB, 5*MB, 5*MB };
	int mem_count = 4;
	TROVE_offset stream_offset_array[4] = { 0,0,0,0 };
	TROVE_size stream_size_array[4] = { 5*MB, 5*MB, 5*MB, 5*MB };
	int stream_count = 4;
	TROVE_size output_size;
	void *user_ptr_array[1] = { (char *) 13 };
        int test_failed = 0;

        TROVE_extent cur_extent;
        TROVE_handle_extent_array extent_array;

	/*************************************************************/
	/* initialization stuff */

	ret = trove_initialize(storage_space, 0, &method_name, 0);
	if (ret < 0) {
	    fprintf(stderr, "initialize failed: run trove-mkfs first.\n");
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

	/* find the parent directory name */
	strcpy(path_name, path_to_file);
	for (i=strlen(path_name); i >= 0; i--) {
	    if (path_name[i] != '/') path_name[i] = '\0';
	    else break;
	}
	file_name = path_to_file + strlen(path_name);
	printf("path is %s\n", path_name);
	printf("file is %s\n", file_name);

        /* find the parent directory handle */
	ret = path_lookup(coll_id, trove_context, path_name, &parent_handle);
	if (ret < 0) {
	    return -1;
	}

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
                                  trove_context,
				  &op_id);
	while (ret == 0) ret = trove_dspace_test(
            coll_id, op_id, trove_context, &count, NULL, NULL, &state,
            TROVE_DEFAULT_TEST_TIMEOUT);
	if (ret < 0) {
	    fprintf(stderr, "dspace create failed.\n");
	    return -1;
	}

	/* TODO: set attributes of file? */

	/* add new file name/handle pair to parent directory */
	key.buffer = file_name;
	key.buffer_sz = strlen(file_name) + 1;
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

	/* memory buffer to xfer */
	mybuffer = (char *)malloc(TEST_SIZE);
	if (!mybuffer)
	{
            fprintf(stderr, "mem.\n");
            return(-1);
	}
        verify_buffer = (char *)malloc(TEST_SIZE);
        if (!verify_buffer)
        {
            fprintf(stderr, "mem.\n");
            return(-1);
        }

	memset(mybuffer, 0xFF, TEST_SIZE);
	memset(verify_buffer, 0xDE, TEST_SIZE);

	mem_offset_array[0] = mybuffer;
	mem_offset_array[1] = (mem_offset_array[0] + 5*MB);
	mem_offset_array[2] = (mem_offset_array[1] + 5*MB);
	mem_offset_array[3] = (mem_offset_array[2] + 5*MB);

	target_mem_offset_array[0] = verify_buffer;
	target_mem_offset_array[1] = (target_mem_offset_array[0] + 5*MB);
	target_mem_offset_array[2] = (target_mem_offset_array[1] + 5*MB);
	target_mem_offset_array[3] = (target_mem_offset_array[2] + 5*MB);

	/********************************/

	ret = trove_bstream_write_list(coll_id,
				       parent_handle,
				       mem_offset_array,
				       mem_size_array,
				       mem_count,
				       stream_offset_array,
				       stream_size_array,
				       stream_count,
				       &output_size,
				       0, /* flags */
				       NULL, /* vtag */
				       user_ptr_array,
                                       trove_context,
				       &op_id);
	while (ret == 0) ret = trove_dspace_test(
            coll_id, op_id, trove_context, &count, NULL, NULL, &state,
            TROVE_DEFAULT_TEST_TIMEOUT);
	if (ret < 0) {
	    fprintf(stderr, "listio write failed\n");
	    return -1;
	}

        /* should read this back out and verify here */
	ret = trove_bstream_read_list(coll_id,
                                      parent_handle,
                                      target_mem_offset_array,
                                      mem_size_array,
                                      mem_count,
                                      stream_offset_array,
                                      stream_size_array,
                                      stream_count,
                                      &output_size,
                                      0, /* flags */
                                      NULL, /* vtag */
                                      user_ptr_array,
                                      trove_context,
                                      &op_id);
	while (ret == 0) ret = trove_dspace_test(
            coll_id, op_id, trove_context, &count, NULL, NULL, &state,
            TROVE_DEFAULT_TEST_TIMEOUT);
	if (ret < 0)
        {
	    fprintf(stderr, "listio read failed\n");
	    return -1;
	}

        for(i = 0; i < TEST_SIZE; i++)
        {
            if (mybuffer[i] != verify_buffer[i])
            {
                fprintf(stderr,"data mismatch at index %d\n",i);
                test_failed = 1;
                break;
            }
        }

        fprintf(stderr,"This bstream listio test %s\n",
                (test_failed ? "failed miserably" : "passed"));

        trove_close_context(coll_id, trove_context);
	trove_finalize();
	return 0;
}


int path_lookup(TROVE_coll_id coll_id,
                TROVE_context_id trove_context,
                char *path, TROVE_handle *out_handle_p)
{
    int ret, count;
    TROVE_ds_state state;
    TROVE_keyval_s key, val;
    TROVE_op_id op_id;
    TROVE_handle handle;

    char root_handle_string[] = ROOT_HANDLE_STRING;

    /* get root */
    key.buffer = root_handle_string;
    key.buffer_sz = strlen(root_handle_string) + 1;
    val.buffer = &handle;
    val.buffer_sz = sizeof(handle);

    ret = trove_collection_geteattr(coll_id, &key, &val,
                                    0, NULL, trove_context, &op_id);
    while (ret == 0) ret = trove_dspace_test(
        coll_id, op_id, trove_context, &count, NULL, NULL, &state,
        TROVE_DEFAULT_TEST_TIMEOUT);
    if (ret < 0) {
	fprintf(stderr, "collection geteattr (for root handle) failed.\n");
	return -1;
    }

    /* TODO: handle more than just a root handle! */

    *out_handle_p = handle;
    return 0;
}
