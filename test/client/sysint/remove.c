/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <client.h>
#include "helper.h"

int main(int argc,char **argv)
{
    int ret = -1;
    char str_buf[256] = {0};
    char *filename = (char *)0;
    PVFS_fs_id cur_fs;
    pvfs_mntlist mnt = {0,NULL};
    PVFS_sysresp_init resp_init;
    PVFS_sysreq_remove req_remove;

    if (argc != 2)
    {
        printf("usage: %s file_to_remove\n", argv[0]);
        return 1;
    }
    filename = argv[1];

    if (parse_pvfstab(NULL,&mnt))
    {
        printf("Failed to parse pvfstab\n");
        return ret;
    }

    memset(&resp_init, 0, sizeof(resp_init));
    if (PVFS_sys_initialize(mnt, &resp_init))
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
    printf("File to be removed is %s\n",str_buf);

    memset(&req_remove,0,sizeof(PVFS_sysreq_remove));

    cur_fs = resp_init.fsid_list[0];

    req_remove.entry_name = str_buf;
    req_remove.parent_refn.handle =
        lookup_parent_handle(filename,cur_fs);
    req_remove.parent_refn.fs_id = cur_fs;
    req_remove.credentials.uid = 100;
    req_remove.credentials.gid = 100;
    req_remove.credentials.perms = 1877;

    ret = PVFS_sys_remove(&req_remove);
    if (ret < 0)
    {
        printf("remove failed with errcode = %d\n",ret);
        return(-1);
    }

    printf("===================================");
    printf("file named %s has been removed.", filename);

    //close it down
    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }
    return(0);
}
