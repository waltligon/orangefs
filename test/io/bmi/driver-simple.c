/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "gossip.h"
#include <mpi.h>
#include "bmi.h"
#include "bench-initialize.h"
#include "bench-args.h"

int main(
    int argc,
    char *argv[])
{
    int ret = -1;
    int world_rank = 0;
    MPI_Comm comm;
    PVFS_BMI_addr_t *bmi_peer_array;
    int *mpi_peer_array;
    int num_clients;
    struct bench_options opts;
    int i = 0;
    bmi_context_id context;

    /* start up benchmark environment */
    ret = bench_init(&opts, argc, argv, &num_clients, &world_rank, &comm,
		     &bmi_peer_array, &mpi_peer_array, &context);
    if (ret < 0)
    {
	fprintf(stderr, "bench_init() failure.\n");
	return (-1);
    }

    /* print some diagnostics */
    if (world_rank < opts.num_servers)
    {
	printf("server: %d, talks to: \n", world_rank);
	for (i = 0; i < num_clients; i++)
	{
	    printf("%d ", mpi_peer_array[i]);
	}
	printf("\n");
    }
    else
    {
	printf("client: %d, talks to: \n", world_rank);
	for (i = 0; i < opts.num_servers; i++)
	{
	    printf("%d ", mpi_peer_array[i]);
	}
	printf("\n");
    }

    /* shutdown interfaces */
    BMI_close_context(context);
    BMI_finalize();
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
