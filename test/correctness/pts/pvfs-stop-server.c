/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* 
 * pvfs-stop-server: calls a script that stops the server
 * Author: Michael Speth
 * Date: 6/19/2003
 * Tab Size: 3
 */

#include <client.h>
#include <sys/time.h>
#include "mpi.h"
#include "pts.h"
#include "pvfs-helper.h"

/* Preconditions: Parameters must be valid
 *  * Parameters: comm - special pts communicator, rank - the rank of the process, buf -  * (not used), rawparams - configuration information to specify which function to test * Postconditions: 0 if no errors and nonzero otherwise
 *   */
int pvfs_stop_server(MPI_Comm *comm, int rank, char *buf, void *rawparams){
	system("./run-server stop >& server_stop.log");
}
