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
    PVFS_sysresp_create resp_create;
    char* entry_name;
    PVFS_pinode_reference parent_refn;
    PVFS_sys_attr attr;
    PVFS_credentials credentials;

    if (argc != 2)
    {
        fprintf(stderr,"Usage: %s filename\n",argv[0]);
        return ret;
    }
    filename = argv[1];

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
	PVFS_perror("PVFS_util_init_defaults", ret);
	return (-1);
    }
    ret = PVFS_util_get_default_fsid(&cur_fs);
    if (ret < 0)
    {
	PVFS_perror("PVFS_util_get_default_fsid", ret);
	return (-1);
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

    memset(&resp_create, 0, sizeof(PVFS_sysresp_create));
    PVFS_util_gen_credentials(&credentials);

    entry_name = str_buf;
    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
    attr.owner = credentials.uid;
    attr.group = credentials.gid;
    attr.perms = 1877;
    attr.atime = attr.ctime = attr.mtime = 
	time(NULL);

    ret = PVFS_util_lookup_parent(filename, cur_fs, credentials, 
                                  &parent_refn.handle);
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_lookup_parent", ret);
	return(-1);
    }
    parent_refn.fs_id = cur_fs;

    printf("File to be created is %s under parent %Lu\n",
           str_buf, Lu(parent_refn.handle));

    ret = PVFS_sys_create(entry_name, parent_refn, attr,
                          credentials, &resp_create);
    if (ret < 0)
    {
        PVFS_perror("create failed with errcode", ret);
        return(-1);
    }
	
    // print the handle 
    printf("--create--\n"); 
    printf("Handle: %Ld\n",Ld(resp_create.pinode_refn.handle));

    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }

    return(0);
}
