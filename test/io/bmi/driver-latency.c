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

#define ITERATIONS 1000

static int bmi_server(
    struct bench_options *opts,
    struct mem_buffers *bmi_recv_bufs,
    struct mem_buffers *bmi_send_bufs,
    PVFS_BMI_addr_t addr,
    enum bmi_buffer_type buffer_type,
    double *wtime,
    bmi_context_id context);
static int bmi_client(
    struct bench_options *opts,
    struct mem_buffers *bmi_recv_bufs,
    struct mem_buffers *bmi_send_bufs,
    PVFS_BMI_addr_t addr,
    enum bmi_buffer_type buffer_type,
    double *wtime,
    bmi_context_id context);
static int mpi_server(
    struct bench_options *opts,
    struct mem_buffers *mpi_recv_bufs,
    struct mem_buffers *mpi_send_bufs,
    int addr,
    double *wtime);
static int mpi_client(
    struct bench_options *opts,
    struct mem_buffers *mpi_recv_bufs,
    struct mem_buffers *mpi_send_bufs,
    int addr,
    double *wtime);

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
    struct mem_buffers mpi_send_bufs;
    struct mem_buffers mpi_recv_bufs;
    struct mem_buffers bmi_send_bufs;
    struct mem_buffers bmi_recv_bufs;
    enum bmi_buffer_type buffer_type = BMI_EXT_ALLOC;
    double mpi_time, bmi_time;
    bmi_context_id context;

    /* start up benchmark environment */
    ret = bench_init(&opts, argc, argv, &num_clients, &world_rank, &comm,
		     &bmi_peer_array, &mpi_peer_array, &context);
    if (ret < 0)
    {
	fprintf(stderr, "bench_init() failure.\n");
	return (-1);
    }

    /* verify that we didn't get any weird parameters */
    if (num_clients > 1 || opts.num_servers > 1)
    {
	fprintf(stderr, "Too many procs specified.\n");
	return (-1);
    }

    /* setup MPI buffers */
    ret = alloc_buffers(&mpi_send_bufs, ITERATIONS, opts.message_len);
    ret += alloc_buffers(&mpi_recv_bufs, ITERATIONS, opts.message_len);
    if (ret < 0)
    {
	fprintf(stderr, "alloc_buffers() failure.\n");
	return (-1);
    }

    /* setup BMI buffers (differs depending on command line args) */
    if (opts.flags & BMI_ALLOCATE_MEMORY)
    {
	buffer_type = BMI_PRE_ALLOC;
	ret = BMI_alloc_buffers(&bmi_send_bufs, ITERATIONS, opts.message_len,
				bmi_peer_array[0], BMI_SEND);
	ret += BMI_alloc_buffers(&bmi_recv_bufs, ITERATIONS, opts.message_len,
				 bmi_peer_array[0], BMI_RECV);
	if (ret < 0)
	{
	    fprintf(stderr, "BMI_alloc_buffers() failure.\n");
	    return (-1);
	}
    }
    else
    {
	buffer_type = BMI_EXT_ALLOC;
	ret = alloc_buffers(&bmi_send_bufs, ITERATIONS, opts.message_len);
	ret += alloc_buffers(&bmi_recv_bufs, ITERATIONS, opts.message_len);
	if (ret < 0)
	{
	    fprintf(stderr, "alloc_buffers() failure.\n");
	    return (-1);
	}
    }

    /* mark all send buffers */
    ret = mark_buffers(&bmi_send_bufs);
    ret += mark_buffers(&mpi_send_bufs);
    if (ret < 0)
    {
	fprintf(stderr, "mark_buffers() failure.\n");
	return (-1);
    }

	/******************************************************************/
    /* Actually measure some stuff */

    /* BMI series */
    if (world_rank == 0)
    {
	ret = bmi_server(&opts, &bmi_recv_bufs, &bmi_send_bufs,
			 bmi_peer_array[0], buffer_type, &bmi_time, context);
    }
    else
    {
	ret = bmi_client(&opts, &bmi_recv_bufs, &bmi_send_bufs,
			 bmi_peer_array[0], buffer_type, &bmi_time, context);
    }
    if (ret < 0)
    {
	return (-1);
    }

    /* MPI series */
    if (world_rank == 0)
    {
	ret = mpi_server(&opts, &mpi_recv_bufs, &mpi_send_bufs,
			 mpi_peer_array[0], &mpi_time);
    }
    else
    {
	ret = mpi_client(&opts, &mpi_recv_bufs, &mpi_send_bufs,
			 mpi_peer_array[0], &mpi_time);
    }
    if (ret < 0)
    {
	return (-1);
    }


	/******************************************************************/
#if 0
    if (!(opts.flags & REUSE_BUFFERS))
    {
	/* verify received buffers */
	ret = check_buffers(&mpi_recv_bufs);
	if (ret < 0)
	{
	    fprintf(stderr, "MPI buffer verification failed.\n");
	    return (-1);
	}
	ret = check_buffers(&bmi_recv_bufs);
	if (ret < 0)
	{
	    fprintf(stderr, "BMI buffer verification failed.\n");
	    return (-1);
	}
    }
#endif
    /* print out results */


    if (world_rank == 0)
    {
	bench_args_dump(&opts);
	printf("number of iterations: %d\n", ITERATIONS);
	printf
	    ("all times measure round trip in seconds unless otherwise noted\n");
	printf("\"ave\" field is computed as (total time)/iterations\n");
    }

    /* enforce output ordering */
    fflush(stdout);
    MPI_Barrier(MPI_COMM_WORLD);

    if (world_rank != 0)
    {
	printf("%d\t%f\t%f\t(size,total,ave)", bmi_recv_bufs.size,
	       bmi_time, (bmi_time / ITERATIONS));
	printf(" bmi server\n");

	printf("%d\t%f\t%f\t(size,total,ave)", mpi_recv_bufs.size,
	       mpi_time, (mpi_time / ITERATIONS));
	printf(" mpi server\n");
    }

    /* enforce output ordering */
    fflush(stdout);
    MPI_Barrier(MPI_COMM_WORLD);

    if (world_rank == 0)
    {
	printf("%d\t%f\t%f\t(size,total,ave)", bmi_recv_bufs.size,
	       bmi_time, (bmi_time / ITERATIONS));
	printf(" bmi client\n");

	printf("%d\t%f\t%f\t(size,total,ave)", mpi_recv_bufs.size,
	       mpi_time, (mpi_time / ITERATIONS));
	printf(" mpi client\n");
    }
    /* free buffers */
    free_buffers(&mpi_send_bufs);
    free_buffers(&mpi_recv_bufs);

    if (opts.flags & BMI_ALLOCATE_MEMORY)
    {
	BMI_free_buffers(&bmi_send_bufs, bmi_peer_array[0], BMI_SEND);
	BMI_free_buffers(&bmi_recv_bufs, bmi_peer_array[0], BMI_RECV);
    }
    else
    {
	free_buffers(&bmi_send_bufs);
	free_buffers(&bmi_recv_bufs);
    }

    /* shutdown interfaces */
    BMI_close_context(context);
    BMI_finalize();
    MPI_Finalize();
    return 0;
}

static int bmi_server(
    struct bench_options *opts,
    struct mem_buffers *bmi_recv_bufs,
    struct mem_buffers *bmi_send_bufs,
    PVFS_BMI_addr_t addr,
    enum bmi_buffer_type buffer_type,
    double *wtime,
    bmi_context_id context)
{
    int i = 0;
    bmi_size_t actual_size;
    bmi_op_id_t bmi_id;
    void *send_buffer = NULL;
    void *recv_buffer = NULL;
    int ret = -1;
    int outcount;
    int error_code;
    double time1, time2;

    MPI_Barrier(MPI_COMM_WORLD);
    time1 = MPI_Wtime();

    for (i = 0; i < ITERATIONS; i++)
    {
	/* set buffer to use */
	if (opts->flags & REUSE_BUFFERS)
	{
	    recv_buffer = bmi_recv_bufs->buffers[0];
	    send_buffer = bmi_send_bufs->buffers[0];
	}
	else
	{
	    recv_buffer = bmi_recv_bufs->buffers[i];
	    send_buffer = bmi_send_bufs->buffers[i];
	}

	/* receive a message */
	error_code = 0;
	ret = BMI_post_recv(&bmi_id, addr, recv_buffer,
			    bmi_recv_bufs->size, &actual_size, buffer_type, 0,
			    NULL, context);
	if (ret == 0)
	{
	    do
	    {
		ret = BMI_test(bmi_id, &outcount, &error_code, &actual_size,
			       NULL, 0, context);
	    } while (ret == 0 && outcount == 0);
	}
	if (ret < 0 || error_code != 0)
	{
	    fprintf(stderr, "Server: BMI recv error.\n");
	    return (-1);
	}

	/* send a message */
	error_code = 0;
	ret = BMI_post_send(&bmi_id, addr, send_buffer,
			    bmi_send_bufs->size, buffer_type, 0, NULL, context);
	if (ret == 0)
	{
	    do
	    {
		ret = BMI_test(bmi_id, &outcount, &error_code, &actual_size,
			       NULL, 0, context);
	    } while (ret == 0 && outcount == 0);
	}
	if (ret < 0 || error_code != 0)
	{
	    fprintf(stderr, "Server: BMI send error.\n");
	    return (-1);
	}
    }

    time2 = MPI_Wtime();

    *wtime = time2 - time1;
    return (0);
}


static int bmi_client(
    struct bench_options *opts,
    struct mem_buffers *bmi_recv_bufs,
    struct mem_buffers *bmi_send_bufs,
    PVFS_BMI_addr_t addr,
    enum bmi_buffer_type buffer_type,
    double *wtime,
    bmi_context_id context)
{
    int i = 0;
    bmi_size_t actual_size;
    bmi_op_id_t bmi_id;
    void *send_buffer = NULL;
    void *recv_buffer = NULL;
    int ret = -1;
    int outcount;
    int error_code;
    double time1, time2;

    MPI_Barrier(MPI_COMM_WORLD);
    time1 = MPI_Wtime();
    for (i = 0; i < ITERATIONS; i++)
    {
	/* set buffer to use */
	if (opts->flags & REUSE_BUFFERS)
	{
	    recv_buffer = bmi_recv_bufs->buffers[0];
	    send_buffer = bmi_send_bufs->buffers[0];
	}
	else
	{
	    recv_buffer = bmi_recv_bufs->buffers[i];
	    send_buffer = bmi_send_bufs->buffers[i];
	}

	/* send a message */
	error_code = 0;
	ret = BMI_post_send(&bmi_id, addr, send_buffer,
			    bmi_send_bufs->size, buffer_type, 0, NULL, context);
	if (ret == 0)
	{
	    do
	    {
		ret = BMI_test(bmi_id, &outcount, &error_code, &actual_size,
			       NULL, 0, context);
	    } while (ret == 0 && outcount == 0);
	}
	if (ret < 0 || error_code != 0)
	{
	    fprintf(stderr, "Client: BMI send error.\n");
	    return (-1);
	}

	/* receive a message */
	error_code = 0;
	ret = BMI_post_recv(&bmi_id, addr, recv_buffer,
			    bmi_recv_bufs->size, &actual_size, buffer_type, 0,
			    NULL, context);
	if (ret == 0)
	{
	    do
	    {
		ret = BMI_test(bmi_id, &outcount, &error_code, &actual_size,
			       NULL, 0, context);
	    } while (ret == 0 && outcount == 0);
	}
	if (ret < 0 || error_code != 0)
	{
	    fprintf(stderr, "Client: BMI recv error.\n");
	    return (-1);
	}
    }
    time2 = MPI_Wtime();

    *wtime = time2 - time1;
    return (0);
}

static int mpi_server(
    struct bench_options *opts,
    struct mem_buffers *mpi_recv_bufs,
    struct mem_buffers *mpi_send_bufs,
    int addr,
    double *wtime)
{
    int i = 0;
    void *send_buffer = NULL;
    void *recv_buffer = NULL;
    int ret = -1;
    int flag;
    MPI_Request request;
    MPI_Status status;
    double time1, time2;

    MPI_Barrier(MPI_COMM_WORLD);
    time1 = MPI_Wtime();
    for (i = 0; i < ITERATIONS; i++)
    {
	/* set buffer to use */
	if (opts->flags & REUSE_BUFFERS)
	{
	    recv_buffer = mpi_recv_bufs->buffers[0];
	    send_buffer = mpi_send_bufs->buffers[0];
	}
	else
	{
	    recv_buffer = mpi_recv_bufs->buffers[i];
	    send_buffer = mpi_send_bufs->buffers[i];
	}

	/* recv a message */
	ret = MPI_Irecv(recv_buffer, mpi_recv_bufs->size, MPI_BYTE, addr,
			0, MPI_COMM_WORLD, &request);
	if (ret != MPI_SUCCESS)
	{
	    fprintf(stderr, "MPI_Irecv() failure.\n");
	    return (-1);
	}
	do
	{
	    ret = MPI_Test(&request, &flag, &status);
	} while (ret == MPI_SUCCESS && flag == 0);
	if (ret != MPI_SUCCESS)
	{
	    fprintf(stderr, "MPI_Test() failure.\n");
	    return (-1);
	}

	/* send a message */
	ret = MPI_Isend(send_buffer, mpi_recv_bufs->size, MPI_BYTE, addr,
			0, MPI_COMM_WORLD, &request);
	if (ret != MPI_SUCCESS)
	{
	    fprintf(stderr, "MPI_Isend() failure.\n");
	    return (-1);
	}
	do
	{
	    ret = MPI_Test(&request, &flag, &status);
	} while (ret == MPI_SUCCESS && flag == 0);
	if (ret != MPI_SUCCESS)
	{
	    fprintf(stderr, "MPI_Test() failure.\n");
	    return (-1);
	}

    }
    time2 = MPI_Wtime();

    *wtime = time2 - time1;
    return (0);
}

static int mpi_client(
    struct bench_options *opts,
    struct mem_buffers *mpi_recv_bufs,
    struct mem_buffers *mpi_send_bufs,
    int addr,
    double *wtime)
{
    int i = 0;
    void *send_buffer = NULL;
    void *recv_buffer = NULL;
    int ret = -1;
    int flag;
    MPI_Request request;
    MPI_Status status;
    double time1, time2;

    MPI_Barrier(MPI_COMM_WORLD);
    time1 = MPI_Wtime();
    for (i = 0; i < ITERATIONS; i++)
    {
	/* set buffer to use */
	if (opts->flags & REUSE_BUFFERS)
	{
	    recv_buffer = mpi_recv_bufs->buffers[0];
	    send_buffer = mpi_send_bufs->buffers[0];
	}
	else
	{
	    recv_buffer = mpi_recv_bufs->buffers[i];
	    send_buffer = mpi_send_bufs->buffers[i];
	}

	/* send a message */
	ret = MPI_Isend(send_buffer, mpi_recv_bufs->size, MPI_BYTE, addr,
			0, MPI_COMM_WORLD, &request);
	if (ret != MPI_SUCCESS)
	{
	    fprintf(stderr, "MPI_Isend() failure.\n");
	    return (-1);
	}
	do
	{
	    ret = MPI_Test(&request, &flag, &status);
	} while (ret == MPI_SUCCESS && flag == 0);
	if (ret != MPI_SUCCESS)
	{
	    fprintf(stderr, "MPI_Test() failure.\n");
	    return (-1);
	}

	/* recv a message */
	ret = MPI_Irecv(recv_buffer, mpi_recv_bufs->size, MPI_BYTE, addr,
			0, MPI_COMM_WORLD, &request);
	if (ret != MPI_SUCCESS)
	{
	    fprintf(stderr, "MPI_Irecv() failure.\n");
	    return (-1);
	}
	do
	{
	    ret = MPI_Test(&request, &flag, &status);
	} while (ret == MPI_SUCCESS && flag == 0);
	if (ret != MPI_SUCCESS)
	{
	    fprintf(stderr, "MPI_Test() failure.\n");
	    return (-1);
	}

    }
    time2 = MPI_Wtime();

    *wtime = time2 - time1;
    return (0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
