/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
/* NOTE: if you make any changes to the encoding definitions in this file, 
 * please update the PVFS2_PROTO_VERSION in pvfs2-req-proto.h accordingly
 */

/** \file
 *
 *  Definitions of types used throughout PVFS2.
 */
#ifndef __PVFS2_TYPES_H
#define __PVFS2_TYPES_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <limits.h>
#include <errno.h>
#endif

#ifndef INT32_MAX
/* definition taken from stdint.h */
#define INT32_MAX (2147483647)
#endif

#ifndef NAME_MAX
#define NAME_MAX 255
#endif

/* figure out the size of a pointer */
#if defined(__WORDSIZE)
  #define PVFS2_SIZEOF_VOIDP __WORDSIZE
#elif defined(BITS_PER_LONG)
  #define PVFS2_SIZEOF_VOIDP BITS_PER_LONG
#elif defined(INTPTR_MIN)
  #if   INTPTR_MIN == INT32_MIN
    #define PVFS2_SIZEOF_VOIDP 32
  #elif INTPTR_MIN == INT64_MIN
    #define PVFS2_SIZEOF_VOIDP 64
  #endif
#else
  #error "Unhandled size of void pointer"
#endif

/* we need to align some variables in 32bit case to match alignment
 * in 64bit case
 */
#if PVFS2_SIZEOF_VOIDP == 32
#define PVFS2_ALIGN_VAR(_type, _name) \
    _type _name; \
    int32_t __pad##_name
#else
#define PVFS2_ALIGN_VAR(_type, _name) _type _name
#endif

/* empty stubs to turn off encoding definition generation */
#include "pvfs2-encode-stubs.h"
#include "pvfs2-hint.h"

/* Basic types used throughout the code. */
typedef uint8_t PVFS_boolean;
typedef int32_t PVFS_error;
typedef int64_t PVFS_offset;
typedef int64_t PVFS_size;
typedef int64_t PVFS_id_gen_t;

/** Opaque value representing a destination address. */
typedef int64_t PVFS_BMI_addr_t;

inline void encode_PVFS_BMI_addr_t(char **pptr, const PVFS_BMI_addr_t *x);
inline int encode_PVFS_BMI_addr_t_size_check(const PVFS_BMI_addr_t *x);
inline void decode_PVFS_BMI_addr_t(char **pptr, PVFS_BMI_addr_t *x);

#define encode_PVFS_error encode_int32_t
#define decode_PVFS_error decode_int32_t
#define encode_PVFS_offset encode_int64_t
#define decode_PVFS_offset decode_int64_t
#define encode_PVFS_size encode_int64_t
#define decode_PVFS_size decode_int64_t
#define encode_PVFS_id_gen_t encode_int64_t
#define decode_PVFS_id_gen_t decode_int64_t

/* Basic types used by communication subsystems. */
typedef int32_t PVFS_msg_tag_t;
typedef PVFS_id_gen_t PVFS_context_id;

enum PVFS_flowproto_type
{
    FLOWPROTO_DUMP_OFFSETS = 1,
    FLOWPROTO_BMI_CACHE = 2,
    FLOWPROTO_MULTIQUEUE = 3
};
#define FLOWPROTO_DEFAULT FLOWPROTO_MULTIQUEUE

/* supported wire encoding types */
enum PVFS_encoding_type
{
    ENCODING_DIRECT = 1,
    ENCODING_LE_BFIELD = 2,
    ENCODING_XDR = 3
};

/* these values must correspond to the defined encoding types above */
#define ENCODING_INVALID_MIN                    0
#define ENCODING_INVALID_MAX                    4
#define ENCODING_SUPPORTED_MIN ENCODING_LE_BFIELD
#define ENCODING_SUPPORTED_MAX ENCODING_LE_BFIELD
#define ENCODING_IS_VALID(enc_type)      \
((enc_type > ENCODING_INVALID_MIN) &&    \
 (enc_type < ENCODING_INVALID_MAX))
#define ENCODING_IS_SUPPORTED(enc_type)  \
((enc_type >= ENCODING_SUPPORTED_MIN) && \
 (enc_type <= ENCODING_SUPPORTED_MAX))
#define ENCODING_DEFAULT ENCODING_LE_BFIELD

/* basic types used by storage subsystem */

/** Unique identifier for an object on a PVFS2 file system. */
typedef uint64_t PVFS_handle;

/** Identifier for a specific PVFS2 file system; administrator
 *  must guarantee that these are unique in the context of all
 *  PVFS2 file systems reachable by a given client.
 */
typedef int32_t PVFS_fs_id;
typedef uint64_t PVFS_ds_position;
typedef int32_t PVFS_ds_flags;


#define encode_PVFS_handle encode_uint64_t
#define decode_PVFS_handle decode_uint64_t
#define encode_PVFS_fs_id encode_int32_t
#define decode_PVFS_fs_id decode_int32_t
#define decode_PVFS_ds_position decode_uint64_t
#define encode_PVFS_ds_position encode_uint64_t

/* Basic types used within metadata. */
typedef uint32_t PVFS_uid;
typedef uint32_t PVFS_gid;
typedef uint64_t PVFS_time;
typedef uint32_t PVFS_permissions;
typedef uint64_t PVFS_flags;
#define encode_PVFS_uid encode_uint32_t
#define decode_PVFS_uid decode_uint32_t
#define encode_PVFS_gid encode_uint32_t
#define decode_PVFS_gid decode_uint32_t
#define encode_PVFS_time encode_int64_t
#define decode_PVFS_time decode_int64_t
#define encode_PVFS_permissions encode_uint32_t
#define decode_PVFS_permissions decode_uint32_t
#define encode_PVFS_flags encode_uint64_t
#define decode_PVFS_flags decode_uint64_t

/* contiguous range of handles */
typedef struct
{
    PVFS_handle first;
    PVFS_handle last;
} PVFS_handle_extent;
endecode_fields_2(
    PVFS_handle_extent,
    PVFS_handle, first,
    PVFS_handle, last);

/* an array of contiguous ranges of handles */
typedef struct
{
    uint32_t extent_count;
    PVFS_handle_extent *extent_array;
} PVFS_handle_extent_array;
endecode_fields_1a(
    PVFS_handle_extent_array,
    skip4,,
    uint32_t, extent_count,
    PVFS_handle_extent, extent_array);

/* Layout algorithm for converting from server lists in the config
 * to a list of servers to use to store datafiles for a file.
 */
enum PVFS_sys_layout_algorithm
{
    /* order the datafiles according to the server list */
    PVFS_SYS_LAYOUT_NONE = 1,

    /* choose the first datafile randomly, and then round-robin in-order */
    PVFS_SYS_LAYOUT_ROUND_ROBIN = 2,

    /* choose each datafile randomly */
    PVFS_SYS_LAYOUT_RANDOM = 3,

    /* order the datafiles based on the list specified */
    PVFS_SYS_LAYOUT_LIST = 4
};
#define PVFS_SYS_LAYOUT_DEFAULT NULL

/* The list of datafile servers that can be passed into PVFS_sys_create
 * to specify the exact layout of a file.  The count parameter will override
 * the num_dfiles field in the attribute.
 */
struct PVFS_sys_server_list
{
    int32_t count;
    PVFS_BMI_addr_t *servers;
};

/* The server laout struct passed to PVFS_sys_create.  The algorithm
 * specifies how the servers are chosen to layout the file.  If the
 * algorithm is set to PVFS_SYS_LAYOUT_LIST, the server_list parameter
 * is used to determine the layout.
 */
typedef struct PVFS_sys_layout_s
{
    /* The algorithm to use to layout the file */
    enum PVFS_sys_layout_algorithm algorithm;

    /* The server list specified if the
     * PVFS_SYS_LAYOUT_LIST algorithm is chosen.
     */
    struct PVFS_sys_server_list server_list;
} PVFS_sys_layout;
#define extra_size_PVFS_sys_layout PVFS_REQ_LIMIT_LAYOUT

inline void encode_PVFS_sys_layout(char **pptr, const struct PVFS_sys_layout_s *x);
inline void decode_PVFS_sys_layout(char **pptr, struct PVFS_sys_layout_s *x);

/* predefined special values for types */
#define PVFS_CONTEXT_NULL    ((PVFS_context_id)-1)
#define PVFS_HANDLE_NULL     ((PVFS_handle)0)
#define PVFS_FS_ID_NULL       ((PVFS_fs_id)0)
#define PVFS_OP_NULL         ((PVFS_id_gen_t)0)
#define PVFS_BMI_ADDR_NULL ((PVFS_BMI_addr_t)0)
#define PVFS_ITERATE_START    (INT32_MAX - 1)
#define PVFS_ITERATE_END      (INT32_MAX - 2)
#define PVFS_READDIR_START PVFS_ITERATE_START
#define PVFS_READDIR_END   PVFS_ITERATE_END

#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif

/* permission bits */
#define PVFS_O_EXECUTE (1 << 0)
#define PVFS_O_WRITE   (1 << 1)
#define PVFS_O_READ    (1 << 2)
#define PVFS_G_EXECUTE (1 << 3)
#define PVFS_G_WRITE   (1 << 4)
#define PVFS_G_READ    (1 << 5)
#define PVFS_U_EXECUTE (1 << 6)
#define PVFS_U_WRITE   (1 << 7)
#define PVFS_U_READ    (1 << 8)
/* no PVFS_U_VTX (sticky bit) */
#define PVFS_G_SGID    (1 << 10)
#define PVFS_U_SUID    (1 << 11)

/* valid permission mask */
#define PVFS_PERM_VALID \
(PVFS_O_EXECUTE | PVFS_O_WRITE | PVFS_O_READ | PVFS_G_EXECUTE | \
 PVFS_G_WRITE | PVFS_G_READ | PVFS_U_EXECUTE | PVFS_U_WRITE | \
 PVFS_U_READ | PVFS_G_SGID | PVFS_U_SUID)

#define PVFS_USER_ALL  (PVFS_U_EXECUTE|PVFS_U_WRITE|PVFS_U_READ)
#define PVFS_GROUP_ALL (PVFS_G_EXECUTE|PVFS_G_WRITE|PVFS_G_READ)
#define PVFS_OTHER_ALL (PVFS_O_EXECUTE|PVFS_O_WRITE|PVFS_O_READ)

#define PVFS_ALL_EXECUTE (PVFS_U_EXECUTE|PVFS_G_EXECUTE|PVFS_O_EXECUTE)
#define PVFS_ALL_WRITE   (PVFS_U_WRITE|PVFS_G_WRITE|PVFS_O_WRITE)
#define PVFS_ALL_READ    (PVFS_U_READ|PVFS_G_READ|PVFS_O_READ)

/** Object and attribute types. */
typedef enum
{
    PVFS_TYPE_NONE =              0,
    PVFS_TYPE_METAFILE =    (1 << 0),
    PVFS_TYPE_DATAFILE =    (1 << 1),
    PVFS_TYPE_DIRECTORY =   (1 << 2),
    PVFS_TYPE_SYMLINK =     (1 << 3),
    PVFS_TYPE_DIRDATA =     (1 << 4),
    PVFS_TYPE_INTERNAL =    (1 << 5)   /* for the server's private use */
} PVFS_ds_type;

#define decode_PVFS_ds_type decode_enum
#define encode_PVFS_ds_type encode_enum

#ifdef __KERNEL__
#include <linux/fs.h>
#endif

#if defined(FS_IMMUTABLE_FL)

#define PVFS_IMMUTABLE_FL FS_IMMUTABLE_FL
#define PVFS_APPEND_FL    FS_APPEND_FL
#define PVFS_NOATIME_FL   FS_NOATIME_FL

#else

/* PVFS Object Flags (PVFS_flags); Add more as we implement them */
#define PVFS_IMMUTABLE_FL 0x10ULL
#define PVFS_APPEND_FL    0x20ULL
#define PVFS_NOATIME_FL   0x80ULL

#endif

/* Key/Value Pairs */
/* Extended attributes are stored on objects with */
/* a Key/Value pair.  A key or a value is simply */
/* a byte string of some length.  Keys are normally */
/* strings, and thus are printable ASCII and NULL */
/* terminated.  Values are any sequence of bytes */
/* and are user interpreted.  This struct represents */
/* EITHER a key or a value.  This struct is IDENTICAL */
/* to a TROVE_keyval_s defined in src/io/trove/trove-types.h */
/* but is duplicated here to maintain separation between */
/* the Trove implementation and PVFS2.  This struct should */
/* be used everywhere but within Trove. WBL 6/2005*/
typedef struct PVFS_ds_keyval_s
{
        void *buffer;      /* points to actual key or value */
        int32_t buffer_sz; /* the size of the area pointed to by buffer */
        int32_t read_sz;   /* when reading, the actual number of bytes read */
                           /* only valid after a read */
} PVFS_ds_keyval;

typedef struct
{
    uint32_t count;
} PVFS_ds_keyval_handle_info;

/* attribute masks used by system interface callers */
#define PVFS_ATTR_SYS_SIZE                  (1 << 20)
#define PVFS_ATTR_SYS_LNK_TARGET            (1 << 24)
#define PVFS_ATTR_SYS_DFILE_COUNT           (1 << 25)
#define PVFS_ATTR_SYS_DIRENT_COUNT          (1 << 26)
#define PVFS_ATTR_SYS_DIR_HINT              (1 << 27)
#define PVFS_ATTR_SYS_BLKSIZE               (1 << 28)
#define PVFS_ATTR_SYS_UID                   (1 << 0)
#define PVFS_ATTR_SYS_GID                   (1 << 1)
#define PVFS_ATTR_SYS_PERM                  (1 << 2)
#define PVFS_ATTR_SYS_ATIME                 (1 << 3)
#define PVFS_ATTR_SYS_CTIME                 (1 << 4)
#define PVFS_ATTR_SYS_MTIME                 (1 << 5)
#define PVFS_ATTR_SYS_TYPE                  (1 << 6)
#define PVFS_ATTR_SYS_ATIME_SET             (1 << 7)
#define PVFS_ATTR_SYS_MTIME_SET             (1 << 8)
#define PVFS_ATTR_SYS_COMMON_ALL \
(PVFS_ATTR_SYS_UID   | PVFS_ATTR_SYS_GID   | \
 PVFS_ATTR_SYS_PERM  | PVFS_ATTR_SYS_ATIME | \
 PVFS_ATTR_SYS_CTIME | PVFS_ATTR_SYS_MTIME | \
 PVFS_ATTR_SYS_TYPE)

#define PVFS_ATTR_SYS_ALL                    \
(PVFS_ATTR_SYS_COMMON_ALL | PVFS_ATTR_SYS_SIZE | \
 PVFS_ATTR_SYS_LNK_TARGET | PVFS_ATTR_SYS_DFILE_COUNT | \
 PVFS_ATTR_SYS_DIRENT_COUNT | PVFS_ATTR_SYS_DIR_HINT | PVFS_ATTR_SYS_BLKSIZE)
#define PVFS_ATTR_SYS_ALL_NOHINT                \
(PVFS_ATTR_SYS_COMMON_ALL | PVFS_ATTR_SYS_SIZE | \
 PVFS_ATTR_SYS_LNK_TARGET | PVFS_ATTR_SYS_DFILE_COUNT | \
 PVFS_ATTR_SYS_DIRENT_COUNT | PVFS_ATTR_SYS_BLKSIZE)
#define PVFS_ATTR_SYS_ALL_NOSIZE                   \
(PVFS_ATTR_SYS_COMMON_ALL | PVFS_ATTR_SYS_LNK_TARGET | \
 PVFS_ATTR_SYS_DFILE_COUNT | PVFS_ATTR_SYS_DIRENT_COUNT \
 | PVFS_ATTR_SYS_DIR_HINT | PVFS_ATTR_SYS_BLKSIZE)
#define PVFS_ATTR_SYS_ALL_SETABLE \
(PVFS_ATTR_SYS_COMMON_ALL-PVFS_ATTR_SYS_TYPE) 
#define PVFS_ATTR_SYS_ALL_TIMES \
((PVFS_ATTR_SYS_COMMON_ALL-PVFS_ATTR_SYS_TYPE) | PVFS_ATTR_SYS_ATIME_SET | PVFS_ATTR_SYS_MTIME_SET)


/* Extended attribute flags */
#define PVFS_XATTR_CREATE  0x1
#define PVFS_XATTR_REPLACE 0x2

/* Extended attribute flags */
#define PVFS_XATTR_CREATE  0x1
#define PVFS_XATTR_REPLACE 0x2

/** statfs and misc. server statistic information. */
typedef struct
{
    PVFS_fs_id fs_id;
    PVFS_size bytes_available;
    PVFS_size bytes_total;
    uint64_t ram_total_bytes;
    uint64_t ram_free_bytes;
    uint64_t load_1;
    uint64_t load_5;
    uint64_t load_15;
    uint64_t uptime_seconds;
    uint64_t handles_available_count;
    uint64_t handles_total_count;
} PVFS_statfs;
endecode_fields_12(
    PVFS_statfs,
    skip4,,
    PVFS_fs_id, fs_id,
    PVFS_size, bytes_available,
    PVFS_size, bytes_total,
    uint64_t, ram_total_bytes,
    uint64_t, ram_free_bytes,
    uint64_t, load_1,
    uint64_t, load_5,
    uint64_t, load_15,
    uint64_t, uptime_seconds,
    uint64_t, handles_available_count,
    uint64_t, handles_total_count);

/** object reference (uniquely refers to a single file, directory, or
    symlink).
*/
typedef struct
{
    PVFS_handle handle;
    PVFS_fs_id fs_id;
    int32_t    __pad1;
} PVFS_object_ref;

/** Credentials (stubbed for future authentication methods). */
typedef struct
{
    PVFS_uid uid;
    PVFS_gid gid;
} PVFS_credentials;
endecode_fields_2(
    PVFS_credentials,
    PVFS_uid, uid,
    PVFS_gid, gid);

/* max length of BMI style URI's for identifying servers */
#define PVFS_MAX_SERVER_ADDR_LEN 256
/* max length of PVFS filename */
#define PVFS_NAME_MAX            256
/* max len of individual path element */
#define PVFS_SEGMENT_MAX         PVFS_NAME_MAX

/* max extended attribute name len as imposed by the VFS and exploited for the
 * upcall request types.
 * NOTE: Please retain them as multiples of 8 even if you wish to change them
 * This is *NECESSARY* for supporting 32 bit user-space binaries on a 64-bit kernel.
 */
#define PVFS_MAX_XATTR_NAMELEN   256 /* Not the same as XATTR_NAME_MAX defined
                                        by <linux/xattr.h> */
#define PVFS_MAX_XATTR_VALUELEN  8192 /* Not the same as XATTR_SIZE_MAX defined
                                        by <linux/xattr.h> */ 
#define PVFS_MAX_XATTR_LISTLEN   8    /* Not the same as XATTR_LIST_MAX
                                          defined by <linux/xattr.h> */

/* This structure is used by the VFS-client interaction alone */
typedef struct {
    char key[PVFS_MAX_XATTR_NAMELEN];
    int32_t  key_sz; /* int32_t for portable, fixed-size structures */
    int32_t  val_sz;
    char val[PVFS_MAX_XATTR_VALUELEN];
} PVFS_keyval_pair;

/** Directory entry contents. */
typedef struct
{
    char d_name[PVFS_NAME_MAX + 1];
    PVFS_handle handle;
} PVFS_dirent;
endecode_fields_2(
    PVFS_dirent,
    here_string, d_name,
    PVFS_handle, handle);

/** Predefined server parameters that can be manipulated at run-time
 *  through the mgmt interface.
 */
enum PVFS_server_param
{
    PVFS_SERV_PARAM_INVALID = 0,
    PVFS_SERV_PARAM_GOSSIP_MASK = 1, /* gossip debugging on or off */
    PVFS_SERV_PARAM_FSID_CHECK = 2,  /* verify that an fsid is ok */
    PVFS_SERV_PARAM_ROOT_CHECK = 3,  /* verify existance of root handle */
    PVFS_SERV_PARAM_MODE = 4,        /* change the current server mode */
    PVFS_SERV_PARAM_EVENT_ENABLE = 5,    /* event enable */
    PVFS_SERV_PARAM_EVENT_DISABLE = 6, /* event disable */
    PVFS_SERV_PARAM_SYNC_META = 7,   /* metadata sync flags */
    PVFS_SERV_PARAM_SYNC_DATA = 8,   /* file data sync flags */
    PVFS_SERV_PARAM_DROP_CACHES = 9
};

enum PVFS_mgmt_param_type
{
    PVFS_MGMT_PARAM_TYPE_UINT64,
    PVFS_MGMT_PARAM_TYPE_STRING
} ;

struct PVFS_mgmt_setparam_value
{
    enum PVFS_mgmt_param_type type;
    union
    {
        uint64_t value;
        char *string_value;
    } u;
};

encode_enum_union_2_struct(
    PVFS_mgmt_setparam_value,
    type, u,
    uint64_t, value,        PVFS_MGMT_PARAM_TYPE_UINT64,
    string,   string_value, PVFS_MGMT_PARAM_TYPE_STRING);

enum PVFS_server_mode
{
    PVFS_SERVER_NORMAL_MODE = 1,      /* default server operating mode */
    PVFS_SERVER_ADMIN_MODE = 2        /* administrative mode */
};

/* PVFS2 ACL structures */
typedef struct {
    int32_t  p_tag;
    uint32_t p_perm;
    uint32_t p_id;
} pvfs2_acl_entry;

/* These defines match that of the POSIX defines */
#define PVFS2_ACL_UNDEFINED_ID   (-1)

/* p_tag entry in struct posix_acl_entry */
#define PVFS2_ACL_USER_OBJ    (0x01)
#define PVFS2_ACL_USER        (0x02)
#define PVFS2_ACL_GROUP_OBJ   (0x04)
#define PVFS2_ACL_GROUP       (0x08)
#define PVFS2_ACL_MASK        (0x10)
#define PVFS2_ACL_OTHER       (0x20)

/* permissions in the p_perm field */
#define PVFS2_ACL_READ       (0x04)
#define PVFS2_ACL_WRITE      (0x02)
#define PVFS2_ACL_EXECUTE    (0x01)

/* PVFS2 errors
 *
 * Errors are made up of a code to indicate the error type and a class
 * that indicates where the error came from.  These are |'d together.
 */
int PVFS_strerror_r(int errnum, char *buf, int n);
void PVFS_perror(const char *text, int retcode);
void PVFS_perror_gossip(const char* text, int retcode);
PVFS_error PVFS_get_errno_mapping(PVFS_error error);
PVFS_error PVFS_errno_to_error(int err);

/* special bits used to differentiate PVFS error codes from system
 * errno values
 */
#define PVFS_ERROR_BIT           (1 << 30)
#define PVFS_NON_ERRNO_ERROR_BIT (1 << 29)
#define IS_PVFS_ERROR(__error)            \
((__error)&(PVFS_ERROR_BIT))
#define IS_PVFS_NON_ERRNO_ERROR(__error)  \
(((__error)&(PVFS_NON_ERRNO_ERROR_BIT)) && IS_PVFS_ERROR(__error))

/* 7 bits are used for the errno mapped error codes */
#define PVFS_ERROR_CODE(__error) \
((__error) & (PVFS_error)(0x7f|PVFS_ERROR_BIT))
#define PVFS_ERROR_CLASS(__error) \
((__error) & ~((PVFS_error)(0x7f|PVFS_ERROR_BIT|PVFS_NON_ERRNO_ERROR_BIT)))
#define PVFS_NON_ERRNO_ERROR_CODE(__error) \
((__error) & (PVFS_error)(127|PVFS_ERROR_BIT|PVFS_NON_ERRNO_ERROR_BIT))

#define PVFS_ERROR_BMI    (1 << 7) /* BMI-specific error */
#define PVFS_ERROR_TROVE  (2 << 7) /* Trove-specific error */
#define PVFS_ERROR_FLOW   (3 << 7)
#define PVFS_ERROR_SM     (4 << 7) /* state machine specific error */
#define PVFS_ERROR_SCHED  (5 << 7)
#define PVFS_ERROR_CLIENT (6 << 7)
#define PVFS_ERROR_DEV    (7 << 7) /* device file interaction */

#define PVFS_ERROR_CLASS_BITS                                          \
(PVFS_ERROR_BMI | PVFS_ERROR_TROVE | PVFS_ERROR_FLOW | PVFS_ERROR_SM | \
 PVFS_ERROR_SCHED | PVFS_ERROR_CLIENT | PVFS_ERROR_DEV)

/* a shorthand to make the error code definitions more readable */
#define E(num) (num|PVFS_ERROR_BIT)

/* PVFS2 error codes, compliments of asm/errno.h */
#define PVFS_EPERM            E(1) /* Operation not permitted */
#define PVFS_ENOENT           E(2) /* No such file or directory */
#define PVFS_EINTR            E(3) /* Interrupted system call */
#define PVFS_EIO              E(4) /* I/O error */
#define PVFS_ENXIO            E(5) /* No such device or address */
#define PVFS_EBADF            E(6) /* Bad file number */
#define PVFS_EAGAIN           E(7) /* Try again */
#define PVFS_ENOMEM           E(8) /* Out of memory */
#define PVFS_EFAULT           E(9) /* Bad address */
#define PVFS_EBUSY           E(10) /* Device or resource busy */
#define PVFS_EEXIST          E(11) /* File exists */
#define PVFS_ENODEV          E(12) /* No such device */
#define PVFS_ENOTDIR         E(13) /* Not a directory */
#define PVFS_EISDIR          E(14) /* Is a directory */
#define PVFS_EINVAL          E(15) /* Invalid argument */
#define PVFS_EMFILE          E(16) /* Too many open files */
#define PVFS_EFBIG           E(17) /* File too large */
#define PVFS_ENOSPC          E(18) /* No space left on device */
#define PVFS_EROFS           E(19) /* Read-only file system */
#define PVFS_EMLINK          E(20) /* Too many links */
#define PVFS_EPIPE           E(21) /* Broken pipe */
#define PVFS_EDEADLK         E(22) /* Resource deadlock would occur */
#define PVFS_ENAMETOOLONG    E(23) /* File name too long */
#define PVFS_ENOLCK          E(24) /* No record locks available */
#define PVFS_ENOSYS          E(25) /* Function not implemented */
#define PVFS_ENOTEMPTY       E(26) /* Directory not empty */
#define PVFS_ELOOP           E(27) /* Too many symbolic links encountered */
#define PVFS_EWOULDBLOCK     E(28) /* Operation would block */
#define PVFS_ENOMSG          E(29) /* No message of desired type */
#define PVFS_EUNATCH         E(30) /* Protocol driver not attached */
#define PVFS_EBADR           E(31) /* Invalid request descriptor */
#define PVFS_EDEADLOCK       E(32)
#define PVFS_ENODATA         E(33) /* No data available */
#define PVFS_ETIME           E(34) /* Timer expired */
#define PVFS_ENONET          E(35) /* Machine is not on the network */
#define PVFS_EREMOTE         E(36) /* Object is remote */
#define PVFS_ECOMM           E(37) /* Communication error on send */
#define PVFS_EPROTO          E(38) /* Protocol error */
#define PVFS_EBADMSG         E(39) /* Not a data message */
#define PVFS_EOVERFLOW       E(40) /* Value too large for defined data type */
#define PVFS_ERESTART        E(41) /* Interrupted system call should be restarted */
#define PVFS_EMSGSIZE        E(42) /* Message too long */
#define PVFS_EPROTOTYPE      E(43) /* Protocol wrong type for socket */
#define PVFS_ENOPROTOOPT     E(44) /* Protocol not available */
#define PVFS_EPROTONOSUPPORT E(45) /* Protocol not supported */
#define PVFS_EOPNOTSUPP      E(46) /* Operation not supported on transport endpoint */
#define PVFS_EADDRINUSE      E(47) /* Address already in use */
#define PVFS_EADDRNOTAVAIL   E(48) /* Cannot assign requested address */
#define PVFS_ENETDOWN        E(49) /* Network is down */
#define PVFS_ENETUNREACH     E(50) /* Network is unreachable */
#define PVFS_ENETRESET       E(51) /* Network dropped connection because of reset */
#define PVFS_ENOBUFS         E(52) /* No buffer space available */
#define PVFS_ETIMEDOUT       E(53) /* Connection timed out */
#define PVFS_ECONNREFUSED    E(54) /* Connection refused */
#define PVFS_EHOSTDOWN       E(55) /* Host is down */
#define PVFS_EHOSTUNREACH    E(56) /* No route to host */
#define PVFS_EALREADY        E(57) /* Operation already in progress */
#define PVFS_EACCES          E(58) /* Access not allowed */
#define PVFS_ECONNRESET      E(59) /* Connection reset by peer */

/***************** non-errno/pvfs2 specific error codes *****************/
#define PVFS_ECANCEL    (1|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_EDEVINIT   (2|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_EDETAIL    (3|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_EHOSTNTFD  (4|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_EADDRNTFD  (5|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_ENORECVR   (6|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_ETRYAGAIN  (7|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))

/* NOTE: PLEASE DO NOT ARBITRARILY ADD NEW ERRNO ERROR CODES!
 *
 * IF YOU CHOOSE TO ADD A NEW ERROR CODE (DESPITE OUR PLEA), YOU ALSO
 * NEED TO INCREMENT PVFS_ERRNO MAX (BELOW) AND ADD A MAPPING TO A
 * UNIX ERRNO VALUE IN THE MACROS BELOW (USED IN
 * src/common/misc/errno-mapping.c and the kernel module)
 */
#define PVFS_ERRNO_MAX          60

/*
 * If system headers do not define these, assign them, with arbitrary
 * numbers.  These values must be unique with respect to defined errors
 * and each other to avoid collisions in case statements elsewhere.
 */
#ifndef EUNATCH
#define EUNATCH -6060842
#endif

#ifndef EBADR
#define EBADR -6060843
#endif

#ifndef EDEADLOCK
#define EDEADLOCK -6060844
#endif

#ifndef ENONET
#define ENONET -6060845
#endif

#ifndef ECOMM
#define ECOMM -6060846
#endif

#ifndef ERESTART
#define ERESTART -6060847
#endif

#ifndef ETIME
#define ETIME -6060848
#endif

#ifndef EBADMSG
#define EBADMSG -6060849
#endif

#define DECLARE_ERRNO_MAPPING()                       \
PVFS_error PINT_errno_mapping[PVFS_ERRNO_MAX + 1] = { \
    0,     /* leave this one empty */                 \
    EPERM, /* 1 */                                    \
    ENOENT,                                           \
    EINTR,                                            \
    EIO,                                              \
    ENXIO,                                            \
    EBADF,                                            \
    EAGAIN,                                           \
    ENOMEM,                                           \
    EFAULT,                                           \
    EBUSY, /* 10 */                                   \
    EEXIST,                                           \
    ENODEV,                                           \
    ENOTDIR,                                          \
    EISDIR,                                           \
    EINVAL,                                           \
    EMFILE,                                           \
    EFBIG,                                            \
    ENOSPC,                                           \
    EROFS,                                            \
    EMLINK, /* 20 */                                  \
    EPIPE,                                            \
    EDEADLK,                                          \
    ENAMETOOLONG,                                     \
    ENOLCK,                                           \
    ENOSYS,                                           \
    ENOTEMPTY,                                        \
    ELOOP,                                            \
    EWOULDBLOCK,                                      \
    ENOMSG,                                           \
    EUNATCH, /* 30 */                                 \
    EBADR,                                            \
    EDEADLOCK,                                        \
    ENODATA,                                          \
    ETIME,                                            \
    ENONET,                                           \
    EREMOTE,                                          \
    ECOMM,                                            \
    EPROTO,                                           \
    EBADMSG,                                          \
    EOVERFLOW, /* 40 */                               \
    ERESTART,                                         \
    EMSGSIZE,                                         \
    EPROTOTYPE,                                       \
    ENOPROTOOPT,                                      \
    EPROTONOSUPPORT,                                  \
    EOPNOTSUPP,                                       \
    EADDRINUSE,                                       \
    EADDRNOTAVAIL,                                    \
    ENETDOWN,                                         \
    ENETUNREACH, /* 50 */                             \
    ENETRESET,                                        \
    ENOBUFS,                                          \
    ETIMEDOUT,                                        \
    ECONNREFUSED,                                     \
    EHOSTDOWN,                                        \
    EHOSTUNREACH,                                     \
    EALREADY,                                         \
    EACCES,                                           \
    ECONNRESET,   /* 59 */                            \
    0         /* PVFS_ERRNO_MAX */                    \
};                                                    \
const char *PINT_non_errno_strerror_mapping[] = {     \
    "Success", /* 0 */                                \
    "Operation cancelled (possibly due to timeout)",  \
    "Device initialization failed",                   \
    "Detailed per-server errors are available",       \
    "Unknown host",                                   \
    "No address associated with name",                \
    "Unknown server error",                           \
    "Host name lookup failure",                       \
};                                                    \
PVFS_error PINT_non_errno_mapping[] = {               \
    0,     /* leave this one empty */                 \
    PVFS_ECANCEL,   /* 1 */                           \
    PVFS_EDEVINIT,  /* 2 */                           \
    PVFS_EDETAIL,   /* 3 */                           \
    PVFS_EHOSTNTFD, /* 4 */                           \
    PVFS_EADDRNTFD, /* 5 */                           \
    PVFS_ENORECVR,  /* 6 */                           \
    PVFS_ETRYAGAIN, /* 7 */                           \
}

/*
  NOTE: PVFS_get_errno_mapping will convert a PVFS_ERROR_CODE to an
  errno value.  If the error code is a pvfs2 specific error code
  (i.e. a PVFS_NON_ERRNO_ERROR_CODE), PVFS_get_errno_mapping will
  return an index into the PINT_non_errno_strerror_mapping array which
  can be used for getting the pvfs2 specific strerror message given
  the error code.  if the value is not a recognized error code, the
  passed in value will be returned unchanged.
*/
#define DECLARE_ERRNO_MAPPING_AND_FN()                     \
extern PVFS_error PINT_errno_mapping[];                    \
extern PVFS_error PINT_non_errno_mapping[];                \
extern const char *PINT_non_errno_strerror_mapping[];      \
PVFS_error PVFS_get_errno_mapping(PVFS_error error)        \
{                                                          \
    PVFS_error ret = error, mask = 0;                      \
    int32_t positive = ((error > -1) ? 1 : 0);             \
    if (IS_PVFS_NON_ERRNO_ERROR((positive? error: -error)))\
    {                                                      \
    mask = (PVFS_NON_ERRNO_ERROR_BIT | PVFS_ERROR_BIT |    \
            PVFS_ERROR_CLASS_BITS);                        \
    ret = PVFS_NON_ERRNO_ERROR_CODE(                       \
          ((positive ? error : abs(error))) & ~mask);      \
    }                                                      \
    else if (IS_PVFS_ERROR((positive? error: -error)))     \
    {                                                      \
    mask = (PVFS_ERROR_BIT | PVFS_ERROR_CLASS_BITS);       \
    ret = PINT_errno_mapping[                              \
        PVFS_ERROR_CODE(((positive ? error :               \
                             abs(error))) & ~mask)];       \
    }                                                      \
    return ret;                        			   \
}                                                          \
PVFS_error PVFS_errno_to_error(int err)                    \
{                                                          \
    PVFS_error e = 0;                                      \
    \
    for(; e < PVFS_ERRNO_MAX; ++e)                         \
    {                                                      \
        if(PINT_errno_mapping[e] == err)                   \
        {                                                  \
            return e;                                      \
        }                                                  \
    }                                                      \
    return 0;                                              \
}                                                          \
DECLARE_ERRNO_MAPPING()
#define PVFS_ERROR_TO_ERRNO(__error) PVFS_get_errno_mapping(__error)

/** These structures/calls are used when returning detailed lists of
 *  errors from a particular call.  This is done to report on specific,
 *  per-server failures.
 */
typedef struct
{
    PVFS_error error;
    PVFS_BMI_addr_t addr;
} PVFS_error_server;

typedef struct
{
    int count_allocated;
    int count_used;
    int count_exceeded; /* set if we ran out of space for errors */
    /* structure is alloc'd larger for more errors */
    PVFS_error_server error[1];
} PVFS_error_details;

PVFS_error_details *PVFS_error_details_new(int count);
void PVFS_error_details_free(PVFS_error_details *details);

/** PVFS I/O operation types, used in both system and server interfaces.
 */
enum PVFS_io_type
{
    PVFS_IO_READ  = 1,
    PVFS_IO_WRITE = 2
};

/*
 * Filesystem "magic" number unique to PVFS2 kernel interface.  Used by
 * ROMIO to auto-detect access method given a mounted path.
 */
#define PVFS2_SUPER_MAGIC 0x20030528

/* flag value that can be used with mgmt_iterate_handles to retrieve
 * reserved handle values
 */
#define PVFS_MGMT_RESERVED 1

#endif /* __PVFS2_TYPES_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
