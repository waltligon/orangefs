/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <client.h>
#include <sys/time.h>
#include <time.h>
#include "helper.h"

int main(int argc,char **argv)
{
    int ret = -1;
    char *dirname = (char *)0;
    char str_buf[256] = {0};
    PVFS_fs_id cur_fs;
    pvfs_mntlist mnt = {0,NULL};
    PVFS_sysresp_init resp_init;
    PVFS_sysresp_mkdir resp_mkdir;
    char* entry_name;
    PVFS_pinode_reference parent_refn;
    PVFS_sys_attr attr;
    PVFS_credentials credentials;

    gossip_enable_stderr();
    gossip_set_debug_mask(1,CLIENT_DEBUG);

    if (argc != 2)
    {
        fprintf(stderr,"Usage: %s dirname\n",argv[0]);
        return ret;
    }
    dirname = argv[1];

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

    if (PINT_remove_base_dir(dirname,str_buf,256))
    {
        if (dirname[0] != '/')
        {
            printf("You forgot the leading '/'\n");
        }
        printf("Cannot retrieve dir name for creation on %s\n",
               dirname);
        return(-1);
    }
    printf("Directory to be created is %s\n",str_buf);

    memset(&resp_mkdir, 0, sizeof(PVFS_sysresp_mkdir));

    cur_fs = resp_init.fsid_list[0];

    entry_name = str_buf;
    parent_refn.handle =
        lookup_parent_handle(dirname,cur_fs);
    parent_refn.fs_id = cur_fs;
    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
    attr.owner = 100;
    attr.group = 100;
    attr.perms = 1877;
    attr.atime = attr.ctime = attr.mtime =
	time(NULL);
    credentials.perms = 1877;
    credentials.uid = 100;
    credentials.gid = 100;

    ret = PVFS_sys_mkdir(entry_name, parent_refn, attr, 
			credentials, &resp_mkdir);
    if (ret < 0)
    {
        printf("mkdir failed\n");
        return(-1);
    }
    // print the handle 
    printf("--mkdir--\n"); 
    printf("Handle:%Ld\n",resp_mkdir.pinode_refn.handle);
    printf("FSID:%d\n",parent_refn.fs_id);

    //close it down
    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }
    
	 gossip_disable();

    return(0);
}
