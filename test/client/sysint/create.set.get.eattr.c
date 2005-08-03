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
#include "pvfs2-debug.h"
#include "str-utils.h"
#include "pint-sysint-utils.h"

int gossip_set_debug_mask(int, uint64_t);

int main(int argc, char **argv)
{
    int ret = -1;
    char str_buf[256] = {0};
    char *filename = (char *)0;
    char *key_s = (char *)0;
    char *val_s = (char *)0;
    PVFS_fs_id cur_fs;
    PVFS_sysresp_create resp_create;
    char* entry_name;
    PVFS_object_ref parent_refn;
    PVFS_sys_attr attr;
    PVFS_credentials credentials;
	 PVFS_ds_keyval key, val;

    if (argc != 4)
    {
        fprintf(stderr,"Usage: %s filename key value \n",argv[0]);
        return ret;
    }
    filename = argv[1];
    key_s = argv[2];
    val_s = argv[3];

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
	     PVFS_perror("PVFS_util_init_defaults errcode", ret);
	     return (-1);
    }
    ret = PVFS_util_get_default_fsid(&cur_fs);
    if (ret < 0)
    {
	     PVFS_perror("PVFS_util_get_default_fsid errcode", ret);
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

    memset(&resp_create, 0, sizeof(PVFS_sysresp_create));
    PVFS_util_gen_credentials(&credentials);

    entry_name = str_buf;
    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
    attr.owner = credentials.uid;
    attr.group = credentials.gid;
    attr.perms = 1877;
    attr.atime = attr.ctime = attr.mtime = time(NULL);

    ret = PINT_lookup_parent(filename, cur_fs, &credentials, 
                             &parent_refn.handle);
    if(ret < 0)
    {
	     PVFS_perror("PVFS_util_lookup_parent errcode", ret);
	     return(-1);
    }
    parent_refn.fs_id = cur_fs;

    printf("File to be created is %s under parent %Lu\n",
           str_buf, Lu(parent_refn.handle));

    ret = PVFS_sys_create(entry_name, parent_refn, attr,
                          &credentials, NULL, &resp_create);
    if (ret < 0)
    {
        PVFS_perror("create failed with errcode", ret);
        return(-1);
    }
	
    // print the handle 
    printf("--create--\n"); 
    printf("Handle: %Ld\n",Ld(resp_create.ref.handle));

	 // set extended attribute
	 printf("--seteattr--\n");
	 key.buffer = key_s;
	 key.buffer_sz = strlen(key_s) + 1;
	 val.buffer = val_s;
	 val.buffer_sz = strlen(val_s) + 1;
	 ret = PVFS_sys_seteattr(resp_create.ref, &credentials, &key, &val, 0);
    if (ret < 0)
    {
        PVFS_perror("seteattr failed with errcode", ret);
        return(-1);
    }

	 //gossip_enable_stderr();
	 //gossip_set_debug_mask(1,0xffffffffffffffffUL);

	 // get extended attribute
	 printf("--geteattr--\n");
	 val.buffer_sz = strlen(val_s) + 10;
	 val.buffer = malloc(val.buffer_sz);
	 ret = PVFS_sys_geteattr(resp_create.ref, &credentials, &key, &val);
    if (ret < 0)
    {
        PVFS_perror("geteattr failed with errcode", ret);
    }

	 // safety valve!
	 ((char *)val.buffer)[val.buffer_sz-1] = 0;

	 // print result if we got any
    printf("Returned %d bytes in value: %s\n", val.read_sz,
		  (char *)val.buffer);

	 if (!strncmp(val.buffer, key_s, val.read_sz) &&
              val.read_sz == strlen(key_s))
			 printf("Success!\n");
	 else
			 printf("Failure!\n");

	 // clean up
	 printf("--finalize--\n");
    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }

    return(0);
}
