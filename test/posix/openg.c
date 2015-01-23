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
/* the _syscallXX apprroach is not portable. instead, we'll use syscall and
 * sadly forego any type checking.  For reference, here are the prototypes for
 * the system calls 
static long openfh(const void *, size_t);
static long openg(const char *, void *, size_t *, int, int);
*/

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
	size_t len = 0;
	void *ptr = NULL;
	struct stat sbuf;
	char opt[] = "f:m:", *fname = NULL;
	double begin, end;
	enum muck_options_t muck_options = DONT_MUCK;

	while ((c = getopt(argc, argv, opt)) != EOF) {
		switch (c) {
			case 'f':
				fname = optarg;
				break;
			case 'm':
				muck_options = atoi(optarg);
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
		exit(1);
	}
	len = MAX_LENGTH;
	ptr = (void *) calloc(len, sizeof(char));
	if (ptr == NULL)
	{
		perror("calloc failed:");
		exit(1);
	}
	begin = Wtime();
	err = syscall(__NR_openg, fname, ptr, &len, O_RDONLY, 0775);
	if (err < 0) {
		perror("openg(2) error:");
		exit(1);
	}
	end = Wtime();
	printf("openg on %s yielded length %ld [Time %g msec]\n",
				fname, (unsigned long) len, msec_diff(&end, &begin));
	muck_with_buffer(ptr, len, muck_options);
	begin = Wtime();
	fd = syscall(__NR_openfh, ptr, len);
	if (fd < 0) {
		fprintf(stderr, "openfh failed: %s\n", strerror(errno));
		free(ptr);
		exit(1);
	}
	end = Wtime();
	printf("openfh returned %d [Time %g msec]\n", fd, msec_diff(&end, &begin));
	fstat(fd, &sbuf);
	printf("stat indicates file size %ld\n", (unsigned long) sbuf.st_size);
	if (sbuf.st_size > 0)
		sha1_file_digest(fd); 
	close(fd);
	free(ptr);
	return 0;
}


