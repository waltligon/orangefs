#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char **argv)	
{
	int ret = -1;
	int fd = -1;
	int buf_size = 8*1024*1024;
	char* buffer = NULL;
	off_t pos = 0;

	if(argc != 2)
	{
		fprintf(stderr, "usage: %s <filename>\n", argv[0]);
		return(-1);
	}

	buffer = (char*)malloc(buf_size);
	if(!buffer)
	{
		perror("malloc");
		return(-1);
	}

	fd = open(argv[1], O_RDONLY);
	if(fd < 0)
	{
		perror("open");
		return(-1);
	}

	ret = read(fd, buffer, buf_size);
	if(ret < 0)
	{
		perror("read");
		return(-1);
	};

	pos = lseek(fd, 0, SEEK_CUR);
	printf("lseek returned: %d\n", (int)pos);

	close(fd);

	return(0);
}
