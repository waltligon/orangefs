/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __STATE_MACHINE_H
#define __STATE_MACHINE_H

#include "job.h"
#include "pvfs2-server.h"

union PINT_state_array_values
{
    int (*state_action)(struct PINT_server_op *, job_status_s *);
    int return_value;
    int flag;
    void *nested_machine; /* NOTE: this is really a PINT_state_machine */
    union PINT_state_array_values *next_state;
};

typedef struct PINT_state_machine_s
{
    PINT_state_array_values *state_machine;
    char *name;
    void (*init_fun)(void);
} PINT_state_machine;

enum {
    JMP_NOT_READY = 99,
    DEFAULT_ERROR = -1,
};

/* Prototypes */
int PINT_state_machine_init(void);
int PINT_state_machine_halt(void);
int PINT_state_machine_next(PINT_server_op*,job_status_s *r);
PINT_state_array_values *PINT_state_machine_locate(PINT_server_op*);
int PINT_state_machine_initialize_unexpected(PINT_server_op*, job_status_s *ret);
PINT_state_array_values *PINT_pop_state(PINT_server_op *s);
void PINT_push_state(PINT_server_op *s, PINT_state_array_values *p);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif /* __PVFS_SERVER_STATE */
