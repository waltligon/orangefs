/* 
 *  (C) 2001 Clemson University 
 *  
 *   See COPYING in top-level directory.
 *  
 */

#ifndef __REPLICATION_SERVER_UTILS_H
#define __REPLICATION_SERVER_UTILS_H

#include "replication-common-utils.h"

struct replicate_descriptor_s
{
   PVFS_handle handle;
   PVFS_size bstream_size;
   PVFS_msg_tag_t session_tag;
   PVFS_BMI_addr_t svr_addr;
   void *encoded_resp_p;
   int received;
   replication_endpoint_status_t *endpt_status; 
};

typedef struct replicate_descriptor_s replicate_descriptor_t;

#endif /* __REPLICATION_SERVER_UTILS_H */
/*
 * Local variables:
 *   c-indent-level: 4
 *   c-basic-offset: 4
 * End:
 *
 * vim:  ts=8 sts=4 sw=4 expandtab
 */                                                    
