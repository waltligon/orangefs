#ifndef __TROVE_TEST_H
#define __TROVE_TEST_H

#define ROOT_HANDLE_STRING "root_handle"

enum {
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

static int path_lookup(TROVE_coll_id coll_id, char *path, TROVE_handle *out_handle_p)
{
    int i=0, ret, count, path_off=0;
    TROVE_ds_state state;
    TROVE_keyval_s key, val;
    TROVE_op_id op_id;
    TROVE_handle handle, parent_handle;
    TROVE_ds_attributes_s s_attr;
    char dir[PATH_SIZE];

    char root_handle_string[] = ROOT_HANDLE_STRING;

    /* get root handle */
    key.buffer = root_handle_string;
    key.buffer_sz = strlen(root_handle_string) + 1;
    val.buffer = &handle;
    val.buffer_sz = sizeof(handle);
    ret = trove_collection_geteattr(coll_id, &key, &val, 0, NULL, &op_id);
    while (ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
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
	ret = trove_keyval_read(coll_id, parent_handle, &key, &val, 0, NULL, NULL, &op_id);
	while (ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
	if (ret < 0) {
	    fprintf(stderr, "keyval read failed.\n");
	    return -1;
	}
	if (state != 0) {
	    fprintf(stderr, "keyval read failed.\n");
	    return -1;
	}

	/* TODO: verify that this is in fact a directory! */
	ret = trove_dspace_getattr(coll_id, handle, &s_attr, NULL, &op_id);
	while (ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
	if (ret < 0) return -1;
	if (state != 0) return -1;
	
	if (s_attr.type != TROVE_TEST_DIR) {
	    fprintf(stderr, "%s is not a directory.\n", dir);
	    return -1;
	}

#if 0
	printf("  path_lookup: handle for path component %s is %d\n", dir, (int) handle);
#endif
    }

    *out_handle_p = handle;
    return 0;
}

#endif
