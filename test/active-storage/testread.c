#include <mpi.h>
#include "subfile.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


int main(int argc, char **argv)
{
    MPI_Datatype newtype;
    int i, ndims, array_of_gsizes[3], array_of_distribs[3];
    int order, nprocs, j, len,k;
    int array_of_dargs[3], array_of_psizes[3];
    double *readbuf;
    int	bufcount, mynod, array_size;
    char *filename;
    int errs=0, toterrs;
    MPI_File fh;
    MPI_Status status;
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
    double t_read_start, t_read, t_max, t_run;
    double t_open_start, t_open;
    double t_close_start, t_close;

    long long int read_amount, io_len;
    int sfnum;
    double io_bandwidth;
    char **tmp;
    MPI_Status mpi_status;
    int ncid; 
    int cube_id;

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
    readbuf = (double *) malloc(bufcount * sizeof(double));
    for (i=0; i<bufcount; i++) readbuf[i] = 1;

    MPI_Type_free(&newtype);


/*************************************************************/
    sfp = (subfile *)malloc (sizeof(subfile)); 

    ndims = 3;
    order = MPI_ORDER_C;

    MPI_Barrier(MPI_COMM_WORLD);
    t_run = MPI_Wtime();
    t_read = 0;
    t_open = 0;
    t_close = 0;
    read_amount = 0;

    k=2;

    for (i=0; i<ndims; i++) {
		if ((array_of_gsizes[i]%array_of_psizes[i]) == 0)
			subsizes[i] = array_of_gsizes[i]/array_of_psizes[i];
		else
			subsizes[i] = array_of_gsizes[i]/array_of_psizes[i] + 1;
 		g_sizes[i] = (MPI_Offset) array_of_gsizes[i];
    }

    starts[0] = subsizes[0] *(mynod / ((array_of_psizes[2]) * (array_of_psizes[1])));
    starts[1] = subsizes[1] *((mynod / (array_of_psizes[2])) % (array_of_psizes[1]));
    starts[2] = subsizes[2] * (mynod % (array_of_psizes[2]));


//    printf("mynod:%d, start:%d, %d, %d\n", mynod, starts[0], starts[1], starts[2]);
//    printf("mynod:%d, subsizes:%d, %d, %d\n", mynod, subsizes[0], subsizes[1], subsizes[2]);
   
   
    subfile_mapping_local2global(MPI_COMM_WORLD, ndims, g_sizes, subsizes, starts, sfp);

//    sprintf(sfilename, "%s%d.",filename,sfnum);
    sprintf(sfilename, "%s.%03d.",filename,k);
//    printf("k:%d, sfilename:%s\n",k, sfilename);



//    printf("sfsizes:%d, %d, %d\n", sfsizes[0], sfsizes[1], sfsizes[2]);

    subfile_mapping_global2subfile(MPI_COMM_WORLD, MPI_ORDER_C, sfsizes, sfp);

    t_open_start = MPI_Wtime();
    subfile_open(MPI_COMM_WORLD, sfilename, 0, MPI_INFO_NULL, sfp, &ncid);
    t_open = t_open + MPI_Wtime() - t_open_start;

//    subfile_set_view(sfp);
  
    t_read_start = MPI_Wtime();
//    subfile_read(sfp, readbuf, 1, MPI_DOUBLE, status);
    subfile_read(sfp, cube_id, readbuf, bufcount, MPI_DOUBLE, mpi_status);
    t_read = t_read + MPI_Wtime() - t_read_start;

    t_close_start = MPI_Wtime();
//    subfile_file_close(sfp);
    t_close = t_close + MPI_Wtime() - t_close_start;
    
//    printf("mynod:%d,t_read:%f, read_len:%d\n",mynod, t_read, read_amount);
   
//    subfile_sync(sfp);    


    subfile_close(sfp);
	


    t_run = MPI_Wtime()  - t_run;
    
    MPI_Reduce(&t_open, &t_max, 1, MPI_DOUBLE, MPI_MAX,
                     0, MPI_COMM_WORLD);
    if (mynod == 0){
	    t_open = t_max;
// 	    printf("The max open time:%d,t_write:%f\n",mynod, t_open);
    }

    MPI_Reduce(&t_read, &t_max, 1, MPI_DOUBLE, MPI_MAX,
                     0, MPI_COMM_WORLD);
    if (mynod == 0){
	    t_read = t_max;
 	    printf("The max read time:%f\n", t_read);
    }


    MPI_Reduce(&t_close, &t_max, 1, MPI_DOUBLE, MPI_MAX,
                     0, MPI_COMM_WORLD);
    if (mynod == 0){
	    t_close = t_max;
// 	    printf("The max close time:%d,t_close:%f\n",mynod, t_close);
    }

    MPI_Reduce(&t_run, &t_max, 1, MPI_DOUBLE, MPI_MAX,
                     0, MPI_COMM_WORLD);
    if (mynod == 0){
	    t_run = t_max;
// 	    printf("The max write time:%d,t_write:%f\n",mynod, t_write);
    }


    MPI_Reduce(&read_amount, &io_len, 1, MPI_LONG, MPI_SUM,
                     0, MPI_COMM_WORLD);
    if (mynod == 0){
   	    printf("The total of processes:%d, array size: %d X %d X %d, subfile size: %d X %d X %d\n",
			 nprocs, array_of_gsizes[0], array_of_gsizes[1],array_of_gsizes[2],
			 sfsizes[0], sfsizes[1], sfsizes[2]);
 //  	    printf("The total read length:%ld bytes, open time:%f, read time:%f, close time:%f, run time:%f\n", io_len, t_open, t_read, t_close, t_run);
	    io_bandwidth = (io_len / 1048576.0)/t_run;
//   	    printf("The total bandwidth is %fMB/S\n", io_bandwidth);
//   	    printf("%f\t, %f\t, %f\t, %f\t, %f\n", t_open, t_read, t_close, t_run, io_bandwidth);
   	    printf("resulti read: %dX%dX%d, %f\t, %f\t, %f\t, %f\t, %f\n", sfsizes[0], sfsizes[1], sfsizes[2], t_open, t_read, t_close, t_run, io_bandwidth);
    }

    

    double sum = 0;
    for (i=0; i<bufcount; i++) { 
	sum += readbuf[i];
    }
    printf("read rank:%d, sum:%f\n", mynod, sum);

    free(readbuf);
    free(filename);

    MPI_Finalize();
    return 0;
}

