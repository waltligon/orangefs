/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <client.h>
#include <sys/time.h>
#include "helper.h"

/* make uid, gid and perms passed in later */
PVFS_handle lookup_parent_handle(char *filename, PVFS_fs_id fs_id)
{
    char buf[MAX_PVFS_PATH_LEN] = {0};
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_look;

    memset(&resp_look,0,sizeof(PVFS_sysresp_lookup));

    if (PINT_get_base_dir(filename,buf,MAX_PVFS_PATH_LEN))
    {
        if (filename[0] != '/')
        {
            gossip_err("Invalid dirname (no leading '/')\n");
        }
        gossip_err("cannot get parent directory of %s\n",filename);
        return (PVFS_handle)0;
    }

    /* retrieve the parent handle */
    credentials.uid = 100;
    credentials.gid = 100;
    credentials.perms = 1877;

    gossip_debug(CLIENT_DEBUG, "looking up the parent handle of %s for fsid = %d\n",
           buf,fs_id);

    if (PVFS_sys_lookup(fs_id, buf, credentials ,&resp_look))
    {
        gossip_err("Lookup failed on %s\n",buf);
        return (PVFS_handle)0;
    }
    return resp_look.pinode_refn.handle;
}
