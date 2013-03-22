/*                                                              
 * Copyright (C) 2012 Clemson University and Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 *
 * LDAP identity-mapping functions                                                                
 *
*/

#ifndef _PINT_LDAP_MAP_H_
#define _PINT_LDAP_MAP_H_

#include "pvfs2-config.h"
#include "pvfs2-types.h"

/* mode definitions */
#define PVFS2_LDAP_SEARCH_CN    0
#define PVFS2_LDAP_SEARCH_DN    1

/* number of retries */
#define PVFS2_LDAP_RETRIES      3

/* connect to configured LDAP server */
int PINT_ldap_initialize(void);

/* use info from credential certificate to retrieve uid/groups */
int PINT_ldap_map_credential(PVFS_credential *cred,
                             PVFS_uid *uid,
                             uint32_t *num_groups,
                             PVFS_gid *group_array);

/* check userid/password to allow user certificate retrieval */
int PINT_ldap_authenticate(const char *userid,
                           const char *password);

/* close LDAP connection */
void PINT_ldap_finalize(void);


#endif
