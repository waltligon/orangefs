/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_SERVER_H
#define __PVFS2_SERVER_H

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
    PVFS_object_attr base_attr; /* used to hold attributes of the base handle, which don't go in resp */
};

struct PINT_server_readdir_op {
    PVFS_object_attr directory_attr; /* used to hold attributes of directory to read */
    PVFS_handle dirent_handle; /* used to hold handle of dirdata dspace from which entries are read */
};

struct PINT_server_crdirent_op {
    PVFS_object_attr parent_attr; /* used to hold attributes of the parent directory */
    PVFS_handle dirent_handle; /* used to hold handle of dirdata dspace that we'll write the dirent into */
};

struct PINT_server_remove_op {
    PVFS_object_attr object_attr; /* used to hold attributes of object to be removed */
};

/* This structure is passed into the void *ptr 
 * within the job interface.  Used to tell us where
 * to go next in our state machine.
 */
typedef struct PINT_server_op
{
    int op;
    int strsize;
    int enc_type;
    job_id_t scheduled_id; /* holds id from request scheduler so we can release it later */

    PVFS_ds_keyval_s key;
    PVFS_ds_keyval_s val;
    PVFS_ds_keyval_s *key_a;
    PVFS_ds_keyval_s *val_a;

    bmi_addr_t addr;
    bmi_msg_tag_t tag;
    PINT_state_array_values *current_state;
    struct PVFS_server_req_s *req;
    struct PVFS_server_resp_s *resp;
    struct BMI_unexpected_info unexp_bmi_buff;
    struct PINT_encoded_msg encoded;
    struct PINT_decoded_msg decoded;
    flow_descriptor* flow_d;
    union {
	/* request-specific scratch spaces for use during processing */
	struct PINT_server_lookup_op   lookup;
	struct PINT_server_crdirent_op crdirent;
	struct PINT_server_readdir_op  readdir;
	struct PINT_server_remove_op   remove;
    } u;
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

