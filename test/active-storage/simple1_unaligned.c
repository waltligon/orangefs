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
  int errs = 0;
  int size, rank, i, count;
  double *buf;
  MPI_File fh;
  MPI_Comm comm;
  MPI_Status status;
  char infile[80] = "pvfs2:/mnt/pvfs2/test1_unaligned";
  char infile1[80] = "pvfs2:/mnt/pvfs2/edge17695.bin";
  //int nitem = 2*1024; /* 2048*8byes = 16KB, will go through flow */
  //int nitem = 64*1024*8; /* 4MB */
  //int nitem = 8192; /* 64 KB */
  int nitem = (32768)*2; /* 2 stripes if strip size is 256KB */
  //int nitem = (32768)*4; /* 4 stripes when strip size if 256KB */
  //int nitem = (32768)*3; /* 3 stripes if strip size is 256KB */
  //int nitem = (32768-5)*4; /* 4 stripes if strip size is 256KB */
  //int nitem = (32768-5)*6; /* 6 stripes if strip size is 256KB */

  /* for KMEANS */
  int *numObjs;
  int *numCoords;
  float **objects;
  int err;

  MPI_Init( &argc, &argv );
 
  comm = MPI_COMM_WORLD;

  MPI_File_open( comm, infile, MPI_MODE_RDWR | MPI_MODE_CREATE, MPI_INFO_NULL, &fh );
  MPI_Comm_size( comm, &size );
  MPI_Comm_rank( comm, &rank );

  buf = (double *)malloc( nitem * sizeof(double) );

  /* initialize random seed: */
  srand ( time(NULL) );

  double max, min, sum=0.0;
  buf[0] = rand()%4096;
  printf("%lf\n", buf[0]);
  max = min = sum = buf[0];
  for(i=1; i<nitem; i++) {
    double t = rand()%4096;
    if (i<10 || (i%32768)==0 || i == (nitem-1) || i == (nitem-2)) printf("a[%d]: %lf\n", i, t);
    if (t>max) max = t;
    if (t<min) min = t;
    sum += t;
    buf[i] = t;
  }
  printf ("max=%lf, min=%lf, sum=%lf, mean=%lf\n", max, min, sum, sum/nitem);

  /* Write to file */
  MPI_File_write_at( fh, OFFSET, buf, nitem, MPI_DOUBLE, &status );

  MPI_File_close(&fh);

  memset( &status, 0xff, sizeof(MPI_Status) );

  double *tmp = (double *)malloc( nitem * sizeof(double) );
#if 0
  MPI_File_open( comm, infile, MPI_MODE_RDWR, MPI_INFO_NULL, &fh );

  MPI_File_read_at_ex( fh, 1, tmp, nitem, MPI_DOUBLE, MPI_MAX, &status );

  printf ("max=%lf (active storage)\n", tmp[0]); 

  MPI_File_close(&fh);
  
  MPI_File_open( comm, infile, MPI_MODE_RDWR, MPI_INFO_NULL, &fh );

  MPI_File_read_at_ex( fh, 1, tmp, nitem, MPI_DOUBLE, MPI_MIN, &status );

  printf ("min=%lf (active storage)\n", tmp[0]); 

  MPI_File_close(&fh);
#endif
  err = MPI_File_open( comm, infile, MPI_MODE_RDWR, MPI_INFO_NULL, &fh );

  if (err != MPI_SUCCESS) {
    char errstr[MPI_MAX_ERROR_STRING];
    int  errlen;
    MPI_Error_string(err, errstr, &errlen);
    printf("Error at opening file %s (%s)\n", infile1, errstr);
    MPI_Finalize();
    exit(1);
  }

  MPI_File_read_at_ex( fh, OFFSET, tmp, nitem, MPI_DOUBLE, MPI_SUM, &status );
  
  printf ("sum=%lf (active storage)\n", tmp[0]); 

  MPI_File_close( &fh );

#if 0
  MPI_File_open( comm, infile, MPI_MODE_RDWR, MPI_INFO_NULL, &fh );

  MPI_File_read_at_ex( fh, OFFSET, tmp, nitem, MPI_DOUBLE, MPI_MEAN, &status );

  printf ("mean=%lf\n", tmp[0]); 

  MPI_File_close( &fh );
#endif

  err = MPI_File_open( comm, infile1, MPI_MODE_RDONLY, MPI_INFO_NULL, &fh );

  if (err != MPI_SUCCESS) {
    char errstr[MPI_MAX_ERROR_STRING];
    int  errlen;
    MPI_Error_string(err, errstr, &errlen);
    printf("Error at opening file %s (%s)\n", infile1, errstr);
    MPI_Finalize();
    exit(1);
  }

  /* read numObjs & numCoords from the 1st 2 integers */
  MPI_File_read(fh, numObjs, 1, MPI_INT, &status);
  printf("numObjs=%d\n", *numObjs);
  MPI_File_read(fh, numCoords, 1, MPI_INT, &status);
  printf("numCoords=%d\n", *numCoords);
  
  objects    = (float**)malloc((*numObjs) * sizeof(float*));
  assert(objects != NULL);
  objects[0] = (float*) malloc((*numObjs)*(*numCoords) * sizeof(float));
  assert(objects[0] != NULL);
  for (i=1; i<(*numObjs); i++)
    objects[i] = objects[i-1] + (*numCoords);
  
  MPI_File_read_ex(fh, objects[0], (*numObjs)*(*numCoords),
		   MPI_FLOAT, MPI_KMEANS, &status);

  printf ("kmean=%lf\n", tmp[0]); 
  
  MPI_File_close( &fh );

  free( buf );
  free( tmp );

  free(objects[0]);
  free(objects);

  MPI_Finalize();
  return errs;
}
