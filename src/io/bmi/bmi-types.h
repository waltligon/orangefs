/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Header file for types and other defines used throughout the BMI.
 */

#ifndef __BMI_TYPES_H
#define __BMI_TYPES_H

#include <stdlib.h>
#include "id-generator.h"
#include "pvfs2-debug.h"
#include "pvfs2-types.h"

/* PVFS type mapping */
typedef PVFS_size bmi_size_t;	/* data region size */
typedef PVFS_msg_tag_t bmi_msg_tag_t;	/* message tag */
typedef PVFS_context_id bmi_context_id; /* context identifier */
typedef id_gen_t bmi_op_id_t;	/* network operation handle */
typedef id_gen_t bmi_addr_t;	/* network address handle */

/* TODO: not using a real type for this yet; need to specify what
 * error codes look like */
typedef int32_t bmi_error_code_t;	/* error code information */

/* BMI method initialization flags */
enum
{
    BMI_INIT_SERVER = 1
};

enum bmi_op_type
{
    BMI_SEND = 1,
    BMI_RECV = 2
};

/* BMI memory buffer flags */
enum
{
    BMI_PRE_ALLOC = 4,
    BMI_EXT_ALLOC = 8
};

/* BMI get_info and set_info options */
enum
{
    BMI_DROP_ADDR = 1,
    BMI_CHECK_INIT = 2,
    BMI_CHECK_MAXSIZE = 3,
    BMI_GET_METH_ADDR = 4
};

/* mappings from PVFS errors to BMI errors */
#define BMI_EPERM           (PVFS_EPERM | PVFS_ERROR_BMI)
#define BMI_ENOENT          (PVFS_ENOENT | PVFS_ERROR_BMI)
#define BMI_EINTR           (PVFS_EINTR | PVFS_ERROR_BMI)
#define BMI_EIO             (PVFS_EIO | PVFS_ERROR_BMI)
#define BMI_ENXIO           (PVFS_ENXIO | PVFS_ERROR_BMI)
#define BMI_EBADF           (PVFS_EBADF | PVFS_ERROR_BMI)
#define BMI_EAGAIN          (PVFS_EAGAIN | PVFS_ERROR_BMI)
#define BMI_ENOMEM          (PVFS_ENOMEM | PVFS_ERROR_BMI)
#define BMI_EFAULT          (PVFS_EFAULT | PVFS_ERROR_BMI)
#define BMI_EBUSY           (PVFS_EBUSY | PVFS_ERROR_BMI)
#define BMI_EEXIST          (PVFS_EEXIST | PVFS_ERROR_BMI)
#define BMI_ENODEV          (PVFS_ENODEV | PVFS_ERROR_BMI)
#define BMI_ENOTDIR         (PVFS_ENOTDIR | PVFS_ERROR_BMI)
#define BMI_EISDIR          (PVFS_EISDIR | PVFS_ERROR_BMI)
#define BMI_EINVAL          (PVFS_EINVAL | PVFS_ERROR_BMI)
#define BMI_EMFILE          (PVFS_EMFILE | PVFS_ERROR_BMI)
#define BMI_EFBIG           (PVFS_EFBIG | PVFS_ERROR_BMI)
#define BMI_ENOSPC          (PVFS_ENOSPC | PVFS_ERROR_BMI)
#define BMI_EROFS           (PVFS_EROFS | PVFS_ERROR_BMI)
#define BMI_EMLINK          (PVFS_EMLINK | PVFS_ERROR_BMI)
#define BMI_EPIPE           (PVFS_EPIPE | PVFS_ERROR_BMI)
#define BMI_EDEADLK         (PVFS_EDEADLK | PVFS_ERROR_BMI)
#define BMI_ENAMETOOLONG    (PVFS_ENAMETOOLONG | PVFS_ERROR_BMI)
#define BMI_ENOLCK          (PVFS_ENOLCK | PVFS_ERROR_BMI)
#define BMI_ENOSYS          (PVFS_ENOSYS | PVFS_ERROR_BMI)
#define BMI_ENOTEMPTY       (PVFS_ENOTEMPTY | PVFS_ERROR_BMI)
#define BMI_ELOOP           (PVFS_ELOOP | PVFS_ERROR_BMI)
#define BMI_EWOULDBLOCK     (PVFS_EWOULDBLOCK | PVFS_ERROR_BMI)
#define BMI_ENOMSG          (PVFS_ENOMSG | PVFS_ERROR_BMI)
#define BMI_EUNATCH         (PVFS_EUNATCH | PVFS_ERROR_BMI)
#define BMI_EBADR           (PVFS_EBADR | PVFS_ERROR_BMI)
#define BMI_EDEADLOCK       (PVFS_EDEADLOCK | PVFS_ERROR_BMI)
#define BMI_ENODATA         (PVFS_ENODATA | PVFS_ERROR_BMI)
#define BMI_ETIME           (PVFS_ETIME | PVFS_ERROR_BMI)
#define BMI_ENONET          (PVFS_ENONET | PVFS_ERROR_BMI)
#define BMI_EREMOTE         (PVFS_EREMOTE | PVFS_ERROR_BMI)
#define BMI_ECOMM           (PVFS_ECOMM | PVFS_ERROR_BMI)
#define BMI_EPROTO          (PVFS_EPROTO | PVFS_ERROR_BMI)
#define BMI_EBADMSG         (PVFS_EBADMSG | PVFS_ERROR_BMI)
#define BMI_EOVERFLOW       (PVFS_EOVERFLOW | PVFS_ERROR_BMI)
#define BMI_ERESTART        (PVFS_ERESTART | PVFS_ERROR_BMI)
#define BMI_EMSGSIZE        (PVFS_EMSGSIZE | PVFS_ERROR_BMI)
#define BMI_EPROTOTYPE      (PVFS_EPROTOTYPE | PVFS_ERROR_BMI)
#define BMI_ENOPROTOOPT     (PVFS_ENOPROTOOPT | PVFS_ERROR_BMI)
#define BMI_EPROTONOSUPPORT (PVFS_EPROTONOSUPPORT | PVFS_ERROR_BMI)
#define BMI_EOPNOTSUPP      (PVFS_EOPNOTSUPP | PVFS_ERROR_BMI)
#define BMI_EADDRINUSE      (PVFS_EADDRINUSE | PVFS_ERROR_BMI)
#define BMI_EADDRNOTAVAIL   (PVFS_EADDRNOTAVAIL | PVFS_ERROR_BMI)
#define BMI_ENETDOWN        (PVFS_ENETDOWN | PVFS_ERROR_BMI)
#define BMI_ENETUNREACH     (PVFS_ENETUNREACH | PVFS_ERROR_BMI)
#define BMI_ENETRESET       (PVFS_ENETRESET | PVFS_ERROR_BMI)
#define BMI_ENOBUFS         (PVFS_ENOBUFS | PVFS_ERROR_BMI)
#define BMI_ETIMEDOUT       (PVFS_ETIMEDOUT | PVFS_ERROR_BMI)
#define BMI_ECONNREFUSED    (PVFS_ECONNREFUSED | PVFS_ERROR_BMI)
#define BMI_EHOSTDOWN       (PVFS_EHOSTDOWN | PVFS_ERROR_BMI)
#define BMI_EHOSTUNREACH    (PVFS_EHOSTUNREACH | PVFS_ERROR_BMI)
#define BMI_EALREADY        (PVFS_EALREADY | PVFS_ERROR_BMI)
#define BMI_EREMOTEIO       (PVFS_EREMOTEIO | PVFS_ERROR_BMI)
#define BMI_ENOMEDIUM       (PVFS_ENOMEDIUM | PVFS_ERROR_BMI)
#define BMI_EMEDIUMTYPE     (PVFS_EMEDIUMTYPE | PVFS_ERROR_BMI)

#endif /* __BMI_TYPES_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
