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
        "       -r record_size : record size in bytes\n"
        "       -p search pattern \n"
        "       -t test normal storage module\n"
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
    int _debug, use_gen_file = 0, use_actsto = 0, use_normalsto = 0;
    int use_gpu = 0;

    MPI_Offset disp, offset, file_size;
    MPI_Datatype etype, ftype, buftype;
    MPI_Info info;

    int errs = 0;
    int nprocs, rank, i, count;
    char *fname = NULL;
    MPI_File fh;
    MPI_Comm comm;
    MPI_Status status;
    int64_t nitem = 0;
    int fsize = 0, type_size, rsize = 0, nrecord = 0;
    double stime, etime, iotime, comptime, elapsed_time;
    double max_iotime, max_comptime;
    char *search_string = NULL;

    MPI_Init( &argc, &argv );
 
    comm = MPI_COMM_WORLD;

    MPI_Comm_size( comm, &nprocs );
    MPI_Comm_rank( comm, &rank );
 
    while ( (opt=getopt(argc,argv,"i:s:r:p:godhxt"))!= EOF) {
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
        case 'r': rsize = atoi(optarg);
            break;
        case 'p': search_string = optarg;
            break;
        case 's': fsize = atoi(optarg);
            if (rank ==0) printf("fsize = %d (MB)\n", fsize);
            if (fsize == 0)
                nitem = 0;
            else {
                MPI_Type_size(MPI_CHAR, &type_size);
                nitem = fsize*1024; 
                nitem = nitem*1024;
                nitem = nitem/type_size;
                nitem = nitem/nprocs;
            }
            break;
        case 'x': use_actsto = 1;
            break;
        case 't': use_normalsto = 1;
            break;
        default: is_print_usage = 1;
            break;
        }
    }

    if (fname == NULL || is_print_usage == 1 || 
        nitem == 0 || rsize == 0 || search_string == NULL) {
        if (rank == 0) usage(argv[0]);
        MPI_Finalize();
        exit(1);
    }

    nrecord = nitem/rsize;

    if (rank == 0) {
        printf("nitem = %d\n", nitem);
        printf("num of records = %d\n", nrecord);
        printf("type_size (MPI_CHAR) = %d\n", type_size);
    }

    char *buf = (char *)malloc( nitem * sizeof(char) );
   
    if (use_normalsto == 1)
    {
        int result;

        MPI_File_open (comm, fname, MPI_MODE_RDONLY, MPI_INFO_NULL, &fh);
        memset (&status, 0xff, sizeof(MPI_Status));

        /* Set the file view */
        disp = rank * nitem * type_size;
        printf("%d: disp = %lld\n", rank, disp);
        etype = MPI_CHAR;
        ftype = MPI_CHAR;

        result = MPI_File_set_view(fh, disp, etype, ftype, "native", MPI_INFO_NULL);

        if(result != MPI_SUCCESS) 
            sample_error(result, "MPI_File_set_view");

        offset = rank * nitem * type_size;
        printf("%d: offset=%d, nitem=%d\n", rank , offset, nitem);

        stime = MPI_Wtime();
        MPI_File_read_at(fh, offset, buf, nitem, MPI_CHAR, &status);
        etime = MPI_Wtime();
        iotime = etime - stime;

        if(_debug==1) printf("%d: iotime = %10.4f\n", rank, iotime);
        MPI_Reduce(&iotime, &max_iotime, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
        
        char myString[rsize];
        char *tmp = buf;
        
        stime = MPI_Wtime();
        
        for(i=0; i<nrecord; i++) {
            strncpy(myString, tmp, rsize);
            if(strstr(myString, search_string)) {
                printf("Found by rank(%d): %s\n", rank, myString);
                fflush(stdout);
            }
            tmp += rsize;
        }
        etime = MPI_Wtime();
        
        comptime = etime - stime;
        
        MPI_Reduce(&comptime, &max_comptime, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

        if(rank == 0) {
            elapsed_time = max_comptime + max_iotime;
            printf("<<Result (grep) with normal read>>\n"
                   "Grep time        = %10.4f sec\n"
                   "I/O time         = %10.4f sec\n"
                   "total time       = %10.4f sec\n\n", max_comptime, max_iotime, elapsed_time);
        }
    } /* normal I/O */
  
    MPI_File_close(&fh);

    if(use_actsto == 1) {
        /* MPI_GREP */
        MPI_Info_create(&info);
        
        char tmp_str[80];
        sprintf(tmp_str, "%d:%d:%s", use_gpu, rsize, search_string);
        MPI_Info_set(info, "grep", tmp_str);

        MPI_File_open( comm, fname, MPI_MODE_RDWR, info, &fh );
        
        stime = MPI_Wtime();
        MPI_File_read_at_ex( fh, offset, buf, nitem, MPI_CHAR, MPI_GREP, &status );
        etime = MPI_Wtime();
        elapsed_time = etime-stime;
        if(rank == 0)
            printf ("<<Result (grep) with active storage>>\n"
                    "(in %10.4f sec)\n", elapsed_time);
    
        MPI_File_close(&fh);
    }
    
    free( buf );
    MPI_Finalize();
    return 0;
}
