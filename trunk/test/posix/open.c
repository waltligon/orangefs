/*
 * Simple test program to demonstrate
 * the functionality of the open
 * system call!
 */
#include <stdio.h>
#ifdef WIN32
#include <Windows.h>
#include <io.h>
#else
#include <sys/time.h>
#endif
#include <string.h>
#include <errno.h>
#ifndef WIN32
#include <unistd.h>
#endif
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#ifndef WIN32
#include <linux/unistd.h>
#include "sha1.h"
#include "crc32c.h"
#endif
#include "mpi.h"

#ifdef WIN32
#define open(f, o, p)    _open(f, o, p)
#define close(f)         _close(f)
#define unlink(f)        _unlink(f)
#endif

#if 0
static inline double msec_diff(double *end, double *begin)
{
	return (*end - *begin);
}
#endif

#ifdef WIN32
static double Wtime(void)
{
    LARGE_INTEGER counter;

    QueryPerformanceCounter(&counter);

    return (double) counter.QuadPart;
}
#else
static double Wtime(void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return((double)t.tv_sec * 1e03 + (double)(t.tv_usec) * 1e-03);
}
#endif

#if 0
struct file_handle_generic {
	/* Filled by VFS */
	int32_t   fhg_magic; /* magic number */
	int32_t   fhg_fsid; /* file system identifier */
	int32_t   fhg_flags; 	/* flags associated with the file object */
	int32_t   fhg_crc_csum; /* crc32c check sum of the blob */
	unsigned char fhg_hmac_sha1[24]; /* hmac-sha1 message authentication code */
};
#endif

int main(int argc, char *argv[])
{
	int c, fd, a;
	int niters = 10, do_unlink = 0, do_create = 0;
	char opt[] = "f:n:cu", *fname = NULL;
	double begin, end, tdiff = 0.0, max_diff;
	int open_flags = 0;
	int i, rank, np;
#ifdef WIN32
        LARGE_INTEGER freq;
        HANDLE hFile;
#endif

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &np);
#ifdef WIN32
        /* just use one option for now */
        fname = argv[1];
        open_flags |= O_CREAT;
        do_create = 1;
#else
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
#endif
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
#ifdef WIN32
                        hFile = CreateFile(fname,
                                           GENERIC_READ | GENERIC_WRITE,
                                           FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                           NULL,
                                           CREATE_ALWAYS,
                                           FILE_ATTRIBUTE_NORMAL,
                                           NULL);
                        if (hFile == INVALID_HANDLE_VALUE)
                        {
                            fprintf(stderr, "CreateFile: %d\n", GetLastError());
                            MPI_Finalize();
                            exit(1);
                        }
#else
			fd = open(fname, open_flags, 0775);
			if (fd < 0) {
				perror("open(2) error:");
				MPI_Finalize();
				exit(1);
			}
#endif
			end = Wtime();
			tdiff += (end - begin);
#ifdef WIN32
                        CloseHandle(hFile);
#else
			close(fd);
#endif
			if (rank == 0 && i < (niters - 1))
				unlink(fname);
	}
        
#ifdef WIN32
        QueryPerformanceFrequency(&freq);
        tdiff = tdiff / (double) freq.QuadPart * 1000.0;
#endif
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
