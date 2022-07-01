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
#ifndef WIN32
#include <pwd.h>
#include <grp.h>
#endif
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
#include "server-config.h"
#include "pint-perf-counter.h"
#include "server-config-mgr.h"

extern job_context_id server_job_context;

#define PVFS2_SERVER_DEFAULT_TIMEOUT_MS      1000
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
 
/* used to keep a random, but handy, list of keys around */
typedef struct PINT_server_trove_keys
{
    char *key;
    int size;
} PINT_server_trove_keys_s;

/* wrapper object that keeps track of all of the servers where a given OID is stored */
typedef struct PINT_handle_SID_store_object
{
    int32_t count; /* This number should match the associated number of copies set for this OID */
    PVFS_handle handle;
    PVFS_SID *sids;
} PINT_handle_SID_s;

/* This is defined in src/server/pvfs2-server.c
 * These values index this table
 */
extern PINT_server_trove_keys_s Trove_Common_Keys[];
/* Reserved keys */
enum 
{
    ROOT_HANDLE_KEY          = 0,
    DIR_ENT_KEY              = 1,
    METAFILE_HANDLES_KEY     = 2,
    METAFILE_DIST_KEY        = 3,
    SYMLINK_TARGET_KEY       = 4,
    METAFILE_LAYOUT_KEY      = 5,
    NUM_DFILES_REQ_KEY       = 6, /* only used for unstuff - will remove */
    DIST_DIR_ATTR_KEY        = 7, /* obsolete in V3 */
    DIST_DIRDATA_BITMAP_KEY  = 8,
    DIST_DIRDATA_HANDLES_KEY = 9,
    OBJECT_PARENT_KEY        = 10,
    POSIX_ACL_KEY            = 11
};

/* This is defined in src/common/misc/pvfs2-internal.h
 * These values index this table
 * The first NUM_SPECIAL_KEYS of these are automatically
 * Read when getting metadata
 *
 * WBL V3 Uncomment this declaration if it doesn't cause problems
 */
extern PINT_server_trove_keys_s Trove_Special_Keys[];
/* optional; user-settable keys */
enum 
{
    DIST_NAME_KEY          = 0, /* string, name of a distribution */
    DIST_PARAMS_KEY        = 1, /* struct, parameters to a distribution */
    SERVER_LIST_KEY        = 2, /* string, list of servers part of distribution */
    DIR_SERVER_LIST_KEY    = 3, /* string, list of servers part of distribution */
    NUM_SPECIAL_KEYS       = 4, /* not an index */
    METAFILE_HINT_KEY      = 4,
    MIRROR_COPIES_KEY      = 5,
    MIRROR_HANDLES_KEY     = 6,
    MIRROR_STATUS_KEY      = 7,
};

/* These keys went away in V3 */
#if 0
    NUM_DFILES_KEY         = 2, /* these in get-attr and mkdir */
    DEFAULT_NUM_DFILES_KEY = 2, /* alias ? This is supposed to be in config */
    LAYOUT_KEY             = 3,
};
#endif

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
    SERVER_PRECREATE_INIT      = (1 << 18),
    SERVER_UID_MGMT_INIT       = (1 << 19), 
    SERVER_SECURITY_INIT       = (1 << 20),
    SERVER_SID_INIT            = (1 << 21),
    SERVER_FILESYS_INIT        = (1 << 22),
    SERVER_BMI_CLIENT_INIT     = (1 << 23),
    SERVER_CAPCACHE_INIT       = (1 << 24),
    SERVER_CREDCACHE_INIT      = (1 << 25),
    SERVER_CERTCACHE_INIT      = (1 << 26),
} PINT_server_status_flag;

/* this combination of flags is used to control pre-init of the server
 * when reading a remove config file
 */
#define SERVER_CLIENT_INIT \
            SERVER_BMI_CLIENT_INIT | SERVER_TROVE_INIT | SERVER_FILESYS_INIT |\
            SERVER_FLOW_INIT | SERVER_CACHED_CONFIG_INIT

typedef enum
{
    PRELUDE_PERM_CHECK_DONE    = (1<<0),
    PRELUDE_NO_SCHEDULE        = (1<<1),
} PINT_prelude_flag;

struct PINT_server_create_op
{
/* V3 */
#if 0
    const char **io_servers;
    const char **remote_io_servers;
    int num_io_servers;
    PVFS_handle* handle_array_remote; 
    int handle_array_remote_count;
#endif
    PVFS_handle* handle_array_local; 
    int handle_array_local_count;
    PVFS_error saved_error_code;
    int handle_index;
};

/*MIRROR structures*/
typedef struct 
{
   /* session identifier created in the PVFS_SERV_IO request.  also used as  */
   /* the flow identifier.                                                   */
   bmi_msg_tag_t session_tag;

   /*destination server address*/
   PVFS_BMI_addr_t svr_addr;

   /*status from PVFS_SERV_IO*/
   PVFS_error io_status;

   /*variables used to setup write completion ack*/
   void        *encoded_resp_p;
   job_status_s recv_status;
   job_id_t     recv_id;

   /*variables used to setup flow between the src & dest datahandle*/
   flow_descriptor *flow_desc;
   job_status_s     flow_status;
   job_id_t         flow_job_id;
  
} write_job_t;


/*This structure is used during the processing of a "mirror" request.*/
struct PINT_server_mirror_op
{
   /*keep up with the number of outstanding jobs*/
   int job_count;

   /*maximum response size for the write request*/
   int max_resp_sz;

   /*info about each job*/
   write_job_t *jobs;
};
typedef struct PINT_server_mirror_op PINT_server_mirror_op;

/* Source refers to the handle being copied, and destination refers to        */
/* its copy.                                                                  */
struct PINT_server_create_copies_op
{
    /*number of I/O servers required to meet the mirroring request.           */
    uint32_t io_servers_required;

    /*mirroring mode. attribute key is user.pvfs2.mirror.mode*/
    uint32_t mirror_mode;

    /*the expected mirroring mode tells us how to edit the retrieved mirroring*/
    /*mode.  Example: if mirroring was called when immutable was set, then    */
    /*the expected mirroring mode would be MIRROR_ON_IMMUTABLE.               */
    uint32_t expected_mirror_mode;

    /*buffer holding list of remote servers for all copies of the file*/
    char **my_remote_servers;

    /*saved error code*/
    PVFS_error saved_error_code;

    /*number of copies desired. value of user.pvfs2.mirror.copies attribute*/
    uint32_t copies;

    /*successful/failed writes array in order of source handles         */
    /*0=>successful  !UINT64_HIGH=>failure   UINT64_HIGH=>initial state */
    /*accessed as if a 2-dimensional array [SrcHandleNR][#ofCopies]     */
    PVFS_handle *writes_completed;

    /*number of attempts at writing handles*/
    int retry_count;

    /*list of server names that will be used as destination servers*/
    char **io_servers;                       

    /*source remote server names in distribution*/
    char **remote_io_servers;

    /*source local server names in distribution*/                
    char **local_io_servers;

    /*number of source server names in the distribution*/                     
    int num_io_servers;

    /*number of source remote server names in distribution*/                  
    int remote_io_servers_count;             

    /*number of source local server names in distribution*/
    int local_io_servers_count;              

    /*source datahandles in order of distribution*/
    PVFS_handle *handle_array_base;

    /*local source datahandles*/
    PVFS_handle *handle_array_base_local;

    /*destination datahandles in order of distribution*/          
    PVFS_handle *handle_array_copies;        

    /*local destination datahandles*/
    PVFS_handle *handle_array_copies_local;  

    /*remote destination datahandles*/
    PVFS_handle *handle_array_copies_remote;

    /*number of local source datahandles*/
    int handle_array_base_local_count; 

    /*number of local destination datahandles*/
    int handle_array_copies_local_count;      

    /*number of remote destination datahandles*/
    int handle_array_copies_remote_count;     

    /*number of source datahandles*/
    uint32_t dfile_count;

    /*source metadata handle*/                     
    PVFS_handle metadata_handle; 

    /*source file system*/
    PVFS_fs_id fs_id; 

    /*number of io servers defined in the current file system*/
    int io_servers_count; 

    /*size of the source distribution structure */
    uint32_t dist_size;

    /*distribution structure for basic_dist*/
    PINT_dist *dist;

    /*local source handles' attribute structure*/
    /*populates bstream_array_base_local with byte stream size*/
    PVFS_ds_attributes *ds_attr_a;

    /*local source handles' byte stream size*/
    /*index corresponds to handle_array_base*/
    PVFS_size *bstream_array_base_local;
};
typedef struct PINT_server_create_copies_op PINT_server_create_copies_op;


/*This macro is used to initialize a PINT_server_op structure when pjmp'ing */
/*to pvfs2_create_immutable_copies_sm.                                      */
#define PVFS_SERVOP_IMM_COPIES_FILL(__new_p,__cur_p)                           \
do {                                                                           \
   memcpy(__new_p,__cur_p,sizeof(struct PINT_server_op));                      \
   (__new_p)->op = PVFS_SERV_IMM_COPIES;                                       \
   memset(&((__new_p)->u.create_copies),0,sizeof((__new_p)->u.create_copies)); \
}while(0)



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

    PVFS_ds_attributes *ds_attr_array;
    PVFS_object_attr attr;

    PVFS_ID *temp_dirent_store;
    int temp_dirent_sid_count;

    int dirdata_server_index;
    int dirdata_sid_index;

    int array_index;
};

struct PINT_server_readdir_op
{
    uint64_t directory_version;
    PVFS_handle dirent_handle;  /* holds handle of dirdata dspace from
                                   which entries are read */
    PVFS_ID *keyval_db_entries;
    PVFS_size dirdata_size;
};

typedef struct
{
    int start_entry;
    int nentries;
} split_msg_boundary;

struct PINT_server_crdirent_op
{
#if 0
    PVFS_credential credential;
#endif
    PVFS_capability capability;
#if 0
    char *name;
    PVFS_handle new_handle;     /* handle of new entry */
    PVFS_SID   *new_sid;        /* sids of new entry */
    PVFS_handle parent_handle;  /* handle of dir (metadata) */
    PVFS_SID   *parent_sid;     /* sids of dir (metadata) */
    PVFS_fs_id  fs_id;
    PVFS_handle dirdata_handle; /* holds handle of dirdata dspace that */
                                /* we'll write the dirent into */
    PVFS_SID   *dirdata_sid;    /* sids of dirdata we write dirent to */
    PVFS_size   dirent_count;   /* number of dirents in target dirdata */
    PVFS_size   sid_count;      /* All should be the metadata const */
#endif
    PVFS_ds_keyval_handle_info keyval_handle_info;
    PVFS_object_attr     dirdata_attr;
    PVFS_object_attr     metahandle_attr;
    PVFS_ds_attributes   dirdata_ds_attr;
    PVFS_ID             *keyval_temp_store;
    PVFS_ds_attributes   metahandle_ds_attr;

    /* index of node to receive directory entries when a split is necessary. */
    int                  split_node;

    /* Save old directory attrs in case we have to back out due to an error. */
    PVFS_object_attr     saved_attr;

    /* variables used for sending mgmt_split_dirent request */
    PVFS_BMI_addr_t      svr_addr;     /* destination server address */
    PVFS_error          *split_status; /* status from PVFS_SERV_MGMT_SPLIT_DIRENT */
    PINT_dist           *dist;         /* distribution structure for basic_dist */
    int                 read_all_directory_entries;
    int                 nentries;
    PVFS_handle        *entry_handles;
    PVFS_SID           *entry_sid;
    char              **entry_names;
    int                 num_msgs_required;
    split_msg_boundary *msg_boundaries;
    PVFS_ds_keyval     *entries_key_a;
    PVFS_ds_keyval     *entries_val_a;
    PVFS_handle        *remote_dirdata_handles;
};

struct PINT_server_setattr_op
{
    PVFS_handle *remote_dirdata_handles;
};

struct PINT_server_rmdirent_op
{
/*    PVFS_handle dirdata_handle; */
    PVFS_handle *entry_handle; /* holds handle of dirent object,
                                * removed entry */
    PVFS_SID *sid_array;       /* holds sids of dirent object */
    PVFS_size dirent_count;
/*    PVFS_object_attr dirdata_attr; */
    PVFS_ds_attributes dirdata_ds_attr; 
};

struct PINT_server_chdirent_op
{
    PVFS_handle dirdata_handle;
    PVFS_handle *old_dirent_handle;      /* buffer for old info from dirent */
    PVFS_SID *old_sid_array;
    int32_t old_sid_count;
    
    int dir_attr_update_required;
    PVFS_object_attr dirdata_attr;
    PVFS_ds_attributes dirdata_ds_attr;
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
    int local_dirdata_index;    /* holds the index into the 
                                   attr.dirdata_handles array for
                                   the local dirdata handle */
    int *remote_dirdata_index;  /* hold indices into the
                                   attr.dirdata_handles array for
                                   remote dirdata handles*/
    int num_remote_dirdata_indices; /* number of remote dirdata handles */

    /* for dirdata rebuild */
    int saved_error_code;
    int need_rebuild_dirdata_local;
    int rebuild_local_dirdata_index;
    int num_rebuild_dirdata_remote;
    int *rebuild_dirdata_index_array_remote;
    PVFS_handle handle_local;
    PVFS_handle* handle_array_remote;
};

struct PINT_server_mgmt_remove_dirent_op
{
    PVFS_handle dirdata_handle;
};

struct PINT_server_mgmt_split_dirent_op
{
    PVFS_ID *keyval_temp_store;
};

/* WBL V3 removing precreate */
#if 0
struct PINT_server_precreate_pool_refiller_op
{
    PVFS_handle pool_handle;
    PVFS_handle* precreate_handle_array;
    PVFS_fs_id fsid;
    char* host;
    PVFS_BMI_addr_t host_addr;
    PVFS_handle_extent_array handle_extent_array;
    PVFS_ds_type type;
    PVFS_capability capability;
};
#endif

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
    PVFS_ID *keyval_temp_array;

    PVFS_SID *sid_array;
    int sid_count;
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
    PVFS_handle handle;        /* handle of data we want to flush to disk */
    int flags;            /* any special flags for flush */
};

struct PINT_server_truncate_op
{
    PVFS_handle handle;        /* handle of datafile we resize */
    PVFS_offset size;        /* new size of datafile */
};

struct PINT_server_mkdir_op
{
    PVFS_fs_id fs_id;
    PVFS_handle handle;        /* metadata handle passed by request */
/*
    PVFS_size init_dirdata_size;
*/
    PVFS_capability server_to_server_capability;
    PVFS_object_attr *saved_attr;

    /* dist-dir-struct
     * not in resp, only return meta handle
     * should be in attr up-level, PINT_server_op*/

    /* inherit from create_op */
    /* not using these right now
    const char **dirdata_servers;
    const char **remote_dirdata_servers;
    int num_dirdata_servers;
    PVFS_handle* handle_array_local;
    PVFS_handle* handle_array_remote;
    int handle_array_local_count;
    int handle_array_remote_count;
    */
    PVFS_error saved_error_code;
    int dirdata_index;      /* index for looping through all dirdata */
    int rmt_dirdata_index;  /* index for looping through remote dirdata */
    int *rmt_dirdata_array; /* array of indicies of the dirdata that are remote */
    PVFS_OID *dirdata_parent_buffer; /* OID and SIDs of the dir (parent to dirdatas) */
};

struct PINT_server_getattr_op
{
    PVFS_handle handle;
    PVFS_fs_id fs_id;
    PVFS_ds_attributes dirdata_ds_attr;
    uint64_t attrmask;
    PVFS_ds_keyval_handle_info keyval_handle_info;
    int num_dfiles_req;
    PVFS_handle *mirror_dfile_status_array;
    PVFS_credential credential;
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

struct PINT_server_tree_communicate_op
{
    int num_pjmp_frames;
    int num_partitions;
    PVFS_handle* handle_array_local; 
    PVFS_handle* handle_array_remote; 
    uint32_t *local_join_size;
    uint32_t *remote_join_size;
    int handle_array_local_count;
    int handle_array_remote_count;
    int handle_index;
    int32_t sid_count; /* V3 FIXME */
    PVFS_SID *sid_array; /* V3 FIXME */
};

struct PINT_server_mgmt_get_dirent_op
{
    PVFS_handle handle;
    PVFS_ID *keyval_temp_store;
};

struct PINT_server_mgmt_create_root_dir_op
{
    PVFS_handle lost_and_found_handle;
    PVFS_capability capability;
    PVFS_credential credential;
    int num_dirdata_servers;
    PVFS_handle* handle_array_local;
    PVFS_handle* handle_array_remote;
    int handle_array_local_count;
    int handle_array_remote_count;
    PVFS_error saved_error_code;
    int handle_index;
};

struct PINT_server_perf_update_op
{
    struct PINT_perf_counter *pc;
    struct PINT_perf_counter *tpc;
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

    /* variables used for monitoring and timing requests */
    PINT_event_id event_id;
    struct timespec start_time;     /* start time of a timer in ns */

    /* holds id from request scheduler so we can release it later */
    job_id_t scheduled_id; 

    /* generic structures used in most server operations */
    PVFS_ds_keyval key, val; 
    PVFS_ds_keyval *key_a;
    PVFS_ds_keyval *val_a;
    int *error_a;
    int keyval_count;

    int free_val;

    /* generic int for use by state machines that are accessing
     * PINT_server_op structs before pjumping to them. */
    uint32_t local_index;

    /* attributes structure associated with target of operation; may be 
     * partially filled in by prelude nested state machine (for 
     * permission checking); may be used/modified by later states as well
     *
     * the ds_attr is used by the prelude sm only (and for pulling the
     * size out in the get-attr server sm); don't use it otherwise --
     * the object_attr is prepared for other sm's, so use it instead.
     */
    PVFS_ds_attributes         ds_attr;
    PVFS_object_attr           attr;
    PVFS_object_attrmask       orig_mask; /* temp mask holder */

    PVFS_BMI_addr_t            addr;   /* address of client that contacted us */
    bmi_msg_tag_t              tag;      /* operation tag */
    /* information about unexpected message that initiated this operation */
    struct BMI_unexpected_info unexp_bmi_buff;

    /* decoded request and response structures */
    struct PVFS_server_req    *req; 
    struct PVFS_server_resp    resp; 

    /* encoded request and response structures */
    struct PINT_encoded_msg    encoded;
    struct PINT_decoded_msg    decoded;

    PINT_sm_msgarray_op        msgarray_op;

    /* used by prelude when creating a "missing" DIRDATA or DATA object */
    int32_t                    new_target_object;  /* flag for host stsate machine */
    PVFS_handle                target_handle;
    PVFS_fs_id                 target_fs_id;
    PVFS_object_attr          *target_object_attr;

    PINT_prelude_flag          prelude_mask;

    enum PINT_server_req_access_type access_type;
    enum PINT_server_sched_policy sched_policy;
    
    /* used in a pjmp to remember how many frames we pushed but be
     * careful about nesting
     */
    int                        num_pjmp_frames;
    struct PINT_mpa_join      *join;

    /* Used just about everywhere so this is a std place to keep it */
    int32_t                    metasidcnt; /* number of sids per handle for metadata */

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
        struct PINT_server_setattr_op setattr;
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
        struct PINT_server_mgmt_split_dirent_op mgmt_split_dirent;
        struct PINT_server_mgmt_get_dirdata_op mgmt_get_dirdata_handle;
/*
        struct PINT_server_precreate_pool_refiller_op
                                               precreate_pool_refiller;
*/
        struct PINT_server_batch_create_op batch_create;
        struct PINT_server_batch_remove_op batch_remove;
        struct PINT_server_unstuff_op unstuff;
        struct PINT_server_create_copies_op create_copies;
        struct PINT_server_mirror_op mirror;
        struct PINT_server_tree_communicate_op tree_communicate;
        struct PINT_server_mgmt_get_dirent_op mgmt_get_dirent;
        struct PINT_server_mgmt_create_root_dir_op mgmt_create_root_dir;
        struct PINT_server_perf_update_op perf_update;
    } u;

} PINT_server_op;

/* V3 call to server_local needs a propoer argument */
#define PINT_CREATE_SUBORDINATE_SERVER_FRAME(__smcb,                                \
                                             __s_op,                                \
                                             __sid,                                 \
                                             __fs_id,                               \
                                             __location,                            \
                                             __req,                                 \
                                             __task_id)                             \
do {                                                                                \
      struct server_configuration_s *__config = PINT_server_config_mgr_get_config();\
      __s_op = (PINT_server_op *)malloc(sizeof(struct PINT_server_op));             \
      if(!__s_op)                                                                   \
      {                                                                             \
          gossip_err("%s:Error allocating subordinate server frame\n"               \
                    ,__func__);                                                     \
          return -PVFS_ENOMEM;                                                      \
      }                                                                             \
      memset(__s_op, 0, sizeof(struct PINT_server_op));                             \
      __s_op->req = &__s_op->decoded.stub_dec.req;                                  \
      PINT_sm_push_frame(__smcb, __task_id, __s_op);                                \
      if (__location != REMOTE_OPERATION &&                                         \
           (__location == LOCAL_OPERATION ||                                        \
             (!PVFS_SID_is_null(&(__sid)) &&                                        \
              !PVFS_SID_cmp(&(__sid),&(__config->host_sid))                         \
             )                                                                      \
           )                                                                        \
         )                                                                          \
      {                                                                             \
          __location = LOCAL_OPERATION;                                             \
          __req = __s_op->req;                                                      \
      }                                                                             \
      else                                                                          \
      {                                                                             \
        memset(&__s_op->msgarray_op, 0, sizeof(PINT_sm_msgarray_op));               \
        PINT_serv_init_msgarray_params(__s_op, __fs_id);                            \
      }                                                                             \
} while (0)

#define PINT_CLEANUP_SUBORDINATE_SERVER_FRAME(__s_op) \
    do { \
        PINT_cleanup_capability(&__s_op->req->capability); \
        free(__s_op); \
    } while (0)

/* state machine permission function */
typedef int (*PINT_server_req_perm_fun)(PINT_server_op *s_op);

#define PINT_GET_OBJECT_REF_DEFINE(req_name)                             \
static inline int PINT_get_object_ref_##req_name(                        \
    struct PVFS_server_req *req, PVFS_fs_id *fs_id, PVFS_handle *handle) \
{                                                                        \
    *fs_id = req->u.req_name.fs_id;                                      \
    *handle = req->u.req_name.handle;                                    \
    return 0;                                                            \
}

#define PINT_GET_CREDENTIAL_DEFINE(req_name)             \
static inline int PINT_get_credential_##req_name(        \
    struct PVFS_server_req *req, PVFS_credential **cred) \
{                                                        \
    *cred = &req->u.req_name.credential;                  \
    return 0;                                            \
}

enum PINT_server_req_access_type PINT_server_req_readonly(
                                    struct PVFS_server_req *req);
enum PINT_server_req_access_type PINT_server_req_modify(
                                    struct PVFS_server_req *req);

/* This struct is used when the unexpected SM needs to get req specific
 * control info from the different SMs via the get_ctrl method defined
 * below in PVFS_server_req_params
 */
struct PINT_server_req_ctrl
{
    PVFS_fs_id fs_id;
    PVFS_OID *handles;
    int count;          /* number of handles */
    PVFS_SID *sids;
    int sid_count;
};

struct PINT_server_req_params
{
    const char* string_name;

    /* For each request that specifies an object ref we
     * call the permission function set by the op state machine
     * to authorize access.
     */
    PINT_server_req_perm_fun perm;

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

    /* A callback implemented by the request to return the attribute
     * struct imbedded in the server request structure for a given op.
     */
    int (*get_attr)(struct PVFS_server_req *req,
                    PVFS_object_attr *attr);

    /* A callback implemented by the request to return the object reference
     * from the server request structure.
     */
    int (*get_object_ref)(struct PVFS_server_req *req,
                          PVFS_fs_id *fs_id,
                          PVFS_handle *handle);

    /* A callback implemented by the request to return the credential from
     * the server request structure. If the server request does not contain
     * a credential this field should be set to NULL.
     */
    int (*get_credential)(struct PVFS_server_req *req,
                          PVFS_credential **cred);

    void (*get_ctrl)(struct PVFS_server_req *req,
                     struct PINT_server_req_ctrl *req_ctrl);

    /* The state machine that performs the request */
    struct PINT_state_machine_s *state_machine;
};

struct PINT_server_req_entry
{
    enum PVFS_server_op op_type;
    struct PINT_server_req_params *params;
};

extern struct PINT_server_req_entry PINT_server_req_table[];

/* Exported Prototypes */

int PINT_server_req_get_attr(struct PVFS_server_req *req,
                             PVFS_object_attr *attr);

int PINT_server_req_get_object_ref(struct PVFS_server_req *req,
                                   PVFS_fs_id *fs_id,
                                   PVFS_handle *handle);

int PINT_server_req_get_credential(struct PVFS_server_req *req,
                                   PVFS_credential **cred);

void PINT_server_req_get_ctrl(struct PVFS_server_req *req,
                              struct PINT_server_req_ctrl *ctrl);

/* This hideous bit of code declares a function that takes a
 * pointer to a PVFS_server_req and returns pointer to another
 * function that itself returns
 * an int and takes a PVFS_server_op as an argument
 */
PINT_server_req_perm_fun
        PINT_server_req_get_perm_fun(struct PVFS_server_req *req);

enum PINT_server_req_access_type
        PINT_server_req_get_access_type(struct PVFS_server_req *req);

enum PINT_server_sched_policy
        PINT_server_req_get_sched_policy(struct PVFS_server_req *req);

const char* PINT_map_server_op_to_string(enum PVFS_server_op op);

/* PINT_ACCESS_DEBUG()
 *
 * macro for consistent printing of access records
 *
 * no return value
 */
#ifdef GOSSIP_DISABLE_DEBUG
#ifdef WIN32
#define PINT_ACCESS_DEBUG(__s_op, __mask, format, ...) do {} while (0)
#else
#define PINT_ACCESS_DEBUG(__s_op, __mask, format, f...) do {} while (0)
#endif
#else
#ifdef WIN32
#define PINT_ACCESS_DEBUG(__s_op, __mask, format, ...)                     \
    PINT_server_access_debug(__s_op, __mask, format, __VA_ARGS__)
#else
#define PINT_ACCESS_DEBUG(__s_op, __mask, format, f...)                     \
    PINT_server_access_debug(__s_op, __mask, format, ##f)
#endif
#endif

#ifndef GOSSIP_DISABLE_DEBUG
#ifdef WIN32
void PINT_server_access_debug(PINT_server_op * s_op,
                              PVFS_debug_mask debug_mask,
                              const char * format,
                              ...);
#else
void PINT_server_access_debug(PINT_server_op * s_op,
                              PVFS_debug_mask debug_mask,
                              const char * format,
                              ...) __attribute__((format(printf, 3, 4)));
#endif
#endif 

/* server side state machines */
extern struct PINT_state_machine_s pvfs2_mirror_sm;
extern struct PINT_state_machine_s pvfs2_pjmp_call_msgpairarray_sm;
extern struct PINT_state_machine_s pvfs2_pjmp_get_attr_sm;
extern struct PINT_state_machine_s pvfs2_pjmp_remove_work_sm;
extern struct PINT_state_machine_s pvfs2_pjmp_mirror_work_sm;
extern struct PINT_state_machine_s pvfs2_pjmp_create_immutable_copies_sm;
extern struct PINT_state_machine_s pvfs2_pjmp_get_attr_work_sm;
extern struct PINT_state_machine_s pvfs2_pjmp_set_attr_work_sm;

/* nested state machines */
extern struct PINT_state_machine_s pvfs2_set_attr_work_sm;
extern struct PINT_state_machine_s pvfs2_set_attr_with_prelude_sm;
extern struct PINT_state_machine_s pvfs2_get_attr_sm;
extern struct PINT_state_machine_s pvfs2_get_attr_work_sm;
extern struct PINT_state_machine_s pvfs2_get_attr_with_prelude_sm;
extern struct PINT_state_machine_s pvfs2_prelude_sm;
extern struct PINT_state_machine_s pvfs2_prelude_work_sm;
extern struct PINT_state_machine_s pvfs2_final_response_sm;
extern struct PINT_state_machine_s pvfs2_check_entry_not_exist_sm;
extern struct PINT_state_machine_s pvfs2_remove_work_sm;
extern struct PINT_state_machine_s pvfs2_remove_with_prelude_sm;
extern struct PINT_state_machine_s pvfs2_mkdir_work_sm;
extern struct PINT_state_machine_s pvfs2_crdirent_work_sm;
extern struct PINT_state_machine_s pvfs2_unexpected_sm;
extern struct PINT_state_machine_s pvfs2_create_immutable_copies_sm;
extern struct PINT_state_machine_s pvfs2_mirror_work_sm;
extern struct PINT_state_machine_s pvfs2_tree_remove_work_sm;
extern struct PINT_state_machine_s pvfs2_tree_get_file_size_work_sm;
extern struct PINT_state_machine_s pvfs2_tree_getattr_work_sm;
extern struct PINT_state_machine_s pvfs2_tree_setattr_work_sm;
extern struct PINT_state_machine_s pvfs2_call_msgpairarray_sm;
extern struct PINT_state_machine_s pvfs2_dirdata_split_sm;

extern void tree_getattr_free(PINT_server_op *s_op);
extern void tree_setattr_free(PINT_server_op *s_op);
extern void tree_remove_free(PINT_server_op *s_op);
extern void mkdir_free(struct PINT_server_op *s_op);
extern void getattr_free(struct PINT_server_op *s_op);

/* V3 */
/* Exported Prototypes */
int server_perf_start_rollover(struct PINT_perf_counter *pc,
                               struct PINT_perf_counter *tpc);

/* keyval management prototypes */
void free_keyval_buffers(struct PINT_server_op *s_op);
void keep_keyval_buffers(struct PINT_server_op *s_op, int buf);
/* this macro is used in keyval management to represent the key and val
 * pointers of the s_op
 */
#define KEYVAL -1

/* exported state machine resource reclamation function */
int server_post_unexpected_recv(void);
int server_state_machine_start( PINT_smcb *smcb, job_status_s *js_p);
int server_state_machine_complete(PINT_smcb *smcb);
int server_state_machine_terminate(PINT_smcb *smcb, job_status_s *js_p);
int server_state_machine_wait(void);

/* lists of server ops */
extern struct qlist_head posted_sop_list;
extern struct qlist_head inprogress_sop_list;

/* starts state machines not associated with an incoming request */
int server_state_machine_alloc_noreq(enum PVFS_server_op op,
                                     struct PINT_smcb ** new_op);
int server_state_machine_start_noreq(struct PINT_smcb *new_op);
int server_state_machine_complete_noreq(PINT_smcb *smcb);

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
