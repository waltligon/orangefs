/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \addtogroup troveint
 *
 * @{
 */

/** \file
 *  Types and other defines used throughout Trove.  Many of the Trove
 *  types are defined in terms of PVFS2 types rather than using the
 *  PVFS2 types themselves.  This is because we wanted to separate the
 *  Trove package, in case it was useful in other projects.  It may be
 *  that at some later date we will make a pass through Trove and
 *  eliminate many of these "extra" types.
 */

#ifndef __TROVE_TYPES_H
#define __TROVE_TYPES_H

/* PVFS type mappings */
#include "pvfs3-handle.h"
#include "pvfs2-internal.h"
#include "pvfs2-types.h"
#include "pvfs2-storage.h"

typedef PVFS_handle                TROVE_handle;
/* V3 */
#if 0
typedef PVFS_handle_extent         TROVE_extent;
typedef PVFS_handle_extent         TROVE_handle_extent;
typedef PVFS_handle_extent_array   TROVE_handle_extent_array;
#endif
typedef PVFS_size                  TROVE_size;
typedef PVFS_offset                TROVE_offset;
typedef PVFS_id_gen_t              TROVE_op_id;
typedef PVFS_fs_id                 TROVE_coll_id;
typedef PVFS_ds_type               TROVE_ds_type;
typedef PVFS_vtag                  TROVE_vtag_s;
typedef PVFS_ds_flags              TROVE_ds_flags;
typedef PVFS_ds_keyval             TROVE_keyval_s;
typedef PVFS_ds_keyval_handle_info TROVE_keyval_handle_info;
typedef PVFS_ds_position           TROVE_ds_position;
typedef PVFS_ds_attributes         TROVE_ds_attributes_s;
typedef PVFS_ds_attributes         TROVE_ds_attributes;
typedef PVFS_error                 TROVE_ds_state;
typedef PVFS_context_id            TROVE_context_id;
typedef PVFS_statfs		   TROVE_statfs;
typedef PVFS_coll_getinfo_options  TROVE_coll_getinfo_options;
typedef PVFS_object_ref            TROVE_object_ref;

typedef enum
{
    TROVE_METHOD_DBPF = 0,
    TROVE_METHOD_DBPF_ALTAIO,
    TROVE_METHOD_DBPF_NULLAIO,
    TROVE_METHOD_DBPF_DIRECTIO
} TROVE_method_id;

typedef TROVE_method_id (*TROVE_method_callback)(TROVE_coll_id);

#define TROVE_HANDLE_NULL          PVFS_HANDLE_NULL
#define TROVE_HANDLE_NULL_INIT     PVFS_HANDLE_NULL_INIT
#define TROVE_COLL_ID_NULL         PVFS_FS_ID_NULL

#define trove_ds_attr_to_stored PVFS_ds_attr_to_stored
#define trove_ds_stored_to_attr PVFS_ds_stored_to_attr

/* These are not error codes, they are  non-error codes,
 * add no bits
 */
#define TROVE_SUCCESS         PVFS_SUCCESS
#define TROVE_ERROR           PVFS_ERROR

/* These return codes are for comparison functions
 */
#define TROVE_EQ              0
#define TROVE_LT              -1
#define TROVE_GT              1

/* mappings from PVFS errors to TROVE errors */
#define TROVE_EPERM           (PVFS_EPERM | PVFS_ERROR_TROVE)
#define TROVE_ENOENT          (PVFS_ENOENT | PVFS_ERROR_TROVE)
#define TROVE_EINTR           (PVFS_EINTR | PVFS_ERROR_TROVE)
#define TROVE_EIO             (PVFS_EIO | PVFS_ERROR_TROVE)
#define TROVE_ENXIO           (PVFS_ENXIO | PVFS_ERROR_TROVE)
#define TROVE_EBADF           (PVFS_EBADF | PVFS_ERROR_TROVE)
#define TROVE_EAGAIN          (PVFS_EAGAIN | PVFS_ERROR_TROVE)
#define TROVE_ENOMEM          (PVFS_ENOMEM | PVFS_ERROR_TROVE)
#define TROVE_EFAULT          (PVFS_EFAULT | PVFS_ERROR_TROVE)
#define TROVE_EBUSY           (PVFS_EBUSY | PVFS_ERROR_TROVE)
#define TROVE_EEXIST          (PVFS_EEXIST | PVFS_ERROR_TROVE)
#define TROVE_ENODEV          (PVFS_ENODEV | PVFS_ERROR_TROVE)
#define TROVE_ENOTDIR         (PVFS_ENOTDIR | PVFS_ERROR_TROVE)
#define TROVE_EISDIR          (PVFS_EISDIR | PVFS_ERROR_TROVE)
#define TROVE_EINVAL          (PVFS_EINVAL | PVFS_ERROR_TROVE)
#define TROVE_EMFILE          (PVFS_EMFILE | PVFS_ERROR_TROVE)
#define TROVE_EFBIG           (PVFS_EFBIG | PVFS_ERROR_TROVE)
#define TROVE_ENOSPC          (PVFS_ENOSPC | PVFS_ERROR_TROVE)
#define TROVE_EROFS           (PVFS_EROFS | PVFS_ERROR_TROVE)
#define TROVE_EMLINK          (PVFS_EMLINK | PVFS_ERROR_TROVE)
#define TROVE_EPIPE           (PVFS_EPIPE | PVFS_ERROR_TROVE)
#define TROVE_EDEADLK         (PVFS_EDEADLK | PVFS_ERROR_TROVE)
#define TROVE_ENAMETOOLONG    (PVFS_ENAMETOOLONG | PVFS_ERROR_TROVE)
#define TROVE_ENOLCK          (PVFS_ENOLCK | PVFS_ERROR_TROVE)
#define TROVE_ENOSYS          (PVFS_ENOSYS | PVFS_ERROR_TROVE)
#define TROVE_ENOTEMPTY       (PVFS_ENOTEMPTY | PVFS_ERROR_TROVE)
#define TROVE_ELOOP           (PVFS_ELOOP | PVFS_ERROR_TROVE)
#define TROVE_EWOULDBLOCK     (PVFS_EWOULDBLOCK | PVFS_ERROR_TROVE)
#define TROVE_ENOMSG          (PVFS_ENOMSG | PVFS_ERROR_TROVE)
#define TROVE_EUNATCH         (PVFS_EUNATCH | PVFS_ERROR_TROVE)
#define TROVE_EBADR           (PVFS_EBADR | PVFS_ERROR_TROVE)
#define TROVE_EDEADLOCK       (PVFS_EDEADLOCK | PVFS_ERROR_TROVE)
#define TROVE_ENODATA         (PVFS_ENODATA | PVFS_ERROR_TROVE)
#define TROVE_ETIME           (PVFS_ETIME | PVFS_ERROR_TROVE)
#define TROVE_ENONET          (PVFS_ENONET | PVFS_ERROR_TROVE)
#define TROVE_EREMOTE         (PVFS_EREMOTE | PVFS_ERROR_TROVE)
#define TROVE_ECOMM           (PVFS_ECOMM | PVFS_ERROR_TROVE)
#define TROVE_EPROTO          (PVFS_EPROTO | PVFS_ERROR_TROVE)
#define TROVE_EBADMSG         (PVFS_EBADMSG | PVFS_ERROR_TROVE)
#define TROVE_EOVERFLOW       (PVFS_EOVERFLOW | PVFS_ERROR_TROVE)
#define TROVE_ERESTART        (PVFS_ERESTART | PVFS_ERROR_TROVE)
#define TROVE_EMSGSIZE        (PVFS_EMSGSIZE | PVFS_ERROR_TROVE)
#define TROVE_EPROTOTYPE      (PVFS_EPROTOTYPE | PVFS_ERROR_TROVE)
#define TROVE_ENOPROTOOPT     (PVFS_ENOPROTOOPT | PVFS_ERROR_TROVE)
#define TROVE_EPROTONOSUPPORT (PVFS_EPROTONOSUPPORT | PVFS_ERROR_TROVE)
#define TROVE_EOPNOTSUPP      (PVFS_EOPNOTSUPP | PVFS_ERROR_TROVE)
#define TROVE_EADDRINUSE      (PVFS_EADDRINUSE | PVFS_ERROR_TROVE)
#define TROVE_EADDRNOTAVAIL   (PVFS_EADDRNOTAVAIL | PVFS_ERROR_TROVE)
#define TROVE_ENETDOWN        (PVFS_ENETDOWN | PVFS_ERROR_TROVE)
#define TROVE_ENETUNREACH     (PVFS_ENETUNREACH | PVFS_ERROR_TROVE)
#define TROVE_ENETRESET       (PVFS_ENETRESET | PVFS_ERROR_TROVE)
#define TROVE_ENOBUFS         (PVFS_ENOBUFS | PVFS_ERROR_TROVE)
#define TROVE_ETIMEDOUT       (PVFS_ETIMEDOUT | PVFS_ERROR_TROVE)
#define TROVE_ECONNREFUSED    (PVFS_ECONNREFUSED | PVFS_ERROR_TROVE)
#define TROVE_EHOSTDOWN       (PVFS_EHOSTDOWN | PVFS_ERROR_TROVE)
#define TROVE_EHOSTUNREACH    (PVFS_EHOSTUNREACH | PVFS_ERROR_TROVE)
#define TROVE_EALREADY        (PVFS_EALREADY | PVFS_ERROR_TROVE)
#define TROVE_EREMOTEIO       (PVFS_EREMOTEIO | PVFS_ERROR_TROVE)
#define TROVE_ENOMEDIUM       (PVFS_ENOMEDIUM | PVFS_ERROR_TROVE)
#define TROVE_EMEDIUMTYPE     (PVFS_EMEDIUMTYPE | PVFS_ERROR_TROVE)
#define TROVE_ECANCEL         (PVFS_ECANCEL | PVFS_ERROR_TROVE)
#define TROVE_EACCES          (PVFS_EACCES | PVFS_ERROR_TROVE)
#define TROVE_ERANGE          (PVFS_ERANGE | PVFS_ERROR_TROVE)

#endif

/* @} */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
