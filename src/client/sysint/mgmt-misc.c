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

/* PVFS_mgmt_map_addr()
 *
 * maps a given opaque server address back to a string address, also
 * fills in server type
 *
 * returns pointer to string on success, NULL on failure
 */
const char* PVFS_mgmt_map_addr(
    PVFS_fs_id fs_id,
    PVFS_credentials credentials,
    PVFS_id_gen_t addr,
    int* server_type)
{
    return(PINT_bucket_map_addr(&g_server_config, fs_id,
	addr, server_type));
}

/* PVFS_mgmt_statfs_all()
 *
 * helper function on top of PVFS_mgmt_statfs_list(); automatically
 * generates list of all servers and operates on that list
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PVFS_mgmt_statfs_all(
    PVFS_fs_id fs_id,
    PVFS_credentials credentials,
    struct PVFS_mgmt_server_stat* stat_array,
    int* inout_count_p)
{
    PVFS_id_gen_t* addr_array = NULL;
    int real_count = 0;
    int ret = -1;

    ret = PINT_bucket_count_servers(&g_server_config, fs_id, 
	(PVFS_MGMT_IO_SERVER|PVFS_MGMT_META_SERVER), &real_count);
    if(ret < 0)
	return(ret);

    if(real_count > *inout_count_p)
	return(-PVFS_EOVERFLOW);

    *inout_count_p = real_count;

    addr_array = (PVFS_id_gen_t*)malloc(real_count*sizeof(PVFS_id_gen_t));
    if(!addr_array)
	return(-PVFS_ENOMEM);

    /* generate default list of servers */
    ret = PINT_bucket_get_server_array(&g_server_config,
	fs_id,
	(PVFS_MGMT_IO_SERVER|PVFS_MGMT_META_SERVER),
	addr_array, 
	&real_count);
    if(ret < 0)
    {
	free(addr_array);
	return(ret);
    }

    /* issue setparam call */
    ret = PVFS_mgmt_statfs_list(
	fs_id,
	credentials,
	stat_array,
	addr_array,
	real_count);

    free(addr_array);

    return(ret);
}


/* PVFS_mgmt_setparam_all()
 *
 * helper function on top of PVFS_mgmt_setparam_list(); automatically
 * generates list of all servers and operates on that list
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PVFS_mgmt_setparam_all(
    PVFS_fs_id fs_id,
    PVFS_credentials credentials,
    enum PVFS_server_param param,
    int64_t value)
{
    PVFS_id_gen_t* addr_array = NULL;
    int count = 0;
    int ret = -1;

    ret = PINT_bucket_count_servers(&g_server_config, fs_id, 
	(PVFS_MGMT_IO_SERVER|PVFS_MGMT_META_SERVER), &count);
    if(ret < 0)
	return(ret);

    addr_array = (PVFS_id_gen_t*)malloc(count*sizeof(PVFS_id_gen_t));
    if(!addr_array)
	return(-PVFS_ENOMEM);

    /* generate default list of servers */
    ret = PINT_bucket_get_server_array(&g_server_config,
	fs_id,
	(PVFS_MGMT_IO_SERVER|PVFS_MGMT_META_SERVER),
	addr_array, 
	&count);
    if(ret < 0)
    {
	free(addr_array);
	return(ret);
    }

    /* issue setparam call */
    ret = PVFS_mgmt_setparam_list(
	fs_id,
	credentials,
	param,
	value,
	addr_array,
	count);

    free(addr_array);

    return(ret);
}

/* PVFS_mgmt_get_server_array()
 *
 * fills in an array of opaque server addresses of the specified type
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PVFS_mgmt_get_server_array(
    PVFS_fs_id fs_id,
    PVFS_credentials credentials,
    int server_type,
    PVFS_id_gen_t* addr_array,
    int* inout_count_p)
{
    return(PINT_bucket_get_server_array(&g_server_config, fs_id, 
	server_type, addr_array, inout_count_p));
}

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

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

