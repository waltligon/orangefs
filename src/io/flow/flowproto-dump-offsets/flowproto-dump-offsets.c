/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "gossip.h"
#include "flow.h"
#include "flowproto-support.h"
#include "quicklist.h"
#include "pvfs2-request.h"

/**********************************************************
 * interface prototypes 
 */
int flowproto_dump_offsets_initialize(int flowproto_id);

int flowproto_dump_offsets_finalize(void);

int flowproto_dump_offsets_getinfo(flow_descriptor * flow_d,
				int option,
				void *parameter);

int flowproto_dump_offsets_setinfo(flow_descriptor * flow_d,
				int option,
				void *parameter);

int flowproto_dump_offsets_post(flow_descriptor * flow_d);

int flowproto_dump_offsets_find_serviceable(flow_descriptor ** flow_d_array,
				   int *count,
				   int max_idle_time_ms);

int flowproto_dump_offsets_service(flow_descriptor * flow_d);

char flowproto_dump_offsets_name[] = "flowproto_dump_offsets";

struct flowproto_ops flowproto_dump_offsets_ops = {
    flowproto_dump_offsets_name,
    flowproto_dump_offsets_initialize,
    flowproto_dump_offsets_finalize,
    flowproto_dump_offsets_getinfo,
    flowproto_dump_offsets_setinfo,
    flowproto_dump_offsets_post,
    flowproto_dump_offsets_find_serviceable,
    flowproto_dump_offsets_service
};

/****************************************************
 * internal types and global variables
 */

/* types of flows */
enum flow_type
{
    BMI_TO_TROVE = 1,
    TROVE_TO_BMI = 2,
    BMI_TO_MEM = 3,
    MEM_TO_BMI = 4
};

/* default buffer size to use for I/O */
static int DEFAULT_BUFFER_SIZE = 1024 * 1024 * 4;
/* our assigned flowproto id */
static int flowproto_dump_offsets_id = -1;

/* max number of discontig regions we will handle at once */
#define MAX_REGIONS 16

/****************************************************
 * internal helper function declarations
 */

static int check_support(struct flowproto_type_support *type);
static void service_mem_to_bmi(flow_descriptor * flow_d);
static void service_bmi_to_mem(flow_descriptor * flow_d);
static void service_bmi_to_trove(flow_descriptor * flow_d);
static void service_trove_to_bmi(flow_descriptor * flow_d);

/****************************************************
 * public interface functions
 */

/* flowproto_dump_offsets_initialize()
 *
 * initializes the flowprotocol
 *
 * returns 0 on success, -errno on failure
 */
int flowproto_dump_offsets_initialize(int flowproto_id)
{
    int ret = -1;
    int tmp_maxsize;
    gossip_ldebug(FLOW_PROTO_DEBUG, "flowproto_dump_offsets initialized.\n");

    /* make sure that the bmi interface is initialized */
    if ((ret = BMI_get_info(0, BMI_CHECK_INIT, NULL)) < 0)
    {
	return (ret);
    }

    /* TODO: make sure that the trove interface is initialized */

    /* make sure that the default buffer size does not exceed the
     * max buffer size allowed by BMI
     */
    if ((ret = BMI_get_info(0, BMI_CHECK_MAXSIZE, &tmp_maxsize)) < 0)
    {
	return (ret);
    }
    if (tmp_maxsize < DEFAULT_BUFFER_SIZE)
    {
	DEFAULT_BUFFER_SIZE = tmp_maxsize;
    }
    if (DEFAULT_BUFFER_SIZE < 1)
    {
	gossip_lerr("Error: BMI buffer size too small!\n");
	return (-EINVAL);
    }

    flowproto_dump_offsets_id = flowproto_id;

    return (0);
}

/* flowproto_dump_offsets_finalize()
 *
 * shuts down the flow protocol
 *
 * returns 0 on success, -errno on failure
 */
int flowproto_dump_offsets_finalize(void)
{
    gossip_ldebug(FLOW_PROTO_DEBUG, "flowproto_dump_offsets shut down.\n");
    return (0);
}

/* flowproto_dump_offsets_getinfo()
 *
 * reads optional parameters from the flow protocol
 *
 * returns 0 on success, -errno on failure
 */
int flowproto_dump_offsets_getinfo(flow_descriptor * flow_d,
				int option,
				void *parameter)
{
    int* type;

    switch (option)
    {
    case FLOWPROTO_TYPE_QUERY:
	type = parameter;
	if(*type == FLOWPROTO_DUMP_OFFSETS)
	    return(0);
	else
	    return(-ENOPROTOOPT);
    default:
	return (-ENOSYS);
	break;
    }
}

/* flowproto_dump_offsets_setinfo()
 *
 * sets optional flow protocol parameters
 *
 * returns 0 on success, -errno on failure
 */
int flowproto_dump_offsets_setinfo(flow_descriptor * flow_d,
				int option,
				void *parameter)
{
    return (-ENOSYS);
}

/* flowproto_dump_offsets_post()
 *
 * informs the flow protocol that it is responsible for the given flow
 *
 * returns 0 on success, -errno on failure
 */
int flowproto_dump_offsets_post(flow_descriptor * flow_d)
{
    flow_d->flowproto_id = flowproto_dump_offsets_id;

    /* we are ready for service now */
    flow_d->state = FLOW_SVC_READY;

    return (0);
}

/* flowproto_dump_offsets_find_serviceable()
 *
 * checks to see if any previously posted flows need to be serviced
 *
 * returns 0 on success, -errno on failure
 */
int flowproto_dump_offsets_find_serviceable(flow_descriptor ** flow_d_array,
				   int *count,
				   int max_idle_time_ms)
{
    *count = 0;
    return(0);
}

/* flowproto_dump_offsets_service()
 *
 * services a single flow descriptor
 *
 * returns 0 on success, -ERRNO on failure
 */
int flowproto_dump_offsets_service(flow_descriptor * flow_d)
{

    gossip_ldebug(FLOW_PROTO_DEBUG, "flowproto_dump_offsets_service() called.\n");

    if (flow_d->state != FLOW_SVC_READY)
    {
	gossip_lerr("Error: invalid state.\n");
	return (-EINVAL);
    }

    /* handle the flow differently depending on what type it is */
    /* we don't check return values because errors are indicated by the
     * flow->state at this level
     */
    if(flow_d->src.endpoint_id == BMI_ENDPOINT &&
	flow_d->dest.endpoint_id == MEM_ENDPOINT)
    {
	service_bmi_to_mem(flow_d);
    }
    else if(flow_d->src.endpoint_id == MEM_ENDPOINT &&
	flow_d->dest.endpoint_id == BMI_ENDPOINT)
    {
	service_mem_to_bmi(flow_d);
    }
    else if(flow_d->src.endpoint_id == BMI_ENDPOINT &&
	flow_d->dest.endpoint_id == TROVE_ENDPOINT)
    {
	service_bmi_to_trove(flow_d);
    }
    else if(flow_d->src.endpoint_id == TROVE_ENDPOINT &&
	flow_d->dest.endpoint_id == BMI_ENDPOINT)
    {
	service_trove_to_bmi(flow_d);
    }
    else
    {
	gossip_lerr("Error; unknown or unsupported endpoint pair.\n");
	return (-EINVAL);
    }

    return (0);
}

/*******************************************************
 * definitions for internal utility functions
 */

/* service_mem_to_bmi() 
 *
 * services a particular type of flow
 *
 * no return value
 */
static void service_mem_to_bmi(flow_descriptor * flow_d)
{
    int ret = -1;
    PVFS_offset offset_array[MAX_REGIONS];
    PVFS_size size_array[MAX_REGIONS];
    int i;
    PINT_Request_result tmp_result;

    memset(&tmp_result, 0, sizeof(PINT_Request_result));
    tmp_result.offset_array = offset_array;
    tmp_result.size_array = size_array;
    tmp_result.bytemax = DEFAULT_BUFFER_SIZE;
    tmp_result.segmax = MAX_REGIONS;

    flow_d->total_transfered = 0;

    gossip_err("DUMP OFFSETS %p: MEMORY to BMI.\n", flow_d);
    gossip_err("*********************************************\n");
    do
    {
	tmp_result.bytes = 0;
	tmp_result.segs = 0;

	gossip_err("DUMP OFFSETS %p: PINT_Process_request().\n",
	    flow_d);
	ret = PINT_Process_request(flow_d->file_req_state,
	    flow_d->mem_req_state, &flow_d->file_data, &tmp_result,
	    PINT_CLIENT);

	if(ret < 0)
	{
	    flow_d->state = FLOW_ERROR;
	    flow_d->error_code = ret;
	    return;
	}

	gossip_err("DUMP OFFSETS %p: bytes: %ld, segs: %ld\n",
	    flow_d, (long)tmp_result.bytes, (long)tmp_result.segs);
	for(i=0; i<tmp_result.segs; i++)
	{
	    gossip_err(
	    "DUMP OFFSETS %p: seg: %d, mem offset: 0x%lx, size: %ld\n", 
		flow_d, i, ((long)offset_array[i] +
		(long)flow_d->src.u.mem.buffer), (long)size_array[i]);
	}

	flow_d->total_transfered += tmp_result.bytes;

    } while (!PINT_REQUEST_DONE(flow_d->file_req_state) && ret >= 0);

    flow_d->state = FLOW_COMPLETE;

    return;
}

/* service_bmi_to_mem() 
 *
 * services a particular type of flow
 *
 * no return value
 */
static void service_bmi_to_mem(flow_descriptor * flow_d)
{
    int ret = -1;
    PVFS_offset offset_array[MAX_REGIONS];
    PVFS_size size_array[MAX_REGIONS];
    int i;
    PINT_Request_result tmp_result;

    memset(&tmp_result, 0, sizeof(PINT_Request_result));
    tmp_result.offset_array = offset_array;
    tmp_result.size_array = size_array;
    tmp_result.bytemax = DEFAULT_BUFFER_SIZE;
    tmp_result.segmax = MAX_REGIONS;

    flow_d->total_transfered = 0;

    gossip_err("DUMP OFFSETS %p: BMI to MEMORY.\n", flow_d);
    gossip_err("*********************************************\n");
    do
    {
	tmp_result.bytes = 0;
	tmp_result.segs = 0;

	gossip_err("DUMP OFFSETS %p: PINT_Process_request().\n",
	    flow_d);
	ret = PINT_Process_request(flow_d->file_req_state,
	    flow_d->mem_req_state, &flow_d->file_data, &tmp_result,
	    PINT_CLIENT);
	if(ret < 0)
	{
	    flow_d->state = FLOW_ERROR;
	    flow_d->error_code = ret;
	    return;
	}

	gossip_err("DUMP OFFSETS %p: bytes: %ld, segs: %ld\n",
	    flow_d, (long)tmp_result.bytes, (long)tmp_result.segs);
	for(i=0; i<tmp_result.segs; i++)
	{
	    gossip_err(
	    "DUMP OFFSETS %p: seg: %d, mem offset: 0x%lx, size: %ld\n", 
		flow_d, i, ((long)offset_array[i] +
		(long)flow_d->src.u.mem.buffer), (long)size_array[i]);
	}

	flow_d->total_transfered += tmp_result.bytes;

    } while (!PINT_REQUEST_DONE(flow_d->file_req_state) && ret >= 0);

    flow_d->state = FLOW_COMPLETE;

    return;
}

/* service_bmi_to_trove() 
 *
 * services a particular type of flow
 *
 * no return value
 */
static void service_bmi_to_trove(flow_descriptor * flow_d)
{
    int ret = -1;
    PVFS_offset offset_array[MAX_REGIONS];
    PVFS_size size_array[MAX_REGIONS];
    int i;
    PINT_Request_result tmp_result;

    memset(&tmp_result, 0, sizeof(PINT_Request_result));
    tmp_result.offset_array = offset_array;
    tmp_result.size_array = size_array;
    tmp_result.bytemax = DEFAULT_BUFFER_SIZE;
    tmp_result.segmax = MAX_REGIONS;

    flow_d->total_transfered = 0;

    gossip_err("DUMP OFFSETS %p: BMI to TROVE.\n", flow_d);
    gossip_err("*********************************************\n");
    do
    {
	tmp_result.bytes = 0;
	tmp_result.segs = 0;

	gossip_err("DUMP OFFSETS %p: PINT_Process_request().\n",
	    flow_d);
	ret = PINT_Process_request(flow_d->file_req_state,
	    flow_d->mem_req_state, &flow_d->file_data, &tmp_result,
	    PINT_SERVER);

	if(ret < 0)
	{
	    flow_d->state = FLOW_ERROR;
	    flow_d->error_code = ret;
	    return;
	}

	gossip_err("DUMP OFFSETS %p: bytes: %ld, segs: %ld\n",
	    flow_d, (long)tmp_result.bytes, (long)tmp_result.segs);
	for(i=0; i<tmp_result.segs; i++)
	{
	    gossip_err(
	    "DUMP OFFSETS %p: seg: %d, file offset: %ld, size: %ld\n", 
		flow_d, i, (long)offset_array[i], (long)size_array[i]);
	}

	flow_d->total_transfered += tmp_result.bytes;

    } while (!PINT_REQUEST_DONE(flow_d->file_req_state) && ret >= 0);

    flow_d->state = FLOW_COMPLETE;

    return;
}

/* service_trove_to_bmi() 
 *
 * services a particular type of flow
 *
 * no return value
 */
static void service_trove_to_bmi(flow_descriptor * flow_d)
{
    int ret = -1;
    PVFS_offset offset_array[MAX_REGIONS];
    PVFS_size size_array[MAX_REGIONS];
    int i;
    PINT_Request_result tmp_result;

    memset(&tmp_result, 0, sizeof(PINT_Request_result));
    tmp_result.offset_array = offset_array;
    tmp_result.size_array = size_array;
    tmp_result.bytemax = DEFAULT_BUFFER_SIZE;
    tmp_result.segmax = MAX_REGIONS;

    flow_d->total_transfered = 0;

    gossip_err("DUMP OFFSETS %p: TROVE to BMI.\n", flow_d);
    gossip_err("*********************************************\n");
    do
    {
	tmp_result.bytes = 0;
	tmp_result.segs = 0;

	gossip_err("DUMP OFFSETS %p: PINT_Process_request().\n",
	    flow_d);

	ret = PINT_Process_request(flow_d->file_req_state,
	    flow_d->mem_req_state, &flow_d->file_data, &tmp_result,
	    PINT_SERVER);
	if(ret < 0)
	{
	    flow_d->state = FLOW_ERROR;
	    flow_d->error_code = ret;
	    return;
	}

	gossip_err("DUMP OFFSETS %p: bytes: %ld, segs: %ld\n",
	    flow_d, (long)tmp_result.bytes, (long)tmp_result.segs);
	for(i=0; i<tmp_result.segs; i++)
	{
	    gossip_err(
	    "DUMP OFFSETS %p: seg: %d, file offset: %ld, size: %ld\n", 
		flow_d, i, (long)offset_array[i], (long)size_array[i]);
	}

	flow_d->total_transfered += tmp_result.bytes;

    } while (!PINT_REQUEST_DONE(flow_d->file_req_state) && ret >= 0);

    flow_d->state = FLOW_COMPLETE;

    return;
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
