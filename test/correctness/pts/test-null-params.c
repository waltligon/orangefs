/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* 
 * test-null-params: tests behavior of all sys-init functions will paramater
 * values set to null.
 * Author: Michael Speth
 * Date: 5/27/2003
 * Tab Size: 3
 */

#include <client.h>
#include <sys/time.h>
#include "mpi.h"
#include "pints.h"
#include "pvfs-helper.h"

/* 
 * Preconditions: none
 * Parameters: nullCase - the test case that is checked for this function
 * Postconditions: returs error code of sys initialize; however, I'm not sure what will happen if null params are passed into sys_init so this might seg-fault.
 */
static PVFS_fs_id test_system_init(int nullCase)
{
	int ret = -1;

   memset(&pvfs_helper,0,sizeof(pvfs_helper));

   ret = parse_pvfstab(NULL,&pvfs_helper.mnt);
   if(ret > -1){
		gossip_disable();

      /* init the system interface */
		if(nullCase == 0){
      	ret = PVFS_sys_initialize(NULL,NULL);
		}
		else if(nullCase == 1){
      	ret = PVFS_sys_initialize(NULL,
                                  &pvfs_helper.resp_init);
		}
		else if(nullCase == 2){
      	ret = PVFS_sys_initialize(pvfs_helper.mnt,NULL);
		}
      if(ret > -1){
			pvfs_helper.initialized = 1;
         pvfs_helper.num_test_files = NUM_TEST_FILES;
         ret = 0;
      }
      else{
      	fprintf(stderr, "Error: PVFS_sys_initialize() failure. = %d\n", ret);
      }
	}    
   else{
        fprintf(stderr, "Error: parse_pvfstab() failure.\n");
   }
   return ret;
}

/* Preconditions: none
 * Parameters: nullCase - the test case that is checked for this function
 * Postconditions: returns the error code given by lookup - thats if it doesn't segfault or other catostrophic failure
 */
static int test_lookup(int nullCase){
	int fs_id
	PVFS_credentials credentials;
   PVFS_sysresp_lookup resp_lookup;
	char *name;

   memset(&resp_lookup, 0, sizeof(req_lookup));

	name = (char *)malloc(sizeof(char)*100);
	name = strcpy(name,"name");

	fs_id = initialize_sysint();

   credentials.uid = 100;
   credentials.gid = 100;
   credentials.perms = PVFS_U_WRITE|PVFS_U_READ;

	switch(nullCase){
		case 0:
   		return PVFS_sys_lookup(NULL,NULL,NULL,NULL);
			break;
		case 1:
   		return PVFS_sys_lookup(NULL,credentials,&resp_lookup);
			break;
		case 2:
			credentials.uid = NULL;
   		return PVFS_sys_lookup(fs_id,credentials,&resp_lookup);
			break;
		case 3:
			credentials.gid = NULL;
   		return PVFS_sys_lookup(fs_id,credentials,&resp_lookup);
			break;
		case 4:
			credentials.perms = NULL;
   		return PVFS_sys_lookup(fs_id,credentials,&resp_lookup);
			break;
		case 5:
   		return PVFS_sys_lookup(fs_id,credentials,NULL);
			//PVFS_sys_lookup(fs_id,name,credentials,&resp_lookup);
			break;
		default:
			fprintf(stderr,"Error - not a case\n");
	}
	return -2;
}

/* Preconditions: none
 * Parameters: nullCase - the test case that is checked for this function
 * Postconditions: returns error from getattr
 */
static int test_getattr(int nullCase){
	int fs_id,ret;
	PVFS_credentials credentials;
	PVFS_pinode_refernce pinode_refn;
	uint32_t attrmask;
   PVFS_sysresp_lookup resp_lookup;
	PVFS_sysresp_getattr resp_getattr;
	char *name;

   memset(&req_lookup, 0, sizeof(req_lookup));
   memset(&resp_lookup, 0, sizeof(req_lookup));

	name = (char *)malloc(sizeof(char)*100);
	name = strcpy(name,"name");

	fs_id = initialize_sysint();

   credentials.uid = 100;
   credentials.gid = 100;
   credentials.perms = PVFS_U_WRITE|PVFS_U_READ;

  	if((ret = PVFS_sys_lookup(&req_lookup,&resp_lookup))< 0){
		fprintf(stderr,"lookup failed %d\n"ret);
		return -1;
	}

//    req_getattr.pinode_refn.handle = resp_create->pinode_refn.handle;
    pinode_refn.handle = resp_lookup->attr->meta->dfh
    pinode_refn.fs_id = fs_id;
    attrmask = ATTR_META;

	switch(nullCase){
		case 0:
			return PVFS_sys_getattr(NULL,NULL,NULL,NULL);
			break;
		case 1:
			return PVFS_sys_getattr(NULL,attrmask,credentials,&resp_getattr);
			break;
		case 2:
			pinode_refn.handle = NULL;
			return PVFS_sys_getattr(pinode_refn.handle,attrmask,credentials,&resp_getattr);
			break;
		case 3:
			pinode_refn.fs_id = NULL;
			return PVFS_sys_getattr(pinode_refn.handle,attrmask,credentials,&resp_getattr);
			break;
		case 4:
			return PVFS_sys_getattr(pinode_refn.handle,NULL,credentials,&resp_getattr);
			break;
		case 5:
			return PVFS_sys_getattr(pinode_refn.handle,attrmask,NULL,&resp_getattr);
			break;
		case 6:
   		credentials.uid = NULL;
			return PVFS_sys_getattr(pinode_refn.handle,attrmask,credentials,&resp_getattr);
			break;
		case 7:
   		credentials.gid = NULL;
			return PVFS_sys_getattr(pinode_refn.handle,attrmask,credentials,&resp_getattr);
			break;
		case 8:
   		credentials.perms = NULL;
			return PVFS_sys_getattr(pinode_refn.handle,attrmask,credentials,&resp_getattr);
			break;
		case 9:
			return PVFS_sys_getattr(pinode_refn.handle,attrmask,credentials,NULL);
			break;
	}
	return -2;
}

/* Preconditions: None
 * Parameters: nullCase - the test case that is checked for this function
 * Postconditions:
 */
static int test_setattr(int nullCase){
	return -2;
}

/* Preconditions: None
 * Parameters: nullCase - the test case that is checked for this function
 * Postconditions: returns the error returned by mkdir
 */
static int test_mkdir(int nullCase){
	PVFS_pinode_reference parent_refn;
	uint32_t attrmask;
	PVFS_object_attr attr;
   PVFS_sysresp_mkdir resp_mkdir;

   int ret = -1;
	int fs_id
	PVFS_credentials credentials;
   PVFS_sysresp_lookup resp_lookup;
	char *name;

   memset(&req_mkdir, 0, sizeof(req_mkdir));
   memset(&resp_mkdir, 0, sizeof(req_mkdir));
                                  
   memset(&req_lookup, 0, sizeof(req_lookup));
   memset(&resp_lookup, 0, sizeof(req_lookup));

	name = (char *)malloc(sizeof(char)*100);
	name = strcpy(name,"name");

	fs_id = initialize_sysint();

   credentials.uid = 100;
   credentials.gid = 100;
   credentials.perms = PVFS_U_WRITE|PVFS_U_READ;
  	if((ret = PVFS_sys_lookup(fs_id,name,&resp_lookup))< 0){
		fprintf(stderr,"lookup failed %d\n"ret);
		return -1;
	}
        
//   req_mkdir.parent_refn.handle = parent;
   parent_refn.handle = attr->meta->dfh
   parent_refn.fs_id = fs_id; 
   attrmask = ATTR_BASIC;
   attr.owner = 100;
   attr.group = 100;
   attr.perms = 1877;
   attr.objtype = PVFS_TYPE_DIRECTORY;
   credentials.perms = 1877;
   credentials.uid = 100;
   credentials.gid = 100;

	switch(nullCase){
		case 0:
   		return PVFS_sys_mkdir(NULL,NULL,NULL, NULL, NULL, NULL);
			break;
		case 1:
   		return PVFS_sys_mkdir(NULL,parent_refn,attrmask,attr, &resp_mkdir);
			break;
		case 2:
			parent_refn.handle = NULL;
   		return PVFS_sys_mkdir(fs_id,parent_refn,attrmask,attr, &resp_mkdir);
			break;
		case 3:
			parent_refn.fs_id = NULL;
   		return PVFS_sys_mkdir(fs_id,parent_refn,attrmask,attr, &resp_mkdir);
			break;
		case 4:
   		return PVFS_sys_mkdir(fs_id,parent_refn,NULL,attr, &resp_mkdir);
			break;
		case 5:
   		return PVFS_sys_mkdir(fs_id,parent_refn,attrmask,NULL, &resp_mkdir);
			break;
		case 6:
   		attr.owner = NULL;
   		return PVFS_sys_mkdir(fs_id,parent_refn,attrmask,attr, &resp_mkdir);
			break;
		case 7:
   		attr.group = NULL;
   		return PVFS_sys_mkdir(fs_id,parent_refn,attrmask,attr, &resp_mkdir);
			break;
		case 8:
   		attr.perms = NULL;
   		return PVFS_sys_mkdir(fs_id,parent_refn,attrmask,attr, &resp_mkdir);
			break;
		case 9:
   		attr.objtype = NULL;
   		return PVFS_sys_mkdir(fs_id,parent_refn,attrmask,attr, &resp_mkdir);
			break;
		case 10:
   		return PVFS_sys_mkdir(fs_id,parent_refn,attrmask,attr, NULL);
			break;

		default:
			fprintf(stderr,"Error - no more cases\n");
	}
	return -2;
}

/* Preconditions: none
 * Parameters: nullCase - the test case that is checked for this function
 * Postconditions: returns error code of readdir
 */
static int test_readdir(int nullCase){

   int i, iter, ret;
                         
	PVFS_pinode_reference pinode_refn;
	PVFS_ds_position token;
	int pvfs_dirent_incount;
	PVFS_credentials credentials;
   PVFS_sysresp_readdir resp_readdir;

   int ret = -1;
	int fs_id
   PVFS_sysresp_lookup resp_lookup;
	char *name;

   memset(&resp_readdir,0,sizeof(PVFS_sysresp_readdir));
   memset(&req_readdir,0,sizeof(PVFS_sysreq_readdir));

   memset(&req_lookup, 0, sizeof(req_lookup));
   memset(&resp_lookup, 0, sizeof(req_lookup));

	name = (char *)malloc(sizeof(char)*100);
	name = strcpy(name,"name");

	fs_id = initialize_sysint();

   credentials.uid = 100;
   credentials.gid = 100;
   credentials.perms = PVFS_U_WRITE|PVFS_U_READ;
  	if((ret = PVFS_sys_lookup(fs_id,name,credentials,&resp_lookup))< 0){
		fprintf(stderr,"lookup failed %d\n"ret);
		return -1;
	}

   //req_readdir.pinode_refn.handle = handle;
   pinode_refn.handle = resp_lookup->attr->meta->dfh;
   pinode_refn.fs_id = fs_id;
   token = PVFS2_READDIR_START;
   pvfs_dirent_incount = ndirs;
    
   credentials.uid = 100;
   credentials.gid = 100;
   credentials.perms = 1877;
    
	switch(nullCase){
		case 0:
  			return PVFS_sys_readdir(NULL,NULL,NULL, NULL, NULL);
			break;
		case 1:
  			return PVFS_sys_readdir(NULL, token, pvfs_dirent_incount, credentials,&resp_readdir);
			break;
		case 2:
   		pinode_refn.handle = NULL;
  			return PVFS_sys_readdir(pinode_refn, token, pvfs_dirent_incount, credentials,&resp_readdir);
			break;
		case 3:
   		pinode_refn.fs_id = NULL;
  			return PVFS_sys_readdir(pinode_refn, token, pvfs_dirent_incount, credentials,&resp_readdir);
			break;
		case 4:
  			return PVFS_sys_readdir(pinode_refn, NULL, pvfs_dirent_incount, credentials,&resp_readdir);
			break;
		case 5:
  			return PVFS_sys_readdir(pinode_refn, token, NULL, credentials,&resp_readdir);
			break;
		case 6:
  			return PVFS_sys_readdir(pinode_refn, token, pvfs_dirent_incount, NULL,&resp_readdir);
			break;
		case 7:
   		credentials.uid = NULL
  			return PVFS_sys_readdir(pinode_refn, token, pvfs_dirent_incount, credentials,&resp_readdir);
			break;
		case 8:
   		credentials.gid = NULL
  			return PVFS_sys_readdir(pinode_refn, token, pvfs_dirent_incount, credentials,&resp_readdir);
			break;
		case 9:
   		credentials.perm = NULL
  			return PVFS_sys_readdir(pinode_refn, token, pvfs_dirent_incount, credentials,&resp_readdir);
			break;
		case 10:
  			return PVFS_sys_readdir(pinode_refn, token, pvfs_dirent_incount, credentials,NULL);
			break;
	}
	return -2;
}
