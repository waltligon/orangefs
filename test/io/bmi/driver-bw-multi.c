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

static int bmi_server_postall(
    struct bench_options *opts,
    struct mem_buffers *bmi_buf_array,
    int num_clients,
    PVFS_BMI_addr_t * addr_array,
    enum bmi_buffer_type buffer_type,
    double *wtime,
    int world_rank,
    bmi_context_id context);
static int bmi_client_postall(
    struct bench_options *opts,
    struct mem_buffers *bmi_buf_array,
    int num_servers,
    PVFS_BMI_addr_t * addr_array,
    enum bmi_buffer_type buffer_type,
    double *wtime,
    int world_rank,
    bmi_context_id context);
static int mpi_client_postall(
    struct bench_options *opts,
    struct mem_buffers *mpi_buf_array,
    int num_servers,
    int *addr_array,
    double *wtime,
    int world_rank);
static int mpi_server_postall(
    struct bench_options *opts,
    struct mem_buffers *mpi_buf_array,
    int num_clients,
    int *addr_array,
    double *wtime,
    int world_rank);

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
    enum bmi_buffer_type buffer_type = BMI_EXT_ALLOC;
    struct mem_buffers *mpi_buf_array = NULL;
    struct mem_buffers *bmi_buf_array = NULL;
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
    double total_data_xfer = 0;
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

    /* setup buffers */
    if (im_a_server)
    {
	/* allocate array to track buffer sets */
	mpi_buf_array = (struct mem_buffers *) malloc(num_clients *
						      sizeof(struct
							     mem_buffers));
	bmi_buf_array =
	    (struct mem_buffers *) malloc(num_clients *
					  sizeof(struct mem_buffers));
	if (!mpi_buf_array || !bmi_buf_array)
	{
	    fprintf(stderr, "malloc failure.\n");
	    return (-1);
	}

	/* actually allocate buffers */
	for (i = 0; i < num_clients; i++)
	{
	    if (opts.flags & BMI_ALLOCATE_MEMORY)
	    {
		buffer_type = BMI_PRE_ALLOC;
		ret = BMI_alloc_buffers(&(bmi_buf_array[i]), num_messages,
					opts.message_len, bmi_peer_array[i],
					BMI_RECV);
	    }
	    else
	    {
		ret = alloc_buffers(&(bmi_buf_array[i]), num_messages,
				    opts.message_len);
	    }

	    ret += alloc_buffers(&(mpi_buf_array[i]), num_messages,
				 opts.message_len);

	    if (ret < 0)
	    {
		fprintf(stderr, "alloc_buffers failure.\n");
		return (-1);
	    }
	}
    }
    else
    {
	/* allocate array to track buffer sets */
	mpi_buf_array = (struct mem_buffers *) malloc(opts.num_servers *
						      sizeof(struct
							     mem_buffers));
	bmi_buf_array =
	    (struct mem_buffers *) malloc(opts.num_servers *
					  sizeof(struct mem_buffers));
	if (!mpi_buf_array || !bmi_buf_array)
	{
	    fprintf(stderr, "malloc failure.\n");
	    return (-1);
	}

	/* actually allocate buffers */
	for (i = 0; i < opts.num_servers; i++)
	{
	    if (opts.flags & BMI_ALLOCATE_MEMORY)
	    {
		buffer_type = BMI_PRE_ALLOC;
		ret = BMI_alloc_buffers(&(bmi_buf_array[i]), num_messages,
					opts.message_len, bmi_peer_array[i],
					BMI_SEND);
	    }
	    else
	    {
		ret = alloc_buffers(&(bmi_buf_array[i]), num_messages,
				    opts.message_len);
	    }

	    ret += alloc_buffers(&(mpi_buf_array[i]), num_messages,
				 opts.message_len);

	    if (ret < 0)
	    {
		fprintf(stderr, "alloc_buffers failure.\n");
		return (-1);
	    }

	    /* only the "client" marks its buffers */
	    ret = mark_buffers(&(bmi_buf_array[i]));
	    ret += mark_buffers(&(mpi_buf_array[i]));
	    if (ret < 0)
	    {
		fprintf(stderr, "mark_buffers() failure.\n");
		return (-1);
	    }
	}
    }

	/********************************************************/
    /* Actually measure some stuff */

    if (im_a_server)
    {
	ret = bmi_server_postall(&opts, bmi_buf_array, num_clients,
				 bmi_peer_array, buffer_type, &bmi_time,
				 world_rank, context);
    }
    else
    {
	ret = bmi_client_postall(&opts, bmi_buf_array, opts.num_servers,
				 bmi_peer_array, buffer_type, &bmi_time,
				 world_rank, context);
    }
    if (ret < 0)
    {
	fprintf(stderr, "failure in main routine, MPI task %d.\n", world_rank);
	return (-1);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    if (im_a_server)
    {
	ret = mpi_server_postall(&opts, mpi_buf_array, num_clients,
				 mpi_peer_array, &mpi_time, world_rank);
    }
    else
    {
	ret = mpi_client_postall(&opts, mpi_buf_array, opts.num_servers,
				 mpi_peer_array, &mpi_time, world_rank);
    }

    if (ret < 0)
    {
	fprintf(stderr, "failure in main routine, MPI task %d.\n", world_rank);
	return (-1);
    }

    MPI_Barrier(MPI_COMM_WORLD);

	/********************************************************/
    /* Done measuring */
#if 0
    /* server verifies buffers that it receives */
    if (!(opts.flags & REUSE_BUFFERS) && im_a_server)
    {
	for (i = 0; i < num_clients; i++)
	{
	    ret = check_buffers(&(mpi_buf_array[i]));
	    if (ret < 0)
	    {
		fprintf(stderr, "*********************************\n");
		fprintf(stderr, "MPI buffer verification failed.\n");
		return (-1);
	    }
	    ret = check_buffers(&(bmi_buf_array[i]));
	    if (ret < 0)
	    {
		fprintf(stderr, "**********************************\n");
		fprintf(stderr, "BMI buffer verification failed.\n");
		return (-1);
	    }
	}
    }
#endif
    /* release buffers */
    if (im_a_server)
    {
	for (i = 0; i < num_clients; i++)
	{
	    free_buffers(&(mpi_buf_array[i]));
	    if (opts.flags & BMI_ALLOCATE_MEMORY)
	    {
		BMI_free_buffers(&(bmi_buf_array[i]), bmi_peer_array[i],
				 BMI_RECV);
	    }
	    else
	    {
		free_buffers(&(bmi_buf_array[i]));
	    }
	}
    }
    else
    {
	for (i = 0; i < opts.num_servers; i++)
	{
	    free_buffers(&(mpi_buf_array[i]));
	    if (opts.flags & BMI_ALLOCATE_MEMORY)
	    {
		BMI_free_buffers(&(bmi_buf_array[i]), bmi_peer_array[i],
				 BMI_SEND);
	    }
	    else
	    {
		free_buffers(&(bmi_buf_array[i]));
	    }
	}
    }

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

    /* do this first to get nice output ordering */
    if (world_rank == 0) {
	bench_args_dump(&opts);
        fflush(stdout);
    }
    MPI_Barrier(MPI_COMM_WORLD);

    /* exactly one "client" and one "server" compute and print
     * statistics
     */
    if (world_rank == 0)
    {
	total_data_xfer = (double)opts.num_servers * (double)num_clients *
            (double)num_messages * (double)opts.message_len;
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

    /* enforce output ordering */
    fflush(stdout);
    MPI_Barrier(MPI_COMM_WORLD);

    if (world_rank == opts.num_servers)
    {
	total_data_xfer = (double)opts.num_servers * (double)num_clients *
            (double)num_messages * (double)opts.message_len;

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

#if 0
    MPI_Allreduce(&read_tim, &max_read_tim, 1, MPI_DOUBLE, MPI_MAX,
		  MPI_COMM_WORLD);
    MPI_Allreduce(&read_tim, &min_read_tim, 1, MPI_DOUBLE, MPI_MIN,
		  MPI_COMM_WORLD);
    MPI_Allreduce(&read_tim, &sum_read_tim, 1, MPI_DOUBLE, MPI_SUM,
		  MPI_COMM_WORLD);

    ret = MPI_Comm_size(comm, &comm_size);
    if (ret != MPI_SUCCESS)
    {
	fprintf(stderr, "Comm_size failure.\n");
	return (-1);
    }
    printf("comm size: %d\n", comm_size);
#endif
    /* shutdown interfaces */
    BMI_close_context(context);
    BMI_finalize();
    MPI_Finalize();
    return 0;
}


static int bmi_server_postall(
    struct bench_options *opts,
    struct mem_buffers *bmi_buf_array,
    int num_clients,
    PVFS_BMI_addr_t * addr_array,
    enum bmi_buffer_type buffer_type,
    double *wtime,
    int world_rank,
    bmi_context_id context)
{
    double time1, time2;
    int i, j;
    int **done;
    int *done_index;
    bmi_op_id_t **ids;
    int num_buffers = bmi_buf_array[0].num_buffers;
    void *recv_buffer;
    int ret;
    bmi_size_t actual_size;
    int outcount;
    bmi_op_id_t *id_array;
    int *index_array;
    bmi_error_code_t *error_code_array;
    bmi_size_t *actual_size_array;
    int done_clients = 0;

    /* allocate a lot of arrays to keep up with what message we are
     * on for each peer
     */
    done = (int **) malloc(num_clients * sizeof(int *));
    done_index = (int *) malloc(num_clients * sizeof(int));
    ids = (bmi_op_id_t **) malloc(num_clients * sizeof(bmi_op_id_t *));
    id_array = (bmi_op_id_t *) malloc(num_clients * sizeof(bmi_op_id_t));
    actual_size_array = (bmi_size_t *) malloc(num_clients * sizeof(bmi_size_t));
    error_code_array = (bmi_error_code_t *) malloc(num_clients *
						   sizeof(bmi_error_code_t));
    index_array = (int *) malloc(num_clients * sizeof(int));

    if (!done || !done_index || !ids || !id_array || !actual_size_array
	|| !error_code_array || !index_array)
    {
	fprintf(stderr, "malloc error.\n");
	return (-1);
    }
    for (i = 0; i < num_clients; i++)
    {
	done[i] = (int *) malloc(num_buffers * sizeof(int));
	ids[i] = (bmi_op_id_t *) malloc(num_buffers * sizeof(bmi_op_id_t));
	if (!done[i] || !ids[i])
	{
	    fprintf(stderr, "malloc error.\n");
	    return (-1);
	}
    }

    /* barrier and then start timing */
    MPI_Barrier(MPI_COMM_WORLD);
    time1 = MPI_Wtime();

    /* post everything at once */
    for (i = 0; i < num_buffers; i++)
    {
	for (j = 0; j < num_clients; j++)
	{
	    if (opts->flags & REUSE_BUFFERS)
	    {
		recv_buffer = bmi_buf_array[j].buffers[0];
	    }
	    else
	    {
		recv_buffer = bmi_buf_array[j].buffers[i];
	    }

	    ret = BMI_post_recv(&(ids[j][i]), addr_array[j], recv_buffer,
				bmi_buf_array[0].size, &actual_size,
				buffer_type, 0, NULL, context);
	    if (ret < 0)
	    {
		fprintf(stderr, "Server: BMI recv error.\n");
		return (-1);
	    }
	    else if (ret == 0)
	    {
		done[j][i] = 0;
	    }
	    else
	    {
		/* mark that this message completed immediately */
		done[j][i] = 1;
	    }
	}
    }

    /* find the first message to test for each client */
    for (i = 0; i < num_clients; i++)
    {
	for (j = 0; j < num_buffers; j++)
	{
	    if (done[i][j] == 0)
	    {
		done_index[i] = j;
		id_array[i] = ids[i][j];
		break;
	    }
	}
	if (j == num_buffers)
	{
	    /* this client is completely done */
	    done_index[i] = -1;
	    id_array[i] = 0;
	    done_clients++;
	}
    }

    /* while there is still work to do */
    while (done_clients < num_clients)
    {
	outcount = 0;
	do
	{
	    /* test the earliest uncompleted message for each host-
	     * there are zero entries for hosts that are already
	     * finished
	     */
	    ret = BMI_testsome(num_clients, id_array, &outcount, index_array,
			       error_code_array, actual_size_array, NULL, 0,
			       context);
	} while (ret == 0 && outcount == 0);

	if (ret < 0)
	{
	    fprintf(stderr, "testsome error.\n");
	    return (-1);
	}

	/* for each completed message, mark that it is done and
	 * adjust indexes to test for next uncompleted message
	 */
	for (i = 0; i < outcount; i++)
	{
	    if (error_code_array[i] != 0)
	    {
		fprintf(stderr, "BMI op failure.\n");
		return (-1);
	    }

	    (done_index[index_array[i]])++;
	    if (done_index[index_array[i]] == num_buffers)
	    {
		done_index[index_array[i]] = -1;
		id_array[index_array[i]] = 0;
		done_clients++;
	    }
	    else
	    {
		while (done[index_array[i]][done_index[index_array[i]]] == 1)
		{
		    (done_index[index_array[i]])++;
		}
		if (done_index[index_array[i]] == num_buffers)
		{
		    done_index[index_array[i]] = -1;
		    id_array[index_array[i]] = 0;
		    done_clients++;
		}
		else
		{
		    id_array[index_array[i]] =
			ids[index_array[i]][done_index[index_array[i]]];
		}
	    }
	}
    }

    /* stop timing */
    time2 = MPI_Wtime();

    *wtime = time2 - time1;

    return (0);
}


static int bmi_client_postall(
    struct bench_options *opts,
    struct mem_buffers *bmi_buf_array,
    int num_servers,
    PVFS_BMI_addr_t * addr_array,
    enum bmi_buffer_type buffer_type,
    double *wtime,
    int world_rank,
    bmi_context_id context)
{
    double time1, time2;
    int i, j;
    int **done;
    int *done_index;
    bmi_op_id_t **ids;
    int num_buffers = bmi_buf_array[0].num_buffers;
    void *send_buffer;
    int ret;
    int outcount;
    bmi_op_id_t *id_array;
    int *index_array;
    bmi_error_code_t *error_code_array;
    bmi_size_t *actual_size_array;
    int done_servers = 0;

    /* allocate a lot of arrays to keep up with what message we are
     * on for each peer
     */
    done = (int **) malloc(num_servers * sizeof(int *));
    done_index = (int *) malloc(num_servers * sizeof(int));
    ids = (bmi_op_id_t **) malloc(num_servers * sizeof(bmi_op_id_t *));
    id_array = (bmi_op_id_t *) malloc(num_servers * sizeof(bmi_op_id_t));
    actual_size_array = (bmi_size_t *) malloc(num_servers * sizeof(bmi_size_t));
    error_code_array = (bmi_error_code_t *) malloc(num_servers *
						   sizeof(bmi_error_code_t));
    index_array = (int *) malloc(num_servers * sizeof(int));

    if (!done || !done_index || !ids || !id_array || !actual_size_array
	|| !error_code_array || !index_array)
    {
	fprintf(stderr, "malloc error.\n");
	return (-1);
    }
    for (i = 0; i < num_servers; i++)
    {
	done[i] = (int *) malloc(num_buffers * sizeof(int));
	ids[i] = (bmi_op_id_t *) malloc(num_buffers * sizeof(bmi_op_id_t));
	if (!done[i] || !ids[i])
	{
	    fprintf(stderr, "malloc error.\n");
	    return (-1);
	}
    }

    /* barrier and then start timing */
    MPI_Barrier(MPI_COMM_WORLD);
    time1 = MPI_Wtime();

    /* post everything at once */
    for (i = 0; i < num_buffers; i++)
    {
	for (j = 0; j < num_servers; j++)
	{
	    if (opts->flags & REUSE_BUFFERS)
	    {
		send_buffer = bmi_buf_array[j].buffers[0];
	    }
	    else
	    {
		send_buffer = bmi_buf_array[j].buffers[i];
	    }

	    ret = BMI_post_send(&(ids[j][i]), addr_array[j], send_buffer,
				bmi_buf_array[0].size, buffer_type, 0, NULL,
				context);
	    if (ret < 0)
	    {
		fprintf(stderr, "Client: BMI send error.\n");
		return (-1);
	    }
	    else if (ret == 0)
	    {
		done[j][i] = 0;
	    }
	    else
	    {
		/* mark that this message completed immediately */
		done[j][i] = 1;
	    }
	}
    }

    /* find the first message to test for each client */
    for (i = 0; i < num_servers; i++)
    {
	for (j = 0; j < num_buffers; j++)
	{
	    if (done[i][j] == 0)
	    {
		done_index[i] = j;
		id_array[i] = ids[i][j];
		break;
	    }
	}
	if (j == num_buffers)
	{
	    /* this client is completely done */
	    done_index[i] = -1;
	    id_array[i] = 0;
	    done_servers++;
	}
    }

    /* while there is still work to do */
    while (done_servers < num_servers)
    {
	outcount = 0;
	do
	{
	    /* test the earliest uncompleted message for each host-
	     * there are zero entries for hosts that are already
	     * finished
	     */
	    ret = BMI_testsome(num_servers, id_array, &outcount, index_array,
			       error_code_array, actual_size_array, NULL, 0,
			       context);
	} while (ret == 0 && outcount == 0);

	if (ret < 0)
	{
	    fprintf(stderr, "testsome error.\n");
	    return (-1);
	}

	/* for each completed message, mark that it is done and
	 * adjust indexes to test for next uncompleted message
	 */
	for (i = 0; i < outcount; i++)
	{
	    if (error_code_array[i] != 0)
	    {
		fprintf(stderr, "BMI op failure.\n");
		return (-1);
	    }

	    (done_index[index_array[i]])++;
	    if (done_index[index_array[i]] == num_buffers)
	    {
		done_index[index_array[i]] = -1;
		id_array[index_array[i]] = 0;
		done_servers++;
	    }
	    else
	    {
		while (done[index_array[i]][done_index[index_array[i]]] == 1)
		{
		    (done_index[index_array[i]])++;
		}
		if (done_index[index_array[i]] == num_buffers)
		{
		    done_index[index_array[i]] = -1;
		    id_array[index_array[i]] = 0;
		    done_servers++;
		}
		else
		{
		    id_array[index_array[i]] =
			ids[index_array[i]][done_index[index_array[i]]];
		}
	    }
	}
    }

    /* stop timing */
    time2 = MPI_Wtime();

    *wtime = time2 - time1;

    return (0);
}


static int mpi_server_postall(
    struct bench_options *opts,
    struct mem_buffers *mpi_buf_array,
    int num_clients,
    int *addr_array,
    double *wtime,
    int world_rank)
{
    int num_buffers = mpi_buf_array[0].num_buffers;
    double time1, time2;
    int i, j;
    void *recv_buffer;
    int ret = -1;
    MPI_Request **requests = NULL;
    MPI_Request *req_array = NULL;
    int *request_index = NULL;
    int done_clients = 0;
    int outcount = 0;
    int *index_array = NULL;
    MPI_Status *status_array = NULL;


    /* allocate a lot of arrays to keep up with what message we are
     * on for each peer
     */
    requests = (MPI_Request **) malloc(num_clients * sizeof(MPI_Request *));
    request_index = (int *) malloc(num_clients * sizeof(int));
    req_array = (MPI_Request *) malloc(num_clients * sizeof(MPI_Request));
    index_array = (int *) malloc(num_clients * sizeof(int));
    status_array = (MPI_Status *) malloc(num_clients * sizeof(MPI_Status));

    if (!requests || !request_index || !req_array || !index_array ||
	!status_array)
    {
	fprintf(stderr, "malloc error.\n");
	return (-1);
    }
    for (i = 0; i < num_clients; i++)
    {
	request_index[i] = 0;
	requests[i] = (MPI_Request *) malloc(num_buffers * sizeof(MPI_Request));
	if (!requests[i])
	{
	    fprintf(stderr, "malloc error.\n");
	    return (-1);
	}
    }

    /* barrier and then start timing */
    MPI_Barrier(MPI_COMM_WORLD);
    time1 = MPI_Wtime();

    /* post everything at once */
    for (i = 0; i < num_buffers; i++)
    {
	for (j = 0; j < num_clients; j++)
	{
	    if (opts->flags & REUSE_BUFFERS)
	    {
		recv_buffer = mpi_buf_array[j].buffers[0];
	    }
	    else
	    {
		recv_buffer = mpi_buf_array[j].buffers[i];
	    }

	    ret = MPI_Irecv(recv_buffer, mpi_buf_array[0].size,
			    MPI_BYTE, addr_array[j], 0, MPI_COMM_WORLD,
			    &requests[j][i]);
	    if (ret != MPI_SUCCESS)
	    {
		fprintf(stderr, "MPI_Irecv failure.\n");
		return (-1);
	    }
	}
    }

    /* go until all peers have finished */
    while (done_clients < num_clients)
    {
	/* build array of requests to test for */
	for (i = 0; i < num_clients; i++)
	{
	    if (request_index[i] == -1)
	    {
		req_array[i] = MPI_REQUEST_NULL;
	    }
	    else
	    {
		req_array[i] = requests[i][request_index[i]];
	    }
	}

	/* test for completion */
	do
	{
	    ret = MPI_Testsome(num_clients, req_array, &outcount,
			       index_array, status_array);
	} while (ret == MPI_SUCCESS && outcount == 0);

	if (ret != MPI_SUCCESS)
	{
	    fprintf(stderr, "MPI_Testsome failure.\n");
	    return (-1);
	}

	/* for each completed message, mark that it is done and
	 * adjust indexes to test for next uncompleted message
	 */
	for (i = 0; i < outcount; i++)
	{
	    (request_index[index_array[i]])++;
	    if (request_index[index_array[i]] == num_buffers)
	    {
		request_index[index_array[i]] = -1;
		done_clients++;
	    }
	}
    }

    /* stop timing */
    time2 = MPI_Wtime();

    *wtime = time2 - time1;

    return (0);
}


static int mpi_client_postall(
    struct bench_options *opts,
    struct mem_buffers *mpi_buf_array,
    int num_servers,
    int *addr_array,
    double *wtime,
    int world_rank)
{
    int num_buffers = mpi_buf_array[0].num_buffers;
    double time1, time2;
    int i, j;
    void *send_buffer;
    int ret = -1;
    MPI_Request **requests = NULL;
    MPI_Request *req_array = NULL;
    int *request_index = NULL;
    int done_servers = 0;
    int outcount = 0;
    int *index_array = NULL;
    MPI_Status *status_array = NULL;


    /* allocate a lot of arrays to keep up with what message we are
     * on for each peer
     */
    requests = (MPI_Request **) malloc(num_servers * sizeof(MPI_Request *));
    request_index = (int *) malloc(num_servers * sizeof(int));
    req_array = (MPI_Request *) malloc(num_servers * sizeof(MPI_Request));
    index_array = (int *) malloc(num_servers * sizeof(int));
    status_array = (MPI_Status *) malloc(num_servers * sizeof(MPI_Status));

    if (!requests || !request_index || !req_array || !index_array ||
	!status_array)
    {
	fprintf(stderr, "malloc error.\n");
	return (-1);
    }
    for (i = 0; i < num_servers; i++)
    {
	request_index[i] = 0;
	requests[i] = (MPI_Request *) malloc(num_buffers * sizeof(MPI_Request));
	if (!requests[i])
	{
	    fprintf(stderr, "malloc error.\n");
	    return (-1);
	}
    }

    /* barrier and then start timing */
    MPI_Barrier(MPI_COMM_WORLD);
    time1 = MPI_Wtime();

    /* post everything at once */
    for (i = 0; i < num_buffers; i++)
    {
	for (j = 0; j < num_servers; j++)
	{
	    if (opts->flags & REUSE_BUFFERS)
	    {
		send_buffer = mpi_buf_array[j].buffers[0];
	    }
	    else
	    {
		send_buffer = mpi_buf_array[j].buffers[i];
	    }

	    ret = MPI_Isend(send_buffer, mpi_buf_array[0].size,
			    MPI_BYTE, addr_array[j], 0, MPI_COMM_WORLD,
			    &(requests[j][i]));
	    if (ret != MPI_SUCCESS)
	    {
		fprintf(stderr, "MPI_Isend failure.\n");
		return (-1);
	    }
	}
    }

    /* go until all peers have finished */
    while (done_servers < num_servers)
    {
	/* build array of requests to test for */
	for (i = 0; i < num_servers; i++)
	{
	    if (request_index[i] == -1)
	    {
		req_array[i] = MPI_REQUEST_NULL;
	    }
	    else
	    {
		req_array[i] = requests[i][request_index[i]];
	    }
	}

	/* test for completion */
	do
	{
	    ret = MPI_Testsome(num_servers, req_array, &outcount,
			       index_array, status_array);
	} while (ret == MPI_SUCCESS && outcount == 0);

	if (ret != MPI_SUCCESS)
	{
	    fprintf(stderr, "MPI_Testsome failure.\n");
	    return (-1);
	}

	/* for each completed message, mark that it is done and
	 * adjust indexes to test for next uncompleted message
	 */
	for (i = 0; i < outcount; i++)
	{
	    (request_index[index_array[i]])++;
	    if (request_index[index_array[i]] == num_buffers)
	    {
		request_index[index_array[i]] = -1;
		done_servers++;
	    }
	}
    }

    /* stop timing */
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
