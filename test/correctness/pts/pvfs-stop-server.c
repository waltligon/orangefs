/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* 
 * pvfs-stop-server: calls a script that stops the server
 * Author: Michael Speth
 * Date: 6/19/2003
 */

#include <stdlib.h>
#include <sys/time.h>

#include "client.h"
#include "mpi.h"
#include "pts.h"
#include "pvfs-helper.h"
#include "pvfs-stop-server.h"

/* Preconditions: Parameters must be valid
 *  * Parameters: comm - special pts communicator, rank - the rank of the process, buf -  * (not used), rawparams - configuration information to specify which function to test * Postconditions: 0 if no errors and nonzero otherwise
 *   */
int pvfs_stop_server(MPI_Comm * comm __unused,
		     int rank __unused,
		     char *buf __unused,
		     void *rawparams __unused)
{
    system("./run-server stop >& server_stop.log");
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
