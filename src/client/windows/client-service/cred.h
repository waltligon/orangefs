/*
 * (C) 2010-2013 Clemson University and Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 *
 * Windows credential function declarations
 *
 */

#ifndef __CRED_H
#define __CRED_H

#include "pvfs2-types.h"
#include "pvfs2-req-proto.h"

int init_credential(PVFS_uid uid, PVFS_gid group_array[], uint32_t num_groups,
                    PVFS_credential *cred);

void cleanup_credential(PVFS_credential *cred);                              

int credential_in_group(PVFS_credential *cred, PVFS_gid group);

/* void credential_add_group(PVFS_credential *cred, PVFS_gid group); 

void credential_set_timeout(PVFS_credential *cred, PVFS_time timeout);
*/

#endif