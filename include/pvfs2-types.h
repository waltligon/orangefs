/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_TYPES_H
#define __PVFS2_TYPES_H

#include "pvfs2-config.h"

#ifdef __KERNEL__
#ifndef __WORDSIZE
#include <asm/types.h>
#define __WORDSIZE BITS_PER_LONG
#endif
#include <linux/types.h>
#else
#include <stdint.h>
#endif /* __KERNEL__ */

#ifndef INT32_MAX
/* definition taken from stdint.h */
#define INT32_MAX (2147483647)
#endif

/* empty stubs to turn off encoding definition generation */
#include "pvfs2-encode-stubs.h"

/* basic types used throughout code */
typedef uint8_t PVFS_boolean;
typedef int32_t PVFS_error;
typedef int64_t PVFS_offset;
typedef int64_t PVFS_size;
typedef int64_t PVFS_id_gen_t;
#define encode_PVFS_error encode_int32_t
#define decode_PVFS_error decode_int32_t
#define encode_PVFS_offset encode_int64_t
#define decode_PVFS_offset decode_int64_t
#define encode_PVFS_size encode_int64_t
#define decode_PVFS_size decode_int64_t
#define encode_PVFS_id_gen_t encode_int64_t
#define decode_PVFS_id_gen_t decode_int64_t

/* basic types used by communication subsystems */
typedef int32_t PVFS_msg_tag_t;
typedef int32_t PVFS_context_id;
enum PVFS_flowproto_type
{
    FLOWPROTO_BMI_TROVE = 1,
    FLOWPROTO_DUMP_OFFSETS = 2,
    FLOWPROTO_BMI_CACHE = 3,
    FLOWPROTO_MULTIQUEUE = 4
};
#define FLOWPROTO_DEFAULT FLOWPROTO_MULTIQUEUE

/* supported wire encoding types */
enum PVFS_encoding_type
{
    ENCODING_DIRECT = 0,
    ENCODING_LE_BFIELD = 1,
    ENCODING_XDR = 2
};
#define ENCODING_DEFAULT ENCODING_LE_BFIELD

/* basic types used by storage subsystem */
typedef uint64_t PVFS_handle;
typedef int32_t PVFS_fs_id;
typedef int32_t PVFS_ds_position;
typedef int32_t PVFS_ds_flags;
#define encode_PVFS_handle encode_uint64_t
#define decode_PVFS_handle decode_uint64_t
#define encode_PVFS_fs_id encode_int32_t
#define decode_PVFS_fs_id decode_int32_t
#define decode_PVFS_ds_position decode_int32_t
#define encode_PVFS_ds_position encode_int32_t

/* basic types used within metadata */
typedef uint32_t PVFS_uid;
typedef uint32_t PVFS_gid;
typedef int64_t PVFS_time;
typedef uint32_t PVFS_permissions;
#define encode_PVFS_uid encode_uint32_t
#define decode_PVFS_uid decode_uint32_t
#define encode_PVFS_gid encode_uint32_t
#define decode_PVFS_gid decode_uint32_t
#define encode_PVFS_time encode_int64_t
#define decode_PVFS_time decode_int64_t
#define encode_PVFS_permissions encode_uint32_t
#define decode_PVFS_permissions decode_uint32_t

/* some limits */
/* TODO: is there a better place for this stuff?  maybe merge with
 * limits at the top of pvfs2-req-proto.h somewhere?
 */
/* max length of BMI style URI's for identifying servers */
#define PVFS_MAX_SERVER_ADDR_LEN 256

/* contiguous range of handles */
struct PVFS_handle_extent_s
{
    PVFS_handle first;
    PVFS_handle last;
};
typedef struct PVFS_handle_extent_s PVFS_handle_extent;
endecode_fields_2(PVFS_handle_extent,
  PVFS_handle, first,
  PVFS_handle, last);

/* an array of contiguous ranges of handles */
struct PVFS_handle_extent_array_s
{
    uint32_t extent_count;
    PVFS_handle_extent *extent_array;
};
typedef struct PVFS_handle_extent_array_s PVFS_handle_extent_array;
endecode_fields_0a(PVFS_handle_extent_array,
  uint32_t, extent_count,
  PVFS_handle_extent, extent_array);

/* predefined special values for types */
#define PVFS_HANDLE_NULL ((PVFS_handle)0) /* invalid object handle */
#define PVFS_OP_NULL ((id_gen_t)0)        /* invalid op id for I/O subsystems */
#define PVFS_ITERATE_START (INT32_MAX - 1)
#define PVFS_ITERATE_END   (INT32_MAX - 2)
#define PVFS_READDIR_START PVFS_ITERATE_START

/* permission bits */
#define PVFS_O_EXECUTE	(1 << 0)
#define PVFS_O_WRITE	(1 << 1)
#define PVFS_O_READ	(1 << 2)
#define PVFS_G_EXECUTE	(1 << 3)
#define PVFS_G_WRITE	(1 << 4)
#define PVFS_G_READ	(1 << 5)
#define PVFS_U_EXECUTE	(1 << 6)
#define PVFS_U_WRITE	(1 << 7)
#define PVFS_U_READ	(1 << 8)

/* object and attribute types */
enum PVFS_ds_type_e
{
    PVFS_TYPE_NONE =              0,
    PVFS_TYPE_METAFILE =    (1 << 0),
    PVFS_TYPE_DATAFILE =    (1 << 1),
    PVFS_TYPE_DIRECTORY =   (1 << 2),
    PVFS_TYPE_SYMLINK =	    (1 << 3),
    PVFS_TYPE_DIRDATA =	    (1 << 4)
};
typedef enum PVFS_ds_type_e PVFS_ds_type;
#define decode_PVFS_ds_type decode_enum
#define encode_PVFS_ds_type encode_enum

/* internal attribute masks, common to all obj types */
#define PVFS_ATTR_COMMON_UID	(1 << 0)
#define PVFS_ATTR_COMMON_GID	(1 << 1)
#define PVFS_ATTR_COMMON_PERM	(1 << 2)
#define PVFS_ATTR_COMMON_ATIME	(1 << 3)
#define PVFS_ATTR_COMMON_CTIME	(1 << 4)
#define PVFS_ATTR_COMMON_MTIME	(1 << 5)
#define PVFS_ATTR_COMMON_TYPE	(1 << 6)
#define PVFS_ATTR_COMMON_ALL                       \
(PVFS_ATTR_COMMON_UID   | PVFS_ATTR_COMMON_GID   | \
 PVFS_ATTR_COMMON_PERM  | PVFS_ATTR_COMMON_ATIME | \
 PVFS_ATTR_COMMON_CTIME | PVFS_ATTR_COMMON_MTIME | \
 PVFS_ATTR_COMMON_TYPE)

/* internal attribute masks for metadata objects */
#define PVFS_ATTR_META_DIST	(1 << 10)
#define PVFS_ATTR_META_DFILES	(1 << 11)
#define PVFS_ATTR_META_ALL	(PVFS_ATTR_META_DIST | PVFS_ATTR_META_DFILES)

/* internal attribute masks for datafile objects */
#define PVFS_ATTR_DATA_SIZE	(1 << 15)
#define PVFS_ATTR_DATA_ALL	PVFS_ATTR_DATA_SIZE

/* internal attribute masks for symlink objects */
#define PVFS_ATTR_SYMLNK_TARGET	(1 << 18)
#define PVFS_ATTR_SYMLNK_ALL	PVFS_ATTR_SYMLNK_TARGET

/* attribute masks used by system interface callers */
#define PVFS_ATTR_SYS_SIZE	 (1 << 20)
#define PVFS_ATTR_SYS_LNK_TARGET (1 << 24)
#define PVFS_ATTR_SYS_DFILE_COUNT (1 << 25)
#define PVFS_ATTR_SYS_UID	PVFS_ATTR_COMMON_UID
#define PVFS_ATTR_SYS_GID	PVFS_ATTR_COMMON_GID
#define PVFS_ATTR_SYS_PERM	PVFS_ATTR_COMMON_PERM
#define PVFS_ATTR_SYS_ATIME	PVFS_ATTR_COMMON_ATIME
#define PVFS_ATTR_SYS_CTIME	PVFS_ATTR_COMMON_CTIME
#define PVFS_ATTR_SYS_MTIME	PVFS_ATTR_COMMON_MTIME
#define PVFS_ATTR_SYS_TYPE	PVFS_ATTR_COMMON_TYPE
#define PVFS_ATTR_SYS_ALL       \
(PVFS_ATTR_COMMON_ALL | PVFS_ATTR_SYS_SIZE | PVFS_ATTR_SYS_LNK_TARGET | PVFS_ATTR_SYS_DFILE_COUNT)
#define PVFS_ATTR_SYS_ALL_NOSIZE (PVFS_ATTR_COMMON_ALL | PVFS_ATTR_SYS_LNK_TARGET | PVFS_ATTR_SYS_DFILE_COUNT)
#define PVFS_ATTR_SYS_ALL_SETABLE (PVFS_ATTR_COMMON_ALL-PVFS_ATTR_COMMON_TYPE)

/* statfs and misc server statistic information */
struct PVFS_statfs_s
{
    PVFS_fs_id fs_id;
    PVFS_size bytes_available;
    PVFS_size bytes_total;
    uint64_t ram_total_bytes;
    uint64_t ram_free_bytes;
    uint64_t uptime_seconds;
    uint64_t handles_available_count;
    uint64_t handles_total_count;
};
typedef struct PVFS_statfs_s PVFS_statfs;
endecode_fields_8(
    PVFS_statfs,
    PVFS_fs_id, fs_id,
    PVFS_size, bytes_available,
    PVFS_size, bytes_total,
    uint64_t, ram_total_bytes,
    uint64_t, ram_free_bytes,
    uint64_t, uptime_seconds,
    uint64_t, handles_available_count,
    uint64_t, handles_total_count);

/* pinode reference (uniquely refers to a single pinode) */
struct PVFS_pinode_reference_s
{
    PVFS_handle handle;
    PVFS_fs_id fs_id;
};
typedef struct PVFS_pinode_reference_s PVFS_pinode_reference;

/* credentials (stubbed for future authentication methods) */
struct PVFS_credentials_s
{
    PVFS_uid uid;
    PVFS_gid gid;
};
typedef struct PVFS_credentials_s PVFS_credentials;
endecode_fields_2(PVFS_credentials, PVFS_uid, uid, PVFS_gid, gid);

/* directory entry */
#define PVFS_NAME_MAX    256	/* max length of PVFS filename */
#define PVFS_SEGMENT_MAX 128    /* max len of individual path element */
struct PVFS_dirent_s
{
    char d_name[PVFS_NAME_MAX + 1];
    PVFS_handle handle;
};
typedef struct PVFS_dirent_s PVFS_dirent;
endecode_fields_2(PVFS_dirent, here_string, d_name, PVFS_handle, handle);

/* these are predefined server parameters that can be manipulated
 * through the mgmt interface
 */
enum PVFS_server_param
{
    PVFS_SERV_PARAM_INVALID = 0,
    PVFS_SERV_PARAM_GOSSIP_MASK = 1,  /* gossip debugging on or off */
    PVFS_SERV_PARAM_FSID_CHECK = 2,   /* verify that a specific fsid is ok */
    PVFS_SERV_PARAM_ROOT_CHECK = 3,   /* verify existance of root handle */
    PVFS_SERV_PARAM_MODE = 4,	      /* change the current server mode */
    PVFS_SERV_PARAM_EVENT_ON = 5,     /* event logging on or off */
    PVFS_SERV_PARAM_EVENT_MASKS = 6   /* API masks for event logging */
};

enum PVFS_server_mode
{
    PVFS_SERVER_NORMAL_MODE = 1,      /* default server operating mode */
    PVFS_SERVER_ADMIN_MODE = 2	      /* administrative mode */
};

/* PVFS2 errors
 *
 * Errors are made up of a code to indicate the error type and a class
 * that indicates where the error came from.  These are |'d together.
 */

void PVFS_perror(char *text,
		 int retcode);
void PVFS_perror_gossip(char* text,
			int retcode);

/* special bit used to differentiate PVFS error codes from system
 * errno values
 */
#define PVFS_ERROR_BIT    (1 << 30)
#define IS_PVFS_ERROR(__error) ((__error)&(PVFS_ERROR_BIT))

/* 7 bits are used for the error code */
#define PVFS_ERROR_CODE(__error) ((__error) & (int32_t) (0x7f|PVFS_ERROR_BIT))
#define PVFS_ERROR_CLASS(__error) ((__error) & ~((int32_t) 0x7f))

#define PVFS_ERROR_BMI    (1 << 7)	/* BMI-specific error (e.g. socket got closed ) */
#define PVFS_ERROR_TROVE  (2 << 7)	/* Trove-specific error (e.g. no space on device) */
#define PVFS_ERROR_FLOW   (3 << 7)
#define PVFS_ERROR_SM     (4 << 7)	/* state machine specific error */
#define PVFS_ERROR_SCHED  (5 << 7)
#define PVFS_ERROR_CLIENT (6 << 7)
#define PVFS_ERROR_DEV    (7 << 7)	/* device file interaction */

/* PVFS_ERROR_TO_ERRNO - macro for mapping from a PVFS error value
 * to a local UNIX errno value
 */
extern int32_t PINT_errno_mapping[];
#define PVFS_ERROR_TO_ERRNO(__error) PINT_errno_mapping[PVFS_ERROR_CODE((__error)& ~(PVFS_ERROR_BIT))]

/* PVFS2 error codes, compliments of asm/errno.h */
#define PVFS_EPERM		 (1|(PVFS_ERROR_BIT))	/* Operation not permitted */
#define PVFS_ENOENT		 (2|(PVFS_ERROR_BIT))	/* No such file or directory */
#define PVFS_EINTR		 (3|(PVFS_ERROR_BIT))	/* Interrupted system call */
#define PVFS_EIO		 (4|(PVFS_ERROR_BIT))	/* I/O error */
#define PVFS_ENXIO		 (5|(PVFS_ERROR_BIT))	/* No such device or address */
#define PVFS_EBADF		 (6|(PVFS_ERROR_BIT))	/* Bad file number */
#define PVFS_EAGAIN		 (7|(PVFS_ERROR_BIT))	/* Try again */
#define PVFS_ENOMEM		 (8|(PVFS_ERROR_BIT))	/* Out of memory */
#define PVFS_EFAULT		 (9|(PVFS_ERROR_BIT))	/* Bad address */
#define PVFS_EBUSY		(10|(PVFS_ERROR_BIT))	/* Device or resource busy */
#define PVFS_EEXIST		(11|(PVFS_ERROR_BIT))	/* File exists */
#define PVFS_ENODEV		(12|(PVFS_ERROR_BIT))	/* No such device */
#define PVFS_ENOTDIR		(13|(PVFS_ERROR_BIT))	/* Not a directory */
#define PVFS_EISDIR		(14|(PVFS_ERROR_BIT))	/* Is a directory */
#define PVFS_EINVAL		(15|(PVFS_ERROR_BIT))	/* Invalid argument */
#define PVFS_EMFILE		(16|(PVFS_ERROR_BIT))	/* Too many open files */
#define PVFS_EFBIG		(17|(PVFS_ERROR_BIT))	/* File too large */
#define PVFS_ENOSPC		(18|(PVFS_ERROR_BIT))	/* No space left on device */
#define PVFS_EROFS		(19|(PVFS_ERROR_BIT))	/* Read-only file system */
#define PVFS_EMLINK		(20|(PVFS_ERROR_BIT))	/* Too many links */
#define PVFS_EPIPE		(21|(PVFS_ERROR_BIT))	/* Broken pipe */
#define PVFS_EDEADLK		(22|(PVFS_ERROR_BIT))	/* Resource deadlock would occur */
#define PVFS_ENAMETOOLONG	(23|(PVFS_ERROR_BIT))	/* File name too long */
#define PVFS_ENOLCK		(24|(PVFS_ERROR_BIT))	/* No record locks available */
#define PVFS_ENOSYS		(25|(PVFS_ERROR_BIT))	/* Function not implemented */
#define PVFS_ENOTEMPTY	        (26|(PVFS_ERROR_BIT))	/* Directory not empty */
#define PVFS_ELOOP		(27|(PVFS_ERROR_BIT))	/* Too many symbolic links encountered */
#define PVFS_EWOULDBLOCK	(28|(PVFS_ERROR_BIT))	/* Operation would block */
#define PVFS_ENOMSG		(29|(PVFS_ERROR_BIT))	/* No message of desired type */
#define PVFS_EUNATCH		(30|(PVFS_ERROR_BIT))	/* Protocol driver not attached */
#define PVFS_EBADR		(31|(PVFS_ERROR_BIT))	/* Invalid request descriptor */
#define PVFS_EDEADLOCK	        (32|(PVFS_ERROR_BIT))
#define PVFS_ENODATA		(33|(PVFS_ERROR_BIT))	/* No data available */
#define PVFS_ETIME		(34|(PVFS_ERROR_BIT))	/* Timer expired */
#define PVFS_ENONET		(35|(PVFS_ERROR_BIT))	/* Machine is not on the network */
#define PVFS_EREMOTE		(36|(PVFS_ERROR_BIT))	/* Object is remote */
#define PVFS_ECOMM		(37|(PVFS_ERROR_BIT))	/* Communication error on send */
#define PVFS_EPROTO		(38|(PVFS_ERROR_BIT))	/* Protocol error */
#define PVFS_EBADMSG		(39|(PVFS_ERROR_BIT))	/* Not a data message */
#define PVFS_EOVERFLOW	        (40|(PVFS_ERROR_BIT))	/* Value too large for defined data type */
#define PVFS_ERESTART	        (41|(PVFS_ERROR_BIT))	/* Interrupted system call should be restarted */
#define PVFS_EMSGSIZE	        (42|(PVFS_ERROR_BIT))	/* Message too long */
#define PVFS_EPROTOTYPE	        (43|(PVFS_ERROR_BIT))	/* Protocol wrong type for socket */
#define PVFS_ENOPROTOOPT	(44|(PVFS_ERROR_BIT))	/* Protocol not available */
#define PVFS_EPROTONOSUPPORT	(45|(PVFS_ERROR_BIT))	/* Protocol not supported */
#define PVFS_EOPNOTSUPP	        (46|(PVFS_ERROR_BIT))	/* Operation not supported on transport endpoint */
#define PVFS_EADDRINUSE	        (47|(PVFS_ERROR_BIT))	/* Address already in use */
#define PVFS_EADDRNOTAVAIL	(48|(PVFS_ERROR_BIT))	/* Cannot assign requested address */
#define PVFS_ENETDOWN	        (49|(PVFS_ERROR_BIT))	/* Network is down */
#define PVFS_ENETUNREACH	(50|(PVFS_ERROR_BIT))	/* Network is unreachable */
#define PVFS_ENETRESET	        (51|(PVFS_ERROR_BIT))	/* Network dropped connection because of reset */
#define PVFS_ENOBUFS		(52|(PVFS_ERROR_BIT))	/* No buffer space available */
#define PVFS_ETIMEDOUT	        (53|(PVFS_ERROR_BIT))	/* Connection timed out */
#define PVFS_ECONNREFUSED	(54|(PVFS_ERROR_BIT))	/* Connection refused */
#define PVFS_EHOSTDOWN	        (55|(PVFS_ERROR_BIT))	/* Host is down */
#define PVFS_EHOSTUNREACH	(56|(PVFS_ERROR_BIT))	/* No route to host */
#define PVFS_EALREADY	        (57|(PVFS_ERROR_BIT))	/* Operation already in progress */

/* NOTE: PLEASE DO NOT ARBITRARILY ADD NEW ERROR CODES!
 *
 * IF YOU CHOOSE TO ADD A NEW ERROR CODE (DESPITE OUR PLEA),
 * YOU ALSO NEED TO INCREMENT PVFS_ERRNO MAX (BELOW) AND ADD
 * A MAPPING TO A UNIX ERRNO VALUE IN src/common/misc/errno-mapping.c
 */

#define PVFS_ERRNO_MAX          57

/* PVFS I/O operation types, used in both system and server interfaces */
enum PVFS_io_type {
    PVFS_IO_READ = 1,
    PVFS_IO_WRITE = 2
};

/* Printf wrappers for 32- and 64-bit compatibility.  Imagine trying
 * to print out a PVFS_handle, which is typedefed to a uint64_t.  On
 * a 32-bit machine, you use format "%Lu", while a 64-bit machine wants
 * the format "%lu", and each machine complains at the use of the opposite.
 * This is only a problem on primitive types that are bigger than the
 * smallest supported machine, i.e. bigger than 32 bits.
 *
 * Rather than changing the printf format string, which is the "right"
 * thing to do, we instead cast the parameters to the printf().  But only
 * on one of the architectures so the other one will complain if the format
 * string really is incorrect.
 *
 * Here we choose 32-bit machines as the dominant type.  If a format
 * specifier and a parameter are mismatched, that machine will issue
 * a warning, while 64-bit machines will silently perform the cast.
 */
#if (__WORDSIZE == 32)
#  define Lu(x) (x)
#  define Ld(x) (x)
#elif (__WORDSIZE == 64)
#  define Lu(x) (unsigned long long)(x)
#  define Ld(x) (long long)(x)
#else
/* If you see this, you can change the #if tests above to work around it. */
#  error Unknown wordsize, perhaps your system headers are not POSIX
#endif

#endif /* __PVFS2_TYPES_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
