
/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 *
 * built from create.c
 * 	make a file, then flush it.
 */

#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#include "client.h"
#include "pvfs2-util.h"
#include "str-utils.h"
#include "pint-sysint-utils.h"
#include "pvfs2-internal.h"

int main(int argc, char **argv)
{
    int ret = 0;
    PVFS_credentials creds;
    PVFS_sys_attr attr;
    PVFS_Request memreq;
    PVFS_sysresp_create create_resp;
    PVFS_sysresp_io io_resp;
    PVFS_sysresp_lookup lookup_resp;
    PVFS_fs_id curfs;
    char * buff0 = "0000000000";
    char * buff1 = "1111111111";
    char * buff2 = "2222222222";
    char readbuff[10];
    
    ret = PVFS_util_init_defaults();
    if(ret < 0)
    {
        PVFS_perror("PVFS_util_init_defaults", ret);
        return -1;
    }

    ret = PVFS_util_get_default_fsid(&curfs);
    if(ret < 0)
    {
        PVFS_perror("PVFS_util_get_default_fsid", ret);
        return -1;
    }

    PVFS_util_gen_credentials(&creds);

    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
    attr.owner = creds.uid;
    attr.group = creds.gid;
    attr.perms = 1877;
    attr.atime = attr.ctime = attr.mtime = time(NULL);

    ret = PVFS_sys_lookup(curfs, "/", &creds, &lookup_resp, 0);
    if(ret < 0)
    {
        PVFS_perror("lookup failed with error code", ret);
    }

    ret = PVFS_sys_create(
        argv[1], lookup_resp.ref, attr, &creds, NULL, &create_resp);
    if(ret < 0)
    {
        PVFS_perror("create failed with error code", ret);
        return -1;
    }

    PVFS_Request_contiguous(10, PVFS_BYTE, &memreq);

    setenv("PINT_SET_VERSION_TEST", "0", 1);
    ret = PVFS_sys_io(create_resp.ref, PVFS_BYTE, 0, 
                      buff0, memreq, &creds,
                      &io_resp,
                      PVFS_IO_WRITE, PVFS_SYNCH_NONE);
    if(ret < 0)
    {
        PVFS_perror("write failed with error code", ret);
        return -1;
    }

    printf("Wrote (0): %*s\n", 10, buff0);

    setenv("PINT_SET_VERSION_TEST", "2", 1);
    ret = PVFS_sys_io(create_resp.ref, PVFS_BYTE, 0,
                      buff2, memreq, &creds,
                      &io_resp,
                      PVFS_IO_WRITE, PVFS_SYNCH_NONE);
    if(ret < 0)
    {
        PVFS_perror("write failed with error code", ret);
        return -1;
    }

    printf("Wrote (2): %*s\n", 10, buff2);

    setenv("PINT_SET_VERSION_TEST", "1", 1);
    ret = PVFS_sys_io(create_resp.ref, PVFS_BYTE, 0,
                      buff1, memreq, &creds,
                      &io_resp,
                      PVFS_IO_WRITE, PVFS_SYNCH_NONE);
    if(ret < 0)
    {
        PVFS_perror("write failed with error code", ret);
        return -1;
    }

    printf("Wrote (1): %*s\n", 10, buff1);
    
    ret = PVFS_sys_io(create_resp.ref, PVFS_BYTE, 0,
                      readbuff, memreq, &creds,
                      &io_resp,
                      PVFS_IO_READ, PVFS_SYNCH_NONE);
    if(ret < 0)
    {
        PVFS_perror("write failed with error code", ret);
        return -1;
    }

    printf("Read: %*s\n", 10, readbuff);

    PVFS_sys_remove(argv[1], lookup_resp.ref, &creds);

    ret = PVFS_sys_finalize();
    if(ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return ret;
    }
        
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
