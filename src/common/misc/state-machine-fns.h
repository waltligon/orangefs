/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __STATE_MACHINE_FNS_H
#define __STATE_MACHINE_FNS_H

#include "gossip.h"

/* STATE-MACHINE-FNS.H
 *
 * This file implements a small collection of functions used when
 * interacting with the state machine system implemented in
 * state-machine.h.  Probably you'll only need these functions in one
 * file per instance of a state machine implementation.
 *
 * Note that state-machine.h must be included before this is included.
 * This is usually accomplished through including some *other* file that
 * includes state-machine.h, because state-machine.h needs a key #define
 * before it can be included.
 *
 * The structure holding the table of PINT_OP_STATE structures also needs to
 * be declared; its name should be #defined as PINT_OP_STATE_TABLE.  If you
 * don't define this, you don't get the extern or the _locate function
 * (currently client might not need them?).
 *
 * A good example of this is the pvfs2-server.h in the src/server directory,
 * which includes state-machine.h at the bottom, and server-state-machine.c,
 * which includes first pvfs2-server.h and then state-machine-fns.h.
 */

#ifndef __STATE_MACHINE_H
#error "state-machine.h must be included before state-machine-fns.h is included."
#endif

#if 0
#ifndef PINT_OP_STATE_TABLE
#error "PINT_OP_STATE_TABLE must be defined before state-machine-fns.h is included."
#endif
#endif

/* Prototypes for functions defined in here */
static inline int PINT_state_machine_halt(void);
static inline int PINT_state_machine_next(struct PINT_OP_STATE *,job_status_s *r);
#ifdef PINT_OP_STATE_TABLE
static union PINT_state_array_values *PINT_state_machine_locate(struct PINT_OP_STATE *);
#endif
static inline union PINT_state_array_values *PINT_pop_state(struct PINT_OP_STATE *s);
static inline void PINT_push_state(struct PINT_OP_STATE *s, union PINT_state_array_values *p);

#ifdef PINT_OP_STATE_TABLE
extern struct PINT_state_machine_s *PINT_OP_STATE_TABLE[];
#endif

/* Function: PINT_state_machine_halt(void)
   Params: None
   Returns: True
   Synopsis: This function is used to shutdown the state machine 
 */
static inline int PINT_state_machine_halt(void)
{
    return 0;
}

/* Function: PINT_state_machine_next()
   Params: 
   Returns:   return value of state action

   Synopsis: Runs through a list of return values to find the next function to
   call.  Calls that function.  Once that function is called, this one exits
   and we go back to pvfs2-server.c's while loop.
 */
static inline int PINT_state_machine_next(struct PINT_OP_STATE *s, 
					  job_status_s *r)
{

    int code_val = r->error_code;       /* temp to hold the return code */
    int retval;            /* temp to hold return value of state action */
    union PINT_state_array_values *loc; /* temp pointer into state memory */

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
	    /* each entry has a return value followed by a next state
	     * pointer, so we increment by two.
	     */
	    loc += 2;
	}

	/* skip over the return code to get to the pointer for the
	 * next state
	 */
	loc += 1;

	/* Update the server_op struct to reflect the new location
	 * see if the selected return value is a STATE_RETURN */
	if (loc->flag == SM_RETURN)
	{
	    s->current_state = PINT_pop_state(s);
	    s->current_state += 1; /* skip state flags */
	}
    } while (loc->flag == SM_RETURN);

    s->current_state = loc->next_state;

    /* To do nested states, we check to see if the next state is
     * a nested state machine, and if so we push the return state
     * onto a stack */
    while (s->current_state->flag == SM_NESTED)
    {
	PINT_push_state(s, NULL);
	s->current_state += 1; /* skip state flag; now we point to the state
				* machine */

	/* NOTE: nested_machine is defined as a void * to eliminate a nasty
	 * cross-structure dependency that I couldn't handle any more.  -- Rob
	 */
	s->current_state = ((struct PINT_state_machine_s *) s->current_state->nested_machine)->state_machine;
    }

    /* skip over the flag so that we point to the function for the next
     * state.  then call it.
     */
    s->current_state += 1;

    retval = (s->current_state->state_action)(s,r);

    /* return to the while loop in pvfs2-server.c */
    return retval;
}

#ifdef PINT_OP_STATE_TABLE
/* Function: PINT_state_machine_locate(void)
   Params:  
   Returns:  Pointer to the start of the state machine indicated by
	          s_op->op
   Synopsis: This function is used to start a state machines execution.
 */

static union PINT_state_array_values *PINT_state_machine_locate(struct PINT_OP_STATE *s_op)
{
    /* check for valid inputs */
    if (!s_op || s_op->op < 0 || s_op->op > PVFS_MAX_SERVER_OP)
    {
	gossip_err("State machine requested not valid\n");
	return NULL;
    }
    if (PINT_OP_STATE_TABLE[s_op->op] != NULL)
    {
	/* this returns a pointer to a "PINT_state_array_values"
	 * structure, whose state_action member is the function to call.
	 */
	return PINT_OP_STATE_TABLE[s_op->op]->state_machine + 1;
    }
    gossip_err("State machine not found for operation %d\n",s_op->op);
    return NULL;
}
#endif

static inline union PINT_state_array_values *PINT_pop_state(struct PINT_OP_STATE *s)
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

static inline void PINT_push_state(struct PINT_OP_STATE *s,
				   union PINT_state_array_values *p)
{
    if (s->stackptr > PINT_STATE_STACK_SIZE)
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

#endif
