/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "client.h"
#include "pvfs2-util.h"
#include "pvfs2-internal.h"
#include "pvfs2-mgmt.h"

int main(int argc,char **argv)
{
    int ret = -1;
    PVFS_fs_id fs_id;
    PVFS_credentials credentials;

    if (argc != 1)
    {
        fprintf(stderr, "USAGE: %s\n", argv[0]);
        return 1;
    }

    PVFS_util_gen_credentials(&credentials);

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

    ret = PVFS_mgmt_setparam_all(fs_id,
        &credentials,
        PVFS_SERV_PARAM_TEST_PRECREATE,
        0,
        NULL,
        NULL);

    if(ret < 0)
    {
        PVFS_perror("PVFS_mgmg_setparam_all(PVFS_SERV_PARAM_TEST_PRECREATE)", ret);
    }

    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }
    return 0;
}
