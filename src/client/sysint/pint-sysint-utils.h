/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* internal helper functions used by the system interface */

#ifndef __PINT_SYSINT_UTILS_H
#define __PINT_SYSINT_UTILS_H

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include <assert.h>

#include "pvfs2-types.h"
#include "pvfs2-attr.h"
#include "gossip.h"
#include "job.h"
#include "bmi.h"
#include "pvfs2-sysint.h"
#include "gen-locks.h"
#include "pint-cached-config.h"
#include "pvfs2-sysint.h"

#include "dotconf.h"
#include "trove.h"
#include "server-config.h"

/* maps bmi address to handle ranges/extents */
typedef struct bmi_host_extent_table_s
{
    char *bmi_address;

    /* ptrs are type struct extent */
    PINT_llist *extent_list;
} bmi_host_extent_table_s;

typedef struct config_fs_cache_s
{
    struct qlist_head hash_link;
    struct filesystem_configuration_s *fs;

    /* ptrs are type bmi_host_extent_table_s */
    PINT_llist *bmi_host_extent_tables;

    /* index into fs->meta_handle_ranges obj (see server-config.h) */
    PINT_llist *meta_server_cursor;

    /* index into fs->data_handle_ranges obj (see server-config.h) */
    PINT_llist *data_server_cursor;

    /*
      the following fields are used to cache arrays of unique physical
      server addresses, of particular use to the mgmt interface
    */
    phys_server_desc_s* io_server_array;
    int io_server_count;
    phys_server_desc_s* meta_server_array;
    int meta_server_count;
    phys_server_desc_s* server_array;
    int server_count;

} config_fs_cache_s;

int PINT_check_perms(
    PVFS_object_attr attr,
    PVFS_permissions mode,
    int uid,
    int gid);
int PINT_do_lookup (
    char* name,
    PVFS_object_ref parent,
    PVFS_credentials cred,
    PVFS_object_ref *entry);
int PINT_server_get_config(
    struct server_configuration_s *config,
    struct PVFS_sys_mntent* mntent);
void PINT_release_pvfstab(void);

struct server_configuration_s *PINT_get_server_config_struct(
    PVFS_fs_id fs_id);
void PINT_put_server_config_struct(
    struct server_configuration_s *config);

int PINT_lookup_parent(
    char *filename,
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    PVFS_handle * handle);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif
