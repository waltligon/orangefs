/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <gossip.h>
#include <mpi.h>
#include <bmi.h>
#include <bench-initialize.h>
#include <bench-args.h>
#include <strings.h>

int bench_init(struct bench_options* opts, int argc, char* argv[], int*
	num_clients, int* world_rank, MPI_Comm* comm, bmi_addr_t**
	bmi_peer_array, int** mpi_peer_array, bmi_context_id* context)
{
	int ret = -1;
	char local_proc_name[256];
	int i = 0;

	/* start up MPI interface */
	ret = MPI_Init(&argc, &argv);
	if(ret != MPI_SUCCESS)
	{
		fprintf(stderr, "MPI_Init() failure.\n");
		return(-1);
	}

	/* parse command line arguments */
	ret = bench_args(opts, argc, argv);
	if(ret < 0)
	{
		fprintf(stderr, "bench_args() failure.\n");
		return(-1);
	}

	/* setup MPI parameters */
	ret = bench_initialize_mpi_params(argc, argv, opts->num_servers,
		num_clients, world_rank, comm, local_proc_name);
	if(ret < 0)
	{
		fprintf(stderr, "bench_initialize_mpi_params() failure.\n");
		return(-1);
	}

	/* startup BMI interface */
	if(*world_rank < opts->num_servers)
	{
		*bmi_peer_array =
			(bmi_addr_t*)malloc((*num_clients)*sizeof(bmi_addr_t));
		*mpi_peer_array =
			(int*)malloc((*num_clients)*sizeof(int));
		if(!(*bmi_peer_array) || !(*mpi_peer_array))
		{
			return(-1);
		}
		for(i=0; i<(*num_clients); i++)
		{
			(*mpi_peer_array)[i] = i+opts->num_servers;
		}
		ret = bench_initialize_bmi_interface(opts->method_name,
			BMI_INIT_SERVER, context);
	}
	else
	{
		*bmi_peer_array =
			(bmi_addr_t*)malloc(opts->num_servers*sizeof(bmi_addr_t));
		*mpi_peer_array =
			(int*)malloc(opts->num_servers*sizeof(int));
		if(!(*bmi_peer_array) || !(*mpi_peer_array))
		{
			return(-1);
		}
		for(i=0; i<opts->num_servers; i++)
		{
			(*mpi_peer_array)[i] = i;
		}
		ret = bench_initialize_bmi_interface(opts->method_name, 0, context);
	}
	if(ret < 0)
	{
		fprintf(stderr, "bench_initialize_bmi_interface() failure.\n");
		return(-1);
	}

	MPI_Barrier(MPI_COMM_WORLD);

	/* gather BMI addresses */
	if(*world_rank < opts->num_servers)
	{
		ret = bench_initialize_bmi_addresses_server(opts->num_servers,
			*num_clients, *bmi_peer_array, local_proc_name);
	}
	else
	{
		ret = bench_initialize_bmi_addresses_client(opts->num_servers,
			*num_clients, *bmi_peer_array, opts->method_name, *context);
	}
	if(ret < 0)
	{
		fprintf(stderr, "bench_initialize_bmi_addresses() failure.\n");
		return(-1);
	}

	return(0);
}

int bench_initialize_bmi_interface(char* method, int flags,
	bmi_context_id* context)
{
	char local_address[256];
	int ret = -1;

	gossip_enable_stderr();
	gossip_set_debug_mask(0, 0);

	/* build the local listening address */
	if(strcmp(method, "bmi_tcp") == 0)
	{
		sprintf(local_address, "tcp://NULL:%d\n", BMI_TCP_PORT);
	}
	else if(strcmp(method, "bmi_gm") == 0)
	{
		sprintf(local_address, "gm://NULL:%d\n", BMI_GM_PORT);
	}
	else
	{
		fprintf(stderr, "Bad method: %s\n", method);
		return(-1);
	}

	if(flags & BMI_INIT_SERVER)
	{
		ret = BMI_initialize(method, local_address, BMI_INIT_SERVER);
	}
	else
	{

		ret = BMI_initialize(method, NULL, 0);
	}
	if(ret < 0)
	{
		return(ret);
	}

	ret = BMI_open_context(context);
	return(ret);

}


int bench_initialize_mpi_params(int argc, char** argv, int num_servers, 
	int* num_clients, int* world_rank, MPI_Comm* comm, 
	char* local_proc_name)
{
	int ret = -1;
	int numprocs, proc_namelen;
	char* trunc_point = NULL;

	/* find out total # of processes & local id */
	MPI_Comm_size(MPI_COMM_WORLD,&numprocs);
	MPI_Comm_rank(MPI_COMM_WORLD,world_rank);

	/* sanity check environment */
	if(numprocs < 2)
	{
		fprintf(stderr, "bad number of procs.\n");
		return(-1);
	}
	if(num_servers > (numprocs - 1))
	{
		fprintf(stderr, "bad number of servers.\n");
		return(-1);
	}

	*num_clients = numprocs - num_servers;

	/* put half of procs in server group, other half in client group */
	if(*world_rank < num_servers)
	{
		ret = MPI_Comm_split(MPI_COMM_WORLD, 0, 0, comm);
	}
	else
	{
		ret = MPI_Comm_split(MPI_COMM_WORLD, 1, 0, comm);
	}
	if(ret != MPI_SUCCESS)
	{
		fprintf(stderr, "MPI_Comm_spit() failure.\n");
		return(-1);
	}

	/* record the name of this processor */
	MPI_Get_processor_name(local_proc_name,&proc_namelen);

	/* trim off all but hostname portion */
	trunc_point = index(local_proc_name, '.');
	if(trunc_point)
	{
	    trunc_point[0] = '\0';
	}

	return(0);
}

int bench_initialize_bmi_addresses_server(int num_servers, int num_clients, 
	bmi_addr_t* client_array, char* local_proc_name)
{
	int i=0;
	int ret = -1;
	struct BMI_unexpected_info this_info;
	int outcount = 0;

	/* send the name of the process to the clients */
	for(i=0; i<num_clients; i++)
	{
		ret = MPI_Send(local_proc_name, 256, MPI_BYTE, (num_servers+i), 0, 
			MPI_COMM_WORLD);
		if(ret != MPI_SUCCESS)
		{
			return(-1);
		}
	}

	/* receive an unexpected message to acquire the BMI addresses and
	 * verify connectivity
	 */
	for(i=0; i<num_clients; i++)
	{
		do
		{
			ret = BMI_testunexpected(1, &outcount, &this_info, 0);
		} while(ret == 0 && outcount == 0);
		if(ret < 0)
		{
			return(-1);
		}
		client_array[i] = this_info.addr;
		free(this_info.buffer);
	}

	return(0);
}

int bench_initialize_bmi_addresses_client(int num_servers, int num_clients, 
	bmi_addr_t* server_array, char* method_name, bmi_context_id context)
{
	int i=0;
	int ret = -1;
	char server_name[256];
	char bmi_server_name[256];
	bmi_op_id_t bmi_id;
	int outcount, error_code;
	bmi_size_t actual_size;
	MPI_Status status_foo;

	/* receive all of the process names of the servers */
	for(i=0; i<num_servers; i++)
	{
		ret = MPI_Recv(server_name, 256, MPI_BYTE, i, 0, MPI_COMM_WORLD, 
			&status_foo);
		if(ret != MPI_SUCCESS)
		{
			return(-1);
		}
		/* convert process nams into BMI addresses and lookup */
		if(strcmp(method_name, "bmi_tcp") == 0)
		{
			sprintf(bmi_server_name, "tcp://%s:%d", server_name, BMI_TCP_PORT);
		}
		else if(strcmp(method_name, "bmi_gm") == 0)
		{
			sprintf(bmi_server_name, "gm://%s:%d", server_name, BMI_GM_PORT);
		}
		else
		{
			return(-1);
		}
		ret = BMI_addr_lookup(&server_array[i], bmi_server_name);
		if(ret < 0)
		{
			return(-1);
		}
	}

	/* send an unexpected message to servers to inform them of client
	 * addresses and verify connectivity
	 */
	for(i=0; i<num_servers; i++)
	{
		ret = BMI_post_sendunexpected(&bmi_id, server_array[i], &ret,
			sizeof(int), BMI_EXT_ALLOC, 0, NULL, context);
		if(ret == 0)
		{
			do
			{
				ret = BMI_test(bmi_id, &outcount, &error_code,
					&actual_size, NULL, 0, context);
			} while(ret == 0 && outcount == 0);
		}
		if(ret < 0 || error_code != 0)
		{
			return(-1);
		}
	}

	return(0);
}
