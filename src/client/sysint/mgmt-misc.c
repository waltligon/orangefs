/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 *
 */

/* this file includes definitions for trivial mgmt routines that do not 
 * warrant their own .c file
 */

#include <assert.h>

#include "pvfs2-types.h"
#include "pvfs2-mgmt.h"
#include "bmi.h"
#include "pint-sysint-utils.h"
#include "pint-bucket.h"
#include "server-config.h"

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
    struct PINT_bucket_server_info* info_array;

    ret = PINT_collect_physical_server_info(fs_id, count, &info_array);

    /* the above call allocates an array of information, actually not 
     * needed here 
     */
    if(ret == 0)
	free(info_array);
    
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

