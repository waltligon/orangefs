/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Some config values for the prototype pvfs2 StateMachine */


#ifndef __PVFS_SERVER_STATE
#define __PVFS_SERVER_STATE

#include <pvfs2-req-proto.h>
#include <trove.h>
#include <job.h>
#include <bmi.h>
#include <errno.h>
#include <gossip.h>
#include <pvfs2-debug.h>
#include <pvfs2-storage.h>
#include <PINT-reqproto-encode.h>
#include <pack.h>

typedef struct PINT_server_op state_action_struct;

union PINT_state_array_values
{
	int (*state_action)(state_action_struct*,job_status_s*);
	int return_value;
	union PINT_state_array_values *next_state;
};

typedef union PINT_state_array_values PINT_state_array_values;

enum {
	JMP_NOT_READY = 99,
	DEFAULT_ERROR = -1,
	SERVER_OP_TABLE_SIZE = 99
};

/* Values for use in KeyVal Array */

enum {
	ROOT_HANDLE_KEY = 0,
	METADATA_KEY = 1,
	DIR_ENT_KEY = 2,
	KEYVAL_ARRAY_SIZE = 6
};

typedef struct PINT_server_trove_keys
{
	char *key;
	int size;
} PINT_server_trove_keys_s;

typedef struct PINT_state_machine_s
{
	PINT_state_array_values *state_machine;
	char *name;
	void (*init_fun)(void);
} PINT_state_machine_s;


/* Prototypes */

int PINT_state_machine_init(void);
int PINT_state_machine_halt(void);
int PINT_state_machine_next(state_action_struct*,job_status_s *r);
PINT_state_array_values *PINT_state_machine_locate(state_action_struct*);
int PINT_state_machine_initialize_unexpected(state_action_struct*, job_status_s *ret);

#include <pvfs2-server.h>


#endif /* __PVFS_SERVER_STATE */
