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

#include "trove.h"
#include "server-config.h"

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
