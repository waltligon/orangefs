#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <time.h>
#include <assert.h>

#define OFFSET 0

/* Test reading and writing zero bytes (set status correctly) */

int main( int argc, char *argv[] )
{
  int size, rank, i, count, err, nproc;
  double *buf;
  MPI_File fh;
  MPI_Comm comm;
  MPI_Status status;
  char infile[80] = "pvfs2:/mnt/pvfs2/out10";

  /* for KMEANS */
  int numObjs;
  int numCoords;
  float **objects;

  MPI_Init( &argc, &argv );

  comm = MPI_COMM_WORLD;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &nproc);

  err = MPI_File_open( comm, infile, MPI_MODE_RDONLY, MPI_INFO_NULL, &fh );

  if (err != MPI_SUCCESS) {
    char errstr[MPI_MAX_ERROR_STRING];
    int  errlen;
    MPI_Error_string(err, errstr, &errlen);
    printf("Error at opening file %s (%s)\n", infile, errstr);
    MPI_Finalize();
    exit(1);
  }

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

  
  MPI_File_read_ex(fh, objects[0], (numObjs)*(numCoords),
		   MPI_FLOAT, MPI_KMEANS, &status);

  //printf ("kmean=%lf\n", objects[0]); 
  
  MPI_File_close( &fh );

  free(objects[0]);
  free(objects);

  MPI_Finalize();
  return err;
}
