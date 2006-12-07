/*
 * Simple test program to demonstrate
 * the functionality of the openg/openfh
 * system call using MPI!
 */
#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <linux/unistd.h>
#include "mpi.h"
#include "sha1.h"
#include "crc32c.h"

#if defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__)
/* FIXME:
 * PLEASE CHANGE THIS SYSTEM
 * CALL NUMBER IN CASE YOUR
 * ARCHITECTURE IS NOT IA-32 
 * OR IF YOUR KERNEL SYSCALL NUMBERS
 * ARE DIFFERENT. YOU HAVE BEEN WARNED!!!!!
 */
#define __NR_openg  311
#define __NR_openfh 312
#elif defined (x86_64) || defined (__x86_64__)
#define __NR_openg  273 
#define __NR_openfh 274
#endif

static long openg(const char *pathname, void *uhandle, size_t *uhandle_len,
                  int flags, int mode)
{
    return syscall(__NR_openg, pathname, uhandle, uhandle_len, flags, mode);
}

static long openfh(const void *uhandle, size_t handle_len)
{
    return syscall(__NR_openfh, uhandle, handle_len);
}

#define MAX_LENGTH 128

struct hbuf {
	char  	handle[MAX_LENGTH];
	size_t   handle_length;
};

static MPI_Datatype build_data_type(void)
{
	MPI_Datatype d, handle_type;
	MPI_Aint indices[3];
	int  blen[3], types[3];
	struct hbuf hb;

	if (MPI_Type_contiguous(MAX_LENGTH, MPI_CHAR, &handle_type) != MPI_SUCCESS) {
		fprintf(stderr, "Could not create handle datatype\n");
		return (MPI_Datatype)0;
	}
	blen[0] = 1; indices[0] = 0; types[0] = handle_type;
	blen[1] = 1; indices[1] = (unsigned long)&(hb.handle_length) - (unsigned long)(&hb); types[1] = MPI_INT;
	blen[2] = 1; indices[2] = sizeof(hb); types[2] = MPI_UB;

	if (MPI_Type_struct(3, blen, indices, types, &d) != MPI_SUCCESS) {
		fprintf(stderr,  "Could not create a struct datatype\n");
		return (MPI_Datatype)0;
	}
	if (MPI_Type_commit(&d) != MPI_SUCCESS) {
		fprintf(stderr, "Could not commit datatypes!\n");
		return (MPI_Datatype)0;
	}
	return d;
}

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
	int c, fd, err;
	int i, rank, np, do_unlink = 0, do_create = 0;
	char opt[] = "n:f:cu", *fname = NULL;
	double begin_openg, end_openg, begin_openfh, end_openfh, begin_total, end_total;
	double openg_total = 0.0, openfh_total = 0.0, time_total = 0.0;
	double openg_final, openfh_final, total_final;

	struct hbuf hb;
	MPI_Datatype d;
	int openg_flags = 0, niters = 10;

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &np);

	while ((c = getopt(argc, argv, opt)) != EOF) {
		switch (c) {
			case 'n':
				niters = atoi(optarg);
				break;
			case 'f':
				fname = optarg;
				break;
			case 'c':
				openg_flags |= O_CREAT;
				do_create = 1;
				break;
			case 'u':
				do_unlink = 1;
				break;
			case '?':
			default:
				fprintf(stderr, "Invalid arguments\n");
				fprintf(stderr, "Usage: %s -f <fname> -c -n <number of iterations>\n",
						argv[0]);
				MPI_Finalize();
				exit(1);
		}
	}
	if (fname == NULL)
	{
		fprintf(stderr, "Usage: %s -f <fname> -c -n <number of iterations>\n",
				argv[0]);
		MPI_Finalize();
		exit(1);
	}
	if ((d = build_data_type()) == (MPI_Datatype) 0) {
		fprintf(stderr, "Could not construct datatype\n");
		MPI_Finalize();
		exit(1);
	}
	hb.handle_length = MAX_LENGTH;
	for (i = 0; i < niters; i++)
	{
		MPI_Barrier(MPI_COMM_WORLD);
		begin_total = Wtime();
		begin_openg = end_openg = 0;
		/* Rank 0 does the openg */
		if (rank == 0)
		{
			openg_flags |= O_RDONLY;
			begin_openg = Wtime();
			err = openg(fname, (void *) hb.handle, (size_t *) &hb.handle_length, openg_flags, 0775);
			if (err < 0) {
				perror("openg error:");
				MPI_Finalize();
				exit(1);
			}
			end_openg = Wtime();
			openg_total += (end_openg - begin_openg);
		}

		/* Broadcast the handle buffer to everyone */
		if ((err = MPI_Bcast(&hb, 1, d, 0, MPI_COMM_WORLD)) != MPI_SUCCESS) {
			char str[256];
			int len = sizeof(str);

			MPI_Error_string(err, str, &len);
			fprintf(stderr, "MPI_Bcast failed: %s\n", str);
			MPI_Finalize();
			exit(1);
		}

		begin_openfh = Wtime();
		fd = openfh((void *) hb.handle, (size_t) hb.handle_length);
		if (fd < 0) {
			char h[256];
			gethostname(h, 256);
			fprintf(stderr, "openfh on rank %d (%s) failed: %s\n",
					rank, h, strerror(errno));
			MPI_Finalize();
			exit(1);
		}
		end_openfh = Wtime();
		end_total = Wtime();
		openfh_total += (end_openfh - begin_openfh);
		time_total += (end_total - begin_total);

		close(fd);
		if (rank == 0 && do_create && i < (niters - 1))
			unlink(fname);
	}
	/* Average of niterations */
	openg_total = openg_total / niters;
	openfh_total = openfh_total / niters;
	time_total = time_total / niters;

/*	printf("Rank %d  (openg %g, bcast %g, openfh %g, total_time %g\n",
			rank, openg_total, (time_total - (openfh_total + openg_total)),
				openfh_total, time_total); */

	openg_final = openg_total;
	MPI_Allreduce(&openfh_total, &openfh_final, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
	MPI_Allreduce(&time_total, &total_final, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);

	if(rank == 0)
	{
	    printf("Total time for openg/openfh: (create? %s) [Time ( %g %g %g ) %g msec niters %d]\n",
		   do_create ? "yes" : "no", 
		   openg_final, (total_final - (openg_final + openfh_final)),
		   openfh_final, total_final, niters);
	    if (do_unlink)
		    unlink(fname);
	}
	MPI_Finalize();
	return 0;
}


