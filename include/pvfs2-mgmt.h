/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This header includes prototypes for management functions.  */

#ifndef __PVFS2_MGMT_H
#define __PVFS2_MGMT_H

#include "pvfs2-types.h"

/* low level statfs style information for each server */
/* see PVFS_mgmt_statfs_all() */
struct PVFS_mgmt_server_stat
{
    PVFS_fs_id fs_id;
    PVFS_size bytes_available;
    PVFS_size bytes_total;
    const char* bmi_address;
    int server_type;
};

/* performance monitoring statistics */
struct PVFS_mgmt_perf_stat
{
    uint32_t id;	    /* timestep id */
    uint64_t start_time_ms; /* start time of perf set, ms since epoch */
    int64_t write;	    /* bytes written */
    int64_t read;	    /* bytes read */
};

/* values which may be or'd together in the flags field above */
enum
{
    PVFS_MGMT_IO_SERVER = 1,
    PVFS_MGMT_META_SERVER = 2
};


int PVFS_mgmt_count_servers(
    PVFS_fs_id fs_id,
    PVFS_credentials credentials,
    int server_type,
    int* count);

int PVFS_mgmt_get_server_array(
    PVFS_fs_id fs_id,
    PVFS_credentials credentials,
    int server_type,
    PVFS_id_gen_t* addr_array,
    int* inout_count_p);

const char* PVFS_mgmt_map_addr(
    PVFS_fs_id fs_id,
    PVFS_credentials credentials,
    PVFS_id_gen_t addr,
    int* server_type);

int PVFS_mgmt_setparam_list(
    PVFS_fs_id fs_id,
    PVFS_credentials credentials,
    enum PVFS_server_param param,
    int64_t value,
    PVFS_id_gen_t* addr_array,
    int count);

int PVFS_mgmt_setparam_all(
    PVFS_fs_id fs_id,
    PVFS_credentials credentials,
    enum PVFS_server_param param,
    int64_t value);

int PVFS_mgmt_statfs_list(
    PVFS_fs_id fs_id,
    PVFS_credentials credentials,
    struct PVFS_mgmt_server_stat* stat_array,
    PVFS_id_gen_t* addr_array,
    int count);

int PVFS_mgmt_statfs_all(
    PVFS_fs_id fs_id,
    PVFS_credentials credentials,
    struct PVFS_mgmt_server_stat* stat_array,
    int* inout_count_p);

int PVFS_mgmt_noop(
    PVFS_credentials credentials,
    PVFS_id_gen_t addr);

#endif /* __PVFS2_MGMT_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
