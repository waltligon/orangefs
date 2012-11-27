/*                                                              
 * Copyright (C) 2012 Clemson University and Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 *
 * UID-mapping functions                                                                
 *
*/

#ifndef _PINT_UID_MAP_H_
#define _PINT_UID_MAP_H_

#include "pvfs2-config.h"
#include "pvfs2-types.h"

/* Map user using credential certificate */
int PINT_map_credential(PVFS_credential *cred, 
                        PVFS_uid *uid,
                        uint32_t *num_groups,
                        PVFS_gid *group_array);
#endif
