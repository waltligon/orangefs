/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#include "client.h"
#include "pvfs2-util.h"
#include "str-utils.h"

int main(int argc, char **argv)
{
    int ret = -1;
    char str_buf[256] = {0};
    char *filename = (char *)0;
    PVFS_fs_id cur_fs;
    pvfs_mntlist mnt = {0,NULL};
    PVFS_sysresp_init resp_init;
    PVFS_sysresp_symlink resp_sym;
    char* entry_name = NULL;
    char *target = NULL;
    PVFS_pinode_reference parent_refn;
    PVFS_sys_attr attr;
    PVFS_credentials credentials;

    if (argc != 3)
    {
        fprintf(stderr,"Usage: %s filename target\n",argv[0]);
        return ret;
    }
    filename = argv[1];
    target = argv[2];

    if (PVFS_util_parse_pvfstab(&mnt))
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
        printf("Cannot retrieve link name for creation on %s\n",
               filename);
        return(-1);
    }
    printf("Link to be created is %s\n",str_buf);

    memset(&resp_sym, 0, sizeof(PVFS_sysresp_symlink));

    cur_fs = resp_init.fsid_list[0];

    entry_name = str_buf;
    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
    attr.owner = getuid();
    attr.group = getgid();
    attr.perms = 1877;
    attr.atime = attr.ctime = attr.mtime = time(NULL);
    credentials.uid = attr.owner;
    credentials.gid = attr.group;

    ret = PVFS_util_lookup_parent(filename, cur_fs, credentials, 
                                  &parent_refn.handle);
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_lookup_parent", ret);
	return(-1);
    }
    parent_refn.fs_id = cur_fs;

    ret = PVFS_sys_symlink(entry_name, parent_refn, target,
                           attr, credentials, &resp_sym);
    if (ret < 0)
    {
        printf("symlink failed with errcode = %d\n", ret);
        return(-1);
    }
	
    printf("--symlink--\n"); 
    printf("Handle: %Ld\n", Ld(resp_sym.pinode_refn.handle));

    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }

    return(0);
}
