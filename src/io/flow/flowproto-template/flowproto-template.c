/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* this is an example of a flowproto implementation */

#include <errno.h>

#include <gossip.h>
#include <flow.h>
#include <flowproto-support.h>

/* interface prototypes */
int template_flowproto_initialize(
	int flowproto_id);

int template_flowproto_finalize(
	void);

int template_flowproto_getinfo(
	flow_descriptor* flow_d, 
	int option, 
	void* parameter);

int template_flowproto_setinfo(
	flow_descriptor* flow_d, 
	int option, 
	void* parameter);

void* template_flowproto_memalloc(
	flow_descriptor* flow_d, 
	PVFS_size size, 
	PVFS_bitfield send_recv_flag);

int template_flowproto_memfree(
	flow_descriptor* flow_d, 
	void* buffer,
	PVFS_bitfield send_recv_flag);

int template_flowproto_announce_flow(
	flow_descriptor* flow_d);

int template_flowproto_check(
	flow_descriptor* flow_d, 
	int* count);

int template_flowproto_checksome(
	flow_descriptor** flow_d_array, 
	int* count, 
	int* index_array);

int template_flowproto_checkworld(
	flow_descriptor** flow_d_array, 	
	int* count);
	
int template_flowproto_service(
	flow_descriptor* flow_d);

char template_flowproto_name[] = "template_flowproto";

struct flowproto_ops flowproto_template_ops = 
{
	template_flowproto_name,
	template_flowproto_initialize,
	template_flowproto_finalize,
	template_flowproto_getinfo,
	template_flowproto_setinfo,
	template_flowproto_memalloc,
	template_flowproto_memfree,
	template_flowproto_announce_flow,
	template_flowproto_check,
	template_flowproto_checksome,
	template_flowproto_checkworld,
	template_flowproto_service
};

int template_flowproto_initialize(
	int flowproto_id)
{
	return(0);
}

int template_flowproto_finalize(
	void)
{
	return(0);
}

int template_flowproto_getinfo(
	flow_descriptor* flow_d, 
	int option, 
	void* parameter)
{
	return(-ENOSYS);
}

int template_flowproto_setinfo(
	flow_descriptor* flow_d, 
	int option, 
	void* parameter)
{
	return(-ENOSYS);
}

void* template_flowproto_memalloc(
	flow_descriptor* flow_d, 
	PVFS_size size, 
	PVFS_bitfield send_recv_flag)
{
	return(NULL);
}

int template_flowproto_memfree(
	flow_descriptor* flow_d, 
	void* buffer,
	PVFS_bitfield send_recv_flag)
{
	return(-ENOSYS);
}

int template_flowproto_announce_flow(
	flow_descriptor* flow_d)
{
	return(-ENOSYS);
}

int template_flowproto_check(
	flow_descriptor* flow_d, 
	int* count)
{
	return(-ENOSYS);
}

int template_flowproto_checksome(
	flow_descriptor** flow_d_array, 
	int* count, 
	int* index_array)
{
	return(-ENOSYS);
}

int template_flowproto_checkworld(
	flow_descriptor** flow_d_array, 
	int* count)
{
	return(-ENOSYS);
}

int template_flowproto_service(
	flow_descriptor* flow_d)
{
	return(-ENOSYS);
}



