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
	int ret = -1,i = 0;
	pvfs_mntlist mnt = {0,NULL};

/*
	if (argc > 1)
	{
		sscanf(argv[1], "%d", (int*)&cmd_handle);
		printf("using handle %d\n",(int) cmd_handle);
	}
*/

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

	/* lookup the root handle */
	req_look.credentials.perms = 1877;
	req_look.name = malloc(2);/*null terminator included*/
	req_look.name[0] = '/';
	req_look.name[1] = '\0';
	req_look.fs_id = resp_init.fsid_list[0];
	printf("looking up the root handle for fsid = %d\n", req_look.fs_id);
	ret = PVFS_sys_lookup(&req_look,&resp_look);
	if (ret < 0)
	{
		printf("Lookup failed with errcode = %d\n", ret);
		return(-1);
	}
	// print the handle 
	printf("--lookup--\n"); 
	printf("ROOT Handle:%ld\n", (long int)resp_look.pinode_refn.handle);
	

	req_readdir = (PVFS_sysreq_readdir *)malloc(sizeof(PVFS_sysreq_readdir));
	if (!req_readdir)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	resp_readdir = (PVFS_sysresp_readdir *)malloc(sizeof(PVFS_sysresp_readdir));
	if (!resp_readdir)
	{
		printf("Error in malloc\n");
		return(-1);
	}

	// Fill in the dir info 

	req_readdir->pinode_refn.handle = resp_look.pinode_refn.handle;
	req_readdir->pinode_refn.fs_id = req_look.fs_id;
	req_readdir->token = 1;
	req_readdir->pvfs_dirent_incount = 6;
	resp_readdir->dirent_array = (PVFS_dirent *)malloc(sizeof(PVFS_dirent) *
			req_readdir->pvfs_dirent_incount);
	resp_readdir->pvfs_dirent_outcount = 6;

	req_readdir->credentials.uid = 100;
	req_readdir->credentials.gid = 100;
	req_readdir->credentials.perms = 1877;


	// call readdir 
	ret = PVFS_sys_readdir(req_readdir,resp_readdir);
	if (ret < 0)
	{
		printf("readdir failed with errcode = %d\n", ret);
		return(-1);
	}
	
	// print the handle 
	printf("--readdir--\n"); 
	printf("Token:%ld\n",(long int)resp_readdir->token);
	for(i = 0;i < resp_readdir->pvfs_dirent_outcount;i++)
	{
		printf("name:%s\n",resp_readdir->dirent_array[i].d_name);
	}

	//close it down
	ret = PVFS_sys_finalize();
	if (ret < 0)
	{
		printf("finalizing sysint failed with errcode = %d\n", ret);
		return (-1);
	}

	return(0);
}
