/*
 * Simple test program to demonstrate
 * the functionality of the open
 * system call!
 */
#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <linux/unistd.h>
#include "sha1.h"
#include "crc32c.h"
#include "mpi.h"

static inline double msec_diff(double *end, double *begin)
{
	return (*end - *begin);
}

static double Wtime(void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return((double)t.tv_sec * 1e03 + (double)(t.tv_usec) * 1e-03);
}

struct file_handle_generic {
	/* Filled by VFS */
	int32_t   fhg_magic; /* magic number */
	int32_t   fhg_fsid; /* file system identifier */
	int32_t   fhg_flags; 	/* flags associated with the file object */
	int32_t   fhg_crc_csum; /* crc32c check sum of the blob */
	unsigned char fhg_hmac_sha1[24]; /* hmac-sha1 message authentication code */
};

int main(int argc, char *argv[])
{
	int c, fd, a;
	int niters = 10, do_unlink = 0, do_create = 0;
	char opt[] = "f:n:cu", *fname = NULL;
	double begin, end, tdiff = 0.0, max_diff;
	int open_flags = 0;
	int i, rank, np;

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &np);

	while ((c = getopt(argc, argv, opt)) != EOF) {
		switch (c) {
			case 'u':
				do_unlink = 1;
				break;
			case 'n':
				niters = atoi(optarg);
				break;
			case 'f':
				fname = optarg;
				break;
			case 'c':
				open_flags |= O_CREAT;
				do_create = 1;
				break;
			case '?':
			default:
				fprintf(stderr, "Invalid arguments\n");
				fprintf(stderr, "Usage: %s -f <fname> -c {create} -u {unlink} -n <num iterations>\n", argv[0]);
				MPI_Finalize();
				exit(1);
		}
	}
	if (fname == NULL)
	{
		fprintf(stderr, "Usage: %s -f <fname> -c {create} -u {unlink} -n <num iterations>\n", argv[0]);
		MPI_Finalize();
		exit(1);
	}

	for (i = 0; i < niters; i++)
	{
			a = MPI_Barrier(MPI_COMM_WORLD);
			open_flags |= O_RDONLY;

			begin = Wtime();
			fd = open(fname, open_flags, 0775);
			if (fd < 0) {
				perror("open(2) error:");
				MPI_Finalize();
				exit(1);
			}
			end = Wtime();
			tdiff += (end - begin);
			close(fd);
			if (rank == 0 && i < (niters - 1))
				unlink(fname);
	}
	tdiff = tdiff / niters;
	MPI_Allreduce(&tdiff, &max_diff, 1, 
			MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
	if(rank == 0)
	{
	    printf("Total time for open: (create? %s) [Time %g msec niters %d]\n",
			    do_create ? "yes" : "no", max_diff, niters);
	    if (do_unlink)
		    unlink(fname);
	}

	MPI_Finalize();
	return 0;
}
