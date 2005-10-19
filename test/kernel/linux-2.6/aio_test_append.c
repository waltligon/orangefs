/*
 * Adapted this test program from 
 * http://developer.osdl.org/daniel/AIO/TESTS/aiodio_append.c
 * Needs libaio-devel RPM installed.
 * and compile as 
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

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

#define NUM_AIO 16
#define AIO_SIZE 64*1024

/*
 * append to the end of a file using AIO DIRECT.
 */
void aiodio_append(char *filename)
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
		memset(bufptr, 0, AIO_SIZE);
		io_prep_pwrite(&iocb_array[i], fd, bufptr, AIO_SIZE, offset);
		iocbs[i] = &iocb_array[i];
		offset += AIO_SIZE;
	}

	/*
	 * Start the 1st NUM_AIO requests
	 */
	if ((w = io_submit(myctx, NUM_AIO, iocbs)) < 0) {
		fprintf(stderr, "io_submit write returned %d\n", w);
	}

	/*
	 * As AIO requests finish, keep issuing more AIOs.
	 */
	for (; i < 1000; i++) {
		int n;
		struct iocb *iocbp;

		n = io_getevents(myctx, 1, 1, &event, 0);
		iocbp = event.obj;

		io_prep_pwrite(iocbp, fd, iocbp->u.c.buf, AIO_SIZE, offset);
		offset += AIO_SIZE;
		if ((w = io_submit(myctx, 1, &iocbp)) < 0) {
			fprintf(stderr, "write %d returned %d\n", i, w);
		}
	}
}

int main(int argc, char **argv)
{
	int pid[NUM_CHILDREN];
	int num_children = 1;
	int i, error_exit = 0, c;
	char *filename = "a";

	while ((c = getopt(argc, argv, "f:"))!= EOF)
	{
		switch (c)
		{
			case 'f': filename = optarg; break;
			default: exit(1);
		}
	}
	for (i = 0; i < num_children; i++) {
		if ((pid[i] = fork()) == 0) {
			/* child */
			return read_eof(filename);
		} else if (pid[i] < 0) {
			/* error */
			perror("fork error");
			break;
		} else {
			/* Parent */
			continue;
		}
	}

	/*
	 * Parent appends to end of file using direct i/o
	 */

	aiodio_append(filename);

	for (i = 0; i < num_children; i++) {
		kill(pid[i], SIGTERM);
	}
	error_exit = 0;
	for (i = 0; i < num_children; i++)
	{
		int status;
		pid_t p;

		p = waitpid(pid[i], &status, 0);
		if (p < 0)
			perror("waitpid:");
		else {
			if (WIFEXITED(status) && WEXITSTATUS(status) == 1) {
				error_exit = 1;
			}
		}
	}
	if (error_exit)
	{
		fprintf(stderr, "AIO Append Test Failed!\n");
		return 1;
	}
	else 
	{
		fprintf(stderr, "AIO Append Test Passed!\n");
		return 0;
	}
}
