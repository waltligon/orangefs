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
    int fs_buf_size,
    char *server_buf,
    int server_buf_size)
{
    int ret = -PVFS_EINVAL;
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

    sm_p = (PINT_client_sm *) malloc(sizeof(*sm_p));
    if (sm_p == NULL)
    {
        return -PVFS_ENOMEM;
    }
    memset(sm_p, 0, sizeof(*sm_p));

    sm_p->u.get_config.persist_config_buffers = 1;

    PINT_init_msgarray_params(&sm_p->msgarray_params, *fsid);

    PINT_init_sysint_credentials(sm_p->cred_p, &creds);

    config = PINT_get_server_config_struct(*fsid);

    mntent.the_pvfs_config_server =
        (char*)PINT_cached_config_map_addr(config, *fsid, *addr, &server_type);

    PINT_put_server_config_struct(config);

    cur_fs = PINT_config_find_fs_id(config, *fsid);

    mntent.encoding = cur_fs->encoding;
    mntent.flowproto = cur_fs->flowproto;

    mntent.fs_id = *fsid;

    mntent.pvfs_fs_name = cur_fs->file_system_name;
    sm_p->u.get_config.config = config;

    sm_p->msgpair.enc_type = cur_fs->encoding;

    sm_p->u.get_config.mntent = &mntent;

    sm_p->msgarray_count = 1;
    sm_p->msgarray = &(sm_p->msgpair);

    ret = PINT_client_state_machine_post(sm_p, PVFS_SERVER_GET_CONFIG, &op_id, NULL);

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
    server_buf[server_buf_size-1] = '\0';

    /* The following copies the retrieved configuration buffers
       into the return buffers */
    strncpy(fs_buf, sm_p->u.get_config.fs_config_buf, (fs_buf_size - 1));
    strncpy(server_buf, sm_p->u.get_config.server_config_buf,
            (server_buf_size - 1));

  exit_path:

    if (sm_p && sm_p->u.get_config.persist_config_buffers)
    {
        free(sm_p->u.get_config.fs_config_buf);
        free(sm_p->u.get_config.server_config_buf);
        sm_p->u.get_config.fs_config_buf = NULL;
        sm_p->u.get_config.server_config_buf = NULL;
    }

    PVFS_mgmt_release(op_id);
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
