/*
  this test is not for generating a consistent file -- it just tries
  to do a lot of I/O on the same file

  compile with:
  gcc threaded_write.c -lpthread -o threaded_write

  run like:
  ./threaded_write /mnt/pvfs2/testfile 40
*/
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <fcntl.h>

#define MAX_NUM_THREADS                  64
#define DATA_SIZE_PER_THREAD  (1024*1024*8)

typedef struct
{
    int fd;
    int size;
    int error_code;
    int index;
} threaded_write_info_t;

void *write_file(void *ptr)
{
    char *buf = NULL;
    threaded_write_info_t *info = (threaded_write_info_t *)ptr;
    struct iovec iov[2];

    if (info)
    {
        buf = (char *)malloc(info->size);
        if (buf)
        {
            memset(buf, (char)info->index, info->size);
            memset(&iov, 0, sizeof(struct iovec));

            iov[0].iov_base = buf;
            iov[0].iov_len = (info->size / 2);

            iov[1].iov_base = buf + iov[0].iov_len;
            iov[1].iov_len = (info->size / 2);

            fprintf(stderr, "thread %d writing data (%d bytes))\n",
                    info->index, info->size);

            info->error_code = writev(info->fd, iov, 2);
            free(buf);
        }
        fprintf(stderr, "thread %d returning\n", info->index);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    int fd = 0, i = 0, num_threads = 0;
    pthread_t threads[MAX_NUM_THREADS];
    threaded_write_info_t info[MAX_NUM_THREADS];

    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <out_file> <num_threads>\n", argv[0]);
        return 1;
    }

    num_threads = atoi(argv[2]);
    if ((num_threads < 1) || (num_threads > MAX_NUM_THREADS))
    {
        fprintf(stderr, "num_threads should be between 1 and %d\n",
                MAX_NUM_THREADS);
        return 1;
    }

    fd = open(argv[1], O_WRONLY|O_CREAT|O_TRUNC, 0666);
    if (fd == -1)
    {
        fprintf(stderr, "Failed to open file %s for writing\n", argv[1]);
        return 1;
    }

    for(i = 0; i < num_threads; i++)
    {
        memset(&info[i], 0, sizeof(threaded_write_info_t));

        info[i].fd = fd;
        info[i].size = DATA_SIZE_PER_THREAD;
        info[i].index = i;

        if (pthread_create(&threads[i], NULL, write_file, (void *)&info[i]))
        {
            fprintf(stderr, "Failed to spawn thread %d\n", i);
            break;
        }
    }

    for(i = 0; i < num_threads; i++)
    {
        pthread_join(threads[i], NULL);
        if (info[i].error_code == -1)
        {
            fprintf(stderr, "Thread %d failed to write\n", i);
        }
    }

    close(fd);
    return 0;
}
