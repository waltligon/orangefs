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
#define SM_NONE   0
#define SM_NEXT   1
#define SM_RETURN 2
#define SM_EXTERN 3
#define SM_NESTED 5
#define SM_JUMP   6
#define SM_TERMINATE 7

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif
