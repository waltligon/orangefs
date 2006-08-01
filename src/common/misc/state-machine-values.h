/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __STATE_MACHINE_VALUES_H
#define __STATE_MACHINE_VALUES_H

/* these values are used both in statecomp (to generate the state machine
 * code), and in the state machine processing.
 *
 * they are kept separate from the rest of the code to keep the number of
 * includes for the statecomp code down.
 */
enum PINT_state_code {
    SM_NONE  = 0,
    SM_NEXT  = 1,
    SM_RETURN= 2,
    SM_EXTERN= 3,
    SM_NESTED= 5,
    SM_JUMP  = 6,
    SM_TERM  = 7,
    SM_PJMP  = 8,
    SM_RUN   = 9
};

/* these define things like stack size and so forth for the common
 * state machine code.
 */

#define PINT_STATE_STACK_SIZE                  8
#define PINT_FRAME_STACK_SIZE                  8

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif
