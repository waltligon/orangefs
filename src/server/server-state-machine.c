/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <assert.h>

#include "pvfs2-server.h"
#include "state-machine-fns.h"

extern PINT_state_machine pvfs2_get_config_sm;
extern PINT_state_machine pvfs2_get_attr_sm;
extern PINT_state_machine pvfs2_set_attr_sm;
extern PINT_state_machine pvfs2_create_sm;
extern PINT_state_machine pvfs2_crdirent_sm;
extern PINT_state_machine pvfs2_mkdir_sm;
extern PINT_state_machine pvfs2_readdir_sm;
extern PINT_state_machine pvfs2_lookup_sm;
extern PINT_state_machine pvfs2_io_sm;
extern PINT_state_machine pvfs2_remove_sm;
extern PINT_state_machine pvfs2_rmdirent_sm;

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
    PINT_server_op_table[PVFS_SERV_INVALID]      = NULL;
    PINT_server_op_table[PVFS_SERV_CREATE]       = &pvfs2_create_sm;
    PINT_server_op_table[PVFS_SERV_REMOVE]       = &pvfs2_remove_sm;
    PINT_server_op_table[PVFS_SERV_IO]           = &pvfs2_io_sm;
    PINT_server_op_table[PVFS_SERV_GETATTR]      = &pvfs2_get_attr_sm;
    PINT_server_op_table[PVFS_SERV_SETATTR]      = &pvfs2_set_attr_sm;
    PINT_server_op_table[PVFS_SERV_LOOKUP_PATH]  = &pvfs2_lookup_sm;
    PINT_server_op_table[PVFS_SERV_CREATEDIRENT] = &pvfs2_crdirent_sm;
    PINT_server_op_table[PVFS_SERV_RMDIRENT]     = &pvfs2_rmdirent_sm;
    PINT_server_op_table[PVFS_SERV_MKDIR]        = &pvfs2_mkdir_sm;
    PINT_server_op_table[PVFS_SERV_READDIR]      = &pvfs2_readdir_sm;
    PINT_server_op_table[PVFS_SERV_GETCONFIG]    = &pvfs2_get_config_sm;

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
