/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include <pint-userlib.h>
#include <pvfs2-userlib.h>

/* Global mount list */
pvfs_mntlist mnt;
static gen_mutex_t mt_mnt;

/* pvfs_lib_init 
 *
 * Initialize the POSIX library interface
 *
 * returns 0 on success, -1 on error
 */
int pvfs_init(void)
{
	int ret = 0;

	/* Do we need to lock now? Will initialize ever be called
	 * by multiple threads? */
	/* Grab the mutex */
	gen_mutex_lock(&mt_mnt);

	/* Parse the pvfstab file */
	/* Populate a structure with params got from pvfstab */
	ret = parse_pvfstab(NULL,&mnt);
	if (ret < 0)
	{
		printf("Parsing error\n");
		return(-1);
	}
	/* Initialize System Interface and pass in the above struct */
	ret = PVFS_sys_initialize(mnt);
	if (ret < 0)
	{	
		printf("Error in initializing System Interface\n");
		return(-1);
	}

	/* Release the mutex */
	gen_mutex_unlock(&mt_mnt);
	
	return(0);
}
