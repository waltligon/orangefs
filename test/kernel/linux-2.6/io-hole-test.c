#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#define MAX_BUF_LEN 5


int main(int argc, char **argv)
{
    int fd = -1;
    char buf[MAX_BUF_LEN] = {0};
    ssize_t n_written = 0, n_read = 0;
    off_t pos = 0;

    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <output filename>\n", argv[0]);
        return 1;
    }

    memset(buf, 'A', MAX_BUF_LEN);

    fd = open(argv[1], (O_RDWR|O_CREAT|O_TRUNC), 0666);
    if (fd == -1)
    {
        fprintf(stderr, "Failed to open file %s\n", argv[1]);
        return 1;
    }

    n_written = write(fd, buf, MAX_BUF_LEN);
    if (n_written != MAX_BUF_LEN)
    {
        fprintf(stderr, "Failed to write first buffer\n");
        return 1;
    }
    fprintf(stderr, "Wrote %d bytes\n", MAX_BUF_LEN);

    pos = lseek(fd, 100000, SEEK_CUR);
    if (pos == (off_t)-1)
    {
        fprintf(stderr, "Failed to seek\n");
        return 1;
    }
    fprintf(stderr, "lseek() returned %ld\n", pos);

    n_written = write(fd, buf, MAX_BUF_LEN);
    if (n_written != MAX_BUF_LEN)
    {
        fprintf(stderr, "Failed to write first buffer\n");
        return 1;
    }
    fprintf(stderr, "Wrote %d bytes\n", MAX_BUF_LEN);

    pos = lseek(fd, 10, SEEK_SET);
    if (pos == (off_t)-1)
    {
        fprintf(stderr, "Failed to seek\n");
        return 1;
    }
    fprintf(stderr, "lseek() returned %ld\n", pos);

    n_read = read(fd, buf, MAX_BUF_LEN);
    if (n_read != MAX_BUF_LEN)
    {
        fprintf(stderr, "Failed to read %d bytes at offset %d\n",
                (int)MAX_BUF_LEN, (int)pos);
        return 1;
    }
    fprintf(stderr, "Read %d bytes\n", MAX_BUF_LEN);

    close(fd);
    return 0;
}
