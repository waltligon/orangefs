#ifndef INCLUDE_TESTPVFSDATATYPEHVECTOR_H
#define INCLUDE_TESTPVFSDATATYPEHVECTOR_H

#include <mpi.h>
#include <pts.h>

int test_pvfs_datatype_hvector(MPI_Comm *mycomm, int myid, char *buf, void *params);

#endif
