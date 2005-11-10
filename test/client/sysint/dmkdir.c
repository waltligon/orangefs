/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <client.h>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#include "pvfs2-util.h"
#include "str-utils.h"
#include "pint-sysint-utils.h"

int main(int argc,char **argv)
{
    int ret = -1;
    char *dirname = (char *)0;
    char str_buf[256] = {0};
    char str_buf2[256] = {0};
    char str_buf3[256] = {0};
    PVFS_fs_id cur_fs;
    PVFS_sysresp_mkdir resp_mkdir;
    char* entry_name;
    PVFS_object_ref parent_refn;
    PVFS_sys_attr attr;
    PVFS_credentials credentials;

    if (argc != 2)
    {
        fprintf(stderr,"Usage: %s dirname\n",argv[0]);
        return ret;
    }
    dirname = argv[1];

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

    snprintf(str_buf2, 256, "%s-%d", str_buf, 1);
    snprintf(str_buf3, 256, "%s-%d", str_buf, 2);
    printf("Directories to be created are %s and %s and %s\n",
           str_buf, str_buf2, str_buf3);

    memset(&resp_mkdir, 0, sizeof(PVFS_sysresp_mkdir));
    PVFS_util_gen_credentials(&credentials);

    entry_name = str_buf;
    ret = PINT_lookup_parent(dirname, cur_fs, &credentials, 
                             &parent_refn.handle);
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_lookup_parent", ret);
	return(-1);
    }

    parent_refn.fs_id = cur_fs;
    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
    attr.owner = credentials.uid;
    attr.group = credentials.gid;
    attr.perms = 0777;
    attr.atime = attr.ctime = attr.mtime = time(NULL);

    ret = PVFS_sys_mkdir(entry_name, parent_refn, attr, 
                         &credentials, &resp_mkdir);
    if (ret < 0)
    {
        printf("mkdir failed\n");
        return(-1);
    }
    // print the handle 
    printf("--mkdir--\n"); 
    printf("Handle:%llu\n",llu(resp_mkdir.ref.handle));
    printf("FSID:%d\n",parent_refn.fs_id);

    ret = PVFS_sys_mkdir(str_buf2, resp_mkdir.ref, attr, 
                         &credentials, &resp_mkdir);
    if (ret < 0)
    {
        printf("mkdir failed\n");
        return(-1);
    }
    // print the handle 
    printf("--mkdir--\n"); 
    printf("Handle:%llu\n",llu(resp_mkdir.ref.handle));
    printf("FSID:%d\n",parent_refn.fs_id);

    ret = PVFS_sys_mkdir(str_buf3, resp_mkdir.ref, attr, 
			&credentials, &resp_mkdir);
    if (ret < 0)
    {
        printf("mkdir failed\n");
        return(-1);
    }
    // print the handle 
    printf("--mkdir--\n"); 
    printf("Handle:%llu\n",llu(resp_mkdir.ref.handle));
    printf("FSID:%d\n",parent_refn.fs_id);


    //close it down
    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }
    
    return(0);
}
