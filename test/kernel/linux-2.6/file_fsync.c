#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

int main(int argc, char **argv)	
{
    int ret = -1, fd = -1;
    int buf_size = 2*1024*1024;
    char* buffer = NULL;

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <filename>\n", argv[0]);
        return(-1);
    }

    buffer = (char *)malloc(buf_size);
    if (!buffer)
    {
        perror("malloc");
        return(-1);
    }

    printf("Using testfile %s\n", argv[1]);
    fd = open(argv[1], (O_CREAT | O_WRONLY), 0666);
    if (fd < 0)
    {
        perror("open");
        return(-1);
    }

    memset(buffer, 'Z', buf_size);

    ret = write(fd, buffer, buf_size);
    if (ret < 0)
    {
        fprintf(stderr, "errno is %x\n", errno);
        perror("write");
        return(-1);
    }

    fsync(fd);
    close(fd);

    return 0;
}
