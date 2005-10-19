#if 0
/* Magic self-executing C source code.  Run "sh aio_test_sparse.c"
*  set -ex
*  gcc -g -Wall -O2 $0 -o aio_test_sparse
*  exit 0
*/
#endif

/* Adapted from 
 * http://developer.osdl.org/daniel/AIO/TESTS/aiodio_sparse.c
 */


#define _GNU_SOURCE
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include <libaio.h>

#define NUM_CHILDREN 1000

int debug;

/*
 * aio_test_sparse - issue async writes to holes is a file while
 *	concurrently reading the file and checking that the read never reads
 *	uninitailized data.
 */

char *check_zero(char *buf, int size)
{
	char *p;

	p = buf;

	while (size > 0) {
		if (*buf != 0) {
			fprintf(stderr, "non zero buffer at buf[%d] => 0x%02x,%02x,%02x,%02x\n",
				buf - p, (unsigned int)buf[0],
				size > 1 ? (unsigned int)buf[1] : 0,
				size > 2 ? (unsigned int)buf[2] : 0,
				size > 3 ? (unsigned int)buf[3] : 0);
			if (debug)
				fprintf(stderr, "buf %p, p %p\n", buf, p);
			return buf;
		}
		buf++;
		size--;
	}
	return 0;	/* all zeros */
}

int read_sparse(char *filename, int filesize)
{
	int fd;
	int i;
	int j;
	int r;
	char buf[4096];

	while ((fd = open(filename, O_RDONLY)) < 0) {
		sleep(1);	/* wait for file to be created */
	}

	for (i = 0 ; i < 100000000; i++) {
		off_t offset = 0;
		char *badbuf;

		if (debug > 1 && (i % 10) == 0) {
			fprintf(stderr, "child %d, read loop count %d\n", 
				getpid(), i);
		}
		lseek(fd, SEEK_SET, 0);
		for (j = 0; j < filesize+1; j += sizeof(buf)) {
			r = read(fd, buf, sizeof(buf));
			if (r > 0) {
				if ((badbuf = check_zero(buf, sizeof(buf)))) {
					fprintf(stderr, "non-zero read at offset %d\n",
						offset + badbuf - buf);
					kill(getppid(), SIGTERM);
					exit(10);
				}
			}
			offset += r;
		}
	}
	return 0;
}

volatile int got_signal;

void
sig_term_func(int i, siginfo_t *si, void *p)
{
	if (debug)
		fprintf(stderr, "sig(%d, %p, %p)\n", i, si, p);
	got_signal++;
}

/*
 * do async writes to a sparse file
 */
void aio_sparse(char *filename, int align, int writesize, int filesize, int num_aio)
{
	int fd;
	int i;
	int w;
	static struct sigaction s;
	struct iocb **iocbs;
	off_t offset;
	io_context_t myctx;
	struct io_event event;
	int aio_inflight;

	s.sa_sigaction = sig_term_func;
	s.sa_flags = SA_SIGINFO;
	sigaction(SIGTERM, &s, 0);

	if ((num_aio * writesize) > filesize) {
		num_aio = filesize / writesize;
	}
	memset(&myctx, 0, sizeof(myctx));
	io_queue_init(num_aio, &myctx);

	iocbs = (struct iocb **)malloc(sizeof(struct iocb *) * num_aio);
	for (i = 0; i < num_aio; i++) {
		if ((iocbs[i] = (struct iocb *)malloc(sizeof(struct iocb))) == 0) {
			perror("cannot malloc iocb");
			return;
		}
	}

	fd = open(filename, O_WRONLY|O_CREAT, 0666);

	if (fd < 0) {
		perror("cannot create file");
		return;
	}

	ftruncate(fd, filesize);

	/*
	 * allocate the iocbs array and iocbs with buffers
	 */
	offset = 0;
	for (i = 0; i < num_aio; i++) {
		void *bufptr;

		if (posix_memalign(&bufptr, align, writesize)) {
			perror("cannot malloc aligned memory");
			close(fd);
			unlink(filename);
			return;
		}
		memset(bufptr, 0, writesize);
		io_prep_pwrite(iocbs[i], fd, bufptr, writesize, offset);
		offset += writesize;
	}

	/*
	 * start the 1st num_aio write requests
	 */
	if ((w = io_submit(myctx, num_aio, iocbs)) < 0) {
		perror("io_submit failed");
		close(fd);
		unlink(filename);
		return;
	}
	if (debug) 
		fprintf(stderr, "io_submit() return %d\n", w);

	/*
	 * As AIO requests finish, keep issuing more AIO until done.
	 */
	aio_inflight = num_aio;
	if (debug)
		fprintf(stderr, "aio_test_sparse: %d i/o in flight\n", aio_inflight);
	while (offset < filesize)  {
		int n;
		struct iocb *iocbp;

		if (debug)
			fprintf(stderr, "aio_test_sparse: offset %ld filesize %d inflight %d\n",
				offset, filesize, aio_inflight);

		if ((n = io_getevents(myctx, 1, 1, &event, 0)) != 1) {
			if (-n != EINTR)
				fprintf(stderr, "io_getevents() returned %d\n", n);
			break;
		}
		if (debug)
			fprintf(stderr, "aio_test_sparse: io_getevent() returned %d\n", n);
		aio_inflight--;
		if (got_signal)
			break;		/* told to stop */
		/*
		 * check if write succeeded.
		 */
		iocbp = event.obj;
		if (event.res2 != 0 || event.res != iocbp->u.c.nbytes) {
			fprintf(stderr,
				"AIO write offset %lld expected %ld got %ld\n",
				iocbp->u.c.offset, iocbp->u.c.nbytes,
				event.res);
			break;
		}
		if (debug)
			fprintf(stderr, "aio_test_sparse: io_getevent() res %ld res2 %ld\n",
				event.res, event.res2);
		
		/* start next write */
		io_prep_pwrite(iocbp, fd, iocbp->u.c.buf, writesize, offset);
		offset += writesize;
		if ((w = io_submit(myctx, 1, &iocbp)) < 0) {
			fprintf(stderr, "io_submit failed at offset %ld\n",
				offset);
			perror("");
			break;
		}
		if (debug) 
			fprintf(stderr, "io_submit() return %d\n", w);
		aio_inflight++;
	}

	/*
	 * wait for AIO requests in flight.
	 */
	while (aio_inflight > 0) {
		int n;
		struct iocb *iocbp;

		if ((n = io_getevents(myctx, 1, 1, &event, 0)) != 1) {
			perror("io_getevents failed");
			break;
		}
		aio_inflight--;
		/*
		 * check if write succeeded.
		 */
		iocbp = event.obj;
		if (event.res2 != 0 || event.res != iocbp->u.c.nbytes) {
			fprintf(stderr,
				"AIO write offset %lld expected %ld got %ld\n",
				iocbp->u.c.offset, iocbp->u.c.nbytes,
				event.res);
		}
	}
	if (debug)
		fprintf(stderr, "AIO DIO write done unlinking file\n");
	close(fd);
	unlink(filename);
}


int usage(void)
{
	fprintf(stderr, "usage: aio_test_sparse [-n children] [-s filesize]"
		" [-w writesize] [-r readsize] [-f fname] \n");
	exit(1);
}

/*
 * Scale value by kilo, mega, or giga.
 */
long long scale_by_kmg(long long value, char scale)
{
	switch (scale) {
	case 'g':
	case 'G':
		value *= 1024;
	case 'm':
	case 'M':
		value *= 1024;
	case 'k':
	case 'K':
		value *= 1024;
		break;
	case '\0':
		break;
	default:
		usage();
		break;
	}
	return value;
}

/*
 *	usage:
 * aio_test_sparse [-r readsize] [-w writesize] [-n chilren] [-a align] [-i num_aio] [-f filename]
 */

int main(int argc, char **argv)
{
	int pid[NUM_CHILDREN];
	int num_children = 1;
	int i;
	char *filename = "file";
	long alignment = 512;
	int readsize = 65536;
	int writesize = 65536;
	int filesize = 100*1024*1024;
	int num_aio = 16;
	int children_errors = 0;
	int c;
	extern char *optarg;
	extern int optind, optopt, opterr;

	while ((c = getopt(argc, argv, "df:r:w:n:a:s:i:")) != -1) {
		char *endp;
		switch (c) {
		case 'f':
			filename = optarg;
			break;
		case 'd':
			debug++;
			break;
		case 'i':
			num_aio = atoi(optarg);
			break;
		case 'a':
			alignment = strtol(optarg, &endp, 0);
			alignment = (int)scale_by_kmg((long long)alignment,
                                                        *endp);
			break;
		case 'r':
			readsize = strtol(optarg, &endp, 0);
			readsize = (int)scale_by_kmg((long long)readsize, *endp);
			break;
		case 'w':
			writesize = strtol(optarg, &endp, 0);
			writesize = (int)scale_by_kmg((long long)writesize, *endp);
			break;
		case 's':
			filesize = strtol(optarg, &endp, 0);
			filesize = (int)scale_by_kmg((long long)filesize, *endp);
			break;
		case 'n':
			num_children = atoi(optarg);
			if (num_children > NUM_CHILDREN) {
				fprintf(stderr,
					"number of children limited to %d\n",
					NUM_CHILDREN);
				num_children = NUM_CHILDREN;
			}
			break;
		case '?':
			usage();
			break;
		}
	}

	for (i = 0; i < num_children; i++) {
		if ((pid[i] = fork()) == 0) {
			/* child */
			return read_sparse(filename, filesize);
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
	 * Parent write to a hole in a file using async i/o
	 */

	aio_sparse(filename, alignment, writesize, filesize, num_aio);

	if (debug)
		fprintf(stderr, "aio_sparse done writing, kill children\n");

	for (i = 0; i < num_children; i++) {
		kill(pid[i], SIGTERM);
	}

	for (i = 0; i < num_children; i++) {
		int status;
		pid_t p;

		p = waitpid(pid[i], &status, 0);
		if (p < 0) {
			perror("waitpid");
		} else {
			if (WIFEXITED(status) && WEXITSTATUS(status) == 10) {
				children_errors++;
				if (debug) {
					fprintf(stderr, "child %d bad exit\n", p);
				}
			}
		}
	}
	if (debug)
		fprintf(stderr, "aio_sparse %d children had errors\n",
			children_errors);
	if (children_errors) {
		fprintf(stderr, "AIO Sparse Test failed!\n");
		return 1;
	}
	else {
		fprintf(stderr, "AIO Sparse Test passed!\n");
		return 0;
	}
}
