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

extern struct server_configuration_s g_server_config;

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
    int server_type,
    int* count)
{
    return(PINT_bucket_count_servers(&g_server_config,
	fs_id, server_type, count));
}

/* PVFS_mgmt_build_virt_server_list()
 *
 * allocates a string consisting of a space delimited list of addresses for 
 * pvfs2 servers, as taken from the configuration file data for the file system
 * in question.  server_type indicates whether I/O or meta servers should be 
 * listed.
 * NOTE: caller must free char* pointer
 * 
 * returns pointer to string on success, NULL on failure
 */
char* PVFS_mgmt_build_virt_server_list(
    PVFS_fs_id fs_id,
    PVFS_credentials credentials,
    int server_type)
{
    int tmp_server_type;

    if(server_type != PVFS_MGMT_IO_SERVER 
	&& server_type != PVFS_MGMT_META_SERVER)
    {
	return(NULL);
    }

    if(server_type == PVFS_MGMT_IO_SERVER)
	tmp_server_type = PINT_BUCKET_IO;
    else
	tmp_server_type = PINT_BUCKET_META;

    return(PINT_bucket_build_virt_server_list(&g_server_config, fs_id, 
	tmp_server_type));
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

