/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <client.h>

#include "pvfs2-util.h"
#include "str-utils.h"

int main(int argc,char **argv)
{
    int ret = -1;
    char str_buf[256] = {0};
    char *filename = (char *)0;
    PVFS_fs_id cur_fs;
    pvfs_mntlist mnt = {0,NULL};
    PVFS_sysresp_init resp_init;
    char* entry_name;
    PVFS_pinode_reference parent_refn;
    PVFS_credentials credentials;

    if (argc != 2)
    {
        printf("usage: %s file_to_remove\n", argv[0]);
        return 1;
    }
    filename = argv[1];

    if (PVFS_util_parse_pvfstab(NULL,&mnt))
    {
        printf("Failed to parse pvfstab\n");
        return ret;
    }

    memset(&resp_init, 0, sizeof(resp_init));
    if (PVFS_sys_initialize(mnt, CLIENT_DEBUG, &resp_init))
    {
        printf("Failed to initialize system interface\n");
        return ret;
    }

    if (PVFS_util_remove_base_dir(filename,str_buf,256))
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

    cur_fs = resp_init.fsid_list[0];

    entry_name = str_buf;
    ret = PVFS_util_lookup_parent(filename, cur_fs, credentials,
	&parent_refn.handle);
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_lookup_parent", ret);
	return(-1);
    }
    parent_refn.fs_id = cur_fs;
    credentials.uid = 100;
    credentials.gid = 100;

    ret = PVFS_sys_remove(entry_name, parent_refn, credentials);
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
