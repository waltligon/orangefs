/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <trove.h>
#include <trove-test.h>

char storage_space[SSPACE_SIZE] = "/tmp/storage-space-foo";
char file_system[FS_SIZE] = "fs-foo";
char path_to_file[PATH_SIZE] = "/bar";
TROVE_handle requested_file_handle = 4095;

extern char *optarg;

int parse_args(int argc, char **argv);
int path_lookup(TROVE_coll_id coll_id, char *path, TROVE_handle *out_handle_p);

int main(int argc, char **argv)
{
    int ret, count, i;
    TROVE_op_id op_id;
    TROVE_coll_id coll_id;
    TROVE_handle file_handle, parent_handle;
    TROVE_ds_state state;
    TROVE_keyval_s key, val;
    char *method_name, *file_name;
    TROVE_handle handle_buf;
    char path_name[PATH_SIZE];


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
    printf("path is %s\n", path_name);
    printf("file is %s\n", file_name);

    /* find the parent directory handle */
    ret = path_lookup(coll_id, path_name, &parent_handle);
    if (ret < 0) {
	return -1;
    }

    /* TODO: verify that this is in fact a directory! */
    /* TODO: make a is_dir function... maybe make a full blown stat(2)? */
    
    /* Q: how do I know what handle to use for the to-be-deleted file? */
    /* A: look it up: */
    memset(&key, 0, sizeof(key));
    memset(&val, 0, sizeof(val));
    key.buffer = file_name;
    key.buffer_sz = strlen(file_name)+1;
    val.buffer = &handle_buf;
    val.buffer_sz = sizeof(TROVE_handle);

    ret = trove_keyval_read(coll_id, parent_handle, &key, &val, 0, NULL, NULL, &op_id);
    while (ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
    if ( ret < 0 || state == -1) {
	    fprintf(stderr, "read failed for key %s\n", file_name);
	    return -1;
    }
    file_handle = *(TROVE_handle*)val.buffer;

    /* 'handles are everything':  now that we've gotten a handle from the
     * file_name, we can wipe the keyval (via name) and the dspace (via
     * handle)*/

    key.buffer = file_name;
    key.buffer_sz = strlen(file_name)+1;

    ret = trove_keyval_remove(coll_id, parent_handle, &key, 0, NULL, NULL, &op_id);
    while (ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
    if (ret < 0 ) {
	    fprintf(stderr, "removal failed for %s\n", file_name);
	    return -1;
    }

    /* the question: is it up to the caller to clean up the dspace if it removed the last entry?  no no no*/
    /* gar gar being dense:  the dspace gets removed.  */
    ret = trove_dspace_remove(coll_id, 
		    file_handle,
		    NULL,
		    &op_id);
    while (ret == 0) trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
    if (ret < 0) {
	fprintf(stderr, "dspace remove failed.\n");
	return -1;
    }

   trove_finalize();

    return 0;
}

int path_lookup(TROVE_coll_id coll_id, char *path, TROVE_handle *out_handle_p)
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
    ret = trove_collection_geteattr(coll_id, &key, &val, 0, NULL, &op_id);
    while (ret == 0) trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
    if (ret < 0) {
	fprintf(stderr, "collection geteattr (for root handle) failed.\n");
	return -1;
    }

    /* TODO: handle more than just a root handle! */

    *out_handle_p = handle;
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
 */
