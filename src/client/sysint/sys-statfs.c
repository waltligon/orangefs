/* 
 * (C) 2003 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <assert.h>

#include "client-state-machine.h"
#include "state-machine-fns.h"
#include "pvfs2-debug.h"
#include "gossip.h"
#include "pvfs2-mgmt.h"


int PVFS_sys_statfs(
    PVFS_fs_id fs_id,
    PVFS_credentials credentials,
    PVFS_sysresp_statfs* resp)
{
    int num_servers = 0;
    int ret = -1;
    struct PVFS_mgmt_server_stat* stat_array = NULL;
    int i;
    int num_io_servers = 0;
    PVFS_size min_bytes_available = 0;
    PVFS_size min_bytes_total = 0;

    /* first, determine how many servers are in the file system */
    ret = PVFS_mgmt_count_servers(fs_id, credentials, 
	(PVFS_MGMT_IO_SERVER|PVFS_MGMT_META_SERVER), &num_servers);
    if(ret < 0)
    {
	return(ret);
    }

    /* allocate room for statfs info from all servers */
    stat_array = (struct PVFS_mgmt_server_stat*)malloc(num_servers*sizeof(struct 
	PVFS_mgmt_server_stat));
    if(!stat_array)
    {
	return(-PVFS_ENOMEM);
    }

    /* gather all statfs information */
    ret = PVFS_mgmt_statfs_all(
	fs_id,
	credentials,
	stat_array,
	&num_servers);
    if(ret < 0)
    {
	free(stat_array);
	return(ret);
    }

    /* aggregate statistics down into one statfs structure */

    resp->statfs_buf.fs_id = fs_id;
    resp->statfs_buf.bytes_available = 0;
    resp->statfs_buf.bytes_total = 0;
    resp->statfs_buf.handles_available_count = 0;
    resp->statfs_buf.handles_total_count = 0;
    for(i=0; i<num_servers; i++)
    {
	if(stat_array[i].server_type & PVFS_MGMT_IO_SERVER)
	{
	    num_io_servers++;
	    if(min_bytes_available == 0 || 
		min_bytes_available > stat_array[i].bytes_available)
	    {
		min_bytes_available = stat_array[i].bytes_available;
	    }
	    if(min_bytes_total == 0 || 
		min_bytes_total > stat_array[i].bytes_total)
	    {
		min_bytes_total = stat_array[i].bytes_total;
	    }
	}
	resp->statfs_buf.handles_available_count 
	    += stat_array[i].handles_available_count;
	resp->statfs_buf.handles_total_count 
	    += stat_array[i].handles_total_count;
    }
    resp->statfs_buf.bytes_available = min_bytes_available*num_io_servers;
    resp->statfs_buf.bytes_total = min_bytes_total*num_io_servers;
    resp->server_count = num_servers;

    free(stat_array);

    return(0);
}

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 noexpandtab
 */
