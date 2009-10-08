#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <time.h>
#include <unistd.h> /* getopt() */

/* usage() */
static void usage(char *argv0) {
  char *help =
    "Usage: %s [switches] -i filename\n"
    "       -i filename    : file containing data to be clustered\n"
    "       -g : use generated file\n";
  fprintf(stderr, help, argv0);
}

int main( int argc, char *argv[] )
{
  int opt;
  extern char   *optarg;
  extern int     optind;
  int is_output_timing=0, is_print_usage = 0;
  int _debug, use_gen_file = 0, use_actsto = 0;

  int errs = 0;
  int size, rank, i, count;
  char *fname = NULL;
  float *buf;
  MPI_File fh;
  MPI_Comm comm;
  MPI_Status status;
  int nitem = 0;
  double stime, etime, iotime, comptime;
 
  MPI_Init( &argc, &argv );
 
  comm = MPI_COMM_WORLD;

  MPI_Comm_size( comm, &size );
  MPI_Comm_rank( comm, &rank );

  while ( (opt=getopt(argc,argv,"i:n:godhx"))!= EOF) {
    switch (opt) {
    case 'i': fname = optarg;
      break;
    case 'o': is_output_timing = 1;
      break;
    case 'g': use_gen_file = 1;
      break;
    case 'd': _debug = 1;
      break;
    case 'h': is_print_usage = 1;
      break;
    case 'n': nitem = atoi(optarg);
      printf("nitem = %d\n", nitem);
      break;
    case 'x': use_actsto = 1;
      break;
    default: is_print_usage = 1;
      break;
    }
  }

  if (fname == NULL || is_print_usage == 1 || nitem == 0) {
    if (rank == 0) usage(argv[0]);
    MPI_Finalize();
    exit(1);
  }
 
  /* initialize random seed: */
  srand(time(NULL));

  if(use_gen_file == 1) {
    float max, min, sum=0.0;
    int t;

    MPI_File_open( comm, fname, MPI_MODE_RDWR | MPI_MODE_CREATE, MPI_INFO_NULL, &fh );
    buf = (float *)malloc( nitem * sizeof(float) );

    buf[0] = rand()%4096;
    printf("%lf\n", buf[0]);
    max = min = sum = buf[0];

    for(i=1; i<nitem; i++) {
      t = rand()%4096;
      if (t>max) max = t;
      if (t<min) min = t;
      sum += t;
      buf[i] = t;
      if (i<10) printf("%lf\n", buf[i]);
    }
    
    printf("sizeof(MPI_FLOAT)=%d\n", sizeof(MPI_FLOAT));
    printf ("max=%lf, min=%lf, sum=%lf\n", max, min, sum);

    /* Write to file */
    MPI_File_write( fh, buf, nitem, MPI_FLOAT, &status );

    MPI_Get_count( &status, MPI_INT, &count );
    printf("count=%d\n", count);
    if (count != nitem) {
      //errs++;
      fprintf( stderr, "Wrong count (%d) on write\n", count );
      fflush(stderr);
    }

    MPI_File_close(&fh);
  }
  
  MPI_File_open( comm, fname, MPI_MODE_RDWR, MPI_INFO_NULL, &fh );
  /* Read nothing (check status) */
  memset( &status, 0xff, sizeof(MPI_Status) );

  float *tmp = (float *)malloc( nitem * sizeof(float) );

  stime = MPI_Wtime();
  MPI_File_read(fh, tmp, nitem, MPI_FLOAT, &status);
  etime = MPI_Wtime();
  iotime = etime - stime;
  printf("I/O time = %10.4f sec\n", iotime);

  stime = MPI_Wtime();
  float sum = 0.0;
  for(i=0; i<nitem; i++) {
      sum += tmp[i];
  }
  etime = MPI_Wtime();

  comptime = etime - stime;
  printf("Computation time = %10.4f sec\n", comptime);

  MPI_File_close(&fh);
#if 0  
  MPI_File_open( comm, fname, MPI_MODE_RDWR, MPI_INFO_NULL, &fh );

  MPI_File_read_ex( fh, tmp, nitem, MPI_FLOAT, MPI_MAX, &status );

  printf ("max=%lf\n", tmp[0]);

  MPI_File_close(&fh);
  
  MPI_File_open( comm, fname, MPI_MODE_RDWR, MPI_INFO_NULL, &fh );

  MPI_File_read_ex( fh, tmp, nitem, MPI_FLOAT, MPI_MIN, &status );

  printf ("min=%lf\n", tmp[0]); 

  MPI_File_close(&fh);
  
  MPI_File_open( comm, fname, MPI_MODE_RDWR, MPI_INFO_NULL, &fh );

  MPI_File_read_ex( fh, tmp, nitem, MPI_FLOAT, MPI_SUM, &status );

  printf ("sum=%lf\n", tmp[0]); 
#endif
  free( buf );
  free( tmp );
  MPI_File_close( &fh );
 
  MPI_Finalize();
  return errs;
}
