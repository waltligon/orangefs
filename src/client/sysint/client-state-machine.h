/*
 * (C) 2003 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_CLIENT_STATE_MACHINE_H
#define __PVFS2_CLIENT_STATE_MACHINE_H

/*
  NOTE: state-machine.h is included at the bottom so we can define all
  the client-sm structures before it's included
*/
#include "pvfs2-sysint.h"
#include "pvfs2-types.h"
#include "pvfs2-storage.h"
#include "pvfs2-util.h"
#include "PINT-reqproto-encode.h"
#include "job.h"
#include "trove.h"
#include "acache.h"
#include "id-generator.h"

#define PINT_STATE_STACK_SIZE 3

#define MAX_LOOKUP_SEGMENTS PVFS_REQ_LIMIT_PATH_SEGMENT_COUNT
#define MAX_LOOKUP_CONTEXTS PVFS_REQ_LIMIT_MAX_SYMLINK_RESOLUTION_COUNT

/*
  TODO: the following constants should be run-time configurable
  eventually
*/

/* jobs that send or receive request messages will timeout if they do
 * not complete within PVFS2_CLIENT_JOB_TIMEOUT seconds; flows will
 * timeout if they go for more than PVFS2_CLIENT_JOB_TIMEOUT seconds
 * without moving any data.
 */
#define PVFS2_CLIENT_JOB_TIMEOUT 30

/* the maximum number of times to retry restartable client operations */
#define PVFS2_CLIENT_RETRY_LIMIT  5

/* the number of milliseconds to delay before retries */
#define PVFS2_CLIENT_RETRY_DELAY  2000

/*
 * This structure holds everything that we need for the state of a
 * message pair.  We need arrays of these in some cases, so it's
 * convenient to group it like this.
 *
 */
typedef struct PINT_client_sm_msgpair_state_s
{
    /* NOTE: fs_id, handle, retry flag, and comp_fn, should be filled
     * in prior to going into the msgpair code path.
     */
    PVFS_fs_id fs_id;
    PVFS_handle handle;

    /* should be either PVFS_MSGPAIR_RETRY, or PVFS_MSGPAIR_NO_RETRY*/
    int retry_flag;

    /* don't use this -- internal msgpairarray use only */
    int retry_count;

    /* comp_fn called after successful reception and decode of
     * respone, if the msgpair state machine is used for processing.
     */
    int (* comp_fn)(void *sm_p, struct PVFS_server_resp *resp_p, int i);

    /* comp_ct used to keep up with number of operations remaining */
    int comp_ct;

    /* server address */
    PVFS_BMI_addr_t svr_addr;

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

    /* op_status is the code returned from the server, if the
     * operation was actually processed (recv_status.error_code == 0)
     */
    PVFS_error op_status;

    /*
      used in the retry code path to know if we've already completed
      or not (to avoid re-doing the work we've already done)
    */
    int complete;

} PINT_client_sm_msgpair_state;

/* PINT_client_sm_recv_state_s
 *
 * This is used for extra receives, such as acknowledgements from
 * servers at the end of write operations.
 */
typedef struct PINT_client_sm_recv_state_s
{
    int max_resp_sz;
    void *encoded_resp_p;
    job_id_t recv_id;
    job_status_s recv_status;
    PVFS_error op_status;
} PINT_client_sm_recv_state;

struct PINT_client_remove_sm
{
    char *object_name;   /* input parameter */
    int stored_error_code;
    int	retry_count;
};

struct PINT_client_create_sm
{
    char *object_name;                /* input parameter */
    PVFS_sysresp_create *create_resp; /* in/out parameter*/
    PVFS_sys_attr sys_attr;           /* input parameter */

    int retry_count;
    int num_data_files;
    int stored_error_code;

    PINT_dist *dist;
    PVFS_handle metafile_handle;
    PVFS_handle *datafile_handles;
    PVFS_BMI_addr_t *data_server_addrs;
    PVFS_handle_extent_array *io_handle_extent_array;
};

struct PINT_client_mkdir_sm
{
    char *object_name;              /* input parameter  */
    PVFS_sysresp_mkdir *mkdir_resp; /* in/out parameter */
    PVFS_sys_attr sys_attr;         /* input parameter  */

    int retry_count;
    int stored_error_code;
    PVFS_handle metafile_handle;
};

struct PINT_client_symlink_sm
{
    PVFS_object_ref parent_ref;     /* input parameter */
    char *link_name;                /* input parameter */
    char *link_target;              /* input parameter */
    PVFS_sysresp_symlink *sym_resp; /* in/out parameter*/
    PVFS_sys_attr sys_attr;         /* input parameter */

    int retry_count;
    int stored_error_code;
    PVFS_handle symlink_handle;
};

struct PINT_client_getattr_sm
{
    PVFS_object_ref object_ref;           /* input parameter */
    uint32_t attrmask;                    /* input parameter */

    PVFS_size *size_array;                /* from datafile attribs */
    PVFS_sysresp_getattr *getattr_resp_p; /* destination for output */
};

struct PINT_client_setattr_sm
{
    PVFS_object_ref refn;   /* input parameter */
    PVFS_sys_attr sys_attr; /* input parameter */
};

typedef struct
{
    /* the index of the current context (in the context array) */
    int index;

    /* the metafile's dfile server index we're communicating with */
    int server_nr;

    /* the data handle we're responsible for doing I/O on */
    PVFS_handle data_handle;

    /* a reference to the msgpair we're using for communication */
    PINT_client_sm_msgpair_state *msg;

    job_id_t flow_job_id;
    job_status_s flow_status;
    flow_descriptor flow_desc;
    PVFS_msg_tag_t session_tag;

    PINT_client_sm_recv_state write_ack;

    /*
      all *_has_been_posted fields are used at io_analyze_results time
      to know if we should be checking for errors on particular fields
    */
    int msg_send_has_been_posted;
    int msg_recv_has_been_posted;
    int flow_has_been_posted;
    int write_ack_has_been_posted;

    /*
      all *_in_progress fields are used at cancellation time to
      determine what operations are currently in flight
    */
    int msg_send_in_progress;
    int msg_recv_in_progress;
    int flow_in_progress;
    int write_ack_in_progress;

} PINT_client_io_ctx;

struct PINT_client_io_sm
{
    /* input parameters */
    enum PVFS_io_type io_type;
    PVFS_Request file_req;
    PVFS_offset file_req_offset;
    void *buffer;
    PVFS_Request mem_req;

    /* output parameter */
    PVFS_sysresp_io *io_resp_p;

    enum PVFS_flowproto_type flowproto_type;
    enum PVFS_encoding_type encoding;

    int datafile_count;
    int *datafile_index_array;

    int msgpair_completion_count;
    int flow_completion_count;
    int write_ack_completion_count;

    PINT_client_io_ctx *contexts;

    int total_cancellations_remaining;

    int retry_count;
    int stored_error_code;
};

struct PINT_client_flush_sm
{
};

struct PINT_client_readdir_sm
{
    PVFS_object_ref object_ref;         /* looked up */
    PVFS_ds_position pos_token;         /* input parameter */
    int dirent_limit;                   /* input parameter */
    PVFS_sysresp_readdir *readdir_resp; /* in/out parameter*/
};

typedef struct
{
    char *seg_name;
    char *seg_remaining;
    PVFS_object_attr seg_attr;
    PVFS_object_ref seg_starting_refn;
    PVFS_object_ref seg_resolved_refn;
} PINT_client_lookup_sm_segment;

typedef struct
{
    int total_segments;
    int current_segment;
    PINT_client_lookup_sm_segment segments[MAX_LOOKUP_SEGMENTS];
    PVFS_object_ref ctx_starting_refn;
    PVFS_object_ref ctx_resolved_refn;
} PINT_client_lookup_sm_ctx;

struct PINT_client_lookup_sm
{
    char *orig_pathname;              /* input parameter */
    PVFS_object_ref starting_refn;    /* input parameter */
    PVFS_sysresp_lookup *lookup_resp; /* in/out parameter*/
    int follow_link;                  /* input parameter */
    int current_context;
    PINT_client_lookup_sm_ctx contexts[MAX_LOOKUP_CONTEXTS];
};

struct PINT_client_rename_sm
{
    char *entries[2];                /* old/new input entry names */
    PVFS_object_ref parent_refns[2]; /* old/new input parent refns */

    PVFS_object_ref refns[2];        /* old/new object refns */
    int retry_count;
    int stored_error_code;
    int rmdirent_index;
    int target_dirent_exists;
    PVFS_handle old_dirent_handle;
};

struct PINT_client_mgmt_setparam_list_sm 
{
    PVFS_fs_id fs_id;
    enum PVFS_server_param param;
    int64_t value;
    PVFS_id_gen_t *addr_array;
    int count;
    int64_t *old_value_array;
    int *root_check_status_array;
    PVFS_error_details *details;
};

struct PINT_client_mgmt_statfs_list_sm
{
    PVFS_fs_id fs_id;
    struct PVFS_mgmt_server_stat *stat_array;
    int count; 
    PVFS_id_gen_t *addr_array;
    PVFS_error_details *details;
};

struct PINT_client_mgmt_perf_mon_list_sm
{
    PVFS_fs_id fs_id;
    struct PVFS_mgmt_perf_stat **perf_matrix;
    uint64_t *end_time_ms_array;
    int server_count; 
    int history_count; 
    PVFS_id_gen_t *addr_array;
    uint32_t *next_id_array;
    PVFS_error_details *details;
};

struct PINT_client_mgmt_event_mon_list_sm
{
    PVFS_fs_id fs_id;
    struct PVFS_mgmt_event **event_matrix;
    int server_count; 
    int event_count; 
    PVFS_id_gen_t *addr_array;
    PVFS_error_details *details;
};

struct PINT_client_mgmt_iterate_handles_list_sm
{
    PVFS_fs_id fs_id;
    int server_count; 
    PVFS_id_gen_t *addr_array;
    PVFS_handle **handle_matrix;
    int *handle_count_array;
    PVFS_ds_position *position_array;
    PVFS_error_details *details;
};

struct PINT_client_mgmt_get_dfile_array_sm
{
    PVFS_handle *dfile_array;
    int dfile_count;
};

struct PINT_client_truncate_sm
{
    PVFS_size size; /* new logical size of object*/
};

struct PINT_server_get_config_sm
{
    struct PVFS_sys_mntent *mntent;
    char *fs_config_buf;
    char *server_config_buf;
    uint32_t fs_config_buf_size;
    uint32_t server_config_buf_size;
    int persist_config_buffers;
};

typedef struct PINT_client_sm
{
    /*
      internal state machine values; the stack is used for tracking
      movement through nested state machines
    */
    int stackptr;
    union PINT_state_array_values *current_state;
    union PINT_state_array_values *state_stack[PINT_STATE_STACK_SIZE];

    /* the system interface operation type (defined below) */
    int op;

    /* indicates when the operation as a whole is finished */
    int op_complete;

    /* indicates when the operation has been cancelled */
    int op_cancelled;

    /* stores the final operation error code on operation exit */
    PVFS_error error_code;

    int comp_ct; /* used to keep up with completion of multiple
		  * jobs for some states; typically set and
		  * then decremented to zero as jobs complete */

    /* indicates that an ncache hit has been made */ 
    int ncache_hit;

    /* indicates that an acache hit has been made */ 
    int acache_hit;

    /* on acache hit, the attribute here can be used */
    PINT_pinode *pinode;

    /*
      on acache miss, an attribute is typically stored here for later
      insertion into the acache (if possible)
    */
    PVFS_object_attr acache_attr;

    /* generic msgpair used with msgpair substate */
    PINT_client_sm_msgpair_state msgpair;

    /* msgpair array ptr used when operations can be performed
     * concurrently.  this must be allocated within the upper-level
     * state machine and is used with the msgpairarray sm.
     */
    int msgarray_count;
    PINT_client_sm_msgpair_state *msgarray;

    /*
      internal pvfs_object references; used in conjunction with the
      sm_common state machine routines, or otherwise as scratch object
      references during sm processing
    */
    PVFS_object_ref object_ref;
    PVFS_object_ref parent_ref;

    /* used internally in the remove helper state machine */
    int datafile_count;
    PVFS_handle *datafile_handles;

    /* used internally by client-state-machine.c */
    PVFS_sys_op_id sys_op_id;
    void *user_ptr;

    PVFS_credentials *cred_p;
    union
    {
	struct PINT_client_remove_sm remove;
	struct PINT_client_create_sm create;
	struct PINT_client_mkdir_sm mkdir;
	struct PINT_client_symlink_sm sym;
	struct PINT_client_getattr_sm getattr;
	struct PINT_client_setattr_sm setattr;
	struct PINT_client_io_sm io;
	struct PINT_client_flush_sm flush;
	struct PINT_client_readdir_sm readdir;
	struct PINT_client_lookup_sm lookup;
	struct PINT_client_rename_sm rename;
	struct PINT_client_mgmt_setparam_list_sm setparam_list;
	struct PINT_client_truncate_sm  truncate;
	struct PINT_client_mgmt_statfs_list_sm statfs_list;
	struct PINT_client_mgmt_perf_mon_list_sm perf_mon_list;
	struct PINT_client_mgmt_event_mon_list_sm event_mon_list;
	struct PINT_client_mgmt_iterate_handles_list_sm iterate_handles_list;
	struct PINT_client_mgmt_get_dfile_array_sm get_dfile_array;
	struct PINT_server_get_config_sm get_config;
    } u;
} PINT_client_sm;

/* sysint post/test functions */
int PINT_client_state_machine_post(
    PINT_client_sm *sm_p,
    int pvfs_sys_op,
    PVFS_sys_op_id *op_id,
    void *user_ptr);

int PINT_sys_dev_unexp(
    struct PINT_dev_unexp_info *info,
    job_status_s *jstat,
    PVFS_sys_op_id *op_id,
    void *user_ptr);

int PINT_client_state_machine_test(
    PVFS_sys_op_id op_id,
    int *error_code);

int PINT_client_state_machine_testsome(
    PVFS_sys_op_id *op_id_array,
    int *op_count, /* in/out */
    void **user_ptr_array,
    int *error_code_array,
    int timeout_ms);

/* exposed wrapper around the client-state-machine testsome function */
static inline int PINT_sys_testsome(
    PVFS_sys_op_id *op_id_array,
    int *op_count, /* in/out */
    void **user_ptr_array,
    int *error_code_array,
    int timeout_ms)
{
    return PINT_client_state_machine_testsome(
        op_id_array, op_count, user_ptr_array,
        error_code_array, timeout_ms);
}

/* exposed wrappers around the id-generator code */
static inline int PINT_id_gen_safe_register(
    PVFS_sys_op_id *new_id,
    void *item)
{
    return id_gen_safe_register(new_id, item);
}

static inline void *PINT_id_gen_safe_lookup(
    PVFS_sys_op_id id)
{
    return id_gen_safe_lookup(id);
}

static inline int PINT_id_gen_safe_unregister(
    PVFS_sys_op_id id)
{
    return id_gen_safe_unregister(id);
}

/* used with post call to tell the system what state machine to use
 * when processing a new PINT_client_sm structure.
 */
enum
{
    PVFS_SYS_REMOVE                = 1,
    PVFS_SYS_CREATE                = 2,
    PVFS_SYS_MKDIR                 = 3,
    PVFS_SYS_SYMLINK               = 4,
    PVFS_SYS_GETATTR               = 5,
    PVFS_SYS_IO                    = 6,
    PVFS_SYS_FLUSH                 = 7,
    PVFS_SYS_TRUNCATE              = 8,
    PVFS_SYS_READDIR               = 9,
    PVFS_SYS_SETATTR               = 10,
    PVFS_SYS_LOOKUP                = 11,
    PVFS_SYS_RENAME                = 12,
    PVFS_MGMT_SETPARAM_LIST        = 70,
    PVFS_MGMT_NOOP                 = 71,
    PVFS_MGMT_STATFS_LIST          = 72,
    PVFS_MGMT_PERF_MON_LIST        = 73,
    PVFS_MGMT_ITERATE_HANDLES_LIST = 74,
    PVFS_MGMT_GET_DFILE_ARRAY      = 75,
    PVFS_MGMT_EVENT_MON_LIST       = 76,
    PVFS_SERVER_GET_CONFIG         = 77,
    PVFS_CLIENT_JOB_TIMER          = 200,
    PVFS_DEV_UNEXPECTED            = 300
};

/* prototypes of helper functions */
int PINT_serv_prepare_msgpair(
    PVFS_object_ref object_ref,
    struct PVFS_server_req *req_p,
    struct PINT_encoded_msg *encoded_req_out_p,
    void **encoded_resp_out_pp,
    PVFS_BMI_addr_t *svr_addr_p,
    int *max_resp_sz_out_p,
    PVFS_msg_tag_t *session_tag_out_p);

int PINT_serv_decode_resp(
    PVFS_fs_id fs_id,
    void *encoded_resp_p,
    struct PINT_decoded_msg *decoded_resp_p,
    PVFS_BMI_addr_t *svr_addr_p,
    int actual_resp_sz,
    struct PVFS_server_resp **resp_out_pp);

int PINT_serv_free_msgpair_resources(
    struct PINT_encoded_msg *encoded_req_p,
    void *encoded_resp_p,
    struct PINT_decoded_msg *decoded_resp_p,
    PVFS_BMI_addr_t *svr_addr_p,
    int max_resp_sz);

int PINT_serv_msgpairarray_resolve_addrs(
    int count, 
    PINT_client_sm_msgpair_state* msgarray);

int PINT_client_bmi_cancel(job_id_t id);

int PINT_client_io_cancel(job_id_t id);

/* internal non-blocking helper methods */
int PINT_client_wait_internal(
    PVFS_sys_op_id op_id,
    const char *in_op_str,
    int *out_error,
    const char *in_class_str);

void PINT_sys_release(
    PVFS_sys_op_id op_id);

/* internal helper macros */
#define PINT_sys_wait(op_id, in_op_str, out_error)            \
PINT_client_wait_internal(op_id, in_op_str, out_error, "sys")

#define PINT_mgmt_wait(op_id, in_op_str, out_error)           \
PINT_client_wait_internal(op_id, in_op_str, out_error, "mgmt")

#define PINT_mgmt_release(op_id) PINT_sys_release(op_id)

#define PINT_init_sysint_credentials(sm_p_cred_p, user_cred_p)\
do {                                                          \
    sm_p_cred_p = PVFS_util_dup_credentials(user_cred_p);     \
    if (!sm_p_cred_p)                                         \
    {                                                         \
        gossip_lerr("Failed to copy user credentials\n");     \
        free(sm_p);                                           \
        return -PVFS_ENOMEM;                                  \
    }                                                         \
} while(0)

/* misc helper methods */
struct server_configuration_s *PINT_get_server_config_struct(
    PVFS_fs_id fs_id);

/************************************
 * state-machine.h included here
 ************************************/
#define PINT_OP_STATE       PINT_client_sm
#if 0
#define PINT_OP_STATE_TABLE PINT_server_op_table
#endif

#include "state-machine.h"

/* system interface function state machines */
extern struct PINT_state_machine_s pvfs2_client_remove_sm;
extern struct PINT_state_machine_s pvfs2_client_create_sm;
extern struct PINT_state_machine_s pvfs2_client_mkdir_sm;
extern struct PINT_state_machine_s pvfs2_client_symlink_sm;
extern struct PINT_state_machine_s pvfs2_client_getattr_sm;
extern struct PINT_state_machine_s pvfs2_client_setattr_sm;
extern struct PINT_state_machine_s pvfs2_client_io_sm;
extern struct PINT_state_machine_s pvfs2_client_flush_sm;
extern struct PINT_state_machine_s pvfs2_client_readdir_sm;
extern struct PINT_state_machine_s pvfs2_client_lookup_sm;
extern struct PINT_state_machine_s pvfs2_client_rename_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_setparam_list_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_statfs_list_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_perf_mon_list_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_event_mon_list_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_iterate_handles_list_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_get_dfile_array_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_noop_sm;
extern struct PINT_state_machine_s pvfs2_client_truncate_sm;
extern struct PINT_state_machine_s pvfs2_client_job_timer_sm;
extern struct PINT_state_machine_s pvfs2_server_get_config_sm;

/* nested state machines (helpers) */
extern struct PINT_state_machine_s pvfs2_client_msgpairarray_sm;
extern struct PINT_state_machine_s pvfs2_client_getattr_acache_sm;
extern struct PINT_state_machine_s pvfs2_client_lookup_ncache_sm;
extern struct PINT_state_machine_s pvfs2_client_remove_helper_sm;
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
