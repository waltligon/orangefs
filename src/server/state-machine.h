/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Some config values for the prototype pvfs2 StateMachine */


#ifndef __PVFS_SERVER_STATE
#define __PVFS_SERVER_STATE

#include <pvfs2-req-proto.h>
#include <job.h>
#include <bmi.h>
#include <errno.h>
#include <gossip.h>
#include <pvfs2-debug.h>
#include <pvfs2-storage.h>
#include <PINT-reqproto-encode.h>
#include <pack.h>

#define set_fun(i,j,k) do {\
	i.state_machine[j].handler = k;\
} while(0)

#define set_ret(i,j,k) do {\
	i.state_machine[j].retVal = k;\
} while(0)

#define set_ptr(i,j,k) do {\
	i.state_machine[j].index = &(i.state_machine[k]);\
} while(0)

#define STATE_FXN_HEAD(__name)	int __name(PINT_server_op *s_op, job_status_s *ret)

#define STATE_FXN_RET(__name) return(__name);

typedef struct PINT_server_op handler_structure;

union PINT_state_array_values
{
	int retVal;
	int (*handler)(handler_structure*,job_status_s*);
	union PINT_state_array_values *index;
};

typedef union PINT_state_array_values PINT_state_array_values;

enum {
	JMP_NOT_READY = 99,
	DEFAULT_ERROR = -1,
	SERVER_REQ_ARRAY_SIZE = 99
};

/* Values for use in KeyVal Array */

enum {
	ROOT_HANDLE_KEY = 0,
	METADATA_KEY = 2,
	DIR_ENT_KEY = 4,
	KEYVAL_ARRAY_SIZE = 6
};

typedef struct PINT_state_machine_s
{
	PINT_state_array_values *state_machine;
	char *name;
	void (*init_fun)(void);
} PINT_state_machine_s;


/* Prototypes */

int PINT_state_machine_init(void);
int PINT_state_machine_halt(void);
int PINT_state_machine_next(handler_structure*,job_status_s *r);
PINT_state_array_values *PINT_state_machine_locate(handler_structure*);
int PINT_state_machine_initialize_unexpected(handler_structure*, job_status_s *ret);

#include <pvfs2-server.h>


#endif /* __PVFS_SERVER_STATE */
