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

#include "pvfs2-internal.h"
#include "pvfs2-types.h"
#include "pvfs2-mgmt.h"
#include "bmi.h"
#include "pint-sysint-utils.h"
#include "pint-cached-config.h"
#include "pint-util.h"
#include "server-config.h"
#include "client-state-machine.h"
#include "sid.h"
#include "sidcacheval.h"

/* PVFS_MGMT_IO_SERVER is 1 and PVFS_MGMT_META_SERVER is 2.
 * Those values are SID_SERVER_ROOT and SID_SERVER_PRIME.
 * Config "Type DATA META" is stored as SID_SERVER_DATA / SID_SERVER_META.
 */
static uint32_t mgmt_server_type_to_sid(int server_type)
{
    uint32_t sid_type = 0;

    if (server_type & PVFS_MGMT_IO_SERVER)
    {
        sid_type |= SID_SERVER_DATA;
    }
    if (server_type & PVFS_MGMT_META_SERVER)
    {
        sid_type |= SID_SERVER_META;
    }
    if (sid_type == 0)
    {
        sid_type = (uint32_t)server_type;
    }
    return sid_type;
}

/* V3 cleanup - use BMI_rev_lookup instread */
#if 0
/** Maps a given opaque server address back to a string address.  Also
 *  fills in server type.
 *
 *  \return Pointer to string on success, NULL on failure.
 */
const char *PVFS_mgmt_map_addr(PVFS_fs_id fs_id,
                               PVFS_BMI_addr_t addr,
                               int *server_type)
{
    return PINT_cached_config_map_addr(fs_id, addr, server_type);
}

PVFS_error PVFS_mgmt_map_handle(PVFS_fs_id fs_id,
                                PVFS_handle handle,
                                PVFS_BMI_addr_t *addr)
{
    return PINT_cached_config_map_to_server(addr, handle, fs_id);
}
#endif

/** Mgmt interface call to get BMI addr from a SID
 */
PVFS_error PVFS_mgmt_get_addr(PVFS_BMI_addr_t *addr, PVFS_SID *sid)
{
    /* return PVFS_error? */
    return PVFS_SID_get_addr(addr, sid);
}

/** Obtains file system statistics from all servers in a given
 *  file system.
 *
 *  \return 0 on success, -PVFS_error on failure.
 */
PVFS_error PVFS_mgmt_statfs_all(PVFS_fs_id fs_id,
                                const PVFS_credential *credential,
                                struct PVFS_mgmt_server_stat *stat_array,
                                int *inout_count_p,
                                PVFS_error_details *details,
                                PVFS_hint hints)
{
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_BMI_addr_t *addr_array = NULL;
    int real_count = 0;
    struct SID_type_s stype = {SID_SERVER_ALL, fs_id};

/* V3 cleanup */
#if 0
    ret = PINT_cached_config_count_servers(
                            fs_id,
                            PVFS_MGMT_IO_SERVER | PVFS_MGMT_META_SERVER,
                            &real_count);
#endif

    ret = PVFS_SID_count_all(fs_id, &real_count);
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
                            real_count * sizeof(PVFS_BMI_addr_t));
    if (addr_array == NULL)
    {
	return -PVFS_ENOMEM;
    }

    /* generate default list of servers */

/* V3 cleanup */
#if 0
    ret = PINT_cached_config_get_server_array(
                            fs_id,
                            PVFS_MGMT_IO_SERVER | PVFS_MGMT_META_SERVER,
                            addr_array,
                            &real_count);
#endif

    /* to fix server type field, return SIDs here, then look them up to
     * get the server type with SID_get_type after the statfs_list
     * alternatively the server could look itself up as part of the
     * statfs call.
     */
    ret = PVFS_SID_get_server_first_n(addr_array, NULL, &real_count, stype);
    if (ret < 0)
    {
	free(addr_array);
	return ret;
    }
    
    ret = PVFS_mgmt_statfs_list(fs_id,
                                credential,
                                stat_array,
                                addr_array,
                                real_count,
                                details,
                                hints);

    free(addr_array);

    return ret;
}

/** Set a single run-time parameter on all servers in a given
 *  file system.
 *
 *  \return 0 on success, -PVFS_error on failure.
 */
PVFS_error PVFS_mgmt_setparam_all(PVFS_fs_id fs_id,
                                  const PVFS_credential *credential,
                                  enum PVFS_server_param param,
                                  struct PVFS_mgmt_setparam_value *value,
                                  PVFS_error_details *details,
                                  PVFS_hint hints)
{
    int count = 0;
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_BMI_addr_t *addr_array = NULL;
    struct SID_type_s stype = {SID_SERVER_ALL, fs_id};

/* V3 replace with SIDcache call */
#if 0
    ret = PINT_cached_config_count_servers(
                            fs_id,
                            PVFS_MGMT_IO_SERVER | PVFS_MGMT_META_SERVER,
                            &count);
#endif

    ret = PVFS_SID_count_all(fs_id, &count);
    if (ret < 0)
    {
	return ret;
    }

    addr_array = (PVFS_BMI_addr_t *)malloc(count * sizeof(PVFS_BMI_addr_t));
    if (addr_array == NULL)
    {
	return -PVFS_ENOMEM;
    }

    /* generate default list of servers */

/* V3 cleanup */
#if 0
    ret = PINT_cached_config_get_server_array(
                                fs_id,
                                PVFS_MGMT_IO_SERVER | PVFS_MGMT_META_SERVER,
                                addr_array,
                                &count);
#endif

    ret = PVFS_SID_get_server_first_n(addr_array, NULL, &count, stype);
    if (ret < 0)
    {
	free(addr_array);
	return ret;
    }

    ret = PVFS_mgmt_setparam_list(fs_id,
                                  credential,
                                  param,
                                  value,
                                  addr_array,
                                  count,
                                  details,
                                  hints);

    free(addr_array);

    return ret;
}

/** Sets a single run-time parameter on a specific server.
 */
PVFS_error PVFS_mgmt_setparam_single(PVFS_fs_id fs_id,
                                     const PVFS_credential *credential,
                                     enum PVFS_server_param param,
                                     struct PVFS_mgmt_setparam_value *value,
                                     char *server_addr_str,
                                     PVFS_error_details *details,
                                     PVFS_hint hints)
{
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_BMI_addr_t addr;

    if (server_addr_str &&
            (BMI_addr_lookup(&addr, server_addr_str, NULL) == 0))
    {
        ret = PVFS_mgmt_setparam_list(fs_id,
                                      credential,
                                      param,
                                      value,
                                      &addr,
                                      1,
                                      details,
                                      hints);
    }
    return ret;
}

/** Obtains a list of all servers of a given type in a specific
 *  file system.
 *
 *  \return 0 on success, -PVFS_error on failure.
 */
PVFS_error PVFS_mgmt_get_server_array(PVFS_fs_id fs_id,
                                      int server_type,
                                      PVFS_BMI_addr_t *addr_array,
                                      int *inout_count_p)
{
    PVFS_error ret = -PVFS_EINVAL;
    struct SID_type_s stype;

    stype.fsid = fs_id;
    stype.server_type = mgmt_server_type_to_sid(server_type);
    ret = PVFS_SID_get_server_first_n(addr_array, NULL, inout_count_p, stype);
    return ret;
}

/** Counts the number of servers of a given type present in a file
 *  system.
 *
 *  \param count pointer to address where output count is stored
 *
 *  \return 0 on success, -PVFS_error on failure.
 */
PVFS_error PVFS_mgmt_count_servers(PVFS_fs_id fs_id,
                                   int server_type,
                                   int *count)
{
    uint32_t sid_type = mgmt_server_type_to_sid(server_type);
    uint32_t bit;
    int total = 0;
    PVFS_error ret;

    if (!count)
    {
        return -PVFS_EINVAL;
    }
    /* A single role is an exact type key. A mask is the sum of
     * those roles. One server that is both meta and data is
     * counted once per role.
     */
    if (sid_type == 0 || (sid_type & (sid_type - 1)) == 0)
    {
        return PVFS_SID_count_type(fs_id, (int)sid_type, count);
    }
    for (bit = 1; bit != 0; bit <<= 1)
    {
        int part = 0;

        if (!(sid_type & bit))
        {
            continue;
        }
        ret = PVFS_SID_count_type(fs_id, (int)bit, &part);
        if (ret < 0)
        {
            return ret;
        }
        total += part;
    }
    *count = total;
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
