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

#define PINT_STATE_STACK_SIZE 8  /* size of stack for nested state machines */

/* Some config values for the prototype pvfs2 server */
enum
{
    PVFS2_DEBUG_SERVER = 32,
    BMI_UNEXP = 999, /* Give the Server the idea of what BMI_Unexpected Ops are */
    MAX_JOBS = 10 /* also defined in a config file, but nice to have */
};


/* used to keep a random, but handy, list of keys around */
typedef struct PINT_server_trove_keys
{
	char *key;
	int size;
} PINT_server_trove_keys_s;

enum {
    ROOT_HANDLE_KEY      = 0,
    METADATA_KEY         = 1,
    DIR_ENT_KEY          = 2,
    METAFILE_HANDLES_KEY = 3,
    METAFILE_DIST_KEY    = 4,
    KEYVAL_ARRAY_SIZE    = 6
};

typedef enum
{
    STATUS_UNKNOWN = 0,            /* default value                      */
    DEALLOC_INIT_MEMORY,           /* de-alloc any memory we have        */
    SHUTDOWN_GOSSIP_INTERFACE,     /* turn off gossip interface          */
    SHUTDOWN_ENCODE_INTERFACE,     /* turn off protocol encoder          */
    SHUTDOWN_BMI_INTERFACE,        /* turn off bmi interface             */
    SHUTDOWN_FLOW_INTERFACE,       /* turn off flow interface            */
    SHUTDOWN_STORAGE_INTERFACE,    /* turn off storage interface         */
    SHUTDOWN_JOB_INTERFACE,        /* turn off job interface             */
    SHUTDOWN_HIGH_LEVEL_INTERFACE, /* turn off high level interface      */
    STATE_MACHINE_HALT,            /* state machine failure              */
    CHECK_DEPS_QUEUE,              /* Check Deps/ Queue                  */
    UNEXPECTED_BMI_FAILURE,        /* BMI unexpected failure             */
    UNEXPECTED_POSTINIT_FAILURE,   /* running fine; failed in while loop */
    UNEXPECTED_LOOP_END,           /* outside of while loop in main()    */
} PINT_server_status_code;

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


/* Globals for Server Interface */

/* nested state machines */
extern struct PINT_state_machine_s pvfs2_prelude_sm;
extern struct PINT_state_machine_s pvfs2_final_response_sm;

/* Exported Prototypes */
struct server_configuration_s *get_server_config_struct(void);

/* INCLUDE STATE-MACHINE.H DOWN HERE */
#define PINT_OP_STATE       PINT_server_op
#define PINT_OP_STATE_TABLE PINT_server_op_table

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

