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
#include "pts.h"
#include "pvfs-helper.h"
#include "null-params.h"

extern pvfs_helper_t pvfs_helper;

/* 
 * Preconditions: none
 * Parameters: nullCase - the test case that is checked for this function
 * Postconditions: returs error code of sys initialize; however, I'm not sure what will happen if null params are passed into sys_init so this might seg-fault.
 * Hase 3 test cases
 */
static int test_system_init(int nullCase)
{
	int ret = -1;

   memset(&pvfs_helper,0,sizeof(pvfs_helper));

   ret = parse_pvfstab(NULL,&pvfs_helper.mnt);
   if(ret > -1){
		gossip_disable();

      /* init the system interface */
		if(nullCase == 0){
			pvfs_helper.mnt.nr_entry = NULL;
      	ret = PVFS_sys_initialize(pvfs_helper.mnt,&pvfs_helper.resp_init);
		}
		else if(nullCase == 1){
			pvfs_helper.mnt.ptab_p = NULL;
      	ret = PVFS_sys_initialize(pvfs_helper.mnt,
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
 * Hase 6 test cases
 */
static int test_lookup(int nullCase){
	int fs_id;
	PVFS_credentials credentials;
   PVFS_sysresp_lookup resp_lookup;
	char *name;

	name = (char *)malloc(sizeof(char)*100);
	name = strcpy(name,"name");

	fs_id = initialize_sysint();

   credentials.uid = 100;
   credentials.gid = 100;
   credentials.perms = PVFS_U_WRITE|PVFS_U_READ;

	switch(nullCase){
		case 0:
   		return PVFS_sys_lookup(NULL,name,credentials,&resp_lookup);
			break;
		case 1:
   		return PVFS_sys_lookup(fs_id,NULL,credentials,&resp_lookup);
			break;
		case 2:
			credentials.uid = NULL;
   		return PVFS_sys_lookup(fs_id,name,credentials,&resp_lookup);
			break;
		case 3:
			credentials.gid = NULL;
   		return PVFS_sys_lookup(fs_id,name,credentials,&resp_lookup);
			break;
		case 4:
			credentials.perms = NULL;
   		return PVFS_sys_lookup(fs_id,name,credentials,&resp_lookup);
			break;
		case 5:
   		return PVFS_sys_lookup(fs_id,name,credentials,NULL);
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
 * Has 7 Test Cases
 */
static int test_getattr(int nullCase){
	int fs_id,ret;
	PVFS_credentials credentials;
	PVFS_pinode_reference pinode_refn;
	uint32_t attrmask;
   PVFS_sysresp_lookup resp_lookup;
	PVFS_sysresp_getattr resp_getattr;
	char *name;

	name = (char *)malloc(sizeof(char)*100);
	name = strcpy(name,"name");

	fs_id = initialize_sysint();

   credentials.uid = 100;
   credentials.gid = 100;
   credentials.perms = PVFS_U_WRITE|PVFS_U_READ;

  	if((ret = PVFS_sys_lookup(fs_id,name,credentials,&resp_lookup))< 0){
		fprintf(stderr,"lookup failed %d\n",ret);
		return -1;
	}

//    req_getattr.pinode_refn.handle = resp_create->pinode_refn.handle;
//    pinode_refn.handle = resp_lookup->attr->meta->dfh
//    pinode_refn.fs_id = fs_id;
	pinode_refn = resp_lookup.pinode_refn;
   //attrmask = ATTR_META;
	attrmask = PVFS_ATTR_SYS_ALL_NOSIZE;

	switch(nullCase){
		case 0:
			pinode_refn.handle = NULL;
			return PVFS_sys_getattr(pinode_refn,attrmask,credentials,&resp_getattr);
			break;
		case 1:
			pinode_refn.fs_id = NULL;
			return PVFS_sys_getattr(pinode_refn,attrmask,credentials,&resp_getattr);
			break;
		case 2:
			return PVFS_sys_getattr(pinode_refn,NULL,credentials,&resp_getattr);
			break;
		case 3:
   		credentials.uid = NULL;
			return PVFS_sys_getattr(pinode_refn,attrmask,credentials,&resp_getattr);
			break;
		case 4:
   		credentials.gid = NULL;
			return PVFS_sys_getattr(pinode_refn,attrmask,credentials,&resp_getattr);
			break;
		case 5:
   		credentials.perms = NULL;
			return PVFS_sys_getattr(pinode_refn,attrmask,credentials,&resp_getattr);
			break;
		case 6:
			return PVFS_sys_getattr(pinode_refn,attrmask,credentials,NULL);
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
 * Has 10 test cases
 */
static int test_mkdir(int nullCase){
	PVFS_pinode_reference parent_refn;
	uint32_t attrmask;
	PVFS_object_attr attr;
   PVFS_sysresp_mkdir resp_mkdir;

   int ret = -1;
	int fs_id;
	PVFS_credentials credentials;
   PVFS_sysresp_lookup resp_lookup;
	char *name;

	name = (char *)malloc(sizeof(char)*100);
	name = strcpy(name,"name");

	fs_id = initialize_sysint();

   credentials.uid = 100;
   credentials.gid = 100;
   credentials.perms = PVFS_U_WRITE|PVFS_U_READ;
  	if((ret = PVFS_sys_lookup(fs_id,name,credentials,&resp_lookup))< 0){
		fprintf(stderr,"lookup failed %d\n",ret);
		return -1;
	}
        
//   req_mkdir.parent_refn.handle = parent;
//   parent_refn.handle = attr->meta->dfh
 //  parent_refn.fs_id = fs_id; 
	parent_refn = resp_lookup.pinode_refn;
//   attrmask = ATTR_BASIC;
	attrmask = PVFS_ATTR_SYS_ALL_NOSIZE;
   attr.owner = 100;
   attr.group = 100;
   attr.perms = 1877;
   attr.objtype = PVFS_TYPE_DIRECTORY;
   credentials.perms = 1877;
   credentials.uid = 100;
   credentials.gid = 100;

	switch(nullCase){
		case 0:
   		return PVFS_sys_mkdir(NULL,parent_refn,attr, credentials, &resp_mkdir);
			break;
		case 1:
			parent_refn.handle = NULL;
   		return PVFS_sys_mkdir(name, parent_refn, attr, credentials, &resp_mkdir);
			break;
		case 2:
			parent_refn.fs_id = NULL;
   		return PVFS_sys_mkdir(name, parent_refn, attr, credentials, &resp_mkdir);
			break;
		case 3:
   		attr.owner = NULL;
   		return PVFS_sys_mkdir(name, parent_refn, attr, credentials, &resp_mkdir);
			break;
		case 4:
   		attr.group = NULL;
   		return PVFS_sys_mkdir(name ,parent_refn, attr, credentials, &resp_mkdir);
			break;
		case 5:
   		attr.perms = NULL;
   		return PVFS_sys_mkdir(name, parent_refn, attr, credentials, &resp_mkdir);
			break;
/* Note: not testing for the union objtype */
		case 6:
			credentials.uid = NULL;
   		return PVFS_sys_mkdir(name, parent_refn, attr, credentials, &resp_mkdir);
			break;
		case 7:
			credentials.gid = NULL;
   		return PVFS_sys_mkdir(name, parent_refn, attr, credentials, &resp_mkdir);
			break;
		case 8:
			credentials.perms = NULL;
   		return PVFS_sys_mkdir(name, parent_refn, attr, credentials, &resp_mkdir);
			break;
		case 9:
   		return PVFS_sys_mkdir(name, parent_refn, attr, credentials, NULL);
			break;
		default:
			fprintf(stderr,"Error - no more cases\n");
	}
	return -2;
}

/* Preconditions: none
 * Parameters: nullCase - the test case that is checked for this function
 * Postconditions: returns error code of readdir
 * Has 8 Test cases
 */
static int test_readdir(int nullCase){

   int ret;
                         
	PVFS_pinode_reference pinode_refn;
	PVFS_ds_position token;
	int pvfs_dirent_incount;
	PVFS_credentials credentials;
   PVFS_sysresp_readdir resp_readdir;

	int fs_id;
   PVFS_sysresp_lookup resp_lookup;
	char *name;

	name = (char *)malloc(sizeof(char)*100);
	name = strcpy(name,"name");

	fs_id = initialize_sysint();

   credentials.uid = 100;
   credentials.gid = 100;
   credentials.perms = PVFS_U_WRITE|PVFS_U_READ;
  	if((ret = PVFS_sys_lookup(fs_id,name,credentials,&resp_lookup))< 0){
		fprintf(stderr,"lookup failed %d\n",ret);
		return -1;
	}

   //req_readdir.pinode_refn.handle = handle;
//   pinode_refn.handle = resp_lookup->attr->meta->dfh;
 //  pinode_refn.fs_id = fs_id;
	pinode_refn = resp_lookup.pinode_refn;
   token = PVFS2_READDIR_START;
//   pvfs_dirent_incount = ndirs;
   pvfs_dirent_incount = 1;
    
   credentials.uid = 100;
   credentials.gid = 100;
   credentials.perms = 1877;
    
	switch(nullCase){
		case 0:
   		pinode_refn.handle = NULL;
  			return PVFS_sys_readdir(pinode_refn, token, pvfs_dirent_incount, credentials,&resp_readdir);
			break;
		case 1:
   		pinode_refn.fs_id = NULL;
  			return PVFS_sys_readdir(pinode_refn, token, pvfs_dirent_incount, credentials,&resp_readdir);
			break;
		case 2:
  			return PVFS_sys_readdir(pinode_refn, NULL, pvfs_dirent_incount, credentials,&resp_readdir);
			break;
		case 3:
  			return PVFS_sys_readdir(pinode_refn, token, NULL, credentials,&resp_readdir);
			break;
		case 4:
   		credentials.uid = NULL;
  			return PVFS_sys_readdir(pinode_refn, token, pvfs_dirent_incount, credentials,&resp_readdir);
			break;
		case 5:
   		credentials.gid = NULL;
  			return PVFS_sys_readdir(pinode_refn, token, pvfs_dirent_incount, credentials,&resp_readdir);
			break;
		case 6:
   		credentials.perms = NULL;
  			return PVFS_sys_readdir(pinode_refn, token, pvfs_dirent_incount, credentials,&resp_readdir);
			break;
		case 7:
  			return PVFS_sys_readdir(pinode_refn, token, pvfs_dirent_incount, credentials,NULL);
			break;
	}
	return -2;
}

/* Preconditions: none
 * Parameters: nullCase - the test case that is checked for this function
 * Postconditions: returns error code of readdir
 * Has 12 test cases
 */
static int test_create(int nullCase){
   int ret, fs_id;
   PVFS_object_attr attr;
   PVFS_credentials credentials;
   PVFS_sysresp_lookup resp_look;
   PVFS_sysresp_create resp_create;
   PVFS_sysresp_getattr resp_getattr;
	char *filename;	
	uint32_t attrmask;

	filename = (char *)malloc(sizeof(char)*100);
	filename = strcpy(filename,"name");

   credentials.uid = 100;
   credentials.gid = 100;
   credentials.perms = 1877;

	fs_id = initialize_sysint();

   ret = PVFS_sys_lookup(fs_id, filename, credentials, &resp_look);
   if (ret < 0)
   {
       printf("Lookup failed with errcode = %d\n", ret);
       return (-1);
   }

	switch(nullCase){
		case 0:
			return PVFS_sys_create(NULL, resp_look.pinode_refn, attr, credentials, &resp_create);
			break;
		case 1:
			resp_look.pinode_refn.handle = NULL;
			return PVFS_sys_create(filename, resp_look.pinode_refn, attr, credentials, &resp_create);
			break;
		case 2:
   		resp_look.pinode_refn.fs_id = NULL;
			return PVFS_sys_create(filename, resp_look.pinode_refn, attr, credentials, &resp_create);
			break;
		case 3:
			attr.owner = NULL;
			return PVFS_sys_create(filename, resp_look.pinode_refn, attr, credentials, &resp_create);
			break;
		case 4:
			attr.group = NULL;
			return PVFS_sys_create(filename, resp_look.pinode_refn, attr, credentials, &resp_create);
			break;
		case 5:
			attr.perms = NULL;
			return PVFS_sys_create(filename, resp_look.pinode_refn, attr, credentials, &resp_create);
			break;
		case 8:
			credentials.gid = NULL;
			return PVFS_sys_create(filename, resp_look.pinode_refn, attr, credentials, &resp_create);
			break;
		case 9:
			credentials.uid = NULL;
			return PVFS_sys_create(filename, resp_look.pinode_refn, attr, credentials, &resp_create);
			break;
		case 10:
			credentials.perms = NULL;
			return PVFS_sys_create(filename, resp_look.pinode_refn, attr, credentials, &resp_create);
			break;
		case 11:
			return PVFS_sys_create(filename, resp_look.pinode_refn, attr, credentials, NULL);
			break;
		default:
			fprintf(stderr,"Error - incorect case number \n");	
			return -3;
	}
	return -2;
}

/* Preconditions: none
 * Parameters: nullCase - the test case that is checked for this function
 * Postconditions: returns error code of readdir
 * Has 6 tset cases
 */
static int test_remove(int nullCase){
   PVFS_credentials credentials;
   PVFS_sysresp_lookup resp_look;
	char *filename;
	int ret;
	int fs_id;
	
	filename = (char *)malloc(sizeof(char)*100);
	filename = strcpy(filename,"name");

   credentials.uid = 100;
   credentials.gid = 100;
   credentials.perms = 1877;

	fs_id = initialize_sysint();

   ret = PVFS_sys_lookup(fs_id, filename, credentials, &resp_look);
   if (ret < 0)
   {
       printf("Lookup failed with errcode = %d\n", ret);
       return (-1);
   }
	switch(nullCase){
		case 0:
			return PVFS_sys_remove(NULL, resp_look.pinode_refn, credentials);
			break;
		case 1:
			resp_look.pinode_refn.handle = NULL;
			return PVFS_sys_remove(filename, resp_look.pinode_refn, credentials);
			break;
		case 2:
			resp_look.pinode_refn.fs_id = NULL;
			return PVFS_sys_remove(filename, resp_look.pinode_refn, credentials);
			break;
		case 3:
			credentials.uid = NULL;
			return PVFS_sys_remove(filename, resp_look.pinode_refn, credentials);
			break;
		case 4:
			credentials.gid = NULL;
			return PVFS_sys_remove(filename, resp_look.pinode_refn, credentials);
			break;
		case 5:
			credentials.perms = NULL;
			return PVFS_sys_remove(filename, resp_look.pinode_refn, credentials);
			break;
		default:
			fprintf(stderr,"Error: invalid case number \n");
	}
	return -2;
}

/* Preconditions: none
 * Parameters: nullCase - the test case that is checked for this function
 * Postconditions: returns error code of readdir
 */
static int test_rename(int nullCase){

//	return PVFS_sys_rename(old_name, old_parent_refn, new_name, new_parent_refn, credentials);
	return -2;
}

/* Preconditions: none
 * Parameters: nullCase - the test case that is checked for this function
 * Postconditions: returns error code of readdir
 */
static int test_symlink(int nullCase){
	return -2;
}

/* Preconditions: none
 * Parameters: nullCase - the test case that is checked for this function
 * Postconditions: returns error code of readdir
 */
static int test_readlink(int nullCase){
	return -2;
}

/* Preconditions: none
 * Parameters: nullCase - the test case that is checked for this function
 * Postconditions: returns error code of readdir
 * Has 9 test cases
 */
static int test_read(int nullCase){
   PVFS_credentials credentials;
   PVFS_sysresp_lookup resp_lk;
   PVFS_Request req_io;
   PVFS_sysresp_io resp_io;
   char *filename;
   char io_buffer[100];
	int fs_id, ret;

	filename = (char *)malloc(sizeof(char)*100);
	filename = strcpy(filename,"name");

   memset(&req_io,0,sizeof(PVFS_Request));
   memset(&resp_io,0,sizeof(PVFS_sysresp_io));

   credentials.uid = 100;
   credentials.gid = 100;
   credentials.perms = (PVFS_U_WRITE | PVFS_U_READ);
   memset(&resp_lk,0,sizeof(PVFS_sysresp_lookup));

	fs_id = initialize_sysint();

   ret = PVFS_sys_lookup(fs_id,filename, credentials, &resp_lk);
	if(ret < 0){
		debug_printf("test_pvfs_datatype_hvector: lookup failed "
                   "on %s\n",filename);
	}

	switch(nullCase){
		case 0:
			resp_lk.pinode_refn.handle = NULL;
			return PVFS_sys_read(resp_lk.pinode_refn, req_io, io_buffer, 100, credentials, &resp_io);
			break;
		case 1:
			resp_lk.pinode_refn.fs_id = NULL;
			return PVFS_sys_read(resp_lk.pinode_refn, req_io, io_buffer, 100, credentials, &resp_io);
			break;
		case 2:
			return PVFS_sys_read(resp_lk.pinode_refn, NULL, io_buffer, 100, credentials, &resp_io);
			break;
		case 3:
			return PVFS_sys_read(resp_lk.pinode_refn, req_io, NULL, 100, credentials, &resp_io);
			break;
		case 4:
			return PVFS_sys_read(resp_lk.pinode_refn, req_io, io_buffer, NULL, credentials, &resp_io);
			break;
		case 5:
			credentials.uid = NULL;
			return PVFS_sys_read(resp_lk.pinode_refn, req_io, io_buffer, 100, credentials, &resp_io);
			break;
		case 6:
			credentials.gid = NULL;
			return PVFS_sys_read(resp_lk.pinode_refn, req_io, io_buffer, 100, credentials, &resp_io);
			break;
		case 7:
			credentials.perms = NULL;
			return PVFS_sys_read(resp_lk.pinode_refn, req_io, io_buffer, 100, credentials, &resp_io);
			break;
		case 8:
			return PVFS_sys_read(resp_lk.pinode_refn, req_io, io_buffer, 100, credentials, NULL);
			break;
	}
	return -2;
}

/* Preconditions: none
 * Parameters: nullCase - the test case that is checked for this function
 * Postconditions: returns error code of readdir
 * Has 9 test cases
 */
static int test_write(int nullCase){
   PVFS_credentials credentials;
   PVFS_sysresp_lookup resp_lk;
   PVFS_Request req_io;
   PVFS_sysresp_io resp_io;
   char *filename;
   char io_buffer[100];
	int fs_id, ret;

	filename = (char *)malloc(sizeof(char)*100);
	filename = strcpy(filename,"name");

   memset(&req_io,0,sizeof(PVFS_Request));
   memset(&resp_io,0,sizeof(PVFS_sysresp_io));

   credentials.uid = 100;
   credentials.gid = 100;
   credentials.perms = (PVFS_U_WRITE | PVFS_U_READ);
   memset(&resp_lk,0,sizeof(PVFS_sysresp_lookup));

	fs_id = initialize_sysint();

   ret = PVFS_sys_lookup(fs_id,filename, credentials, &resp_lk);
	if(ret < 0){
		debug_printf("test_pvfs_datatype_hvector: lookup failed "
                   "on %s\n",filename);
	}

	switch(nullCase){
		case 0:
			resp_lk.pinode_refn.handle = NULL;
			return PVFS_sys_write(resp_lk.pinode_refn, req_io, io_buffer, 100, credentials, &resp_io);
			break;
		case 1:
			resp_lk.pinode_refn.fs_id = NULL;
			return PVFS_sys_write(resp_lk.pinode_refn, req_io, io_buffer, 100, credentials, &resp_io);
			break;
		case 2:
			return PVFS_sys_write(resp_lk.pinode_refn, NULL, io_buffer, 100, credentials, &resp_io);
			break;
		case 3:
			return PVFS_sys_write(resp_lk.pinode_refn, req_io, NULL, 100, credentials, &resp_io);
			break;
		case 4:
			return PVFS_sys_write(resp_lk.pinode_refn, req_io, io_buffer, NULL, credentials, &resp_io);
			break;
		case 5:
			credentials.uid = NULL;
			return PVFS_sys_write(resp_lk.pinode_refn, req_io, io_buffer, 100, credentials, &resp_io);
			break;
		case 6:
			credentials.gid = NULL;
			return PVFS_sys_write(resp_lk.pinode_refn, req_io, io_buffer, 100, credentials, &resp_io);
			break;
		case 7:
			credentials.perms = NULL;
			return PVFS_sys_write(resp_lk.pinode_refn, req_io, io_buffer, 100, credentials, &resp_io);
			break;
		case 8:
			return PVFS_sys_write(resp_lk.pinode_refn, req_io, io_buffer, 100, credentials, NULL);
			break;
	}
	return -2;
}

/* Preconditions: Parameters must be valid
 * Parameters: comm - special pts communicator, rank - the rank of the process, buf -  * (not used), rawparams - configuration information to specify which function to test
 * Postconditions: 0 if no errors and nonzero otherwise
 */
int test_null_params(MPI_Comm *comm, int rank, char *buf, void *rawparams){
	int ret = -1;
	null_params *params;

	params = (null_params *)rawparams;
    /* right now, the system interface isn't threadsafe, so we just want to run with one process. */
   if(rank == 0){
		if(params->p1 >= 0 && params->p2 >= 0){
			switch(params->p1){
				case 0:
					return test_system_init(params->p2);
					break;
				case 1:
					return test_lookup(params->p2);
					break;
				case 2:
					return test_getattr(params->p2);
					break;
				case 3:
					return test_setattr(params->p2);
					break;
				case 4:
					return test_mkdir(params->p2);
					break;
				case 5:
					return test_readdir(params->p2);
					break;
				case 6:
					return test_create(params->p2);
					break;
				case 7:
					return test_remove(params->p2);
					break;
				case 8:
					return test_rename(params->p2);
					break;
				case 9:
					return test_symlink(params->p2);
					break;
				case 10:
					return test_readlink(params->p2);
					break;
				case 11:
					return test_read(params->p2);
					break;
				case 12:
					return test_write(params->p2);
					break;
				default:
					fprintf(stderr,"Error: invalid param\n");
					return -2;
			}	
		}
	}
	return ret;
}
