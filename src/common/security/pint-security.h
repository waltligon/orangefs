/* 
 * (C) 2009 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#ifndef _PINT_SECURITY_H_
#define _PINT_SECURITY_H_

#include "pvfs2-config.h"
#include "pvfs2-types.h"


/* POSIX-style execute permission */
#define PINT_CAP_EXEC         (1 << 0)
/* POSIX-style write permission */
#define PINT_CAP_WRITE        (1 << 1)
/* POSIX-style read permission */
#define PINT_CAP_READ         (1 << 2)
/* permission to set attributes on a handle */
#define PINT_CAP_SETATTR      (1 << 3)
/* permission to create new object */
#define PINT_CAP_CREATE       (1 << 4)
/* permission to perform administrative-level functions */
#define PINT_CAP_ADMIN        (1 << 5)
/* permission to remove an object */
#define PINT_CAP_REMOVE       (1 << 6)
/* permission to create multiple new objects */
#define PINT_CAP_BATCH_CREATE (1 << 7)
/* permission to remove multiple objects */
#define PINT_CAP_BATCH_REMOVE (1 << 8)

#ifdef WIN32
#define PINT_SECURITY_CHECK(rc, label, format, ...) \
    do { \
        if ((rc) != 0) \
        { \
            gossip_err("%s: " format, __func__, __VA_ARGS__); \
            PINT_security_error(__func__, (rc)); \
            goto label; \
        } \
    } while (0)

#define PINT_SECURITY_CHECK_RET(rc, format, ...) \
    do { \
        if ((rc) != 0) \
        { \
            gossip_err("%s: " format, __func__, __VA_ARGS__); \
            PINT_security_error(__func__, (rc)); \
            return (rc); \
        } \
    } while (0)

#define PINT_SECURITY_CHECK_NULL(ptr, label, format, ...) \
    do { \
        if ((ptr) == NULL) \
        { \
            gossip_err("%s: " format, __func__, __VA_ARGS__); \
            PINT_security_error(__func__, -PVFS_ESECURITY); \
            goto label; \
        } \
    } while (0)

#define PINT_SECURITY_CHECK_NULL_RET(ptr, format, ...) \
    do { \
        if ((ptr) == NULL) \
        { \
            gossip_err("%s: " format, __func__, __VA_ARGS__); \
            PINT_security_error(__func__, -PVFS_ESECURITY); \
            return -PVFS_ESECURITY; \
        } \
    } while (0)
#else
#define PINT_SECURITY_CHECK(rc, label, format, f...) \
    do { \
        if ((rc) != 0) \
        { \
            gossip_err("%s: " format, __func__, ##f); \
            PINT_security_error(__func__, (rc)); \
            goto label; \
        } \
    } while (0)

#define PINT_SECURITY_CHECK_RET(rc, format, f...) \
    do { \
        if ((rc) != 0) \
        { \
            gossip_err("%s: " format, __func__, ##f); \
            PINT_security_error(__func__, (rc)); \
            return (rc); \
        } \
    } while (0)

#define PINT_SECURITY_CHECK_NULL(ptr, label, format, f...) \
    do { \
        if ((ptr) == NULL) \
        { \
            gossip_err("%s: " format, __func__, ##f); \
            PINT_security_error(__func__, -PVFS_ESECURITY); \
            goto label; \
        } \
    } while (0)

#define PINT_SECURITY_CHECK_NULL_RET(ptr, format, f...) \
    do { \
        if ((ptr) == NULL) \
        { \
            gossip_err("%s: " format, __func__, ##f); \
            PINT_security_error(__func__, -PVFS_ESECURITY); \
            return -PVFS_ESECURITY; \
        } \
    } while (0)
#endif

#ifdef WIN32
#define PINT_SECURITY_ERROR(rc, format, ...) \
    do { \
        gossip_err("%s: " format, __func__, __VA_ARGS__); \
        PINT_security_error(__func__, (rc)); \
    } while (0)
#else
#define PINT_SECURITY_ERROR(rc, format, f...) \
    do { \
        gossip_err("%s: " format, __func__, ##f); \
        PINT_security_error(__func__, (rc)); \
    } while (0)
#endif

int PINT_security_initialize(void);
int PINT_security_finalize(void);

#ifdef ENABLE_CERTCACHE
int PINT_security_cache_ca_cert(void);
#endif

int PINT_init_capability(PVFS_capability *cap);
int PINT_sign_capability(PVFS_capability *cap);
int PINT_verify_capability(const PVFS_capability *cap);
int PINT_server_to_server_capability(PVFS_capability *capability,
                                     PVFS_fs_id fs_id,
                                     int num_handles,
                                     PVFS_handle *handle_array);


int PINT_init_credential(PVFS_credential *cred);
int PINT_sign_credential(PVFS_credential *cred);
int PINT_verify_credential(const PVFS_credential *cred);

void PINT_security_error(const char *prefix, 
                         int err);

#endif /* _PINT_SECURITY_H_ */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
