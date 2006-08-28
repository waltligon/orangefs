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

_syscall2(long, openfh, const void *, uhandle, size_t, handle_len);
_syscall5(long, openg, const char *, pathname, void *, uhandle, size_t *, uhandle_len, int, flags, int, mode);

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

enum muck_options_t {
	DONT_MUCK = 0,
	DUMB_MUCK = 1,
	SMART_MUCK = 2,
};

struct file_handle_generic {
	/* Filled by VFS */
	int32_t   fhg_magic; /* magic number */
	int32_t   fhg_fsid; /* file system identifier */
	int32_t   fhg_flags; 	/* flags associated with the file object */
	int32_t   fhg_crc_csum; /* crc32c check sum of the blob */
	unsigned char fhg_hmac_sha1[24]; /* hmac-sha1 message authentication code */
};

static void muck_with_buffer(char *buffer, size_t len, 
		enum muck_options_t muck_options)
{
	if (muck_options == DONT_MUCK) {
		return;
	}
	else if (muck_options == DUMB_MUCK) {
		int pos = rand() % len;
		int cnt = rand() % (len - pos);
		memset(buffer + pos, 0, cnt);
	}
	else {
		struct file_handle_generic *fh = (struct file_handle_generic *) buffer;

		/* modify some fields and recalc crc32 */
		fh->fhg_flags = 1;
		fh->fhg_crc_csum = compute_check_sum((unsigned char *) buffer, 12);
		/* purpose is to show that HMACs will catch this kind of behavior */
	}
}

int main(int argc, char *argv[])
{
	int c, fd, err;
	int rank, np;
	size_t len;
	struct stat sbuf;
	char opt[] = "f:m:c", *fname = NULL;
	double begin_openg, end_openg, begin_openfh, end_openfh, max_end;
	enum muck_options_t muck_options = DONT_MUCK;
	struct hbuf hb;
	MPI_Datatype d;
	int openg_flags = 0;

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &np);

	while ((c = getopt(argc, argv, opt)) != EOF) {
		switch (c) {
			case 'f':
				fname = optarg;
				break;
			case 'm':
				muck_options = atoi(optarg);
				break;
			case 'c':
				openg_flags |= O_CREAT;
				break;
			case '?':
			default:
				fprintf(stderr, "Invalid arguments\n");
				MPI_Finalize();
				exit(1);
		}
	}
	if (fname == NULL)
	{
		fprintf(stderr, "Usage: %s -f <fname> -m {muck options 0,1,2} -c\n",
				argv[0]);
		MPI_Finalize();
		exit(1);
	}
	if (muck_options != DONT_MUCK && muck_options != DUMB_MUCK
			&& muck_options != SMART_MUCK)
	{
		fprintf(stderr, "Usage: %s -f <fname> -m {muck options 0,1,2}\n",
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
	/* Rank 0 does the openg */
	if (rank == 0)
	{
		begin_openg = Wtime();
		openg_flags |= O_RDONLY;
		err = openg(fname, (void *) hb.handle, (size_t *) &hb.handle_length, openg_flags, 0);
		if (err < 0) {
			perror("openg error:");
			MPI_Finalize();
			exit(1);
		}
		end_openg = Wtime();
		printf("Rank %d openg on %s yielded length %ld [Time %g msec]\n",
				rank, fname, (unsigned long) hb.handle_length, msec_diff(&end_openg, &begin_openg));
	}

	/* Broadcast the handle buffer to everyone */
	if ((err = MPI_Bcast(&hb, 1, d, 0, MPI_COMM_WORLD)) != MPI_SUCCESS) {
		char str[256];
		len = 256;
		MPI_Error_string(err, str, (int *) &len);
		fprintf(stderr, "MPI_Bcast failed: %s\n", str);
		MPI_Finalize();
		exit(1);
	}
	/* muck with buffers if need be */
/*	muck_with_buffer(hb.handle, hb.handle_length, muck_options); */
	begin_openfh = Wtime();
	fd = openfh((void *) hb.handle, (size_t) hb.handle_length);
	if (fd < 0) {
		fprintf(stderr, "openfh on rank %d failed: %s\n",
				rank, strerror(errno));
		MPI_Finalize();
		exit(1);
	}
	end_openfh = Wtime();
	printf("Rank %d:  openfh returned fd: %d [Time %g msec]\n",
			rank, fd, msec_diff(&end_openfh, &begin_openfh));
/*	fstat(fd, &sbuf);
	printf("Rank %d: stat indicates file size %lu\n", rank, (unsigned long) sbuf.st_size);
	sha1_file_digest(fd); */
	close(fd);
	MPI_Allreduce(&end_openfh, &max_end, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
	if(rank == 0)
	{
	    printf("Total time for openg/openfh: [Time %g msec]\n",
		   msec_diff(&max_end, &begin_openg));
	}

	MPI_Finalize();
	return 0;
}


