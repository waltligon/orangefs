#include <client.h>

extern int parse_pvfstab(char *fn,pvfs_mntlist *mnt);

int main(int argc,char **argv)
{

	PVFS_sysreq_lookup *req_lk = NULL;
	PVFS_sysresp_lookup *resp_lk = NULL;
	PVFS_sysreq_getattr *req_gattr = NULL;
	PVFS_sysresp_getattr *resp_gattr = NULL;
	PVFS_sysreq_setattr *req_sattr = NULL;
	PVFS_sysreq_mkdir *req_mkdir = NULL;
	PVFS_sysresp_mkdir *resp_mkdir = NULL;
	PVFS_sysreq_rmdir *req_rmdir = NULL;
	PVFS_sysreq_readdir *req_readdir = NULL;
	PVFS_sysresp_readdir *resp_readdir = NULL;
/*
	PVFS_sysreq_create *req_create = NULL;
	PVFS_sysresp_create *resp_create = NULL;
	PVFS_sysreq_statfs *req_statfs = NULL;
	PVFS_sysresp_statfs *resp_statfs = NULL;
	*/
	char filename[80] = "/parl/fshorte/sysint/file1";
	char dirname[256] = "/parl/fshorte/sysint/home";
	int ret = -1,i = 0;
	PVFS_fs_id fsid = 9;
	pvfs_mntlist mnt = {0,NULL};

	/* Parse PVFStab */
	ret = parse_pvfstab(NULL,&mnt);
	if (ret < 0)
	{
		printf("Parsing error\n");
		return(-1);
	}
	/*Init the system interface*/
	ret = PVFS_sys_initialize(mnt);
	if(ret < 0)
	{
		printf("PVFS_sys_initialize() failure. = %d\n", ret);
		return(ret);
	}
	printf("SYSTEM INTERFACE INITIALIZED\n");
	
#if 0
	/* test the lookup function */
	/*Alloc memory and fill the structures*/
	
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
	
	req_lk->name = (char *)malloc(strlen(filename) + 1);
	if (!req_lk->name)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	strncpy(req_lk->name,filename,strlen(filename));
	req_lk->name[strlen(filename)] = '\0';
	req_lk->fs_id = fsid;
	req_lk->credentials.perms = 7;
	ret = PVFS_sys_lookup(req_lk,resp_lk);
	if (ret < 0)
	{
		printf("Lookup failed\n");
		return(-1);
	}
	// print the handle 
	printf("--lookup--\n"); 
	printf("Handle:%ld\n", (long int)resp_lk->pinode_refn.handle);
	printf("FSID:%ld\n", (long int)resp_lk->pinode_refn.fs_id);

	//close it down
	//ret = PVFS_sys_finalize();

	//Init the system interface
	// Setattr test 
	/*ret = PVFS_sys_init();
	if(ret < 0)
	{
		printf("PVFS_sys_init() failure.\n");
		return(ret);
	}*/
#endif

	// test the setattr function 
	//	Alloc memory and fill the structures
	printf("SETATTR HERE===>\n");
	req_sattr = (PVFS_sysreq_setattr *)malloc(sizeof(PVFS_sysreq_setattr));
	if (!req_sattr)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	
	// fill in the handle 
	req_sattr->pinode_refn.handle = 420;//resp_lk->pinode_refn.handle;
	req_sattr->pinode_refn.fs_id = 1;
	req_sattr->attrmask = ATTR_BASIC;
	req_sattr->attr.owner = 12345;
	req_sattr->attr.group = 56789;
	req_sattr->attr.perms = 255;

	//use it
	ret = PVFS_sys_setattr(req_sattr);
	if (ret < 0)
	{
		printf("setattr failed with errcode = %d\n", ret);
		return(-1);
	}
	// print the handle 
	printf("--setattr--\n"); 
	printf("Handle:%ld\n",(long int)req_sattr->pinode_refn.handle);
	printf("FSID:%ld\n",(long int)req_sattr->pinode_refn.fs_id);
	printf("mask:%d\n",req_sattr->attrmask);
	printf("uid:%d\n",req_sattr->attr.owner);
	printf("gid:%d\n",req_sattr->attr.group);
	printf("permissions:%d\n",req_sattr->attr.perms);
		

	// Init the system interface 
	// Getattr test 

	// Test the getattr function 
	//	Alloc memory and fill the structures 

	printf("GETATTR HERE===>\n");
	req_gattr = (PVFS_sysreq_getattr *)malloc(sizeof(PVFS_sysreq_getattr));
	if (!req_gattr)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	resp_gattr = (PVFS_sysresp_getattr *)malloc(sizeof(PVFS_sysresp_getattr));
	if (!resp_gattr)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	
	// Fill in the handle 
	req_gattr->pinode_refn.handle = req_sattr->pinode_refn.handle;
	req_gattr->pinode_refn.fs_id = 1;
	req_gattr->attrmask = ATTR_BASIC;

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
	
#if 0
	// close it down
	ret = PVFS_sys_finalize();

	// Init the system interface 
	// Getattr test 
	ret = PVFS_sys_init();
	if(ret < 0)
	{
		printf("PVFS_sys_init() failure.\n");
		return(ret);
	}
	
	// Test the getattr function 
	//	Alloc memory and fill the structures 
	req_gattr = (PVFS_sysreq_getattr *)malloc(sizeof(PVFS_sysreq_getattr));
	if (!req_gattr)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	resp_gattr = (PVFS_sysresp_getattr *)malloc(sizeof(PVFS_sysresp_getattr));
	if (!resp_gattr)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	
	// Fill in the handle 
	//req_gattr->pinode_no.handle = resp_lk->pinode_no.handle + 2;
	req_gattr->pinode_no.handle = resp_lk->pinode_no.handle;
	req_gattr->pinode_no.fs_id = 0;
	req_gattr->attrmask = ATTR_UID + ATTR_GID + ATTR_SIZE + ATTR_PERM;

	// Use it 
	ret = PVFS_sys_getattr(req_gattr,resp_gattr);
	if (ret < 0)
	{
		printf("getattr failed\n");
		return(-1);
	}
	// print the handle 
	printf("--getattr--\n"); 
	printf("Handle:%ld\n",(long int)resp_gattr->pinode_no.handle);
	printf("mask:%d\n",resp_gattr->attrmask);
	printf("uid:%d\n",resp_gattr->attr.owner);
	printf("gid:%d\n",resp_gattr->attr.group);
	printf("size:%lu\n",(unsigned long)resp_gattr->attr.size);
	printf("permissions:%d\n",resp_gattr->attr.perms);
	
	//close it down
	ret = PVFS_sys_finalize();

	// Init the system interface 
	// mkdir test 

	ret = PVFS_sys_init();
	if(ret < 0)
	{
		printf("PVFS_sys_init() failure.\n");
		return(ret);
	}

	// test the mkdir function 
	//	Alloc memory and fill the structures
	req_mkdir = (PVFS_sysreq_mkdir *)malloc(sizeof(PVFS_sysreq_mkdir));
	if (!req_mkdir)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	resp_mkdir = (PVFS_sysresp_mkdir *)malloc(sizeof(PVFS_sysresp_mkdir));
	if (!resp_mkdir)
	{
		printf("Error in malloc\n");
		return(-1);
	}

	// Fill in the dir info 
	req_mkdir->entry_name = (char *)malloc(strlen(dirname) + 1);
	if (!req_mkdir->entry_name)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	strncpy(req_mkdir->entry_name,dirname,strlen(dirname));
	req_mkdir->entry_name[strlen(dirname)] = '\0';
	req_mkdir->parent_refn.handle = req_sattr->pinode_refn.handle - 1;
	req_mkdir->parent_refn.fs_id = 1;
	req_mkdir->attrmask = ATTR_BASIC;
	req_mkdir->attr.owner = 12345;
	req_mkdir->attr.group = 56789;
	req_mkdir->attr.perms = 63;
	req_mkdir->attr.objtype = ATTR_DIR;
	req_mkdir->credentials.perms = 7;

	// call mkdir 
	ret = PVFS_sys_mkdir(req_mkdir,resp_mkdir);
	if (ret < 0)
	{
		printf("mkdir failed\n");
		return(-1);
	}
	// print the handle 
	printf("--mkdir--\n"); 
	printf("Handle:%ld\n",(long int)(resp_mkdir->pinode_refn.handle & 127));
	printf("FSID:%ld\n",(long int)req_mkdir->parent_refn.fs_id);

	// test the readdir function 
	//	Alloc memory and fill the structures
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
	req_readdir->pinode_refn.handle = resp_mkdir->pinode_refn.handle;
	req_readdir->pinode_refn.fs_id = 1;
	req_readdir->token = PVFS_TOKEN_START;
	req_readdir->pvfs_dirent_incount = 6;
	resp_readdir->dirent_array = (PVFS_dirent *)malloc(sizeof(PVFS_dirent) *\
			req_readdir->pvfs_dirent_incount);
	resp_readdir->pvfs_dirent_outcount = 6;

	// call readdir 
	ret = PVFS_sys_readdir(req_readdir,resp_readdir);
	if (ret < 0)
	{
		printf("readdir failed\n");
		return(-1);
	}
	
	// print the handle 
	printf("--readdir--\n"); 
	printf("Token:%ld\n",(long int)resp_readdir->token);
	for(i = 0;i < resp_readdir->pvfs_dirent_outcount;i++)
	{
		printf("name:%s\n",resp_readdir->dirent_array[i].d_name);
	}

	// test the rmdir function 
	//	Alloc memory and fill the structures
	printf("--rmdir--\n"); 
	req_rmdir = (PVFS_sysreq_rmdir *)malloc(sizeof(PVFS_sysreq_rmdir));
	if (!req_rmdir)
	{
		printf("Error in malloc\n");
		return(-1);
	}

	// Fill in the dir info 
	req_rmdir->entry_name = (char *)malloc(strlen(dirname) + 1);
	if (!req_rmdir->entry_name)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	strncpy(req_rmdir->entry_name,dirname,strlen(dirname));
	req_rmdir->entry_name[strlen(dirname)] = '\0';
	req_rmdir->parent_refn.handle = resp_mkdir->pinode_refn.handle - 1;
	req_rmdir->parent_refn.fs_id = 1;

	// call rmdir 
	ret = PVFS_sys_rmdir(req_rmdir);
	if (ret < 0)
	{
		printf("rmdir failed\n");
		return(-1);
	}
	// test the statfs function 
	//	Alloc memory and fill the structures
	req_statfs = (PVFS_sysreq_statfs *)malloc(sizeof(PVFS_sysreq_statfs));
	if (!req_statfs)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	resp_statfs = (PVFS_sysresp_statfs *)malloc(sizeof(PVFS_sysresp_statfs));
	if (!resp_statfs)
	{
		printf("Error in malloc\n");
		return(-1);
	}

	// Fill in the dir info 
	req_statfs->fs_id = 0;

	// call statfs 
	ret = PVFS_sys_statfs(req_statfs,resp_statfs);
	if (ret < 0)
	{
		printf("statfs failed\n");
		return(-1);
	}
	
	// print the handle 
	printf("--statfs--\n"); 
	printf("Meta stats\n");
	printf("filetotal:%d\n",resp_statfs->statfs.mstat.filetotal);
	printf("filesystem id:%d\n",req_statfs->fs_id);
	
	printf("IO stats\n");
	printf("blocksize:%lu\n",(unsigned long int)resp_statfs->statfs.iostat.blksize);
	printf("blockfree:%u\n",resp_statfs->statfs.iostat.blkfree);
	printf("blockstotal:%u\n",resp_statfs->statfs.iostat.blktotal);
	printf("filestotal:%u\n",resp_statfs->statfs.iostat.filetotal);
	printf("filefree id:%u\n",resp_statfs->statfs.iostat.filefree);

	// test the create function 
	//	Alloc memory and fill the structures
	req_create = (PVFS_sysreq_create *)malloc(sizeof(PVFS_sysreq_create));
	if (!req_create)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	resp_create = (PVFS_sysresp_create *)malloc(sizeof(PVFS_sysresp_create));
	if (!resp_create)
	{
		printf("Error in malloc\n");
		return(-1);
	}

	// Fill in the create info 
	req_create->name = (char *)malloc(strlen(filename2) + 1);
	if (!req_create->name)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	strncpy(req_create->name,filename2,strlen(filename2));
	req_create->name[strlen(filename2)] = '\0';
	//req_create->pinode_no.handle = 0;
	//req_create->handles = NULL;
	//req_create->handle_cnt = 0; 
	req_create->attrmask = ATTR_UID + ATTR_GID + ATTR_SIZE + ATTR_PERM;
	req_create->attr.owner = 12345;
	req_create->attr.group = 56789;
	req_create->attr.u.meta.size = 512;
	req_create->attr.perms = 642;
	
	// Fill in the dist 
	//req_create->dist = malloc(sizeof(PVFS_dist));
	req_create->dist.type = PVFS_DIST_STRIPED;
	req_create->dist.u.striped.base = 0;
	req_create->dist.u.striped.pcount = 3;
	req_create->dist.u.striped.ssize = 512;

	// call create 
	ret = PVFS_sys_create(req_create,resp_create);
	if (ret < 0)
	{
		printf("create failed\n");
		return(-1);
	}
	
	// print the handle 
	printf("--create--\n"); 
	printf("Handle:%ld\n",(long int)resp_create->pinode_no.handle);
#endif
	//close it down
	ret = PVFS_sys_finalize();
	if (ret < 0)
	{
		printf("finalizing sysint failed with errcode = %d\n", ret);
		return (-1);
	}

	return(0);

}
