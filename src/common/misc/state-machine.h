/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __STATE_MACHINE_H
#define __STATE_MACHINE_H

#include <state-machine-values.h>

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

#ifndef PINT_OP_STATE
#error "PINT_OP_STATE must be defined before state-machine.h is included."
#endif

union PINT_state_array_values
{
    int (*state_action)(struct PINT_OP_STATE *, job_status_s *);
    int return_value;
    int flag;
    struct PINT_state_machine_s *nested_machine; /* NOTE: this is really a PINT_state_machine * (void *)*/
    union PINT_state_array_values *next_state;
};

struct PINT_state_machine_s
{
    union PINT_state_array_values *state_machine;
    char *name;
    void (*init_fun)(void);
};

enum {
    JMP_NOT_READY = 99,
    DEFAULT_ERROR = -1,
};

#define ENCODE_TYPE 0
#define SM_STATE_RETURN -1
#define SM_NESTED_STATE 1

/* Prototypes for functions provided by user */
int PINT_state_machine_initialize_unexpected(struct PINT_OP_STATE *, job_status_s *ret);
int PINT_state_machine_completion(struct PINT_OP_STATE *);
int PINT_state_machine_init(void);

/* NOTE: All other function prototypes are defined in state-machine-fns.h */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif
