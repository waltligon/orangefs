/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <pint-sysint.h>

int main(int argc, char* argv[])
{
	char* path = "/test/some/dir/misc/stuff/file.txt";
	char *segment = NULL;
	int num_segments = 10,i, ret=0;

	gossip_enable_stderr();
	gossip_set_debug_mask(1,CLIENT_DEBUG);

	printf("path = %s\n",path);
#if 0
	for(i= 0; i < num_segments; i++)
	{
		ret = get_next_path(path,&segment,25);
		if (ret < 0)
		{
			printf("errcode = %d\n",ret);
			continue;
		}
		printf("segment = %s\n",segment);
		free(segment);
	}
#endif
	for(i= 0; i < num_segments; i++)
	{
		ret = get_path_element(path,&segment,i);
		if (ret < 0)
		{
			printf("errcode = %d\n",ret);
			continue;
		}
		printf("segment = %s\n",segment);
		free(segment);
	}

	gossip_disable();

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
