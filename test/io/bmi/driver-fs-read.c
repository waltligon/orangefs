/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <mpi.h>
#include "gossip.h"
#include "bmi.h"
#include "bench-initialize.h"
#include "bench-args.h"
#include "bench-mem.h"

#define TESTCOUNT 10
#define MSG_SIZE (256*1024)

struct svr_xfer_state
{
    int step;

    PVFS_BMI_addr_t addr;
    bmi_msg_tag_t tag;

    void* unexp_buffer;
    bmi_size_t unexp_size;

    struct request* req;
    struct response* resp;

    void** buffer_array;
    int buffer_array_size;

    int list_factor;
    void** buffer_list;
    bmi_size_t* size_list;
};

struct request
{
    int foo;
};

struct response
{
    int bar;
};

int num_done = 0;

int svr_handle_next(struct svr_xfer_state* state, bmi_context_id context);
int client_handle_next(struct svr_xfer_state* state, bmi_context_id context);
int prepare_states(struct svr_xfer_state* state_array, PVFS_BMI_addr_t*
    addr_array, struct bench_options* opts, int count, int svr_flag);
int teardown_states(struct svr_xfer_state* state_array, PVFS_BMI_addr_t*
    addr_array, struct bench_options* opts, int count, int svr_flag);

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
    struct svr_xfer_state* state_array = NULL;
    struct svr_xfer_state* tmp_state = NULL;
    int num_requested = 0;
    struct BMI_unexpected_info info_array[TESTCOUNT];
    bmi_op_id_t id_array[TESTCOUNT];
    bmi_error_code_t error_array[TESTCOUNT];
    bmi_size_t size_array[TESTCOUNT];
    void* user_ptr_array[TESTCOUNT];
    int outcount = 0;
    int indexer = 0;
    int state_size;
    int i;
    double time1, time2;

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

    num_messages = opts.total_len / MSG_SIZE;
    if(opts.total_len%MSG_SIZE)
        num_messages++;

    if(im_a_server)
        state_size = num_clients;
    else
        state_size = opts.num_servers;

    /* allocate array to hold state of concurrent xfers */
    state_array = (struct
        svr_xfer_state*)malloc(state_size*sizeof(struct svr_xfer_state));
    assert(state_array);
    memset(state_array, 0, state_size*sizeof(struct svr_xfer_state));

    if(im_a_server)
        prepare_states(state_array, bmi_peer_array, &opts, state_size, 1);
    else
        prepare_states(state_array, bmi_peer_array, &opts, state_size, 0);

    MPI_Barrier(MPI_COMM_WORLD);

    time1 = MPI_Wtime();
    
    if(im_a_server)
    {
        while(num_done < num_clients)
        {
            outcount = 0;
            ret = BMI_testunexpected(TESTCOUNT, &outcount, info_array, 0);
            assert(ret >= 0);
            indexer = 0;
            while(indexer < outcount)
            {
                assert(info_array[indexer].error_code == 0);
                state_array[num_requested].addr = info_array[indexer].addr;
                state_array[num_requested].tag = info_array[indexer].tag;
                state_array[num_requested].unexp_buffer = info_array[indexer].buffer;
                state_array[num_requested].unexp_size = info_array[indexer].size;
                ret = svr_handle_next(&state_array[num_requested], context);
                assert(ret == 0);
                indexer++;
                num_requested++;
            }

            outcount = 0;
            ret = BMI_testcontext(TESTCOUNT, id_array, &outcount,
                error_array, size_array, user_ptr_array, 0, context);
            assert(ret >= 0);
            indexer = 0;
            while(indexer < outcount)
            {
                assert(error_array[indexer] == 0);
                tmp_state = user_ptr_array[indexer];
                ret = svr_handle_next(tmp_state, context);
                indexer++;
            }
        }
    }
    else
    {
        for(i=0; i< opts.num_servers; i++)
        {
            ret = client_handle_next(&state_array[i], context);
            assert(ret == 0);
        }

        while(num_done < opts.num_servers)
        {
            outcount = 0;
            ret = BMI_testcontext(TESTCOUNT, id_array, &outcount,
                error_array, size_array, user_ptr_array, 0, context);
            assert(ret >= 0);
            indexer = 0;
            while(indexer < outcount)
            {
                assert(error_array[indexer] == 0);
                tmp_state = user_ptr_array[indexer];
                ret = client_handle_next(tmp_state, context);
                indexer++;
            }
        }
    }
    time2 = MPI_Wtime();
    bmi_time = (time2-time1);

    MPI_Barrier(MPI_COMM_WORLD);

    /* TODO: release bmi memory */
    if(im_a_server)
        teardown_states(state_array, bmi_peer_array, &opts, state_size, 1);
    else
        teardown_states(state_array, bmi_peer_array, &opts, state_size, 0);

    MPI_Barrier(MPI_COMM_WORLD);

    /* run mpi version */
    mpi_time = 0;

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
	    MSG_SIZE;
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
	     MSG_SIZE, opts.num_servers, min_bmi_time, max_bmi_time,
	     ave_bmi_time, stddev_bmi_time, agg_bmi_bw / (1024 * 1024));
	printf
	    ("%d %d %f %f %f %f %f (msg_len,servers,min,max,ave,stddev,agg_MB/s) mpi server\n",
	     MSG_SIZE, opts.num_servers, min_mpi_time, max_mpi_time,
	     ave_mpi_time, stddev_mpi_time, agg_mpi_bw / (1024 * 1024));
    }

    if (world_rank == opts.num_servers)
    {
	total_data_xfer = opts.num_servers * num_clients * num_messages *
	    MSG_SIZE;

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
	     MSG_SIZE, num_clients, min_bmi_time, max_bmi_time,
	     ave_bmi_time, stddev_bmi_time, agg_bmi_bw / (1024 * 1024));
	printf
	    ("%d %d %f %f %f %f %f (msg_len,clients,min,max,ave,stddev,agg_MB/s) mpi client\n",
	     MSG_SIZE, num_clients, min_mpi_time, max_mpi_time,
	     ave_mpi_time, stddev_mpi_time, agg_mpi_bw / (1024 * 1024));
    }

    /* shutdown interfaces */
    BMI_close_context(context);
    BMI_finalize();
    MPI_Finalize();
    return 0;
}

int client_handle_next(struct svr_xfer_state* state, bmi_context_id context)
{
    int ret = 0;
    bmi_op_id_t tmp_id;
    bmi_size_t actual_size;
    int i;

    switch(state->step)
    {
        case 0:
            /* post recv for response */
            ret = BMI_post_recv(&tmp_id, state->addr, state->resp,
                sizeof(struct response), &actual_size, BMI_PRE_ALLOC,
                state->tag, state, context);
            if(ret < 0)
            {
                PVFS_perror("BMI_post_recv", ret);
            }
            assert(ret == 0);

            /* send request */
            ret = BMI_post_sendunexpected(&tmp_id, state->addr, state->req,
                sizeof(struct request), BMI_PRE_ALLOC, state->tag,
                state, context);
            assert(ret >= 0);        
            state->step++;
            if(ret == 1)
                return(client_handle_next(state, context));
            else
                return(0);
            break;

        case 1:
            /* send completed */
            state->step++;
            return(0);
            break;

        default:

            /* recv completed (resp or bulk) */
            state->step++;
            if(state->step == (state->buffer_array_size + 3))
            {
                num_done++;
                return(0);
            }

            for(i=0; i<state->list_factor; i++)
            {
                state->buffer_list[i] = state->buffer_array[state->step-3] +
                    ((MSG_SIZE/state->list_factor)*i);
            }
            ret = BMI_post_recv_list(&tmp_id, state->addr,
                state->buffer_list, state->size_list, state->list_factor,
                MSG_SIZE, &actual_size, BMI_PRE_ALLOC,
                state->tag, state, context);
            assert(ret >= 0);
            if(ret == 1)
                return(client_handle_next(state, context));
            else
                return(0);

        break;
    }

    return(0);
};

int svr_handle_next(struct svr_xfer_state* state, bmi_context_id context)
{
    int ret = 0;
    bmi_op_id_t tmp_id;
    int i;

    switch(state->step)
    {
        case 0:
            /* received a request */
            free(state->unexp_buffer);
            /* post a response send */
            ret = BMI_post_send(&tmp_id, state->addr, state->resp,
                sizeof(struct response), BMI_PRE_ALLOC, state->tag,
                state, context);
            assert(ret >= 0);
            state->step++;
            if(ret == 1)
                return(svr_handle_next(state, context));
            else
                return(0);
            break;

        case 1:
            /* response send completed */
            state->step++;

            /* post data sends */
            for(i=0; i<state->buffer_array_size; i++)
            {
                ret = BMI_post_send(&tmp_id, state->addr,
                    state->buffer_array[i], MSG_SIZE, BMI_PRE_ALLOC,
                    state->tag, state, context);
                assert(ret >= 0);
                if(ret == 1)
                    state->step++;
            }

            if(state->step == (state->buffer_array_size + 2))
                num_done++;
            return(0);
            break;

        default:
            state->step++;
            if(state->step == (state->buffer_array_size + 2))
            {
                num_done++;
            }
            return(0);
            break;
    }
    
    return(0);
};

int prepare_states(struct svr_xfer_state* state_array, PVFS_BMI_addr_t*
    addr_array, struct bench_options* opts, int count, int svr_flag)
{
    int i,j;

    if(svr_flag == 0)
    {
        /* CLIENT */
        for(i=0; i<count; i++)
        {
            state_array[i].addr = addr_array[i];
            state_array[i].tag = 0;
            /* allocate request */
            state_array[i].req = BMI_memalloc(addr_array[i],
                sizeof(struct request), BMI_SEND);
            assert(state_array[i].req);
            /* allocate response */
            state_array[i].resp = BMI_memalloc(addr_array[i],
                sizeof(struct response), BMI_RECV);
            assert(state_array[i].resp);

            /* allocate array of buffers for bulk transfer */
            state_array[i].buffer_array_size = opts->total_len/MSG_SIZE;
            if(opts->total_len%MSG_SIZE)
                state_array[i].buffer_array_size++;
            state_array[i].buffer_array =
                (void**)malloc(state_array[i].buffer_array_size*sizeof(void*));
            assert(state_array[i].buffer_array);
            for(j=0; j<state_array[i].buffer_array_size; j++)
            {
                state_array[i].buffer_array[j] =
                   malloc(MSG_SIZE);
                assert(state_array[i].buffer_array[j]);
            }

            /* setup scratch area for list transfers */
            state_array[i].list_factor = opts->list_io_factor;
            state_array[i].buffer_list =
                (void**)malloc(opts->list_io_factor*sizeof(void*));
            state_array[i].size_list =
                (bmi_size_t*)malloc(opts->list_io_factor*sizeof(bmi_size_t));
            assert(state_array[i].buffer_list);
            assert(state_array[i].size_list);
            /* preset size list */
            for(j=0; j<opts->list_io_factor; j++)
            {
                state_array[i].size_list[j] = MSG_SIZE/opts->list_io_factor;
            }
            state_array[i].size_list[opts->list_io_factor-1] +=
                (MSG_SIZE%opts->list_io_factor);

        }
    }
    else
    {
        /* SERVER */
        for(i=0; i<count; i++)
        {
            /* allocate response */
            state_array[i].resp = BMI_memalloc(addr_array[i],
                sizeof(struct response), BMI_SEND);
            assert(state_array[i].resp);

            /* allocate array of buffers for bulk transfer */
            state_array[i].buffer_array_size = opts->total_len/MSG_SIZE;
            if(opts->total_len%MSG_SIZE)
                state_array[i].buffer_array_size++;
            state_array[i].buffer_array =
                (void**)malloc(state_array[i].buffer_array_size*sizeof(void*));
            assert(state_array[i].buffer_array);
            for(j=0; j<state_array[i].buffer_array_size; j++)
            {
                state_array[i].buffer_array[j] =
                    BMI_memalloc(addr_array[i], MSG_SIZE, BMI_SEND);
                assert(state_array[i].buffer_array[j]);
            }
        }
    }

    return(0);
}

int teardown_states(struct svr_xfer_state* state_array, PVFS_BMI_addr_t*
    addr_array, struct bench_options* opts, int count, int svr_flag)
{
    int i;

    if(svr_flag == 0)
    {
        for(i=0; i<count; i++)
        {
            /* free request */
            BMI_memfree(addr_array[i], state_array[i].req, 
                sizeof(struct request), BMI_SEND);
            assert(state_array[i].req);
            /* free response */
            BMI_memfree(addr_array[i], state_array[i].resp, 
                sizeof(struct request), BMI_RECV);
            assert(state_array[i].resp);

            /* TODO: free message buffers */
        }
    }
    else
    {
        for(i=0; i<count; i++)
        {
            /* free response */
            BMI_memfree(addr_array[i], state_array[i].resp, 
                sizeof(struct request), BMI_SEND);
            assert(state_array[i].resp);
        }

        /* TODO: free message buffers */
    }

    return(0);
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
