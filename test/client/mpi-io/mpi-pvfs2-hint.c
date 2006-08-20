#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <mpi.h>

#include <getopt.h>


extern char *optarg;
extern int optind, opterr, optopt;

#ifndef PATH_MAX
#define PATH_MAX FILENAME_MAX
#endif

char opt_basedir[PATH_MAX];

void usage(char *name);
int parse_args(int argc, char **argv);
void handle_error(int errcode, char *str);
int test_opens(int nfiles, char * test_dir, MPI_Info info);

void usage(char *name)
{
	fprintf(stderr, "usage: %s -d /path/to/directory \n", name);
	exit(-1);
}
int parse_args(int argc, char **argv)
{
	int c;
	while ( (c = getopt(argc, argv, "d:")) != -1 ) {
		switch (c) {
			case 'd':
				strncpy(opt_basedir, optarg, PATH_MAX);
				break;
			case '?':
			case ':':
			default:
				usage(argv[0]);
		}
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

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

	parse_args(argc, argv);

	/* provide hints if you want  */
	info = MPI_INFO_NULL;
    
    MPI_Info_create(& info);
    MPI_Info_set(info, "CREATE_SET_METAFILE_NODE", "localhost");
    MPI_Info_set(info, "REQUEST_ID", "Sample MPI request id");

	test_opens(1, opt_basedir, info);
    
    MPI_Info_free(& info);

	MPI_Finalize();
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

