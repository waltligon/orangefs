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
#include <bmi.h>
#include <bench-mem.h>

int alloc_buffers(struct mem_buffers* bufs, int num_buffers, int size)
{
	int i = 0;
	
	bufs->buffers = (void**)malloc(num_buffers*sizeof(void*));
	if(!bufs->buffers)
	{
		return(-1);
	}

	for(i=0; i<num_buffers; i++)
	{
		bufs->buffers[i] = (void*)malloc(size);
		if(!bufs->buffers[i])
		{
			return(-1);
		}
	}
	bufs->num_buffers = num_buffers;
	bufs->size = size;

	return(0);
}

int BMI_alloc_buffers(struct mem_buffers* bufs, int num_buffers, int
	size, bmi_addr_t addr, bmi_flag_t send_recv)
{

	int i = 0;
	
	bufs->buffers = (void**)malloc(num_buffers*sizeof(void*));
	if(!bufs->buffers)
	{
		return(-1);
	}

	for(i=0; i<num_buffers; i++)
	{
		bufs->buffers[i] = (void*)BMI_memalloc(addr, size, send_recv);
		if(!bufs->buffers[i])
		{
			return(-1);
		}
	}
	bufs->num_buffers = num_buffers;
	bufs->size = size;

	return(0);
}

int free_buffers(struct mem_buffers* bufs)
{
	int i=0;

	for(i=0; i<bufs->num_buffers; i++)
	{
		free(bufs->buffers[i]);
	}
	free(bufs->buffers);
	bufs->num_buffers = 0;
	bufs->size = 0;

	return(0);
}

int BMI_free_buffers(struct mem_buffers* bufs, bmi_addr_t addr,
	bmi_flag_t send_recv)
{
	int i=0;

	for(i=0; i<bufs->num_buffers; i++)
	{
		BMI_memfree(addr, bufs->buffers[i], bufs->size, send_recv);
	}
	free(bufs->buffers);
	bufs->num_buffers = 0;
	bufs->size = 0;

	return(0);
}

int mark_buffers(struct mem_buffers* bufs)
{
	long i=0;
	int buf_index = 0;
	int buf_offset = 0;
	int longs_per_buf = 0;

	if((bufs->size % (sizeof(long))) != 0)
	{
		fprintf(stderr, "Could not mark, odd size.\n");
		return(-1);
	}

	longs_per_buf = bufs->size/sizeof(long);

	for(i=0; i<(bufs->num_buffers*longs_per_buf); i++)
	{
		buf_index = i/longs_per_buf;
		buf_offset = i%longs_per_buf;
		((long*)(bufs->buffers[buf_index]))[buf_offset] = i;
	}
	
	return(0);
}

int check_buffers(struct mem_buffers* bufs)
{

	long i=0;
	int buf_index = 0;
	int buf_offset = 0;
	int longs_per_buf = 0;

	longs_per_buf = bufs->size/sizeof(long);

	for(i=0; i<(bufs->num_buffers*longs_per_buf); i++)
	{
		buf_index = i/longs_per_buf;
		buf_offset = i%longs_per_buf;
		if(((long*)(bufs->buffers[buf_index]))[buf_offset] != i)
		{
			fprintf(stderr, "check_buffers() failure.\n");
			fprintf(stderr, "buffer: %d, offset: %d\n", buf_index,
				buf_offset);
			fprintf(stderr, "expected: %ld, got: %ld\n", i, 
				((long*)(bufs->buffers[buf_index]))[buf_offset]);
			return(-1);
		}
	}
	
	return(0);
}

