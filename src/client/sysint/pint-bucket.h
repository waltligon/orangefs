/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_BUCKET_H
#define __PINT_BUCKET_H

/* FIXME: header in header */
#include "pvfs2-types.h"
#include "bmi.h"
#include "dotconf.h"
#include "trove.h"
#include "server-config.h"

/* This is the interface to the bucket management component of the
 * system interface.  It is responsible for managing the list of meta
 * and data servers and mapping between handle ranges and servers.
 */

int PINT_bucket_initialize(void);

int PINT_bucket_finalize(void);

int PINT_handle_load_mapping(
    struct server_configuration_s *config,
    struct filesystem_configuration_s *fs);

int PINT_bucket_get_next_meta(
    struct server_configuration_s *config,
    PVFS_fs_id fsid,
    bmi_addr_t *meta_addr);

int PINT_bucket_get_next_io(
    struct server_configuration_s *config,
    PVFS_fs_id fsid,
    int num_servers,
    bmi_addr_t *io_addr_array);

int PINT_bucket_map_to_server(
    bmi_addr_t *server_addr,
    PVFS_handle handle,
    PVFS_fs_id fsid);

int PINT_bucket_get_num_meta(
    PVFS_fs_id fsid,
    int *num_meta);

int PINT_bucket_get_num_io(
    PVFS_fs_id fsid,
    int *num_io);

int PINT_bucket_get_server_name(
    char *server_name,
    int max_server_name_len,
    PVFS_handle handle,
    PVFS_fs_id fsid);

int PINT_bucket_get_root_handle(
    PVFS_fs_id fsid,
    PVFS_handle *fh_root);

#endif /* __PINT_BUCKET_H */
