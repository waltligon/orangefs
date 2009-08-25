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
#include <assert.h>

#include "client.h"
#include "pvfs2-util.h"
#include "pvfs2-internal.h"
#include "pvfs2.h"
#include "str-utils.h"
#include "bmi.h"
#include "security-util.h"

/*
  arbitrarily restrict the number of paths
  that this ls version can take as arguments
*/
#define MAX_NUM_PATHS       8

int main(int argc,char **argv)
{
    PVFS_sysresp_lookup resp_look;
    PVFS_sysresp_getattr *resp_gattr = NULL;
    PVFS_object_ref pinode_refn;
    uint32_t attrmask;
    PVFS_fs_id fs_id;
	PVFS_credential *creds;
    PVFS_credential *cred;
    char *filename = NULL;
    int ret = -1;
    time_t r_atime, r_mtime, r_ctime;
	char pvfs_path[MAX_NUM_PATHS][PVFS_NAME_MAX];
    const PVFS_util_tab* tab;
    int found_one = 0;
    int ncreds;
	int i = 0;
    PVFS_BMI_addr_t addr;

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

    tab = PVFS_util_parse_pvfstab(NULL);
    if (!tab)
    {
        fprintf(stderr, "Error: failed to parse pvfstab.\n");
        return(-1);
    }

    for(i = 0; i < MAX_NUM_PATHS; i++)
    {
        memset(pvfs_path[i],0,PVFS_NAME_MAX);
    }

    ret = PVFS_sys_initialize(GOSSIP_NO_DEBUG);
    if (ret < 0)
    {
	PVFS_perror("PVFS_sys_initialize", ret);
	return(-1);
    }

    /* initialize each file system that we found in the tab file */
    for(i = 0; i < tab->mntent_count; i++)
    {
	ret = PVFS_sys_fs_add(&tab->mntent_array[i]);
	if (ret == 0)
        {
	    found_one = 1;
        }
    }

    if (!found_one)
    {
	fprintf(stderr, "Error: could not initialize any file systems "
                "from %s\n", tab->tabfile_name);
	PVFS_sys_finalize();
	return(-1);
    }

    /* generate a credential for each known file system */

    creds = calloc(tab->mntent_count, sizeof(PVFS_credential));
	cred = calloc(1, sizeof(PVFS_credential));
    if (!creds || !cred)
    {
        perror("calloc");
        PVFS_sys_finalize();
        exit(EXIT_FAILURE);
    }
    ncreds = 0;

    for (i = 0; i < tab->mntent_count; i++)
    {

        ret = BMI_addr_lookup(&addr, 
                              tab->mntent_array[i].the_pvfs_config_server);
        if (ret < 0)
        {
            fprintf(stderr, "Failed to resolve BMI address %s\n",
                    tab->mntent_array[i].the_pvfs_config_server);
			return (ret);
        }

        ret = PVFS_util_gen_credential(tab->mntent_array[i].fs_id,
                                       addr,
                                       NULL,
                                       NULL,
                                       &creds[i]);
        if (ret < 0)
        {
            fprintf(stderr, "Failed to generate credential for fsid %d\n",
                    tab->mntent_array[i].fs_id);
        }
		else
		{
			break;
		}

        ncreds += 1;

    }

	fs_id = tab->mntent_array[i].fs_id;
	cred = &creds[i];

	memset(&resp_look,0,sizeof(PVFS_sysresp_lookup));
    
    ret = PVFS_sys_lookup(fs_id, filename, cred,
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

    ret = PVFS_sys_getattr(pinode_refn, attrmask, cred, resp_gattr, NULL);
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
