/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 *
 */

#include <assert.h>

#include "pvfs2-types.h"
#include "pvfs2-mgmt.h"
#include "bmi.h"
#include "pint-sysint.h"

/* TODO: if we add any more small mgmt functions, they should probably all 
 * be put together in one generic .c file...
 */

/* PVFS_mgmt_count_servers()
 *
 * counts the number of physical servers present in a given PVFS2 file 
 * system
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PVFS_mgmt_count_servers(
    PVFS_fs_id fs_id,
    PVFS_credentials credentials,
    int* count)
{
    int ret = -1;
    bmi_addr_t* addr_array;

    ret = PINT_collect_physical_addrs(fs_id, count, &addr_array);

    /* the above call allocates a list of addresses, which we actually
     * don't need here 
     */
    if(ret == 0)
	free(addr_array);
    
    return(ret);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

