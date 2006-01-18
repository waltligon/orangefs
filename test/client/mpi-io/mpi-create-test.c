/*
 * usage:  -d /path/to/directory -n number_of_files
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <assert.h>
#include <mpi.h>
#include <math.h>

#ifndef PATH_MAX
#define PATH_MAX FILENAME_MAX
#endif

extern char *optarg;
int opt_nfiles = -1;
char opt_basedir[PATH_MAX];
int opt_dirarg = -1;

void usage(char *name);
int parse_args(int argc, char **argv);
void handle_error(int errcode, char *str);

void usage(char *name)
{
    fprintf(stderr, "usage: %s -d /path/to/directory -n #_of_files\n", name);
    exit(-1);
}
int parse_args(int argc, char **argv)
{
    int c;

	 if(argc < 3)
	 {
		  usage(argv[0]);
	 }

    while ( (c = getopt(argc, argv, "d:n:")) != -1 ) {
		  switch (c) {
				case 'd':
					 strncpy(opt_basedir, optarg, PATH_MAX);
					 opt_dirarg = 1;
					 break;
				case 'n':
					 opt_nfiles = atoi(optarg);
					 break;
				case '?':
				case ':':
				default:
					 usage(argv[0]);
		  }
	 }

	 if(opt_nfiles == -1 || opt_dirarg == -1)
	 {
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
    int i;
    char test_files[PATH_MAX][100];
    MPI_File  fh[100];
    int errcode;

    int rank, nprocs;
    MPI_Info info;
    double test_start, test_end;
    double times[100];
	 double time, avg_time, dev_time, total_time;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    parse_args(argc, argv);

    if(opt_nfiles > 100)
    {
		  fprintf(stderr, "number of files must be <= 100\n");
		  exit(1);
    }

    /* provide hints if you want  */
    info = MPI_INFO_NULL;

    for(i = 0; i < opt_nfiles; ++i)
    {
		  snprintf(test_files[i], PATH_MAX, "%s/testfile.%d", opt_basedir, i);
    }

	 total_time = 0;
    for(i = 0; i < opt_nfiles; ++i)
    {
		  test_start = MPI_Wtime();

		  errcode = MPI_File_open(MPI_COMM_WORLD, test_files[i], 
										  MPI_MODE_CREATE|MPI_MODE_RDWR, 
										  info, &fh[i]); 
		  if(errcode != MPI_SUCCESS)
		  {
				handle_error(errcode, "MPI_File_open");
		  }

		  test_end = MPI_Wtime();
		  time = test_end - test_start;

		  MPI_Allreduce(&time, &times[i], 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);

		  total_time += times[i];
    }

    avg_time = total_time / opt_nfiles;
	 dev_time = 0;
    for(i = 0; i < opt_nfiles; ++i)
    {
		  dev_time += (times[i] - avg_time) * (times[i] - avg_time);
	 }
	 dev_time = sqrt(dev_time / opt_nfiles);

	 for(i = 0; i < opt_nfiles; ++i)
	 {
		  errcode = MPI_File_close(&fh[i]);
		  if(errcode != MPI_SUCCESS)
		  {
				handle_error(errcode, "MPI_File_close");
		  }
	 }

    for(i = 0; i < opt_nfiles; ++i)
    {
		  MPI_File_delete(test_files[i], info);
    }

    MPI_Finalize();

	 if (rank == 0) {
		  printf("procs: %d\n# operations: %d\navg (s): %f\ndev (s): %f\n",
					nprocs, opt_nfiles, avg_time, dev_time);
	 }
    return 0;
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


