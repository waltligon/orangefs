/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>

#include <trove.h>

#include "trove-test.h"

char storage_space[SSPACE_SIZE] = "/tmp/trove-test-space";
char file_system[FS_SIZE] = "fs-foo";

TROVE_handle requested_file_handle = 9999;
struct teststruct {
    int a;
    long b;
    int size;
    char *string;
};

int main(int argc, char ** argv )
{

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
			      TROVE_SYNC /* flags */,
			      NULL,
			      &op_id);
    while (ret == 0) ret = trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
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
	
	
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
