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
	PVFS_sysreq_readdir *req_readdir = NULL;
	PVFS_sysresp_readdir *resp_readdir = NULL;
	int ret = -1,i = 0, name_sz = 0;
	pvfs_mntlist mnt = {0,NULL};
	int max_dirents_returned = 25;
	char* starting_point = NULL;

	switch(argc)
	{
		case 3:
			sscanf(argv[2], "%d", &max_dirents_returned);
		case 2:
			name_sz = strlen(argv[1]) + 1;
			starting_point = malloc(name_sz);
			memcpy(starting_point, argv[1], name_sz);
			break;
		default:
			name_sz = 2;
			starting_point = malloc(name_sz);
			starting_point[0] = '/';
			starting_point[1] = '\0';
	}
	printf("no more than %d dirents should be returned\n",max_dirents_returned);

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

	/* lookup the root handle */
	req_look.credentials.uid = 100;
	req_look.credentials.gid = 100;
	req_look.credentials.perms = 1877;
	req_look.name = starting_point;
	req_look.fs_id = resp_init.fsid_list[0];
	ret = PVFS_sys_lookup(&req_look,&resp_look);
	if (ret < 0)
	{
		printf("Lookup failed with errcode = %d\n", ret);
		free(starting_point);
		return(-1);
	}
	/* print the handle */
	/*printf("ROOT Handle:%ld\n", (long int)resp_look.pinode_refn.handle);*/
	

	req_readdir = (PVFS_sysreq_readdir *)malloc(sizeof(PVFS_sysreq_readdir));
	if (req_readdir == NULL)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	resp_readdir = (PVFS_sysresp_readdir *)malloc(sizeof(PVFS_sysresp_readdir));
	if (resp_readdir == NULL)
	{
		printf("Error in malloc\n");
		return(-1);
	}

	printf("LOOKUP_RESPONSE===>\n\tresp_look.pinode_refn.handle = %ld\n\tresp_look.pinode_refn.fs_id = %d\n",resp_look.pinode_refn.handle, resp_look.pinode_refn.fs_id);

	req_readdir->pinode_refn.handle = resp_look.pinode_refn.handle;
	req_readdir->pinode_refn.fs_id = req_look.fs_id;
	req_readdir->token = PVFS2_READDIR_START;
	req_readdir->pvfs_dirent_incount = max_dirents_returned;

	req_readdir->credentials.uid = 100;
	req_readdir->credentials.gid = 100;
	req_readdir->credentials.perms = 1877;


	/* call readdir */
        memset(resp_readdir,0,sizeof(PVFS_sysresp_readdir));
	ret = PVFS_sys_readdir(req_readdir,resp_readdir);
	if (ret < 0)
	{
		printf("readdir failed with errcode = %d\n", ret);
		return(-1);
	}
	
	printf("===>READDIR\n"); 
	printf("Token:%ld\n",(long int)resp_readdir->token);
	printf("Returned %d dirents\n",resp_readdir->pvfs_dirent_outcount);
	for(i = 0;i < resp_readdir->pvfs_dirent_outcount;i++)
	{
		printf("name:%s\t%d\n",resp_readdir->dirent_array[i].d_name,
				resp_readdir->dirent_array[i].handle);
	}

	/*close it down*/
	ret = PVFS_sys_finalize();
	if (ret < 0)
	{
		printf("finalizing sysint failed with errcode = %d\n", ret);
		return (-1);
	}
        if (resp_readdir->pvfs_dirent_outcount)
            free(resp_readdir->dirent_array); /*allocated by the system interface*/
	free(req_readdir);		/* allocated by us */
	free(resp_readdir);		/* allocated by us */

	free(starting_point);

	return(0);
}
