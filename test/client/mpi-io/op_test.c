/*
 * Joe insley reported slow open behavior.  Let's find out what is going on
 * when an application opens a bunch of files 
 *
 * This can be further extented to test open behavior across several file
 * systems ( pvfs, pvfs2, nfs, testfs )
 *
 * The timing and command-line parsing were so useful that this was further
 * extended to test resize operations
 *
 * usage:  -d /path/to/directory -n number_of_files [-O] [-R]
 */

#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include <mpi.h>

#ifndef PATH_MAX
#define PATH_MAX FILENAME_MAX
#endif

extern char *optarg;
int opt_nfiles;
char opt_basedir[PATH_MAX];
int opt_do_open=0;
int opt_do_resize=0;

void usage(char *name);
int parse_args(int argc, char **argv);
void handle_error(int errcode, char *str);
int test_opens(int nfiles, char * test_dir, MPI_Info info);
int test_resize(int rank, unsigned int seed, int iterations, 
		char * test_dir, MPI_Info info);

void usage(char *name)
{
	fprintf(stderr, "usage: %s -d /path/to/directory -n #_of_files [TEST}\n", name);
	fprintf(stderr, "   where TEST is one of:\n"
			"     -O       test file open times\n"
			"     -R       test file resize times\n");

		exit(-1);
}
int parse_args(int argc, char **argv)
{
	int c;
	while ( (c = getopt(argc, argv, "d:n:OR")) != -1 ) {
		switch (c) {
			case 'd':
				strncpy(opt_basedir, optarg, PATH_MAX);
				break;
			case 'n':
				opt_nfiles = atoi(optarg);
				break;
			case 'O':
				opt_do_open = 1;
				break;
			case 'R':
				opt_do_resize = 1;
				break;
			case '?':
			case ':':
			default:
				usage(argv[0]);
		}
	}
	if ( (opt_do_open == 0) && (opt_do_resize == 0) ) {
		usage(argv[0]);
	}
	return 0;
}

void handle_error(int errcode, char *str)
{
        char msg[MPI_MAX_ERROR_STRING];
        int resultlen;
        MPI_Error_string(errcode, msg, &resultlen);
        fprintf(stderr, "%s: %s\n", str, msg);
        MPI_Abort(MPI_COMM_WORLD, 1);
}


int main(int argc, char **argv)
{
	int rank, nprocs;
	MPI_Info info;
	double test_start, test_end;
	double test_time, total_time;
	pid_t	seed; 	/* for tests that need some pseudorandomness */

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &nprocs);


	parse_args(argc, argv);

	/* provide hints if you want  */
	info = MPI_INFO_NULL;

	/* we want some semblance of randomness, yet want all processors to
	 * have the same values.  everyone's got to seed w/ the same value */
	if (rank == 0) 
		seed = getpid();

	MPI_Bcast(&seed, sizeof(pid_t), MPI_BYTE, 0, MPI_COMM_WORLD);

	test_start = MPI_Wtime();
	if (opt_do_open)
		test_opens(opt_nfiles, opt_basedir, info);
	else if (opt_do_resize)
		test_resize(rank, seed, opt_nfiles, opt_basedir, info);

	test_end = MPI_Wtime();
	test_time = test_end - test_start;
	MPI_Allreduce(&test_time, &total_time, 1, 
			MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
	MPI_Finalize();

	if (rank == 0) {
		if (opt_do_open) {
			printf("%f seconds to open %d files: %f secs/open\n", 
					total_time, opt_nfiles, 
					(total_time)/opt_nfiles);
		} else if (opt_do_resize) {
			printf("%f seconds to perform %d resize ops: %f secs/opeeration\n", 
					total_time, opt_nfiles, 
					(total_time)/opt_nfiles);
		}
			
	}
	return 0;
}

/* in directory 'test_dir', open and immediately close 'nfiles' files */
/* a possible variant: open all the files first, then close */
/* also test MPI_File_open behavior when there are a ton of files */
int test_opens(int nfiles, char * test_dir, MPI_Info info)
{
	int i;
	char test_file[PATH_MAX];
	MPI_File fh;
	int errcode;

	for (i=0; i<nfiles; i++) {
		snprintf(test_file, PATH_MAX, "%s/testfile.%d", test_dir, i);
		errcode = MPI_File_open(MPI_COMM_WORLD, test_file, 
				MPI_MODE_CREATE|MPI_MODE_RDWR, info, &fh);
		if (errcode != MPI_SUCCESS) {
			handle_error(errcode, "MPI_File_open");
		}
		errcode = MPI_File_close(&fh);
		if (errcode != MPI_SUCCESS) {
			handle_error(errcode, "MPI_File_close");
		}

	}
	/* since handle_error aborts, if we got here we are a-ok */
	return 0;
}

/* stuff these into separate object files. have a structure that provides a
 * test() and result() function and a .time member */

/* inside directory 'test_dir', create a file and resize it to 'iterations'
 * different sizes */

/* pass in a seed for the pseudorandom number generator: not fair to add the
 * cost of MPI_Bcast to the resize operation */

int test_resize(int rank, unsigned int seed, int iterations, 
		char * test_dir, MPI_Info info)
{
	int i;
	char test_file[PATH_MAX];
	MPI_File fh;
	int errcode;
	MPI_Offset size;

	snprintf(test_file, PATH_MAX, "%s/testfile", test_dir);
	errcode = MPI_File_open(MPI_COMM_WORLD, test_file, 
			MPI_MODE_CREATE|MPI_MODE_RDWR, info, &fh);
	if (errcode != MPI_SUCCESS) {
		handle_error(errcode, "MPI_File_open");
	}
	srand(seed);

	for(i=0; i<iterations; i++) {
		size = rand();
		printf("resizing file to %Ld bytes\n", size);
		errcode = MPI_File_set_size(fh, size);
		if (errcode != MPI_SUCCESS) {
			handle_error(errcode, "MPI_File_set_size");
		}
	}

	errcode = MPI_File_close(&fh);
	if (errcode != MPI_SUCCESS) {
		handle_error(errcode, "MPI_File_close");
	}
	return 0;
}
