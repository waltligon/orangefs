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

#include "pvfs2-sysint.h"
#include "pvfs2-types.h"
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

    /* fs_id and handle to be operated on, if available */

    PVFS_fs_id fs_id;
    PVFS_handle handle;

    /* comp_fn called after successful reception and decode of respone,
     * if the msgpair state machine is used for processing.
     */
    int (* comp_fn)(void *sm_p, /* actually (struct PINT_client_sm *) */
		    struct PVFS_server_resp *resp_p,
		    int i);

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

    /* op_status is the code returned from the server, if the operation
     * was actually processed (recv_status.error_code == 0)
     */
    PVFS_error op_status;
} PINT_client_sm_msgpair_state;

/* PINT_client_sm_recv_state_s
 *
 * This is used for extra receives, such as acknowledgements from
 * servers at the end of write operations.
 */
typedef struct PINT_client_sm_recv_state_s {
    int max_resp_sz;
    void *encoded_resp_p;
    job_id_t recv_id;
    job_status_s recv_status;
    PVFS_error op_status;
} PINT_client_sm_recv_state;

/* PINT_client_remove_sm */
struct PINT_client_remove_sm {
    char                         *object_name;    /* input parameter */
    PVFS_pinode_reference         parent_ref;     /* input parameter */
    PVFS_pinode_reference         object_ref;     /* looked up */
    int                           datafile_count; /* from attribs */
    PVFS_handle                  *datafile_handles;
    PINT_client_sm_msgpair_state *msgpair;        /* for datafile remove */
};

/* PINT_client_create_sm */
struct PINT_client_create_sm {
    char                         *object_name;    /* input parameter */
    PVFS_sysresp_create          *create_resp;    /* in/out parameter*/
    PVFS_sys_attr                *sys_attr;       /* input parameter */
    int                          num_data_files;
    bmi_addr_t                   meta_server_addr;
    bmi_addr_t                   *data_server_addrs;
    PVFS_handle_extent_array     *io_handle_extent_array;
    PVFS_handle                  metafile_handle;
    PVFS_handle                  *datafile_handles;
    PVFS_Dist                    *dist;
};

/* PINT_client_mkdir_sm */
struct PINT_client_mkdir_sm {
/* ENOTIMPL ;-) */
};

/* PINT_client_symlink_sm */
struct PINT_client_symlink_sm {
    PVFS_pinode_reference        parent_ref;     /* input parameter */
    char                         *link_name;      /* input parameter */
    char                         *link_target;    /* input parameter */
    PVFS_sysresp_symlink         *sym_resp;       /* in/out parameter*/
    PVFS_sys_attr                *sys_attr;       /* input parameter */
    bmi_addr_t                   meta_server_addr;
    PVFS_handle                  symlink_handle;
};

/* PINT_client_getattr_sm */
struct PINT_client_getattr_sm {
    PVFS_pinode_reference object_ref;     /* input parameter */
    uint32_t              attrmask;       /* input parameter */
    int                   datafile_count; /* from object attribs */
    PVFS_handle          *datafile_handles;
    PVFS_Dist            *dist_p;
    uint32_t              dist_size;
    PVFS_size            *size_array;     /* from datafile attribs */
    PVFS_sysresp_getattr *getattr_resp_p; /* destination for output */
};

/* PINT_client_io_sm
 *
 * Data specific to I/O operations on the client side.
 */
struct PINT_client_io_sm {
    /* input parameters */
    PVFS_pinode_reference object_ref;
    enum PVFS_io_type     io_type;
    PVFS_Request          file_req;
    PVFS_offset           file_req_offset;
    void                 *buffer;
    PVFS_Request          mem_req;

    /* cached from object attributes */
    int                   datafile_count;
    PVFS_handle          *datafile_handles;
    PVFS_Dist            *dist_p;
    uint32_t              dist_size;

    /* data regarding flows */
    int                     flow_comp_ct;
    flow_descriptor        *flow_array;
    job_status_s           *flow_status_array;
    enum PVFS_flowproto_type flowproto_type;

    /* session tags, used in all messages */
    PVFS_msg_tag_t         *session_tag_array;

    /* data regarding final acknowledgements (writes only) */
    int                        ack_comp_ct;
    PINT_client_sm_recv_state *ackarray;

    /* output parameter */
    PVFS_sysresp_io      *io_resp_p;
};

/* PINT_client_flush_sm */
struct PINT_client_flush_sm {
    PVFS_pinode_reference	object_ref; /* input parameter */
    int				datafile_count; /* from attribs */
    PVFS_handle			*datafile_handles;
    PINT_client_sm_msgpair_state *msgpair; /* used in datafile flush */
};

struct PINT_client_mgmt_setparam_list_sm 
{
    PVFS_fs_id fs_id;
    enum PVFS_server_param param;
    int64_t value;
    PVFS_id_gen_t* addr_array;
    int count;
    int64_t* old_value_array;
};

struct PINT_client_mgmt_statfs_list_sm
{
    PVFS_fs_id fs_id;
    struct PVFS_mgmt_server_stat* stat_array;
    int count; 
    PVFS_id_gen_t* addr_array;
};

struct PINT_client_mgmt_perf_mon_list_sm
{
    struct PVFS_mgmt_perf_stat** perf_matrix;
    uint64_t* end_time_ms;
    int server_count; 
    int history_count; 
    PVFS_id_gen_t* addr_array;
    uint32_t* next_id_array;
};

struct PINT_client_mgmt_iterate_handles_list_sm
{
    PVFS_fs_id fs_id;
    int server_count; 
    PVFS_id_gen_t* addr_array;
    PVFS_handle** handle_matrix;
    int* handle_count_array;
    PVFS_ds_position* position_array;
};

struct PINT_client_mgmt_get_dfile_array_sm
{
    PVFS_pinode_reference pinode_refn;
    PVFS_handle* dfile_array;
    int dfile_count;
};

struct PINT_client_truncate_sm {
    PVFS_pinode_reference	object_ref;	/* input parameter */
    PVFS_size			size;		/* new logical size of object*/
    int				datafile_count;	/* from attribs */
    PVFS_handle			*datafile_handles;
    PVFS_Dist			*distribution;	/* datafile distribution meth*/
    PINT_client_sm_msgpair_state *msgpair;	/* used in truncate op */
};

typedef struct PINT_client_sm {
    /* STATE MACHINE VALUES */
    int stackptr; /* stack of contexts for nested state machines */
    union PINT_state_array_values *current_state; /* xxx */
    union PINT_state_array_values *state_stack[PINT_STATE_STACK_SIZE];

    int op; /* holds pvfs system operation type, defined up above */

    /* CLIENT SM VALUES */
    int op_complete; /* used to indicate that the operation as a 
		      * whole is finished.
		      */
    PVFS_error error_code; /* used to hold final job status so client
			    * can determine what finally happened
			    */
    int comp_ct; /* used to keep up with completion of multiple
		  * jobs for some some states; typically set and
		  * then decremented to zero as jobs complete */

    int pcache_hit; /* set if pinode was from pcache */
    PINT_pinode *pinode; /* filled in on pcache hit */
    PVFS_object_attr pcache_attr; /* a scratch attr space */

    /* we need this for create */
    struct server_configuration_s *server_config;

    /* generic msgpair used with msgpair substate */
    PINT_client_sm_msgpair_state msgpair;

    /* msgpair array ptr used when operations can be performed concurrently.
     * obviously this has to be allocated within the upper-level state
     * machine.  used with msgpairs substate typically.
     */
    int msgarray_count;
    PINT_client_sm_msgpair_state *msgarray;

    PVFS_pinode_reference parent_ref;
    PVFS_credentials *cred_p;
    union
    {
	struct PINT_client_remove_sm remove;
	struct PINT_client_create_sm create;
	struct PINT_client_mkdir_sm mkdir;
	struct PINT_client_symlink_sm sym;
	struct PINT_client_getattr_sm getattr;
	struct PINT_client_io_sm io;
	struct PINT_client_flush_sm flush;
	struct PINT_client_mgmt_setparam_list_sm setparam_list;
	struct PINT_client_truncate_sm  truncate;
	struct PINT_client_mgmt_statfs_list_sm statfs_list;
	struct PINT_client_mgmt_perf_mon_list_sm perf_mon_list;
	struct PINT_client_mgmt_iterate_handles_list_sm iterate_handles_list;
	struct PINT_client_mgmt_get_dfile_array_sm get_dfile_array;
    } u;
} PINT_client_sm;

/* prototypes of post/test functions */
int PINT_client_state_machine_post(PINT_client_sm *sm_p, int pvfs_sys_op);
int PINT_client_state_machine_test(void);

/* used with post call to tell the system what state machine to use
 * when processing a new PINT_client_sm structure.
 */
enum {
    PVFS_SYS_REMOVE  = 1,
    PVFS_SYS_CREATE  = 2,
    PVFS_SYS_MKDIR   = 3,
    PVFS_SYS_SYMLINK = 4,
    PVFS_SYS_GETATTR = 5,
    PVFS_SYS_IO      = 6,
    PVFS_SYS_FLUSH   = 7,
    PVFS_SYS_TRUNCATE= 8,
    PVFS_MGMT_SETPARAM_LIST = 9,
    PVFS_MGMT_NOOP   = 10,
    PVFS_MGMT_STATFS_LIST = 11,
    PVFS_MGMT_PERF_MON_LIST = 12,
    PVFS_MGMT_ITERATE_HANDLES_LIST = 13,
    PVFS_MGMT_GET_DFILE_ARRAY = 14
};

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

/* TODO: is this the right name for this function? */
int PINT_serv_msgpairarray_resolve_addrs(int count, 
    PINT_client_sm_msgpair_state* msgarray);

/* INCLUDE STATE-MACHINE.H DOWN HERE */
#define PINT_OP_STATE       PINT_client_sm
#if 0
#define PINT_OP_STATE_TABLE PINT_server_op_table
#endif

#include "state-machine.h"

/* misc helper methods */
struct server_configuration_s *PINT_get_server_config_struct(void);

/* system interface function state machines */
extern struct PINT_state_machine_s pvfs2_client_remove_sm;
extern struct PINT_state_machine_s pvfs2_client_create_sm;
/* extern struct PINT_state_machine_s pvfs2_client_mkdir_sm; */
extern struct PINT_state_machine_s pvfs2_client_symlink_sm;
extern struct PINT_state_machine_s pvfs2_client_getattr_sm;
extern struct PINT_state_machine_s pvfs2_client_io_sm;
extern struct PINT_state_machine_s pvfs2_client_flush_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_setparam_list_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_statfs_list_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_perf_mon_list_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_iterate_handles_list_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_get_dfile_array_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_noop_sm;
extern struct PINT_state_machine_s pvfs2_client_truncate_sm;

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
