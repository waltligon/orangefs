/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_SERVER_H
#define __PVFS2_SERVER_H

#define PINTSTACKSIZE 8  /* size of stack for nested state machines */

typedef union PINT_state_array_values PINT_state_array_values;

/* Some config values for the prototype pvfs2 server */
enum
{
    PVFS2_DEBUG_SERVER = 32,
    BMI_UNEXP = 999, /* Give the Server the idea of what BMI_Unexpected Ops are */
    MAX_JOBS = 10 /* also defined in a config file, but nice to have */
};

typedef enum
{
    STATUS_UNKNOWN = 0,            /* default value                      */
    DEALLOC_INIT_MEMORY,           /* de-alloc any memory we have        */
    SHUTDOWN_GOSSIP_INTERFACE,     /* turn off gossip interface          */
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
    PVFS_object_attr directory_attr; /* holds attributes of directory to read */
    PVFS_handle dirent_handle;       /* holds handle of dirdata dspace from which entries are read */
};

struct PINT_server_rmdirent_op {
    PVFS_object_attr parent_attr;             /* holds attributes of directory from which entry will be removed */
    PVFS_handle dirdata_handle, entry_handle; /* holds handle of dirdata object, removed entry */
};

struct PINT_server_crdirent_op {
    PVFS_object_attr parent_attr; /* holds attributes of the parent directory */
    PVFS_handle dirent_handle;    /* holds handle of dirdata dspace that we'll write the dirent into */
};

struct PINT_server_remove_op {
    PVFS_object_attr object_attr; /* holds attributes of object to be removed as read from keyval space*/
    PVFS_handle dirdata_handle;   /* holds dirdata dspace handle in the event that we are removing a directory */
};

struct PINT_server_getconfig_op {
    int strsize; /* used to hold string lengths during getconfig processing */
};

struct PINT_server_io_op {
    flow_descriptor* flow_d;
};
    
/* This structure is passed into the void *ptr 
 * within the job interface.  Used to tell us where
 * to go next in our state machine.
 *
 * This structure is allocated and memset() to zero in PINT_server_cp_bmi_unexp().
 * s_op->op is set to BMI_UNEXP at that time, but reset in intialize_unexpected.
 */
typedef struct PINT_server_op
{
    int op; /* op == req->op after initialize_unexpected */
    int enc_type;
    job_id_t scheduled_id; /* holds id from request scheduler so we can release it later */

    PVFS_ds_keyval_s key, val; /* generic structures used in most server operations */
    PVFS_ds_keyval_s *key_a;
    PVFS_ds_keyval_s *val_a;

    bmi_addr_t addr; /* set in initialize_unexpected */
    bmi_msg_tag_t tag; /* set in initialize_unexpected */
    PINT_state_array_values *current_state; /* initialized in initialize_unexpected */
    PINT_state_array_values *state_stack[PINTSTACKSIZE]; 
    int stackptr; /* stack of contexts for nested state machines */
    struct PVFS_server_req_s *req; /* req == decoded.buffer after initialize_unexpected */
    struct PVFS_server_resp_s *resp; /* resp space allocated, memset(0) in initialize_unexpected
				      *
                                      * note: resp->op == req->op after initialize_unexpected also!
				      */
    struct BMI_unexpected_info unexp_bmi_buff;
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
    } u; /* TODO: RENAME TO 'scratch' */
} PINT_server_op;


/* Globals for Server Interface */

/* Exported Prototypes */
struct server_configuration_s *get_server_config_struct(void);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif /* __PVFS_SERVER_H */

