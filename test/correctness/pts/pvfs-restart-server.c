/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* 
 * pvfs-restart-server: calls a script that restarts the server
 * Author: Michael Speth
 * Date: 6/19/2003
 */

#include "client.h"
#include <unistd.h>
#include <sys/time.h>
#include "mpi.h"
#include "pts.h"
#include "pvfs-helper.h"

/* Preconditions: Parameters must be valid
 *  * Parameters: comm - special pts communicator, rank - the rank of the process, buf -  * (not used), rawparams - configuration information to specify which function to test * Postconditions: 0 if no errors and nonzero otherwise
 *   */
int pvfs_restart_server(MPI_Comm * comm,
			int rank,
			char *buf,
			void *rawparams)
{
    system("./run-server restart >& server_restart.log");
    /* sleep in seconds to let the server fully start */
    sleep(2);
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
