#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#include <trove.h>
#include <trove-test.h>


char storage_space[SSPACE_SIZE] = "/tmp/storage-space-foo";
char file_system[FS_SIZE] = "fs-foo";


TROVE_handle requested_file_handle = 9999;
struct teststruct {
	int a;
	long b;
	int size;
	char *string;
};

int path_lookup(TROVE_coll_id coll_id, char *path, TROVE_handle *out_handle_p);

int main(int argc, char ** argv ) {

	int ret, count;
	TROVE_size buffsz;
	TROVE_op_id op_id;
	TROVE_coll_id coll_id;
	TROVE_ds_state state;
	TROVE_handle file_handle, parent_handle;
	char *method_name;

	struct teststruct foo = { 8, 8, 0, NULL };
	struct teststruct bar;

	foo.string = strdup("monkey");
	foo.size = strlen(foo.string);

	ret = trove_initialize(storage_space, 0, &method_name, 0);
	if (ret < 0 ) {
		fprintf(stderr, "initialize failed\n");
		return -1;
	}

	ret = trove_collection_lookup(file_system, &coll_id, NULL, &op_id);
	if (ret < 0 ) {
		fprintf(stderr, "collection lookup failed");
		return -1;
	}

	ret = path_lookup(coll_id, "/", &parent_handle);
	if (ret < 0 ) {
		return -1;
	}

	file_handle = requested_file_handle;
	ret = trove_dspace_create(coll_id,
			&file_handle,
			0xffffffff,
			TROVE_TEST_BSTREAM,
			NULL,
			NULL,
			&op_id);
	while (ret == 0) trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
	if (ret < 0) {
		fprintf(stderr, "dspace create failed.\n");
		return -1;
	}

	/* not sure where to find the handle for bstream for the handle
	 * generator.  store some keys into the collection? */

	buffsz = sizeof(foo);
	ret = trove_bstream_write_at(coll_id, file_handle, 
			&foo, &buffsz,
			0, 0, NULL, NULL, &op_id);
	while ( ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
	if (ret < 0 ) {
		fprintf(stderr, "bstream write failed.\n");
		return -1;
	}
	ret = trove_bstream_write_at(coll_id, file_handle,
			foo.string, &buffsz,
			buffsz, 0, NULL, NULL, &op_id);
	while ( ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
	if (ret < 0 ) {
		fprintf(stderr, "bstream write failed.\n");
		return -1;
	}

	buffsz = sizeof(bar);
	ret = trove_bstream_read_at(coll_id, file_handle,
			&bar, &buffsz,
			0, 0, NULL, NULL, &op_id);
	while ( ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
	if (ret < 0 ) {
		fprintf(stderr, "bstream read failed.\n");
		return -1;
	}
	bar.string = malloc(bar.size + 1);
	ret = trove_bstream_read_at(coll_id, file_handle, 
			bar.string, &buffsz,
			buffsz, 0, NULL, NULL, &op_id);
	while ( ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
	if (ret < 0 ) {
		fprintf(stderr, "bstream write failed.\n");
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

