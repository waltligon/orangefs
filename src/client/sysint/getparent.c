/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <string.h>

#include "pvfs2-sysint.h"
#include "str-utils.h"
#include "gossip.h"
#include "pvfs2-util.h"

int PVFS_sys_getparent(
    PVFS_fs_id fs_id,
    char *entry_name,
    PVFS_credentials *credentials,
    PVFS_sysresp_getparent *resp)
{
    int ret = -PVFS_EINVAL;
    char parent_buf[PVFS_NAME_MAX] = {0};
    char file_buf[PVFS_SEGMENT_MAX] = {0};
    PVFS_sysresp_lookup resp_look;

    if ((entry_name == NULL) || (resp == NULL))
    {
        return ret;
    }

    if (PINT_get_base_dir(entry_name,parent_buf,PVFS_NAME_MAX))
    {
        if (entry_name[0] != '/')
        {
            gossip_err("Invalid dirname (no leading '/')\n");
        }
        return ret;
    }

    memset(&resp_look,0,sizeof(PVFS_sysresp_lookup));
    ret = PVFS_sys_lookup(fs_id, parent_buf, credentials,
                          &resp_look, PVFS2_LOOKUP_LINK_NO_FOLLOW);
    if (ret)
    {
        gossip_err("Lookup failed on %s\n",parent_buf);
        return ret;
    }

    if (PVFS_util_remove_base_dir(entry_name,file_buf,PVFS_SEGMENT_MAX))
    {
	gossip_err("invalid filename: %s\n", entry_name);
	return ret;
    }

    strncpy(resp->basename, file_buf, PVFS_SEGMENT_MAX);
    resp->parent_ref = resp_look.ref;

    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
