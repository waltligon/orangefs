/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <assert.h>

#include "pvfs2-server.h"
#include "state-machine-fns.h"

extern PINT_state_machine get_config;
extern PINT_state_machine get_attr;
extern PINT_state_machine set_attr;
extern PINT_state_machine create;
extern PINT_state_machine crdirent;
extern PINT_state_machine mkdir_;
extern PINT_state_machine readdir_;
extern PINT_state_machine lookup;
extern PINT_state_machine io;
extern PINT_state_machine remove_;
extern PINT_state_machine rmdirent;

/* table of state machines, indexed based on PVFS_server_op enumeration */
/* NOTE: this table is initialized at run time in PINT_state_machine_init() */
PINT_state_machine *PINT_server_op_table[PVFS_MAX_SERVER_OP+1] = {NULL};

/* 
 * Function: PINT_state_machine_initialize_unexpected(s_op,ret)
 *
 * Params:   PINT_server_op *s_op
 *           job_status_s *ret
 *    
 * Returns:  int
 * 
 * Synopsis: Intialize request structure, first location, and call
 *           respective init function.
 *
 * Initialization:
 * - sets s_op->op, addr, tag
 * - allocates space for s_op->resp and memset()s it to zero
 * - points s_op->req to s_op->decoded.buffer
 */
int PINT_state_machine_initialize_unexpected(PINT_server_op *s_op,
					     job_status_s *ret)
{
    int retval = -1;

    retval = PINT_decode(s_op->unexp_bmi_buff.buffer,
		PINT_ENCODE_REQ,
		&s_op->decoded,
		s_op->unexp_bmi_buff.addr,
		s_op->unexp_bmi_buff.size);
    if(retval < 0)
    {
	return(retval);
    }

    s_op->req  = (struct PVFS_server_req *) s_op->decoded.buffer;
    assert(s_op->req != NULL);

    s_op->addr = s_op->unexp_bmi_buff.addr;
    s_op->tag  = s_op->unexp_bmi_buff.tag;
    s_op->op   = s_op->req->op;
    s_op->current_state = PINT_state_machine_locate(s_op);

    if(!s_op->current_state)
    {
	gossip_err("System not init for function\n");
	return(-1);
    }

    /* allocate and zero memory for (unencoded) response */
    s_op->resp = (struct PVFS_server_resp *)
	malloc(sizeof(struct PVFS_server_resp));

    if (!s_op->resp)
    {
	gossip_err("Out of Memory");
	ret->error_code = 1;
	return(-PVFS_ENOMEM);
    }
    memset(s_op->resp, 0, sizeof(struct PVFS_server_resp));

    s_op->resp->op = s_op->req->op;

    return ((s_op->current_state->state_action))(s_op,ret);
}


/* Function: PINT_state_machine_init(void)
   Params: None
   Returns: True
   Synopsis: This function is used to initialize the state machine 
	 for operation.  Calls each machine's initi function if specified.
 */

int PINT_state_machine_init(void)
{
    int i;

    /* fill in indexes for each supported request type */
    PINT_server_op_table[PVFS_SERV_INVALID] = NULL;
    PINT_server_op_table[PVFS_SERV_CREATE] = &create;
    PINT_server_op_table[PVFS_SERV_REMOVE] = &remove_;
    PINT_server_op_table[PVFS_SERV_IO] = &io;
    PINT_server_op_table[PVFS_SERV_GETATTR] = &get_attr;
    PINT_server_op_table[PVFS_SERV_SETATTR] = &set_attr;
    PINT_server_op_table[PVFS_SERV_LOOKUP_PATH] = &lookup;
    PINT_server_op_table[PVFS_SERV_CREATEDIRENT] = &crdirent;
    PINT_server_op_table[PVFS_SERV_RMDIRENT] = &rmdirent;
    PINT_server_op_table[PVFS_SERV_MKDIR] = &mkdir_;
    PINT_server_op_table[PVFS_SERV_READDIR] = &readdir_;
    PINT_server_op_table[PVFS_SERV_GETCONFIG] = &get_config;

    /* initialize each state machine */
    for (i = 0 ; i <= PVFS_MAX_SERVER_OP; i++)
    {
	if(PINT_server_op_table[i] && PINT_server_op_table[i]->init_fun)
	{
	    (PINT_server_op_table[i]->init_fun)();
	}
    }
    return(0);
}







/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
