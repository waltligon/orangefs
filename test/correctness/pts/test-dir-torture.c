/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <client.h>
#include <sys/time.h>
#include "mpi.h"
#include "pts.h"
#include "pvfs-helper.h"
#include "pvfs2-util.h"


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
    pvfs_mntlist mnt = { 0, NULL };	/* use pvfstab in cwd */

    memset(&resp_init, 0, sizeof(resp_init));

    ret = PVFS_util_parse_pvfstab(NULL, &mnt);
    if (ret < 0)
    {
	printf("Parsing error\n");
	return -1;
    }

    ret = PVFS_sys_initialize(mnt, CLIENT_DEBUG, &resp_init);
    if (ret < 0)
    {
	printf("PVFS_sys_initialize() failure. = %d\n", ret);
	return (ret);
    }

    return resp_init.fsid_list[0];
}

/*
 * handle:  handle of parent directory
 * fs_id:   our file system
 * depth:   how many directories to make at this level
 * rank:    rank in the mpi process group 
 */

int recursive_create_dir(PVFS_handle handle,
			 PVFS_fs_id fs_id,
			 int depth,
			 int ndirs,
			 int rank)
{
    int i;
    char name[PVFS_SEGMENT_MAX];
    PVFS_pinode_reference refn;
    PVFS_pinode_reference out_refn;

    /* base case: we've gone far enough */
    if (depth == 0)
	return 0;

    refn.handle = handle;
    refn.fs_id = fs_id;

    for (i = 0; i < ndirs; i++)
    {
	snprintf(name, PVFS_SEGMENT_MAX, "depth=%d-rank=%d-iter=%d", depth, rank, i);
	create_dir(refn, name, &out_refn);
	if (out_refn.handle < 0)
	{
	    return -1;
	}
	else
	{
	    recursive_create_dir(out_refn.handle, out_refn.fs_id,
                                 depth - 1, ndirs, rank);
	}
    }
    return 0;
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
int test_dir_torture(MPI_Comm * comm,
		     int rank,
		     char *buf,
		     void *rawparams)
{
    int ret = -1;
    PVFS_fs_id fs_id;
    PVFS_pinode_reference root_refn;
    generic_params *myparams = (generic_params *) rawparams;
    int nerrs = 0;

    fs_id = system_init();
    if (fs_id < 0)
    {
	printf("System initialization error\n");
	return (fs_id);
    }

    get_root(fs_id, &root_refn);

    /*
      this will make n^n directories, so be careful
      about running the test with mode=100
    */
    nerrs = recursive_create_dir(root_refn.handle, root_refn.fs_id,
                                 myparams->mode, myparams->mode, rank);

    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
	printf("finalizing sysint failed with errcode = %d\n", ret);
	return (-1);
    }

    return -nerrs;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
