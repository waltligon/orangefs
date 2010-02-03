#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <time.h>
#include <assert.h>
#include <unistd.h> /* getopt() */

#define OFFSET 0
int _debug;

/* usage() */
static void usage(char *argv0) {
  char *help =
    "Usage: %s [switches] -i filename -c num_cluster -t threshold -g -h\n"
    "       -i filename    : file containing data to be clustered\n"
    "       -c num_of_cluster\n"
    "       -g : use GPU accelerator\n"
    "       -t threshold\n";
  fprintf(stderr, help, argv0);
}

int main( int argc, char *argv[] )
{
  int opt;
  extern char   *optarg;
  extern int     optind;
  int is_output_timing, is_print_usage = 0;
  int use_gpu = 0, num_clusters = 0;
  int size, rank, i, count, err, nproc;
  double *buf;
  MPI_File fh;
  MPI_Comm comm;
  MPI_Status status;
  char *filename = NULL;
  MPI_Info info;
  float threshold = 0.0;
  double stime, etime;

  /* for KMEANS */
  int numObjs;
  int numCoords;
  float **objects;

  MPI_Init( &argc, &argv );

  comm = MPI_COMM_WORLD;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &nproc);

  while ( (opt=getopt(argc,argv,"i:c:t:odhg"))!= EOF) {
    switch (opt) {
    case 'i': filename = optarg;
      break;
    case 'c': num_clusters = atoi(optarg);
      break;
    case 'o': is_output_timing = 1;
      break;
    case 'd': _debug = 1;
      break;
    case 'h': is_print_usage = 1;
      break;
    case 'g': use_gpu = 1;
      printf("use_gpu is set to 1\n");
      break;
    case 't': threshold = atof(optarg);
        printf("threshold=%f\n", threshold);
        break;
    default: is_print_usage = 1;
      break;
    }
  }

  if (filename == 0 || is_print_usage == 1 || num_clusters == 0 || threshold == 0.0) {
    if (rank == 0) usage(argv[0]);
    MPI_Finalize();
    exit(1);
  }

  err = MPI_File_open( comm, filename, MPI_MODE_RDONLY, MPI_INFO_NULL, &fh );

  if (err != MPI_SUCCESS) {
    char errstr[MPI_MAX_ERROR_STRING];
    int  errlen;
    MPI_Error_string(err, errstr, &errlen);
    printf("Error at opening file %s (%s)\n", filename, errstr);
    MPI_Finalize();
    exit(1);
  }

  MPI_Info_create(&info);

  /* read numObjs & numCoords from the 1st 2 integers */
  MPI_File_read(fh, &numObjs, 1, MPI_INT, &status);
  printf("numObjs=%d\n", numObjs);
  MPI_File_read(fh, &numCoords, 1, MPI_INT, &status);
  printf("numCoords=%d\n", numCoords);

  char tmp_str[80];
  sprintf(tmp_str, "%d:%d:%d:%d:%f", use_gpu, numObjs, numCoords, num_clusters, threshold);
  MPI_Info_set(info, "kmeans", tmp_str);

  MPI_File_close(&fh);

  /* reopen with hint, and then reread numObjs and numCoords */
  err = MPI_File_open( comm, filename, MPI_MODE_RDONLY, info, &fh );
  /* read numObjs & numCoords from the 1st 2 integers */
  MPI_File_read(fh, &numObjs, 1, MPI_INT, &status);
  printf("numObjs=%d\n", numObjs);
  MPI_File_read(fh, &numCoords, 1, MPI_INT, &status);
  printf("numCoords=%d\n", numCoords);

  objects    = (float**)malloc((numObjs) * sizeof(float*));
  assert(objects != NULL);
  objects[0] = (float*) malloc((numObjs)*(numCoords) * sizeof(float));
  assert(objects[0] != NULL);
  for (i=1; i<numObjs; i++)
    objects[i] = objects[i-1] + (numCoords);

  stime = MPI_Wtime(); 
  MPI_File_read_ex(fh, objects[0], (numObjs)*(numCoords),
		   MPI_FLOAT, MPI_KMEANS, &status);

  etime = MPI_Wtime();
  printf("%d: time =%10.4f\n", rank, etime-stime);
  //printf ("kmean=%lf\n", objects[0]); 
  
  MPI_File_close( &fh );

  free(objects[0]);
  free(objects);

  MPI_Finalize();
  return err;
}
