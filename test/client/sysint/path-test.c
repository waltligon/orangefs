
#include <stdio.h>
#include <stdlib.h>
#include <pint-sysint.h>

/*int get_path_element(char *path, char** segment, int element)*/

int main(int argc, char* argv[])
{
	char* path = NULL;
	char *segment = NULL;
	int num_segments = 10,i, ret=0;

	gossip_enable_stderr();
	gossip_set_debug_mask(1,CLIENT_DEBUG);

	path = malloc(41);

	strcpy(path,"/home/fshorte/porn/mp3s/crap/foobar.txt");

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
	free(path);

	gossip_disable();

	return 0;
}
