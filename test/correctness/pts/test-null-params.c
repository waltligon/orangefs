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
   PVFS_sysreq_lookup req_lookup;
   PVFS_sysresp_lookup resp_lookup;
	char *name;

   memset(&req_lookup, 0, sizeof(req_lookup));
   memset(&resp_lookup, 0, sizeof(req_lookup));

	name = (char *)malloc(sizeof(char)*100);
	name = strcpy(name,"name");

	fs_id = initialize_sysint();

   req_lookup.name = name;
   req_lookup.fs_id = fs_id;
   req_lookup.credentials.uid = 100;
   req_lookup.credentials.gid = 100;
   req_lookup.credentials.perms = PVFS_U_WRITE|PVFS_U_READ;

	if(nullCase == 0){
   	return PVFS_sys_lookup(NULL,NULL);
	}
	else if(nullCase == 1){
   	return PVFS_sys_lookup(NULL,&resp_lookup);
	}
	else if(nullCase == 2){
   	return PVFS_sys_lookup(&req_lookup,NULL);
	}
}

/* Preconditions: none
 * Parameters: nullCase - the test case that is checked for this function
 * Postconditions: returns error from getattr
 */
static int test_getattr(int nullCase){
	int fs_id,ret;

   PVFS_sysreq_lookup req_lookup;
   PVFS_sysresp_lookup resp_lookup;
	PVFS_sysreq_getattr req_getattr;
	PVFS_sysresp_getattr resp_getattr;
	char *name;

   memset(&req_lookup, 0, sizeof(req_lookup));
   memset(&resp_lookup, 0, sizeof(req_lookup));

	name = (char *)malloc(sizeof(char)*100);
	name = strcpy(name,"name");

	fs_id = initialize_sysint();

   req_lookup.name = name;
   req_lookup.fs_id = fs_id;
   req_lookup.credentials.uid = 100;
   req_lookup.credentials.gid = 100;
   req_lookup.credentials.perms = PVFS_U_WRITE|PVFS_U_READ;

  	if((ret = PVFS_sys_lookup(&req_lookup,&resp_lookup))< 0){
		fprintf(stderr,"lookup failed %d\n"ret);
		return -1;
	}

//    req_getattr.pinode_refn.handle = resp_create->pinode_refn.handle;
    req_getattr.pinode_refn.handle = resp_lookup->attr->meta->dfh
    req_getattr.pinode_refn.fs_id = fs_id;
    req_getattr.attrmask = ATTR_META;

	if(nullCase == 0){
		return PVFS_sys_getattr(NULL,NULL);
	}
	else if(nullCase == 1){
		return PVFS_sys_getattr(NULL,&resp_getattr);
	}
	else if(nullCase == 2){
		return PVFS_sys_getattr(&req_getattr,NULL);
	}
}

/* Preconditions: None
 * Parameters: nullCase - the test case that is checked for this function
 * Postconditions:
 */
static int test_setattr(int nullCase){
	return -1;
}

/* Preconditions: None
 * Parameters: nullCase - the test case that is checked for this function
 * Postconditions: returns the error returned by mkdir
 */
static int test_mkdir(int nullCase){
	PVFS_sysreq_mkdir req_mkdir;
   PVFS_sysresp_mkdir resp_mkdir;

   int ret = -1;
	int fs_id
   PVFS_sysreq_lookup req_lookup;
   PVFS_sysresp_lookup resp_lookup;
	char *name;

   memset(&req_mkdir, 0, sizeof(req_mkdir));
   memset(&resp_mkdir, 0, sizeof(req_mkdir));
                                  
   memset(&req_lookup, 0, sizeof(req_lookup));
   memset(&resp_lookup, 0, sizeof(req_lookup));

	name = (char *)malloc(sizeof(char)*100);
	name = strcpy(name,"name");

	fs_id = initialize_sysint();

   req_lookup.name = name;
   req_lookup.fs_id = fs_id;
   req_lookup.credentials.uid = 100;
   req_lookup.credentials.gid = 100;
   req_lookup.credentials.perms = PVFS_U_WRITE|PVFS_U_READ;
  	if((ret = PVFS_sys_lookup(&req_lookup,&resp_lookup))< 0){
		fprintf(stderr,"lookup failed %d\n"ret);
		return -1;
	}
        
   req_mkdir.entry_name = name;
//   req_mkdir.parent_refn.handle = parent;
   req_mkdir.parent_refn.handle = resp_lookup->attr->meta->dfh
   req_mkdir.parent_refn.fs_id = fs_id; 
   req_mkdir.attrmask = ATTR_BASIC;
   req_mkdir.attr.owner = 100;
   req_mkdir.attr.group = 100;
   req_mkdir.attr.perms = 1877;
   req_mkdir.attr.objtype = ATTR_DIR;
   req_mkdir.credentials.perms = 1877;
   req_mkdir.credentials.uid = 100;
   req_mkdir.credentials.gid = 100;

	if(nullCase == 0){
   	return PVFS_sys_mkdir(NULL,NULL);
	}
	else if(nullCase == 1){
   	return PVFS_sys_mkdir(NULL, &resp_mkdir);
	}
	else if(nullCase == 2){
   	return PVFS_sys_mkdir(&req_mkdir, NULL);
	}
}

/* Preconditions: none
 * Parameters: nullCase - the test case that is checked for this function
 * Postconditions: returns error code of readdir
 */
static int test_readdir(int nullCase){

   int i, iter, ret;
                         
   PVFS_sysreq_readdir req_readdir;
   PVFS_sysresp_readdir resp_readdir;

   int ret = -1;
	int fs_id
   PVFS_sysreq_lookup req_lookup;
   PVFS_sysresp_lookup resp_lookup;
	char *name;

   memset(&resp_readdir,0,sizeof(PVFS_sysresp_readdir));
   memset(&req_readdir,0,sizeof(PVFS_sysreq_readdir));

   memset(&req_lookup, 0, sizeof(req_lookup));
   memset(&resp_lookup, 0, sizeof(req_lookup));

	name = (char *)malloc(sizeof(char)*100);
	name = strcpy(name,"name");

	fs_id = initialize_sysint();

   req_lookup.name = name;
   req_lookup.fs_id = fs_id;
   req_lookup.credentials.uid = 100;
   req_lookup.credentials.gid = 100;
   req_lookup.credentials.perms = PVFS_U_WRITE|PVFS_U_READ;
  	if((ret = PVFS_sys_lookup(&req_lookup,&resp_lookup))< 0){
		fprintf(stderr,"lookup failed %d\n"ret);
		return -1;
	}

   //req_readdir.pinode_refn.handle = handle;
   req_readdir.pinode_refn.handle = resp_lookup->attr->meta->dfh;
   req_readdir.pinode_refn.fs_id = fs_id;
   req_readdir.token = PVFS2_READDIR_START;
   req_readdir.pvfs_dirent_incount = ndirs;
    
   req_readdir.credentials.uid = 100;
   req_readdir.credentials.gid = 100;
   req_readdir.credentials.perms = 1877;
    
	if(nullCase == 0){
  		return  PVFS_sys_readdir(NULL,NULL);
	}
	else if(nullCase == 1){
  		return PVFS_sys_readdir(NULL,&resp_readdir);
	}
	else if(nullCase == 2){
  		return PVFS_sys_readdir(&req_readdir,NULL);
	}
}
