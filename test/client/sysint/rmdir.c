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
	PVFS_sysreq_rmdir *req_rmdir = NULL;
	int ret = -1, name_sz = 0;
	pvfs_mntlist mnt = {0,NULL};


	if (argc != 2)
	{
            printf("usage: %s dir_to_remove\n", argv[0]);
            return 1;
        }

	printf("creating a file named %s\n", argv[1]);

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
	

	// test the rmdir function 
	printf("--rmdir--\n"); 
	req_rmdir = (PVFS_sysreq_rmdir *)malloc(sizeof(PVFS_sysreq_rmdir));
	if (!req_rmdir)
	{
		printf("Error in malloc\n");
		return(-1);
	}

	req_rmdir->entry_name = argv[1];
	req_rmdir->parent_refn.handle = resp_look.pinode_refn.handle;
	req_rmdir->parent_refn.fs_id = resp_look.pinode_refn.fs_id;
	req_rmdir->credentials.uid = 100;
	req_rmdir->credentials.gid = 100;
	req_rmdir->credentials.perms = 1877;

	// call rmdir 
	ret = PVFS_sys_rmdir(req_rmdir);
	if (ret < 0)
	{
		printf("rmdir failed with errcode = %d\n",ret);
		return(-1);
	}

	printf("===================================");
	printf("Directory named %s has been removed.", argv[1]);

	//close it down
	ret = PVFS_sys_finalize();
	if (ret < 0)
	{
		printf("finalizing sysint failed with errcode = %d\n", ret);
		return (-1);
	}
	return(0);
}

