/*
 * (C) 2003 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_CLIENT_STATE_MACHINE_H
#define __PVFS2_CLIENT_STATE_MACHINE_H

/* NOTE: STATE-MACHINE.H IS INCLUDED AT THE BOTTOM!  THIS IS SO WE CAN
 * DEFINE ALL THE STRUCTURES WE NEED BEFORE WE INCLUDE IT.
 */

#include "pvfs2-storage.h"
#include "job.h"
#include "trove.h"

#define PINT_STATE_STACK_SIZE 3

struct PINT_client_remove_sm {
    char                 *object_name; /* input parameter */
    PVFS_pinode_reference parent_ref;  /* input parameter */
    PVFS_pinode_reference object_ref;  /* discovered during processing */
};

/* HACK!!! */
typedef union PINT_state_array_values PINT_state_array_values;

typedef struct PINT_client_sm_s {
    /* STATE MACHINE VALUES */
    int stackptr; /* stack of contexts for nested state machines */
    PINT_state_array_values *current_state; /* xxx */
    PINT_state_array_values *state_stack[PINT_STATE_STACK_SIZE];

    /* CLIENT SM VALUES */
#if 0
    int op; /* ??? */
#endif
    int comp_ct; /* used to keep up with completion of multiple
		  * jobs for some some states; typically set and
		  * then decremented to zero as jobs complete */

    struct PVFS_server_req req;
    struct PVFS_server_resp *resp_p; /* */
    PVFS_credentials *cred_p;
    union {
	struct PINT_client_remove_sm remove;
    } u;
} PINT_client_sm;

/* INCLUDE STATE-MACHINE.H DOWN HERE */
#define PINT_OP_STATE       PINT_client_sm
#define PINT_OP_STATE_TABLE PINT_server_op_table

#include "state-machine.h"

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif
