/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <client.h>
#include <sys/time.h>
#include "mpi.h"
#include "pts.h"

extern int parse_pvfstab(char *filename,pvfs_mntlist *pvfstab_p);
 
/* 
 * helper function to initialize pvfs
 * doesn't take any parameters (relies heavily on some expected default information)
 *
 * returns: fs_id of a pvfs file system 
 * 	or -1 if error
 */
PVFS_fs_id system_init(void)
{
	PVFS_sysresp_init resp_init;
	int ret = -1;
	pvfs_mntlist mnt = {0,NULL}; /* use pvfstab in cwd */

	ret = parse_pvfstab(NULL, &mnt);
	if (ret < 0)
	{
		printf("Parsing error\n");
		return -1;
	}

	ret = PVFS_sys_initialize(mnt, &resp_init);
	if(ret < 0)
	{
		printf("PVFS_sys_initialize() failure. = %d\n", ret);
		return(ret);
	}

	return resp_init.fsid_list[0];
}
/*
 * helper function to get the root handle
 * fs_id:   fsid of our file system
 *
 * returns:  handle to the root directory
 * 	-1 if a problem
 */

PVFS_handle get_root(PVFS_fs_id fs_id)
{
	PVFS_sysreq_lookup req_look;
	PVFS_sysresp_lookup resp_look;
	int ret = -1;

	req_look.credentials.perms = 1877;
	req_look.name = malloc(2);/*null terminator included*/
	req_look.name[0] = '/';
	req_look.name[1] = '\0';
	req_look.fs_id = fs_id;
	printf("looking up the root handle for fsid = %d\n", req_look.fs_id);
	ret = PVFS_sys_lookup(&req_look,&resp_look);
	if (ret < 0)
	{
		printf("Lookup failed with errcode = %d\n", ret);
		return(-1);
	}
	return (PVFS_handle)resp_look.pinode_refn.handle;
}

/*
 * simple helper to make a pvfs2 directory
 *
 * parent:   handle of parent directory
 * fs_id:    fsid of filesystem on which parent dir exists
 * name:     name of directory to create
 *
 * returns a handle to the new directory
 *          -1 if some error happened
 */
PVFS_handle create_dir(PVFS_handle parent, PVFS_fs_id fs_id, char *name)
{
	PVFS_sysreq_mkdir req_mkdir;
	PVFS_sysresp_mkdir resp_mkdir;

	memset(&req_mkdir, 0, sizeof(req_mkdir));
	memset(&resp_mkdir, 0, sizeof(req_mkdir));

	int ret=-1;

	req_mkdir.entry_name = name; 
	req_mkdir.parent_refn.handle = parent;
	req_mkdir.parent_refn.fs_id = fs_id;
	req_mkdir.attrmask = ATTR_BASIC;
	req_mkdir.attr.owner = 100;
	req_mkdir.attr.group = 100;
	req_mkdir.attr.perms = 1877;
	req_mkdir.attr.objtype = ATTR_DIR;
	req_mkdir.credentials.perms = 1877;

	ret = PVFS_sys_mkdir(&req_mkdir,&resp_mkdir);
	if (ret < 0)
	{
		printf("mkdir failed\n");
		return(-1);
	}
	free(req_mkdir.entry_name);
	return (PVFS_handle)resp_mkdir.pinode_refn.handle;
}

/*
 * driver for the test
 * comm:	special pts communicator
 * rank:	rank among processes
 * buf:		stuff data in here ( not used )
 * rawparams:	our configuration information
 *
 * returns: 
 * 	0:  	all went well
 * 	nonzero: errors encountered making one or more directories
 */
int test_dir_torture(MPI_Comm *comm, int rank,  char *buf, void *rawparams) 
{
	int ret = -1;
	PVFS_fs_id fs_id;
	PVFS_handle root_handle, dir_handle;
	generic_params *myparams = (generic_params *)rawparams;
	int i, nerrs=0;
	char name[PVFS_NAME_MAX];

	fs_id = system_init();
	if (fs_id < 0)
	{
		printf("System initialization error\n");
		return(ret);
	}

	root_handle = get_root(fs_id);

	for (i=0; i<myparams->mode; i++) {
		snprintf(name, PVFS_NAME_MAX, "%d-%d-testdir", i, rank);
		dir_handle = create_dir(root_handle, fs_id, name);
		if (dir_handle < 0) ++nerrs;
	}

	ret = PVFS_sys_finalize();
	if (ret < 0)
	{
		printf("finalizing sysint failed with errcode = %d\n", ret);
		return (-1);
	}

	return -nerrs;
}
