/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


/* io.c
 *
 * Usually the MPI versions can be compiled from this directory with
 * something like:
 *
 * mpicc -D__USE_MPI__ -I../include io.c -L../lib -lpvfs \
 *       -o io
 *
 * This is derived from code given to me by Rajeev Thakur.  Dunno where
 * it originated.
 *
 * It's purpose is to produce aggregate bandwidth numbers for varying
 * block sizes, number of processors, an number of iterations.
 *
 * Compile time defines determine whether or not it will use mpi, while
 * all other options are selectable via command line.
 *
 * NOTE: This code assumes that all command line arguments make it out to all
 * the processes that make up the parallel job, which isn't always the case.
 * So if it doesn't work on some platform, that might be why.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <error.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include <mpi.h>


/* DEFAULT VALUES FOR OPTIONS */
int64_t opt_block     = 1048576*16;
int     opt_iter      = 1;
int     opt_correct   = 0;
int     opt_read      = 1;
int     opt_write     = 1;
int     opt_unlink    = 0;
int	  opt_sync	    = 0;
int     amode         = O_RDWR | O_CREAT | O_LARGEFILE;
char    opt_file[256] = "/foo/test.out\0";

/* function prototypes */
int parse_args(int argc, char **argv);
double Wtime(void);

/* globals needed for getopt */
extern char *optarg;
extern int optind, opterr;

int main(int argc, char **argv)
{
	char *buf, *tmp, *check;
	int fd, i, j, v, mynod=0, nprocs=1, err, sync_err=0, my_correct = 1, correct, myerrno = 0, sync_errno = 0;
	double stim, etim;
	double write_tim = 0;
	double read_tim = 0;
	double read_bw, write_bw;
	double max_read_tim, max_write_tim;
	double min_read_tim, min_write_tim;
	double sum_read_tim, sum_write_tim;
	double ave_read_tim, ave_write_tim;
	double var_read_tim, var_write_tim;
	double sumsq_read_tim, sumsq_write_tim;
	double sq_read_tim, sq_write_tim;
	int64_t iter_jump = 0, loc;

	/* startup MPI and determine the rank of this process */
	MPI_Init(&argc,&argv);
	MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
	MPI_Comm_rank(MPI_COMM_WORLD, &mynod);

	/* parse the command line arguments */
	parse_args(argc, argv);
	if (mynod == 0) printf("# Using vfs calls.\n");

	/* this is how much of the file data is covered on each iteration of
	 * the test.  used to help determine the seek offset on each
	 * iteration
	 */
	iter_jump = nprocs * opt_block;
		
	/* setup a buffer of data to write */
	if (!(tmp = (char *) malloc(opt_block + 256))) {
		perror("malloc");
		goto die_jar_jar_die;
	}
	buf = tmp + 128 - (((long)tmp) % 128);  /* align buffer */

	if (opt_write) {
		/* open the file for writing */
		fd = open(opt_file, amode, 0644);

		if (fd < 0) {
			fprintf(stderr, "node %d, open error: %s\n", mynod,
					  strerror(errno));
			goto die_jar_jar_die;
		}
		
		/* repeat write number of times specified on the command line */
		for (j=0; j < opt_iter; j++) {
			/* seek to an appropriate position depending on the iteration and
			 * rank of the current process */
			loc = (j*iter_jump) + (mynod*opt_block);
			lseek(fd, loc, SEEK_SET);

			if (opt_correct) /* fill in buffer for iteration */ {
				for (i=0,v=mynod+j, check=buf; i<opt_block; i++,v++,check++) 
					*check = (char) v;
			}

			/* discover the starting time of the operation */
			MPI_Barrier(MPI_COMM_WORLD);
			stim = MPI_Wtime();

			/* write out the data */
			err = write(fd, buf, opt_block);
			myerrno = errno;

			if (opt_sync) {
				sync_err = fsync(fd);
				sync_errno = errno;
			}
			/* discover the ending time of the operation */
			etim = MPI_Wtime();
			write_tim += (etim - stim);

			if (err < 0)
				fprintf(stderr, "node %d, write error, loc = %Ld: %s\n",
						  mynod, (long long) mynod*opt_block, strerror(myerrno));
			/* only way sync_err can be nonzero is if opt_sync set*/
			if (opt_sync && sync_err < 0)
				fprintf(stderr, "node %d, sync error, loc = %Ld: %s\n",
						  mynod, (long long) mynod*opt_block, strerror(sync_errno));
			
		} /* end of write loop */

		/* close the file */
		err = close(fd);
		if (err < 0) {
				fprintf(stderr, "node %d, close error after write\n", mynod);
		}

		/* wait for everyone to synchronize at this point */
		MPI_Barrier(MPI_COMM_WORLD);
	} /* end of if (opt_write) */

	if (opt_read) {
		/* open the file to read the data back out */
		fd = open(opt_file, amode, 0644);

		if (fd < 0) {
			fprintf(stderr, "node %d, open error: %s\n", mynod,
					  strerror(errno));
			goto die_jar_jar_die;
		}

		/* repeat the read operation the number of iterations specified */
		for (j=0; j < opt_iter; j++) {
			/* seek to the appropriate spot give the current iteration and
			 * rank within the MPI processes */
			loc = (j*iter_jump)+(mynod*opt_block);
			lseek(fd, loc, SEEK_SET);

			/* discover the start time */
			MPI_Barrier(MPI_COMM_WORLD);
			stim = MPI_Wtime();

			/* read in the file data; if testing for correctness, read into
			 * a second buffer so we can compare data
			 */
			err = read(fd, buf, opt_block);
			myerrno = errno;

			/* discover the end time */
			etim = MPI_Wtime();
			read_tim += (etim - stim);

			if (err < 0)
				fprintf(stderr, "node %d, read error, loc = %Ld: %s\n", mynod,
						  (long long) mynod*opt_block, strerror(myerrno));

			/* if the user wanted to check correctness, compare the write
			 * buffer to the read buffer
			 */
			if (opt_correct) {
				int badct = 0;
				for (i=0,v=mynod+j, check=buf;
					  i < opt_block && badct < 10;
					  i++,v++,check++)
				{
					if (*check != (char) v) {
						my_correct = 0;
						if (badct < 10) {
							badct++;
							fprintf(stderr, "buf[%d] = %d, should be %d\n", 
									  i, *check, (char) v);
						}
					}
				}

				if (badct == 10) fprintf(stderr, "...\n");
				MPI_Allreduce(&my_correct, &correct, 1, MPI_INT, MPI_MIN,
								  MPI_COMM_WORLD);
			}
		} /* end of read loop */

		/* close the file */
		close(fd);
	} /* end of if (opt_read) */

	MPI_Allreduce(&read_tim, &max_read_tim, 1, MPI_DOUBLE, MPI_MAX,
		MPI_COMM_WORLD);
	MPI_Allreduce(&read_tim, &min_read_tim, 1, MPI_DOUBLE, MPI_MIN,
		MPI_COMM_WORLD);
	MPI_Allreduce(&read_tim, &sum_read_tim, 1, MPI_DOUBLE, MPI_SUM,
		MPI_COMM_WORLD);
	/* calculate our part of the summation used for variance */
	sq_read_tim = read_tim - (sum_read_tim / nprocs);
	sq_read_tim = sq_read_tim * sq_read_tim;
	MPI_Allreduce(&sq_read_tim, &sumsq_read_tim, 1, MPI_DOUBLE, MPI_SUM,
		MPI_COMM_WORLD);

	MPI_Allreduce(&write_tim, &max_write_tim, 1, MPI_DOUBLE, MPI_MAX,
		MPI_COMM_WORLD);
	MPI_Allreduce(&write_tim, &min_write_tim, 1, MPI_DOUBLE, MPI_MIN,
		MPI_COMM_WORLD);
	MPI_Allreduce(&write_tim, &sum_write_tim, 1, MPI_DOUBLE, MPI_SUM,
		MPI_COMM_WORLD);
	/* calculate our part of the summation used for variance */
	sq_write_tim = write_tim - (sum_write_tim / nprocs);
	sq_write_tim = sq_write_tim * sq_write_tim;
	MPI_Allreduce(&sq_write_tim, &sumsq_write_tim, 1, MPI_DOUBLE, MPI_SUM,
		MPI_COMM_WORLD);
	/* calculate the mean from the sum */
	ave_read_tim = sum_read_tim / nprocs; 
	ave_write_tim = sum_write_tim / nprocs; 

	/* calculate variance */
	if (nprocs > 1) {
		var_read_tim = sumsq_read_tim / (nprocs-1);
		var_write_tim = sumsq_write_tim / (nprocs-1);
	}
	else {
		var_read_tim = 0;
		var_write_tim = 0;
	}

	/* unlink the file(s) if asked to */
	if (opt_unlink != 0) {
		if (mynod == 0) {
			err = unlink(opt_file);
			if (err < 0) {
				fprintf(stderr, "node %d, unlink error, file = %s: %s\n", mynod,
				opt_file, strerror(myerrno));
			}
		}
	}
	
	/* print out the results on one node */
	if (mynod == 0) {
	   read_bw = ((int64_t)(opt_block*nprocs*opt_iter))/
		(max_read_tim*1000000.0);
	   write_bw = ((int64_t)(opt_block*nprocs*opt_iter))/
		(max_write_tim*1000000.0);
		
		printf("nr_procs = %d, nr_iter = %d, blk_sz = %Ld\n",
		nprocs, opt_iter, (long long) opt_block);
		
		printf("# total_size = %Ld\n", (long long) opt_block*nprocs*opt_iter);
		
		if (opt_write)
			printf("# Write:  min_t = %f, max_t = %f, mean_t = %f, var_t = %f\n", 
			min_write_tim, max_write_tim, ave_write_tim, var_write_tim);
		if (opt_read)
			printf("# Read:  min_t = %f, max_t = %f, mean_t = %f, var_t = %f\n", 
			min_read_tim, max_read_tim, ave_read_tim, var_read_tim);
		
		if (opt_write)
			printf("Write bandwidth = %f Mbytes/sec\n", write_bw);
	   if (opt_read)
			printf("Read bandwidth = %f Mbytes/sec\n", read_bw);
		
		if (opt_correct)
			printf("Correctness test %s.\n", correct ? "passed" : "failed");
	}

die_jar_jar_die:
	free(tmp);
	MPI_Finalize();
	return(0);
}

int parse_args(int argc, char **argv)
{
	int c;
	
	while ((c = getopt(argc, argv, "b:i:f:cwruy")) != EOF) {
		switch (c) {
			case 'b': /* block size */
				opt_block = atoi(optarg);
				break;
			case 'i': /* iterations */
				opt_iter = atoi(optarg);
				break;
			case 'f': /* filename */
				strncpy(opt_file, optarg, 255);
				break;
			case 'c': /* correctness */
				if (opt_write && opt_read) {
					opt_correct = 1;
				}
				else {
					fprintf(stderr, "%s: cannot test correctness without"
					" reading AND writing\n", argv[0]);
					exit(1);
				}
				break;
			case 'u': /* unlink after test */
				opt_unlink = 1;
				break;
			case 'r': /* read only */
				opt_write = 0;
				opt_read = 1;
				opt_correct = 0; /* can't check correctness without both */
				break;
			case 'w': /* write only */
				opt_write = 1;
				opt_read = 0;
				opt_correct = 0; /* can't check correctness without both */
				break;
			case 'y': /* sYnc */
				opt_sync = 1;
				break;
			case '?': /* unknown */
			default:
				fprintf(stderr, "usage: %s [-b blksz] [-i iter] [-f file] "
				"[-c] [-w] [-r] [-y]\n\n"
				"  -b blksz      access blocks of blksz bytes (default=16MB)\n"
				"  -i iter       perform iter accesses (default=1)\n"
				"  -f file       name of file to access\n"
				"  -c            test for correctness\n"
				"  -w            write only\n"
				"  -r            read only\n"
				"  -u            unlink file(s) after test\n"
				"  -y            sYnc after operations\n",
				argv[0]);
				exit(1);
				break;
		}
	}
	return(0);
}

/* Wtime() - returns current time in sec., in a double */
double Wtime()
{
	struct timeval t;
	
	gettimeofday(&t, NULL);
	return((double)t.tv_sec + (double)t.tv_usec / 1000000);
}


/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 


