/*
 * Simple test program to demonstrate
 * the functionality of the openg/openfh
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

#define MAX_LENGTH 128
_syscall2(long, openfh, const void *, uhandle, size_t, handle_len);
_syscall5(long, openg, const char *, pathname, void *, uhandle, size_t *, uhandle_len, int, flags, int, mode);

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
	int c, fd, err, a;
	size_t len = 0;
	void *ptr = NULL;
	struct stat sbuf;
	char opt[] = "f:m:c", *fname = NULL;
	double begin, end, tdiff, max_diff;
	enum muck_options_t muck_options = DONT_MUCK;
	int open_flags = 0;
	int rank, np;

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
				open_flags |= O_CREAT;
				break;
			case '?':
			default:
				fprintf(stderr, "Invalid arguments\n");
				exit(1);
		}
	}
	if (fname == NULL)
	{
		fprintf(stderr, "Usage: %s -f <fname> -m {muck options 0,1,2}\n",
				argv[0]);
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
	len = MAX_LENGTH;
	ptr = (void *) calloc(len, sizeof(char));
	if (ptr == NULL)
	{
		perror("calloc failed:");
		MPI_Finalize();
		exit(1);
	}

	a = MPI_Barrier(MPI_COMM_WORLD);

	begin = Wtime();
	open_flags |= O_RDONLY;
	err = open(fname, open_flags, 0775);
	if (err < 0) {
		perror("open(2) error:");
		MPI_Finalize();
		exit(1);
	}
	end = Wtime();
	printf("open on %s yielded length %ld [Time %g msec]\n",
				fname, (unsigned long) len, msec_diff(&end, &begin));
/*	muck_with_buffer(ptr, len, muck_options);
	fstat(fd, &sbuf);
	printf("stat indicates file size %ld\n", (unsigned long) sbuf.st_size);
*/
	tdiff = end - begin;
	MPI_Allreduce(&tdiff, &max_diff, 1, 
			MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
	if(rank == 0)
	{
	    printf("Total time for open: [Time %g msec]\n", max_diff);
	}
		   
/*	sha1_file_digest(fd); */
	close(fd);
	free(ptr);

	MPI_Finalize();
	return 0;
}
