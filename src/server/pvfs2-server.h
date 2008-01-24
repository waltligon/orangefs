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

/* skip everything except #includes if __SM_CHECK_DEP is already
 * defined; this allows us to get the dependencies right for
 * msgpairarray.sm which relies on conflicting headers for dependency
 * information
 */
#ifndef __SM_CHECK_DEP
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

/* indicates if the attributes for the target object must exist for
 * the operation to proceed (see prelude.sm)
 */
enum PINT_server_req_attrib_flags
{
    PINT_SERVER_ATTRIBS_INVALID = 0,
    PINT_SERVER_ATTRIBS_REQUIRED = 1,
    /* operations that operate on datafiles or on incomplete metafiles
     * do not expect to necessarily find attributes present before
     * starting the operation
     */
    PINT_SERVER_ATTRIBS_NOT_REQUIRED = 2
};

struct PINT_server_req_params
{
    enum PVFS_server_op op_type;
    const char* string_name;
    enum PINT_server_req_permissions perm;
    enum PINT_server_req_attrib_flags attrib_flags;
    struct PINT_state_machine_s* sm;
};

extern struct PINT_server_req_params PINT_server_req_table[];

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
} PINT_server_status_flag;

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
    flow_descriptor* flow_d;
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
};

struct PINT_server_listattr_op
{
    uint32_t nhandles;
    PVFS_handle *handles;
    PVFS_size dirent_count;
    PVFS_fs_id fs_id;
    PVFS_object_attr *attr_a;
    PVFS_ds_attributes *ds_attr_a;
    PVFS_error *errors;
    uint32_t attrmask;
    uint32_t handle_index;
    PVFS_ds_keyval_handle_info keyval_handle_info;
};

/* this is used in both set_eattr, get_eattr and list_eattr */
struct PINT_server_eattr_op
{
    void *buffer;
};
    
/* This structure is passed into the void *ptr 
 * within the job interface.  Used to tell us where
 * to go next in our state machine.
 */
typedef struct PINT_server_op
{
    struct qlist_head   next; /* used to queue structures used for unexp style messages */
    int op_cancelled; /* indicates unexp message was cancelled */
    enum PVFS_server_op op;  /* type of operation that we are servicing */

    /* holds id from request scheduler so we can release it later */
    job_id_t scheduled_id; 

    /* generic structures used in most server operations */
    PVFS_ds_keyval key, val; 
    PVFS_ds_keyval *key_a;
    PVFS_ds_keyval *val_a;
    int *error_a;

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

    /* generic msgpair used with msgpair substate */
    PINT_sm_msgpair_state msgpair;

    /* state information for msgpairarray nested state machine */
    int msgarray_count;
    PINT_sm_msgpair_state *msgarray;
    PINT_sm_msgpair_params msgarray_params;

    PVFS_handle target_handle;
    PVFS_fs_id target_fs_id;
    PVFS_object_attr *target_object_attr;

    union
    {
	/* request-specific scratch spaces for use during processing */
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
	struct PINT_server_flush_op flush;
	struct PINT_server_truncate_op truncate;
	struct PINT_server_mkdir_op mkdir;
        struct PINT_server_mgmt_remove_dirent_op mgmt_remove_dirent;
        struct PINT_server_mgmt_get_dirdata_op mgmt_get_dirdata_handle;
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
/* server operation state machines */
extern struct PINT_state_machine_s pvfs2_get_config_sm;
extern struct PINT_state_machine_s pvfs2_get_attr_sm;
extern struct PINT_state_machine_s pvfs2_list_attr_sm;
extern struct PINT_state_machine_s pvfs2_set_attr_sm;
extern struct PINT_state_machine_s pvfs2_create_sm;
extern struct PINT_state_machine_s pvfs2_batch_create_sm;
extern struct PINT_state_machine_s pvfs2_crdirent_sm;
extern struct PINT_state_machine_s pvfs2_mkdir_sm;
extern struct PINT_state_machine_s pvfs2_readdir_sm;
extern struct PINT_state_machine_s pvfs2_lookup_sm;
extern struct PINT_state_machine_s pvfs2_io_sm;
extern struct PINT_state_machine_s pvfs2_small_io_sm;
extern struct PINT_state_machine_s pvfs2_remove_sm;
extern struct PINT_state_machine_s pvfs2_mgmt_remove_object_sm;
extern struct PINT_state_machine_s pvfs2_mgmt_remove_dirent_sm;
extern struct PINT_state_machine_s pvfs2_mgmt_get_dirdata_handle_sm;
extern struct PINT_state_machine_s pvfs2_rmdirent_sm;
extern struct PINT_state_machine_s pvfs2_chdirent_sm;
extern struct PINT_state_machine_s pvfs2_flush_sm;
extern struct PINT_state_machine_s pvfs2_truncate_sm;
extern struct PINT_state_machine_s pvfs2_setparam_sm;
extern struct PINT_state_machine_s pvfs2_noop_sm;
extern struct PINT_state_machine_s pvfs2_unexpected_sm;
extern struct PINT_state_machine_s pvfs2_statfs_sm;
extern struct PINT_state_machine_s pvfs2_perf_update_sm;
extern struct PINT_state_machine_s pvfs2_job_timer_sm;
extern struct PINT_state_machine_s pvfs2_proto_error_sm;
extern struct PINT_state_machine_s pvfs2_perf_mon_sm;
extern struct PINT_state_machine_s pvfs2_event_mon_sm;
extern struct PINT_state_machine_s pvfs2_iterate_handles_sm;
extern struct PINT_state_machine_s pvfs2_get_eattr_sm;
extern struct PINT_state_machine_s pvfs2_get_eattr_list_sm;
extern struct PINT_state_machine_s pvfs2_set_eattr_sm;
extern struct PINT_state_machine_s pvfs2_set_eattr_list_sm;
extern struct PINT_state_machine_s pvfs2_del_eattr_sm;
extern struct PINT_state_machine_s pvfs2_list_eattr_sm;
extern struct PINT_state_machine_s pvfs2_list_eattr_list_sm;
extern struct PINT_state_machine_s pvfs2_stuffed_create_sm;

/* nested state machines */
extern struct PINT_state_machine_s pvfs2_get_attr_work_sm;
extern struct PINT_state_machine_s pvfs2_prelude_sm;
extern struct PINT_state_machine_s pvfs2_final_response_sm;
extern struct PINT_state_machine_s pvfs2_check_entry_not_exist_sm;
extern struct PINT_state_machine_s pvfs2_remove_work_sm;
extern struct PINT_state_machine_s pvfs2_mkdir_work_sm;

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
    PINT_server_req_table[_op].sm : NULL)
#endif

#include "pvfs2-internal.h"

struct PINT_state_machine_s *server_op_state_get_machine(int);

#endif /* __SM_CHECK_DEP */ 
#endif /* __PVFS_SERVER_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
