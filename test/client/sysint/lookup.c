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

void gen_rand_str(int len, char** gen_str);

int main(int argc,char **argv)
{
    int ret = -1;
    int follow_link = PVFS2_LOOKUP_LINK_NO_FOLLOW;
    PVFS_sysresp_init resp_init;
    PVFS_sysresp_lookup resp_look;
    PVFS_sysresp_lookup resp_lk;
    pvfs_mntlist mnt = {0,NULL};
    PVFS_fs_id fs_id;
    PVFS_credentials credentials;
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

    credentials.uid = getuid();
    credentials.gid = getgid();

    ret = PVFS_util_parse_pvfstab(NULL, &mnt);
    if (ret < 0)
    {
        printf("Parsing error\n");
        return(-1);
    }

    ret = PVFS_sys_initialize(mnt, GOSSIP_NO_DEBUG, &resp_init);
    if(ret < 0)
    {
        printf("PVFS_sys_initialize() failure. = %d\n", ret);
        return(ret);
    }

    fs_id = resp_init.fsid_list[0];
    ret = PVFS_sys_lookup(fs_id, "/", credentials,
                          &resp_look, PVFS2_LOOKUP_LINK_NO_FOLLOW);
    if (ret < 0)
    {
        printf("Lookup failed with errcode = %d\n", ret);
        PVFS_perror("PVFS_perror says", ret);
        return(-1);
    }

    printf("--lookup--\n"); 
    printf("ROOT Handle: %Lu\n", Lu(resp_look.pinode_refn.handle));
	
    memset(&resp_lk,0,sizeof(PVFS_sysresp_lookup));

    ret = PVFS_sys_lookup(fs_id, filename, credentials,
                          &resp_lk, follow_link);
    if (ret < 0)
    {
        printf("Lookup failed with errcode = %d\n", ret);
        PVFS_perror("PVFS_perror says", ret);
        return(-1);
    }

    printf("Handle     : %Lu\n", Lu(resp_lk.pinode_refn.handle));
    printf("FS ID      : %d\n", resp_lk.pinode_refn.fs_id);

    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }
    return 0;
}
