/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Some config values for the prototype pvfs2 server */

#ifndef __PVFS_SERVER_H
#define __PVFS_SERVER_H


enum
{
	PVFS2_DEBUG_SERVER = 32,
	BMI_UNEXP = 999, /* Give the Server the idea of what BMI_Unexpected Ops are */
	MAX_JOBS = 10 /* also defined in a config file, but nice to have */
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
	job_id_t scheduled_id;
	PVFS_ds_keyval_s key;
	PVFS_ds_keyval_s val;
	PVFS_ds_keyval_s *key_a;
	PVFS_ds_keyval_s *val_a;
	bmi_addr_t addr;
	bmi_msg_tag_t tag;
	PINT_state_array_values *current_state;
	struct PVFS_server_req_s *req;
	struct PVFS_server_resp_s *resp;
	struct BMI_unexpected_info *unexp_bmi_buff;
	struct PINT_encoded_msg encoded;
} PINT_server_op;


/* Globals for Server Interface */

/* Exported Prototypes */
struct server_configuration_s *get_server_config_struct(void);

#endif /* __PVFS_SERVER_H */
