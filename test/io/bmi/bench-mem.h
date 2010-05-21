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
#include "bmi.h"

#ifndef __BENCH_MEM_H
#define __BENCH_MEM_H

struct mem_buffers
{
    void **buffers;
    int num_buffers;
    int size;
};

int alloc_buffers(
    struct mem_buffers *bufs,
    int num_buffers,
    int size);
int BMI_alloc_buffers(
    struct mem_buffers *bufs,
    int num_buffers,
    int size,
    PVFS_BMI_addr_t addr,
    enum bmi_op_type send_recv);

int free_buffers(
    struct mem_buffers *bufs);
int BMI_free_buffers(
    struct mem_buffers *bufs,
    PVFS_BMI_addr_t addr,
    enum bmi_op_type send_recv);

int mark_buffers(
    struct mem_buffers *bufs);
int check_buffers(
    struct mem_buffers *bufs);

#endif /* __BENCH_MEM_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
