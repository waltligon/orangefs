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
static int iterations = 10;

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
    char error_msg[512];
    char* buf;
    int ret;
    int current_deleter = 0;
    int delete_tries=0;
    unsigned int DELETE_MAX=10;
    int increase_sleep_time=0;

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

    int i;
    /* Parent does <iterations> number of deletes, then aborts*/
    if (mynod == 0)
    {
    	for (i=0; i < iterations; i++)
        {
            current_deleter++;
            current_deleter = current_deleter % nprocs;
            if(current_deleter == 0)
            {
                current_deleter ++;
            }
            sprintf(file, "%s/%d", opt_dir, current_deleter);
            delete_tries=0;
            increase_sleep_time=0;
            while (1)
            {
               delete_tries++;
               sleep(SLEEP_TIME+increase_sleep_time);
               fprintf(stderr, "Deleting: %s: try #%d sleep time increase(%d)\n", file, delete_tries,increase_sleep_time);
               increase_sleep_time += 30;

               ret = unlink(file);
               if(ret < 0)
               {
                  if (delete_tries > DELETE_MAX || errno != ENOENT)
                  {
                     sprintf(error_msg,"unlink %s",file);
            	     perror(error_msg);
                     MPI_Abort(MPI_COMM_WORLD, 1);
                     break;
                  }
                  else 
                  {
                    /* retry the unlink.  It is possible that the other process
                     * on this same machine hasn't been given the opportunity to 
                     * create its file.  So, lets try it again!
                     */
                    continue;
                  }
               }
               break;
            }/*end while*/
        }/*end for*/

    	fprintf(stderr, "Successfully completed %d of %d iterations.\n", i,iterations);
    	fprintf(stderr, "Calling MPI_Abort and returning success (0). Ugly, but effective.\n");
    	MPI_Abort(MPI_COMM_WORLD, 0);


    }
    /* Children will read and recreate files forever until parent aborts*/
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
                sprintf(error_msg,"open %s",file);
            	perror(error_msg);
                MPI_Abort(MPI_COMM_WORLD, 1);
            }
            else
            {
            	fprintf(stderr, "Opened file %s: node %d.\n",file,mynod);
            }

            while(1)
            {
                ret = write(fd, buf, 1024*1024);
                if(ret < 0)
                {
                    sprintf(error_msg,"write %s ... continuing ...",file);
                	perror(error_msg);
                    sleep(SLEEP_TIME/2);
                    break;
                }
                /*
                else
				{
					fprintf(stderr, "node %d wrote %s successfully\n", mynod,file);
				}
				*/
                ret = read(fd, buf, 1024*1024);
                if(ret < 0)
                {
                    sprintf(error_msg,"read %s",file);
                	perror(error_msg);
                    sleep(SLEEP_TIME/2);
                    break;
                }
                /*
                else
                {
                	fprintf(stderr, "node %d read %s successfully\n", mynod,file);
                }
                */
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

    while ((c = getopt(argc, argv, "i:d:h")) != EOF)
    {
        switch (c)
        {
        case 'd':      /* dir */
            strncpy(opt_dir, optarg, 255);
            break;
        case 'i':
        	iterations = atoi(optarg);
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
    printf(" -i       number of iterations\n");
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
