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

struct PINT_frame_s
{
    int task_id;
    void *frame;
    int error;
    struct qlist_head link;
};

static struct PINT_state_s *PINT_pop_state(struct PINT_smcb *);
static void PINT_push_state(struct PINT_smcb *, struct PINT_state_s *);
static struct PINT_state_s *PINT_sm_task_map(struct PINT_smcb *smcb, int task_id);
static void PINT_sm_start_child_frames(struct PINT_smcb *smcb, int* children_started);

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
    struct PINT_frame_s *f;
    void *my_frame;
    job_id_t id;

    /* notify parent */
    if (smcb->parent_smcb)
    {
        gossip_debug(GOSSIP_STATE_MACHINE_DEBUG, 
                     "[SM Terminating Child]: (%p) (error_code: %d)\n",
                     smcb,
                     /* skip pvfs2_ */
                     (int32_t)r->error_code);
         assert(smcb->parent_smcb->children_running > 0);

         my_frame = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
         /* this will loop from TOS down to the base frame */
         /* base frame will not be processed */
         qlist_for_each_entry(f, &smcb->parent_smcb->frames, link)
         {
             if(my_frame == f->frame)
             {
                 f->error = r->error_code;
                 break;
             }
         }

        if (--smcb->parent_smcb->children_running <= 0)
        {
            /* no more child state machines running, so we can
             * start up the parent state machine again
             */
            job_null(0, smcb->parent_smcb, 0, r, &id, smcb->context);
        }
        return SM_ACTION_DEFERRED;
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
    int children_started = 0;

    if (!(smcb) || !(smcb->current_state) ||
            !(smcb->current_state->flag == SM_RUN ||
              smcb->current_state->flag == SM_PJMP) ||
            !(smcb->current_state->action.func))
    {
        gossip_err("SM invoke called on invalid smcb or state\n");
        return SM_ERROR;
    }

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
            smcb->op_terminate = 1;
            break;
    case SM_ACTION_COMPLETE :
    case SM_ACTION_DEFERRED :
            break;
    default :
            /* error */
            gossip_err("SM Action %s:%s returned invalid return code %d (%p)\n",
                       machine_name, state_name, retval, smcb);
            break;
    }

    /* print post-call debugging info */
    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG, 
                 "[SM Exiting]: (%p) %s:%s (error code: %d), (action: %s)\n",
                 smcb,
                 /* skip pvfs2_ */
                 machine_name,
                 state_name,
                 r->error_code,
                 SM_ACTION_STRING(retval));

    if (retval == SM_ACTION_COMPLETE && smcb->current_state->flag == SM_PJMP)
    {
        /* start child SMs */
        PINT_sm_start_child_frames(smcb, &children_started);
        /* if any children were started, then we return DEFERRED (even
         * though they may have all completed immediately).  The last child
         * issues a job_null that will drive progress from here and we don't
         * want to cause a double transition.
         */
        if (children_started > 0)
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

    /* set the state machine to being completed immediately.  We
     * unset this bit once the state machine is deferred.
     */
    smcb->immediate = 1;

    /* set the base frame to be the current TOS, which should be 0 */
    smcb->base_frame = smcb->frame_count - 1;

    /* run the current state action function */
    ret = PINT_state_machine_invoke(smcb, r);
    if (ret == SM_ACTION_COMPLETE || ret == SM_ACTION_TERMINATE)
    {
        /* keep running until state machine deferrs or terminates */
        ret = PINT_state_machine_continue(smcb, r);
    }

    if(ret == SM_ACTION_DEFERRED)
    {
        /* this state machine isn't completing immediately */
        smcb->immediate = 0;
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
    if(PINT_smcb_cancelled(smcb))
    {
        return SM_ACTION_TERMINATE;
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
                if(!smcb->current_state ||
                   smcb->current_state->trtbl[0].flag == SM_TERM)
                {
                    /* assume nested state machine was invoked without
                     * a parent */
                    return SM_ACTION_TERMINATE;
                }
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
    const char *state_name;
    const char *machine_name;

    /* check for valid inputs */
    if (!smcb || smcb->op < 0 || !smcb->op_get_state_machine)
    {
	gossip_err("State machine requested not valid\n");
	return -PVFS_EINVAL;
    }
    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
            "[SM Locating]: (%p) op-id: %d\n",smcb,(smcb)->op);
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

        state_name = PINT_state_machine_current_state_name(smcb);
        machine_name = PINT_state_machine_current_machine_name(smcb);

        gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
                     "[SM Locating]: (%p) located: %s:%s\n",
                     smcb, machine_name, state_name);

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
    smcb->op = op;
    return PINT_state_machine_locate(smcb);
}

int PINT_smcb_immediate_completion(struct PINT_smcb *smcb)
{
    return smcb->immediate;
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
    /* zero out all members */
    memset(*smcb, 0, sizeof(struct PINT_smcb));

    INIT_QLIST_HEAD(&(*smcb)->frames);
    (*smcb)->base_frame = -1; /* no frames yet */
    (*smcb)->frame_count = 0;

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
        (*smcb)->base_frame = 0;
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
void PINT_smcb_free(struct PINT_smcb *smcb)
{
    struct PINT_frame_s *frame_entry, *tmp;
    assert(smcb);
    qlist_for_each_entry_safe(frame_entry, tmp, &smcb->frames, link)
    {
        if (frame_entry->frame && frame_entry->task_id == 0)
        {
            /* only free if task_id is 0 */
            free(frame_entry->frame);
        }
        qlist_del(&frame_entry->link);
        free(frame_entry);
    }
    free(smcb);
}

/* Function: PINT_pop_state
 * Params: pointer to an smcb pointer
 * Returns: 
 * Synopsis: pops a SM pointer off of a stack for
 *      implementing nested SMs - called by the
 *      "next" routine above
 */
static struct PINT_state_s *PINT_pop_state(struct PINT_smcb *smcb)
{
    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
            "[SM pop_state]: (%p) op-id: %d stk-ptr: %d base-frm: %d\n",
            smcb, smcb->op, smcb->stackptr, smcb->base_frame);
    
    if(smcb->stackptr == 0)
    {
        /* this is not an error, we terminate if we return NULL */
        /* this is return from main */
        return NULL;
    }

    smcb->stackptr--;
    smcb->base_frame = smcb->state_stack[smcb->stackptr].prev_base_frame;
    return smcb->state_stack[smcb->stackptr].state;
}

/* Function: PINT_push_state
 * Params: pointer to an smcb pointer
 * Returns: 
 * Synopsis: pushes a SM pointer into a stack for
 *      implementing nested SMs - called by the
 *      "next" routine above
 */
static void PINT_push_state(struct PINT_smcb *smcb,
                            struct PINT_state_s *p)
{
    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
            "[SM push_state]: (%p) op-id: %d stk-ptr: %d base-frm: %d\n",
            smcb, smcb->op, smcb->stackptr, smcb->base_frame);

    assert(smcb->stackptr < PINT_STATE_STACK_SIZE);

    smcb->state_stack[smcb->stackptr].prev_base_frame = smcb->base_frame;
    smcb->base_frame = smcb->frame_count - 1;
    smcb->state_stack[smcb->stackptr].state = p;
    smcb->stackptr++;
}

/* Function: PINT_sm_frame
 * Params: pointer to smcb, stack index
 * Returns: pointer to frame
 * Synopsis: returns a frame off of the frame stack
 * An index of 0 indicates the base frame specified in the SMCB
 * A +'ve index indicates a frame pushed by this SM
 * A -'ve index indicates a frame from a prior SM
 * smcb->frames.next is the top of stack
 * smcb->frames.prev is the bottom of stack
 */
void *PINT_sm_frame(struct PINT_smcb *smcb, int index)
{
    struct PINT_frame_s *frame_entry;
    struct qlist_head *prev;
    int target = smcb->base_frame + index;

    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
            "[SM frame get]: (%p) op-id: %d index: %d base-frm: %d\n",
            smcb, smcb->op, index, smcb->base_frame);

    if(qlist_empty(&smcb->frames))
    {
        gossip_err("FRAME GET smcb %p index %d target %d -> List empty\n",
                     smcb, index, target);
        return NULL;
    }
    else
    {
        /* target should be 0 .. frame_count-1 now */
        if (target < 0 || target >= smcb->frame_count)
        {
            gossip_err("FRAME GET smcb %p index %d target %d -> Out of range\n",
                     smcb, index, target);
            return NULL;
        }
        prev = smcb->frames.prev;
        while(target)
        {
            target--;
            prev = prev->prev;
        }
        frame_entry = qlist_entry(prev, struct PINT_frame_s, link);
        return frame_entry->frame;
    }
}

/* Function: PINT_sm_push_frame
 * Params: pointer to smcb, void pointer for new frame
 * Returns: 
 * Synopsis: pushes a new frame pointer onto the frame_stack
 */
int PINT_sm_push_frame(struct PINT_smcb *smcb, int task_id, void *frame_p)
{
    struct PINT_frame_s *newframe;
    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
                 "[SM Frame PUSH]: (%p) frame: %p\n",
                 smcb, frame_p);
    newframe = malloc(sizeof(struct PINT_frame_s));
    if(!newframe)
    {
        return -PVFS_ENOMEM;
    }
    newframe->task_id = task_id;
    newframe->frame = frame_p;
    newframe->error = 0;
    qlist_add(&newframe->link, &smcb->frames);
    smcb->frame_count++;
    return 0;
}

/* Function: PINT_sm_pop_frame
 * Params: smcb - pointer to an smcb pointer
 *         task_id - the task id of this frame
 *         error_code - the frame's error if there was one.
 *         remaining - count of remaining frames on the smcb.
 * Returns: frame pointer
 * Synopsis: pops a frame pointer from the frame_stack and returns it
 */
void *PINT_sm_pop_frame(struct PINT_smcb *smcb, 
                        int *task_id,
                        int *error_code,
                        int *remaining)
{
    struct PINT_frame_s *frame_entry;
    void *frame;

    if(qlist_empty(&smcb->frames))
    {
        return NULL;
    }

    frame_entry = qlist_entry(smcb->frames.next, struct PINT_frame_s, link);
    qlist_del(smcb->frames.next);
    smcb->frame_count--;

    if(remaining)
    {
        *remaining = smcb->frame_count;
    }

    frame = frame_entry->frame;
    *error_code = frame_entry->error;
    *task_id = frame_entry->task_id;

    free(frame_entry);

    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
            "[SM Frame POP]: (%p) frame: %p\n",
            smcb, frame);
    return frame;
}

/* Function: PINT_sm_task_map
 * Params: smcb and an integer task_id
 * Returns: The state machine a new child state should execute
 * Synopsis: Uses the task_id and task jump table from the SM
 *      code to decide which SM a new child should run.  Called
 *      by the start_child_frames function
 */
static struct PINT_state_s *PINT_sm_task_map(struct PINT_smcb *smcb, int task_id)
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

static int child_sm_frame_terminate(struct PINT_smcb * smcb, job_status_s * js_p)
{
    PINT_smcb_free(smcb);
    return 0;
}

/* Function: PINT_sm_start_child_frames
 * Params: pointer to an smcb pointer and pointer to count of children
 *      started
 * Returns: number of children started
 * Synopsis: This starts all the enw child SMs based on the frame_stack
 *      This is called by the invoke function above which expects the
 *      number of children to be returned to decide if the state is
 *      deferred or not.
 */
static void PINT_sm_start_child_frames(struct PINT_smcb *smcb, int* children_started)
{
    int retval;
    struct PINT_smcb *new_sm;
    job_status_s r;
    struct PINT_frame_s *f;
    void *my_frame;

    assert(smcb);

    memset(&r, 0, sizeof(job_status_s));

    *children_started = 0;

    my_frame = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
    /* Iterate once up front to determine how many children we are going to
     * run.  This has to be set before starting any children, otherwise if
     * the first one immediately completes it will mistakenly believe it is
     * the last one and signal the parent.
     */
    qlist_for_each_entry(f, &smcb->frames, link)
    {
        /* run from TOS until the parent frame */
        if(f->frame == my_frame)
        {
            break;
        }
        /* increment parent's counter */
        smcb->children_running++;
    }

    /* let the caller know how many children are being started; it won't be
     * able to tell from the running_count because they may all immediately
     * complete before we leave this function.
     */
    *children_started = smcb->children_running;

    qlist_for_each_entry(f, &smcb->frames, link)
    {
        /* run from TOS until the parent frame */
        if(f->frame == my_frame)
        {
            break;
        }
        /* allocate smcb */
        PINT_smcb_alloc(&new_sm, smcb->op, 0, NULL,
                child_sm_frame_terminate, smcb->context);
        /* set parent smcb pointer */
        new_sm->parent_smcb = smcb;
        /* assign frame */
        PINT_sm_push_frame(new_sm, f->task_id, f->frame);
        /* locate SM to run */
        new_sm->current_state = PINT_sm_task_map(smcb, f->task_id);
        /* invoke SM */
        retval = PINT_state_machine_start(new_sm, &r);
        if(retval < 0)
        {
            gossip_err("PJMP child state machine failed to start.\n");
        }
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

