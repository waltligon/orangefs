/*
  this test is for trying to open a file in various modes to see what
  error codes we get.  the ordering of the opens, removes, and the
  expected errno values can change this test drastically, so make sure
  you check those if you modify this and the test is failing in an
  unexpected way

  compile with:
  gcc file_open.c -o file_open

  run like:
  ./file_open /mnt/pvfs/testfile

  for comparison, try running it on another file system such as
  ext2/ext3
*/
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

typedef struct
{
    int flags;
    int mode;
    char flag_description[64];
    int remove_file_indicator;
    int expected_errno;
} open_info_t;

#define REMOVE_FILE_INDICATOR { 0, 0, "", 1, ENOENT }

static open_info_t oinfo[] =
{
    REMOVE_FILE_INDICATOR,

    { (O_RDONLY | O_APPEND), 0666,
      "O_RDONLY | O_APPEND", 0, ENOENT },

    { (O_RDONLY | O_TRUNC), 0666,
      "O_RDONLY | O_TRUNC", 0, ENOENT },

    { (O_CREAT | O_WRONLY), 0666,
      "O_CREAT | O_WRONLY", 0, 0 },

    REMOVE_FILE_INDICATOR,

    { (O_CREAT | O_RDONLY), 0666,
      "O_CREAT | O_RDONLY", 0, 0 },

    REMOVE_FILE_INDICATOR,

    { (O_SYNC | O_RDONLY), 0666,
      "O_SYNC | O_RDONLY", 0, ENOENT },

    { (O_RDONLY | O_APPEND), 0666,
      "O_RDONLY | O_APPEND", 0, ENOENT },

    { (O_RDONLY | O_TRUNC), 0666,
      "O_RDONLY | O_TRUNC", 0, ENOENT },

    { (O_CREAT | O_RDONLY | O_TRUNC), 0666,
      "O_CREAT | O_RDONLY | O_TRUNC", 0, 0 },

    { (O_SYNC | O_RDONLY), 0666,
      "O_SYNC | O_RDONLY", 0, 0 },

    { (O_RDONLY | O_APPEND), 0666,
      "O_RDONLY | O_APPEND", 0, 0 },

    { (O_RDONLY | O_TRUNC), 0666,
      "O_RDONLY | O_TRUNC", 0, 0 },

    { (O_CREAT | O_RDWR), 0666,
      "O_CREAT | O_RDWR", 0, 0 },

    { (O_CREAT | O_WRONLY | O_TRUNC), 0666,
      "O_CREAT | O_WRONLY | O_TRUNC", 0, 0 },

    REMOVE_FILE_INDICATOR,

    { (O_CREAT | O_WRONLY | O_EXCL), 0666,
      "O_CREAT | O_WRONLY | O_EXCL", 0, 0 },

    REMOVE_FILE_INDICATOR,

    { (O_CREAT | O_WRONLY | O_TRUNC), 0666,
      "O_CREAT | O_WRONLY | O_TRUNC", 0, 0 },

    { (O_CREAT | O_WRONLY | O_EXCL), 0666,
      "O_CREAT | O_WRONLY | O_EXCL", 0, EEXIST },

    { (O_CREAT | O_WRONLY), 0666,
      "O_CREAT | O_WRONLY", 0, 0 },

    REMOVE_FILE_INDICATOR
};
#define NUM_OPEN_TESTS (sizeof(oinfo) / sizeof(open_info_t))


int main(int argc, char **argv)	
{
    int ret = -1, fd = -1, i = 0;

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <filename>\n", argv[0]);
        return -1;
    }

    printf("Using testfile %s\n", argv[1]);

    for(i = 0; i < NUM_OPEN_TESTS; i++)
    {
        if (oinfo[i].remove_file_indicator)
        {
            printf("[%d] removing test file %s ... ", i, argv[1]);
            if (unlink(argv[1]))
            {
                if (errno != oinfo[i].expected_errno)
                {
                    printf("FAILED\n");
                    fprintf(stderr, "[%d] *** unlink failure: %s\n", i,
                            strerror(errno));
                    continue;
                }
            }
            printf("OK\n");
            continue;
        }

        printf("[%d] opening file with flags (%s) ... ", i,
               oinfo[i].flag_description);

        fd = open(argv[1], oinfo[i].flags, oinfo[i].mode);
        if (fd < 0)
        {
            if (errno != oinfo[i].expected_errno)
            {
                printf("FAILED\n");
                fprintf(stderr, "[%d] *** open failure: %s\n", i,
                        strerror(errno));
            }
            else
            {
                printf("OK\n  failed with expected errno: %s\n",
                       strerror(errno));
            }
        }
        else
        {
            printf("OK\n");

            if (close(fd))
            {
                fprintf(stderr, "[%d] *** close failure: %s\n", i,
                        strerror(errno));
            }
        }
    }
    return 0;
}
