/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#include "client.h"
#include "pvfs2-util.h"
#include "str-utils.h"
#include "pint-sysint-utils.h"

int main(int argc,char **argv)
{
    int ret = -1;
    char str_buf[256] = {0};
    char *filename = (char *)0;
    PVFS_fs_id cur_fs;
    char* entry_name;
    PVFS_object_ref parent_refn;
    PVFS_credentials credentials;

    if (argc != 2)
    {
        printf("usage: %s file_to_remove\n", argv[0]);
        return 1;
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

    entry_name = str_buf;

    PVFS_util_gen_credentials(&credentials);
    ret = PINT_lookup_parent(filename, cur_fs, &credentials,
                             &parent_refn.handle);
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_lookup_parent", ret);
	return(-1);
    }
    parent_refn.fs_id = cur_fs;

    ret = PVFS_sys_remove(entry_name, parent_refn, &credentials);
    if (ret < 0)
    {
        PVFS_perror("remove failed ", ret);
        return(-1);
    }

    printf("===================================\n");
    printf("file named %s has been removed.\n", filename);

    //close it down
    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }

    return(0);
}
