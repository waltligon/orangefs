#if 0
/* Magic self-executing C source code.  Run "sh aio_test_correctness.c"
*  set -ex
*  gcc -g -Wall -O2 $0 -o aio_test_correctness
*  exit 0
*/
#endif

/* Adapted from 
 * http://developer.osdl.org/daniel/AIO/TESTS/aiodio_append.c
 */

#define _GNU_SOURCE

#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <libaio.h>

#define NUM_CHILDREN 8



int check_zero(char *buf, int size)
{
	int *iptr;

	iptr = (int *)buf;

	while (size > 0) {
		if (*iptr++ != 0) {
			fprintf(stderr, "non zero buffer at buf[%d]\n",
				((char *)iptr) - buf);
			return 1;
		}
		size -= 4;
	}
	return 0;	/* all zeros */
}

int read_eof(char *filename)
{
	int fd;
	int i;
	int r;
	char buf[4096];

	while ((fd = open(filename, O_RDONLY)) < 0) {
		sleep(1);	/* wait for file to be created */
	}

	for (i = 0 ; i < 1000000; i++) {
		off_t offset;
		int bufoff;

		offset = lseek(fd, SEEK_END, 0);
		r = read(fd, buf, 4096);
		if (r > 0) {
			if ((bufoff = check_zero(buf, 4096))) {
				fprintf(stderr, "non-zero read at offset %ld\n",
					offset + bufoff);
				exit(1);
			}
		}
	}
	return 0;
}

#define NUM_AIO 18
#define AIO_SIZE 64*1024

/*
 * write to the different portions of a file using AIO.
 */
void aiodio_write(char *filename, void **all_bufs)
{
	int fd;
	void *bufptr;
	int i;
	int w;
	struct iocb iocb_array[NUM_AIO];
	struct iocb *iocbs[NUM_AIO];
	off_t offset = 0;
	io_context_t myctx;
	struct io_event event;

	fd = open(filename, O_WRONLY|O_CREAT, 0666);
	if (fd < 0) {
		perror("cannot create file");
		return;
	}

	memset(&myctx, 0, sizeof(myctx));
	io_queue_init(NUM_AIO, &myctx);

	for (i = 0; i < NUM_AIO; i++ ) {
		if (posix_memalign(&bufptr, 4096, AIO_SIZE)) {
			perror("cannot malloc aligned memory");
			return;
		}
		all_bufs[i] = bufptr;
		memset(bufptr, rand() % 256, AIO_SIZE);
		io_prep_pwrite(&iocb_array[i], fd, bufptr, AIO_SIZE, offset);
		iocbs[i] = &iocb_array[i];
		offset += AIO_SIZE;
	}

	/*
	 * Start the NUM_AIO requests
	 */
	if ((w = io_submit(myctx, NUM_AIO, iocbs)) < 0) {
		fprintf(stderr, "io_submit write returned %d\n", w);
	}
	/* Wait for them to finish */
	for (i = 0; i < NUM_AIO; i++)
	{
		int n;
		n = io_getevents(myctx, 1, 1, &event, 0);
	}
	close(fd);
	return;
}

/*
 * read from different parts of the file using AIO.
 */
void aiodio_read(char *filename, void **all_bufs)
{
	int fd;
	void *bufptr;
	int i;
	int w;
	struct iocb iocb_array[NUM_AIO];
	struct iocb *iocbs[NUM_AIO];
	off_t offset = 0;
	io_context_t myctx;
	struct io_event event;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror("cannot open file for reading!");
		return;
	}

	memset(&myctx, 0, sizeof(myctx));
	io_queue_init(NUM_AIO, &myctx);

	for (i = 0; i < NUM_AIO; i++ ) {
		if (posix_memalign(&bufptr, 4096, AIO_SIZE)) {
			perror("cannot malloc aligned memory");
			return;
		}
		all_bufs[i] = bufptr;
		memset(bufptr, 0, AIO_SIZE);
		io_prep_pread(&iocb_array[i], fd, bufptr, AIO_SIZE, offset);
		iocbs[i] = &iocb_array[i];
		offset += AIO_SIZE;
	}

	/*
	 * Start the NUM_AIO requests
	 */
	if ((w = io_submit(myctx, NUM_AIO, iocbs)) < 0) {
		fprintf(stderr, "io_submit read returned %d\n", w);
	}

	/* Wait for them to finish */
	for (i = 0; i < NUM_AIO; i++)
	{
		int n;
		n = io_getevents(myctx, 1, 1, &event, 0);
	}
	close(fd);
	return;
}

int main(int argc, char **argv)
{
	int c, i;
	char *filename = "a";
	void *rbuf[NUM_AIO] = {NULL, };
	void *wbuf[NUM_AIO] = {NULL, };

	while ((c = getopt(argc, argv, "f:"))!= EOF)
	{
		switch (c)
		{
			case 'f': filename = optarg; break;
			default: exit(1);
		}
	}
	aiodio_write(filename, wbuf);
	aiodio_read(filename, rbuf);
	c = 0;
	for (i = 0; i < NUM_AIO; i++)
	{
		if (memcmp(rbuf[i], wbuf[i], AIO_SIZE)) {
			printf("Buffer %d did not match\n", i);
			c = 1;
		}
	}
	if (c == 0) {
		printf("AIO Correctness Test Passed!\n");
		return 0;
	}
	else  {
		printf("AIO Correctness Test Failed!\n");
		return 1;
	}
}
