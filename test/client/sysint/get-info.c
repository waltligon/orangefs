/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <client.h>
#include <sys/time.h>

/*why were these commented out?*/

#define ATTR_UID 1
#define ATTR_GID 2
#define ATTR_PERM 4
#define ATTR_ATIME 8
#define ATTR_CTIME 16
#define ATTR_MTIME 32
#define ATTR_TYPE 2048

extern int parse_pvfstab(char *fn,pvfs_mntlist *mnt);

int main(int argc,char **argv)
{
	PVFS_sysresp_init resp_init;
	PVFS_sysreq_lookup req_look;
	PVFS_sysresp_lookup resp_look;
	PVFS_sysreq_getattr *req_gattr = NULL;
	PVFS_sysresp_getattr *resp_gattr = NULL;

	char *filename = NULL;
	int ret = -1,i = 0;
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
	req_look.credentials.uid = 100;
	req_look.credentials.gid = 100;
	req_look.credentials.perms = 1877;
	req_look.name = filename;
	req_look.fs_id = resp_init.fsid_list[0];
	ret = PVFS_sys_lookup(&req_look,&resp_look);
	if (ret < 0)
	{
		printf("Lookup failed with errcode = %d\n", ret);
		return(-1);
	}

	printf("GETATTR HERE===>\n");
	req_gattr = (PVFS_sysreq_getattr *)malloc(sizeof(PVFS_sysreq_getattr));
	if (req_gattr == NULL)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	resp_gattr = (PVFS_sysresp_getattr *)malloc(sizeof(PVFS_sysresp_getattr));
	if (resp_gattr == NULL)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	
	// Fill in the handle 
	req_gattr->pinode_refn.handle = resp_look.pinode_refn.handle;
	req_gattr->pinode_refn.fs_id = resp_init.fsid_list[0];
	req_gattr->attrmask = ATTR_META;

	// Use it 
	ret = PVFS_sys_getattr(req_gattr,resp_gattr);
	if (ret < 0)
	{
		printf("getattr failed with errcode = %d\n", ret);
		return(-1);
	}
	// print the handle 
	printf("--getattr--\n"); 
	printf("Handle:%ld\n",(long int)req_gattr->pinode_refn.handle);
	printf("FSID:%ld\n",(long int)req_gattr->pinode_refn.fs_id);
	printf("mask:%d\n",req_gattr->attrmask);
	printf("uid:%d\n",resp_gattr->attr.owner);
	printf("gid:%d\n",resp_gattr->attr.group);
	printf("permissions:%d\n",resp_gattr->attr.perms);
	printf("atime:%d\n",(int)resp_gattr->attr.atime);
	printf("mtime:%d\n",(int)resp_gattr->attr.mtime);
	printf("ctime:%d\n",(int)resp_gattr->attr.ctime);
	switch(resp_gattr->attr.objtype)
	{
		case ATTR_META:
		printf("METAFILE\n");
		printf("nr_datafiles:%d\n",resp_gattr->attr.u.meta.nr_datafiles);

		for(i=0; i < resp_gattr->attr.u.meta.nr_datafiles; i++)
		{
			printf("\thandle: %d\n", (int)resp_gattr->attr.u.meta.dfh[i]);
		}
		break;

		case ATTR_DATA:
		printf("DATAFILE?? we shouldn't see these\n");
		printf("size written on server = %Ld", resp_gattr->attr.u.data.size);
		break;

		case ATTR_DIR:
		printf("DIRECTORY\n");
		printf("handle: = %d", (int)resp_gattr->attr.u.dir.dfh);
		break;
	}
	

	//close it down
	ret = PVFS_sys_finalize();
	if (ret < 0)
	{
		printf("finalizing sysint failed with errcode = %d\n", ret);
		return (-1);
	}

	free(req_gattr);
	free(resp_gattr);

	free(filename);

	gossip_disable();

	return(0);
}
