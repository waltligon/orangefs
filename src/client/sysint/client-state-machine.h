/*
 * (C) 2003 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_CLIENT_STATE_MACHINE_H
#define __PVFS2_CLIENT_STATE_MACHINE_H

/* NOTE: STATE-MACHINE.H IS INCLUDED AT THE BOTTOM!  THIS IS SO WE CAN
 * DEFINE ALL THE STRUCTURES WE NEED BEFORE WE INCLUDE IT.
 */

#include "pvfs2-storage.h"
#include "PINT-reqproto-encode.h"
#include "job.h"
#include "trove.h"
#include "pcache.h"

#define PINT_STATE_STACK_SIZE 3

/* NOTE:
 * This structure holds everything that we need for the state of a
 * message pair.  We need arrays of these in some cases, so it's
 * convenient to group it like this.
 *
 */
typedef struct PINT_client_sm_msgpair_state_s {
    /* NOTE: these top three elements: fs_id, handle, and comp_fn,
     * should be filled in prior to going into the msgpair code path.
     */

    /* fs_id and handle used to map to server */
    PVFS_fs_id fs_id;
    PVFS_handle handle;
    /* comp_fn called after successful reception and decode of respone,
     * if the msgpair state machine is used for processing.
     */
    int (* comp_fn)(void *sm_p, /* actually (struct PINT_client_sm *) */
		    struct PVFS_server_resp *resp_p);

    /* comp_ct used to keep up with number of operations remaining */
    int comp_ct;

    /* server address */
    bmi_addr_t svr_addr;

    /* req and encoded_req are used to send a request */
    struct PVFS_server_req req;
    struct PINT_encoded_msg encoded_req;

    /* max_resp_sz, svr_addr, and encoded_resp_p used to recv a response */
    int max_resp_sz;
    void *encoded_resp_p;

    /* send_id, recv_id used to track completion of operations */
    job_id_t send_id, recv_id;
    /* send_status, recv_status used for error handling etc. */
    job_status_s send_status, recv_status;
} PINT_client_sm_msgpair_state;


struct PINT_client_remove_sm {
    char                         *object_name; /* input parameter */
    PVFS_pinode_reference         parent_ref;  /* input parameter */
    PVFS_pinode_reference         object_ref;  /* looked up */
    int                           datafile_count; /* from attribs */
    PVFS_handle                  *datafile_handles;
    PINT_client_sm_msgpair_state *msgpair; /* used in datafile remove */
};

typedef struct PINT_client_sm {
    /* STATE MACHINE VALUES */
    int stackptr; /* stack of contexts for nested state machines */
    union PINT_state_array_values *current_state; /* xxx */
    union PINT_state_array_values *state_stack[PINT_STATE_STACK_SIZE];

#if 0
    int op; /* NOTE: THIS IS A HACK AND IS NOT REALLY NEEDED BY CLIENT. */
#endif

    /* CLIENT SM VALUES */
    int op_complete; /* used to indicate that the operation as a 
		      * whole is finished.
		      */
    job_status_s status; /* used to hold final job status so client
			  * can determine what finally happened
			  */
    int comp_ct; /* used to keep up with completion of multiple
		  * jobs for some some states; typically set and
		  * then decremented to zero as jobs complete */

    int pcache_lock; /* used to indicate that we have a lock to release */
    PINT_pinode *object_pinode_p;

    /* generic msgpair used with msgpair substate */
    PINT_client_sm_msgpair_state msgpair;

    /* msgpair array ptr used when operations can be performed concurrently.
     * obviously this has to be allocated within the upper-level state
     * machine.  used with msgpairs substate typically.
     *
     * Q: DO WE WANT TO GET RID OF msgpair ABOVE AND FORCE INTO JUST
     *    USING THIS?  I DON'T THINK SO...
     */
    int msgarray_count;
    PINT_client_sm_msgpair_state *msgarray;

    /* req and encoded_req are needed to send a request */
    struct PVFS_server_req req;
    struct PINT_encoded_msg encoded_req;

    /* max_resp_sz, svr_addr, and encoded_resp_p needed to recv a response */
    int max_resp_sz;
    bmi_addr_t svr_addr;
    void *encoded_resp_p;

    PVFS_credentials *cred_p;
    union {
	struct PINT_client_remove_sm remove;
    } u;
} PINT_client_sm;

/* prototypes of post/test functions */
int PINT_client_state_machine_post(PINT_client_sm *sm_p);
int PINT_client_state_machine_test(void);


/* prototypes of helper functions */
int PINT_serv_prepare_msgpair(PVFS_pinode_reference object_ref,
			      struct PVFS_server_req *req_p,
			      struct PINT_encoded_msg *encoded_req_out_p,
			      void **encoded_resp_out_pp,
			      bmi_addr_t *svr_addr_p,
			      int *max_resp_sz_out_p,
			      PVFS_msg_tag_t *session_tag_out_p);

int PINT_serv_decode_resp(void *encoded_resp_p,
			  struct PINT_decoded_msg *decoded_resp_p,
			  bmi_addr_t *svr_addr_p,
			  int actual_resp_sz,
			  struct PVFS_server_resp **resp_out_pp);

int PINT_serv_free_msgpair_resources(struct PINT_encoded_msg *encoded_req_p,
				     void *encoded_resp_p,
				     struct PINT_decoded_msg *decoded_resp_p,
				     bmi_addr_t *svr_addr_p,
				     int max_resp_sz);


/* INCLUDE STATE-MACHINE.H DOWN HERE */
#define PINT_OP_STATE       PINT_client_sm
#if 0
#define PINT_OP_STATE_TABLE PINT_server_op_table
#endif

#include "state-machine.h"

/* system interface function state machines */
extern struct PINT_state_machine_s pvfs2_client_remove_sm;

/* nested state machines (helpers) */
extern struct PINT_state_machine_s pvfs2_client_msgpair_sm;
extern struct PINT_state_machine_s pvfs2_client_msgpairarray_sm;
extern struct PINT_state_machine_s pvfs2_client_getattr_pcache_sm;
extern struct PINT_state_machine_s pvfs2_client_lookup_dcache_sm;

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif
