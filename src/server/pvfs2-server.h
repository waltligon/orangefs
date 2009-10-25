/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 *  Declarations for use in the PVFS2 server.
 */

#ifndef __PVFS2_SERVER_H
#define __PVFS2_SERVER_H

/* NOTE: STATE-MACHINE.H IS INCLUDED AT THE BOTTOM!  THIS IS SO WE CAN
 * DEFINE ALL THE STRUCTURES WE NEED BEFORE WE INCLUDE IT.
 */

#include <stdint.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>
#include "pvfs2-debug.h"
#include "pvfs2-storage.h"
#include "pvfs2-internal.h"
#include "job.h"
#include "bmi.h"
#include "trove.h"
#include "gossip.h"
#include "PINT-reqproto-encode.h"
#include "msgpairarray.h"
#include "pvfs2-req-proto.h"
#include "state-machine.h"
#include "pint-event.h"
#include "pint-segpool.h"

extern job_context_id server_job_context;

#define PVFS2_SERVER_DEFAULT_TIMEOUT_MS      100
#define BMI_UNEXPECTED_OP                    999

/* BMI operation timeout if not specified in config file */
#define PVFS2_SERVER_JOB_BMI_TIMEOUT_DEFAULT         30
/* Flow operation timeout if not specified in config file */
#define PVFS2_SERVER_JOB_FLOW_TIMEOUT_DEFAULT        30
/* BMI client side operation timeout if not specified in config file */
/* NOTE: the default for this timeout is set higher to allow the client to
 * overcome syncing and queueing delays on the server
 */
#define PVFS2_CLIENT_JOB_BMI_TIMEOUT_DEFAULT         300
/* Flow client side operation timeout if not specified in config file */
#define PVFS2_CLIENT_JOB_FLOW_TIMEOUT_DEFAULT        300
/* maximum number of times for client to retry restartable operations;
 * use INT_MAX to approximate infinity (187 years with 2 sec delay)
 */
#define PVFS2_CLIENT_RETRY_LIMIT_DEFAULT     (5)
/* number of milliseconds that clients will delay between retries */
#define PVFS2_CLIENT_RETRY_DELAY_MS_DEFAULT  2000

/* Specifies the number of handles to be preceated at a time from each
 * server using the batch create request.
 */
#define PVFS2_PRECREATE_BATCH_SIZE_DEFAULT 512
/* precreate pools will be topped off if they fall below this value */
#define PVFS2_PRECREATE_LOW_THRESHOLD_DEFAULT 256

/* types of permission checking that a server may need to perform for
 * incoming requests
 */
enum PINT_server_req_permissions
{
    PINT_SERVER_CHECK_INVALID = 0, /* invalid request */
    PINT_SERVER_CHECK_WRITE = 1,   /* needs write permission */
    PINT_SERVER_CHECK_READ = 2,    /* needs read permission */
    PINT_SERVER_CHECK_NONE = 3,    /* needs no permission */
    PINT_SERVER_CHECK_ATTR = 4,    /* special case for attribute operations; 
                                      needs ownership */
    PINT_SERVER_CHECK_CRDIRENT = 5 /* special case for crdirent operations;
                                      needs write and execute */
};

#define PINT_GET_OBJECT_REF_DEFINE(req_name)                             \
static inline int PINT_get_object_ref_##req_name(                        \
    struct PVFS_server_req *req, PVFS_fs_id *fs_id, PVFS_handle *handle) \
{                                                                        \
    *fs_id = req->u.req_name.fs_id;                                      \
    *handle = req->u.req_name.handle;                                    \
    return 0;                                                            \
}

enum PINT_server_req_access_type PINT_server_req_readonly(
                                    struct PVFS_server_req *req);
enum PINT_server_req_access_type PINT_server_req_modify(
                                    struct PVFS_server_req *req);

struct PINT_server_req_params
{
    const char* string_name;

    /* For each request that specifies an object ref (fsid,handle) we
     * get the common attributes on that object and check the permissions.
     * For the request to proceed the permissions required by this flag
     * must be met.
     */
    enum PINT_server_req_permissions perm;

    /* Specifies the type of access on the object (readonly, modify).  This
     * is used by the request scheduler to determine 
     * which requests to queue (block), and which to schedule (proceed).
     * This is a callback implemented by the request.  For example, sometimes
     * the io request writes, sometimes it reads.
     * Default functions PINT_server_req_readonly and PINT_server_req_modify
     * are used for requests that always require the same access type.
     */
    enum PINT_server_req_access_type (*access_type)(
                                        struct PVFS_server_req *req);

    /* Specifies the scheduling policy for the request.  In some cases,
     * we can bypass the request scheduler and proceed directly with the
     * request.
     */
    enum PINT_server_sched_policy sched_policy;

    /* A callback implemented by the request to return the object reference
     * from the server request structure.
     */
    int (*get_object_ref)(
        struct PVFS_server_req *req, PVFS_fs_id *fs_id, PVFS_handle *handle);

    /* The state machine that performs the request */
    struct PINT_state_machine_s *state_machine;
};

struct PINT_server_req_entry
{
    enum PVFS_server_op op_type;
    struct PINT_server_req_params *params;
};

extern struct PINT_server_req_entry PINT_server_req_table[];

int PINT_server_req_get_object_ref(
    struct PVFS_server_req *req, PVFS_fs_id *fs_id, PVFS_handle *handle);

enum PINT_server_req_permissions
PINT_server_req_get_perms(struct PVFS_server_req *req);
enum PINT_server_req_access_type
PINT_server_req_get_access_type(struct PVFS_server_req *req);
enum PINT_server_sched_policy
PINT_server_req_get_sched_policy(struct PVFS_server_req *req);

const char* PINT_map_server_op_to_string(enum PVFS_server_op op);

/* used to keep a random, but handy, list of keys around */
typedef struct PINT_server_trove_keys
{
    char *key;
    int size;
} PINT_server_trove_keys_s;

extern PINT_server_trove_keys_s Trove_Common_Keys[];
/* Reserved keys */
enum 
{
    ROOT_HANDLE_KEY      = 0,
    DIR_ENT_KEY          = 1,
    METAFILE_HANDLES_KEY = 2,
    METAFILE_DIST_KEY    = 3,
    SYMLINK_TARGET_KEY   = 4,
    METAFILE_LAYOUT_KEY  = 5,
    NUM_DFILES_REQ_KEY   = 6
};

/* optional; user-settable keys */
enum 
{
    DIST_NAME_KEY        = 0,
    DIST_PARAMS_KEY      = 1,
    NUM_DFILES_KEY       = 2,
    NUM_SPECIAL_KEYS     = 3, /* not an index */
    METAFILE_HINT_KEY    = 3,
};

typedef enum
{
    SERVER_DEFAULT_INIT        = 0,
    SERVER_GOSSIP_INIT         = (1 << 0),
    SERVER_CONFIG_INIT         = (1 << 1),
    SERVER_ENCODER_INIT        = (1 << 2),
    SERVER_BMI_INIT            = (1 << 3),
    SERVER_TROVE_INIT          = (1 << 4),
    SERVER_FLOW_INIT           = (1 << 5),
    SERVER_JOB_INIT            = (1 << 6),
    SERVER_JOB_CTX_INIT        = (1 << 7),
    SERVER_REQ_SCHED_INIT      = (1 << 8),
    SERVER_STATE_MACHINE_INIT  = (1 << 9),
    SERVER_BMI_UNEXP_POST_INIT = (1 << 10),
    SERVER_SIGNAL_HANDLER_INIT = (1 << 11),
    SERVER_JOB_OBJS_ALLOCATED  = (1 << 12),
    SERVER_PERF_COUNTER_INIT   = (1 << 13),
    SERVER_EVENT_INIT          = (1 << 14),
    SERVER_JOB_TIME_MGR_INIT   = (1 << 15),
    SERVER_DIST_INIT           = (1 << 16),
    SERVER_CACHED_CONFIG_INIT  = (1 << 17),
    SERVER_PRECREATE_INIT  = (1 << 18),
} PINT_server_status_flag;

struct PINT_server_create_op
{
    const char **io_servers;
    const char **remote_io_servers;
    int num_io_servers;
    PVFS_handle* handle_array_local; 
    PVFS_handle* handle_array_remote; 
    int handle_array_local_count;
    int handle_array_remote_count;
    PVFS_error saved_error_code;
    int handle_index;
};

/* struct PINT_server_lookup_op
 *
 * All the data needed during lookup processing:
 *
 */
struct PINT_server_lookup_op
{
    /* current segment (0..N), number of segments in the path */
    int seg_ct, seg_nr; 

    /* number of attrs read succesfully */
    int attr_ct;

    /* number of handles read successfully */
    int handle_ct;

    char *segp;
    void *segstate;

    PVFS_handle dirent_handle;
    PVFS_ds_attributes *ds_attr_array;
};

struct PINT_server_readdir_op
{
    uint64_t directory_version;
    PVFS_handle dirent_handle;  /* holds handle of dirdata dspace from
                                   which entries are read */
    PVFS_size dirdata_size;
};

struct PINT_server_crdirent_op
{
    char *name;
    PVFS_handle new_handle;
    PVFS_handle parent_handle;
    PVFS_fs_id fs_id;
    PVFS_handle dirent_handle;  /* holds handle of dirdata dspace that
                                 * we'll write the dirent into */
    PVFS_size dirent_count;
    int dir_attr_update_required;
};

struct PINT_server_rmdirent_op
{
    PVFS_handle dirdata_handle;
    PVFS_handle entry_handle; /* holds handle of dirdata object,
                               * removed entry */
    PVFS_size dirent_count;
    int dir_attr_update_required;
};

struct PINT_server_chdirent_op
{
    PVFS_handle dirdata_handle;
    PVFS_handle old_dirent_handle;
    PVFS_handle new_dirent_handle;
    int dir_attr_update_required;
};

struct PINT_server_remove_op
{
    PVFS_handle handle;
    PVFS_fs_id fs_id;
    PVFS_handle dirdata_handle;   /* holds dirdata dspace handle in
                                   * the event that we are removing a
                                   * directory */
    PVFS_size dirent_count;
    PVFS_ds_keyval key;
    PVFS_ds_position pos;
    int key_count;
    int index;
    int remove_keyvals_state;
};

struct PINT_server_mgmt_remove_dirent_op
{
    PVFS_handle dirdata_handle;
};

struct PINT_server_precreate_pool_refiller_op
{
    PVFS_handle pool_handle;
    PVFS_handle* precreate_handle_array;
    PVFS_fs_id fsid;
    char* host;
    PVFS_BMI_addr_t host_addr;
    PVFS_handle_extent_array data_handle_extent_array;
};

struct PINT_server_batch_create_op
{
    int saved_error_code;
    int batch_index;
};

struct PINT_server_batch_remove_op
{
    int handle_index;
    int error_code;
};

struct PINT_server_mgmt_get_dirdata_op
{
    PVFS_handle dirdata_handle;
};

struct PINT_server_getconfig_op
{
    int strsize; /* used to hold string lengths during getconfig
                  * processing */
};

struct PINT_server_io_op
{
    gen_mutex_t mutex;
    PINT_segpool_handle_t seg_handle;
    PINT_Request *file_req;
    PVFS_offset file_req_offset;
    PINT_Request *mem_req;
    
    void *user_ptr;

    void *tmp_buffer;
    PVFS_size count; /* for MEAN operation */

    PVFS_size aggregate_size;

    PINT_request_file_data file_data;
  
    int buffer_size;
    int num_of_buffers;
    
    PVFS_size total_transferred;

    int parallel_sms;
};

/* substibute for flow */
struct PINT_server_pipeline_op
{
    PVFS_fs_id fs_id;
    PVFS_handle handle;
    PVFS_BMI_addr_t address;

    PVFS_handle *dfile_array;
    int dfile_index; /* can be used for Rank */
    int dfile_count;
    struct PINT_dist_s *dist;
    PINT_request_file_data file_data;

    PINT_Request *file_req;
    PVFS_offset file_req_offset;
    PINT_Request *mem_req;

    /* for strip alignment */
    char *tmp_buf; /* FIXME */
    PVFS_size unaligned_sz; /* cunit-right_adj_sz */
    PVFS_size left_adj_sz;
    PVFS_size right_adj_sz;

    enum PVFS_io_type io_type;

    /* AS: operator and data type */
    int op;
    int datatype;
    int use_gpu; /* FIXME: kmeans.sm also has this */
    /* This is for grep operator 
       FIXME: find more appropriate place */
    int rsize;
    char pattern[128];

    char *buffer; 
    PVFS_size buffer_size;
    PVFS_size buffer_used;
    PVFS_size new_buffer_used;
    PVFS_size out_size;
    PINT_segpool_handle_t seg_handle;
    PINT_segpool_unit_id id;
    PVFS_offset *offsets;
    PVFS_size *sizes;
    int segs;
    PVFS_hint hints;
    PVFS_msg_tag_t tag;
    int trove_sync_flag;
    PVFS_offset loff;
    int parallel_sms;
};
 
/* allreduce */
struct PINT_server_allreduce_op
{
    int type; /* SEND (0) or RECV (1) */ /* FIXME: not used anymore? */
    int op; /* {SUM, MAX, MIN} = {0, 1, 2} */
    int datatype; /* MPI_INT, MPI_FLOAT, MPI_DOUBLE */
    PVFS_fs_id fs_id;
    PVFS_hint hints;
    PVFS_handle *dfile_array;
    int myRank;
    int tree_depth;
    int current_depth;
    void *send_buf;
    void *recv_buf;
    PVFS_size buf_sz;
    int mask;
};

/* send_recv */
struct PINT_server_send_recv_op
{
    int type;
    int myRank;
    int mask;
};

/* bcast */
struct PINT_server_bcast_op
{
    int type; /* SEND (0) or RECV (1) */
    int datatype; /* MPI_INT, MPI_FLOAT, MPI_DOUBLE */
    PVFS_fs_id fs_id;
    PVFS_hint hints;
    PVFS_handle *dfile_array;
    int dfile_count;
    int myRank;
    int tree_depth;
    int index;
    void *send_buf;
    void *recv_buf;
    PVFS_size buf_sz;
    int mask;
};


struct PINT_server_kmeans_op
{
    int myRank;
    PVFS_handle *dfile_array;
    int dfile_count;
    PVFS_fs_id fs_id;

    int use_gpu;

    int loop;
    int numClusters;
    int numCoords;
    int numObjs; /* local number of objs */
    int totalNumObjs;
    int *newClusterSize;
    int *clusterSize;
    float delta;
    float delta_tmp;
    float **newClusters;
    int *membership; /* [numObjs] */
    float **objects; /* [numObjs][numCoords] data objects */
    float **clusters; /* [numClusters][numCoords] cluster center */
    float threshold;
    int allreduce_step;
};

struct PINT_server_small_io_op
{
    PVFS_offset offsets[IO_MAX_REGIONS];
    PVFS_size sizes[IO_MAX_REGIONS];
    PVFS_size result_bytes;
};

struct PINT_server_flush_op
{
    PVFS_handle handle;	    /* handle of data we want to flush to disk */
    int flags;		    /* any special flags for flush */
};

struct PINT_server_truncate_op
{
    PVFS_handle handle;	    /* handle of datafile we resize */
    PVFS_offset size;	    /* new size of datafile */
};

struct PINT_server_mkdir_op
{
    PVFS_fs_id fs_id;
    PVFS_handle_extent_array handle_extent_array;
    PVFS_handle dirent_handle;
    PVFS_size init_dirdata_size;
};

struct PINT_server_getattr_op
{
    PVFS_handle handle;
    PVFS_fs_id fs_id;
    PVFS_ds_attributes dirdata_ds_attr;
    uint32_t attrmask;
    PVFS_error* err_array;
    PVFS_ds_keyval_handle_info keyval_handle_info;
    PVFS_handle dirent_handle;
    int num_dfiles_req;
};

struct PINT_server_listattr_op
{
    PVFS_object_attr *attr_a;
    PVFS_ds_attributes *ds_attr_a;
    PVFS_error *errors;
    int parallel_sms;
};

/* this is used in both set_eattr, get_eattr and list_eattr */
struct PINT_server_eattr_op
{
    void *buffer;
};

struct PINT_server_unstuff_op
{
    PVFS_handle* dfile_array;
    int num_dfiles_req;
    PVFS_sys_layout layout;
    void* encoded_layout;
};

/* This structure is passed into the void *ptr 
 * within the job interface.  Used to tell us where
 * to go next in our state machine.
 */
typedef struct PINT_server_op
{
    struct qlist_head   next; /* used to queue structures used for unexp style messages */
    int op_cancelled; /* indicates unexp message was cancelled */
    job_id_t unexp_id;

    enum PVFS_server_op op;  /* type of operation that we are servicing */

    PINT_event_id event_id;

    /* holds id from request scheduler so we can release it later */
    job_id_t scheduled_id; 

    /* generic structures used in most server operations */
    PVFS_ds_keyval key, val; 
    PVFS_ds_keyval *key_a;
    PVFS_ds_keyval *val_a;
    int *error_a;
    int keyval_count;

    int free_val;

    /* attributes structure associated with target of operation; may be 
     * partially filled in by prelude nested state machine (for 
     * permission checking); may be used/modified by later states as well
     *
     * the ds_attr is used by the prelude sm only (and for pulling the
     * size out in the get-attr server sm); don't use it otherwise --
     * the object_attr is prepared for other sm's, so use it instead.
     */
    PVFS_ds_attributes ds_attr;
    PVFS_object_attr attr;

    PVFS_BMI_addr_t addr;   /* address of client that contacted us */
    bmi_msg_tag_t tag; /* operation tag */
    /* information about unexpected message that initiated this operation */
    struct BMI_unexpected_info unexp_bmi_buff;

    /* decoded request and response structures */
    struct PVFS_server_req *req; 
    struct PVFS_server_resp resp; 
    /* encoded request and response structures */
    struct PINT_encoded_msg encoded;
    struct PINT_decoded_msg decoded;

    PINT_sm_msgarray_op msgarray_op;

    PVFS_handle target_handle;
    PVFS_fs_id target_fs_id;
    PVFS_object_attr *target_object_attr;

    enum PINT_server_req_access_type access_type;
    enum PINT_server_sched_policy sched_policy;

    union
    {
	/* request-specific scratch spaces for use during processing */
        struct PINT_server_create_op create;
        struct PINT_server_eattr_op eattr;
        struct PINT_server_getattr_op getattr;
        struct PINT_server_listattr_op listattr;
	struct PINT_server_getconfig_op getconfig;
	struct PINT_server_lookup_op lookup;
	struct PINT_server_crdirent_op crdirent;
	struct PINT_server_readdir_op readdir;
	struct PINT_server_remove_op remove;
	struct PINT_server_chdirent_op chdirent;
	struct PINT_server_rmdirent_op rmdirent;
	struct PINT_server_io_op io;
        struct PINT_server_small_io_op small_io;
	struct PINT_server_pipeline_op pipeline;
	struct PINT_server_allreduce_op allreduce;
	struct PINT_server_send_recv_op send_recv;
	struct PINT_server_bcast_op bcast;
	struct PINT_server_kmeans_op kmeans;
	struct PINT_server_flush_op flush;
	struct PINT_server_truncate_op truncate;
	struct PINT_server_mkdir_op mkdir;
        struct PINT_server_mgmt_remove_dirent_op mgmt_remove_dirent;
        struct PINT_server_mgmt_get_dirdata_op mgmt_get_dirdata_handle;
        struct PINT_server_precreate_pool_refiller_op
            precreate_pool_refiller;
        struct PINT_server_batch_create_op batch_create;
        struct PINT_server_batch_remove_op batch_remove;
        struct PINT_server_unstuff_op unstuff;
    } u;

} PINT_server_op;

/* PINT_ACCESS_DEBUG()
 *
 * macro for consistent printing of access records
 *
 * no return value
 */
#ifdef GOSSIP_DISABLE_DEBUG
#define PINT_ACCESS_DEBUG(__s_op, __mask, format, f...) do {} while (0)
#else
#define PINT_ACCESS_DEBUG(__s_op, __mask, format, f...)                     \
    PINT_server_access_debug(__s_op, __mask, format, ##f)
#endif

void PINT_server_access_debug(PINT_server_op * s_op,
                              int64_t debug_mask,
                              const char * format,
                              ...) __attribute__((format(printf, 3, 4)));

/* nested state machines */
extern struct PINT_state_machine_s pvfs2_get_attr_work_sm;
extern struct PINT_state_machine_s pvfs2_prelude_sm;
extern struct PINT_state_machine_s pvfs2_prelude_work_sm;
extern struct PINT_state_machine_s pvfs2_final_response_sm;
extern struct PINT_state_machine_s pvfs2_check_entry_not_exist_sm;
extern struct PINT_state_machine_s pvfs2_remove_work_sm;
extern struct PINT_state_machine_s pvfs2_mkdir_work_sm;
extern struct PINT_state_machine_s pvfs2_unexpected_sm;
extern struct PINT_state_machine_s pvfs2_pipeline_sm; /* sson */
extern struct PINT_state_machine_s pvfs2_allreduce_sm; /* sson */
extern struct PINT_state_machine_s pvfs2_bcast_sm; /* sson */
extern struct PINT_state_machine_s pvfs2_kmeans_sm; /* sson */
extern struct PINT_state_machine_s pvfs2_send_recv_sm; /* sson */

/* Exported Prototypes */
struct server_configuration_s *get_server_config_struct(void);

/* exported state machine resource reclamation function */
int server_post_unexpected_recv(job_status_s *js_p);
int server_state_machine_start( PINT_smcb *smcb, job_status_s *js_p);
int server_state_machine_complete(PINT_smcb *smcb);
int server_state_machine_terminate(PINT_smcb *smcb, job_status_s *js_p);

/* lists of server ops */
extern struct qlist_head posted_sop_list;
extern struct qlist_head inprogress_sop_list;

/* starts state machines not associated with an incoming request */
int server_state_machine_alloc_noreq(
    enum PVFS_server_op op, struct PINT_smcb ** new_op);
int server_state_machine_start_noreq(
    struct PINT_smcb *new_op);

/* INCLUDE STATE-MACHINE.H DOWN HERE */
#if 0
#define PINT_OP_STATE       PINT_server_op
#define PINT_OP_STATE_GET_MACHINE(_op) \
    ((_op >= 0 && _op < PVFS_SERV_NUM_OPS) ? \
    PINT_server_req_table[_op].params->sm : NULL)
#endif

#include "pvfs2-internal.h"

struct PINT_state_machine_s *server_op_state_get_machine(int);

#endif /* __PVFS_SERVER_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
