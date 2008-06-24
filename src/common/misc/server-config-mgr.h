/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __SERVER_CONFIG_MGR_H
#define __SERVER_CONFIG_MGR_H

#include "server-config.h"

int PINT_server_config_mgr_initialize(void);

int PINT_server_config_mgr_finalize(void);

int PINT_server_config_mgr_reload_cached_config_interface(void);

int PINT_server_config_mgr_add_config(
    struct server_configuration_s *config_s,
    PVFS_fs_id fs_id,
    int* free_config_flag);

int PINT_server_config_mgr_remove_config(
    PVFS_fs_id fs_id);

struct server_configuration_s *__PINT_server_config_mgr_get_config(
    PVFS_fs_id fs_id);

void __PINT_server_config_mgr_put_config(
    struct server_configuration_s *config_s);

int PINT_server_config_mgr_get_abs_min_handle_recycle_time(void);

/* client and server retrieve fs configuration from different sources */
#if defined(__PVFS2_CLIENT__)
#define PINT_server_config_mgr_get_config __PINT_server_config_mgr_get_config
#define PINT_server_config_mgr_put_config __PINT_server_config_mgr_put_config
#elif defined(__PVFS2_SERVER__)
#include "src/server/pvfs2-server.h"
#define PINT_server_config_mgr_get_config(__fsid) get_server_config_struct()
static inline void PINT_server_config_mgr_put_config(
    struct server_configuration_s *config_s) { return; }
#endif

#endif  /* __SERVER_CONFIG_MGR_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
