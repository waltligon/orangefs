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
#include "pvfs2-internal.h"

int main(int argc,char **argv)
{
    PVFS_sysresp_lookup resp_look;
    PVFS_sysresp_getattr *resp_gattr = NULL;
    PVFS_object_ref pinode_refn;
    uint32_t attrmask;
    PVFS_fs_id fs_id;
    char* name;
    PVFS_credentials credentials;
    char *filename = NULL;
    int ret = -1;
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

    name = filename;

    PVFS_util_gen_credentials(&credentials);
    ret = PVFS_sys_lookup(fs_id, name, &credentials,
                          &resp_look, PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);
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
	
    pinode_refn.handle = resp_look.ref.handle;
    pinode_refn.fs_id = fs_id;
    attrmask = PVFS_ATTR_SYS_ALL;

    ret = PVFS_sys_getattr(pinode_refn, attrmask, &credentials, resp_gattr, NULL);
    if (ret < 0)
    {
        printf("getattr failed with errcode = %d\n", ret);
        return(-1);
    }

    r_atime = (time_t)resp_gattr->attr.atime;
    r_mtime = (time_t)resp_gattr->attr.mtime;
    r_ctime = (time_t)resp_gattr->attr.ctime;

    printf("Handle      : %llu\n", llu(pinode_refn.handle));
    printf("FSID        : %d\n", (int)pinode_refn.fs_id);
    printf("mask        : %d\n", resp_gattr->attr.mask);
    printf("uid         : %d\n", resp_gattr->attr.owner);
    printf("gid         : %d\n", resp_gattr->attr.group);
    printf("permissions : %d\n", resp_gattr->attr.perms);
    printf("atime       : %s", ctime(&r_atime));
    printf("mtime       : %s", ctime(&r_mtime));
    printf("ctime       : %s", ctime(&r_ctime));
    printf("file size   : %lld\n", lld(resp_gattr->attr.size));
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
