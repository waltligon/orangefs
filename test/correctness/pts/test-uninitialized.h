#ifndef INCLUDE_UNINITIALIZED_H
#define INCLUDE_UNINITIALIZED_H

int test_uninitialized(MPI_Comm * comm,
		       int rank,
		       char *buf,
		       void *rawparams);

#endif
