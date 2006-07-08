/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __STATE_MACHINE_FNS_H
#define __STATE_MACHINE_FNS_H

#include <assert.h>

#include "gossip.h"
#include "pvfs2-debug.h"
#include "state-machine.h"

/* STATE-MACHINE-FNS.C
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
 * The PINT_OP_STATE_TABLE has been replaced with a macro that must be #defined
 * instead: PINT_OP_STATE_GET_MACHINE.  
 * This allows the _locate function to be used in the client as well.
 *
 * A good example of this is the pvfs2-server.h in the src/server directory,
 * which includes state-machine.h at the bottom, and server-state-machine.c,
 * which includes first pvfs2-server.h and then state-machine-fns.h.
 */


/* Function: PINT_state_machine_halt(void)
   Params: None
   Returns: True
   Synopsis: This function is used to shutdown the state machine 
 */
int PINT_state_machine_halt(void)
{
    return 0;
}

/* Function: PINT_state_machine_invoke
   Params:
   Returns:
   Synopsis: Currently undocumented!  Why is this here?  appears
            only potential use is in the function above, where it
            it isn't even used.
 */
int PINT_state_machine_invoke(struct PINT_smcb *smcb, job_status_s *r)
{
    int retval;
    const char * state_name;
    const char * machine_name;

    state_name = PINT_state_machine_current_state_name(smcb);
    machine_name = PINT_state_machine_current_machine_name(smcb);

    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG, 
                 "[SM Entering]: (%p) %s:%s (status: %d)\n",
                 smcb,
                 /* skip pvfs2_ */
                 machine_name,
                 state_name,
                 (int32_t)r->status_user_tag);
                
    retval = (smcb->current_state->state_action)(smcb,r);

    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG, 
                 "[SM Exiting]: (%p) %s:%s (error code: %d)\n",
                 smcb,
                 /* skip pvfs2_ */
                 machine_name,
                 state_name,
                 r->error_code);
    return retval;
}

/* Function: PINT_state_machine_next()
   Params: 
   Returns:   return value of state action

   Synopsis: Runs through a list of return values to find the next function to
   call.  Calls that function.  Once that function is called, this one exits
   and we go back to pvfs2-server.c's while loop.
 */
int PINT_state_machine_next(struct PINT_smcb *smbc, job_status_s *r)
{
    int code_val = r->error_code;       /* temp to hold the return code */
    int retval;            /* temp to hold return value of state action */
    union PINT_state_array_values *loc; /* temp pointer into state memory */

    do {
	/* skip over the current state action to get to the return code list */
	loc = smcb->current_state + 1;

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

	/* its not legal to actually reach a termination point; preceding
	 * function should have completed state machine.
	 */
	if(loc->flag == SM_TERMINATE)
	{
	    gossip_err("Error: state machine using an invalid termination path.\n");
	    return(-PVFS_EINVAL);
	}

	/* Update the server_op struct to reflect the new location
	 * see if the selected return value is a STATE_RETURN */
	if (loc->flag == SM_RETURN)
	{
	    smcb->current_state = PINT_pop_state(s);
	    smcb->current_state += 1; /* skip state flags */
	}
    } while (loc->flag == SM_RETURN);

    smcb->current_state = loc->next_state;
    smcb->current_state += 2;


    /* To do nested states, we check to see if the next state is
     * a nested state machine, and if so we push the return state
     * onto a stack */
    while (smcb->current_state->flag == SM_JUMP)
    {
	PINT_push_state(smcb, smcb->current_state);
	smcb->current_state += 1; /* skip state flag; now we point to the state
			  	   * machine */

	smcb->current_state = smcb->current_state->nested_machine->state_machine;
        smcb->current_state += 2;
    }

    /* skip over the flag so that we point to the function for the next
     * state.  then call it.
     */
    smcb->current_state += 1;

    /* runs state_action and returns the return code */
    return PINT_state_machine_invoke(smcb, r);
}

/* Function: PINT_state_machine_locate(void)
   Params:   smcb pointer with op correctly set
   Returns:  Pointer to the start of the state machine indicated by
	          smcb->op
   Synopsis: This function locates the state associated with the op
             specified in smcb->op in order to start a state machine's
             execution.
 */
union PINT_state_array_values *PINT_state_machine_locate(struct PINT_smcb *smcb)
{
    union PINT_state_array_values *current_tmp;
    struct PINT_state_machine_s *op_sm;

    /* check for valid inputs */
    if (!smcb || smcb->op < 0)
    {
	gossip_err("State machine requested not valid\n");
	return NULL;
    }
    /* this is a the usage dependant routine to look up the SM */
    op_sm = (*smcb->op_state_get_machine)(smcb->op);
    if (op_sm != NULL)
    {
	current_tmp = op_sm->state_machine;
        current_tmp += 2;
	/* handle the case in which the first state points to a nested
	 * machine, rather than a simple function
	 */
	while(current_tmp->flag == SM_JUMP)
	{
	    PINT_push_state(smcb, current_tmp);
	    current_tmp += 1;
	    current_tmp = ((struct PINT_state_machine_s *)
                           current_tmp->nested_machine)->state_machine;
            current_tmp += 2;
	}

	/* this returns a pointer to a "PINT_state_array_values"
	 * structure, whose state_action member is the function to call.
	 */
	return current_tmp + 1;
    }

    gossip_err("State machine not found for operation %d\n",smcb->op);
    return NULL;
}

/* Function: PINT_smcb_alloc
   Params: pointer to an smcb pointer, and an op code (int)
   Returns: nothing, but fills in pointer argument
   Synopsis: this allocates an smcb struct, including its frame stack
             and sets the op code so you can start the state machine
 */
void PINT_smcb_alloc(
        struct PINT_smcb **smcb,
        int op,
        struct PINT_state_machine_s *(*getmach)(int))
{
    *smcb = (struct PINT_smcb *)malloc(sizeof(struct PINT_smcb));
    if (!(*smcb))
    {
        return -PVFS_ENOMEM;
    }
    memset(*smcb, 0, sizeof(struct PINT_smcb));
    (*smcb)->frame_stack[0] =
            (struct PINT_smcb *)malloc(sizeof(struct PINT_OP_STATE));
    if (!((*smcb)->frame_stack[0]))
    {
        return -PVFS_ENOMEM;
    }
    memset((*smcb)->frame_stack[0], 0, sizeof(struct PINT_OP_STATE));
    (*smcb)->op = op;
    (*smcb)->op_state_get_machine = getmach;
}

/* Function: PINT_smcb_free
   Params: pointer to an smcb pointer
   Returns: nothing, but sets the pointer to NULL
   Synopsis: this frees an smcb struct, including its frame stack
             and anything on the frame stack
 */
void PINT_smcb_free(struct PINT_smcb **smcb)
{
    if (smcb)
    {
        if (*smcb)
        {
            for (i = 0; i < PINT_FRAME_STACK_SIZE; i++)
                if ((*smcb)->frame_stack[i])
                    /* DO we really want to do this??? */
                    free((*smcb)->frame_stack[i]);
            free(*smcb);
        }
        (*smcb) = (struct PINT_smcb *)0;
    }
}

/* Function: PINT_smcb_free
   Params: pointer to an smcb pointer
   Returns: nothing, but sets the pointer to NULL
   Synopsis: this frees an smcb struct, including its frame stack
             and anything on the frame stack
 */
union PINT_state_array_values *PINT_pop_state(struct PINT_smcb *smcb)
{
    assert(smcb->stackptr > 0);

    return smcb->state_stack[--smcb->stackptr];
}

/* Function: PINT_smcb_free
   Params: pointer to an smcb pointer
   Returns: nothing, but sets the pointer to NULL
   Synopsis: this frees an smcb struct, including its frame stack
             and anything on the frame stack
 */
void PINT_push_state(struct PINT_smcb *smcb,
				   union PINT_state_array_values *p)
{
    assert(smcb->stackptr < PINT_STATE_STACK_SIZE);

    smcb->state_stack[smcb->stackptr++] = p;
}

struct PINT_server_op *PINT_frame_server(struct PINT_smcb *smcb, int index)
{
    return &smcb->framestack[smcb->framebase + index].s;
}

struct PINT_client_sm *PINT_frame_client(struct PINT_smcb *smcb, int index)
{
    return &smcb->framestack[smcb->framebase + index].c;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif
