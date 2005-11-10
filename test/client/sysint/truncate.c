/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>

#include "client.h"
#include "pvfs2-util.h"

int main(int argc,char **argv)
{
    int ret = -1;
    char *filename = (char *)0;
    PVFS_sysresp_lookup resp_look;
    PVFS_sysresp_lookup resp_lk;
    PVFS_size trunc_size;
    PVFS_fs_id fs_id;
    PVFS_credentials credentials;
    PVFS_object_ref pinode_refn;
    PVFS_size size;

    if (argc != 3)
    {
        printf("usage: %s file_to_truncate total_file_length\n", argv[0]);
        return 1;
    }
    filename = argv[1];

    {
    /* scanf format must match data size on both 32- and 64-bit machines */
    unsigned long long ull;
    sscanf(argv[2],"%lld",&ull);
    trunc_size = ull;
    }

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
	PVFS_perror("PVFS_util_init_defaults", ret);
	return (-1);
    }
    ret = PVFS_util_get_default_fsid(&fs_id);
    if (ret < 0)
    {
	PVFS_perror("PVFS_util_get_default_fsid", ret);
	return (-1);
    }

    /* lookup the root handle */
    printf("looking up the root handle for fsid = %d\n", fs_id);
    ret = PVFS_sys_lookup(fs_id, "/", &credentials,
                          &resp_look, PVFS2_LOOKUP_LINK_FOLLOW);
    if (ret < 0)
    {
	printf("Lookup failed with errcode = %d\n", ret);
	return(-1);
    }

    memset(&resp_lk,0,sizeof(PVFS_sysresp_lookup));

    PVFS_util_gen_credentials(&credentials);
    ret = PVFS_sys_lookup(fs_id, filename, &credentials,
                          &resp_lk, PVFS2_LOOKUP_LINK_FOLLOW);
    if (ret < 0)
    {
	printf("Lookup failed with errcode = %d\n", ret);
	return(-1);
    }

    size = trunc_size;

    pinode_refn.handle = resp_lk.ref.handle;
    pinode_refn.fs_id = resp_lk.ref.fs_id;

    ret = PVFS_sys_truncate(pinode_refn, size, &credentials);
    if (ret < 0)
    {
        printf("truncate failed with errcode = %d\n",ret);
        return(-1);
    }

    printf("===================================\n");
    printf("file named %s has been truncated to %lld bytes.\n",
           filename, lld(trunc_size));

    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }

    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

