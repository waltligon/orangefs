/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <sys/time.h>

#include "client.h"
#include "gossip.h"

extern int parse_pvfstab(char *fn,pvfs_mntlist *mnt);

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

	gossip_enable_stderr();
	gossip_set_debug_mask(1,CLIENT_DEBUG);

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
	ret = parse_pvfstab(NULL,&mnt);
	if (ret < 0)
	{
		printf("Parsing error\n");
		return(-1);
	}
	/*Init the system interface*/
	ret = PVFS_sys_initialize(mnt, &resp_init);
	if(ret < 0)
	{
		printf("PVFS_sys_initialize() failure. = %d\n", ret);
		return(ret);
	}
	printf("SYSTEM INTERFACE INITIALIZED\n");

	/* lookup the file we're getattr'ing */
	credentials.uid = 100;
	credentials.gid = 100;
	credentials.perms = 1877;
	name = filename;
	fs_id = resp_init.fsid_list[0];
	ret = PVFS_sys_lookup(fs_id, name, credentials, &resp_look);
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
	attrmask = PVFS_ATTR_SYS_ALL_NOSIZE;

	// Use it 
	ret = PVFS_sys_getattr(pinode_refn, attrmask, credentials, resp_gattr);
	if (ret < 0)
	{
		printf("getattr failed with errcode = %d\n", ret);
		return(-1);
	}
	// print the handle 
	printf("--getattr--\n"); 
	printf("Handle:%ld\n",(long int)pinode_refn.handle);
	printf("FSID:%ld\n",(long int)pinode_refn.fs_id);
	printf("mask:%d\n",attrmask);
	printf("uid:%d\n",resp_gattr->attr.owner);
	printf("gid:%d\n",resp_gattr->attr.group);
	printf("permissions:%d\n",resp_gattr->attr.perms);
	printf("atime:%d\n",(int)resp_gattr->attr.atime);
	printf("mtime:%d\n",(int)resp_gattr->attr.mtime);
	printf("ctime:%d\n",(int)resp_gattr->attr.ctime);
	switch(resp_gattr->attr.objtype)
	{
		case PVFS_TYPE_METAFILE:
		printf("METAFILE\n");
#if 0
		/* ifdef out; we don't let this kind of information out of
		 * the system interface!
		 */
		printf("nr_datafiles:%d\n",resp_gattr->attr.u.meta.nr_datafiles);

		for(i=0; i < resp_gattr->attr.u.meta.nr_datafiles; i++)
		{
			printf("\thandle: %d\n", (int)resp_gattr->attr.u.meta.dfh[i]);
		}
#endif
		break;

		case PVFS_TYPE_DIRECTORY:
		printf("DIRECTORY\n");
#if 0
		/* ifdef out; we don't let this kind of information out of
		 * the system interface!
		 */
		printf("handle: = %d", (int)resp_gattr->attr.u.dir.dfh);
#endif
		break;

		default:
		printf("UNKNOWN object type!\n");
		break;
	}
	

	//close it down
	ret = PVFS_sys_finalize();
	if (ret < 0)
	{
		printf("finalizing sysint failed with errcode = %d\n", ret);
		return (-1);
	}

	free(resp_gattr);

	free(filename);

	gossip_disable();

	return(0);
}
