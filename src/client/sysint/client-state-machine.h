/*
 * (C) 2003 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  Declarations for state machine processing on clients.
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
#include "msgpairarray.h"
#include "pint-sysint-utils.h"

/* skip everything except #includes if __SM_CHECK_DEP is already defined; this
 * allows us to get the dependencies right for msgpairarray.sm which relies
 * on conflicting headers for dependency information
 */
#ifndef __SM_CHECK_DEP

#define PINT_STATE_STACK_SIZE 3

#define MAX_LOOKUP_SEGMENTS PVFS_REQ_LIMIT_PATH_SEGMENT_COUNT
#define MAX_LOOKUP_CONTEXTS PVFS_REQ_LIMIT_MAX_SYMLINK_RESOLUTION_COUNT

/* Default client timeout in seconds used to set the timeout for jobs that
 * send or receive request messages.
 */
#define PVFS2_CLIENT_JOB_BMI_TIMEOUT_DEFAULT 30

/* Default number of times to retry restartable client operations. */
#define PVFS2_CLIENT_RETRY_LIMIT_DEFAULT  (5)

/* Default number of milliseconds to delay before retries */
#define PVFS2_CLIENT_RETRY_DELAY_MS_DEFAULT  2000


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
    PVFS_sysresp_getattr *getattr_resp_p; /* destination for output */
};

struct PINT_client_setattr_sm
{
    PVFS_sys_attr sys_attr; /* input parameter */
};

struct PINT_client_mgmt_remove_dirent_sm
{
    char *entry;
};

struct PINT_client_mgmt_create_dirent_sm
{
    char *entry;
    PVFS_handle entry_handle;
};

struct PINT_client_mgmt_get_dirdata_handle_sm
{
    PVFS_handle *dirdata_handle;
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
    PINT_sm_msgpair_state msg;

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

    int *datafile_index_array;
    int datafile_count;

    int msgpair_completion_count;
    int flow_completion_count;
    int write_ack_completion_count;

    PINT_client_io_ctx *contexts;
    int context_count;

    int total_cancellations_remaining;

    int retry_count;
    int stored_error_code;

    PVFS_size total_size;

    PVFS_size * dfile_size_array;
    int small_io;
};

struct PINT_client_flush_sm
{
};

struct PINT_client_readdir_sm
{
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
    int skipped_final_resolution;
    int current_context;
    PINT_client_lookup_sm_ctx contexts[MAX_LOOKUP_CONTEXTS];
};

struct PINT_client_rename_sm
{
    char *entries[2];                /* old/new input entry names */
    PVFS_object_ref parent_refns[2]; /* old/new input parent refns */

    PVFS_object_ref refns[2];        /* old/new object refns */
    PVFS_ds_type types[2];           /* old/new object types */
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
    uint64_t *old_value_array;
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

typedef struct PINT_sm_getattr_state
{
    PVFS_object_ref object_ref;

   /* request sys attrmask.  Some combination of
     * PVFS_ATTR_SYS_*
     */
    uint32_t req_attrmask;
    
    /*
      Either from the acache or full getattr op, this is the resuling
      attribute that can be used by calling state machines
    */
    PVFS_object_attr attr;

    PVFS_ds_type ref_type;

    PVFS_size * size_array;
    PVFS_size size;
    
} PINT_sm_getattr_state;

#define PINT_SM_GETATTR_STATE_FILL(_state, _objref, _mask, _reftype) \
    do { \
        memset(&(_state), 0, sizeof(PINT_sm_getattr_state)); \
        (_state).object_ref.fs_id = (_objref).fs_id; \
        (_state).object_ref.handle = (_objref).handle; \
        (_state).req_attrmask = _mask; \
        (_state).ref_type = _reftype; \
    } while(0)

#define PINT_SM_GETATTR_STATE_CLEAR(_state) \
    do { \
        PINT_free_object_attr(&(_state).attr); \
        memset(&(_state), 0, sizeof(PINT_sm_getattr_state)); \
    } while(0)

#define PINT_SM_DATAFILE_SIZE_ARRAY_INIT(_array, _count) \
    do { \
        (*(_array)) = malloc(sizeof(PVFS_size) * (_count)); \
        memset(*(_array), 0, (sizeof(PVFS_size) * (_count))); \
    } while(0)

#define PINT_SM_DATAFILE_SIZE_ARRAY_DESTROY(_array) \
    do { \
        free(*(_array)); \
        *(_array) = NULL; \
    } while(0)

struct PINT_client_geteattr_sm
{
    int32_t nkey;
    PVFS_ds_keyval *key_array;
    PVFS_size *size_array;
    PVFS_sysresp_geteattr *resp_p;
};

struct PINT_client_seteattr_sm
{
    int32_t nkey;
    int32_t flags; /* flags specify if attrs should not exist (XATTR_CREATE) or
                      if they should exist (XATTR_REPLACE) or neither */
    PVFS_ds_keyval *key_array;
    PVFS_ds_keyval *val_array;
};

struct PINT_client_deleattr_sm
{
    PVFS_ds_keyval *key_p;
};

struct PINT_client_listeattr_sm
{
    PVFS_ds_position pos_token;         /* input parameter */
    int32_t nkey;                       /* input parameter */
    PVFS_size *size_array;              /* Input/Output */
    PVFS_sysresp_listeattr *resp_p;     /* Output */
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

    /* generic getattr used with getattr sub state machines */
    PINT_sm_getattr_state getattr;

    /* msgpair scratch space used within msgpairarray substatemachine */
    /* if you have only a single msg pair you may point sm_p->msgarray */
    /* at this.  Otherwise leave it alone */
    PINT_sm_msgpair_state msgpair;

    /* msgpair array ptr used when operations can be performed
     * concurrently.  this must be allocated within the upper-level
     * state machine and is used with the msgpairarray sm.
     */
    int msgarray_count;
    PINT_sm_msgpair_state *msgarray;
    PINT_sm_msgpair_params msgarray_params;

    PVFS_object_ref object_ref;
    PVFS_object_ref parent_ref;

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
        struct PINT_client_mgmt_remove_dirent_sm mgmt_remove_dirent;
        struct PINT_client_mgmt_create_dirent_sm mgmt_create_dirent;
        struct PINT_client_mgmt_get_dirdata_handle_sm mgmt_get_dirdata_handle;
	struct PINT_server_get_config_sm get_config;
	struct PINT_client_geteattr_sm geteattr;
	struct PINT_client_seteattr_sm seteattr;
	struct PINT_client_deleattr_sm deleattr;
	struct PINT_client_listeattr_sm listeattr;
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

/* debugging method for getting a string macthing the op_type */
char *PINT_client_get_name_str(int op_type);

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
    PVFS_SYS_GETEATTR              = 13,
    PVFS_SYS_SETEATTR              = 14,
    PVFS_SYS_DELEATTR              = 15,
    PVFS_SYS_LISTEATTR             = 16,
    PVFS_SYS_SMALL_IO              = 17,
    PVFS_MGMT_SETPARAM_LIST        = 70,
    PVFS_MGMT_NOOP                 = 71,
    PVFS_MGMT_STATFS_LIST          = 72,
    PVFS_MGMT_PERF_MON_LIST        = 73,
    PVFS_MGMT_ITERATE_HANDLES_LIST = 74,
    PVFS_MGMT_GET_DFILE_ARRAY      = 75,
    PVFS_MGMT_EVENT_MON_LIST       = 76,
    PVFS_MGMT_REMOVE_OBJECT        = 77,
    PVFS_MGMT_REMOVE_DIRENT        = 78,
    PVFS_MGMT_CREATE_DIRENT        = 79,
    PVFS_MGMT_GET_DIRDATA_HANDLE   = 80,
    PVFS_SERVER_GET_CONFIG         = 200,
    PVFS_CLIENT_JOB_TIMER          = 300,
    PVFS_DEV_UNEXPECTED            = 400
};

#define PVFS_OP_SYS_MAXVAL 69
#define PVFS_OP_MGMT_MAXVAL 199

int PINT_client_io_cancel(job_id_t id);

/* internal non-blocking helper methods */
int PINT_client_wait_internal(
    PVFS_sys_op_id op_id,
    const char *in_op_str,
    int *out_error,
    const char *in_class_str);

void PINT_sys_release(PVFS_sys_op_id op_id);

/* internal helper macros */
/** Waits for completion of a system interface function. */
#define PINT_sys_wait(op_id, in_op_str, out_error)            \
PINT_client_wait_internal(op_id, in_op_str, out_error, "sys")

#define PINT_mgmt_wait(op_id, in_op_str, out_error)           \
PINT_client_wait_internal(op_id, in_op_str, out_error, "mgmt")

#define PINT_mgmt_release(op_id) PINT_sys_release(op_id)

#define PINT_init_sysint_credentials(sm_p_cred_p, user_cred_p)\
do {                                                          \
    if (user_cred_p == NULL)                                  \
    {                                                         \
        gossip_lerr("Invalid user credentials! (nil)\n");     \
        free(sm_p);                                           \
        return -PVFS_EINVAL;                                  \
    }                                                         \
    sm_p_cred_p = PVFS_util_dup_credentials(user_cred_p);     \
    if (!sm_p_cred_p)                                         \
    {                                                         \
        gossip_lerr("Failed to copy user credentials\n");     \
        free(sm_p);                                           \
        return -PVFS_ENOMEM;                                  \
    }                                                         \
} while(0)

#define PINT_init_msgarray_params(msgarray_params_ptr, __fsid)     \
do {                                                               \
    PINT_sm_msgpair_params *mpp = msgarray_params_ptr;             \
    struct server_configuration_s *server_config =                 \
        PINT_get_server_config_struct(__fsid);                     \
    mpp->job_context = pint_client_sm_context;                     \
    if (server_config)                                             \
    {                                                              \
        mpp->job_timeout = server_config->client_job_bmi_timeout;  \
        mpp->retry_limit = server_config->client_retry_limit;      \
        mpp->retry_delay = server_config->client_retry_delay_ms;   \
    }                                                              \
    else                                                           \
    {                                                              \
        mpp->job_timeout = PVFS2_CLIENT_JOB_BMI_TIMEOUT_DEFAULT;   \
        mpp->retry_limit = PVFS2_CLIENT_RETRY_LIMIT_DEFAULT;       \
        mpp->retry_delay = PVFS2_CLIENT_RETRY_DELAY_MS_DEFAULT;    \
    }                                                              \
    PINT_put_server_config_struct(server_config);                  \
} while(0)

#define PINT_init_msgpair(sm_p, msg_p)                         \
do {                                                           \
    msg_p = &sm_p->msgpair;                                    \
    memset(msg_p, 0, sizeof(PINT_sm_msgpair_state));           \
    if (sm_p->msgarray && (sm_p->msgarray != &(sm_p->msgpair)))\
    {                                                          \
        free(sm_p->msgarray);                                  \
        sm_p->msgarray = NULL;                                 \
    }                                                          \
    sm_p->msgarray = msg_p;                                    \
    sm_p->msgarray_count = 1;                                  \
} while(0)

/* misc helper methods */
struct server_configuration_s *PINT_get_server_config_struct(
    PVFS_fs_id fs_id);

/************************************
 * state-machine.h included here
 ************************************/
#define PINT_OP_STATE       PINT_client_sm

/* This macro allows the generic state-machine-fns.h locate function
 * to access the appropriate sm struct based on the client operation index
 * from the above enum.  Because the enum starts management operations at
 * 70, the management table was separated out from the sys table and the
 * necessary checks and subtractions are made in this macro.
 */
#define PINT_OP_STATE_GET_MACHINE(_op) \
    ((_op <= PVFS_OP_SYS_MAXVAL) ? (PINT_client_sm_sys_table[_op - 1].sm) : \
     ((_op <= PVFS_OP_MGMT_MAXVAL) ?  \
      (PINT_client_sm_mgmt_table[_op - PVFS_OP_SYS_MAXVAL - 1].sm) : \
      ((_op == PVFS_SERVER_GET_CONFIG) ? (&pvfs2_server_get_config_sm) : \
       ((_op == PVFS_CLIENT_JOB_TIMER) ? (&pvfs2_client_job_timer_sm) : NULL))))

struct PINT_client_op_entry_s
{
    struct PINT_state_machine_s * sm;
};
                                                                    
extern struct PINT_client_op_entry_s PINT_client_sm_sys_table[];
extern struct PINT_client_op_entry_s PINT_client_sm_mgmt_table[];

/* system interface function state machines */
extern struct PINT_state_machine_s pvfs2_client_remove_sm;
extern struct PINT_state_machine_s pvfs2_client_create_sm;
extern struct PINT_state_machine_s pvfs2_client_mkdir_sm;
extern struct PINT_state_machine_s pvfs2_client_symlink_sm;
extern struct PINT_state_machine_s pvfs2_client_sysint_getattr_sm;
extern struct PINT_state_machine_s pvfs2_client_getattr_sm;
extern struct PINT_state_machine_s pvfs2_client_datafile_getattr_sizes_sm;
extern struct PINT_state_machine_s pvfs2_client_setattr_sm;
extern struct PINT_state_machine_s pvfs2_client_io_sm;
extern struct PINT_state_machine_s pvfs2_client_small_io_sm;
extern struct PINT_state_machine_s pvfs2_client_flush_sm;
extern struct PINT_state_machine_s pvfs2_client_readdir_sm;
extern struct PINT_state_machine_s pvfs2_client_lookup_sm;
extern struct PINT_state_machine_s pvfs2_client_rename_sm;
extern struct PINT_state_machine_s pvfs2_client_truncate_sm;
extern struct PINT_state_machine_s pvfs2_client_job_timer_sm;
extern struct PINT_state_machine_s pvfs2_server_get_config_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_setparam_list_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_statfs_list_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_perf_mon_list_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_event_mon_list_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_iterate_handles_list_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_get_dfile_array_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_noop_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_remove_object_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_remove_dirent_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_create_dirent_sm;
extern struct PINT_state_machine_s pvfs2_client_mgmt_get_dirdata_handle_sm;
extern struct PINT_state_machine_s pvfs2_client_get_eattr_sm;
extern struct PINT_state_machine_s pvfs2_client_set_eattr_sm;
extern struct PINT_state_machine_s pvfs2_client_del_eattr_sm;
extern struct PINT_state_machine_s pvfs2_client_list_eattr_sm;

/* nested state machines (helpers) */
extern struct PINT_state_machine_s pvfs2_client_lookup_ncache_sm;
extern struct PINT_state_machine_s pvfs2_client_remove_helper_sm;

#include "state-machine.h"

#endif /* __SM_CHECK_DEP */
#endif /* __PVFS2_CLIENT_STATE_MACHINE_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
