#ifndef INCLUDE_TESTPVFSDATATYPECONTIG_H
#define INCLUDE_TESTPVFSDATATYPECONTIG_H

#include <mpi.h>
#include <pts.h>

int test_pvfs_datatype_contig(MPI_Comm *mycomm, int myid, char *buf, void *params);

#endif
