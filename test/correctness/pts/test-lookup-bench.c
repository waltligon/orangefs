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

extern int parse_pvfstab(char *filename,
			 pvfs_mntlist * pvfstab_p);

/*
 * simple helper to lookup a handle given a filename
 *
 * parent:   handle of parent directory
 * fs_id:    fsid of filesystem on which parent dir exists
 * name:     name of directory to create
 *
 * returns a handle to the new directory
 *          -1 if some error happened
 */
PVFS_handle lookup_name(char *name,
		       PVFS_fs_id fs_id)
{
    PVFS_sysreq_lookup req_lookup;
    PVFS_sysresp_lookup resp_lookup;

    int ret = -1;

    memset(&req_lookup, 0, sizeof(req_lookup));
    memset(&resp_lookup, 0, sizeof(req_lookup));


    req_lookup.name = name;
    req_lookup.fs_id = fs_id;
    req_lookup.credentials.uid = 100;
    req_lookup.credentials.gid = 100;
    req_lookup.credentials.perms = PVFS_U_WRITE|PVFS_U_READ;

    ret = PVFS_sys_lookup(&req_lookup,&resp_lookup);
    if (ret < 0)
    {
       printf("Lookup failed with errcode = %d\n", ret);
       return(-1);
    }

    return (PVFS_handle) resp_lookup.pinode_refn.handle;
}

/*
 * handle:  handle of parent directory
 * fs_id:   our file system
 * depth:   how many directories to make at this level
 * rank:    rank in the mpi process group 
 */

int do_create_lookup(PVFS_handle handle,
			 PVFS_fs_id fs_id,
			 int depth,
			 int ndirs,
			 int rank)
{
    int i;
    char name[PVFS_NAME_MAX];
    char path[PVFS_NAME_MAX]; /*same as name except it has a slash prepending the path*/
    PVFS_handle dir_handle, lookup_handle;
    double before, after, running_total = 0, max=0.0, min = 10000.0, total = 0, current;

    /* base case: we've gone far enough */
    if (depth == 0)
	return 0;

    for (i = 0; i < ndirs; i++)
    {
	snprintf(name, PVFS_NAME_MAX, "depth=%d-rank=%d-iter=%d", depth, rank, i);
	snprintf(path, PVFS_NAME_MAX, "/%s", name);
	dir_handle = create_dir(handle, fs_id, name);
	if (dir_handle < 0)
	{
	    return -1;
	}
	/* lookup the directory we just created */
	before = MPI_Wtime();
	lookup_handle = lookup_name(path, fs_id);
	after = MPI_Wtime();
	if (lookup_handle != dir_handle)
	    return -1;
	current = after - before;
	running_total += current;
	if (max < current)
	{
	    max = current;
	}
	if (min > current)
	{
	    min = current;
	}
	total++;
    }
    printf("ave lookup time: %f seconds\n",(running_total/total));
    printf("max lookup time: %f seconds\n",max);
    printf("min lookup time: %f seconds\n",min);
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
int test_lookup_bench(MPI_Comm * comm,
		     int rank,
		     char *buf,
		     void *rawparams)
{
    int ret = -1;
    PVFS_fs_id fs_id;
    PVFS_handle root_handle;
    generic_params *myparams = (generic_params *) rawparams;
    int nerrs = 0;
    PVFS_sysresp_init resp_init;
    pvfs_mntlist mnt = { 0, NULL };     /* use pvfstab in cwd */
    

    /* right now, the system interface isn't threadsafe, so we just want to run with one process. */

    if (rank == 0)
    {

	memset(&resp_init, 0, sizeof(resp_init));

	ret = parse_pvfstab(NULL, &mnt);
	if (ret < 0)
	{
	    printf("Parsing error\n");
	    return -1;
	}

	ret = PVFS_sys_initialize(mnt, &resp_init);
	if (ret < 0)
	{
	    printf("PVFS_sys_initialize() failure. = %d\n", ret);
	    return (ret);
	}

	fs_id = resp_init.fsid_list[0];
	if (fs_id < 0)
	{
	    printf("System initialization error\n");
	    return (fs_id);
	}

	root_handle = get_root(fs_id);

	/* this will make n directories and look up each */
	nerrs = do_create_lookup(root_handle, fs_id, myparams->mode, myparams->mode, rank);

	ret = PVFS_sys_finalize();
	if (ret < 0)
	{
	    printf("finalizing sysint failed with errcode = %d\n", ret);
	    return (-1);
	}
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
