/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "gossip.h"
#include "pint-dev.h"

int main(int argc, char **argv)	
{
    int ret = 1;
    int outcount = 0;
    struct PINT_dev_unexp_info unexp_array[2];
    char buf1[] = "Hello ";
    char buf2[] = "World.";
    void* buffer_list[2];
    int size_list[2];

    ret = PINT_dev_initialize("/dev/pvfs2-req", 0);
    if(ret < 0)
    {
	PVFS_perror("PINT_dev_initialize", ret);
	return(-1);
    }

    /* try reading out a single unexpected message */
    ret = PINT_dev_test_unexpected(1, &outcount, unexp_array, 5);
    if(ret != 1 || outcount != 1)
    {
	fprintf(stderr, "Error: PINT_dev_testunexpected().\n");
	return(-1);
    }

    printf("Got message: size: %d, tag: %d, payload: %s\n", 
	unexp_array[0].size, (int)unexp_array[0].tag, 
	(char*)unexp_array[0].buffer);

    PINT_dev_release_unexpected(unexp_array);

    /* try reading out two messages */
    ret = PINT_dev_test_unexpected(2, &outcount, unexp_array, 5);
    if(ret != 1 || outcount != 2)
    {
	fprintf(stderr, "Error: PINT_dev_testunexpected().\n");
	return(-1);
    }

    printf("Got message: size: %d, tag: %d, payload: %s\n", 
	unexp_array[0].size, (int)unexp_array[0].tag, 
	(char*)unexp_array[0].buffer);
    printf("Got message: size: %d, tag: %d, payload: %s\n", 
	unexp_array[1].size, (int)unexp_array[0].tag, 
	(char*)unexp_array[1].buffer);

    PINT_dev_release_unexpected(&unexp_array[0]);
    PINT_dev_release_unexpected(&unexp_array[1]);

    /* try writing a message */
    ret = PINT_dev_write(buf1, (strlen(buf1) + 1), PINT_DEV_EXT_ALLOC,
	7);
    if(ret != 0)
    {
	fprintf(stderr, "Error: PINT_dev_write().\n");
	return(-1);
    }

    /* try writing a list message */
    buffer_list[0] = buf1;
    buffer_list[1] = buf2;
    size_list[0] = strlen(buf1);
    size_list[1] = strlen(buf1) + 1;

    ret = PINT_dev_write_list(buffer_list, size_list, 2, (strlen(buf1) +
	strlen(buf2) + 1), PINT_DEV_EXT_ALLOC, 7);
    if(ret != 0)
    {
	fprintf(stderr, "Error: PINT_dev_write_list().\n");
	return(-1);
    }

    PINT_dev_finalize();

    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
