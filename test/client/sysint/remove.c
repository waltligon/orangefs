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
	PVFS_sysreq_remove *req_remove;
	char *filename = NULL;
	int ret = -1, name_sz = 0;
	pvfs_mntlist mnt = {0,NULL};


	if (argc > 1)
	{
		name_sz = strlen(argv[1]) + 1; /*include null terminator*/
		filename = malloc(name_sz);
		memcpy(filename, argv[1], name_sz);
	}
	else
	{
		printf("usage: %s file_to_remove\n", argv[0]);
	}

	printf("creating a file named %s\n", filename);

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
	printf("--remove--\n"); 
	req_remove = (PVFS_sysreq_remove *)malloc(sizeof(PVFS_sysreq_remove));
	if (req_remove == NULL)
	{
		printf("Error in malloc\n");
		return(-1);
	}

	req_remove->entry_name = filename;
	req_remove->parent_refn.handle = resp_look.pinode_refn.handle;
	req_remove->parent_refn.fs_id = resp_look.pinode_refn.fs_id;
	req_remove->credentials.uid = 100;
	req_remove->credentials.gid = 100;
	req_remove->credentials.perms = 1877;

	// call rmdir 
	ret = PVFS_sys_remove(req_remove);
	if (ret < 0)
	{
		printf("remove failed with errcode = %d\n",ret);
		return(-1);
	}

	printf("===================================");
	printf("file named %s has been removed.", filename);

	//close it down
	ret = PVFS_sys_finalize();
	if (ret < 0)
	{
		printf("finalizing sysint failed with errcode = %d\n", ret);
		return (-1);
	}

	free(filename);
	return(0);
}
