/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <assert.h>

#include "pvfs2-server.h"
#include "state-machine-fns.h"

extern struct PINT_state_machine_s pvfs2_get_config_sm;
extern struct PINT_state_machine_s pvfs2_get_attr_sm;
extern struct PINT_state_machine_s pvfs2_set_attr_sm;
extern struct PINT_state_machine_s pvfs2_create_sm;
extern struct PINT_state_machine_s pvfs2_crdirent_sm;
extern struct PINT_state_machine_s pvfs2_mkdir_sm;
extern struct PINT_state_machine_s pvfs2_readdir_sm;
extern struct PINT_state_machine_s pvfs2_lookup_sm;
extern struct PINT_state_machine_s pvfs2_io_sm;
extern struct PINT_state_machine_s pvfs2_remove_sm;
extern struct PINT_state_machine_s pvfs2_rmdirent_sm;

/* table of state machines, indexed based on PVFS_server_op enumeration */
/* NOTE: this table is setup at run time in PINT_state_table_initialize() */
struct PINT_state_machine_s *PINT_server_op_table[PVFS_MAX_SERVER_OP+1] = {NULL};

/* PINT_state_machine_start()
 *
 * initializes fields in the s_op structure and begins execution of
 * the appropriate state machine
 *
 * returns 0 on success, -errno on failure
 */
int PINT_state_machine_start(PINT_server_op *s_op, job_status_s *ret)
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

    s_op->resp.op = s_op->req->op;

    return ((s_op->current_state->state_action))(s_op,ret);
}

/* PINT_state_machine_complete()
 *
 * function to be called at the completion of state machine execution;
 * it frees up any resources associated with the state machine that were
 * allocated before the state machine started executing.  Also returns
 * appropriate return value to make the state machine stop transitioning
 *
 * returns 0
 */
int PINT_state_machine_complete(PINT_server_op *s_op)
{
    /* release the decoding of the unexpected request */
    PINT_decode_release(&(s_op->decoded),PINT_DECODE_REQ);

    /* free the buffer that the unexpected request came in on */
    free(s_op->unexp_bmi_buff.buffer);

    /* free the operation structure itself */
    free(s_op);

    return(0);
}

/* PINT_state_table_initialize()
 *
 * sets up a table of state machines that can be located with
 * PINT_state_machine_locate()
 *
 * returns 0 on success, -errno on failure
 */
int PINT_state_table_initialize(void)
{

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
