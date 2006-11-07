/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#undef _FILE_OFFSET_BITS
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <linux/unistd.h>

#if defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__)
#define __NR_readx  321
#define __NR_writex 322
#elif defined (x86_64) || defined (__x86_64__)
#define __NR_readx  280
#define __NR_writex 281
#endif


struct xtvec {
    off_t xtv_off;
    size_t xtv_len;
};

/* the _syscallXX apprroach is not portable. instead, we'll use syscall and
 * sadly forego any type checking.  For reference, here are the prototypes for
 * the system calls 
static ssize_t readx(unsigned long, const struct iovec *, unsigned long, const struct xtvec *, unsigned long);
static ssize_t writex(unsigned long, const struct iovec *, unsigned long, const struct xtvec *, unsigned long);
*/

#ifndef Ld
#define Ld(x) (x)
#endif

#ifndef FNAME
#define FNAME "/tmp/test.out"
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX_REGIONS
#define MAX_REGIONS 2
#endif

#ifndef MAX_OFFSET
#define MAX_OFFSET 32768
#endif

#ifndef MAX_BUF_SIZE
#define MAX_BUF_SIZE 32768
#endif

static int max_regions = MAX_REGIONS;
static off_t max_offset  = MAX_OFFSET;

static void usage(int argc, char** argv);
static double Wtime(void);
typedef size_t cm_count_t;
typedef off_t cm_offset_t;
typedef size_t cm_size_t;

typedef struct {
    cm_count_t	 cf_valid_count;
    cm_offset_t *cf_valid_start;
    cm_size_t   *cf_valid_size;
    void	*wr_buffer;
    void	*rd_buffer;
    int		 buf_size;
} cm_frame_t;

static int construct_iovec(struct iovec **iov, cm_count_t valid_count, 
	cm_size_t *valid_size, cm_offset_t *valid_offset, char *buf)
{
    int i;
    *iov = (struct iovec *) malloc(valid_count * sizeof(struct iovec));

    for (i = 0; i < valid_count; i++) {
	(*iov)[i].iov_base = buf + valid_offset[i];
	(*iov)[i].iov_len  = valid_size[i];
    }
    return 0;
}

static int construct_xtvec(struct xtvec **xtv, cm_count_t valid_count, 
	cm_size_t *valid_size, cm_offset_t *valid_offset, cm_offset_t base_offset)
{
    int i;
    *xtv = (struct xtvec *) malloc(valid_count * sizeof(struct xtvec));

    for (i = 0; i < valid_count; i++) {
	(*xtv)[i].xtv_off = base_offset + valid_offset[i];
	(*xtv)[i].xtv_len = valid_size[i];
    }
    return 0;
}

static size_t fillup_buffer(cm_frame_t *frame)
{
    size_t c, size = sizeof(double), should_be = 0, chunk_size = 0;

    srand(time(NULL));
    if (frame->buf_size < 0 || frame->buf_size % size != 0)
    {
	fprintf(stderr, "buffer size [%ld] must be a multiple of %ld\n",
		(long) frame->buf_size, (long) size);
	return -1;
    }
    frame->wr_buffer = (char *) calloc(frame->buf_size, sizeof(char));
    assert(frame->wr_buffer);
    frame->rd_buffer = (char *) calloc(frame->buf_size, sizeof(char));
    assert(frame->rd_buffer);

    for (c = 0; c < frame->buf_size / size; c++)
    {
	*((int *) frame->wr_buffer + c) = c;
    }
    frame->cf_valid_count = (rand() % max_regions) + 1;
    frame->cf_valid_start = (cm_offset_t *) calloc(sizeof(cm_offset_t), frame->cf_valid_count);
    frame->cf_valid_size = (cm_size_t *) calloc(sizeof(cm_size_t), frame->cf_valid_count);
    assert(frame->cf_valid_start && frame->cf_valid_size);
    chunk_size = frame->buf_size / frame->cf_valid_count;

    printf("Buffer size is %d bytes\n", frame->buf_size);
    printf("Generating %ld valid regions\n", (long) frame->cf_valid_count);
    for (c = 0; c < frame->cf_valid_count; c++)
    {
	int tmp_start;

	tmp_start = rand() % chunk_size;
	frame->cf_valid_start[c] = c * chunk_size + tmp_start;
	frame->cf_valid_size[c] = (rand() % (chunk_size - tmp_start)) + 1;
	assert(frame->cf_valid_start[c] + frame->cf_valid_size[c] <= frame->buf_size);
	
	printf("(%ld): valid_start: %ld, valid_size: %ld\n",(long) c, (long) frame->cf_valid_start[c],
		(long) frame->cf_valid_size[c]);
	
	should_be += frame->cf_valid_size[c];
    }
    printf("readx/writex should xfer %ld bytes\n", (long) should_be);
    return should_be;
}

int main(int argc, char **argv)
{
    ssize_t ret = -1;
    cm_offset_t offset;
    char *fname = NULL;
    double time1, time2;
    int c;
    size_t should_be = 0;
    cm_frame_t    frame;
    int fh = -1;
    struct iovec *rdiov = NULL, *wriov = NULL;
    struct xtvec *xtv = NULL;
    size_t file_size;
    struct stat statbuf;

    frame.cf_valid_count = 0;
    frame.cf_valid_start = NULL;
    frame.cf_valid_size = NULL;
    frame.wr_buffer   = NULL;
    frame.rd_buffer = NULL;
    frame.buf_size = MAX_BUF_SIZE;

    while ((c = getopt(argc, argv, "o:r:b:f:h")) != EOF)
    {
	switch (c)
	{
	    case 'o':
		max_offset = atoi(optarg);
		break;
	    case 'r':
		max_regions = atoi(optarg);
		break;
	    case 'f':
		fname = optarg;
		break;
	    case 'b':
		frame.buf_size = atoi(optarg);
		break;
	    case 'h':
	    default:
		usage(argc, argv);
		exit(1);
	}
    }
    if (fname == NULL)
    {
	fname = FNAME;
    }
    if ((should_be = fillup_buffer(&frame)) < 0)
    {
	usage(argc, argv);
	exit(1);
    }
    offset = rand() % max_offset;

    /* start writing to the file */

    ret = open(fname, O_RDWR | O_CREAT | O_TRUNC, 0700);
    if (ret < 0)
    {
	printf("open [1] failed [%ld]\n", (long) ret);
	ret = -1;
	goto main_out;
    }
    fh = ret;
    printf("Writing at offset %ld\n", (long) offset);
	
    construct_iovec(&rdiov, frame.cf_valid_count, frame.cf_valid_size, frame.cf_valid_start,
	    frame.rd_buffer);
    construct_iovec(&wriov, frame.cf_valid_count, frame.cf_valid_size, frame.cf_valid_start,
	    frame.wr_buffer);
    construct_xtvec(&xtv, frame.cf_valid_count, frame.cf_valid_size, frame.cf_valid_start, offset);
    if (wriov == NULL || rdiov == NULL || xtv == NULL)
    {
	printf("could not construct iovec or xtvec\n");
	ret = -1;
	goto main_out;
    }
    time1 = Wtime();
    /* write out the data */
    ret = syscall(__NR_writex, fh, wriov, frame.cf_valid_count, xtv, 
	    frame.cf_valid_count);
    time2 = Wtime();
    if (ret < 0)
    {
	printf("writex failed [%ld]\n", (unsigned long) ret);
	ret = -1;
	goto main_out;
    }
    /* sanity check */
    if(should_be != ret)
    {
	fprintf(stderr, "Error: short write!\n");
	fprintf(stderr, "Tried to write %ld bytes at offset %ld.\n", 
	    (long) should_be, (long) offset);
	fprintf(stderr, "Only got %ld bytes.\n",
		(long) ret);
	ret = -1;
	goto main_out;
    }
    /* print some statistics */
    printf("********************************************************\n");
    printf("Writex Test Statistics:\n");
    printf("********************************************************\n");
    printf("Bytes written: %ld\n", (long) Ld(ret));
    printf("Elapsed time: %f seconds\n", (time2-time1));
    printf("Bandwidth: %f MB/second\n",
	(((double)ret)/((double)(1024*1024))/(time2-time1)));
    printf("********************************************************\n\n");

    /* Get the size of the file as well */
    ret = fstat(fh, &statbuf);
    if (ret < 0)
    {
	printf("fstat failed [%ld]\n", (long) ret);
	ret = -1;
	goto main_out;
    }
    file_size = statbuf.st_size;
    printf("Measured file size is %ld\n",(long)file_size);

    /* try to reason the size of the file */
    if (file_size != (offset + frame.cf_valid_start[frame.cf_valid_count - 1]
		+ frame.cf_valid_size[frame.cf_valid_count - 1]))
    {
	printf("file size %ld is not equal to (%ld + %ld + %ld)\n",
		(long) file_size, (long) offset, (long) frame.cf_valid_start[frame.cf_valid_count - 1],
		(long) frame.cf_valid_size[frame.cf_valid_count - 1]);
	ret = -1;
	goto main_out;
    }
    /* close the file */
    ret = close(fh);
    if (ret < 0)
    {
	printf("close [1] failed [%ld]\n", (long) ret);
	ret = -1;
	goto main_out;
    }

    /* reopen the file and check the contents */
    ret = open(fname, O_RDONLY);
    if (ret < 0)
    {
	printf("open [2] failed [%ld]\n", (long) ret);
	ret = -1;
	goto main_out;
    }
    fh = ret;
    /* now read it back from the file and make sure we have the correct data */
    time1 = Wtime();
    ret = syscall(__NR_readx, fh, rdiov, frame.cf_valid_count, xtv, 
	    frame.cf_valid_count);
    time2 = Wtime();
    if(ret < 0)
    {
	printf("readx failed [%ld]\n", (long) ret);
	ret = -1;
	goto main_out;
    }
    /* sanity check */
    if(should_be != ret)
    {
	fprintf(stderr, "Error: short reads!\n");
	fprintf(stderr, "Tried to read %ld bytes at offset %ld.\n", 
	    (long) should_be, (long) offset);
	fprintf(stderr, "Only got %ld bytes.\n", (long) ret);
	ret = -1;
	goto main_out;
    }
    /* print some statistics */
    printf("\n********************************************************\n");
    printf("Readx Test Statistics:\n");
    printf("********************************************************\n");
    printf("Bytes read: %ld\n", (long) Ld(ret));
    printf("Elapsed time: %f seconds\n", (time2-time1));
    printf("Bandwidth: %f MB/second\n",
	(((double)ret)/((double)(1024*1024))/(time2-time1)));
    printf("********************************************************\n");

    ret = 0;
    /* now compare the relevant portions of what was written and what was read back */
    for (c = 0; c < frame.cf_valid_count; c++)
    {
	if (memcmp((char *)frame.rd_buffer + frame.cf_valid_start[c],
		    (char *)frame.wr_buffer + frame.cf_valid_start[c],
		    frame.cf_valid_size[c]))
	{
	    fprintf(stderr, "(%d) -> Read buffer did not match with write buffer from [%ld upto %ld bytes]\n",
		    c, (long) frame.cf_valid_start[c], (long) frame.cf_valid_size[c]);
	    ret = -1;
	}
    }
    if (ret == 0)
    {
	fprintf(stdout, "Test passed!\n");
    }
    else
    {
	fprintf(stdout, "Test failed!\n");
    }

main_out:
    close(fh);

    if (rdiov)
	free(rdiov);
    if (wriov)
	free(wriov);
    if (xtv)
	free(xtv);
    if(frame.rd_buffer)
	free(frame.rd_buffer);
    if(frame.wr_buffer)
	free(frame.wr_buffer);
    if(frame.cf_valid_start)
	free(frame.cf_valid_start);
    if(frame.cf_valid_size)
	free(frame.cf_valid_size);

    return(ret);
}

static void usage(int argc, char** argv)
{
    fprintf(stderr, "Usage: %s -f [filename]\n", argv[0]);
    fprintf(stderr, "	       -b [max. buffer size] default %d bytes.\n", MAX_BUF_SIZE);
    fprintf(stderr, "	       -o [max. file offset] default %d bytes.\n", MAX_OFFSET);
    fprintf(stderr, "	       -r [max. valid regions] default %d regions\n", MAX_REGIONS);
    return;
}

static double Wtime(void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return((double)t.tv_sec + (double)(t.tv_usec) / 1000000);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

