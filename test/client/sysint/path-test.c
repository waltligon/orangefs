/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include "pint-sysint.h"
#include "str-utils.h"

int main(int argc, char* argv[])
{
	char path[] = "/test/some/dir/misc/stuff/file.txt";
	char segment[PVFS_SEGMENT_MAX] = {0};
	int num_segments = 0,i, ret=0;

	printf("path = %s\n",path);

        num_segments = PINT_string_count_segments(path);

	for(i= 0; i < num_segments; i++)
	{
		ret = PINT_get_path_element(path,i,segment,PVFS_SEGMENT_MAX);
		if (ret < 0)
		{
			printf("errcode = %d\n",ret);
			continue;
		}
		printf("segment = %s\n",segment);
	}

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
