/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <pint-userlib.h>
#include <pvfs2-userlib.h>

extern pvfs_mntlist mnt;
static gen_mutex_t mt_mnt; 

/* pvfs_release
 *
 * Shut down the POSIX library interface
 *
 * returns 0 on success, -1 on error
 */
void pvfs_release(void)
{
	int ret = 0;

	/* Grab the mutex */
	gen_mutex_lock(&mt_mnt);	
	
	/* Shut down the System Interface */
	ret = PVFS_sys_finalize();
	if (ret < 0)
	{
		/* Set error and continue */
	}

	/* Deallocate the mount structure */
	free_pvfstab_entry(&mnt);
	
	/* Release the mutex */
	gen_mutex_unlock(&mt_mnt);	
}
