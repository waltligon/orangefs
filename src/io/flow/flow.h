/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This header file provides the application interface to the flow
 * system, including public data structures.
 */

#ifndef __FLOW_H
#define __FLOW_H

#include "quicklist.h"
#include "pvfs2-types.h"
#include "bmi.h"
#include "pint-distribution.h"
#include "pint-request.h"
#include "pvfs2-storage.h"

/********************************************************************
 * endpoint structure 
 */

/* endpoint types that we know about so far */
enum
{
    BMI_ENDPOINT = 1,
    TROVE_ENDPOINT = 2,
    MEM_ENDPOINT = 3
};

typedef int32_t PVFS_endpoint_type;

/* describes BMI endpoints */
struct BMI_endpoint_data
{
    bmi_addr_t address;
};

/* describes trove interface endpoints */
struct trove_endpoint_data
{
    PVFS_coll_id coll_id;
    PVFS_handle handle;
};

/* describes memory region endpoints */
struct mem_endpoint_data
{
    void *buffer;
    PVFS_size size;
    int prealloc_flag;
};

struct flow_endpoint
{
    PVFS_endpoint_type endpoint_id;
    union
    {
	struct BMI_endpoint_data bmi;
	struct trove_endpoint_data trove;
	struct mem_endpoint_data mem;
    }
    u;
};
typedef struct flow_endpoint flow_endpoint;

/* supported getinfo types */
enum
{
    FLOWPROTO_SUPPORT_QUERY = 1,
    FLOWPROTO_TYPE_QUERY = 2
};

/* context id type */
typedef PVFS_context_id FLOW_context_id;

#define FLOW_MAX_CONTEXTS 16

/********************************************************************
 * flow descriptor
 */

struct flow_descriptor
{
	/**********************************************************/
    /* fields that can be set publicly before posting */

    struct flow_endpoint src;	/* src endpoint */
    struct flow_endpoint dest;	/* dest endpoint */
    int flags;	/* optional flags */
    PVFS_msg_tag_t tag;		/* matching tag */
    void *user_ptr;		/* for use by caller */
    PINT_Request *request;	/* I/O request description */
    /* information about the file that we are accessing */
    PINT_Request_file_data *file_data;
    /* can be used to force use of specific flow protocol */
    enum PVFS_flowproto_type type;        

	/***********************************************************/
    /* fields that can be read publicly upon completion */

    int state;	/* final state of flow */
    PVFS_error error_code;	/* specific errno value if failure */
    PVFS_size total_transfered;	/* total amt. of data xfered */

	/***********************************************************/
    /* fields reserved strictly for internal use */

    FLOW_context_id context_id; /* which context the flow belongs to */
    int flowproto_id;		/* identifies which protocol owns this */
    int priority;	/* priority of this flow */
    struct qlist_head sched_queue_link;	/* used by scheduler */
    void *flow_protocol_data;	/* used by flow protocols */
    PINT_Request_state *request_state;	/* req processor state */
    PVFS_offset current_req_offset;	/* offset of request processing */
    PVFS_offset *offset_array;	/* array of offsets being processed */
    PVFS_size *size_array;	/* array of sizes being processed */

};
typedef struct flow_descriptor flow_descriptor;

/* memalloc flags */
enum
{
    FLOW_SEND_BUFFER = 1,
    FLOW_RECV_BUFFER = 2
};

/* valid flow descriptor states */
enum
{
    FLOW_INITIAL = 1,
    FLOW_SVC_READY = 2,
    FLOW_TRANSMITTING = 4,
    FLOW_COMPLETE = 8,
    FLOW_ERROR = 16,
    FLOW_SRC_ERROR = 32,
    FLOW_DEST_ERROR = 64,
    FLOW_COMPLETE_SHORT = 128,
    FLOW_UNPOSTED = 256
};

#define FLOW_FINISH_MASK  (FLOW_COMPLETE | FLOW_ERROR | FLOW_SRC_ERROR |\
 FLOW_DEST_ERROR | FLOW_COMPLETE_SHORT | FLOW_UNPOSTED)

/********************************************************************
 * flow interface functions
 */


int PINT_flow_initialize(const char *flowproto_list,
			 int flags);

int PINT_flow_finalize(void);

flow_descriptor *PINT_flow_alloc(void);

void PINT_flow_reset(flow_descriptor * flow_d);

void PINT_flow_free(flow_descriptor * flow_d);

int PINT_flow_open_context(FLOW_context_id* context_id);

void PINT_flow_close_context(FLOW_context_id context_id);

int PINT_flow_post(flow_descriptor * flow_d, FLOW_context_id context_id);

int PINT_flow_unpost(flow_descriptor * flow_d);

int PINT_flow_setpriority(flow_descriptor * flow_d,
			  int priority);

int PINT_flow_getpriority(flow_descriptor * flow_d,
			  int * priority);

void *PINT_flow_memalloc(flow_descriptor * flow_d,
			 PVFS_size size,
			 int send_recv_flag);

int PINT_flow_memfree(flow_descriptor * flow_d,
		      void *buffer,
		      int send_recv_flag);

int PINT_flow_test(flow_descriptor * flow_d,
		   int *outcount,
		   int max_idle_time_ms,
		   FLOW_context_id context_id);

int PINT_flow_testsome(int incount,
		       flow_descriptor ** flow_array,
		       int *outcount,
		       int *index_array,
		       int max_idle_time_ms,
		       FLOW_context_id context_id);

int PINT_flow_testcontext(int incount,
			flow_descriptor ** flow_array,
			int *outcount,
			int max_idle_time_ms,
			FLOW_context_id context_id);

int PINT_flow_setinfo(flow_descriptor * flow_d,
		      int option,
		      void *parameter);

int PINT_flow_getinfo(flow_descriptor * flow_d,
		      int option,
		      void *parameter);

#endif /* __FLOW_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
