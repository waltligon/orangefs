/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* 
 * test-finalized: the system is initialized and imediatly after finalize is called, then all other functions are tested thereafter
 * Author: Michael Speth
 * Date: 6/26/2003
 * Tab Size: 3
 */

#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>

#include "client.h"
#include "mpi.h"
#include "pts.h"
#include "pvfs-helper.h"
#include "null_params.h"

extern pvfs_helper_t pvfs_helper;


/* Preconditions: none
 * Parameters: none
 * Postconditions: returns the error code given by lookup - thats if it doesn't segfault or other catostrophic failure
 * Hase 1 test cases
 */
static int test_lookup(void)
{
    int fs_id, ret;
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_lookup;
    char *name;

    ret = -2;
    name = (char *) malloc(sizeof(char) * 100);
    name = strcpy(name, "name");

    if (initialize_sysint() < 0)
    {
	debug_printf("UNABLE TO INIT THE SYSTEM INTERFACE\n");
	return -1;
    }
    PVFS_sys_finalize();
    fs_id = pvfs_helper.resp_init.fsid_list[0];

    credentials.uid = 100;
    credentials.gid = 100;

    ret = PVFS_sys_lookup(-1, name, credentials, &resp_lookup);
    return ret;
}

/* Preconditions: none
 * Parameters: none
 * Postconditions: returns error from getattr
 * Has 2 Test Cases
 */
static int test_getattr(void)
{
    int fs_id, ret;
    PVFS_credentials credentials;
    PVFS_pinode_reference pinode_refn;
    uint32_t attrmask;
    PVFS_sysresp_lookup resp_lookup;
    PVFS_sysresp_getattr resp_getattr;
    char *name;

    ret = -2;
    name = (char *) malloc(sizeof(char) * 100);
    name = strcpy(name, "name");

    if (initialize_sysint() < 0)
    {
	debug_printf("UNABLE TO INIT THE SYSTEM INTERFACE\n");
	return -1;
    }
    PVFS_sys_finalize();
    fs_id = pvfs_helper.resp_init.fsid_list[0];

    credentials.uid = 100;
    credentials.gid = 100;

    if ((ret = PVFS_sys_lookup(fs_id, name, credentials, &resp_lookup)) < 0)
    {
	fprintf(stderr, "lookup failed %d\n", ret);
	return ret;
    }

    pinode_refn = resp_lookup.pinode_refn;
    attrmask = PVFS_ATTR_SYS_ALL_NOSIZE;

    ret = PVFS_sys_getattr(pinode_refn, attrmask, credentials, &resp_getattr);
    return ret;
}

/* Preconditions: None
 * Parameters: none
 * Postconditions:
 */
static int test_setattr(void)
{
    return -2;
}

/* Preconditions: None
 * Parameters: none
 * Postconditions: returns the error returned by mkdir
 * Has 2 test cases
 */
static int test_mkdir(void)
{
    PVFS_pinode_reference parent_refn;
    uint32_t attrmask;
    PVFS_sys_attr attr;
    PVFS_sysresp_mkdir resp_mkdir;

    int ret = -2;
    int fs_id;
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_lookup;
    char *name;

    name = (char *) malloc(sizeof(char) * 100);
    name = strcpy(name, "name");

    if (initialize_sysint() < 0)
    {
	debug_printf("UNABLE TO INIT THE SYSTEM INTERFACE\n");
	return -1;
    }
    PVFS_sys_finalize();
    fs_id = pvfs_helper.resp_init.fsid_list[0];

    credentials.uid = 100;
    credentials.gid = 100;
    if ((ret = PVFS_sys_lookup(fs_id, name, credentials, &resp_lookup)) < 0)
    {
	fprintf(stderr, "lookup failed %d\n", ret);
	return -1;
    }

    parent_refn = resp_lookup.pinode_refn;
    attrmask = PVFS_ATTR_SYS_ALL_SETABLE;
    attr.owner = 100;
    attr.group = 100;
    attr.perms = 1877;
    attr.atime = attr.mtime = attr.ctime = 
	time(NULL);
    credentials.uid = 100;
    credentials.gid = 100;

    ret = PVFS_sys_mkdir(name, parent_refn, attr, credentials, &resp_mkdir);
    return ret;
}

/* Preconditions: none
 * Parameters: none
 * Postconditions: returns error code of readdir
 * Has 2 Test cases
 */
static int test_readdir(void)
{

    int ret;

    PVFS_pinode_reference pinode_refn;
    PVFS_ds_position token;
    int pvfs_dirent_incount;
    PVFS_credentials credentials;
    PVFS_sysresp_readdir resp_readdir;

    int fs_id;
    PVFS_sysresp_lookup resp_lookup;
    char *name;

    ret = -2;
    name = (char *) malloc(sizeof(char) * 100);
    name = strcpy(name, "name");

    if (initialize_sysint() < 0)
    {
	debug_printf("UNABLE TO INIT THE SYSTEM INTERFACE\n");
	return -1;
    }
    PVFS_sys_finalize();
    fs_id = pvfs_helper.resp_init.fsid_list[0];

    credentials.uid = 100;
    credentials.gid = 100;
    if ((ret = PVFS_sys_lookup(fs_id, name, credentials, &resp_lookup)) < 0)
    {
	fprintf(stderr, "lookup failed %d\n", ret);
	return -1;
    }

    pinode_refn = resp_lookup.pinode_refn;
    token = PVFS2_READDIR_START;
    pvfs_dirent_incount = 1;

    credentials.uid = 100;
    credentials.gid = 100;

    ret =
	PVFS_sys_readdir(pinode_refn, token, pvfs_dirent_incount, credentials,
			 &resp_readdir);
    return ret;
}

static int test_finalize(void)
{
    int ret = -2;
    if (initialize_sysint() < 0)
    {
	debug_printf("UNABLE TO INIT THE SYSTEM INTERFACE\n");
	return -1;
    }
    PVFS_sys_finalize();
    ret = PVFS_sys_finalize();
    return ret;
}

/* Preconditions: none
 * Parameters: none
 * Postconditions: returns error code of readdir
 * Has 2 test cases
 */
static int test_create(void)
{
    int ret, fs_id;
    PVFS_sys_attr attr;
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_look;
    PVFS_sysresp_create resp_create;
    char *filename;

    ret = -2;
    filename = (char *) malloc(sizeof(char) * 100);
    filename = strcpy(filename, "name");

    credentials.uid = 100;
    credentials.gid = 100;

    if (initialize_sysint() < 0)
    {
	debug_printf("UNABLE TO INIT THE SYSTEM INTERFACE\n");
	return -1;
    }
    PVFS_sys_finalize();
    fs_id = pvfs_helper.resp_init.fsid_list[0];

    ret = PVFS_sys_lookup(fs_id, "/", credentials, &resp_look);
    if (ret < 0)
    {
	printf("Lookup failed with errcode = %d\n", ret);
	return (-1);
    }

    ret =
	PVFS_sys_create(filename, resp_look.pinode_refn, attr, credentials,
			&resp_create);
    return ret;
}

/* Preconditions: none
 * Parameters: nnoe
 * Postconditions: returns error code of readdir
 * Has 2 tset cases
 */
static int test_remove(void)
{
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_look;
    char *filename;
    int ret;
    int fs_id;

    ret = -2;
    filename = (char *) malloc(sizeof(char) * 100);
    filename = strcpy(filename, "name");

    credentials.uid = 100;
    credentials.gid = 100;

    if (initialize_sysint() < 0)
    {
	debug_printf("UNABLE TO INIT THE SYSTEM INTERFACE\n");
	return -1;
    }
    PVFS_sys_finalize();
    fs_id = pvfs_helper.resp_init.fsid_list[0];

    ret = PVFS_sys_lookup(fs_id, filename, credentials, &resp_look);
    if (ret < 0)
    {
	printf("Lookup failed with errcode = %d\n", ret);
	return (-1);
    }
    ret = PVFS_sys_remove(filename, resp_look.pinode_refn, credentials);
    return ret;
}

/* Preconditions: none
 * Parameters: none
 * Postconditions: returns error code of readdir
 */
static int test_rename(void)
{

//      return PVFS_sys_rename(old_name, old_parent_refn, new_name, new_parent_refn, credentials);
    return -2;
}

/* Preconditions: none
 * Parameters: none
 * Postconditions: returns error code of readdir
 */
static int test_symlink(void)
{
    return -2;
}

/* Preconditions: none
 * Parameters: none
 * Postconditions: returns error code of readdir
 */
static int test_readlink(void)
{
    return -2;
}

/* Preconditions: none
 * Parameters: none
 * Postconditions: returns error code of readdir
 * Has 2 test cases
 */
static int test_read(void)
{
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_lk;
    PVFS_Request req_io;
    PVFS_sysresp_io resp_io;
    char *filename;
    char io_buffer[100];
    int fs_id, ret;

    filename = (char *) malloc(sizeof(char) * 100);
    filename = strcpy(filename, "name");

    memset(&req_io, 0, sizeof(PVFS_Request));
    memset(&resp_io, 0, sizeof(PVFS_sysresp_io));

    credentials.uid = 100;
    credentials.gid = 100;
    memset(&resp_lk, 0, sizeof(PVFS_sysresp_lookup));

    if (initialize_sysint() < 0)
    {
	debug_printf("UNABLE TO INIT THE SYSTEM INTERFACE\n");
	return -1;
    }
    PVFS_sys_finalize();
    fs_id = pvfs_helper.resp_init.fsid_list[0];

    ret = PVFS_sys_lookup(fs_id, filename, credentials, &resp_lk);
    if (ret < 0)
    {
	debug_printf("test_pvfs_datatype_hvector: lookup failed "
		     "on %s\n", filename);
    }

    /* TODO: use memory datatype when ready */
    ret =
	PVFS_sys_read(resp_lk.pinode_refn, req_io, 0, io_buffer, NULL, credentials,
		      &resp_io);
    return ret;
}

/* Preconditions: none
 * Parameters: none
 * Postconditions: returns error code of readdir
 * Has 2 test cases
 */
static int test_write(void)
{
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_lk;
    PVFS_Request req_io;
    PVFS_sysresp_io resp_io;
    char *filename;
    char io_buffer[100];
    int fs_id, ret;

    filename = (char *) malloc(sizeof(char) * 100);
    filename = strcpy(filename, "name");

    memset(&req_io, 0, sizeof(PVFS_Request));
    memset(&resp_io, 0, sizeof(PVFS_sysresp_io));

    credentials.uid = 100;
    credentials.gid = 100;
    memset(&resp_lk, 0, sizeof(PVFS_sysresp_lookup));

    if (initialize_sysint() < 0)
    {
	debug_printf("UNABLE TO INIT THE SYSTEM INTERFACE\n");
	return -1;
    }
    PVFS_sys_finalize();
    fs_id = pvfs_helper.resp_init.fsid_list[0];

    ret = PVFS_sys_lookup(fs_id, filename, credentials, &resp_lk);
    if (ret < 0)
    {
	debug_printf("test_pvfs_datatype_hvector: lookup failed "
		     "on %s\n", filename);
    }

    /* TODO: use memory datatype when ready */
    ret =
	PVFS_sys_write(resp_lk.pinode_refn, req_io, 0, io_buffer, NULL, credentials,
		       &resp_io);
    return ret;
}

/* Preconditions: Parameters must be valid
 * Parameters: comm - special pts communicator, rank - the rank of the process, buf -  * (not used), rawparams - configuration information to specify which function to test
 * Postconditions: 0 if no errors and nonzero otherwise
 */
int test_finalized(MPI_Comm * comm,
		   int rank,
		   char *buf,
		   void *rawparams)
{
    int ret = -1;
    null_params *params;

    params = (null_params *) rawparams;
    /* right now, the system interface isn't threadsafe, so we just want to run with one process. */
    if (rank == 0)
    {
	if (params->p1 >= 0 && params->p2 >= 0)
	{
	    switch (params->p1)
	    {
	    case 0:
		fprintf(stderr, "[test_finalized] test_lookup %d\n",
			params->p2);
		ret = test_lookup();
		if(ret >= 0){
		    PVFS_perror("test_lookup",ret);
		    return ret;
		}
		return 0;
		break;
	    case 1:
		fprintf(stderr, "[test_finalized] test_getattr %d\n",
			params->p2);
		ret = test_getattr();
		if(ret >= 0){
		    PVFS_perror("test_getattr",ret);
		    return ret;
		}
		return 0;
		break;
	    case 2:
		fprintf(stderr, "[test_finalized] test_setattr %d\n",
			params->p2);
		ret = test_setattr();
		if(ret >= 0){
		    PVFS_perror("test_setattr",ret);
		    return ret;
		}
		return 0;
		break;
	    case 3:
		fprintf(stderr, "[test_finalized] test_mkdir %d\n", params->p2);
		ret = test_mkdir();
		if(ret >= 0){
		    PVFS_perror("test_mkdir",ret);
		    return ret;
		}
		return 0;
		break;
	    case 4:
		fprintf(stderr, "[test_finalized] test_readdir %d\n",
			params->p2);
		ret = test_readdir();
		if(ret >= 0){
		    PVFS_perror("test_readdir",ret);
		    return ret;
		}
		return 0;
		break;
	    case 5:
		fprintf(stderr, "[test_finalized] test_create %d\n",
			params->p2);
		ret = test_create();
		if(ret >= 0){
		    PVFS_perror("test_create",ret);
		    return ret;
		}
		return 0;
		break;
	    case 6:
		fprintf(stderr, "[test_finalized] test_remove %d\n",
			params->p2);
		ret = test_remove();
		if(ret >= 0){
		    PVFS_perror("test_remove",ret);
		    return ret;
		}
		return 0;
		break;
	    case 7:
		fprintf(stderr, "[test_finalized] test_rename %d\n",
			params->p2);
		ret = test_rename();
		if(ret >= 0){
		    PVFS_perror("test_rename",ret);
		    return ret;
		}
		return 0;
		break;
	    case 8:
		fprintf(stderr, "[test_finalized] test_symlink %d\n",
			params->p2);
		ret = test_symlink();
		if(ret >= 0){
		    PVFS_perror("test_symlink",ret);
		    return ret;
		}
		return 0;
		break;
	    case 9:
		fprintf(stderr, "[test_finalized] test_readlink %d\n",
			params->p2);
		ret = test_readlink();
		if(ret >= 0){
		    PVFS_perror("test_readlink",ret);
		    return ret;
		}
		return 0;
		break;
	    case 10:
		fprintf(stderr, "[test_finalized] test_read %d\n", params->p2);
		ret = test_read();
		if(ret >= 0){
		    PVFS_perror("test_read",ret);
		    return ret;
		}
		return 0;
		break;
	    case 11:
		fprintf(stderr, "[test_finalized] test_write %d\n", params->p2);
		ret = test_write();
		if(ret >= 0){
		    PVFS_perror("test_write",ret);
		    return ret;
		}
		return 0;
		break;
	    case 12:
		fprintf(stderr, "[test_finalized] test_finalize %d\n",
			params->p2);
		ret = test_finalize();
		if(ret >= 0){
		    PVFS_perror("test_finalize",ret);
		    return ret;
		}
		return 0;
		break;
	    default:
		fprintf(stderr, "Error: invalid param %d\n", params->p1);
		return -2;
	    }
	}
    }
    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
