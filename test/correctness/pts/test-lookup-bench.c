/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <sys/time.h>
#include <stdio.h>

#include "client.h"
#include "mpi.h"
#include "pts.h"
#include "pvfs-helper.h"
#include "pvfs2-util.h"


extern pvfs_helper_t pvfs_helper;

/*
 * simple helper to lookup a handle given a filename
 *
 * returns a handle to the new directory
 *          -1 if some error happened
 */
static PVFS_handle simple_lookup_name(char *name,
                                      PVFS_fs_id fs_id)
{
    int ret = -1;
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_lookup;

    memset(&resp_lookup, 0, sizeof(resp_lookup));

    credentials.uid = 100;
    credentials.gid = 100;

    ret = PVFS_sys_lookup(fs_id, name, credentials,
                          &resp_lookup, PVFS2_LOOKUP_LINK_NO_FOLLOW);
    if (ret < 0)
    {
       printf("Lookup failed with errcode = %d\n", ret);
       return(-1);
    }

    return (PVFS_handle) resp_lookup.pinode_refn.handle;
}

int do_create_lookup(PVFS_pinode_reference parent_refn,
                     PVFS_fs_id fs_id,
                     int depth,
                     int ndirs,
                     int rank)
{
    int i;
    char name[PVFS_NAME_MAX];
    char path[PVFS_NAME_MAX]; /*same as name except it has a slash prepending the path*/
    PVFS_handle dir_handle, lookup_handle;
    PVFS_pinode_reference out_refn;
    double before, after, running_total = 0, max=0.0, min = 10000.0, total = 0, current;

    /* base case: we've gone far enough */
    if (depth == 0)
	return 0;

    for (i = 0; i < ndirs; i++)
    {
	snprintf(name, PVFS_NAME_MAX, "depth=%d-rank=%d-iter=%d", depth, rank, i);
	snprintf(path, PVFS_NAME_MAX, "/%s", name);
	if (create_dir(parent_refn, name, &out_refn) < 0)
	{
            printf("creation of %s failed; make sure it doesn't "
                   "already exist!\n",path);
            return -1;
	}
        dir_handle = out_refn.handle;
	/* lookup the directory we just created */
	before = MPI_Wtime();
	lookup_handle = simple_lookup_name(path, fs_id);
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
        if (remove_file(parent_refn,name))
        {
            printf("failed to remove %s; test aborting\n",path);
            return -1;
        }
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
    PVFS_pinode_reference root_refn;
    generic_params *myparams = (generic_params *) rawparams;
    int nerrs = 0;

    /* right now, the system interface isn't threadsafe, so we just want to run with one process. */

    if (rank == 0)
    {

        if (!pvfs_helper.initialized && initialize_sysint())
        {
            debug_printf("test_lookup_bench cannot be initialized!\n");
            return -1;
        }

	fs_id = pvfs_helper.resp_init.fsid_list[0];
	if (fs_id < 0)
	{
	    printf("System initialization error\n");
	    return (fs_id);
	}

        ret = get_root(fs_id, &root_refn);
	if (ret < 0)
        {
	    printf("failed to get root pinode refn: errcode = %d\n", ret);
	    return (-1);
        }

	/* this will make n directories and look up each */
	nerrs = do_create_lookup(root_refn, fs_id, myparams->mode,
                                 myparams->mode, rank);

	ret = finalize_sysint();
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
