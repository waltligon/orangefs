/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <trove.h>
#include <trove-test.h>

char storage_space[SSPACE_SIZE] = "/tmp/storage-space-foo";
char file_system[FS_SIZE] = "fs-foo";
char path_to_dir[PATH_SIZE] = "/default_dir";
TROVE_handle requested_file_handle = 1111;

int parse_args(int argc, char **argv);
int path_lookup(TROVE_coll_id coll_id, char *path, TROVE_handle *out_handle_p);

int main(int argc, char ** argv) 
{
    int ret, count, i;
    char *method_name, *dir_name;
    char path_name[PATH_SIZE];

    TROVE_op_id op_id;
    TROVE_coll_id coll_id;
    TROVE_ds_state state;
    TROVE_handle parent_handle, file_handle;
    TROVE_ds_attributes_s s_attr;
    TROVE_keyval_s key, val;

    ret = parse_args(argc, argv);
    if ( ret < 0 ) { 
	fprintf(stderr, "argument parsing failed.\n");
	return -1;
    }
	
    ret = trove_initialize(storage_space, 0, &method_name, 0);
    if (ret < 0) {
	fprintf(stderr, "initialize failed.\n");
	return -1;
    }
    ret = trove_collection_lookup(file_system, &coll_id, NULL, &op_id);
    if (ret < 0) {
	fprintf(stderr, "collection lookup failed.\n");
	return -1;
    }

    /* find parent directory name */
    strcpy(path_name, path_to_dir);
    for (i=strlen(path_name); i >=0; i--) {
	if (path_name[i] != '/') path_name[i]= '\0';
	else break;
    }
    dir_name = path_to_dir + strlen(path_name);
    printf("path is %s\n", path_name);
    printf("dir is %s\n", dir_name);

    /* find parent directory handle */
    ret = path_lookup(coll_id, path_name, &parent_handle);
    if (ret < 0) {
	return -1;
    }

    file_handle = requested_file_handle;

    /* create new dspace */
    ret = trove_dspace_create(coll_id,
			      &file_handle,
			      0xffffffff,
			      TROVE_TEST_DIR,
			      NULL,
			      NULL,
			      &op_id);
    if (ret < 0) return -1;

    while (ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
    if (ret < 0 ) {
	fprintf(stderr, "dspace create (for %s) failed.\n", dir_name);
	return -1;
    }

    /* set attributes of file */
    s_attr.fs_id  = coll_id; /* for now */
    s_attr.handle = file_handle;
    s_attr.type   = TROVE_TEST_DIR; /* shouldn't need to fill this one in. */
    s_attr.uid    = getuid();
    s_attr.gid    = getgid();
    s_attr.mode   = 0755;
    s_attr.ctime  = time(NULL);
    count = 1;

    ret = trove_dspace_setattr(coll_id, file_handle, &s_attr, NULL, &op_id);
    while (ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
    if (ret < 0) return -1;    /* add new file name/handle pair to parent directory */
    key.buffer = dir_name;
    key.buffer_sz = strlen(dir_name) + 1;
    val.buffer = &file_handle;
    val.buffer_sz = sizeof(file_handle);
    ret = trove_keyval_write(coll_id, parent_handle, &key, &val, 0, NULL, NULL, &op_id);
    while (ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
    if (ret < 0) {
	fprintf(stderr, "keyval write failed.\n");
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
    while (ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
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
		strncpy(path_to_dir, optarg, PATH_SIZE);
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
 * vim: ts=8 sw=4 noexpandtab
 */
