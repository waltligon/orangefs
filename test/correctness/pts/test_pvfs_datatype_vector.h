#ifndef INCLUDE_TESTPVFSDATATYPEVECTOR_H
#define INCLUDE_TESTPVFSDATATYPEVECTOR_H

#include <mpi.h>
#include <pts.h>

int test_pvfs_datatype_vector(MPI_Comm *mycomm, int myid, char *buf, void *params);

#endif
