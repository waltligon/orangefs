/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <sys/types.h>

#include "pvfs2-sysint.h"
#include "pvfs2-req-proto.h"
#include "pint-sysint-utils.h"
#include "pint-cached-config.h"
#include "acache.h"
#include "PINT-reqproto-encode.h"
#include "trove.h"
#include "server-config-mgr.h"
#include "str-utils.h"
#include "pvfs2-util.h"
#include "client-state-machine.h"

/*
  analogous to 'get_server_config_struct' in pvfs2-server.c -- only an
  fs_id is required since any client may know about different server
  configurations during run-time
*/
struct server_configuration_s *PINT_get_server_config_struct(
    PVFS_fs_id fs_id)
{
    return PINT_server_config_mgr_get_config(fs_id);
}

void PINT_put_server_config_struct(struct server_configuration_s *config)
{
    PINT_server_config_mgr_put_config(config);
}

/* PINT_lookup_parent()
 *
 * given a pathname and an fsid, looks up the handle of the parent
 * directory
 *
 * returns 0 on success, -PVFS_errno on failure
 */
int PINT_lookup_parent(
    char *filename,
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    PVFS_handle * handle)
{
    int ret = -PVFS_EINVAL;
    char buf[PVFS_SEGMENT_MAX] = {0};
    PVFS_sysresp_lookup resp_look;

    memset(&resp_look, 0, sizeof(PVFS_sysresp_lookup));

    if (PINT_get_base_dir(filename, buf, PVFS_SEGMENT_MAX))
    {
        if (filename[0] != '/')
        {
            gossip_err("Invalid dirname (no leading '/')\n");
        }
        gossip_err("cannot get parent directory of %s\n", filename);
        *handle = PVFS_HANDLE_NULL;
        return ret;
    }

    ret = PVFS_sys_lookup(fs_id, buf, credentials,
                          &resp_look, PVFS2_LOOKUP_LINK_FOLLOW, NULL);
    if (ret < 0)
    {
        gossip_err("Lookup failed on %s\n", buf);
        *handle = PVFS_HANDLE_NULL;
        return ret;
    }

    *handle = resp_look.ref.handle;
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
