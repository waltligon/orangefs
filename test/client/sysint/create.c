/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <client.h>
#include "helper.h"

int main(int argc, char **argv)
{
    int ret = -1;
    char str_buf[256] = {0};
    char *filename = (char *)0;
    PVFS_fs_id cur_fs;
    pvfs_mntlist mnt = {0,NULL};
    PVFS_sysresp_init resp_init;
    PVFS_sysreq_create req_create;
    PVFS_sysresp_create resp_create;

    gossip_enable_stderr();
    gossip_set_debug_mask(1,CLIENT_DEBUG);

    if (argc != 2)
    {
        fprintf(stderr,"Usage: %s filename\n",argv[0]);
        return ret;
    }
    filename = argv[1];

    if (parse_pvfstab(NULL,&mnt))
    {
        printf("Failed to parse pvfstab\n");
        return ret;
    }

    memset(&resp_init, 0, sizeof(resp_init));
    if (PVFS_sys_initialize(mnt,&resp_init))
    {
        printf("Failed to initialize system interface\n");
        return ret;
    }

    if (PINT_remove_base_dir(filename,str_buf,256))
    {
        if (filename[0] != '/')
        {
            printf("You forgot the leading '/'\n");
        }
        printf("Cannot retrieve entry name for creation on %s\n",
               filename);
        return(-1);
    }
    printf("File to be created is %s\n",str_buf);

    memset(&req_create, 0, sizeof(PVFS_sysreq_create));
    memset(&resp_create, 0, sizeof(PVFS_sysresp_create));

    cur_fs = resp_init.fsid_list[0];

    req_create.entry_name = str_buf;
    req_create.attrmask = (ATTR_UID | ATTR_GID | ATTR_PERM);
    req_create.attr.owner = 100;
    req_create.attr.group = 100;
    req_create.attr.perms = 1877;
    req_create.credentials.uid = 100;
    req_create.credentials.gid = 100;
    req_create.credentials.perms = 1877;
    req_create.attr.u.meta.nr_datafiles = 4;
    req_create.parent_refn.handle =
        lookup_parent_handle(filename,cur_fs);
    req_create.parent_refn.fs_id = cur_fs;

    /* Fill in the dist -- NULL means the system interface used the 
     * "default_dist" as the default
     */
    req_create.attr.u.meta.dist = NULL;

    ret = PVFS_sys_create(&req_create,&resp_create);
    if (ret < 0)
    {
        printf("create failed with errcode = %d\n", ret);
        return(-1);
    }
	
    // print the handle 
    printf("--create--\n"); 
    printf("Handle: %Ld\n",resp_create.pinode_refn.handle);

    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }

    gossip_disable();

    return(0);
}
