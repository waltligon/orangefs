/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* 
 * test-invalid-files: tests behavior of all sys-init functions with
 * an invalid file Author: Michael Speth Date: 6/25/2003 Tab Size: 3
 */
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#include "client.h"
#include "mpi.h"
#include "pts.h"
#include "pvfs2-util.h"
#include "pvfs-helper.h"
#include "null_params.h"
#include "test-concurrent-meta.h"

/**
 * Calls lookup.
 * Preconditions: none
 * Postconditions: calls lookup on the file specified
 * @param name the file
 * @param fs_id the file system id
 * @returns 0 on success and an error code on failure
 */
static int lookup(char *name, int fs_id)
{
    int ret;
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_lookup;

    ret = -2;

    PVFS_util_gen_credentials(&credentials);
    if ((ret = PVFS_sys_lookup(
             fs_id, name, credentials,
             &resp_lookup, PVFS2_LOOKUP_LINK_NO_FOLLOW)) < 0)
    {
        fprintf(stderr, "lookup failed %d\n", ret);
        return ret;
    }
    return 0;
}

/**
 * Gets the attributes for the file specified by name.
 * Preconditions: none
 * Postconditions: gets the attribs of the file
 * @param name the file
 * @param fs_id the file system id
 * @returns 0 on success and an error code on failure
 */
static int getattr(char *name, int fs_id)
{
    int ret;
    PVFS_credentials credentials;
    PVFS_object_ref pinode_refn;
    PVFS_sysresp_getattr resp_getattr;
    uint32_t attrmask;
    PVFS_sysresp_lookup resp_lookup;

    ret = -2;

    PVFS_util_gen_credentials(&credentials);
    if ((ret = PVFS_sys_lookup(
             fs_id, name, credentials,
             &resp_lookup, PVFS2_LOOKUP_LINK_NO_FOLLOW)) < 0)
    {
        fprintf(stderr, "lookup failed %d\n", ret);
        return ret;
    }

    pinode_refn = resp_lookup.ref;
    attrmask = PVFS_ATTR_SYS_ALL_NOSIZE;

    ret = PVFS_sys_getattr(pinode_refn, attrmask, credentials, &resp_getattr);

    return ret;
}

/**
 * Removes the file or directory specified. 
 * Preconditions: none
 * Postconditions: removes the file or directory specified
 * @param name the file or directory to be removed
 * @param fs_id the file system id
 * @returns 0 on success and an error code on failure
 */
static int remove_file_dir(char *name, int fs_id)
{
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_look;
    int ret;

    ret = -2;

    PVFS_util_gen_credentials(&credentials);

    ret = PVFS_sys_lookup(fs_id, name, credentials,
                          &resp_look, PVFS2_LOOKUP_LINK_NO_FOLLOW);
    if (ret < 0)
    {
        printf("Lookup failed with errcode = %d\n", ret);
        return (-1);
    }
    ret = PVFS_sys_remove(name, resp_look.ref, credentials);

    return ret;
}

/**
 * Calls the List directory function. <br>
 * Preconditions: test_dir must be created <br>
 * Postconditions: gets the directories and files in the directory listed
 * @param test_dir the directory to list
 * @param fs_id the file system id
 * @returns 0 on success and an error code on failure
 */
static int list_dir(char *test_dir, int fs_id)
{
    int ret;

    PVFS_object_ref pinode_refn;
    PVFS_ds_position token;
    PVFS_sysresp_readdir resp_readdir;
    int pvfs_dirent_incount;
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_lookup;

    ret = -2;

    PVFS_util_gen_credentials(&credentials);

    if ((ret = PVFS_sys_lookup(
             fs_id, test_dir, credentials,
             &resp_lookup, PVFS2_LOOKUP_LINK_NO_FOLLOW)) < 0)
    {
        fprintf(stderr, "lookup failed %d\n", ret);
        return -1;
    }

    pinode_refn = resp_lookup.ref;
    token = PVFS_READDIR_START;
    pvfs_dirent_incount = 1;

    ret = PVFS_sys_readdir(pinode_refn, token, pvfs_dirent_incount,
                             credentials, &resp_readdir);

    return ret;
}

/**
 * Creates a file with the specified file name in the specified directory. <br>
 * Preconditions: the directory must be a valid pre-existing directory <br>
 * Postconditions: creates a file
 * @param filename the string representation of the file to be created
 * @param directory the string representation of the directory
 * @param fs_id the file system id
 * @returns 0 on success and an error code on failure
 */
static int create_file(char *filename, char *directory, int fs_id)
{
    int ret;
    PVFS_sys_attr attr;
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_look;
    PVFS_sysresp_create resp_create;

    ret = -2;

    PVFS_util_gen_credentials(&credentials);

    attr.mask = PVFS_ATTR_SYS_ALL_NOSIZE;
    attr.owner = credentials.uid;
    attr.group = credentials.gid;
    attr.perms = 1877;
    attr.atime = attr.mtime = attr.ctime = 0xdeadbeef;

    ret = PVFS_sys_lookup(fs_id, directory, credentials,
                          &resp_look, PVFS2_LOOKUP_LINK_NO_FOLLOW);
    if (ret < 0)
    {
        printf("Lookup failed with errcode = %d\n", ret);
        return (-1);
    }

    ret = PVFS_sys_create(filename, resp_look.ref,
                          attr, credentials, NULL, &resp_create);
   return ret;
}

/**
 * Creates the directory specified by name.  Note: the root directory is "/".
 * Preconditions: must have a root directory labled "/" <br>
 * Postconditions: Creates a directory specified by name 
 * @param name the directory to be created
 * @param fs_id the file system id
 * @returns 0 on succes and error code on failure
 */
static int create_dir2(char *name, int fs_id)
{
    PVFS_object_ref parent_refn;
    PVFS_sys_attr attr;
    PVFS_sysresp_mkdir resp_mkdir;

    int ret = -2;
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_lookup;

    PVFS_util_gen_credentials(&credentials);
    if ((ret = PVFS_sys_lookup(
             fs_id, "/", credentials,
             &resp_lookup, PVFS2_LOOKUP_LINK_NO_FOLLOW)) < 0)
    {
        fprintf(stderr, "lookup failed %d\n", ret);
        return -1;
    }

    parent_refn = resp_lookup.ref;
    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
    attr.owner = credentials.uid;
    attr.group = credentials.gid;
    attr.perms = 1877;
    attr.atime = attr.mtime = attr.ctime =
	time(NULL);

    ret = PVFS_sys_mkdir(name, parent_refn, attr, credentials, &resp_mkdir);
    return ret;
}

/* Preconditions: Parameters must be valid
 * Parameters: comm - special pts communicator, rank - the rank of the process, buf -  * (not used), rawparams - configuration information to specify which function to test
 * Postconditions: 0 if no errors and nonzero otherwise
 */
int test_concurrent_meta(MPI_Comm * comm __unused,
		       int rank,
		       char *buf __unused,
		       void *rawparams)
{
    int ret = -1;
    int fs_id, i;
    null_params *params;

    params = (null_params *) rawparams;
    if (initialize_sysint() < 0)
    {
        debug_printf("UNABLE TO INIT THE SYSTEM INTERFACE\n");
        return -1;
    }
    fs_id = pvfs_helper.fs_id;

    /* right now, the system interface isn't threadsafe, so we just want to run with one process. */
    if (rank == 0)
    {

	if (params->p1 >= 0)
	{
	    switch (params->p1)
	    {
	    case 0:
		fprintf(stderr, "[test_concurrent_meta] repeatedly listing dirs while anotehr process adds/remove files/dirs %d\n", params->p2);
		for(i = 0; i < 100; i++)
		{
		    ret = list_dir("/test_dir",fs_id);
		    if(ret >= 0){
			PVFS_perror("list_dir",ret);
			//return ret;
		    }
		}
		return 0;
	    case 1:
		fprintf(stderr,"[test_concurrent_meta] get attribs while removing\n");
		for(i = 0; i < 100; i++)
		{
		    ret = getattr("/test_dir/test_file2",fs_id);
		    if(ret < 0)
		    {
			PVFS_perror("getattr\n",ret);
			//return ret;
		    }
		}
		return 0;
	    case 2:
		fprintf(stderr,"[test_concurrent_meta] lookup files when created\n");
		for(i = 0; i < 100; i++)
		{
		    ret = lookup("/test_dir/test_file3",fs_id);
		    if(ret < 0)
		    {
			PVFS_perror("lookup\n",ret);
			//return ret;
		    }
		}
		return 0;
	    case 3:
		fprintf(stderr,"[test_concurrent_meta] lookup files when dir destyd\n");
		for(i = 0; i < 100; i++)
		{
		    ret = lookup("/test_dir/test_file3",fs_id);
		    if(ret < 0)
		    {
			PVFS_perror("lookup\n",ret);
			//return ret;
		    }
		}
		return 0;
	    case 99:
		fprintf(stderr,"[test_concurrent_meta] setup directory\n");
		ret = create_dir2("test_dir",fs_id);
		return ret;
	    default:
		fprintf(stderr, "Error: invalid param %d\n", params->p1);
		return -2;
	    }
	}
    }

    if (rank == 1)
    {
	if (params->p1 >= 0)
	{
	    switch (params->p1)
	    {
	    case 0:
		fprintf(stderr, "[test_concurrent_meta] repeatedly listing dirs while anotehr process adds/remove files/dirs %d\n", params->p2);
		for(i = 0; i < 100; i++)
		{
		    ret = create_file("test_file","/test_dir",fs_id);
		    if(ret >= 0){
		    	PVFS_perror("create_file",ret);
		    	return ret;
		    }
		    ret = remove_file_dir("/test_dir/test_file",fs_id);
		    if(ret >= 0){
			PVFS_perror("remove",ret);
			return ret;
		    }
		}
		return 0;
	    case 1:
		fprintf(stderr, "[test_concurrent_meta] repeatedly listing dirs while anotehr process adds/remove files/dirs %d\n", params->p2);
		for(i = 0; i < 100; i++)
		{
		    ret = create_file("test_file2","/test_dir",fs_id);
		    if(ret >= 0){
		    	PVFS_perror("create_file",ret);
		    	return ret;
		    }
		    ret = remove_file_dir("/test_dir/test_file2",fs_id);
		    if(ret >= 0){
			PVFS_perror("remove",ret);
			return ret;
		    }
		}
		return 0;
	    case 2:
		fprintf(stderr, "[test_concurrent_meta] repeat lookups %d\n", params->p2);
		for(i = 0; i < 100; i++)
		{
		    ret = create_file("test_file3","/test_dir",fs_id);
		    if(ret >= 0){
		    	PVFS_perror("create_file",ret);
		    	return ret;
		    }
		    ret = remove_file_dir("/test_dir/test_file3",fs_id);
		    if(ret >= 0){
			PVFS_perror("remove",ret);
			return ret;
		    }
		}
		return 0;
	    case 3:
		fprintf(stderr, "[test_concurrent_meta] repeat lookups on dir destyd %d\n", params->p2);
		ret = remove_file_dir("/test_dir",fs_id);
		if(ret >= 0){
		    PVFS_perror("remove",ret);
		    return ret;
		}
		return 0;
	    }
	}
    }

/*     finalize_sysint(); */
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
