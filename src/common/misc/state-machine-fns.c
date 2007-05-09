/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __STATE_MACHINE_FNS_H
#define __STATE_MACHINE_FNS_H

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "gossip.h"
#include "pvfs2-debug.h"
#include "state-machine.h"
#include "client-state-machine.h"

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

/* Function: PINT_state_machine_terminate
   Params: smcb, job status
   Returns: 0 on sucess, otherwise error
   Synopsis: This function cleans up and terminates a SM
        in some cases we may need to keep the SM alive until
        its children terminate or something - in which case we
        will post the relevant job here.
 */
int PINT_state_machine_terminate(struct PINT_smcb *smcb, job_status_s *r)
{
    /* notify parent */
    if (smcb->parent_smcb)
    {
        job_id_t id;
        /* wake up parent, through job interface */
        gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
            "SM job_null called smcb %p\n", smcb->parent_smcb);
        job_null(0, smcb->parent_smcb, 0, r, &id, smcb->context);
    }
    /* call state machine completion function */
    if (smcb->terminate_fn)
    {
        (*smcb->terminate_fn)(smcb, r);
    }
    return 0;
}

/* Function: PINT_state_machine_invoke
   Params: smcb pointer and job status pointer
   Returns: return value of state action
   Synopsis: runs the current state action, produces debugging
        output if needed, checls return value and takes action
        if needed (sets op_terminate if SM_ACTION_TERMINATED is
        returned)
 */
PINT_sm_action PINT_state_machine_invoke(struct PINT_smcb *smcb,
                                         job_status_s *r)
{
    PINT_sm_action retval;
    const char * state_name;
    const char * machine_name;

    if (!(smcb) || !(smcb->current_state) ||
            !(smcb->current_state->flag == SM_RUN ||
              smcb->current_state->flag == SM_PJMP) ||
            !(smcb->current_state->action.func))
    {
        gossip_err("SM invoke called on invalid smcb or state\n");
        return SM_ERROR;
    }

    /* print pre-call debugging info */
    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
            "SM invoke smcb %p op %d\n",smcb,(smcb)->op);

    state_name = PINT_state_machine_current_state_name(smcb);
    machine_name = PINT_state_machine_current_machine_name(smcb);

    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG, 
                 "[SM Entering]: (%p) %s:%s (status: %d)\n",
                 smcb,
                 /* skip pvfs2_ */
                 machine_name,
                 state_name,
                 (int32_t)r->status_user_tag);
     
    /* call state action function */
    retval = (smcb->current_state->action.func)(smcb,r);
    /* process return code */
    switch (retval)
    {
    case SM_ACTION_TERMINATE :
            gossip_debug(GOSSIP_STATE_MACHINE_DEBUG, 
                    "SM Terminates (%p)\n", smcb);
            smcb->op_terminate = 1;
            break;
    case SM_ACTION_COMPLETE :
            gossip_debug(GOSSIP_STATE_MACHINE_DEBUG, 
                    "SM Returns Complete (%p)\n", smcb);
            break;
    case SM_ACTION_DEFERRED :
            gossip_debug(GOSSIP_STATE_MACHINE_DEBUG, 
                    "SM Returns Deferred (%p)\n", smcb);
            break;
    default :
            /* error */
            gossip_err("SM Action %s:%s returned invalid return code %d (%p)\n",
                       machine_name, state_name, retval, smcb);
            break;
    }

    /* print post-call debugging info */
    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG, 
                 "[SM Exiting]: (%p) %s:%s (error code: %d), (sm action: %s)\n",
                 smcb,
                 /* skip pvfs2_ */
                 machine_name,
                 state_name,
                 r->error_code,
                 SM_ACTION_STRING(retval));

    if (smcb->current_state->flag == SM_PJMP)
    {
        /* start child SMs */
        PINT_sm_start_child_frames(smcb);
        gossip_debug(GOSSIP_STATE_MACHINE_DEBUG, 
                "SM (%p) started %d child frames\n",
                smcb, smcb->children_running);
        if (smcb->children_running > 0)
            retval = SM_ACTION_DEFERRED;
        else
            retval = SM_ACTION_COMPLETE;
    }

    return retval;
}

/* Function: PINT_state_machine_start()
   Params: smcb pointer and job status pointer
   Returns: return value of last state action
   Synopsis: Runs the state action pointed to by the
        current state, then continues to run the SM
        as long as return code is SM_ACTION_COMPLETE.
 */

PINT_sm_action PINT_state_machine_start(struct PINT_smcb *smcb, job_status_s *r)
{
    PINT_sm_action ret;

    /* run the current state action function */
    ret = PINT_state_machine_invoke(smcb, r);
    if (ret == SM_ACTION_COMPLETE || ret == SM_ACTION_TERMINATE)
    {
        /* keep running until state machine deferrs or terminates */
        ret = PINT_state_machine_next(smcb, r);
        
        /* note that if ret == SM_ACTION_TERMINATE, we _don't_ call
         * PINT_state_machine_terminate here because that adds the smcb
         * to the completion list.  We don't want to do that on immediate
         * completion
         */
    }
    return ret;
}

/* Function: PINT_state_machine_next()
   Params: smcb pointer and job status pointer
   Returns: return value of last state action
   Synopsis: Runs through a list of return values to find the next function to
   call.  Calls that function.  If that function returned COMPLETED loop
   and repeat.
 */
PINT_sm_action PINT_state_machine_next(struct PINT_smcb *smcb, job_status_s *r)
{
    int i; /* index for transition table */
    struct PINT_tran_tbl_s *transtbl;
    PINT_sm_action ret;   /* holds state action return code */

    if (!smcb)
    {
        gossip_err("SM next called on invald smcb\n");
        return -1;
    }
    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
            "SM next smcb %p op %d\n",smcb,(smcb)->op);
    if (smcb->children_running > 0)
    {
        if (--smcb->children_running > 0)
        {
            /* SM is still deferred */
            return SM_ACTION_DEFERRED;
        }
    }
    /* loop while invoke of new state returns COMPLETED */
    do {
        /* loop while returning from nested SM */
        do {
            if (!smcb->current_state || !smcb->current_state->trtbl)
            {
                gossip_err("SM current state or trtbl is invalid "
                           "(smcb = %p)\n", smcb);
                gossip_backtrace();
                assert(0);
                return -1;
            }
            transtbl = smcb->current_state->trtbl;
    
	    /* for each entry in the transition table there is a return
	    * code followed by a next state pointer to the new state.
	    * This loops through each entry, checking for a match on the
	    * return address, and then sets the new current_state and calls
	    * the new state action function */
            for (i = 0; transtbl[i].return_value != DEFAULT_ERROR; i++)
            {
                if (transtbl[i].return_value == r->error_code)
                    break;
            }
	    /* we expect the last state action function to return
            * SM_ACTION_TERMINATE which sets the smcb->op_terminate
            * flag.  ALSO the state machine must direct the next state
            * to be terminate, which sets loc->flag to SM_TERMINATE.
	    * We'll terminate for EITHER, but print an error if not
            * both.
	    */
	    if(transtbl[i].flag == SM_TERM || smcb->op_terminate)
	    {
                if (!(transtbl[i].flag == SM_TERM))
                {
	            gossip_lerr("Error: state machine returned"
                           " SM_ACTION_TERMINATE but didn't reach terminate\n");
                }
                if (!smcb->op_terminate)
                {
	            gossip_lerr("Error: state machine reached terminate"
                            " without returning SM_ACTION_TERMINATE\n");
                    smcb->op_terminate = 1;
                }
                return SM_ACTION_TERMINATE;
	    }
	    if (transtbl[i].flag == SM_RETURN)
	    {
                /* if this is a return pop the stack
                 * and we'll continue from the state returned to
                 */
	        smcb->current_state = PINT_pop_state(smcb);
	    }
        } while (transtbl[i].flag == SM_RETURN);
        smcb->current_state = transtbl[i].next_state;
        /* To do nested states, we check to see if the next state is
        * a nested state machine, and if so we push the return state
        * onto a stack */
        while (smcb->current_state->flag == SM_JUMP)
        {
	    PINT_push_state(smcb, smcb->current_state);
	    smcb->current_state =
                    smcb->current_state->action.nested->first_state;
        }
        /* runs state_action and returns the return code */
        ret = PINT_state_machine_invoke(smcb, r);
    } while (ret == SM_ACTION_COMPLETE || ret == SM_ACTION_TERMINATE);
    return ret;
}

/* Function: PINT_state_machine_continue
   Params: smcb pointer and job status pointer
   Returns: return value of last state action
   Synopsis: This function essentially calls next, and if the state
             machine terminates, calls terminate to perform cleanup.
             This allows separation from the start call (which calls
             next but does not call terminate if the state machine
             terminates).
*/
PINT_sm_action PINT_state_machine_continue(struct PINT_smcb *smcb, job_status_s *r)
{
    PINT_sm_action ret;

    ret = PINT_state_machine_next(smcb, r);

    if(ret == SM_ACTION_TERMINATE)
    {
        /* process terminating SM */
        PINT_state_machine_terminate(smcb, r);
    }

    return ret;
}

/* Function: PINT_state_machine_locate(void)
   Params:   smcb pointer with op correctly set
   Returns:  1 on successful locate, 0 on locate failure, <0 on error
   Synopsis: This function locates the state associated with the op
             specified in smcb->op in order to start a state machine's
             execution.
 */
int PINT_state_machine_locate(struct PINT_smcb *smcb)
{
    struct PINT_state_s *current_tmp;
    struct PINT_state_machine_s *op_sm;

    /* check for valid inputs */
    if (!smcb || smcb->op < 0 || !smcb->op_get_state_machine)
    {
	gossip_err("State machine requested not valid\n");
	return -PVFS_EINVAL;
    }
    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
            "SM locate smcb %p op %d\n",smcb,(smcb)->op);
    /* this is a the usage dependant routine to look up the SM */
    op_sm = (*smcb->op_get_state_machine)(smcb->op);
    if (op_sm != NULL)
    {
	current_tmp = op_sm->first_state;
	/* handle the case in which the first state points to a nested
	 * machine, rather than a simple function
	 */
	while(current_tmp->flag == SM_JUMP)
	{
	    PINT_push_state(smcb, current_tmp);
	    current_tmp = ((struct PINT_state_machine_s *)
                           current_tmp->action.nested)->first_state;
	}
        smcb->current_state = current_tmp;
	return 1; /* indicates successful locate */
    }
    gossip_err("State machine not found for operation %d\n",smcb->op);
    return 0; /* indicates failed to locate */
}

/* Function: PINT_smcb_set_op
   Params: pointer to an smcb pointer, and an op code (int)
   Returns: nothing
   Synopsis: sets op on existing smcb and reruns locate if
            we have a valid locate func
 */
int PINT_smcb_set_op(struct PINT_smcb *smcb, int op)
{
    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
            "SM set op smcb %p op %d\n",smcb,op);
    smcb->op = op;
    return PINT_state_machine_locate(smcb);
}

/* Function: PINT_smcb_op
   Params: pointer to an smcb pointer
   Returns: op (int)
   Synopsis: returns the op currently set in the smcb
 */
int PINT_smcb_op(struct PINT_smcb *smcb)
{
    return smcb->op;
}

static int PINT_smcb_sys_op(struct PINT_smcb *smcb)
{
    if (smcb->op > 0 && smcb->op < PVFS_OP_SYS_MAXVALID)
        return 1;
    return 0;
}

static int PINT_smcb_mgmt_op(struct PINT_smcb *smcb)
{
    if (smcb->op > PVFS_OP_SYS_MAXVAL && smcb->op < PVFS_OP_MGMT_MAXVALID)
        return 1;
    return 0;
}

static int PINT_smcb_misc_op(struct PINT_smcb *smcb)
{
    return smcb->op == PVFS_SERVER_GET_CONFIG 
        || smcb->op == PVFS_SERVER_FETCH_CONFIG
        || smcb->op == PVFS_CLIENT_JOB_TIMER 
        || smcb->op == PVFS_CLIENT_PERF_COUNT_TIMER 
        || smcb->op == PVFS_DEV_UNEXPECTED;
}

int PINT_smcb_invalid_op(struct PINT_smcb *smcb)
{
    if (!PINT_smcb_sys_op(smcb) && !PINT_smcb_mgmt_op(smcb) && !PINT_smcb_misc_op(smcb))
        return 1;
    return 0;
}

/* Function: PINT_smcb_set_complete
   Params: pointer to an smcb pointer
   Returns: nothing
   Synopsis: sets op_terminate on existing smcb
 */
void PINT_smcb_set_complete(struct PINT_smcb *smcb)
{
    smcb->op_terminate = 1;
}

/* Function: PINT_smcb_complete
   Params: pointer to an smcb pointer
   Returns: op (int)
   Synopsis: returns the op_terminate currently set in the smcb
 */
int PINT_smcb_complete(struct PINT_smcb *smcb)
{
    return smcb->op_terminate;
}

/* Function: PINT_smcb_set_cancelled
   Params: pointer to an smcb pointer
   Returns: nothing
   Synopsis: sets op_cancelled on existing smcb
 */
void PINT_smcb_set_cancelled(struct PINT_smcb *smcb)
{
    smcb->op_cancelled = 1;
}

/* Function: PINT_smcb_cancelled
   Params: pointer to an smcb pointer
   Returns: op (int)
   Synopsis: returns the op_cancelled currently set in the smcb
 */
int PINT_smcb_cancelled(struct PINT_smcb *smcb)
{
    return smcb->op_cancelled;
}

/* Function: PINT_smcb_alloc
   Params: pointer to an smcb pointer, an op code (int), size of frame
            (int), pinter to function to locate SM
   Returns: nothing, but fills in pointer argument
   Synopsis: this allocates an smcb struct, including its frame stack
             and sets the op code so you can start the state machine
 */
int PINT_smcb_alloc(
        struct PINT_smcb **smcb,
        int op,
        int frame_size,
        struct PINT_state_machine_s *(*getmach)(int),
        int (*term_fn)(struct PINT_smcb *, job_status_s *),
        job_context_id context_id)
{
    *smcb = (struct PINT_smcb *)malloc(sizeof(struct PINT_smcb));
    if (!(*smcb))
    {
        return -PVFS_ENOMEM;
    }
    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
            "SM allocate smcb %p op %d\n",*smcb,op);
    /* zero out all members */
    memset(*smcb, 0, sizeof(struct PINT_smcb));
    /* if frame_size given, allocate a frame */
    if (frame_size > 0)
    {
        void *new_frame = malloc(frame_size);
        if (!new_frame)
        {
            free(*smcb);
            *smcb = NULL;
            return -PVFS_ENOMEM;
        }
        /* zero out all members */
        memset(new_frame, 0, frame_size);
        PINT_sm_push_frame(*smcb, 0, new_frame);
    }
    (*smcb)->op = op;
    (*smcb)->op_get_state_machine = getmach;
    (*smcb)->terminate_fn = term_fn;
    (*smcb)->context = context_id;
    /* if a getmach given, lookup state machine */
    if (getmach)
        return PINT_state_machine_locate(*smcb);
    return 0; /* success */
}

/* Function: PINT_smcb_free
 * Params: pointer to an smcb pointer
 * Returns: nothing, but sets the pointer to NULL
 * Synopsis: this frees an smcb struct, including
 * anything on the frame stack with a zero task_id
 */
void PINT_smcb_free(struct PINT_smcb **smcb)
{
    int i;
    assert(smcb && *smcb);
    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
             "SM free smcb %p op %d\n",*smcb,(*smcb)->op);
    for (i = 0; i < PINT_FRAME_STACK_SIZE; i++)
    {
        if ((*smcb)->frame_stack[i].frame &&
            (*smcb)->frame_stack[i].task_id == 0)
        {
            /* only free if task_id is 0 */
            free((*smcb)->frame_stack[i].frame);
        }
        (*smcb)->frame_stack[i].task_id = 0;
        (*smcb)->frame_stack[i].frame = NULL;
    }
    free(*smcb);
    (*smcb) = (struct PINT_smcb *)0;
}

/* Function: PINT_pop_state
 * Params: pointer to an smcb pointer
 * Returns: 
 * Synopsis: pushes a SM pointer onto a stack for
 *      implementing nested SMs - called by the
 *      "next" routine above
 */
/* should probably be STATIC - WBL */
struct PINT_state_s *PINT_pop_state(struct PINT_smcb *smcb)
{
    assert(smcb->stackptr > 0);

    return smcb->state_stack[--smcb->stackptr];
}

/* Function: PINT_push_state
 * Params: pointer to an smcb pointer
 * Returns: 
 * Synopsis: pops a SM pointer off of a stack for
 *      implementing nested SMs - called by the
 *      "next" routine above
 */
/* should probably be STATIC - WBL */
void PINT_push_state(struct PINT_smcb *smcb,
				   struct PINT_state_s *p)
{
    assert(smcb->stackptr < PINT_STATE_STACK_SIZE);

    smcb->state_stack[smcb->stackptr++] = p;
}

/* Function: PINT_sm_frame
 * Params: pointer to smcb, stack index
 * Returns: pointer to frame
 * Synopsis: returns a frame off of the frame stack
 */
void *PINT_sm_frame(struct PINT_smcb *smcb, int index)
{
    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
            "FRAME smcb %p base %d stack %d\n",
            smcb, smcb->framebaseptr, smcb->framestackptr);
    assert(smcb->framebaseptr + index < smcb->framestackptr);
    
    return smcb->frame_stack[smcb->framebaseptr + index].frame;
}

/* Function: PINT_sm_push_frame
 * Params: pointer to smcb, void pointer for new frame
 * Returns: 
 * Synopsis: pushes a new frame pointer onto the frame_stack
 */
void PINT_sm_push_frame(struct PINT_smcb *smcb, int task_id, void *frame_p)
 {
    int index;
    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
            "PUSH smcb %p base %d stack %d\n",
            smcb, smcb->framebaseptr, smcb->framestackptr);
    assert(smcb->framestackptr < PINT_FRAME_STACK_SIZE);

    index = smcb->framestackptr;
    smcb->framestackptr++;
    smcb->frame_stack[index].task_id = task_id;
    smcb->frame_stack[index].frame = frame_p;
 }

/* Function: PINT_sm_set
 * Params: pointer to smcb
 * Returns: 
 * Synopsis: This moves the framebaseptr up to the stop of the frame_stack
 */
void PINT_sm_set_frame(struct PINT_smcb *smcb)
{
    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
            "SET smcb %p base %d stack %d\n",
            smcb, smcb->framebaseptr, smcb->framestackptr);
    assert(smcb->framestackptr >= 0);

    if (smcb->framestackptr == 0)
        smcb->framebaseptr = 0;
    else
        smcb->framebaseptr = smcb->framestackptr - 1;
}

/* Function: PINT_sm_pop_frame
 * Params: pointer to an smcb pointer
 * Returns: frame pointer
 * Synopsis: pops a frame pointer from the frame_stack and returns it
 */
void *PINT_sm_pop_frame(struct PINT_smcb *smcb)
{
    int index;
    void *frame;
    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
            "POP smcb %p base %d stack %d\n",
            smcb, smcb->framebaseptr, smcb->framestackptr);
    assert(smcb->framestackptr > smcb->framebaseptr);

    smcb->framestackptr--;
    index = smcb->framestackptr;
    frame = smcb->frame_stack[index].frame;
    smcb->frame_stack[index].task_id = 0;
    /* so we won't free on smcb_free */
    smcb->frame_stack[index].frame = NULL;
    return frame;
}

/* Function: PINT_sm_task_map
 * Params: smcb and an integer task_id
 * Returns: The state machine a new child state should execute
 * Synopsis: Uses the task_id and task jump table from the SM
 *      code to decide which SM a new child should run.  Called
 *      by the start_child_frames function
 */
/* should probably be STATIC - WBL */
struct PINT_state_s *PINT_sm_task_map(struct PINT_smcb *smcb, int task_id)
{
    struct PINT_pjmp_tbl_s *pjmptbl;
    int i;

    pjmptbl = smcb->current_state->pjtbl;
    for (i = 0; ; i++)
    { if (pjmptbl[i].return_value == task_id ||
                pjmptbl[i].return_value == -1)
            return pjmptbl[i].state_machine->first_state;
    }
}

/* Function: PINT_sm_start_child_frames
 * Params: pointer to an smcb pointer
 * Returns: number of children started
 * Synopsis: This starts all the enw child SMs based on the frame_stack
 *      This is called by the invoke function above which expects the
 *      number of children to be returned to decide if the state is
 *      deferred or not.
 */
/* should probably be STATIC - WBL */
void PINT_sm_start_child_frames(struct PINT_smcb *smcb)
{
    int i, retval;
    struct PINT_smcb *new_sm;
    job_status_s r;

    assert(smcb);

    /* framebaseptr is the current SM, start at +1 */
    for(i = smcb->framebaseptr + 1; i < smcb->framestackptr; i++)
    {
        /* allocate smcb */
        PINT_smcb_alloc(&new_sm, smcb->op, 0, NULL,
                smcb->terminate_fn, smcb->context);
        /* set parent smcb pointer */
        new_sm->parent_smcb = smcb;
        /* increment parent's counter */
        smcb->children_running++;
        /* assign frame */
        PINT_sm_push_frame(new_sm, smcb->frame_stack[i].task_id,
                smcb->frame_stack[i].frame);
        /* locate SM to run */
        new_sm->current_state = PINT_sm_task_map(smcb,
                smcb->frame_stack[i].task_id);
#if 0
        fprintf(stderr,"child %d task_id %d state %p flag %d func %p\n",
            i, smcb->frame_stack[i].task_id, new_sm->current_state,
            new_sm->current_state->flag, new_sm->current_state->action.func);
#endif
        /* invoke SM */
        retval = PINT_state_machine_start(new_sm, &r);
    }
}

char * PINT_sm_action_string[3] =
{
    "DEFERRED",
    "COMPLETE",
    "TERMINATE"
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif

