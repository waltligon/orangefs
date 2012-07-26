/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */

/* N-1 processes will alternate reading and writing to independent files.
 * The remaining process will delete one of the other files every 5 seconds.
 *
 * All file interaction is done through posix calls; no mpi-io.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <mpi.h>
#include <errno.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>


/* DEFAULT VALUES FOR OPTIONS */
static char opt_dir[256] = "";

/* function prototypes */
static int parse_args(
    int argc,
    char **argv);
static void usage(
    void);

/* global vars */
static int mynod = 0;
static int nprocs = 1;

#define SLEEP_TIME 5

int main(
    int argc,
    char **argv)
{
    int fd;
    char file[256];
    char* buf;
    int ret;
    int current_deleter = 0;

    /* startup MPI and determine the rank of this process */
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &mynod);

    if (nprocs < 2)
    {
        fprintf(stderr,
                "Error: this program requires at least two processes.\n");
        exit(1);
    }

    /* parse the command line arguments */
    parse_args(argc, argv);

    if (mynod == 0)
    {
        while(1)
        {
            current_deleter++;
            current_deleter = current_deleter % nprocs;
            if(current_deleter == 0)
                current_deleter ++;

            sleep(SLEEP_TIME);
            sprintf(file, "%s/%d", opt_dir, current_deleter);
            fprintf(stderr, "Deleting: %s\n", file);

            ret = unlink(file);
            if(ret < 0)
            {
                perror("unlink");
                MPI_Abort(MPI_COMM_WORLD, 1);
            }
        }
    }
    else
    {
        buf = malloc(1024*1024);
        if(!buf)
        {
            perror("malloc");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        sprintf(file, "%s/%d", opt_dir, mynod);

        while(1)
        {
            fd = open(file, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR);
            if(fd < 0)
            {
                perror("open");
                MPI_Abort(MPI_COMM_WORLD, 1);
            }

            while(1)
            {
                ret = write(fd, buf, 1024*1024);
                if(ret < 0)
                {
                    perror("write");
                    fprintf(stderr, "... continuing ...");
                    sleep(SLEEP_TIME/2);
                    break;
                }
                ret = read(fd, buf, 1024*1024);
                if(ret < 0)
                {
                    perror("read");
                    fprintf(stderr, "... continuing ...");
                    sleep(SLEEP_TIME/2);
                    break;
                }
            }
        }
    }

    MPI_Finalize();
    return (0);
}

static int parse_args(
    int argc,
    char **argv)
{
    int c;

    while ((c = getopt(argc, argv, "d:h")) != EOF)
    {
        switch (c)
        {
        case 'd':      /* dir */
            strncpy(opt_dir, optarg, 255);
            break;
        case 'h':
            if (mynod == 0)
                usage();
            exit(0);
        case '?':      /* unknown */
            if (mynod == 0)
                usage();
            exit(1);
        default:
            break;
        }
    }
    return (0);
}

static void usage(
    void)
{
    printf("Usage: mpi-active-delete [<OPTIONS>...]\n");
    printf("\n<OPTIONS> is one of\n");
    printf(" -d       directory to place test files in\n");
    printf(" -h       print this help\n");
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
