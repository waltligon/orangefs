/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <client.h>
#include <sys/time.h>
#include "helper.h"

int main(int argc,char **argv)
{
    int ret = -1;
    char *dirname = (char *)0;
    char str_buf[256] = {0};
    PVFS_fs_id cur_fs;
    pvfs_mntlist mnt = {0,NULL};
    PVFS_sysresp_init resp_init;
    PVFS_sysreq_mkdir req_mkdir;
    PVFS_sysresp_mkdir resp_mkdir;

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

    memset(&req_mkdir, 0, sizeof(PVFS_sysreq_mkdir));
    memset(&resp_mkdir, 0, sizeof(PVFS_sysresp_mkdir));

    cur_fs = resp_init.fsid_list[0];

    req_mkdir.entry_name = str_buf;
    req_mkdir.parent_refn.handle =
        lookup_parent_handle(dirname,cur_fs);
    req_mkdir.parent_refn.fs_id = cur_fs;
    req_mkdir.attrmask = ATTR_BASIC;
    req_mkdir.attr.owner = 100;
    req_mkdir.attr.group = 100;
    req_mkdir.attr.perms = 1877;
    req_mkdir.attr.objtype = ATTR_DIR;
    req_mkdir.credentials.perms = 1877;
    req_mkdir.credentials.uid = 100;
    req_mkdir.credentials.gid = 100;

    ret = PVFS_sys_mkdir(&req_mkdir,&resp_mkdir);
    if (ret < 0)
    {
        printf("mkdir failed\n");
        return(-1);
    }
    // print the handle 
    printf("--mkdir--\n"); 
    printf("Handle:%Ld\n",resp_mkdir.pinode_refn.handle);
    printf("FSID:%d\n",req_mkdir.parent_refn.fs_id);

    //close it down
    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }
    return(0);
}
