/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <string.h>

#include "pvfs2-sysint.h"
#include "str-utils.h"
#include "gossip.h"

int PVFS_sys_getparent(PVFS_fs_id fs_id, char *entry_name, 
	PVFS_credentials credentials, PVFS_sysresp_getparent *resp)
{
    char parent_buf[PVFS_NAME_MAX] = {0};
    char file_buf[PVFS_NAME_MAX] = {0};
    
    PVFS_sysresp_lookup resp_look;

    memset(&resp_look,0,sizeof(PVFS_sysresp_lookup));

    if (PINT_get_base_dir(entry_name,parent_buf,PVFS_NAME_MAX))
    {
        if (entry_name[0] != '/')
        {
            gossip_err("Invalid dirname (no leading '/')\n");
        }
        gossip_err("cannot get parent directory of %s\n",entry_name);
        return -1;
    }

    /* retrieve the parent handle */
    if (PVFS_sys_lookup(fs_id, parent_buf, credentials ,&resp_look)) 
    {
        gossip_err("Lookup failed on %s\n",parent_buf);
        return -1;
    }
    resp->parent_refn = resp_look.pinode_refn;

    if (PINT_remove_base_dir(entry_name,file_buf,PVFS_NAME_MAX))
    {
	gossip_err("invalid filename: %s\n", entry_name);
	return -1;
    }
    strncpy(resp->basename, file_buf, PVFS_NAME_MAX);

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
