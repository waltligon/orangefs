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

void gen_rand_str(int len, char** gen_str);
extern int parse_pvfstab(char *fn,pvfs_mntlist *mnt);

int main(int argc,char **argv)
{
	PVFS_sysresp_init resp_init;
	PVFS_sysreq_lookup req_look;
	PVFS_sysresp_lookup resp_look;
	PVFS_sysreq_lookup *req_lk = NULL;
	PVFS_sysresp_lookup *resp_lk = NULL;
	char *filename;
	int name_sz;
	int ret = -1;
	pvfs_mntlist mnt = {0,NULL};

	PVFS_handle lk_handle;
	PVFS_handle lk_fsid;

	if (argc != 2)
	{
		printf("USAGE: %s /path/to/lookup\n", argv[0]);
                return 1;
	}
	name_sz = strlen(argv[1]) + 1; /*include null terminator*/
	filename = malloc(name_sz);

	memcpy(filename, argv[1], name_sz);
	printf("lookup up path %s\n", filename);

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
	
	/* test the lookup function */
	req_lk = (PVFS_sysreq_lookup *)malloc(sizeof(PVFS_sysreq_lookup));
	if (!req_lk)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	resp_lk = (PVFS_sysresp_lookup *)malloc(sizeof(PVFS_sysresp_lookup));
	if (!resp_lk)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	
	req_lk->name = filename;
	if (!req_lk->name)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	req_lk->fs_id = resp_init.fsid_list[0];

	req_lk->credentials.uid = 100;
	req_lk->credentials.gid = 100;
	req_lk->credentials.perms = 1877;

	ret = PVFS_sys_lookup(req_lk,resp_lk);
	if (ret < 0)
	{
		printf("Lookup failed with errcode = %d\n", ret);
		return(-1);
	}
	// print the handle 
	printf("--lookup--\n"); 
	printf("\tHandle:%ld\n", (long int)resp_lk->pinode_refn.handle);
	printf("\tFSID:%ld\n", (long int)resp_lk->pinode_refn.fs_id);

	lk_handle = resp_lk->pinode_refn.handle;
	lk_fsid = resp_lk->pinode_refn.fs_id;

	free(req_lk);
	free(resp_lk);

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
