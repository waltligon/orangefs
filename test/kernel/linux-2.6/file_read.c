#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char **argv)	
{
	int ret = -1;
	int fd = -1;
	char buffer[256];

	if(argc != 2)
	{
		fprintf(stderr, "usage: %s <filename>\n", argv[0]);
		return(-1);
	}

	fd = open(argv[1], O_RDONLY);
	if(fd < 0)
	{
		perror("open");
		return(-1);
	}

	ret = read(fd, buffer, 256);
	printf("read returned: %d\n", ret);

	close(fd);

	return(0);
}
