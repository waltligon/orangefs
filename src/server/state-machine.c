/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <assert.h>

#include "bmi.h"
#include "gossip.h"
#include "job.h"
#include "pvfs2-debug.h"
#include "pvfs2-storage.h"
#include "PINT-reqproto-encode.h"

#include "state-machine.h"
#include "pvfs2-server.h"

/* keep one copy of strings commonly used in trove keyval lookups */
PINT_server_trove_keys_s Trove_Common_Keys[] = {
    {"root_handle", 12},
    {"metadata", 9},
    {"dir_ent", 8},
    {"datafile_handles", 17},
    {"metafile_dist", 14}
};

#define ENCODE_TYPE 0
#define SM_STATE_RETURN -1
#define SM_NESTED_STATE 1

/* Here is the idea...
 * For each state machine, you start with an initial function.
 * Upon posting/completion, you then check return values.
 * A complete state machine with return values should be located
 * in a contiguous region.  
 * Exception: when states are shared between machines. (Damn)
 * Therefore, I guess it really does not matter... but it would be
 * nice if they were as contiguous as possible just to save on the
 * debugging time. dw
*/

extern PINT_state_machine_s get_config;
extern PINT_state_machine_s get_attr;
extern PINT_state_machine_s set_attr;
extern PINT_state_machine_s create;
extern PINT_state_machine_s crdirent;
extern PINT_state_machine_s mkdir_;
extern PINT_state_machine_s readdir_;
extern PINT_state_machine_s lookup;
extern PINT_state_machine_s io;
extern PINT_state_machine_s remove_;
extern PINT_state_machine_s rmdirent;

/* table of state machines, indexed based on PVFS_server_op enumeration */
/* NOTE: this table is initialized at run time in PINT_state_machine_init() */
PINT_state_machine_s *PINT_server_op_table[PVFS_MAX_SERVER_OP+1] = {NULL};

/* 
 * Function: PINT_state_machine_initialize_unexpected(s_op,ret)
 *
 * Params:   PINT_server_op *s_op
 *           job_status_s *ret
 *    
 * Returns:  int
 * 
 * Synopsis: Intialize request structure, first location, and call
 *           respective init function.
 *
 * Initialization:
 * - sets s_op->op, addr, tag
 * - allocates space for s_op->resp and memset()s it to zero
 * - points s_op->req to s_op->decoded.buffer
 */
int PINT_state_machine_initialize_unexpected(PINT_server_op *s_op,
					     job_status_s *ret)
{
    PINT_decode(s_op->unexp_bmi_buff.buffer,
		PINT_ENCODE_REQ,
		&s_op->decoded,
		s_op->unexp_bmi_buff.addr,
		s_op->unexp_bmi_buff.size);

    s_op->req  = (struct PVFS_server_req *) s_op->decoded.buffer;
    assert(s_op->req != NULL);

    s_op->addr = s_op->unexp_bmi_buff.addr;
    s_op->tag  = s_op->unexp_bmi_buff.tag;
    s_op->op   = s_op->req->op;
    s_op->current_state = PINT_state_machine_locate(s_op);

    if(!s_op->current_state)
    {
	gossip_err("System not init for function\n");
	return(-1);
    }

    /* allocate and zero memory for (unencoded) response */
    s_op->resp = (struct PVFS_server_resp *)
	malloc(sizeof(struct PVFS_server_resp));

    if (!s_op->resp)
    {
	gossip_err("Out of Memory");
	ret->error_code = 1;
	return(-ENOMEM);
    }
    memset(s_op->resp, 0, sizeof(struct PVFS_server_resp));

    s_op->resp->op = s_op->req->op;

    return ((s_op->current_state->state_action))(s_op,ret);
}


/* Function: PINT_state_machine_init(void)
   Params: None
   Returns: True
   Synopsis: This function is used to initialize the state machine 
	 for operation.  Calls each machine's initi function if specified.
 */

int PINT_state_machine_init(void)
{
    int i;

    /* fill in indexes for each supported request type */
    PINT_server_op_table[PVFS_SERV_INVALID] = NULL;
    PINT_server_op_table[PVFS_SERV_CREATE] = &create;
    PINT_server_op_table[PVFS_SERV_REMOVE] = &remove_;
    PINT_server_op_table[PVFS_SERV_IO] = &io;
    PINT_server_op_table[PVFS_SERV_GETATTR] = &get_attr;
    PINT_server_op_table[PVFS_SERV_SETATTR] = &set_attr;
    PINT_server_op_table[PVFS_SERV_LOOKUP_PATH] = &lookup;
    PINT_server_op_table[PVFS_SERV_CREATEDIRENT] = &crdirent;
    PINT_server_op_table[PVFS_SERV_RMDIRENT] = &rmdirent;
    PINT_server_op_table[PVFS_SERV_MKDIR] = &mkdir_;
    PINT_server_op_table[PVFS_SERV_READDIR] = &readdir_;
    PINT_server_op_table[PVFS_SERV_GETCONFIG] = &get_config;

    /* initialize each state machine */
    for (i = 0 ; i <= PVFS_MAX_SERVER_OP; i++)
    {
	if(PINT_server_op_table[i] && PINT_server_op_table[i]->init_fun)
	{
	    (PINT_server_op_table[i]->init_fun)();
	}
    }
    return(0);
}


/* Function: PINT_state_machine_halt(void)
   Params: None
   Returns: True
   Synopsis: This function is used to shutdown the state machine 
 */

int PINT_state_machine_halt(void)
{
    return(-1);
}


/* Function: PINT_state_machine_next()
   Params: 
   Returns:   return value of state action
   Synopsis:  Runs through a list of return values to find the
	           next function to call.  Calls that function.  Once
				  that function is called,
				  this one exits and we go
				  back to pvfs2-server.c's while loop.
 */

int PINT_state_machine_next(PINT_server_op *s,job_status_s *r)
{

    int code_val = r->error_code;       /* temp to hold the return code */
    int retval;            /* temp to hold return value of state action */
    PINT_state_array_values *loc;     /* temp pointer into state memory */

    do {
	/* skip over the current state action to get to the return code list */
	loc = s->current_state + 1;

	/* for each entry in the state machine table there is a return
	 * code followed by a next state pointer to the new state.
	 * This loops through each entry, checking for a match on the
	 * return address, and then sets the new current_state and calls
	 * the new state action function */
	while (loc->return_value != code_val &&
		loc->return_value != DEFAULT_ERROR) 
	{
	    /* each entry is two items long */
	    loc += 2;
	}

	/* skip over the return code to get to the next state */
	loc += 1;

	/* Update the server_op struct to reflect the new location
	 * see if the selected return value is a STATE_RETURN */
	if (loc->flag == SM_STATE_RETURN)
	{
	    s->current_state = PINT_pop_state(s);
	    s->current_state += 1; /* skip state flags */
	}
    } while (loc->flag == SM_STATE_RETURN);

    s->current_state = loc->next_state;

    /* To do nested states, we check to see if the next state is
     * a nested state machine, and if so we push the return state
     * onto a stack */
    while (s->current_state->flag == SM_NESTED_STATE)
    {
	PINT_push_state(s, NULL);
	s->current_state += 1; /* skip state flag */
	s->current_state = s->current_state->nested_machine->state_machine;
    }

    /* skip over the flag so we can execute the next state action */
    s->current_state += 1;

    /* Call the new state function then */
    retval = (s->current_state->state_action)(s,r);

    /* return to the while loop in pvfs2-server.c */
    return retval;

}

/* Function: PINT_state_machine_locate(void)
   Params:  
   Returns:  Pointer to the start of the state machine indicated by
	          s_op->op
   Synopsis: This function is used to start a state machines execution.
 */

PINT_state_array_values *PINT_state_machine_locate(PINT_server_op *s_op)
{
    /* check for valid inputs */
    if (!s_op || s_op->op < 0 || s_op->op > PVFS_MAX_SERVER_OP)
    {
	gossip_err("State machine requested not valid\n");
	return NULL;
    }
    if(PINT_server_op_table[s_op->op] != NULL)
    {
	return PINT_server_op_table[s_op->op]->state_machine + 1;
    }
    gossip_err("State machine not found for operation %d\n",s_op->op);
    return NULL;
}

PINT_state_array_values *PINT_pop_state(PINT_server_op *s)
{
    if (s->stackptr <= 0)
    {
	gossip_err("State machine return on empty stack\n");
	return NULL;
    }
    else
    {
	return s->state_stack[--s->stackptr];
    }
}

void PINT_push_state(PINT_server_op *s, PINT_state_array_values *p)
{
    if (s->stackptr > PINTSTACKSIZE)
    {
	gossip_err("State machine jump on full stack\n");
	return;
    }
    else
    {
	s->state_stack[s->stackptr++] = p;
    }
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
