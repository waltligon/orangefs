/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 *
 */

/** \file
 *  \ingroup mgmtint
 *
 *  Various PVFS2 management interface routines.  Many are built on top of
 *  other management interface routines.
 */

#include <assert.h>

#include "pvfs2-types.h"
#include "pvfs2-mgmt.h"
#include "bmi.h"
#include "pint-sysint-utils.h"
#include "pint-cached-config.h"
#include "server-config.h"
#include "client-state-machine.h"

extern int g_admin_mode;

/** Maps a given opaque server address back to a string address.  Also
 *  fills in server type.
 *
 *  \return Pointer to string on success, NULL on failure.
 */
const char *PVFS_mgmt_map_addr(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    PVFS_BMI_addr_t addr,
    int *server_type)
{
    struct server_configuration_s *server_config =
        PINT_get_server_config_struct(fs_id);
    const char *ret = PINT_cached_config_map_addr(
        server_config, fs_id, addr, server_type);

    PINT_put_server_config_struct(server_config);
    return ret;
}

/** Obtains file system statistics from all servers in a given
 *  file system.
 *
 *  \return 0 on success, -PVFS_error on failure.
 */
PVFS_error PVFS_mgmt_statfs_all(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    struct PVFS_mgmt_server_stat *stat_array,
    int *inout_count_p,
    PVFS_error_details *details)
{
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_BMI_addr_t *addr_array = NULL;
    int real_count = 0;
    struct server_configuration_s *server_config = NULL;

    server_config = PINT_get_server_config_struct(fs_id);
    assert(server_config);

    ret = PINT_cached_config_count_servers(
        server_config, fs_id,  PVFS_MGMT_IO_SERVER|PVFS_MGMT_META_SERVER,
        &real_count);
    PINT_put_server_config_struct(server_config);

    if (ret < 0)
    {
	return ret;
    }

    if (real_count > *inout_count_p)
    {
	return -PVFS_EOVERFLOW;
    }

    *inout_count_p = real_count;

    addr_array = (PVFS_BMI_addr_t *)malloc(
        real_count*sizeof(PVFS_BMI_addr_t));
    if (addr_array == NULL)
    {
	return -PVFS_ENOMEM;
    }

    server_config = PINT_get_server_config_struct(fs_id);
    assert(server_config);

    /* generate default list of servers */
    ret = PINT_cached_config_get_server_array(
        server_config, fs_id, PVFS_MGMT_IO_SERVER|PVFS_MGMT_META_SERVER,
        addr_array, &real_count);
    PINT_put_server_config_struct(server_config);

    if (ret < 0)
    {
	free(addr_array);
	return ret;
    }
    
    ret = PVFS_mgmt_statfs_list(
        fs_id, credentials, stat_array, addr_array,
        real_count, details);

    free(addr_array);

    return ret;
}

/** Set a single run-time parameter on all servers in a given
 *  file system.
 *
 *  \return 0 on success, -PVFS_error on failure.
 */
PVFS_error PVFS_mgmt_setparam_all(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    enum PVFS_server_param param,
    uint64_t value,
    uint64_t *old_value_array,
    PVFS_error_details *details)
{
    int count = 0;
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_BMI_addr_t *addr_array = NULL;
    struct server_configuration_s *server_config = NULL;

    server_config = PINT_get_server_config_struct(fs_id);
    assert(server_config);

    ret = PINT_cached_config_count_servers(
        server_config, fs_id,
        PVFS_MGMT_IO_SERVER|PVFS_MGMT_META_SERVER, &count);
    PINT_put_server_config_struct(server_config);

    if (ret < 0)
    {
	return ret;
    }

    addr_array = (PVFS_BMI_addr_t *)malloc(
        (count * sizeof(PVFS_BMI_addr_t)));
    if (addr_array == NULL)
    {
	return -PVFS_ENOMEM;
    }

    server_config = PINT_get_server_config_struct(fs_id);
    assert(server_config);

    /* generate default list of servers */
    ret = PINT_cached_config_get_server_array(
        server_config, fs_id, PVFS_MGMT_IO_SERVER|PVFS_MGMT_META_SERVER,
        addr_array, &count);
    PINT_put_server_config_struct(server_config);

    if (ret < 0)
    {
	free(addr_array);
	return ret;
    }

    ret = PVFS_mgmt_setparam_list(
        fs_id, credentials, param, value, addr_array,
        old_value_array, count, details);

    free(addr_array);

    return ret;
}

/** Sets a single run-time parameter on a specific server.
 */
PVFS_error PVFS_mgmt_setparam_single(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    enum PVFS_server_param param,
    uint64_t value,
    char *server_addr_str,
    uint64_t *old_value,
    PVFS_error_details *details)
{
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_BMI_addr_t addr;

    if (server_addr_str && (BMI_addr_lookup(&addr, server_addr_str) == 0))
    {
        ret = PVFS_mgmt_setparam_list(
            fs_id, credentials, param, value,
            &addr, old_value, 1, details);
    }
    return ret;
}

/** Obtains a list of all servers of a given type in a specific
 *  file system.
 *
 *  \return 0 on success, -PVFS_error on failure.
 */
PVFS_error PVFS_mgmt_get_server_array(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    int server_type,
    PVFS_BMI_addr_t *addr_array,
    int *inout_count_p)
{
    PVFS_error ret = -PVFS_EINVAL;
    struct server_configuration_s *server_config = NULL;

    server_config = PINT_get_server_config_struct(fs_id);
    assert(server_config);

    ret = PINT_cached_config_get_server_array(
        server_config, fs_id, server_type, addr_array, inout_count_p);
    PINT_put_server_config_struct(server_config);
    return ret;
}

/** Counts the number of servers of a given type present in a file
 *  system.
 *
 *  \param count pointer to address where output count is stored
 *
 *  \return 0 on success, -PVFS_error on failure.
 */
PVFS_error PVFS_mgmt_count_servers(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    int server_type,
    int *count)
{
    PVFS_error ret = -PVFS_EINVAL;
    struct server_configuration_s *server_config = NULL;

    server_config = PINT_get_server_config_struct(fs_id);
    assert(server_config);

    ret = PINT_cached_config_count_servers(
        server_config, fs_id, server_type, count);
    PINT_put_server_config_struct(server_config);
    return ret;
}

/** Turns on/off admin mode of system/mgmt interface.  This changes
 *  a local variable that determines the "mode" of subsequent server
 *  requests.
 *
 *  \return 0 on success, -PVFS_error on failure.
 */
PVFS_error PVFS_mgmt_toggle_admin_mode(
    PVFS_credentials *credentials,
    int on_flag)
{
    if(on_flag != 1 && on_flag != 0)
    {	
	return -PVFS_EINVAL;
    }

    g_admin_mode = on_flag;
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
