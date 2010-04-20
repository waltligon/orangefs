/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \addtogroup bmiint
 *
 * @{
 */

/** \file
 *  Types and other defines used throughout BMI.  Many of the BMI
 *  types are defined in terms of PVFS2 types rather than using the PVFS2
 *  types themselves.  This is because we wanted to separate the BMI
 *  package, in case it was useful in other projects.  It may be that at
 *  some later date we will make a pass through BMI and eliminate many of
 *  these "extra" types.
 */

#ifndef __BMI_TYPES_H
#define __BMI_TYPES_H

#include <stdlib.h>

typedef int64_t bmi_size_t;         /**< Data region size */
typedef int32_t bmi_msg_tag_t;      /**< User-specified message tag */
typedef int64_t bmi_context_id;     /**< Context identifier */
typedef int64_t bmi_op_id_t;        /**< Reference to ongoing network op */
typedef struct PVFS_hint_s *bmi_hint;


#ifdef __PVFS2_TYPES_H
typedef PVFS_BMI_addr_t BMI_addr_t;
#else
typedef int64_t BMI_addr_t;
#endif

/* TODO: not using a real type for this yet; need to specify what
 * error codes look like */
typedef int32_t bmi_error_code_t; /**< error code information */

#define BMI_MAX_ADDR_LEN 256

/** BMI method initialization flags */
enum
{
    BMI_INIT_SERVER = 1, /**< set up to listen for unexpected messages */
    BMI_TCP_BIND_SPECIFIC = 2, /**< bind to a specific IP address if INIT_SERVER */
    BMI_AUTO_REF_COUNT = 4 /**< automatically increment ref count on unexpected messages */
};

enum bmi_op_type
{
    BMI_SEND = 1,
    BMI_RECV = 2
};

/** BMI memory buffer flags */
enum bmi_buffer_type
{
    BMI_PRE_ALLOC = 1,
    BMI_EXT_ALLOC = 2
};

/** BMI get_info and set_info options */
enum
{
    BMI_DROP_ADDR = 1,         /**< ask a module to discard an address */
    BMI_CHECK_INIT = 2,        /**< see if a module is initialized */
    BMI_CHECK_MAXSIZE = 3,     /**< check the max msg size a module allows */
    BMI_GET_METH_ADDR = 4,     /**< kludge to return void* pointer to
                                *   underlying module address */
    BMI_INC_ADDR_REF = 5,      /**< increment address reference count */
    BMI_DEC_ADDR_REF = 6,      /**< decrement address reference count */
    BMI_DROP_ADDR_QUERY = 7,   /**< ask a module if it thinks an address
                                *   should be discarded */
    BMI_FORCEFUL_CANCEL_MODE = 8, /**< enables a more forceful cancel mode */
#ifdef USE_TRUSTED
    BMI_TRUSTED_CONNECTION = 9, /**< allows setting the TrustedPorts and Network options */
#endif
    BMI_GET_UNEXP_SIZE = 10,     /**< get the maximum unexpected payload */
    BMI_TCP_BUFFER_SEND_SIZE = 11,
    BMI_TCP_BUFFER_RECEIVE_SIZE = 12,
    BMI_TCP_CLOSE_SOCKET = 13,
    BMI_OPTIMISTIC_BUFFER_REG = 14,
    BMI_TCP_CHECK_UNEXPECTED = 15,
    BMI_ZOID_POST_TIMEOUT = 16
};

enum BMI_io_type
{
    BMI_IO_READ  = 1,
    BMI_IO_WRITE = 2
};

/** used to describe a memory region in passing down a registration
 * hint from IO routines. */
struct bmi_optimistic_buffer_info {
    const void *buffer;
    bmi_size_t len;
    enum BMI_io_type rw;
};

#define BMI_ERROR_BIT  (1 << 30)
#define BMI_NON_ERRNO_ERROR_BIT (1 << 29)
#define BMIE(num) (num|BMI_ERROR_BIT)
#define BMI_NON_ERROR_BIT   (BMI_NON_ERRNO_ERROR_BIT|BMI_ERROR_BIT)

#ifndef BMI_ERROR
#define BMI_ERROR    (1 << 7)  /* BMI-specific error */
#endif

/* mappings from PVFS errors to BMI errors */
#define BMI_EPERM           (BMIE(1) | BMI_ERROR)
#define BMI_ENOENT          (BMIE(2) | BMI_ERROR)
#define BMI_EINTR           (BMIE(3) | BMI_ERROR)
#define BMI_EIO             (BMIE(4) | BMI_ERROR)
#define BMI_ENXIO           (BMIE(5) | BMI_ERROR)
#define BMI_EBADF           (BMIE(6) | BMI_ERROR)
#define BMI_EAGAIN          (BMIE(7) | BMI_ERROR)
#define BMI_ENOMEM          (BMIE(8) | BMI_ERROR)
#define BMI_EFAULT          (BMIE(9) | BMI_ERROR)
#define BMI_EBUSY           (BMIE(10) | BMI_ERROR)
#define BMI_EEXIST          (BMIE(11) | BMI_ERROR)
#define BMI_ENODEV          (BMIE(12) | BMI_ERROR)
#define BMI_ENOTDIR         (BMIE(13) | BMI_ERROR)
#define BMI_EISDIR          (BMIE(14) | BMI_ERROR)
#define BMI_EINVAL          (BMIE(15) | BMI_ERROR)
#define BMI_EMFILE          (BMIE(16) | BMI_ERROR)
#define BMI_EFBIG           (BMIE(17) | BMI_ERROR)
#define BMI_ENOSPC          (BMIE(18) | BMI_ERROR)
#define BMI_EROFS           (BMIE(19) | BMI_ERROR)
#define BMI_EMLINK          (BMIE(20) | BMI_ERROR)
#define BMI_EPIPE           (BMIE(21) | BMI_ERROR)
#define BMI_EDEADLK         (BMIE(22) | BMI_ERROR)
#define BMI_ENAMETOOLONG    (BMIE(23) | BMI_ERROR)
#define BMI_ENOLCK          (BMIE(24) | BMI_ERROR)
#define BMI_ENOSYS          (BMIE(25) | BMI_ERROR)
#define BMI_ENOTEMPTY       (BMIE(26) | BMI_ERROR)
#define BMI_ELOOP           (BMIE(27) | BMI_ERROR)
#define BMI_EWOULDBLOCK     (BMIE(28) | BMI_ERROR)
#define BMI_ENOMSG          (BMIE(29) | BMI_ERROR)
#define BMI_EUNATCH         (BMIE(30) | BMI_ERROR)
#define BMI_EBADR           (BMIE(31) | BMI_ERROR)
#define BMI_EDEADLOCK       (BMIE(32) | BMI_ERROR)
#define BMI_ENODATA         (BMIE(33) | BMI_ERROR)
#define BMI_ETIME           (BMIE(34) | BMI_ERROR)
#define BMI_ENONET          (BMIE(35) | BMI_ERROR)
#define BMI_EREMOTE         (BMIE(36) | BMI_ERROR)
#define BMI_ECOMM           (BMIE(37) | BMI_ERROR)
#define BMI_EPROTO          (BMIE(38) | BMI_ERROR)
#define BMI_EBADMSG         (BMIE(39) | BMI_ERROR)
#define BMI_EOVERFLOW       (BMIE(40) | BMI_ERROR)
#define BMI_ERESTART        (BMIE(41) | BMI_ERROR)
#define BMI_EMSGSIZE        (BMIE(42) | BMI_ERROR)
#define BMI_EPROTOTYPE      (BMIE(43) | BMI_ERROR)
#define BMI_ENOPROTOOPT     (BMIE(44) | BMI_ERROR)
#define BMI_EPROTONOSUPPORT (BMIE(45) | BMI_ERROR)
#define BMI_EOPNOTSUPP      (BMIE(46) | BMI_ERROR)
#define BMI_EADDRINUSE      (BMIE(47) | BMI_ERROR)
#define BMI_EADDRNOTAVAIL   (BMIE(48) | BMI_ERROR)
#define BMI_ENETDOWN        (BMIE(49) | BMI_ERROR)
#define BMI_ENETUNREACH     (BMIE(50) | BMI_ERROR)
#define BMI_ENETRESET       (BMIE(51) | BMI_ERROR)
#define BMI_ENOBUFS         (BMIE(52) | BMI_ERROR)
#define BMI_ETIMEDOUT       (BMIE(53) | BMI_ERROR)
#define BMI_ECONNREFUSED    (BMIE(54) | BMI_ERROR)
#define BMI_EHOSTDOWN       (BMIE(55) | BMI_ERROR)
#define BMI_EHOSTUNREACH    (BMIE(56) | BMI_ERROR)
#define BMI_EALREADY        (BMIE(57) | BMI_ERROR)
#define BMI_EACCES          (BMIE(58) | BMI_ERROR)
#define BMI_ECONNRESET      (BMIE(59) | BMI_ERROR)

#define BMI_ECANCEL	    ((1|BMI_NON_ERROR_BIT) | BMI_ERROR)
#define BMI_EDEVINIT	    ((2|BMI_NON_ERROR_BIT) | BMI_ERROR)
#define BMI_EDETAIL	    ((3|BMI_NON_ERROR_BIT) | BMI_ERROR)
#define BMI_EHOSTNTFD	    ((4|BMI_NON_ERROR_BIT) | BMI_ERROR)
#define BMI_EADDRNTFD	    ((5|BMI_NON_ERROR_BIT) | BMI_ERROR)
#define BMI_ENORECVR	    ((6|BMI_NON_ERROR_BIT) | BMI_ERROR)
#define BMI_ETRYAGAIN	    ((7|BMI_NON_ERROR_BIT) | BMI_ERROR)

/** default bmi error translation function */
int bmi_errno_to_pvfs(int error);

#endif /* __BMI_TYPES_H */

/* @} */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
