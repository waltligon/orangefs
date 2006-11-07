/*
 * Simple test program to demonstrate
 * the functionality of the openg/openfh
 * system call using sockets!
 */
#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <linux/unistd.h>
#include "sockio.h"
#include "sha1.h"
#include "crc32c.h"

#define TEST_PORT 4567

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

/* the _syscallXX apprroach is not portable. instead, we'll use syscall and
 * sadly forego any type checking.  For reference, here are the prototypes for
 * the system calls 
static long openfh(const void *, size_t);
static long openg(const char *, void *, size_t *, int, int);
*/

static inline double usec_diff(double *end, double *begin)
{
	return (*end - *begin);
}

static double Wtime(void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return ((double)t.tv_sec * 1e06 + (double)(t.tv_usec));
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

static void wait_for_handle_buffer(char *ptr, int *len)
{
	int sockfd = new_sock(), max = sockfd + 1;
	int len1;
	fd_set rfd;
	if (sockfd < 0) {
		perror("new_sock: failed");
		exit(1);
	}
	if (bind_sock(sockfd, TEST_PORT) < 0) {
		perror("bind_sock: failed");
		close(sockfd);
		exit(1);
	}
	if (listen(sockfd, 5) < 0) {
		perror("listen: failed");
		close(sockfd);
		exit(1);
	}
	FD_ZERO(&rfd);
	FD_SET(sockfd, &rfd);
	if (select(max, &rfd, NULL, NULL, NULL) < 0) {
		perror("select:");
		close(sockfd);
		exit(1);
	}
	if (FD_ISSET(sockfd, &rfd)) {
		struct sockaddr_in sanew;
		socklen_t salen = sizeof(sanew);

		int new_fd = accept(sockfd, (struct sockaddr *) &sanew, &salen);
		if (new_fd < 0) {
			perror("accept:");
			close(sockfd);
			exit(1);
		}
		if (brecv(new_fd, &len1, sizeof(len1)) < 0) {
			perror("brecv1 failed:");
			close(new_fd);
			close(sockfd);
			exit(1);
		}
		*len = ntohl(len1);
		if (brecv(new_fd, ptr, *len) < 0) {
			perror("brecv2 failed:");
			close(new_fd);
			close(sockfd);
			exit(1);
		}
		close(new_fd);
		close(sockfd);
	}
}

static void send_handle_buffer(char *server, void *ptr, int len)
{
	int sockfd = new_sock(), len1;
	if (sockfd < 0) {
		perror("new_sock: failed");
		exit(1);
	}
	if (connect_sock(sockfd, server, TEST_PORT) < 0) {
		perror("connect: failed");
		close(sockfd);
		exit(1);
	}
	len1 = htonl(len);
	if (bsend(sockfd, &len1, sizeof(len1)) < 0) {
		perror("bsend1: failed");
		close(sockfd);
		exit(1);
	}
	if (bsend(sockfd, ptr, len) < 0) {
		perror("bsend2: failed");
		close(sockfd);
		exit(1);
	}
	close(sockfd);
}

int main(int argc, char *argv[])
{
	int c, fd, err, am_client = -1;
	size_t len = 128;
	char ptr[128];
	struct stat sbuf;
	char *server = NULL;
	char opt[] = "f:m:sch:", *fname = NULL;
	double begin, end;
	enum muck_options_t muck_options = DONT_MUCK;

#ifndef __NR_openg 
	printf("This kernel does not support the openg() extension\n");
	exit(-1);
#endif
#ifndef __NR_openfh
	printf("This kernel does not support the openfh() extension\n");
	exit(-1);
#endif

	while ((c = getopt(argc, argv, opt)) != EOF) {
		switch (c) {
			case 's':
				am_client = 0;
				break;
			case 'c':
				am_client = 1;
				break;
			case 'f':
				fname = optarg;
				break;
			case 'm':
				muck_options = atoi(optarg);
				break;
			case 'h':
				server = optarg;
				break;
			case '?':
			default:
				fprintf(stderr, "Invalid arguments\n");
				exit(1);
		}
	}
	if (am_client < 0) 
	{
		fprintf(stderr, "Usage: %s -f <fname> "
				" -m {muck options 0,1,2} -s {server} -c {client} -h <server name>\n", argv[0]);
		exit(1);
	}
	if (am_client == 1 && (server == NULL || fname == NULL)) {
		fprintf(stderr, "Usage: %s -f <fname> "
				" -m {muck options 0,1,2} -s {server} -c {client} -h <server name>\n", argv[0]);
		exit(1);
	}
	if (muck_options != DONT_MUCK && muck_options != DUMB_MUCK
			&& muck_options != SMART_MUCK)
	{
		fprintf(stderr, "Usage: %s -f <fname> "
				" -m {muck options 0,1,2} -s {server} -c {client} -h <server name>\n", argv[0]);
		exit(1);
	}
	/* if I am the server, wait until I get a packet */
	if (am_client == 0) {
		int new_len = 0;
		wait_for_handle_buffer(ptr, (int *)&new_len);
		len = new_len;
		printf("Got length %ld\n", (unsigned long) len);
	}
	/* if I am the client, openg the file and send a packet */
	else {
		begin = Wtime();
		err = syscall(__NR_openg, fname, ptr, &len, O_RDONLY, 0775);
		end = Wtime();
		if (err < 0) {
			perror("openg error:");
			exit(1);
		}
		printf("openg on %s yielded %ld [Time %g usec]\n",
					fname, (unsigned long) len, usec_diff(&end, &begin));
		send_handle_buffer(server, ptr, len);
	}
	/* muck with buffer if need be */
	muck_with_buffer(ptr, len, muck_options);
	begin = Wtime();
	fd = syscall(__NR_openfh, ptr, len);
	end = Wtime();
	if (fd < 0) {
		fprintf(stderr, "openfh failed: %s\n", strerror(errno));
		free(ptr);
		exit(1);
	}
	printf("openfh returned %d [Time %g usec]\n", fd, usec_diff(&end, &begin));
	fstat(fd, &sbuf);
	printf("stat indicates file size %ld\n", (unsigned long) sbuf.st_size);
	sha1_file_digest(fd);
	close(fd);
	return 0;
}


