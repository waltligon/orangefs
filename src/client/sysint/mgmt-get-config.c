/* 
 * (C) 2003 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "client-state-machine.h"
#include "pvfs2-debug.h"
#include "pvfs2-util.h"
#include "job.h"
#include "gossip.h"
#include "str-utils.h"
#include "pint-cached-config.h"
#include "PINT-reqproto-encode.h"

extern job_context_id pint_client_sm_context;

/*
  given mount information, retrieve the server's configuration by
  issuing a getconfig operation.  on successful response, we parse the
  configuration and fill in the config object specified.

  returns 0 on success, -errno on error
*/
int PVFS_mgmt_get_config(
    const PVFS_fs_id * fsid,
    PVFS_BMI_addr_t * addr,
    char *fs_buf,
    int fs_buf_size)
{
    int ret = -PVFS_EINVAL;
    PINT_smcb *smcb = NULL;
    PINT_client_sm *sm_p = NULL;
    PVFS_error error = 0;
    PVFS_credentials creds;
    struct filesystem_configuration_s *cur_fs = NULL;
    PVFS_sys_op_id op_id;
    struct server_configuration_s *config = NULL;
    struct PVFS_sys_mntent mntent;
    int server_type = 0;

    gossip_debug(GOSSIP_CLIENT_DEBUG, "PVFS_mgmt_get_config entered\n");

    PVFS_util_gen_credentials(&creds);

    PINT_smcb_alloc(&smcb, PVFS_SERVER_GET_CONFIG,
                    sizeof(struct PINT_client_sm),
                    client_op_state_get_machine,
                    client_state_machine_terminate,
                    pint_client_sm_context);
    if(smcb == NULL)
    {
        return -PVFS_ENOMEM;
    }

    sm_p = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);

    sm_p->u.get_config.persist_config_buffers = 1;

    PINT_init_msgarray_params(sm_p, *fsid);

    PINT_init_sysint_credentials(sm_p->cred_p, &creds);

    config = PINT_get_server_config_struct(*fsid);

    mntent.the_pvfs_config_server =
        (char*)PINT_cached_config_map_addr(*fsid, *addr, &server_type);

    PINT_put_server_config_struct(config);

    cur_fs = PINT_config_find_fs_id(config, *fsid);

    mntent.encoding = cur_fs->encoding;
    mntent.flowproto = cur_fs->flowproto;

    mntent.fs_id = *fsid;

    mntent.pvfs_fs_name = cur_fs->file_system_name;
    sm_p->u.get_config.config = config;

    sm_p->msgarray_op.msgpair.enc_type = cur_fs->encoding;

    sm_p->u.get_config.mntent = &mntent;

    PINT_msgpair_init(&sm_p->msgarray_op);

    ret = PINT_client_state_machine_post(
        smcb, &op_id, NULL);

    if (ret)
    {
        PVFS_perror_gossip("PINT_client_state_machine_post call", ret);
        error = ret;
    }
    else
    {
        ret = PVFS_mgmt_wait(op_id, "X-get_config", &error);
        if (ret)
        {
            PVFS_perror_gossip("PVFS_mgmt_wait call", ret);
            error = ret;
        }
    }

    if (error)
    {
        goto exit_path;
    }

    gossip_debug(GOSSIP_CLIENT_DEBUG, "PVFS_mgmt_get_config completed\n");

    /* make sure strings will be null terminated after strncpy */
    fs_buf[fs_buf_size-1] = '\0';

    /* The following copies the retrieved configuration buffers
       into the return buffers */
    strncpy(fs_buf, sm_p->u.get_config.fs_config_buf, (fs_buf_size - 1));

  exit_path:

    if (sm_p && sm_p->u.get_config.persist_config_buffers)
    {
        free(sm_p->u.get_config.fs_config_buf);
        sm_p->u.get_config.fs_config_buf = NULL;
    }

    PINT_mgmt_release(op_id);
    return error;
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
