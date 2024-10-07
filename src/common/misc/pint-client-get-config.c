/* 
 * (C) 2003 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <assert.h>
#ifndef WIN32
#include <unistd.h>
#endif

#include "client-state-machine.h"
#include "pvfs2-debug.h"
#include "pvfs2-util.h"
#include "job.h"
#include "gossip.h"
#include "str-utils.h"
#include "pint-cached-config.h"
#include "pint-client-get-config.h"
#include "PINT-reqproto-encode.h"
#include "security-util.h"
#include "sid.h"

/* <====================== PUBLIC FUNCTIONS =====================> */

/*
  given mount information, retrieve the server's configuration by
  issuing a getconfig operation.  on successful response, we parse the
  configuration and fill in the config object specified.

  returns 0 on success, -errno on error
*/
int PINT_client_get_config(struct server_configuration_s *config,
                           struct PVFS_sys_mntent* mntent_p,
                           const PVFS_credential *credential,
                           PVFS_hint hints)
{
    int ret = -PVFS_EINVAL;
    PINT_smcb *smcb = NULL;
    PINT_client_sm *sm_p = NULL;
    PVFS_error error = 0;
    PVFS_sys_op_id op_id;

    gossip_debug(GOSSIP_CLIENT_DEBUG,
                 "PINT_server_get_config entered\n");

    if (!config || !mntent_p)
    {
	return ret;
    }

    gossip_debug(GOSSIP_CLIENT_DEBUG, "asked for fs name = %s\n",
                 mntent_p->pvfs_fs_name);

    PINT_smcb_alloc(&smcb,
                    PVFS_SERVER_GET_CONFIG,
                    sizeof(struct PINT_client_sm),
                    client_op_state_get_machine,
                    client_state_machine_terminate,
                    pint_client_sm_context);

    if (smcb == NULL)
    {
        return -PVFS_ENOMEM;
    }

    PINT_state_machine_locate(smcb, 1);

    sm_p = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);

    /* NOTE: we set these fields manually here rather than use
     * PINT_init_msgarray_params(), because we don't yet have a server
     * configuration file to override default parameters.
     */
    sm_p->msgarray_op.params.job_context = pint_client_sm_context;
    sm_p->msgarray_op.params.job_timeout = 30;   /* 30 second job timeout */
    sm_p->msgarray_op.params.retry_delay = 2000; /* 2 second retry delay */
    sm_p->msgarray_op.params.retry_limit = 5;    /* retry up to 5 times */

    PINT_msgpair_init(&sm_p->msgarray_op);
    PINT_init_sysint_credential(sm_p->cred_p, credential);
    sm_p->u.get_config.mntent = mntent_p;
    sm_p->u.get_config.config = config;
    
    PVFS_hint_copy(hints, &sm_p->hints);

    ret = PINT_client_state_machine_post(smcb, &op_id, NULL);
    if (ret)
    {
        PVFS_perror_gossip("PINT_client_state_machine_post call", ret);
        error = ret;
    }
    else
    {
        ret = PVFS_sys_wait(op_id, "X-get_config", &error);
        if (ret)
        {
            PVFS_perror_gossip("PVFS_sys_wait call", ret);
            error = ret;
        }
    }

    PINT_sys_release(op_id);
    return(error);
}

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */
