/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>
#ifdef WIN32
#include <time.h>
#else
#include <sys/time.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include <unistd.h>
#endif
#include <sys/types.h>

#include "client.h"
#include "pvfs2-util.h"
#include "pvfs2-internal.h"

void gen_rand_str(int len, char** gen_str);

int main(int argc,char **argv)
{
    int ret = -1;
    int follow_link = PVFS2_LOOKUP_LINK_NO_FOLLOW;
    PVFS_sysresp_lookup resp_lk;
    PVFS_fs_id fs_id;
    PVFS_credential credentials;
    char *filename = NULL;

    if (argc != 2)
    {
        if ((argc == 3) && (atoi(argv[2]) == 1))
        {
            follow_link = PVFS2_LOOKUP_LINK_FOLLOW;
            goto lookup_continue;
        }
        printf("USAGE: %s /path/to/lookup [ 1 ]\n", argv[0]);
        printf(" -- if '1' is the last argument, links "
               "will be followed\n");
        return 1;
    }
  lookup_continue:

    filename = argv[1];
    printf("lookup up path %s\n", filename);

    PVFS_util_gen_credential_defaults(&credentials);

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

    memset(&resp_lk,0,sizeof(PVFS_sysresp_lookup));

    ret = PVFS_sys_lookup(fs_id, filename, &credentials,
                          &resp_lk, follow_link, NULL);
    if (ret < 0)
    {
        printf("Lookup failed with errcode = %d\n", ret);
        PVFS_perror("PVFS_perror says", ret);
        return(-1);
    }

    printf("Handle     : %llu\n", llu(resp_lk.ref.handle));
    printf("FS ID      : %d\n", resp_lk.ref.fs_id);

    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }
    return 0;
}
