/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <asm/uaccess.h>

#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"
#include "pint-dev-shared.h"

static int bufmap_init = 0;

/* pvfs_bufmap_initialize()
 *
 * initializes the mapped buffer interface
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_initialize(struct PVFS_dev_map_desc* user_desc)
{
    if(bufmap_init)
	return(-EALREADY);

    bufmap_init = 1;

    pvfs2_error("Error: function not implemented.\n");
    return(0);
}

/* pvfs_bufmap_finalize()
 *
 * shuts down the mapped buffer interface and releases any resources
 * associated with it
 *
 * no return value
 */
void pvfs_bufmap_finalize(void)
{
    if(!bufmap_init)
    {
	return;
    }

    pvfs2_error("Error: function not implemented.\n");
    return;
}

/* pvfs_bufmap_get()
 *
 * gets a free mapped buffer descriptor, will sleep until one becomes
 * available if necessary
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_get(struct pvfs_bufmap_desc** desc)
{
    pvfs2_error("Error: function not implemented.\n");
    return(-ENOSYS);
}

/* pvfs_bufmap_put()
 *
 * returns a mapped buffer descriptor to the collection
 *
 * no return value
 */
void pvfs_bufmap_put(struct pvfs_bufmap_desc* desc)
{
    pvfs2_error("Error: function not implemented.\n");
    return;
}

/* pvfs_bufmap_size_query()
 *
 * queries to determine the size of the memory represented by each 
 * mapped buffer description
 *
 * returns size on success, -errno on failure
 */
int pvfs_bufmap_size_query(void)
{
    pvfs2_error("Error: function not implemented.\n");
    return(-ENOSYS);
}

/* pvfs_bufmap_copy_to_user()
 *
 * copies data out of a mapped buffer to a user space address
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_copy_to_user(void* to, struct pvfs_bufmap_desc* from,
    int size)
{
    pvfs2_error("Error: function not implemented.\n");
    return(-ENOSYS);
}

/* pvfs2_bufmap_copy_from_user()
 *
 * copies data from a user space address to a mapped buffer
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_copy_from_user(struct pvfs_bufmap_desc* to, void* from,
    int size)
{
    pvfs2_error("Error: function not implemented.\n");
    return(-ENOSYS);
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
