
#ifndef __CHECK_H
#define __CHECK_H

#include "pvfs2-types.h"
#include "pvfs2-attr.h"
#include "pvfs2-server.h"

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
    
PINT_sm_action prelude_perm_check(
    struct PINT_smcb *smcb, job_status_s *js_p);
    
PINT_sm_action prelude_check_acls_if_needed(
    struct PINT_smcb *smcb, job_status_s *js_p);
    
PINT_sm_action prelude_check_acls(
    struct PINT_smcb *smcb, job_status_s *js_p);
    
#endif  /* __CHECK_H */

