#ifndef PVFS_RESTART_SERVER_H
#define PVFS_RESTART_SERVER_H

int pvfs_restart_server(MPI_Comm *comm, int rank,  char *buf, void *rawparams);

#endif
