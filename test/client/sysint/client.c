/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "client.h"
#include "pvfs2-util.h"

void gen_rand_str(int len, char** gen_str);

int main(int argc,char **argv)
{
	PVFS_sysresp_lookup resp_look;
	PVFS_sysresp_lookup *resp_lk = NULL;
	PVFS_sysresp_readdir *resp_readdir = NULL;
#if 0
	PVFS_sysreq_getattr *req_gattr = NULL;
	PVFS_sysresp_getattr *resp_gattr = NULL;
	PVFS_sysreq_setattr *req_sattr = NULL;
	PVFS_sysreq_mkdir *req_mkdir = NULL;
	PVFS_sysresp_mkdir *resp_mkdir = NULL;
	PVFS_sysreq_rmdir *req_rmdir = NULL;
	PVFS_sysreq_statfs *req_statfs = NULL;
	PVFS_sysresp_statfs *resp_statfs = NULL;
#endif
	PVFS_sysresp_create *resp_create = NULL;
	char *filename;
	//char dirname[256] = "/parl/fshorte/sysint/home";
	int ret = -1,i = 0;
	PVFS_fs_id fs_id;
	char* name = "/";
	PVFS_credentials credentials;
	char* entry_name;
	PVFS_pinode_reference parent_refn;
	PVFS_sys_attr attr;
	PVFS_pinode_reference pinode_refn;
	PVFS_ds_position token;
	int pvfs_dirent_incount;

	PVFS_handle lk_handle;
	PVFS_handle lk_fsid;

	gen_rand_str(10,&filename);

	printf("creating a file named %s\n", filename);

	ret = PVFS_util_init_defaults();
	if(ret < 0)
	{
		PVFS_perror("PVFS_util_init_defaults", ret);
		return(-1);
	}
	ret = PVFS_util_get_default_fsid(&fs_id);
	if(ret < 0)
	{
		PVFS_perror("PVFS_util_get_default_fsid", ret);
		return(-1);
	}

	printf("SYSTEM INTERFACE INITIALIZED\n");

	/* lookup the root handle */
	printf("looking up the root handle for fsid = %d\n", fs_id);
	ret = PVFS_sys_lookup(fs_id, name, credentials,
                              &resp_look, PVFS2_LOOKUP_LINK_NO_FOLLOW);
	if (ret < 0)
	{
		printf("Lookup failed with errcode = %d\n", ret);
		return(-1);
	}
	// print the handle 
	printf("--lookup--\n"); 
	printf("ROOT Handle:%ld\n", (long int)resp_look.pinode_refn.handle);

	/* test create */
	resp_create = (PVFS_sysresp_create *)malloc(sizeof(PVFS_sysresp_create));
	if (!resp_create)
	{
		printf("Error in malloc\n");
		return(-1);
	}

	// Fill in the create info 
	entry_name = (char *)malloc(strlen(filename) + 1);
	if (!entry_name)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	memcpy(entry_name,filename,strlen(filename) + 1);
	attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
	attr.owner = 100;
	attr.group = 100;
	attr.perms = 1877;
	attr.atime = attr.mtime = attr.ctime =
	    time(NULL);

	credentials.uid = 100;
	credentials.gid = 100;

	parent_refn.handle = resp_look.pinode_refn.handle;
	parent_refn.fs_id = fs_id;

	
#if 0
	// Fill in the dist 
	//req_create->dist = malloc(sizeof(PVFS_dist));
	req_create->dist.type = PVFS_DIST_STRIPED;
	req_create->dist.u.striped.base = 0;
	req_create->dist.u.striped.pcount = 3;
	req_create->dist.u.striped.ssize = 512;
#endif

	// call create 
	ret = PVFS_sys_create(entry_name, parent_refn, attr,
				credentials, resp_create);
	if (ret < 0)
	{
		printf("create failed with errcode = %d\n", ret);
		return(-1);
	}
	
	// print the handle 
	printf("--create--\n"); 
	printf("Handle:%ld\n",(long int)resp_create->pinode_refn.handle);

#if 0
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
	req_gattr->pinode_refn.handle = resp_create->pinode_refn.handle;
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
	printf("nr_datafiles:%d\n",resp_gattr->attr.u.meta.nr_datafiles);

	for(i=0; i < resp_gattr->attr.u.meta.nr_datafiles; i++)
	{
		printf("\thandle: %d\n", resp_gattr->attr.u.meta.dfh[i]);
	}
#endif
	

	free(entry_name);
	free(resp_create);



	/* test the lookup function */
	resp_lk = (PVFS_sysresp_lookup *)malloc(sizeof(PVFS_sysresp_lookup));
	if (!resp_lk)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	
	name = (char *)malloc(strlen(filename) + 2);
	if (!name)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	name[0] = '/';
	memcpy(name + 1,filename,strlen(filename) + 1 );

	ret = PVFS_sys_lookup(fs_id, name, credentials,
                              resp_lk, PVFS2_LOOKUP_LINK_NO_FOLLOW);
	if (ret < 0)
	{
		printf("Lookup failed with errcode = %d\n", ret);
		return(-1);
	}
	// print the handle 
	printf("--lookup--\n"); 
	printf("Handle:%ld\n", (long int)resp_lk->pinode_refn.handle);
	printf("FSID:%ld\n", (long int)resp_lk->pinode_refn.fs_id);

	lk_handle = resp_lk->pinode_refn.handle;
	lk_fsid = resp_lk->pinode_refn.fs_id;

	free(name);
	free(resp_lk);





#if 0
	/* Test the getattr function */
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
	req_gattr->pinode_refn.handle = lk_handle;
	req_gattr->pinode_refn.fs_id = lk_fsid;
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
	printf("nr_datafiles:%d\n",resp_gattr->attr.u.meta.nr_datafiles);
	

	// test the setattr function 
	printf("SETATTR HERE===>\n");
	req_sattr = (PVFS_sysreq_setattr *)malloc(sizeof(PVFS_sysreq_setattr));
	if (!req_sattr)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	
	// fill in the handle 
	req_sattr->pinode_refn.handle = lk_handle;//resp_lk->pinode_refn.handle;
	req_sattr->pinode_refn.fs_id = lk_fsid;
	req_sattr->attrmask = ATTR_META;
	req_sattr->attr.owner = 12345;
	req_sattr->attr.group = 56789;
	req_sattr->attr.perms = 255;
	req_sattr->attr.atime = 1111111;
	req_sattr->attr.mtime = 2222222;
	req_sattr->attr.ctime = 3333333;
	req_sattr->attr.objtype = PVFS_TYPE_METAFILE;

	req_sattr->attr.u.meta.dfh = NULL;
	req_sattr->attr.u.meta.nr_datafiles = 0;
	//req_sattr->attr.u.meta.dfh = &some_datafile;
	//req_sattr->attr.u.meta.nr_datafiles = 1;

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
	printf("atime:%d\n",(int)req_sattr->attr.atime);
	printf("mtime:%d\n",(int)req_sattr->attr.mtime);
	printf("ctime:%d\n",(int)req_sattr->attr.ctime);
	printf("nr_datafiles:%d\n",req_sattr->attr.u.meta.nr_datafiles);
		

	/* Test the getattr function */
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
	req_gattr->pinode_refn.fs_id = 9;
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
	printf("nr_datafiles:%d\n",resp_gattr->attr.u.meta.nr_datafiles);
#endif
	
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
		printf("getattr failed with errcode = %d\n", ret);
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
	req_mkdir->attr.objtype = PVFS_TYPE_DIRECTORY;

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
#endif

	resp_readdir = (PVFS_sysresp_readdir *)malloc(sizeof(PVFS_sysresp_readdir));
	if (!resp_readdir)
	{
		printf("Error in malloc\n");
		return(-1);
	}

	// Fill in the dir info 

	pinode_refn.handle = resp_look.pinode_refn.handle;
	pinode_refn.fs_id = fs_id;
	token = PVFS_READDIR_START;
	pvfs_dirent_incount = 6;
	credentials.uid = 100;
	credentials.gid = 100;


	// call readdir 
	ret = PVFS_sys_readdir(pinode_refn, token, pvfs_dirent_incount, 
				credentials,resp_readdir);
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
#if 0

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

#endif
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

/* generate random filenames cause ddd sucks and doesn't like taking cmd line
 * arguments (and remove doesn't work yet so I can't cleanup the crap I already
 * created)
 */
void gen_rand_str(int len, char** gen_str)
{
	static char alphabet[] = "abcdefghijklmnopqrstuvwxyz";
	int i;
	struct timeval poop;
	int newchar = 0;
	gettimeofday(&poop, NULL);

	*gen_str = malloc(len + 1);
	for(i = 0; i < len; i++)
	{
		newchar = ((1+(rand() % 26)) + poop.tv_usec) % 26;
		(*gen_str)[i] = alphabet[newchar];
	}
	(*gen_str)[len] = '\0';
}
