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
#include <mpi.h>
#include "bench-args.h"

#ifndef __BENCH_INITIALIZE_H
#define __BENCH_INITIALIZE_H

#define BMI_TCP_PORT 3334
#define BMI_GM_PORT 5

int bench_initialize_bmi_interface(
    char *method,
    int flags,
    bmi_context_id * context);
int bench_initialize_mpi_params(
    int argc,
    char **argv,
    int num_servers,
    int *num_clients,
    int *world_rank,
    MPI_Comm * comm,
    char *local_proc_name);
int bench_initialize_bmi_addresses_server(
    int num_servers,
    int num_clients,
    PVFS_BMI_addr_t * client_array,
    char *local_proc_name);
int bench_initialize_bmi_addresses_client(
    int num_servers,
    int num_clients,
    PVFS_BMI_addr_t * client_array,
    char *method_name,
    bmi_context_id context);
int bench_init(
    struct bench_options *opts,
    int argc,
    char *argv[],
    int *num_clients,
    int *world_rank,
    MPI_Comm * comm,
    PVFS_BMI_addr_t ** bmi_peer_array,
    int **mpi_peer_array,
    bmi_context_id * context);

#endif /* __BENCH_INITIALIZE_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
