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
    int check_num_servers = 0;
    int overflow_flag = 0;
    int ret = -1;
    struct PVFS_mgmt_server_stat* stat_array = NULL;
    int i;

    /* first, determine how many servers are in the file system */
    ret = PVFS_mgmt_count_servers(fs_id, credentials, &num_servers);
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
	num_servers,
	&check_num_servers,
	&overflow_flag,
	stat_array);
    if(ret < 0)
    {
	free(stat_array);
	return(ret);
    }

    /* our logic is wrong somewhere if we didn't correctly calculate the
     * number of servers to receive statfs information from
     */
    assert(num_servers == check_num_servers && overflow_flag == 0);

    /* aggregate statistics down into one statfs structure */

    /* TODO: this is all wrong!
     * need to put in a better free size calculation that takes into 
     * account which servers are I/O servers, and what the default 
     * distribution is?  Need to think about this some more...
     */
    resp->statfs_buf.fs_id = fs_id;
    resp->statfs_buf.bytes_available = 0;
    resp->statfs_buf.bytes_total = 0;
    for(i=0; i<num_servers; i++)
    {
	resp->statfs_buf.bytes_available += stat_array[i].bytes_available;
	resp->statfs_buf.bytes_total += stat_array[i].bytes_total;
    }
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
