/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#undef _FILE_OFFSET_BITS

#include <stdlib.h>
#include <stdio.h>
#include <sys/uio.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <linux/unistd.h>

#if defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__)
#define __NR_readx  321
#define __NR_writex 322
#elif defined (x86_64) || defined (__x86_64__)
#define __NR_readx  280
#define __NR_writex 281
#endif
#define BUFSIZE 65536

static int bufsize = BUFSIZE;

struct xtvec {
	off_t xtv_off;
	size_t xtv_len;
};


/* the _syscallXX apprroach is not portable. instead, we'll use syscall and
 * sadly forego any type checking.  For reference, here are the prototypes for
 * the system calls       
static ssize_t readx(unsigned long fd,
		const struct iovec * iov, unsigned long iovlen, 
		const struct xtvec * xtv, unsigned long xtvlen);
static ssize_t writex(unsigned long fd, 
		const struct iovec * iov, unsigned long iovlen,
		const struct xtvec * xtv, unsigned long xtvlen);
*/

#ifndef min
#define min(a, b) (a) < (b) ? (a) : (b)
#endif

#ifndef max
#define max(a, b) (a) > (b) ? (a) : (b)
#endif

#ifndef Ld
#define Ld(x) (x)
#endif

#ifndef FNAME
#define FNAME "/tmp/test.out"
#endif

static int mem_ct = 25, str_ct = 25;
static double Wtime(void);
static char *fname = FNAME;

static void parse(int argc, char *argv[])
{
	int c;
	while ((c = getopt(argc, argv, "f:b:m:s:")) != EOF) {
		switch (c) {
			case 'f':
				fname = optarg;
				break;
			case 'b':
				bufsize = atoi(optarg);
				break;
			case 'm':
				mem_ct = atoi(optarg);
				break;
			case 's':
				str_ct = atoi(optarg);
				break;
			default:
				fprintf(stderr, "Usage: %s -f <filename> -m <mem count max> -s <stream count max> -b <buffer size>\n", argv[0]);
				exit(1);
		}
	}
	if (mem_ct <= 0 || str_ct <= 0 || bufsize <= 0)
	{
		fprintf(stderr, "Usage: %s -f <filename> -m <mem count max> -s <stream count max> -b <buffer size>\n", argv[0]);
		exit(1);
	}
	return;
}

static ssize_t do_writex(struct iovec *iov, unsigned long ivlen, struct xtvec *xtv, unsigned long xtvlen)
{
	int fd;
	ssize_t ret;
	double time1, time2;
	fd = open(fname, O_RDWR | O_CREAT | O_TRUNC, 0700);
	time1 = Wtime();
	ret = syscall(__NR_writex, fd, iov, ivlen, xtv, xtvlen);
	time2 = Wtime();
	if (ret < 0)
	{
		perror("writex:");
		exit(1);
	}
	close(fd);
	printf("writex: %ld bytes in %g sec: %g MB/sec\n", (long) ret, (time2 - time1), ret * 1e-06/(time2 - time1));
	return ret;
}

static ssize_t do_readx(struct iovec *iov, unsigned long ivlen, struct xtvec *xtv, unsigned long xtvlen)
{
	int fd;
	ssize_t ret;
	double time1, time2;
	fd = open(fname, O_RDONLY);
	time1 = Wtime();
	ret = syscall(__NR_readx, fd, iov, ivlen, xtv, xtvlen);
	time2 = Wtime();
	if (ret < 0)
	{
		perror("readx:");
		exit(1);
	}
	close(fd);
	printf("readx: %ld bytes in %g sec: %g MB/sec\n", (long) ret, (time2 - time1), ret * 1e-06/(time2 - time1));
	return ret;
}


static void fillup_buffers(char ***ptr, int nr_segs, int fill)
{
	int i;
	*ptr = (char **) malloc(nr_segs * sizeof(char *));
	for (i = 0; i < nr_segs; i++) 
	{
		char *p;
		p = (*ptr)[i] = (char *) calloc(1, bufsize);
		if (fill)
		{
			int j;
			for (j = 0; j < bufsize; j++) {
				*((char *) p + j) = 'a' + j % 26;
			}
		}
	}
	return;
}

static void free_buffers(char **ptr, int nr_segs)
{
	int i;
	for (i = 0; i < nr_segs; i++) {
		if (ptr[i])
			free(ptr[i]);
	}
	free(ptr);
}

static int compare_buffers(struct iovec *iov1, struct iovec *iov2, int count)
{
	int i, j;
	for (i = 0; i < count; i++) 
	{
		if (iov1[i].iov_len != iov2[i].iov_len)
		{
			fprintf(stderr, "length mismatch\n");
			break;
		}
		for (j = 0; j < iov1[i].iov_len; j++)
		{
			if (*((char *)iov1[i].iov_base + j) != *((char *) iov2[i].iov_base + j))
			{
				fprintf(stderr, "index %d, char %d in streamsize %ld\n",
						i, j, (long) iov1[i].iov_len);
				break;
			}
		}
		if (j != iov1[i].iov_len)
			break;
		/*
		if (memcmp(iov1[i].iov_base, iov2[i].iov_base, iov1[i].iov_len) == 0)
			continue;
		break;
		*/
	}
	if (i != count)
	{
		fprintf(stderr, "Tests failed\n");
		return -1;
	}
	else {
		printf("Tests passed!\n");
		return 0;
	}
}

int main(int argc, char *argv[])
{
	struct iovec *wvec, *rvec;
	struct xtvec *xc;
	unsigned long i, nr_segs = 0;
	unsigned long xtnr_segs = 0;
	unsigned long total = 0, xt_total = 0, mem_total = 0;
	char **wrptr = NULL, **rdptr = NULL;
	ssize_t total_written, total_read;

	srand(time(NULL));
	parse(argc, argv);

	nr_segs = mem_ct;  
	fillup_buffers(&wrptr, nr_segs, 1);
	fillup_buffers(&rdptr, nr_segs, 0);
	wvec = (struct iovec *) malloc(nr_segs * sizeof(struct iovec));
	rvec = (struct iovec *) malloc(nr_segs * sizeof(struct iovec));
	printf("Original iovec %ld\n", (long) sizeof(off_t));
	for (i = 0; i < nr_segs; i++)
	{
		wvec[i].iov_len  = rand() % bufsize + 1;
		wvec[i].iov_base = (char *) wrptr[i];
		rvec[i].iov_len = wvec[i].iov_len;
		rvec[i].iov_base = (char *) rdptr[i];
		total += wvec[i].iov_len;
		mem_total += wvec[i].iov_len;
		/* printf("%ld) <%p,%p> WRITE %ld bytes\n", i, wvec[i].iov_base, 
			(char *) wvec[i].iov_base + wvec[i].iov_len, (long) wvec[i].iov_len); */
	}
	xtnr_segs = str_ct;
	xc = (struct xtvec *) malloc(xtnr_segs * sizeof(struct xtvec));
	for (i = 0; i < xtnr_segs; i++)
	{
		off_t tmp = rand() % bufsize;
		xc[i].xtv_off = i * bufsize + tmp;
		if (i != xtnr_segs - 1 && total > 0)
		{
			if (bufsize - tmp > total)
				xc[i].xtv_len = total;
			else 
				xc[i].xtv_len = rand() % (bufsize - tmp) + 1;
		}
		else if (total > 0) {
			xc[i].xtv_len = max(total, 0);
		}
		else
			break;
		total -= xc[i].xtv_len;
		/* printf("%ld) <%ld> FOR %ld bytes\n", i, (long) xc[i].xtv_off, (long) xc[i].xtv_len); */
		xt_total += xc[i].xtv_len;
	}
	if (xt_total != mem_total)
	{
		fprintf(stderr, "mem_total (%ld) != xt_total (%ld)\n",
				(long) mem_total, (long) xt_total);
		exit(1);
	}
	xtnr_segs = i;
	mem_total = 0;
	total_written = do_writex(wvec, nr_segs, xc, xtnr_segs);
	for (i = 0; i < nr_segs; i++)
	{
		/* printf("%ld) <%p,%p> READ %ld bytes\n", i, rvec[i].iov_base, 
			(char *) rvec[i].iov_base + rvec[i].iov_len, (long) rvec[i].iov_len); */
		mem_total += rvec[i].iov_len;
	}
	if (xt_total != mem_total)
	{
		fprintf(stderr, "mem_total (%ld) != xt_total (%ld)\n",
				(long) mem_total, (long) xt_total);
		exit(1);
	}
	total_read = do_readx(rvec, nr_segs, xc, xtnr_segs);
	if (total_written != total_read)
	{
		fprintf(stderr, "total written (%ld) != total_read (%ld)\n", (long) total_written,
				(long) total_read);
		exit(1);
	}
	compare_buffers(rvec, wvec, nr_segs);
	free_buffers(rdptr, nr_segs);
	free_buffers(wrptr, nr_segs);
	return 0;
}

static double Wtime(void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return((double)t.tv_sec + (double)(t.tv_usec) / 1000000);
}

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 * End:
 *
 * vim: ts=4 sts=4 sw=4 noexpandtab
 */

