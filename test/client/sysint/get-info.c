/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

#include "client.h"
#include "pvfs2-util.h"

int main(int argc,char **argv)
{
    PVFS_sysresp_init resp_init;
    PVFS_sysresp_lookup resp_look;
    PVFS_sysresp_getattr *resp_gattr = NULL;
    PVFS_pinode_reference pinode_refn;
    uint32_t attrmask;
    PVFS_fs_id fs_id;
    char* name;
    PVFS_credentials credentials;
    char *filename = NULL;
    int ret = -1;
    pvfs_mntlist mnt = {0,NULL};
    time_t r_atime, r_mtime, r_ctime;

    if (argc == 2)
    {
        filename = malloc(strlen(argv[1]) + 1);
        memcpy(filename, argv[1], strlen(argv[1]) +1 );
    }
    else
    {
        printf("usage: %s /file_to_get_info_on\n", argv[0]);
        return (-1);
    }

    ret = PVFS_util_parse_pvfstab(&mnt);
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

    credentials.uid = getuid();
    credentials.gid = getgid();
    name = filename;
    fs_id = resp_init.fsid_list[0];
    ret = PVFS_sys_lookup(fs_id, name, credentials,
                          &resp_look, PVFS2_LOOKUP_LINK_NO_FOLLOW);
    if (ret < 0)
    {
        printf("Lookup failed with errcode = %d\n", ret);
        return(-1);
    }

    resp_gattr = (PVFS_sysresp_getattr *)malloc(sizeof(PVFS_sysresp_getattr));
    if (resp_gattr == NULL)
    {
        printf("Error in malloc\n");
        return(-1);
    }
	
    pinode_refn.handle = resp_look.pinode_refn.handle;
    pinode_refn.fs_id = fs_id;
    attrmask = PVFS_ATTR_SYS_ALL;

    ret = PVFS_sys_getattr(pinode_refn, attrmask, credentials, resp_gattr);
    if (ret < 0)
    {
        printf("getattr failed with errcode = %d\n", ret);
        return(-1);
    }

    r_atime = (time_t)resp_gattr->attr.atime;
    r_mtime = (time_t)resp_gattr->attr.mtime;
    r_ctime = (time_t)resp_gattr->attr.ctime;

    printf("Handle      : %Lu\n", Lu(pinode_refn.handle));
    printf("FSID        : %d\n", (int)pinode_refn.fs_id);
    printf("mask        : %d\n", resp_gattr->attr.mask);
    printf("uid         : %d\n", resp_gattr->attr.owner);
    printf("gid         : %d\n", resp_gattr->attr.group);
    printf("permissions : %d\n", resp_gattr->attr.perms);
    printf("atime       : %s", ctime(&r_atime));
    printf("mtime       : %s", ctime(&r_mtime));
    printf("ctime       : %s", ctime(&r_ctime));
    printf("file size   : %Ld\n", Ld(resp_gattr->attr.size));
    printf("handle type : ");

    switch(resp_gattr->attr.objtype)
    {
        case PVFS_TYPE_METAFILE:
            printf("metafile\n");
            break;
        case PVFS_TYPE_DIRECTORY:
            printf("directory\n");
            break;
        case PVFS_TYPE_SYMLINK:
            printf("symlink\n");
            printf("target      : %s\n",
                   resp_gattr->attr.link_target);
            free(resp_gattr->attr.link_target);
            break;
        default:
            printf("unknown object type!\n");
            break;
    }

    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }

    free(resp_gattr);
    free(filename);

    return(0);
}
