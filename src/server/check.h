
#ifndef __CHECK_H
#define __CHECK_H

#include "pvfs2-types.h"
#include "pvfs2-attr.h"

enum PINT_access_type
{
    PINT_ACCESS_EXECUTABLE = 1,
    PINT_ACCESS_WRITABLE = 2,
    PINT_ACCESS_READABLE = 4,
};

int PINT_check_mode(
    PVFS_object_attr *attr,
    PVFS_uid uid, PVFS_gid gid,
    enum PINT_access_type access_type);

int PINT_check_acls(void *acl_buf, size_t acl_size, 
    PVFS_object_attr *attr,
    PVFS_uid uid, PVFS_gid gid, int want);

#endif  /* __CHECK_H */

