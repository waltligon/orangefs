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
#include "pcache.h"
#include "pint-bucket.h"

#include "dotconf.h"
#include "trove.h"
#include "server-config.h"

/* converts common fields between sys attr and obj attr structures */
#define PINT_CONVERT_ATTR(dest, src, attrmask)	\
do{						\
    (dest)->owner = (src)->owner;		\
    (dest)->group = (src)->group;		\
    (dest)->perms = (src)->perms;		\
    (dest)->atime = (src)->atime;		\
    (dest)->mtime = (src)->mtime;		\
    (dest)->ctime = (src)->ctime;		\
    (dest)->objtype = (src)->objtype;		\
    (dest)->mask = ((src)->mask & attrmask);	\
}while(0)

/* TODO: this function is a hack- will be removed later */
int PINT_sys_getattr(PVFS_pinode_reference pinode_refn, uint32_t attrmask, 
    PVFS_credentials credentials, PVFS_object_attr *out_attr);

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
} config_fs_cache_s;

int PINT_check_perms(PVFS_object_attr attr,PVFS_permissions mode,int uid,int gid);
int PINT_do_lookup (char* name,PVFS_pinode_reference parent,
                PVFS_credentials cred,PVFS_pinode_reference *entry);
int PINT_server_get_config(
    struct server_configuration_s *config,
    pvfs_mntlist mntent_list);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif
