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

extern pvfs_helper_t pvfs_helper;
extern int parse_pvfstab(char *filename,
			 pvfs_mntlist * pvfstab_p);

/*
 * handle:  handle of parent directory
 * fs_id:   our file system
 * depth:   how many directories to make at this level
 * rank:    rank in the mpi process group 
 */

static int remove_dirs(PVFS_handle parent_handle,
			 PVFS_fs_id fs_id,
			 int ndirs,
			 int rank)
{
    int i, ret = -1;
    char name[PVFS_NAME_MAX];
    PVFS_handle dir_handle;

    for (i = 0; i < ndirs; i++)
    {
	snprintf(name, PVFS_NAME_MAX, "/rank%d-iter%d", rank, i);
	ret = remove_dir(parent_handle, fs_id, name);
	if (ret < 0)
	{
	    return -1;
	}
    }
    return 0;
}

static int read_dirs(PVFS_handle handle,
			 PVFS_fs_id fs_id,
			 int ndirs,
			 int rank)
{
    int i, iter, ret;

    PVFS_sysreq_readdir req_readdir;
    PVFS_sysresp_readdir resp_readdir;

    memset(&resp_readdir,0,sizeof(PVFS_sysresp_readdir));
    memset(&req_readdir,0,sizeof(PVFS_sysreq_readdir));

    req_readdir.pinode_refn.handle = handle;
    req_readdir.pinode_refn.fs_id = fs_id;
    req_readdir.token = PVFS2_READDIR_START;
    req_readdir.pvfs_dirent_incount = ndirs;

    req_readdir.credentials.uid = 100;
    req_readdir.credentials.gid = 100;
    req_readdir.credentials.perms = 1877;

    /* call readdir */
    ret = PVFS_sys_readdir(&req_readdir,&resp_readdir);
    if (ret < 0)
    {
	printf("readdir failed with errcode = %d\n", ret);
	return(-1);
    }

    /* examine the results */

    if (resp_readdir.pvfs_dirent_outcount != ndirs)
    {
	debug_printf("we were expecting %d directories, and recieved %d\n",ndirs, resp_readdir.pvfs_dirent_outcount);

	free(resp_readdir.dirent_array);
	return -1;
    }

    /* check each of our directories to ensure that they have sane names */

    for (i = 0; i < ndirs; i++)
    {
	if (0 > sscanf(resp_readdir.dirent_array[i].d_name, "rank%d-iter%d", &rank, &iter))
	{
	    debug_printf("unable to read directory name iter: %d\n",i);
	    free(resp_readdir.dirent_array);
	    return -1;
	}

	if ( (iter > ndirs) || (iter < 0))
	{
	    debug_printf("invalid directory name %s\n",resp_readdir.dirent_array[i].d_name);
	    free(resp_readdir.dirent_array);
	    return -1;
	}
    }

    free(resp_readdir.dirent_array);
    return 0;
}

static int create_dirs(PVFS_handle handle,
			 PVFS_fs_id fs_id,
			 int ndirs,
			 int rank)
{
    int i;
    char name[PVFS_NAME_MAX];
    PVFS_handle dir_handle;

    for (i = 0; i < ndirs; i++)
    {
	snprintf(name, PVFS_NAME_MAX, "rank%d-iter%d", rank, i);
	dir_handle = create_dir(handle, fs_id, name);
	if (dir_handle < 0)
	{
	    return -1;
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
 * 	nonzero: errors encountered making reading, or removing directories
 */
int test_dir_operations(MPI_Comm * comm,
		     int rank,
		     char *buf,
		     void *rawparams)
{
    int ret = -1;
    PVFS_fs_id fs_id = 0;
    PVFS_handle root_handle, dir_handle;
    generic_params *myparams = (generic_params *) rawparams;
    int nerrs = 0;
    char name[PVFS_NAME_MAX];

    if (rank == 0)
    {
	if (initialize_sysint() || (!pvfs_helper.initialized))
	{
	    printf("System initialization error\n");
	    return (-1);
	}
	fs_id = pvfs_helper.resp_init.fsid_list[0];
	printf("fs_id: %ld\n",fs_id );
    }

    MPI_Bcast(&fs_id, 1, MPI_LONG_INT, 0, *comm );
    printf("rank: %d  fs_id: %ld\n", rank, fs_id );


    root_handle = get_root(fs_id);

    /* setup a dir in the root directory to do tests in (so the root dir is
     * less cluttered)
     *
     */

    memset(name,0,PVFS_NAME_MAX);
    snprintf(name, PVFS_NAME_MAX, "dir_op_test");
    if (rank == 0)
    {
	dir_handle = create_dir(root_handle, fs_id, name);
	if (dir_handle < 0)
	{
	    return -1;
	}
    }
    MPI_Barrier(*comm);
    if (rank != 0)
    {
	/* for everyone that didn't create the dir entry, we should get the 
	 * handle via lookup */
	dir_handle = lookup_name(name, root_handle);
	if (dir_handle < 0)
	{
	    return -1;
	}
    }

    ret = create_dirs(dir_handle, fs_id, myparams->mode, rank);
    if (ret < 0)
    {
	printf("creating directories failed with errcode = %d\n", ret);
	return (-1);
    }

    ret = read_dirs(dir_handle, fs_id, myparams->mode, rank);
    if (ret < 0)
    {
	printf("reading directories failed with errcode = %d\n", ret);
	return (-1);
    }

    ret = remove_dirs(dir_handle, fs_id, myparams->mode, rank);
    if (ret < 0)
    {
	printf("removing directories failed with errcode = %d\n", ret);
	return (-1);
    }

    if (rank == 0)
    {
	/* remove the test directory */
	ret = remove_dir(root_handle, fs_id, name);
	if (ret < 0)
	{
	    return -1;
	}
    }

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
