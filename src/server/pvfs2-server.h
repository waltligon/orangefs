/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_SERVER_H
#define __PVFS2_SERVER_H

/* NOTE: STATE-MACHINE.H IS INCLUDED AT THE BOTTOM!  THIS IS SO WE CAN
 * DEFINE ALL THE STRUCTURES WE NEED BEFORE WE INCLUDE IT.
 */

#include "pvfs2-debug.h"
#include "pvfs2-storage.h"
#include "job.h"
#include "trove.h"
#include "gossip.h"

#include "PINT-reqproto-encode.h"


extern job_context_id server_job_context;

/* size of stack for nested state machines */
#define PINT_STATE_STACK_SIZE                  8
#define PVFS2_SERVER_DEFAULT_TIMEOUT_MS      100
#define BMI_UNEXPECTED_OP                    999

/* used to keep a random, but handy, list of keys around */
typedef struct PINT_server_trove_keys
{
    char *key;
    int size;
} PINT_server_trove_keys_s;

enum
{
    ROOT_HANDLE_KEY      = 0,
    METADATA_KEY         = 1,
    DIR_ENT_KEY          = 2,
    METAFILE_HANDLES_KEY = 3,
    METAFILE_DIST_KEY    = 4,
    KEYVAL_ARRAY_SIZE    = 6
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
} PINT_server_status_flag;

/* struct PINT_server_lookup_op
 *
 * All the data needed during lookup processing:
 *
 */
struct PINT_server_lookup_op {
    int seg_ct, seg_nr; /* current segment (0..N), number of segments in the path */
    char *segp;
    void *segstate;

    PVFS_handle dirent_handle;
    PVFS_object_attr base_attr; /* holds attributes of the base handle, which don't go in resp */
};

struct PINT_server_readdir_op {
    PVFS_handle dirent_handle;       /* holds handle of dirdata dspace from which entries are read */
};

struct PINT_server_rmdirent_op {
    PVFS_handle dirdata_handle, entry_handle; /* holds handle of dirdata object, removed entry */
};

struct PINT_server_crdirent_op {
    PVFS_handle dirent_handle;    /* holds handle of dirdata dspace that we'll write the dirent into */
};

struct PINT_server_remove_op {
    PVFS_handle dirdata_handle;   /* holds dirdata dspace handle in the event that we are removing a directory */
};

struct PINT_server_getconfig_op {
    int strsize; /* used to hold string lengths during getconfig processing */
};

struct PINT_server_io_op {
    flow_descriptor* flow_d;
};

struct PINT_server_flush_op {
    PVFS_handle handle;	    /* handle of data we want to flush to disk */
    int flags;		    /* any special flags for flush */
};


    
/* This structure is passed into the void *ptr 
 * within the job interface.  Used to tell us where
 * to go next in our state machine.
 */
typedef struct PINT_server_op
{
    enum PVFS_server_op op;  /* type of operation that we are servicing */
    /* the following fields are used in state machine processing to keep
     * track of the current state
     */
    int stackptr;
    union PINT_state_array_values *current_state; 
    union PINT_state_array_values *state_stack[PINT_STATE_STACK_SIZE]; 

    /* holds id from request scheduler so we can release it later */
    job_id_t scheduled_id; 

    /* generic structures used in most server operations */
    PVFS_ds_keyval key, val; 
    PVFS_ds_keyval *key_a;
    PVFS_ds_keyval *val_a;

    /* attributes structure associated with target of operation; may be 
     * partially filled in by prelude nested state machine (for 
     * permission checking); may be used/modified by later states as well
     */
    PVFS_object_attr attr;

    bmi_addr_t addr;   /* address of client that contacted us */
    bmi_msg_tag_t tag; /* operation tag */
    /* information about unexpected message that initiated this operation */
    struct BMI_unexpected_info unexp_bmi_buff;

    /* decoded request and response structures */
    struct PVFS_server_req *req; 
    struct PVFS_server_resp resp; 
    /* encoded request and response structures */
    struct PINT_encoded_msg encoded;
    struct PINT_decoded_msg decoded;

    union {
	/* request-specific scratch spaces for use during processing */
	struct PINT_server_getconfig_op getconfig;
	struct PINT_server_lookup_op    lookup;
	struct PINT_server_crdirent_op  crdirent;
	struct PINT_server_readdir_op   readdir;
	struct PINT_server_remove_op    remove;
	struct PINT_server_rmdirent_op  rmdirent;
	struct PINT_server_io_op	io;
	struct PINT_server_flush_op	flush;
    } u; /* TODO: RENAME TO 'scratch' */
} PINT_server_op;

/* TODO: maybe this should be put somewhere else? */
/* PINT_map_server_op_to_string()
 *
 * provides a string representation of the server operation number
 *
 * returns a pointer to a static string (DONT FREE IT) on success,
 * null on failure
 */
static inline char* PINT_map_server_op_to_string(enum PVFS_server_op op)
{
    char* ret_ptr = NULL;

    if(op > PVFS_MAX_SERVER_OP)
	return(NULL);

    switch(op)
    {
	case PVFS_SERV_INVALID:
	    ret_ptr = "invalid";
	    break;
	case PVFS_SERV_MGMT_SETPARAM:
	    ret_ptr = "mgmt_setparam";
	    break;
	case PVFS_SERV_CREATE:
	    ret_ptr = "create";
	    break;
	case PVFS_SERV_REMOVE:
	    ret_ptr = "remove";
	    break;
	case PVFS_SERV_IO:
	    ret_ptr = "io";
	    break;
	case PVFS_SERV_GETATTR:
	    ret_ptr = "getattr";
	    break;
	case PVFS_SERV_SETATTR:
	    ret_ptr = "setattr";
	    break;
	case PVFS_SERV_LOOKUP_PATH:
	    ret_ptr = "lookup_path";
	    break;
	case PVFS_SERV_CREATEDIRENT:
	    ret_ptr = "createdirent";
	    break;
	case PVFS_SERV_RMDIRENT:
	    ret_ptr = "rmdirent";
	    break;
	case PVFS_SERV_TRUNCATE:
	    ret_ptr = "truncate";
	    break;
	case PVFS_SERV_MKDIR:
	    ret_ptr = "mkdir";
	    break;
	case PVFS_SERV_READDIR:
	    ret_ptr = "readdir";
	    break;
	case PVFS_SERV_GETCONFIG:
	    ret_ptr = "getconfig";
	    break;
	case PVFS_SERV_WRITE_COMPLETION:
	    ret_ptr = "write_completion";
	    break;
	case PVFS_SERV_FLUSH:
	    ret_ptr = "flush";
	    break;
	case PVFS_SERV_MGMT_NOOP:
	    ret_ptr = "mgmt_noop";
	    break;
    }
    return(ret_ptr);
}

/* PINT_STATE_DEBUG()
 *
 * macro for consistent printing of state transition information
 * through gossip.  will only work within state machine functions.
 *
 * no return value
 */
#define PINT_STATE_DEBUG(fn_name)				\
    gossip_debug(SERVER_DEBUG, "(%p) %s state: %s\n", s_op,	\
    PINT_map_server_op_to_string(s_op->req->op), fn_name);

/* Globals for Server Interface */


/* server operation state machines */
extern struct PINT_state_machine_s pvfs2_get_config_sm;
extern struct PINT_state_machine_s pvfs2_get_attr_sm;
extern struct PINT_state_machine_s pvfs2_set_attr_sm;
extern struct PINT_state_machine_s pvfs2_create_sm;
extern struct PINT_state_machine_s pvfs2_crdirent_sm;
extern struct PINT_state_machine_s pvfs2_mkdir_sm;
extern struct PINT_state_machine_s pvfs2_readdir_sm;
extern struct PINT_state_machine_s pvfs2_lookup_sm;
extern struct PINT_state_machine_s pvfs2_io_sm;
extern struct PINT_state_machine_s pvfs2_remove_sm;
extern struct PINT_state_machine_s pvfs2_rmdirent_sm;
extern struct PINT_state_machine_s pvfs2_flush_sm;
extern struct PINT_state_machine_s pvfs2_setparam_sm;
extern struct PINT_state_machine_s pvfs2_noop_sm;

/* nested state machines */
extern struct PINT_state_machine_s pvfs2_prelude_sm;
extern struct PINT_state_machine_s pvfs2_final_response_sm;

/* Exported Prototypes */
struct server_configuration_s *get_server_config_struct(void);

/* INCLUDE STATE-MACHINE.H DOWN HERE */
#define PINT_OP_STATE       PINT_server_op
#define PINT_OP_STATE_TABLE PINT_server_op_table

/* exported state machine resource reclamation function */
int server_state_machine_complete(PINT_server_op *s_op);


#include "state-machine.h"

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif /* __PVFS_SERVER_H */

