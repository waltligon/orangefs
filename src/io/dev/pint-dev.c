/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "pvfs2-types.h"
#include "gossip.h"
#include "pint-dev.h"

static int pdev_fd = -1;

/* PINT_dev_initialize()
 *
 * initializes the device management interface
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_dev_initialize(
	const char* dev_name,
	int flags)
{
    int ret = -1;

    /* we have to be root to access the device */
    if(getuid() != 0 && geteuid() != 0)
    {
	gossip_err("Error: must be root to open pvfs2 device.\n");
	return(-(PVFS_EPERM|PVFS_ERROR_DEV));
    }

    /* TODO: put in code that sets up the device, copy from pvfsd */

    /* try to open the device */
    pdev_fd = open(dev_name, O_RDWR);
    if(pdev_fd < 0)
    {
	switch(errno)
	{
	    case EACCES:
		return(-(PVFS_EPERM|PVFS_ERROR_DEV));
	    case ENOENT:
		return(-(PVFS_ENOENT|PVFS_ERROR_DEV));
	    default:
		return(-(PVFS_ENODEV|PVFS_ERROR_DEV));
	}
    }

    /* run some ioctls to find out device parameters */

    gossip_lerr("Error: function not implemented.\n");
    return(-(PVFS_ENOSYS|PVFS_ERROR_DEV));
}

/* PINT_dev_finalize()
 *
 * shuts down the device management interface
 *
 * no return value
 */
void PINT_dev_finalize(void)
{
    gossip_lerr("Error: function not implemented.\n");
    return;
}


/* PINT_dev_test_unexpected()
 *
 * tests for the presence of unexpected messages
 *
 * returns number of completed unexpected messages on success, -PVFS_error 
 * on failure
 */
int PINT_dev_test_unexpected(
	int intcount,
	int* outcount,
	struct PINT_dev_unexp_info* info_array,
	int max_idle_time)
{
    gossip_lerr("Error: function not implemented.\n");
    return(-(PVFS_ENOSYS|PVFS_ERROR_DEV));
}

/* PINT_dev_release_unexpected()
 *
 * releases the resources associated with an unexpected device message
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_dev_release_unexpected(
	struct PINT_dev_unexp_info* info)
{
    gossip_lerr("Error: function not implemented.\n");
    return(-(PVFS_ENOSYS|PVFS_ERROR_DEV));
}

/* PINT_dev_write()
 *
 * writes a message buffer into the device
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_dev_write(
	void* buffer,
	int size,
	enum PINT_dev_buffer_type buffer_type,
	id_gen_t tag)
{
    gossip_lerr("Error: function not implemented.\n");
    return(-(PVFS_ENOSYS|PVFS_ERROR_DEV));
}

/* PINT_dev_write_list()
 *
 * writes a set of buffers into the device
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_dev_write_list(
	void** buffer_list,
	int* size_list,
	int list_count,
	int total_size,
	enum PINT_dev_buffer_type buffer_type,
	id_gen_t tag)
{
    gossip_lerr("Error: function not implemented.\n");
    return(-(PVFS_ENOSYS|PVFS_ERROR_DEV));
}

/* PINT_dev_memalloc()
 *
 * allocates a memory buffer optimized for transfer into the device
 *
 * returns pointer to buffer on success, NULL on failure
 */
void* PINT_dev_memalloc(int size)
{
    gossip_lerr("Error: function not implemented.\n");
    return(NULL);
}

/* PINT_dev_memfree()
 *
 * frees a memory buffer that was allocated with PINT_dev_memalloc()
 *
 * no return value
 */
void PINT_dev_memfree(void* buffer, int size)
{
    gossip_lerr("Error: function not implemented.\n");
    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
