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

#include "pvfs2-internal.h"
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
static struct PINT_state_s *PINT_sm_task_map(struct PINT_smcb *smcb,
                                             int task_id);
static void PINT_sm_start_child_frames(struct PINT_smcb *smcb,
                                       int* children_started);

#if defined(__PVFS2_SERVER__)
const char *PINT_map_server_op_to_string(enum PVFS_server_op op);
#endif

/* Function: PINT_state_machine_halt(void)
 * Params: None
 * Returns: True
 * Synopsis: This function is used to shutdown the state machine 
 */
int PINT_state_machine_halt(void)
{
    return 0;
}

/* Function: PINT_state_machine_terminate
 * Params: smcb, job status
 * Returns: 0 on sucess, otherwise error
 * Synopsis: This function cleans up and terminates a SM
 *           in some cases we may need to keep the SM alive until
 *           its children terminate or something - in which case we
 *           will post the relevant job here.
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
                     "%s: (%p) (error_code: %d)\n",
                     __func__, smcb, (int32_t)r->error_code);
         assert(smcb->parent_smcb->children_running > 0);

         my_frame = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
         /* this will loop from TOS down to the base frame */
         /* base frame will not be processed */

         gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
                      "%s: my_frame:%p\n", __func__, my_frame);
#ifdef WIN32
         qlist_for_each_entry(f,
                              &smcb->parent_smcb->frames,
                              link,
                              struct PINT_frame_s)
#else
         qlist_for_each_entry(f, &smcb->parent_smcb->frames, link)
#endif
         {
             if(my_frame == f->frame)
             {
                 f->error = r->error_code;
                 break;
             }
         }

        gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
                     "%s: children_running:%d\n",
                     __func__, smcb->parent_smcb->children_running);

        if (--smcb->parent_smcb->children_running <= 0)
        {
            /* no more child state machines running, so we can
             * start up the parent state machine again
             */
            job_null(0, smcb->parent_smcb, 0, r, &id, smcb->context);
        }
    }

    /* call state machine completion function */
    if (smcb->terminate_fn)
    {
        if (smcb->parent_smcb)
        {
            gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
                         "%s: calling terminate function.\n", __func__);
        }   
        (*smcb->terminate_fn)(smcb, r);
    }
    return 0;
}

/* Function: PINT_state_machine_invoke
 * Params: smcb pointer and job status pointer
 * Returns: return value of state action
 * Synopsis: runs the current state action, produces debugging
 *           output if needed, checls return value and takes action
 *           if needed (sets op_terminate if SM_ACTION_TERMINATED is
 *           returned)
 */
PINT_sm_action PINT_state_machine_invoke(struct PINT_smcb *smcb,
                                         job_status_s *r)
{
    PINT_sm_action retval;
    const char *state_name;
    const char *machine_name;
    int children_started = 0;

    if (!(smcb) ||
        !(smcb->current_state) ||
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
                 smcb, machine_name, state_name,
                 (int32_t)r->status_user_tag);
     
    /* call state action function */
    retval = (smcb->current_state->action.func)(smcb, r);
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
                 smcb, machine_name, state_name,
                 r->error_code, SM_ACTION_STRING(retval));

    if (retval == SM_ACTION_COMPLETE && smcb->current_state->flag == SM_PJMP)
    {
        gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG, "Executing PJMP\n");
        /* start child SMs */
        PINT_sm_start_child_frames(smcb, &children_started);
        /* if any children were started, then we return DEFERRED (even
         * though they may have all completed immediately).  The last child
         * issues a job_null that will drive progress from here and we don't
         * want to cause a double transition.
         */
        if (children_started > 0)
        {
            retval = SM_ACTION_DEFERRED;
        }
        else
        {
            retval = SM_ACTION_COMPLETE;
        }
    }

    return retval;
}

/* Function: PINT_state_machine_start()
 * Params: smcb pointer and job status pointer
 * Returns: return value of last state action
 * Synopsis: Runs the state action pointed to by the
 *           current state, then continues to run the SM
 *           as long as return code is SM_ACTION_COMPLETE.
 *           Asssumes smcb created with smcb_alloc and set to initial condition.
 */

PINT_sm_action PINT_state_machine_start(struct PINT_smcb *smcb, job_status_s *r)
{
    PINT_sm_action ret;

    /* set the state machine to being completed immediately.  We
     * unset this bit once the state machine is deferred.
     */
    smcb->immediate = 1;

    /* under what conditions do we call SM start?  I would assume
     * always after smcb_alloc, which sets the base_frame to 0 since
     * there is only one frame - making this redundant - unless there
     * are undocumented use cases.
     */
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
 * Params: smcb pointer and job status pointer
 * Returns: return value of last state action
 * Synopsis: Runs through a list of return values to find the next function to
 *           call.  Calls that function.  If that function returned COMPLETED
 *           loop and repeat.
 */
PINT_sm_action PINT_state_machine_next(struct PINT_smcb *smcb, job_status_s *r)
{
    int i; /* index for transition table */
    struct PINT_tran_tbl_s *transtbl;
    PINT_sm_action ret;   /* holds state action return code */

    if (!smcb)
    {
        gossip_err("SM next called on invald smcb\n");
        return SM_ACTION_TERMINATE;
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
                {
                    break;
                }
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
        while (smcb->current_state->flag == SM_JUMP ||
               smcb->current_state->flag == SM_SWITCH)
        {
	    PINT_push_state(smcb, smcb->current_state);
            if (smcb->current_state->flag == SM_JUMP)
            {
	        smcb->current_state =
                        smcb->current_state->action.nested->first_state;
            }
            else /* state flag == SM_SWITCH */
            {
                gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG, "SWITCH\n");
                gossip_debug(GOSSIP_STATE_MACHINE_DEBUG, 
                             "======================= NEW REQUEST ======================\n");
                /* All requests should arrive at the server as an
                 * unexpected message which does a PJMP and then
                 * a switch that selects the proper state machine
                 * via PINT_state_machine_locate.  We don't know which
                 * request yet, but SML should provide that.
                 */
                /* locates SM via op, finds first state, follows jumps */
                if (!PINT_state_machine_locate(smcb, 0))
                {
                    return SM_ACTION_TERMINATE;
                }
            }
        }
        /* runs state_action and returns the return code */
        ret = PINT_state_machine_invoke(smcb, r);
    } while (ret == SM_ACTION_COMPLETE || ret == SM_ACTION_TERMINATE);
    return ret;
}

/* Function: PINT_state_machine_continue
 * Params: smcb pointer and job status pointer
 * Returns: return value of last state action
 * Synopsis: This function essentially calls next, and if the state
 *           machine terminates, calls terminate to perform cleanup.
 *           This allows separation from the start call (which calls
 *           next but does not call terminate if the state machine
 *           terminates).
 */
PINT_sm_action PINT_state_machine_continue(struct PINT_smcb *smcb,
                                           job_status_s *r)
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

/* Function: PINT_state_machine_locate(*smcb, dflag)
 * Params:   smcb pointer with op correctly set
 *           and op_get_state_machine set
 *           dflag controls gossip_debug output
 * Returns:  1 on successful locate, 0 on locate failure, <0 on error
 * Synopsis: This function locates the state associated with the op
 *           specified in smcb->op in order to start a state machine's
 *           execution.
 */
int PINT_state_machine_locate(struct PINT_smcb *smcb, int dflag)
{
    struct PINT_state_s *current_tmp;
    struct PINT_state_machine_s *op_sm;
    const char *state_name;
    const char *machine_name;

    gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG, "Starting\n"); 
    /* check for valid inputs */
    if (!smcb || smcb->op < 0 || !smcb->op_get_state_machine)
    {
	gossip_err("State machine requested not valid\n");
	return -PVFS_EINVAL;
    }
#if 0
#if defined(__PVFS2_SERVER__)
    gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG,
                   "Locating op-id: %d (%s)\n", smcb->op,
                   PINT_map_server_op_to_string(smcb->op));
#endif
#if defined(__PVFS2_CLIENT__)
    gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG,
                   "Locating op-id: %d (%s)\n", smcb->op,
                   PINT_client_get_name_str(smcb->op));
#endif
#endif 
    gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG,
                   "calling op_get_state_machine\n"); 
    /* this is a usage dependant routine to look up the SM */
    op_sm = (*smcb->op_get_state_machine)(smcb->op, dflag);

    if (op_sm != NULL)
    {
        /* print result of SM get */
        smcb->current_state = op_sm->first_state;
        machine_name = PINT_state_machine_current_machine_name(smcb);
        gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG,
                       "State Machine: %s\n", machine_name);

	/* handle the case in which the first state points to a nested
	 * machine, rather than a simple function
	 */
	current_tmp = op_sm->first_state;
	while(current_tmp->flag == SM_JUMP)
	{
	    PINT_push_state(smcb, current_tmp);
	    current_tmp = ((struct PINT_state_machine_s *)
                           current_tmp->action.nested)->first_state;
	}
        smcb->current_state = current_tmp;

        /* print resulting state */
        state_name = PINT_state_machine_current_state_name(smcb);
        machine_name = PINT_state_machine_current_machine_name(smcb);

        gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG,
                      "Ready to run: %s:%s\n", machine_name, state_name);

	return 1; /* indicates successful locate */
    }
    gossip_err("State machine not found for operation %d\n", smcb->op);
    return 0; /* indicates failed to locate */
}

/* Function: PINT_smcb_set_op
 * Params: pointer to an smcb pointer, and an op code (int)
 * Returns: nothing
 * Synopsis: sets op on existing smcb and reruns locate if
 *          we have a valid locate func
 */
int PINT_smcb_set_op(struct PINT_smcb *smcb, int op)
{
    if (smcb)
    {
        smcb->op = op;
        return PINT_state_machine_locate(smcb, 0);
    }
    else
    {
        return -PVFS_EINVAL;
    }
}

int PINT_smcb_immediate_completion(struct PINT_smcb *smcb)
{
    if (smcb)
    {
        return smcb->immediate;
    }
    else
    {
        return -PVFS_EINVAL;
    }
}

/* Function: PINT_smcb_op
 * Params: pointer to an smcb pointer
 * Returns: op (int)
 * Synopsis: returns the op currently set in the smcb
 */
int PINT_smcb_op(struct PINT_smcb *smcb)
{
    if (smcb)
    {
        return smcb->op;
    }
    else
    {
        return -PVFS_EINVAL;
    }
}

static int PINT_smcb_sys_op(struct PINT_smcb *smcb)
{
    if (smcb)
    {
        if (smcb->op > 0 && smcb->op < PVFS_OP_SYS_MAXVALID)
        {
            return 1;
        }
        return 0;
    }
    else
    {
        return -PVFS_EINVAL;
    }
}

static int PINT_smcb_mgmt_op(struct PINT_smcb *smcb)
{
    if (smcb)
    {
        if (smcb->op > PVFS_OP_SYS_MAXVAL && smcb->op < PVFS_OP_MGMT_MAXVALID)
        {
            return 1;
        }
        return 0;
    }
    else
    {
        return -PVFS_EINVAL;
    }
}

static int PINT_smcb_misc_op(struct PINT_smcb *smcb)
{
    if (smcb)
    {
        return smcb->op == PVFS_SERVER_GET_CONFIG 
            || smcb->op == PVFS_CLIENT_JOB_TIMER 
            || smcb->op == PVFS_CLIENT_PERF_COUNT_TIMER 
            || smcb->op == PVFS_DEV_UNEXPECTED;
    }
    else
    {
        return -PVFS_EINVAL;
    }
}

int PINT_smcb_invalid_op(struct PINT_smcb *smcb)
{
    if (smcb)
    {
        if (!PINT_smcb_sys_op(smcb) &&
            !PINT_smcb_mgmt_op(smcb) &&
            !PINT_smcb_misc_op(smcb))
        {
            return 1;
        }
        return 0;
    }
    else
    {
        return -PVFS_EINVAL;
    }
}

/* Function: PINT_smcb_set_complete
 * Params: pointer to an smcb pointer
 * Returns: nothing
 * Synopsis: sets op_terminate on existing smcb
 */
void PINT_smcb_set_complete(struct PINT_smcb *smcb)
{
    if (smcb)
    {
        smcb->op_terminate = 1;
    }
}

/* Function: PINT_smcb_complete
 * Params: pointer to an smcb pointer
 * Returns: op (int)
 * Synopsis: returns the op_terminate currently set in the smcb
 */
int PINT_smcb_complete(struct PINT_smcb *smcb)
{
    if (smcb)
    {
        return smcb->op_terminate;
    }
    else
    {
        return -PVFS_EINVAL;
    }
}

/* Function: PINT_smcb_set_cancelled
 * Params: pointer to an smcb pointer
 * Returns: nothing
 * Synopsis: sets op_cancelled on existing smcb
 */
void PINT_smcb_set_cancelled(struct PINT_smcb *smcb)
{
    if (smcb)
    {
        smcb->op_cancelled = 1;
    }
}

/* Function: PINT_smcb_cancelled
 * Params: pointer to an smcb pointer
 * Returns: op (int)
 * Synopsis: returns the op_cancelled currently set in the smcb
 */
int PINT_smcb_cancelled(struct PINT_smcb *smcb)
{
    if (smcb)
    {
        return smcb->op_cancelled;
    }
    else
    {
        return -PVFS_EINVAL;
    }
}

/* Function: PINT_smcb_alloc
 * Params: pointer to an smcb pointer, an op code (int), size of frame
 *          (int), pinter to function to locate SM
 * Returns: nothing, but fills in smcb pointer argument
 * Synopsis: this allocates an smcb struct, including its frame stack
 *           and sets the op code so you can start the state machine
 */
int PINT_smcb_alloc(struct PINT_smcb **smcb,
                    int op,
                    int frame_size,
                    struct PINT_state_machine_s *(*getmach)(int, int),
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
    gossip_ldebug(GOSSIP_STATE_MACHINE_DEBUG, 
                  "New SMCB = (%p)\n", *smcb);

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
        gossip_ldebug(GOSSIP_STATE_MACHINE_DEBUG,
                      "Pushing a new frame (%p)\n", new_frame);
        /* zero out all members */
        memset(new_frame, 0, frame_size);
        PINT_sm_push_frame(*smcb, 0, new_frame);
        (*smcb)->base_frame = 0;
    }
    /* not sure if this should be original req, imbedded op,
     * or something specific for pjmp.  In any case passed
     * in so don't change here.
     */
    (*smcb)->op = op;
    (*smcb)->op_get_state_machine = getmach;
    (*smcb)->terminate_fn = term_fn;
    (*smcb)->context = context_id;
    /* if a getmach given, lookup state machine */
#if 0
    if (getmach)
    {
        return PINT_state_machine_locate(*smcb, 1);
    }
#endif
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

    gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG,"SM smcb free\n");

#ifdef WIN32
    qlist_for_each_entry_safe(frame_entry,
                              tmp,
                              &smcb->frames,
                              link,
                              struct PINT_frame_s,
                              struct PINT_frame_s)
#else
    qlist_for_each_entry_safe(frame_entry, tmp, &smcb->frames, link)
#endif
    {
        if (frame_entry->frame)
        {
           gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG,
                          "frame:%p \ttask-id:%d\n",
                          frame_entry->frame, frame_entry->task_id);
        }
        else
        {
           gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG,
                          "NO FRAME ENTRIES.\n");
        }

        if (frame_entry->frame && frame_entry->task_id == 0)
        {
            /* V3 - are we assured this frame has had any referenced
             * memory freed.  Shouldn't we call a specific free routine
             * on it to make sure and free anything remaining, rather
             * than the generic free?
             */
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
 *           implementing nested SMs - called by the
 *           "next" routine above
 */
static struct PINT_state_s *PINT_pop_state(struct PINT_smcb *smcb)
{
    if (!smcb)
    {
        return NULL;
    }

    if(smcb->stackptr == 0)
    {
        /* this is not an error, we terminate if we return NULL */
        /* this is return from main */
        return NULL;
    }

    smcb->stackptr--;
    smcb->base_frame = smcb->state_stack[smcb->stackptr].prev_base_frame;

    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
                 "[SM pop_state]: smcb:(%p) op-id: %d stk-ptr: %d base-frm: %d frm-cnt: %d\n",
                 smcb, smcb->op, smcb->stackptr, smcb->base_frame, smcb->frame_count);
    
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
    if (!smcb)
    {
        return;
    }

    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
                 "[SM push_state]: smcb:(%p) op-id: %d stk-ptr: %d base-frm: %d frm-cnt: %d\n",
                 smcb, smcb->op, smcb->stackptr, smcb->base_frame, smcb->frame_count);

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

#if 0
    gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
                 "[SM frame get]: (%p) op-id: %d index: %d base-frm: %d\n",
                 smcb, smcb->op, index, smcb->base_frame);
#endif

    if(qlist_empty(&smcb->frames))
    {
        gossip_err("FRAME GET ERROR: (%p) index %d target %d -> List empty\n",
                   smcb, index, target);
        return NULL;
    }
    else
    {
        /* target should be 0 .. frame_count-1 now */
        if (target < 0 || target >= smcb->frame_count)
        {
            gossip_err("FRAME GET ERROR: (%p) index %d target %d -> Out of range\n",
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
        gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
                       "[SM get_frame] smcb:(%p) frame:(%p)\n", smcb, frame_entry->frame);
        gossip_debug(GOSSIP_STATE_MACHINE_DEBUG,
                       "[SM get_frame] op-id: %d stk-ptr: %d base-frm: %d frm-cnt: %d\n",
                       smcb->op, smcb->stackptr, smcb->base_frame, smcb->frame_count);
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
                 "[SM push_frame]: smcb:(%p) frame:(%p) op-id: %d stk-ptr: %d base-frm: %d frm-cnt: %d\n",
                 smcb, frame_p, smcb->op, smcb->stackptr, smcb->base_frame, smcb->frame_count);

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
                 "[SM pop_frame]: smcb:(%p) frame:(%p) op-id: %d stk-ptr: %d base-frm: %d frm-cnt: %d\n",
                 smcb, frame, smcb->op, smcb->stackptr, smcb->base_frame, smcb->frame_count);
    return frame;
}

/* Function: PINT_sm_task_map
 * Params: smcb and an integer task_id
 * Returns: The state machine a new child state should execute
 * Synopsis: Uses the task_id and task jump table from the SM
 *           code to decide which SM a new child should run.  Called
 *           by the start_child_frames function
 */
/* Why do we have a loop without a well defined end?  Bad form!!!
 * I realize we'll have to have some way to pass the size of the
 * pjmptbl.  WBLH
 */
static struct PINT_state_s *PINT_sm_task_map(struct PINT_smcb *smcb,
                                             int task_id)
                                   
{
    struct PINT_pjmp_tbl_s *pjmptbl;
    int i;

    pjmptbl = smcb->current_state->pjtbl;
    gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG,
                   "task_id = %d, pjmptbl = (%p)\n", 
                   task_id, pjmptbl);

    /* loop over number of items in the PJMPTBL
     * This function shuld be called for each new frame
     * (child) in the SM.
     */
    for (i = 0; ; i++)
    {
        gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG,
                       "pjmptbl[%d] = %d, (%p)\n", i, 
                       pjmptbl[i].return_value, pjmptbl[i].state_machine);

        /* -1 is default we don't search further */
        if (pjmptbl[i].return_value == task_id ||
            pjmptbl[i].return_value == -1)
        {
            gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG,
                           "selected pjmptbl[%d]\n", i);
            break;
        }
    }
    if (pjmptbl[i].state_machine)
    {
        return pjmptbl[i].state_machine->first_state;
    }
    else
    {
        return NULL;
    }
}

static int child_sm_frame_terminate(struct PINT_smcb * smcb,
                                    job_status_s * js_p)
{
    gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG, "CHILD TERMINATE\n");

    PINT_smcb_free(smcb);
    return 0;
}

/* Function: PINT_sm_start_child_frames
 * Params: pointer to an smcb pointer and pointer to count of children
 *         started
 * Returns: number of children started
 * Synopsis: This starts all the new child SMs based on the frame_stack
 *           This is called by the invoke function above which expects the
 *           number of children to be returned to decide if the state is
 *           deferred or not.
 */
static void PINT_sm_start_child_frames(struct PINT_smcb *smcb,
                                       int *children_started)
{
    int retval;
    struct PINT_smcb *new_sm;
    job_status_s r;
    struct PINT_frame_s *f;
    void *my_frame;

    assert(smcb);

    gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG, "Starting\n");

    memset(&r, 0, sizeof(job_status_s));

    *children_started = 0;

    my_frame = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
    /* Iterate once up front to determine how many children we are going to
     * run.  This has to be set before starting any children, otherwise if
     * the first one immediately completes it will mistakenly believe it is
     * the last one and signal the parent.
     */
#ifdef WIN32
    qlist_for_each_entry(f, &smcb->frames, link, struct PINT_frame_s)
#else
    qlist_for_each_entry(f, &smcb->frames, link)
#endif
    {
        /* run from TOS until the parent frame */
        if(f->frame == my_frame)
        {
            break;
        }
        /* increment parent's counter */
        smcb->children_running++;
    }
    /* pass back number of children started and 
     * keep this to pass to other funcs
     */
    *children_started = smcb->children_running;

    gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG,
                   "Children Starting = %d\n", smcb->children_running);

#ifdef WIN32
    qlist_for_each_entry(f, &smcb->frames, link, struct PINT_frame_s)
#else
    qlist_for_each_entry(f, &smcb->frames, link)
#endif
    {
        /* run from TOS until the parent frame */
        if(f->frame == my_frame)
        {
            break;
        }
        /* smcb_alloc has been reworked to assume a new request
         * BAD assumption - however it seems this is called
         * in more than one place - need to figure out!
         */
        /* allocate smcb */
        gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG, "Allocating SMCB\n");
        PINT_smcb_alloc(&new_sm,
                        smcb->op, /* set to parent value */
                        0, /* frame size - frames already exist*/
                        smcb->op_get_state_machine, /* set to parent value */
                        child_sm_frame_terminate,
                        smcb->context);

        if (new_sm == NULL)
        {
            /* rewrite this func to return an error code! */
            /* return -PVFS_ENOMEM; */
        }

        /* we select the SM by calling PINT_sm_task_map below */

        gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG, "New SMCB = (%p)\n", new_sm);

        /* set parent smcb pointer */
        new_sm->parent_smcb = smcb;

        /* assign frame */
        PINT_sm_push_frame(new_sm, f->task_id, f->frame);

        gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG,
                       "Push frame (%p) to smcb: (%p) task: %d\n",
                       f->frame, new_sm, f->task_id);

        /* PINT_sm_task_map is static and only called in one place when
         * a PJMP occurs.  When processing a PJMP the state must be
         * set by the PJMPTBL, not the op number in the SMCB.
         */
        /* locate SM to run */
        new_sm->current_state = PINT_sm_task_map(smcb, f->task_id);

        gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG,
                       "new_sm->current_state is %s\n",
                       (new_sm->current_state) ? "VALID" : "INVALID");

        if (new_sm->current_state)
        {
            gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG,
                           "new_sm->current_state->flag is %d\n",
                           new_sm->current_state->flag);
        }

        /* invoke SM */
        gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG,
                       "Calling PINT_state_machine_start (%p)\n", new_sm);
        retval = PINT_state_machine_start(new_sm, &r);
        if(retval < 0)
        {
            gossip_err("PJMP child state machine failed to start.\n");
        }
    }
    gossip_lsdebug(GOSSIP_STATE_MACHINE_DEBUG, "Exiting\n");
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

