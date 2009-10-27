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
    "       -g : use generated file\n"
    "       -s size : file size to generate in MB\n"
    "       -x : test active storage module\n";
  fprintf(stderr, help, argv0);
}

void sample_error(int error, char *string)
{
  fprintf(stderr, "Error %d in %s\n", error, string);
  MPI_Finalize();
  exit(-1);
}

int main( int argc, char *argv[] )
{
  int opt;
  extern char   *optarg;
  extern int     optind;
  int is_output_timing=0, is_print_usage = 0;
  int _debug=0, use_gen_file = 0, use_actsto = 0;

  MPI_Offset disp, offset, file_size;
  MPI_Datatype etype, ftype, buftype;

  int errs = 0;
  int size, rank, i, count;
  char *fname = NULL;
  double *buf;
  MPI_File fh;
  MPI_Comm comm;
  MPI_Status status;
  int64_t nitem = 0;
  int fsize = 0, type_size;
  double stime, etime, iotime, comptime, elapsed_time;
  double max_iotime, max_comptime;

  double max, min, sum=0.0, global_sum;

  MPI_Init( &argc, &argv );
 
  comm = MPI_COMM_WORLD;

  MPI_Comm_size( comm, &size );
  MPI_Comm_rank( comm, &rank );
 
  while ( (opt=getopt(argc,argv,"i:s:godhx"))!= EOF) {
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
    case 's': fsize = atoi(optarg);
      if (rank ==0) printf("fsize = %d (MB)\n", fsize);
      if (fsize == 0)
	nitem = 0;
      else {
	MPI_Type_size(MPI_DOUBLE, &type_size);
	nitem = fsize*1024; 
	nitem = nitem*1024;
	nitem = nitem/type_size;
	//printf("nitem=%lld\n", nitem);
	nitem = nitem/size;
      }
      if (rank == 0) printf("nitem = %d\n", nitem);
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
    int t, result;

    MPI_File_open( comm, fname, MPI_MODE_RDWR | MPI_MODE_CREATE, MPI_INFO_NULL, &fh );

    /* Set the file view */
    disp = rank * nitem * type_size;;
    etype = MPI_DOUBLE;
    ftype = MPI_DOUBLE;
    result = MPI_File_set_view(fh, disp, etype, ftype, "native", MPI_INFO_NULL);

    if(result != MPI_SUCCESS) 
      sample_error(result, "MPI_File_set_view");

    buf = (double *)malloc( nitem * sizeof(double) );

    buf[0] = rand()%4096;
    if(rank==0) printf("%lf\n", buf[0]);
    max = min = sum = buf[0];

    for(i=1; i<nitem; i++) {
      t = rand()%4096;
      if (t>max) max = t;
      if (t<min) min = t;
      sum += t;
      buf[i] = t;
      if (i<10 && rank == 0) printf("%lf\n", buf[i]);
    }
    
    if(rank == 0) {
      printf("MPI_Type_size(MPI_DOUBLE)=%d\n", type_size);
      printf ("max=%lf, min=%lf, sum=%lf\n", max, min, sum);
    }

    /* Write to file */
    MPI_File_write_all( fh, buf, nitem, MPI_DOUBLE, &status );

    MPI_Get_count( &status, MPI_DOUBLE, &count );
    //printf("count=%d\n", count);
    if (count != nitem) {
      fprintf( stderr, "%d: Wrong count (%d) on write\n", rank, count );
      fflush(stderr);
      /* exit */
      MPI_Finalize();
      exit(1);
    }

    if(rank == 0) printf("File is written\n\n");
    MPI_File_close(&fh);
  }
  
  MPI_File_open( comm, fname, MPI_MODE_RDWR, MPI_INFO_NULL, &fh );
  /* Read nothing (check status) */
  memset( &status, 0xff, sizeof(MPI_Status) );

  double *tmp = (double *)malloc( nitem * sizeof(double) );
  offset = rank * nitem * type_size;

  /* start I/O */
  stime = MPI_Wtime();
  MPI_File_read_at(fh, offset, tmp, nitem, MPI_DOUBLE, &status);
  etime = MPI_Wtime();
  /* end I/O */
  iotime = etime - stime;

  if(_debug==1) printf("%d: iotime = %10.4f\n", rank, iotime);
  MPI_Reduce(&iotime, &max_iotime, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
 
  sum = 0.0; /* reset sum */

  /* start computation */
  stime = MPI_Wtime();

  for(i=0; i<nitem; i++) {
      sum += tmp[i];
  }

  MPI_Reduce(&sum, &global_sum, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  etime = MPI_Wtime();
  /* end computation */

  comptime = etime - stime;

  if(_debug==1) printf("%d: comptime = %10.4f\n", rank, comptime);

  MPI_Reduce(&comptime, &max_comptime, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

  if(rank == 0) {
      elapsed_time = max_comptime + max_iotime;
      printf("<<Result (SUM) with normal read>>\n"
             "SUM              = %10.4f \n"
             "Computation time = %10.4f sec\n"
             "I/O time         = %10.4f sec\n"
             "total time       = %10.4f sec\n\n", 
             global_sum, max_comptime, max_iotime, elapsed_time);
  }
      
  MPI_File_close(&fh);

  if(use_actsto == 1) {
    /* MPI_MAX */
    MPI_File_open( comm, fname, MPI_MODE_RDWR, MPI_INFO_NULL, &fh );

    stime = MPI_Wtime();
    MPI_File_read_at_ex( fh, offset, tmp, nitem, MPI_DOUBLE, MPI_MAX, &status );
    etime = MPI_Wtime();
    elapsed_time = etime-stime;
    printf ("<<Result with active storage>>\n"
	    "max=%lf (in %10.4f sec)\n", tmp[0], elapsed_time);
    
    MPI_File_close(&fh);
    
    /* MPI_MIN */
    MPI_File_open( comm, fname, MPI_MODE_RDWR, MPI_INFO_NULL, &fh );
    
    stime = MPI_Wtime();
    MPI_File_read_at_ex( fh, offset, tmp, nitem, MPI_DOUBLE, MPI_MIN, &status );
    etime = MPI_Wtime();
    elapsed_time = etime - stime;
    printf ("min=%lf (in %10.4f sec)\n", tmp[0], elapsed_time); 
    
    MPI_File_close(&fh);
    
    /* MPI_SUM */
    MPI_File_open( comm, fname, MPI_MODE_RDWR, MPI_INFO_NULL, &fh );
    
    stime = MPI_Wtime();
    MPI_File_read_at_ex( fh, offset, tmp, nitem, MPI_DOUBLE, MPI_SUM, &status );
    etime = MPI_Wtime();
    elapsed_time = etime - stime;
    printf ("sum=%lf (in %10.4f sec)\n", tmp[0], elapsed_time); 
    
    MPI_File_close( &fh );
  }
  free( buf );
  free( tmp );
 
  MPI_Finalize();
  return errs;
}
