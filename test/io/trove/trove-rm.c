/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include <trove.h>
#include <trove-test.h>

char storage_space[SSPACE_SIZE] = "/tmp/trove-test-space";
char file_system[FS_SIZE] = "fs-foo";
char path_to_file[PATH_SIZE] = "/bar";
TROVE_handle requested_file_handle = 4095;

int parse_args(int argc, char **argv);

int main(int argc, char **argv)
{
    int ret, count, i;
    char *method_name, *file_name;
    char path_name[PATH_SIZE];

    TROVE_op_id op_id;
    TROVE_coll_id coll_id;
    TROVE_handle file_handle, parent_handle;
    TROVE_ds_state state;
    TROVE_keyval_s key, val;
    TROVE_ds_attributes_s s_attr;
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

    /* TODO: make a is_dir function... maybe make a full blown stat(2)? */
    
    /* look up the handle for the file */
    memset(&key, 0, sizeof(key));
    memset(&val, 0, sizeof(val));
    key.buffer = file_name;
    key.buffer_sz = strlen(file_name)+1;
    val.buffer = &file_handle;
    val.buffer_sz = sizeof(TROVE_handle);

    /* it would be smart to verify that this is a directory first... */
    ret = trove_keyval_read(coll_id, parent_handle, &key, &val,
                            0, NULL, NULL, trove_context, &op_id);
    while (ret == 0) ret = trove_dspace_test(
        coll_id, op_id, trove_context, &count, NULL, NULL, &state,
        TROVE_DEFAULT_TEST_TIMEOUT);
    if ( ret < 0 || state == -1) {
	    fprintf(stderr, "read failed for key %s\n", file_name);
	    return -1;
    }

    ret = trove_dspace_getattr(coll_id,
			       file_handle,
			       &s_attr,
			       0 /* flags */,
			       NULL,
                               trove_context,
			       &op_id);
    while (ret == 0) ret = trove_dspace_test(
        coll_id, op_id, trove_context, &count, NULL, NULL, &state,
        TROVE_DEFAULT_TEST_TIMEOUT);
    if (ret < 0) return -1;

    if (s_attr.type != TROVE_TEST_FILE) {
	fprintf(stderr, "%s is not a file.\n", file_name);
	return -1;
    }

    /* 'handles are everything':  now that we've gotten a handle from the
     * file_name, we can wipe the keyval (via name) and the dspace (via
     * handle)*/

    key.buffer = file_name;
    key.buffer_sz = strlen(file_name)+1;

    ret = trove_keyval_remove(coll_id, parent_handle, &key,
                              0, NULL, NULL, trove_context, &op_id);
    while (ret == 0) ret = trove_dspace_test(
        coll_id, op_id, trove_context, &count, NULL, NULL, &state,
        TROVE_DEFAULT_TEST_TIMEOUT);
    if (ret < 0 ) {
	    fprintf(stderr, "removal failed for %s\n", file_name);
	    return -1;
    }

    /* the question: is it up to the caller to clean up the dspace if it removed the last entry?  no no no*/
    /* gar gar being dense:  the dspace gets removed.  */
    ret = trove_dspace_remove(coll_id, 
			      file_handle,
			      TROVE_SYNC,
			      NULL,
                              trove_context,
			      &op_id);
    while (ret == 0) ret = trove_dspace_test(
        coll_id, op_id, trove_context, &count, NULL, NULL, &state,
        TROVE_DEFAULT_TEST_TIMEOUT);
    if (ret < 0) {
	fprintf(stderr, "dspace remove failed.\n");
	return -1;
    }

    trove_close_context(coll_id, trove_context);
    trove_finalize();
    printf("file %s removed (file handle = %d, parent handle = %d).\n",
	   file_name, 
	   (int) file_handle,
	   (int) parent_handle);

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
 * vim: ts=8 sts=4 sw=4 expandtab
 */
