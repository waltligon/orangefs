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
    PVFS_sysresp_create resp_create;
    char* entry_name;
    PVFS_pinode_reference parent_refn;
    uint32_t attrmask;
    PVFS_object_attr attr;
    PVFS_credentials credentials;

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

    memset(&resp_create, 0, sizeof(PVFS_sysresp_create));

    cur_fs = resp_init.fsid_list[0];

    entry_name = str_buf;
    attrmask = (ATTR_UID | ATTR_GID | ATTR_PERM);
    attr.owner = 100;
    attr.group = 100;
    attr.perms = 1877;
    credentials.uid = 100;
    credentials.gid = 100;
    credentials.perms = 1877;
    attr.u.meta.nr_datafiles = 4;
    parent_refn.handle =
        lookup_parent_handle(filename,cur_fs);
    parent_refn.fs_id = cur_fs;

    /* Fill in the dist -- NULL means the system interface used the 
     * "default_dist" as the default
     */
    attr.u.meta.dist = NULL;

    ret = PVFS_sys_create(entry_name, parent_refn, attrmask, attr,
                credentials, &resp_create);
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
