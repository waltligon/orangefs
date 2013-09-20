#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <string.h>

#ifndef FNAME
#define FNAME "./test.out"
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
static int max_offset  = MAX_OFFSET;

typedef int32_t cm_count_t;
typedef int32_t cm_offset_t;
typedef int32_t cm_size_t;

typedef struct {
    cm_count_t	 cf_valid_count;
    cm_offset_t *cf_valid_start;
    cm_size_t   *cf_valid_size;
    void	*wr_buffer;
    void	*rd_buffer;
    int		 buf_size;
} cm_frame_t;

static int fillup_buffer(cm_frame_t *frame, int *nr_segs, struct iovec **wr_iovec, struct iovec **rd_iovec)
{
    int c, size = sizeof(double), should_be = 0, chunk_size = 0;
	 struct iovec *wr_vector = NULL, *rd_vector = NULL;

    srand(time(NULL));
    if (frame->buf_size < 0 || frame->buf_size % size != 0)
    {
			fprintf(stderr, "buffer size [%d] must be a multiple of %d\n",
				frame->buf_size, size);
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
	 *nr_segs = frame->cf_valid_count;
	 wr_vector = (struct iovec *) malloc(*nr_segs * sizeof(struct iovec));
	 *wr_iovec = wr_vector;
	 rd_vector = (struct iovec *) malloc(*nr_segs * sizeof(struct iovec));
	 *rd_iovec = rd_vector;

    frame->cf_valid_start = (cm_offset_t *) calloc(sizeof(cm_offset_t), frame->cf_valid_count);
    frame->cf_valid_size = (cm_size_t *) calloc(sizeof(cm_size_t), frame->cf_valid_count);

    assert(frame->cf_valid_start && frame->cf_valid_size);
    chunk_size = frame->buf_size / frame->cf_valid_count;

    printf("Buffer size is %d bytes\n", frame->buf_size);
    printf("Generating %d valid regions\n", frame->cf_valid_count);
    for (c = 0; c < frame->cf_valid_count; c++)
    {
			int tmp_start;

			tmp_start = rand() % chunk_size;
			frame->cf_valid_start[c] = c * chunk_size + tmp_start;
			frame->cf_valid_size[c] = (rand() % (chunk_size - tmp_start)) + 1;
			rd_vector[c].iov_base = (char *) frame->rd_buffer + frame->cf_valid_start[c];
			rd_vector[c].iov_len  = frame->cf_valid_size[c];
			wr_vector[c].iov_base = (char *) frame->wr_buffer + frame->cf_valid_start[c];
			wr_vector[c].iov_len  = frame->cf_valid_size[c];
			assert(frame->cf_valid_start[c] + frame->cf_valid_size[c] <= frame->buf_size);
			
			should_be += frame->cf_valid_size[c];
    }
	 for (c = 0; c < frame->cf_valid_count; c++)
	 {
			printf("(%d): Writing %p to %p [%d start %ld bytes]\n", 
					c, wr_vector[c].iov_base, 
					(char *) wr_vector[c].iov_base + wr_vector[c].iov_len, 
					frame->cf_valid_start[c], (long) wr_vector[c].iov_len);
	 }
	 for (c = 0; c < frame->cf_valid_count; c++)
	 {
			printf("(%d): Reading %p to %p [%d start %ld bytes]\n", 
					c, rd_vector[c].iov_base, 
					(char *) rd_vector[c].iov_base + rd_vector[c].iov_len, 
					frame->cf_valid_start[c], (long) rd_vector[c].iov_len);
	 }
    printf("writev and readv should write %d bytes\n", should_be);
    return should_be;
}

int main(int argc, char *argv[])
{
	int fd;
	char *fname = NULL;
	int ret = -1;
	loff_t offset;
	int c, should_be = 0;
	cm_frame_t    frame;
	struct iovec *wr_iov = NULL, *rd_iov = NULL;
	int iov_len = 0;

	frame.cf_valid_count = 0;
	frame.cf_valid_start = NULL;
	frame.cf_valid_size = NULL;
	frame.wr_buffer   = NULL;
	frame.rd_buffer = NULL;
	frame.buf_size = MAX_BUF_SIZE;

	while ((c = getopt(argc, argv, "b:o:r:f:")) != EOF)
	{
		switch (c)
		{
			case 'b':
				frame.buf_size = atoi(optarg);
				break;
			case 'f':
				fname = optarg;
				break;
			case 'o':
				max_offset = atoi(optarg);
				break;
			case 'r':
				max_regions = atoi(optarg);
				break;
			default:
				fprintf(stderr, "Usage: %s -f <fname> -o <offset> -r <regions> -b <buffer size>\n", argv[0]);
				exit(1);
		}
	}
	if (fname == NULL)
		fname = FNAME;
   if ((should_be = fillup_buffer(&frame, &iov_len, &wr_iov, &rd_iov)) < 0)
   {
		exit(1);
	}
   offset = rand() % max_offset;
	printf("Writing at offset %Ld\n", (long long) offset);
	fd = open(fname, O_TRUNC | O_RDWR | O_CREAT, 0700);
	if (fd < 0)
	{
		perror("open:");
		exit(1);
	}
	if (lseek(fd, offset, SEEK_SET) < 0)
	{
		perror("lseek:");
		close(fd);
		exit(1);
	}
	if ((ret = writev(fd, wr_iov, iov_len)) < 0)
	{
		perror("writev:");
		close(fd);
		exit(1);
	}
	printf("Writev returned %d\n", ret);
	close(fd);

	fd = open(fname, O_RDONLY);
	if (fd < 0)
	{
		perror("open:");
		exit(1);
	}
	if (lseek(fd, offset, SEEK_SET) < 0)
	{
		perror("lseek:");
		close(fd);
		exit(1);
	}
	if ((ret = readv(fd, rd_iov, iov_len)) < 0) 
	{
		perror("readv:");
		close(fd);
		exit(1);
	}
	printf("Readv returned %d\n", ret);
	close(fd);
	ret = 0;
	/* now compare the relevant portions of what was written and what was read back */
	for (c = 0; c < frame.cf_valid_count; c++)
	{
		if (memcmp((char *)frame.rd_buffer + frame.cf_valid_start[c],
				 (char *)frame.wr_buffer + frame.cf_valid_start[c],
				 frame.cf_valid_size[c]))
		{
			 fprintf(stderr, "(%d) -> Read buffer did not match with write buffer from [%d upto %d bytes]\n",
				 c, frame.cf_valid_start[c], frame.cf_valid_size[c]);
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
	return 0;
}

