
#ifndef __CHECK_H
#define __CHECK_H

#include "pvfs2-types.h"
#include "pvfs2-attr.h"
#include "pvfs2-server.h"

int PINT_perm_check(struct PINT_server_op *s_op);

int PINT_get_capabilities(void *acl_buf, size_t acl_size, PVFS_uid userid,
    PVFS_gid *group_array, uint32_t num_groups, const PVFS_object_attr *attr, 
    uint32_t *op_mask);
    
#endif  /* __CHECK_H */
