/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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

	printf("looking up information for the file %s\n", filename);

	/* Parse PVFStab */
	ret = PVFS_util_parse_pvfstab(&mnt);
	if (ret < 0)
	{
		printf("Parsing error\n");
		return(-1);
	}
	/*Init the system interface*/
	ret = PVFS_sys_initialize(mnt, CLIENT_DEBUG, &resp_init);
	if(ret < 0)
	{
		printf("PVFS_sys_initialize() failure. = %d\n", ret);
		return(ret);
	}

	/* lookup the file we're getattr'ing */
	credentials.uid = 100;
	credentials.gid = 100;
	name = filename;
	fs_id = resp_init.fsid_list[0];
	ret = PVFS_sys_lookup(fs_id, name, credentials,
                              &resp_look, PVFS2_LOOKUP_LINK_NO_FOLLOW);
	if (ret < 0)
	{
		printf("Lookup failed with errcode = %d\n", ret);
		return(-1);
	}

	printf("GETATTR HERE===>\n");
	resp_gattr = (PVFS_sysresp_getattr *)malloc(sizeof(PVFS_sysresp_getattr));
	if (resp_gattr == NULL)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	
	// Fill in the handle 
	pinode_refn.handle = resp_look.pinode_refn.handle;
	pinode_refn.fs_id = fs_id;
        attrmask = PVFS_ATTR_SYS_ALL;

	// Use it 
	ret = PVFS_sys_getattr(pinode_refn, attrmask, credentials, resp_gattr);
	if (ret < 0)
	{
		printf("getattr failed with errcode = %d\n", ret);
		return(-1);
	}

	printf("--getattr--\n"); 
	printf("Handle      : %Lu\n", Lu(pinode_refn.handle));
	printf("FSID        : %d\n", (int)pinode_refn.fs_id);
	printf("mask        : %d\n", resp_gattr->attr.mask);
	printf("uid         : %d\n", resp_gattr->attr.owner);
	printf("gid         : %d\n", resp_gattr->attr.group);
	printf("permissions : %d\n", resp_gattr->attr.perms);
	printf("atime       : %s", ctime((time_t *)&resp_gattr->attr.atime));
	printf("mtime       : %s", ctime((time_t *)&resp_gattr->attr.mtime));
	printf("ctime       : %s", ctime((time_t *)&resp_gattr->attr.ctime));
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
