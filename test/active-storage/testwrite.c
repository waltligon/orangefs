#include <mpi.h>
#include "subfile.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pnetcdf.h>
static void handle_error(int status);

static void handle_error(int status) {
	  fprintf(stderr, "%s\n", ncmpi_strerror(status));
}

int main(int argc, char **argv)
{
    MPI_Datatype newtype;
    int i, ndims; 
    int array_of_gsizes[3];
    int array_of_distribs[3];
    int order, nprocs, j, len,k;
    int array_of_dargs[3], array_of_psizes[3];
    double *writebuf;
    double *tmpbuf;
    int	bufcount;
    int mynod, array_size;
    char *filename;
    int errs=0, toterrs;
    MPI_File fh;
    MPI_Status mpi_status;
    MPI_Request request;
    MPI_Info info = MPI_INFO_NULL;
    int errcode;
    char sfilename[200]; 
    char sfilename1[200]; 
    MPI_Offset g_sizes[3];
    MPI_Offset sfsizes[3];
    MPI_Offset subsizes[3];
    MPI_Offset starts[3];
    int slice[3]; 
    int mode;
    subfile *sfp;
    subfile *sfp1;
    char *path;
    int status;
    int ncid;
    int dimid1, dimid2, dimid3;
    int cube_dim[3];
    int cube_id;
    MPI_Offset counts[3] = {50, 50, 50};


    int k_loop;
    MPI_Offset write_amount, *io_len;
    int sfnum;
    double io_bandwidth;
    char **tmp;
 

    MPI_Init(&argc,&argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &mynod);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
     
    tmp = argv;

    array_of_gsizes[0] =atoi(argv[1]);
    array_of_gsizes[1] =atoi(argv[2]);
    array_of_gsizes[2] =atoi(argv[3]);
    
    array_of_psizes[0] =atoi(argv[4]);
    array_of_psizes[1] =atoi(argv[5]);
    array_of_psizes[2] =atoi(argv[6]);
  
    sfsizes[0] = atoi(argv[7]);
    sfsizes[1] = atoi(argv[8]);
    sfsizes[2] = atoi(argv[9]);
//    printf("g_sizes:%d, %d, %d\n", array_of_gsizes[0], array_of_gsizes[1], array_of_gsizes[2]);

 if (!mynod) {
        i = 1;
        while ((i < argc) && strcmp("-fname", *argv)) {
            i++;
            argv++;
        }
        if (i >= argc) {
            fprintf(stderr, "\n*#  Usage: test -sfnum subfile_number -fname filename\n\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        argv++;
        len = strlen(*argv);
        filename = (char *) malloc(len+1);
        strcpy(filename, *argv);
        MPI_Bcast(&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Bcast(filename, len+1, MPI_CHAR, 0, MPI_COMM_WORLD);

    }
    else {
        MPI_Bcast(&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
        filename = (char *) malloc(len+1);
        MPI_Bcast(filename, len+1, MPI_CHAR, 0, MPI_COMM_WORLD);
    }
   
/************************************************************/

    ndims = 3;
    order = MPI_ORDER_C;

    array_of_distribs[0] = MPI_DISTRIBUTE_BLOCK;
    array_of_distribs[1] = MPI_DISTRIBUTE_BLOCK;
    array_of_distribs[2] = MPI_DISTRIBUTE_BLOCK;

    array_of_dargs[0] = MPI_DISTRIBUTE_DFLT_DARG;
    array_of_dargs[1] = MPI_DISTRIBUTE_DFLT_DARG;
    array_of_dargs[2] = MPI_DISTRIBUTE_DFLT_DARG;

//    for (i=0; i<ndims; i++) array_of_psizes[i] = 0;
//    MPI_Dims_create(nprocs, ndims, array_of_psizes);
    if (mynod==0) {
	for (i=0; i<ndims; i++) {
//       	    printf ("arrary_of_psizes[%d]:%d\n", i, array_of_psizes[i]);
	}
    }

    MPI_Type_create_darray(nprocs, mynod, ndims, array_of_gsizes,
                           array_of_distribs, array_of_dargs,
                           array_of_psizes, order, MPI_DOUBLE, &newtype);
    MPI_Type_commit(&newtype);


    MPI_Type_size(newtype, &bufcount);
//    printf("bufcount :%d, sizeof(double):%d\n", bufcount,sizeof(double));
    bufcount = bufcount/sizeof(double);
//    printf("bufcount/sizeof(double) :%d\n", bufcount);
    writebuf = (double *) malloc(bufcount * sizeof(double));
//    printf("bufcount:%d\n", bufcount);
    double sum = 0;
    for (i=0; i<bufcount; i++) { 
	writebuf[i] = 1.0*( mynod + 1 );
	sum += writebuf[i];
    }
    printf("write rank:%d, sum:%f\n", mynod, sum);
	    /*
    array_size = array_of_gsizes[0]*array_of_gsizes[1]*array_of_gsizes[2];
    tmpbuf = (double *) calloc(array_size, sizeof(double));
    MPI_Irecv(tmpbuf, 1, newtype, mynod, 10, MPI_COMM_WORLD, &request);
    MPI_Send(writebuf, bufcount, MPI_DOUBLE, mynod, 10, MPI_COMM_WORLD);
    MPI_Wait(&request, &status);

    j = 0;
    for (i=0; i<array_size; i++)
        if (tmpbuf[i]) {
            writebuf[j] = i*1.0;
            j++;
        }
    free(tmpbuf);
*/
    MPI_Type_free(&newtype);
/*
//    printf("j=%d<<<<<<<<<<<<<<<<<>>>>>>>>>>>>>>>>>>>>mynod:%d\n",j, mynod);
    if (j != bufcount) {
        fprintf(stderr, "Error in initializing writebuf on process %d\n", mynod);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
*/
/*************************************************************/
    sfp = (subfile *)malloc (sizeof(subfile)); 

    ndims = 3;
    order = MPI_ORDER_C;

    MPI_Barrier(MPI_COMM_WORLD);
    k=k_loop = 2;

    for (i=0; i<ndims; i++) {
		if ((array_of_gsizes[i]%array_of_psizes[i]) == 0)
			subsizes[i] = array_of_gsizes[i]/array_of_psizes[i];
		else
			subsizes[i] = array_of_gsizes[i]/array_of_psizes[i] + 1;
     		g_sizes[i] = (MPI_Offset)array_of_gsizes[i];
      }

      starts[0] = subsizes[0] *(mynod / ((array_of_psizes[2]) * (array_of_psizes[1])));
      starts[1] = subsizes[1] *((mynod / (array_of_psizes[2])) % (array_of_psizes[1]));
      starts[2] = subsizes[2] * (mynod % (array_of_psizes[2]));


      subfile_mapping_local2global(MPI_COMM_WORLD, ndims, g_sizes, subsizes, starts, sfp);

      subfile_mapping_global2subfile(MPI_COMM_WORLD, MPI_ORDER_C, sfsizes, sfp);

      sprintf(sfilename, "%s.%03d.",filename,k);
      subfile_create(MPI_COMM_WORLD, sfilename, 0, MPI_INFO_NULL, sfp, &ncid);
//      printf("subfile open ncid:%d\n", ncid);
      ncmpi_def_dim(ncid, "x", sfsizes[0], &dimid1);
      ncmpi_def_dim(ncid, "y", sfsizes[1], &dimid2);
      ncmpi_def_dim(ncid, "z", sfsizes[2], &dimid3);
      

      cube_dim[0] = dimid1;
      cube_dim[1] = dimid2;
      cube_dim[2] = dimid3;
      ncmpi_def_var (ncid, "cube", NC_DOUBLE, 3, cube_dim, &cube_id);
      ncmpi_put_att_int(ncid, cube_id, "global_size", NC_INT, 3, array_of_gsizes);


      ncmpi_enddef(ncid);

  
      subfile_write(sfp, cube_id, writebuf, bufcount, MPI_DOUBLE, mpi_status);


      subfile_close(sfp);
	
    if (mynod == 0) {
        printf("finish the subfiling write test\n");
    }

    free(writebuf);
    free(filename);

    MPI_Finalize();
    return 0;
}

