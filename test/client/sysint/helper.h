/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __HELPER_H
#define __HELPER_H

#define MAX_NUM_DIRENTS    512
#define MAX_PVFS_PATH_LEN  512

#include "pint-sysint.h"
#include "str-utils.h"

/* make uid, gid and perms passed in later */
PVFS_handle lookup_parent_handle(char *filename, PVFS_fs_id fs_id);

#endif
