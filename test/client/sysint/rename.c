/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include "client.h"
#include "pvfs2-types.h"
#include "pvfs2-util.h"
#include "str-utils.h"
#include "pint-sysint-utils.h"

int main(int argc,char **argv)
{
    int ret = -1;
    char old_buf[PVFS_SEGMENT_MAX] = {0};
    char new_buf[PVFS_SEGMENT_MAX] = {0};
    char *old_filename = (char *)0;
    char *new_filename = (char *)0;
    PVFS_fs_id cur_fs;
    char* old_entry;
    PVFS_object_ref old_parent_refn;
    char* new_entry;
    PVFS_object_ref new_parent_refn;
    PVFS_credentials credentials;

    if (argc != 3)
    {
        printf("usage: %s old_pathname new_pathname\n", argv[0]);
        return 1;
    }
    old_filename = argv[1];
    new_filename = argv[2];

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

    PVFS_util_gen_credentials(&credentials);

    old_entry = old_buf;
    ret = PINT_lookup_parent(old_filename, cur_fs, &credentials,
                             &old_parent_refn.handle);
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_lookup_parent", ret);
	return(-1);
    }
    old_parent_refn.fs_id = cur_fs;
    new_entry = new_buf;
    ret = PINT_lookup_parent(new_filename, cur_fs, &credentials,
                             &new_parent_refn.handle);
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_lookup_parent", ret);
	return(-1);
    }
    new_parent_refn.fs_id = cur_fs;

    ret = PVFS_sys_rename(old_entry, old_parent_refn, new_entry, 
			new_parent_refn, &credentials);
    if (ret < 0)
    {
        printf("rename failed with errcode = %d\n",ret);
        return(-1);
    }

    printf("===================================\n");
    printf("file named %s has been renamed to %s\n",
           old_filename,  new_filename);

    //close it down
    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }

    return(0);
}
