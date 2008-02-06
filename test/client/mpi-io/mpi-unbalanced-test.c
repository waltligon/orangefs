/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */

/* This is a test from Julian Kunkel that places an unbalanced distributed
 * on the servers.
 * With two servers, the datatype used for the view will place 
 * 64KByte on one server and 128KByte on another.  This should probably
 * be generalized for a better test of this type of workload.
 */
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <assert.h>

int main (int argc, char** argv)
{
    int iter;
    int ret;
    MPI_File fh;
    MPI_Init(&argc, &argv);

    MPI_Aint indices[4];
    MPI_Datatype old_types[4];
    int blocklens[4];
    char fname[255];

    MPI_Datatype dt;

    int total_bytes = 100*1024*1024;

    char * f_buff = malloc( total_bytes );

    if(argc < 2)
    {
        fprintf(stderr, "usage: %s <pvfs test dir>\n", argv[0]);
        exit(1);
    }

    /* creation of datatype */
    blocklens[0] = 1;
    blocklens[1] = 128*1024;
    blocklens[2] = 64*1024;
    blocklens[3] = 1;
    indices[0] = 0;
    indices[1] = 0;
    indices[2] = (128+64)*1024;
    indices[3] = (128+128)*1024;
    old_types[0] = MPI_LB;
    old_types[1] = MPI_BYTE;
    old_types[2] = MPI_BYTE;
    old_types[3] = MPI_UB;

    ret = MPI_Type_struct( 4, blocklens, indices, old_types, & dt );
    assert(ret == 0);

    ret = MPI_Type_commit(& dt);
    assert(ret == 0);

    sprintf(fname, "%s/test.%d", argv[1], rand());
    ret = MPI_File_open( MPI_COMM_WORLD,
                         fname, MPI_MODE_RDWR  | MPI_MODE_CREATE,
                         MPI_INFO_NULL, & fh );
    assert(ret == 0);

    ret = MPI_File_set_view(fh, 0,
                            MPI_BYTE,  /* etype */
                            dt, /* file type */
                            "native", MPI_INFO_NULL);

    assert(ret == 0);
    memset(f_buff, 17, total_bytes );
    for(iter = 0 ; iter < 50; iter ++){
        printf("%d writing %fKByte \n", iter, total_bytes/1024.0f);
        ret = MPI_File_write(
            fh,
            f_buff,
            total_bytes,
            MPI_BYTE,
            MPI_STATUS_IGNORE );
        assert(ret == 0);
    }

    MPI_File_close(& fh);

    MPI_Finalize();

    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
