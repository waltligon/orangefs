/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <client.h>
#include "helper.h"
#include "pvfs2-types.h"
#include "pvfs2-util.h"

int main(int argc,char **argv)
{
    int ret = -1;
    char old_buf[PVFS_SEGMENT_MAX] = {0};
    char new_buf[PVFS_SEGMENT_MAX] = {0};
    char *old_filename = (char *)0;
    char *new_filename = (char *)0;
    PVFS_fs_id cur_fs;
    pvfs_mntlist mnt = {0,NULL};
    PVFS_sysresp_init resp_init;
    char* old_entry;
    PVFS_pinode_reference old_parent_refn;
    char* new_entry;
    PVFS_pinode_reference new_parent_refn;
    PVFS_credentials credentials;

    if (argc != 3)
    {
        printf("usage: %s old_pathname new_pathname\n", argv[0]);
        return 1;
    }
    old_filename = argv[1];
    new_filename = argv[2];

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

    if (PINT_remove_base_dir(old_filename, old_buf, PVFS_SEGMENT_MAX))
    {
        if (old_filename[0] != '/')
        {
            printf("You forgot the leading '/'\n");
        }
        printf("Cannot retrieve entry name for %s\n",
               old_filename);
        return(-1);
    }
    printf("Old filename is %s\n", old_buf);

    if (PINT_remove_base_dir(new_filename, new_buf, PVFS_SEGMENT_MAX))
    {
        if (new_filename[0] != '/')
        {
            printf("You forgot the leading '/'\n");
        }
        printf("Cannot retrieve name %s\n",
               new_filename);
        return(-1);
    }
    printf("New filename is %s\n",new_buf);


    cur_fs = resp_init.fsid_list[0];

    old_entry = old_buf;
    old_parent_refn.handle =
        lookup_parent_handle(old_filename,cur_fs);
    old_parent_refn.fs_id = cur_fs;
    new_entry = new_buf;
    new_parent_refn.handle =
        lookup_parent_handle(old_filename,cur_fs);
    new_parent_refn.fs_id = cur_fs;
    credentials.uid = 100;
    credentials.gid = 100;

    ret = PVFS_sys_rename(old_entry, old_parent_refn, new_entry, 
			new_parent_refn, credentials);
    if (ret < 0)
    {
        printf("rename failed with errcode = %d\n",ret);
        return(-1);
    }

    printf("===================================");
    printf("file named %s has been renamed to %s.", old_filename,  new_filename);

    //close it down
    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }

    return(0);
}
