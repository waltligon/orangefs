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

#include "gen-locks.h"
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
enum flow_endpoint_type
{
    BMI_ENDPOINT = 1,
    TROVE_ENDPOINT = 2,
    MEM_ENDPOINT = 3
};

/* describes BMI endpoints */
struct BMI_endpoint_data
{
    PVFS_BMI_addr_t address;
};

/* describes trove interface endpoints */
struct trove_endpoint_data
{
    PVFS_fs_id coll_id;
    PVFS_handle handle;
};

/* describes memory region endpoints */
struct mem_endpoint_data
{
    void *buffer;
    /* NOTE: there is no buffer size here; that is determined by
     * the memory datatype or the aggregate_size field
     */
};

struct flow_endpoint
{
    enum flow_endpoint_type endpoint_id;
    union
    {
	struct BMI_endpoint_data bmi;
	struct trove_endpoint_data trove;
	struct mem_endpoint_data mem;
    }
    u;
};
typedef struct flow_endpoint flow_endpoint;

/* valid flow descriptor states */
enum flow_state
{
    FLOW_INITIAL = 1,
    FLOW_TRANSMITTING = 2,
    FLOW_COMPLETE = 4,
};

/* supported getinfo types */
enum flow_getinfo_option
{
    FLOWPROTO_TYPE_QUERY = 1,
    FLOW_AMT_COMPLETE_QUERY = 2
};

#define FLOW_MAX_CONTEXTS 16

/********************************************************************
 * flow descriptor
 */

struct flow_descriptor
{
	/**********************************************************/
    /* fields that can be set publicly before posting */

    /* function to be triggered upon completion */
    void(*callback)(struct flow_descriptor* flow_d);

    struct flow_endpoint src;	/* src endpoint */
    struct flow_endpoint dest;	/* dest endpoint */
    PVFS_msg_tag_t tag;		/* matching tag */
    void *user_ptr;		/* for use by caller */
    /* can be used to force use of specific flow protocol */
    enum PVFS_flowproto_type type;        

    PINT_Request *file_req;
    PVFS_offset file_req_offset;
    PINT_Request *mem_req;
    /* NOTE: aggregate_size is _optional_; it can be used to limit the 
     * amount of data transferred through a flow in the absence of a 
     * memory datatype (most commonly done by the pvfs2-server)
     */
    PVFS_size aggregate_size;

    /* information about the datafile that this flow will access */
    PINT_Request_file_data file_data;

	/***********************************************************/
    /* fields that can be read publicly upon completion */

    enum flow_state state;	/* final state of flow */
    PVFS_error error_code;	/* specific errno value if failure */
    PVFS_size total_transfered;	/* total amt. of data xfered */

	/***********************************************************/
    /* fields reserved strictly for internal use */

    gen_mutex_t *flow_mutex;
    int flowproto_id;		/* identifies which protocol owns this */
    void *flow_protocol_data;	/* used by flow protocols */
    /* called upon completion before callback */
    void(*release)(struct flow_descriptor* flow_d);

    PINT_Request_state *file_req_state;
    PINT_Request_state *mem_req_state;
    PINT_Request_result result;
};
typedef struct flow_descriptor flow_descriptor;

/********************************************************************
 * flow interface functions
 */


int PINT_flow_initialize(const char *flowproto_list,
			 int flags);

int PINT_flow_finalize(void);

flow_descriptor *PINT_flow_alloc(void);

void PINT_flow_reset(flow_descriptor * flow_d);

void PINT_flow_free(flow_descriptor * flow_d);

int PINT_flow_post(flow_descriptor * flow_d);

int PINT_flow_cancel(flow_descriptor * flow_d);

int PINT_flow_setinfo(flow_descriptor * flow_d,
		      int option,
		      void *parameter);

int PINT_flow_getinfo(flow_descriptor * flow_d,
		      enum flow_getinfo_option opt,
		      void *parameter);

#endif /* __FLOW_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
