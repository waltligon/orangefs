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
#include "bench-mem.h"

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
    int im_a_server = 0;
    int num_messages = 0;
    double bmi_time, mpi_time;
    double max_bmi_time, max_mpi_time;
    double min_bmi_time, min_mpi_time;
    double sum_bmi_time, sum_mpi_time;
    double sq_bmi_time, sq_mpi_time;
    double sumsq_bmi_time, sumsq_mpi_time;
    double var_bmi_time, var_mpi_time;
    double stddev_bmi_time, stddev_mpi_time;
    double agg_bmi_bw, agg_mpi_bw;
    double ave_bmi_time, ave_mpi_time;
    int total_data_xfer = 0;
    bmi_context_id context = -1;

    /* start up benchmark environment */
    ret = bench_init(&opts, argc, argv, &num_clients, &world_rank, &comm,
		     &bmi_peer_array, &mpi_peer_array, &context);
    if (ret < 0)
    {
	fprintf(stderr, "bench_init() failure.\n");
	return (-1);
    }

    /* note whether we are a "server" or not */
    if (world_rank < opts.num_servers)
    {
	im_a_server = 1;
    }

    num_messages = opts.total_len / opts.message_len;


    MPI_Barrier(MPI_COMM_WORLD);

    /* reduce seperately among clients and servers to get min 
     * times and max times
     */
    MPI_Allreduce(&mpi_time, &max_mpi_time, 1, MPI_DOUBLE, MPI_MAX, comm);
    MPI_Allreduce(&mpi_time, &min_mpi_time, 1, MPI_DOUBLE, MPI_MIN, comm);
    MPI_Allreduce(&mpi_time, &sum_mpi_time, 1, MPI_DOUBLE, MPI_SUM, comm);
    MPI_Allreduce(&bmi_time, &max_bmi_time, 1, MPI_DOUBLE, MPI_MAX, comm);
    MPI_Allreduce(&bmi_time, &min_bmi_time, 1, MPI_DOUBLE, MPI_MIN, comm);
    MPI_Allreduce(&bmi_time, &sum_bmi_time, 1, MPI_DOUBLE, MPI_SUM, comm);
    sq_bmi_time = bmi_time * bmi_time;
    sq_mpi_time = mpi_time * mpi_time;
    MPI_Allreduce(&sq_bmi_time, &sumsq_bmi_time, 1, MPI_DOUBLE, MPI_SUM, comm);
    MPI_Allreduce(&sq_mpi_time, &sumsq_mpi_time, 1, MPI_DOUBLE, MPI_SUM, comm);

    /* exactly one "client" and one "server" compute and print
     * statistics
     */
    if (world_rank == 0)
    {
	bench_args_dump(&opts);

	total_data_xfer = opts.num_servers * num_clients * num_messages *
	    opts.message_len;
	if (opts.num_servers > 1)
	{
	    var_bmi_time = sumsq_bmi_time -
		sum_bmi_time * sum_bmi_time / (double) (opts.num_servers);
	    var_mpi_time = sumsq_mpi_time -
		sum_mpi_time * sum_mpi_time / (double) (opts.num_servers);
	}
	else
	{
	    var_bmi_time = 0;
	    var_mpi_time = 0;
	}
	ave_bmi_time = sum_bmi_time / opts.num_servers;
	ave_mpi_time = sum_mpi_time / opts.num_servers;
	stddev_bmi_time = sqrt(var_bmi_time);
	stddev_mpi_time = sqrt(var_mpi_time);
	agg_bmi_bw = (double) total_data_xfer / max_bmi_time;
	agg_mpi_bw = (double) total_data_xfer / max_mpi_time;

	printf
	    ("%d %d %f %f %f %f %f (msg_len,servers,min,max,ave,stddev,agg_MB/s) bmi server\n",
	     opts.message_len, opts.num_servers, min_bmi_time, max_bmi_time,
	     ave_bmi_time, stddev_bmi_time, agg_bmi_bw / (1024 * 1024));
	printf
	    ("%d %d %f %f %f %f %f (msg_len,servers,min,max,ave,stddev,agg_MB/s) mpi server\n",
	     opts.message_len, opts.num_servers, min_mpi_time, max_mpi_time,
	     ave_mpi_time, stddev_mpi_time, agg_mpi_bw / (1024 * 1024));
    }

    if (world_rank == opts.num_servers)
    {
	total_data_xfer = opts.num_servers * num_clients * num_messages *
	    opts.message_len;

	if (num_clients > 1)
	{
	    var_bmi_time = sumsq_bmi_time -
		sum_bmi_time * sum_bmi_time / (double) (num_clients);
	    var_mpi_time = sumsq_mpi_time -
		sum_mpi_time * sum_mpi_time / (double) (num_clients);
	}
	else
	{
	    var_bmi_time = 0;
	    var_mpi_time = 0;
	}
	stddev_bmi_time = sqrt(var_bmi_time);
	stddev_mpi_time = sqrt(var_mpi_time);
	agg_bmi_bw = (double) total_data_xfer / max_bmi_time;
	agg_mpi_bw = (double) total_data_xfer / max_mpi_time;
	ave_bmi_time = sum_bmi_time / num_clients;
	ave_mpi_time = sum_mpi_time / num_clients;

	printf
	    ("%d %d %f %f %f %f %f (msg_len,clients,min,max,ave,stddev,agg_MB/s) bmi client\n",
	     opts.message_len, num_clients, min_bmi_time, max_bmi_time,
	     ave_bmi_time, stddev_bmi_time, agg_bmi_bw / (1024 * 1024));
	printf
	    ("%d %d %f %f %f %f %f (msg_len,clients,min,max,ave,stddev,agg_MB/s) mpi client\n",
	     opts.message_len, num_clients, min_mpi_time, max_mpi_time,
	     ave_mpi_time, stddev_mpi_time, agg_mpi_bw / (1024 * 1024));
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
