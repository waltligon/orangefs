/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __STATE_MACHINE_H
#define __STATE_MACHINE_H

#include <state-machine-values.h>
#if 0
#include <client-state-machine.h>
#include <pvfs2-server.h>
#endif

/* STATE-MACHINE.H
 *
 * This file sets up all the definitions necessary for our state machine
 * implementation.  This is set up in this (somewhat obscure) way because
 * we want:
 * - a very fast state machine implementation (few dereferences)
 * - easy access to the state data for the implementation
 * - reuse across client and server (different state data)
 *
 * The important thing to note about this file is that it requires that
 * PINT_OP_STATE be defined.  This must be a typedef to the structure
 * that holds the necessary state information for a given state machine.
 * There are four fields that must exist and are used by the state machine
 * implementation:
 * - int op;
 * - int stackptr;
 * - PINT_state_array_values *current_state;
 * - PINT_state_array_values *state_stack[PINT_STATE_STACK_SIZE];
 *
 * Also, PINT_STATE_STACK_SIZE must be defined or enum'd before that
 * declaration.
 *
 * The file state-machine-fns.h defines a set of functions for use in
 * interacting with the state machine.  There are also a couple of other
 * functions that must be defined, in particular some sort of initialization
 * function.  See src/server/server-state-machine.c for examples.
 */

#include "job.h"

/* op_state - this is the user-specific (client or server) information
 * that must be maintained for each running state machine.
 * This is an opaque type that cannot be dereferenced by the state
 * machine driver code.
 */
typedef void PINT_op_state;

/* State machine control block - one per running instance of a state
 * machine
 */
typedef struct PINT_smcb
{
    /* state machine execution variables */
    int stackptr;
    int framebaseptr;
    int framestackptr;
    union PINT_state_array_values *current_state;
    union PINT_state_array_values *state_stack[PINT_STATE_STACK_SIZE];
    PINT_op_state *frame_stack[PINT_FRAME_STACK_SIZE];
    /* usage specific routinet to look up SM from OP */
    struct PINT_state_machine_s *(*op_get_state_machine)(int);
    /* state machine context and control variables */
    int op; /* this field externally indicates type of state machine */
    PVFS_id_gen_t op_id; /* unique ID for this operation */
    int op_complete; /* indicates operation is complete */
    int op_cancelled; /* indicates operation is cancelled */
    void *user_ptr; /* external user pointer */
} PINT_smcb;

#define PINT_SET_OP_COMPLETE do{__SMCB->op_complete = 1;} while (0)

/* this union defines the possibles values for each word of the
   the state machine memory.  Properly formed state machine programs
   consist of these values in a particular order as documented
   externally
 */
union PINT_state_array_values
{
    /* these added for debugging? */
    const char *state_name;
    struct PINT_state_machine_s *parent_machine;
    /* are they still needed?? */
    int (*state_action)(struct PINT_smcb *, job_status_s *);
    int return_value;
    int flag;
    struct PINT_state_machine_s *nested_machine;
    union PINT_state_array_values *next_state;
};

struct PINT_state_machine_s
{
    const char *name;
    union PINT_state_array_values *state_machine;
};

enum {
    JMP_NOT_READY = 99,
    DEFAULT_ERROR = -1,
};

#define ENCODE_TYPE 0
#define SM_STATE_RETURN -1
#define SM_NESTED_STATE 1

/* Prototypes for functions provided by user */
int PINT_state_machine_start(PINT_op_state *, job_status_s *ret);
int PINT_state_machine_complete(PINT_op_state *);

/* This macro returns the state machine string of the current machine.
 * We assume the first 6 characters of every state machine name are "pvfs2_".
 */
#define PINT_state_machine_current_machine_name(smcb) \
    (((smcb)->current_state - 2)->parent_machine->name + 6)

/* This macro returns the current state invoked */
#define PINT_state_machine_current_state_name(smcb) \
    (((smcb)->current_state - 3)->state_name)

/* Prototypes for functions defined in by state machine code */
int PINT_state_machine_halt(void);
int PINT_state_machine_next(struct PINT_smcb *,job_status_s *);
int PINT_state_machine_invoke(struct PINT_smcb *, job_status_s *);
int PINT_state_machine_locate(struct PINT_smcb *) __attribute__((used));
int PINT_smcb_alloc(struct PINT_smcb **, int, int,
        struct PINT_state_machine_s *(*getmach)(int));
void PINT_smcb_free(struct PINT_smcb **);
union PINT_state_array_values *PINT_pop_state(struct PINT_smcb *);
void PINT_push_state(struct PINT_smcb *, union PINT_state_array_values *);
PINT_op_state *PINT_sm_frame(struct PINT_smcb *, int);

/* This macro is used in calls to PINT_sm_fram() */
#define PINT_FRAME_CURRENT 0

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif
