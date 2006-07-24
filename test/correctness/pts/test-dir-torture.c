/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <sys/time.h>

#include "client.h"
#include "mpi.h"
#include "pts.h"
#include "pvfs-helper.h"
#include "pvfs2-util.h"
#include "test-dir-torture.h"

/*
 * handle:  handle of parent directory
 * fs_id:   our file system
 * depth:   how many directories to make at this level
 * rank:    rank in the mpi process group 
 */

static int recursive_create_dir(PVFS_handle handle,
			 PVFS_fs_id fs_id,
			 int depth,
			 int ndirs,
			 int rank)
{
    int i;
    char name[PVFS_SEGMENT_MAX];
    PVFS_object_ref refn;
    PVFS_object_ref out_refn;

    /* base case: we've gone far enough */
    if (depth == 0)
	return 0;

    refn.handle = handle;
    refn.fs_id = fs_id;

    for (i = 0; i < ndirs; i++)
    {
	snprintf(name, PVFS_SEGMENT_MAX, "depth=%d-rank=%d-iter=%d",
                 depth, rank, i);

	if (create_dir(refn, name, &out_refn) < 0)
        {
            fprintf(stderr, "Failed to create dir %s\n",name);
            return -1;
        }

        recursive_create_dir(out_refn.handle, out_refn.fs_id,
                             depth - 1, ndirs, rank);

        if (remove_dir(refn, name) < 0)
        {
            fprintf(stderr, "Faild to remove dir %s.  This is a "
                    "real error.\n",name);
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
 * 	nonzero: errors encountered making one or more directories
 */
int test_dir_torture(MPI_Comm * comm __unused,
		     int rank,
		     char *buf __unused,
		     void *rawparams)
{
    PVFS_fs_id fs_id;
    PVFS_object_ref root_refn;
    generic_params *myparams = (generic_params *) rawparams;
    int nerrs = 0;

    if (!pvfs_helper.initialized && initialize_sysint())
    {
        debug_printf("test_dir_torture cannot be initialized!\n");
        return -1;
    }

    fs_id = pvfs_helper.fs_id;
    get_root(fs_id, &root_refn);

    /*
      this will make n^n directories, so be careful
      about running the test with mode=100
    */
    nerrs = recursive_create_dir(root_refn.handle, root_refn.fs_id,
                                 myparams->mode, myparams->mode, rank);

    return -nerrs;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
