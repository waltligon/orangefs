/* 
 *  (C) 2001 Clemson University 
 *  
 *   See COPYING in top-level directory.
 *  
 */
#ifndef __REPLICATION_CLIENT_UTILS_H
#define __REPLICATION_CLIENT_UTILS_H

#include "server-config.h"


/* This function pulls replication information from the config file and stores the data in the
 * replication structure within the create structure (sm_p->u.create.replication).
 */
int get_replication_from_config(replication_s *replication_p, PVFS_object_ref *parent);

/* wrapper function that copies variables from filesystem config to a replication structure */
int copy_replication_info_from_config(filesystem_configuration_s *fs
                                     ,replication_s *dest_p);


#endif /* __REPLICATION_CLIENT_UTILS_H */


/*
 * Local variables:
 *   c-indent-level: 4
 *   c-basic-offset: 4
 * End:
 *
 * vim:  ts=8 sts=4 sw=4 expandtab
 */                                                    
