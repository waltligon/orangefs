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
    char *filename = (char *)0;
    pvfs_mntlist mnt = {0,NULL};
    PVFS_sysresp_init resp_init;
    PVFS_sysreq_truncate req_truncate;
    PVFS_sysreq_lookup req_look;
    PVFS_sysreq_lookup req_lk;
    PVFS_sysresp_lookup resp_look;
    PVFS_sysresp_lookup resp_lk;
    PVFS_size trunc_size;

    gossip_enable_stderr();
    gossip_set_debug_mask(1,CLIENT_DEBUG);

    if (argc != 3)
    {
        printf("usage: %s file_to_truncate total_file_length\n", argv[0]);
        return 1;
    }
    filename = argv[1];

    sscanf(argv[2],"%lld",&trunc_size);

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

    /* lookup the root handle */
    req_look.credentials.perms = 1877;
    req_look.name = malloc(2);/*null terminator included*/
    req_look.name[0] = '/';
    req_look.name[1] = '\0';
    req_look.fs_id = resp_init.fsid_list[0];
    printf("looking up the root handle for fsid = %d\n", req_look.fs_id);
    ret = PVFS_sys_lookup(&req_look,&resp_look);
    if (ret < 0)
    {
	printf("Lookup failed with errcode = %d\n", ret);
	return(-1);
    }

    /* test the lookup function */
    memset(&req_lk,0,sizeof(PVFS_sysreq_lookup));
    memset(&resp_lk,0,sizeof(PVFS_sysresp_lookup));

    req_lk.name = filename;
    req_lk.fs_id = resp_init.fsid_list[0];
    req_lk.credentials.uid = 100;
    req_lk.credentials.gid = 100;
    req_lk.credentials.perms = U_WRITE|U_READ;

    ret = PVFS_sys_lookup(&req_lk,&resp_lk);
    if (ret < 0)
    {
	printf("Lookup failed with errcode = %d\n", ret);
	return(-1);
    }

    req_truncate.size = trunc_size;

    req_truncate.pinode_refn.handle = resp_lk.pinode_refn.handle;
    req_truncate.pinode_refn.fs_id = resp_lk.pinode_refn.fs_id;
    req_truncate.credentials.uid = 100;
    req_truncate.credentials.gid = 100;
    req_truncate.credentials.perms = 1877;

    ret = PVFS_sys_truncate(&req_truncate);
    if (ret < 0)
    {
        printf("truncate failed with errcode = %d\n",ret);
        return(-1);
    }

    printf("===================================");
    printf("file named %s has been truncated to %lld bytes.", filename, trunc_size);

    //close it down
    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }

    gossip_disable();

    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

