/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* For a trove file show its list of keys  */

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <getopt.h>

#include <trove.h>
#include "trove-test.h"
#include "pvfs2-internal.h"

char storage_space[SSPACE_SIZE] = "/tmp/trove-test-space";
char file_system[FS_SIZE] = "fs-foo";
char path_to_file[PATH_SIZE];

#define KEYVAL_ARRAY_LEN 10
int parse_args(int argc, char **argv);

static inline int file_lookup(
    TROVE_coll_id coll_id,
    /*TROVE_context_id context_id, FIXME: Hacked for now...uses 0*/
    char *path,
    TROVE_handle *out_handle_p)
{
    int i=0, ret, count, path_off=0;
    TROVE_ds_state state;
    TROVE_keyval_s key, val;
    TROVE_op_id op_id;
    TROVE_handle handle, parent_handle;
    TROVE_ds_attributes_s s_attr;
    char dir[PATH_SIZE];
    TROVE_context_id context_id = 0; /* FIXME: Hacked for now */

    /* get root handle */
    key.buffer = ROOT_HANDLE_KEYSTR;
    key.buffer_sz = ROOT_HANDLE_KEYLEN;
    val.buffer = &handle;
    val.buffer_sz = sizeof(handle);
    ret = trove_collection_geteattr(
        coll_id, &key, &val, 0, NULL, context_id, &op_id);
    while (ret == 0) ret = trove_dspace_test(
        coll_id, op_id, context_id, &count, NULL, NULL, &state,
        TROVE_DEFAULT_TEST_TIMEOUT);
    if (ret < 0) {
	fprintf(stderr, "collection geteattr (for root handle) failed.\n");
	return -1;
    }

#if 0
    printf("path_lookup: looking up %s, root handle is %d\n", path, (int) handle);
#endif

    for (;;) {
	parent_handle = handle;
	while (path[path_off] == '/') path_off++; /* get past leading "/"s */
	if (path[path_off] == 0) break;
	
	/* chop off the next part of the path */
	i = 0;
	while (path[path_off] != 0 && path[path_off] != '/') {
	    dir[i] = path[path_off++];
	    i++;
	}
	dir[i] = 0;
	
	key.buffer = dir;
	key.buffer_sz = strlen(dir) + 1; /* including terminator...maybe we shouldn't do that? */
	val.buffer = &handle;
	val.buffer_sz = sizeof(handle);
	ret = trove_keyval_read(
            coll_id, parent_handle, &key, &val, 0,
            NULL, NULL, context_id, &op_id, NULL);
	while (ret == 0) ret = trove_dspace_test(
            coll_id, op_id, context_id, &count, NULL, NULL, &state,
            TROVE_DEFAULT_TEST_TIMEOUT);
	if (ret < 0) {
	    fprintf(stderr, "keyval read failed.\n");
	    return -1;
	}
	if (state != 0) {
	    fprintf(stderr, "keyval read failed.\n");
	    return -1;
	}

	ret = trove_dspace_getattr(
            coll_id, handle, &s_attr, 0, NULL, context_id, &op_id, NULL);
	while (ret == 0) ret = trove_dspace_test(
            coll_id, op_id, context_id, &count, NULL, NULL, &state,
            TROVE_DEFAULT_TEST_TIMEOUT);
	if (ret < 0) return -1;
	if (state != 0) return -1;
    }
    *out_handle_p = handle;
    return 0;
}

int main(int argc, char **argv)
{
    int ret, count, i;
    TROVE_op_id op_id;
    TROVE_coll_id coll_id;
    TROVE_handle file_handle;
    char path_name[PATH_SIZE];
    TROVE_context_id trove_context = -1;
    TROVE_ds_state state;
    TROVE_keyval_s key[KEYVAL_ARRAY_LEN];
    TROVE_ds_position pos = TROVE_ITERATE_START;
    char key_names[KEYVAL_ARRAY_LEN][PATH_SIZE];

    ret = parse_args(argc, argv);
    if (ret < 0) {
	fprintf(stderr, "argument parsing failed.\n");
	return -1;
    }

    ret = trove_initialize(
        TROVE_METHOD_DBPF, NULL, storage_space, 0);
    if (ret < 0) {
	fprintf(stderr, "initialize failed.\n");
	return -1;
    }

    /* try to look up collection used to store file system */
    ret = trove_collection_lookup(
        TROVE_METHOD_DBPF, file_system, &coll_id, NULL, &op_id);
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

    strcpy(path_name, path_to_file);

    /* find the handle */
    ret = file_lookup(coll_id, path_name, &file_handle);
    if (ret < 0) 
    {
	return -1;
    }
    /* iterate thru the keys of this file */
    for (i = 0; i < KEYVAL_ARRAY_LEN; i++)
    {
        key[i].buffer = key_names[i];
        key[i].buffer_sz = PATH_SIZE;
    }
    for (;;) {
        int num_processed = KEYVAL_ARRAY_LEN, it_ret;
        it_ret = trove_keyval_iterate_keys(coll_id,
                                           file_handle,
                                           &pos,
                                           key,
                                           &num_processed,
                                           0,
                                           NULL,
                                           NULL,
                                           trove_context,
                                           &op_id,
                                           NULL);
        if (it_ret == -1)
            return -1;
        while (it_ret == 0) it_ret = trove_dspace_test(
                coll_id, op_id, trove_context, &count, NULL, NULL, &state,
                TROVE_DEFAULT_TEST_TIMEOUT);
        if (it_ret < 0)
            return -1;
        if (num_processed == 0) return 0;
        for (i = 0; i < num_processed; i++) {
            printf("key %s\n", key_names[i]);
        }
    }
    trove_close_context(coll_id, trove_context);
    trove_finalize(TROVE_METHOD_DBPF);
    return 0;
}

int parse_args(int argc, char **argv)
{
    int c;

    while ((c = getopt(argc, argv, "s:c:f:")) != EOF) {
	switch (c) {
	    case 's':
		strncpy(storage_space, optarg, SSPACE_SIZE);
		break;
	    case 'c': /* collection */
		strncpy(file_system, optarg, FS_SIZE);
		break;
	    case 'f':
		strncpy(path_to_file, optarg, PATH_SIZE);
		break;
	    case '?':
	    default:
		fprintf(stderr, "%s: [-s storage space] [-c collection] [-f file_path ]\n", argv[0]);
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
