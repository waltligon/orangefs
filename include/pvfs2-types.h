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
#ifdef WIN32
#include "wincommon.h"
#else
#include <sys/time.h>
#endif
#include <limits.h>
#include <errno.h>
#endif
#include <pvfs3-handle.h>

#ifndef INT32_MAX
/* definition taken from stdint.h */
#define INT32_MAX (2147483647)
#endif

#ifndef UINT32_MAX
#define UINT32_MAX (4294967295U)
#endif

#ifndef NAME_MAX
#define NAME_MAX 255
#endif
#ifndef PATH_MAX
#define PATH_MAX 4096
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
#elif defined(_WIN64)
  #define PVFS2_SIZEOF_VOIDP 64
#elif defined(WIN32)
  #define PVFS2_SIZEOF_VOIDP 32
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

extern char *gpptr; /* q&d debug */

/* Basic types used throughout the code. */
typedef uint8_t PVFS_boolean;
typedef int32_t PVFS_error;
typedef int64_t PVFS_offset;
typedef int64_t PVFS_size;
typedef int64_t PVFS_id_gen_t;

/** Opaque value representing a destination address. */
typedef int64_t PVFS_BMI_addr_t;

/* Windows - inline functions can't be exported.  And, in gcc 5.3 and newer 
 * inline functions must have a corresponding definition in a header file,
 * so we are just removing the inlines here since the compiler will inline
 * if it thinks it is advantageous.
 * */
void encode_PVFS_BMI_addr_t(char **pptr, const PVFS_BMI_addr_t *x);
int encode_PVFS_BMI_addr_t_size_check(const PVFS_BMI_addr_t *x);
void decode_PVFS_BMI_addr_t(char **pptr, PVFS_BMI_addr_t *x);
void defree_PVFS_BMI_addr_t(PVFS_BMI_addr_t *x);

/* this stuff is all good, if out of place */

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_error encode_int32_t
#define decode_PVFS_error decode_int32_t
#define defree_PVFS_error defree_int32_t
#define encode_PVFS_offset encode_int64_t
#define decode_PVFS_offset decode_int64_t
#define defree_PVFS_offset defree_int64_t
#define encode_PVFS_size encode_int64_t
#define decode_PVFS_size decode_int64_t
#define defree_PVFS_size defree_int64_t
#define encode_PVFS_id_gen_t encode_int64_t
#define decode_PVFS_id_gen_t decode_int64_t
#define defree_PVFS_id_gen_t defree_int64_t
#endif

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
#define PVFS2_ENCODING_DEFAULT ENCODING_LE_BFIELD

/* basic types used by storage subsystem */

/** Unique identifier for an object on a PVFS2 file system. */
/*typedef uint64_t PVFS_handle;*/

/*#define encode_PVFS_handle encode_uint64_t*/
/*#define decode_PVFS_handle decode_uint64_t*/

/** Unique identifier for an object on a PVFS3 file system */
typedef PVFS_OID PVFS_handle;       /* 128-bit */

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_handle encode_PVFS_OID
#define decode_PVFS_handle decode_PVFS_OID
#define defree_PVFS_handle defree_PVFS_OID
#endif

/** Identifier for a specific PVFS2 file system; administrator
 *  must guarantee that these are unique in the context of all
 *  PVFS2 file systems reachable by a given client.
 */
typedef int32_t PVFS_fs_id;
typedef uint64_t PVFS_ds_position;
typedef int32_t PVFS_ds_flags;

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_fs_id encode_int32_t
#define decode_PVFS_fs_id decode_int32_t
#define defree_PVFS_fs_id defree_int32_t
#define encode_PVFS_ds_position encode_uint64_t
#define decode_PVFS_ds_position decode_uint64_t
#define defree_PVFS_ds_position defree_uint64_t
#endif

/* Basic types used within metadata. */
typedef uint32_t PVFS_uid;
#define PVFS_UID_MAX UINT32_MAX
typedef uint32_t PVFS_gid;
#define PVFS_GID_MAX UINT32_MAX
typedef uint64_t PVFS_time;
typedef uint32_t PVFS_permissions;
typedef uint64_t PVFS_flags;
#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_uid encode_uint32_t
#define decode_PVFS_uid decode_uint32_t
#define defree_PVFS_uid defree_uint32_t
#define encode_PVFS_gid encode_uint32_t
#define decode_PVFS_gid decode_uint32_t
#define defree_PVFS_gid defree_uint32_t
#define encode_PVFS_time encode_int64_t
#define decode_PVFS_time decode_int64_t
#define defree_PVFS_time defree_int64_t
#define encode_PVFS_permissions encode_uint32_t
#define decode_PVFS_permissions decode_uint32_t
#define defree_PVFS_permissions defree_uint32_t
#define encode_PVFS_flags encode_uint64_t
#define decode_PVFS_flags decode_uint64_t
#define defree_PVFS_flags defree_uint64_t
#endif

/* V3 handle ranges defunct */
#if 0
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

#endif

/* Gossip Masks are all over the code */

typedef struct PVFS_debug_mask_s
{
    uint64_t mask1;
    uint64_t mask2;
} PVFS_debug_mask;

endecode_fields_2(
        PVFS_debug_mask,
        uint64_t, mask1,
        uint64_t, mask2);

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
    PVFS_SYS_LAYOUT_LIST = 4,

    /* order the datafiles based on the list specified */
    PVFS_SYS_LAYOUT_LOCAL = 5
};
/* These define the valid range of layout numbers */
#define PVFS_SYS_LAYOUT_NULL 0
#define PVFS_SYS_LAYOUT_MAX 5
/* This is used to sat layout if none is requested */
#define PVFS_SYS_LAYOUT_DEFAULT_ALGORITHM PVFS_SYS_LAYOUT_ROUND_ROBIN
/* This is the code for a default layout */
#define PVFS_SYS_LAYOUT_DEFAULT NULL
/* For list layout this is the largest string encoding */
#define PVFS_SYS_LIMIT_LAYOUT 4096

/* these are redefining function defs which crash horribly.  
 * The defs for these funcs are in pint-util.c
 */
#if 0
#define encode_PVFS_sys_layout_algorithm encode_enum
#define decode_PVFS_sys_layout_algorithm decode_enum
#define defree_PVFS_sys_layout_algorithm defree_enum
#endif

/* The list of datafile servers that can be passed into PVFS_sys_create
 * to specify the exact layout of a file.  The count parameter will override
 * the num_dfiles field in the attribute.
 */
struct PVFS_sys_server_list
{
    int32_t count;
    PVFS_BMI_addr_t *servers;
};

#if 0
endecode_fields_1a_struct(
    PVFS_sys_server_list,
    skip4,,
    int32_t, count,
    PVFS_BMI_addr_t, servers);
#endif

/* The server layout struct passed to PVFS_sys_create.  The algorithm
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

/* in V3 layout should never need to cross the wire to/from servers
 * layout is processed on a client machine when it picks a set of SIDs
 * for a set of OIDs.  OIDs are all selected in advance, even when the
 * object is not actually created right away.  OIDs are sent to servers
 * in order to create the objects, this is after the layout.  Thus,
 * no further need to encode or decode a layout.  
 * Layouts ARE stored on directories in hints, but at the moment this
 * is a distinct data type PVFS_dirhint_layout_s 
 * defined in proto/pvfs2-attr.c
 */
#if 0
endecode_fields_2(
    PVFS_sys_layout,
    PVFS_sys_layout_algorithm, algorithm,
    PVFS_sys_server_list, server_list);
#endif

#define extra_size_PVFS_sys_layout PVFS_SYS_LIMIT_LAYOUT

void encode_PVFS_sys_layout(char **pptr, const struct PVFS_sys_layout_s *x);
void decode_PVFS_sys_layout(char **pptr, struct PVFS_sys_layout_s *x);
void defree_PVFS_sys_layout(struct PVFS_sys_layout_s *x);

/* predefined special values for types */
#define PVFS_CONTEXT_NULL   ((PVFS_context_id)-1)
/*#define PVFS_HANDLE_NULL  ((PVFS_handle)0)*/  /* moved to pvfs2-handle.h */
#define PVFS_FS_ID_NULL     ((PVFS_fs_id)0)
#define PVFS_OP_NULL        ((PVFS_id_gen_t)0)
#define PVFS_BMI_ADDR_NULL  ((PVFS_BMI_addr_t)0)
#define PVFS_ITERATE_START  (INT32_MAX - 1)
#define PVFS_ITERATE_END    (INT32_MAX - 2)
#define PVFS_READDIR_START  PVFS_ITERATE_START
#define PVFS_READDIR_END    PVFS_ITERATE_END

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
(PVFS_O_EXECUTE | PVFS_O_WRITE | PVFS_O_READ |  \
 PVFS_G_EXECUTE | PVFS_G_WRITE | PVFS_G_READ | \
 PVFS_U_EXECUTE | PVFS_U_WRITE | PVFS_U_READ | \
 PVFS_G_SGID | PVFS_U_SUID)

#define PVFS_USER_ALL  (PVFS_U_EXECUTE|PVFS_U_WRITE|PVFS_U_READ)
#define PVFS_GROUP_ALL (PVFS_G_EXECUTE|PVFS_G_WRITE|PVFS_G_READ)
#define PVFS_OTHER_ALL (PVFS_O_EXECUTE|PVFS_O_WRITE|PVFS_O_READ)

#define PVFS_ALL_EXECUTE (PVFS_U_EXECUTE|PVFS_G_EXECUTE|PVFS_O_EXECUTE)
#define PVFS_ALL_WRITE   (PVFS_U_WRITE|PVFS_G_WRITE|PVFS_O_WRITE)
#define PVFS_ALL_READ    (PVFS_U_READ|PVFS_G_READ|PVFS_O_READ)

/** Object and attribute types. */
/* Not sure why this is encoded as bits - I can't think of a use case
 * for putting more than one type in a variable.  Maybe revisit
 * later.
 *
 * If this enum is modified the server parameters related to the precreate
 * pool batch and low threshold sizes may need to be modified to reflect
 * this change. Also, the PVFS_DS_TYPE_COUNT #define below must be updated
 */
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
#define PVFS_DS_TYPE_COUNT      7      /* total number of DS types defined in
                                        * the PVFS_ds_type enum */

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_ds_type encode_enum
#define decode_PVFS_ds_type decode_enum
#define defree_PVFS_ds_type defree_enum
#endif

/* helper to translate bit-shifted enum types to array index number in the 
 * range (0-(PVFS_DS_TYPE_COUNT-1)) */
#define PVFS_ds_type_to_int(__type, __intp)         \
do {                                                \
    uint32_t r = 0;                                 \
    PVFS_ds_type t = __type;                        \
    if( t == 0 )                                    \
    {                                               \
        *((uint32_t *)__intp) = 0;                  \
    }                                               \
    else                                            \
    {                                               \
        while( t >>= 1 )                            \
        {                                           \
            r++;                                    \
        }                                           \
        *((uint32_t *)__intp) = r + 1;              \
    }                                               \
} while( 0 )

/* helper to translate array index int to a proper PVFS_ds_type bit-shifted
 * value */
#define int_to_PVFS_ds_type(__i, __typep)           \
do {                                                \
    if( __i == 0 )                                  \
    {                                               \
        *((PVFS_ds_type *)__typep) = 0;             \
    }                                               \
    else                                            \
    {                                               \
        *((PVFS_ds_type *)__typep) = 1 << (__i - 1);\
    }                                               \
} while(0)

#ifdef __KERNEL__
#include <linux/fs.h>
#endif


/*The value for PVFS_MIRROR_FL will not conflict with the FS values.*/
#if defined(FS_IMMUTABLE_FL)

#define PVFS_IMMUTABLE_FL FS_IMMUTABLE_FL
#define PVFS_APPEND_FL    FS_APPEND_FL
#define PVFS_NOATIME_FL   FS_NOATIME_FL
#define PVFS_MIRROR_FL    0x01000000ULL

#else

/* PVFS Object Flags (PVFS_flags); Add more as we implement them */
#define PVFS_IMMUTABLE_FL 0x10ULL
#define PVFS_APPEND_FL    0x20ULL
#define PVFS_NOATIME_FL   0x80ULL
#define PVFS_MIRROR_FL    0x01000000ULL

#endif

#define ALL_FS_META_HINT_FLAGS \
   (PVFS_IMMUTABLE_FL |        \
    PVFS_APPEND_FL    |        \
    PVFS_NOATIME_FL )

/* controls how replicants are updated */
enum PVFS_mirror_mode_e { 
   BEGIN_MIRROR_MODE   = 100,
   NO_MIRRORING        = 100,
   MIRROR_ON_IMMUTABLE = 200,
   END_MIRROR_MODE     = 200
};

#define USER_PVFS2_MIRROR_HANDLES "user.pvfs2.mirror.handles"
#define USER_PVFS2_MIRROR_COPIES  "user.pvfs2.mirror.copies"
#define USER_PVFS2_MIRROR_STATUS  "user.pvfs2.mirror.status"
#define USER_PVFS2_MIRROR_MODE    "user.pvfs2.mirror.mode"

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

/* Under V3 these mask flags are the same as those used in an obj_attr
 * Thus, any change here or in src/proto/pvfs2-attr.h must be reflexted
 * in the other.  We may try to make that semi-automatic eventually
 */

/* attribute masks used by system interface callers */
#define PVFS_ATTR_SYS_UID                   (1UL << 0)
#define PVFS_ATTR_SYS_GID                   (1UL << 1)
#define PVFS_ATTR_SYS_PERM                  (1UL << 2)
#define PVFS_ATTR_SYS_ATIME                 (1UL << 3)
#define PVFS_ATTR_SYS_CTIME                 (1UL << 4)
#define PVFS_ATTR_SYS_MTIME                 (1UL << 5)
#define PVFS_ATTR_SYS_NTIME                 (1UL << 6)
#define PVFS_ATTR_SYS_TYPE                  (1UL << 7)
#define PVFS_ATTR_SYS_ATIME_SET             (1UL << 8)
#define PVFS_ATTR_SYS_CTIME_SET             (1UL << 9)
#define PVFS_ATTR_SYS_MTIME_SET             (1UL << 10)
#define PVFS_ATTR_SYS_NTIME_SET             (1UL << 11)

#define PVFS_ATTR_SYS_DFILE_COUNT           (1UL << 17)
#define PVFS_ATTR_SYS_MIRROR_COPIES_COUNT   (1UL << 18) /* dfile SID_COUNT */
#define PVFS_ATTR_SYS_MIRROR_MODE           (1UL << 19)
#define PVFS_ATTR_SYS_SIZE                  (1UL << 20) /* from meta - whole file */

#define PVFS_ATTR_SYS_LNK_TARGET            (1UL << 23)
#define PVFS_ATTR_SYS_DIRENT_COUNT          (1UL << 24)

#define PVFS_ATTR_SYS_BLKSIZE               (1UL << 28)

#define PVFS_ATTR_SYS_DIR_HINT_INIT         (1UL << 30)
#define PVFS_ATTR_SYS_DIR_HINT_MAX          (1UL << 31)
#define PVFS_ATTR_SYS_DIR_HINT_SPLIT_SIZE   (1UL << 32)
#define PVFS_ATTR_SYS_DIR_INIT              (1UL << 35)
#define PVFS_ATTR_SYS_DIR_MAX               (1UL << 36)
#define PVFS_ATTR_SYS_DIR_SPLIT_SIZE        (1UL << 40)

/* "extras" are items not in dspace but in keyval space - may be multiple items */
#define PVFS_ATTR_SYS_DISTDIR_ATTR          (1UL << 43) /* get dirdata extras */
#define PVFS_ATTR_SYS_DIR_HINT              (1UL << 44) /* get hint extras */

#define PVFS_ATTR_SYS_CAPABILITY            (1UL << 55)

#define PVFS_ATTR_SYS_FASTEST               (1UL << 62)
#define PVFS_ATTR_SYS_LATEST                (1UL << 63)

#define PVFS_ATTR_SYS_COMMON_ALL \
                 (PVFS_ATTR_SYS_UID   | \
                  PVFS_ATTR_SYS_GID   | \
                  PVFS_ATTR_SYS_PERM  | \
                  PVFS_ATTR_SYS_ATIME | \
                  PVFS_ATTR_SYS_CTIME | \
                  PVFS_ATTR_SYS_MTIME | \
                  PVFS_ATTR_SYS_NTIME | \
                  PVFS_ATTR_SYS_TYPE)

#define PVFS_ATTR_SYS_ALL            \
                 (PVFS_ATTR_SYS_COMMON_ALL |          \
                  PVFS_ATTR_SYS_DFILE_COUNT |         \
                  PVFS_ATTR_SYS_MIRROR_COPIES_COUNT | \
                  PVFS_ATTR_SYS_MIRROR_MODE |         \
                  PVFS_ATTR_SYS_SIZE |                \
                  PVFS_ATTR_SYS_LNK_TARGET |          \
                  PVFS_ATTR_SYS_DIRENT_COUNT |        \
                  PVFS_ATTR_SYS_BLKSIZE |             \
                  PVFS_ATTR_SYS_DIR_HINT_INIT |       \
                  PVFS_ATTR_SYS_DIR_HINT_MAX |        \
                  PVFS_ATTR_SYS_DIR_HINT_SPLIT_SIZE | \
                  PVFS_ATTR_SYS_DIR_INIT |            \
                  PVFS_ATTR_SYS_DIR_MAX |             \
                  PVFS_ATTR_SYS_DIR_SPLIT_SIZE |      \
                  PVFS_ATTR_SYS_DISTDIR_ATTR |        \
                  PVFS_ATTR_SYS_DIR_HINT |            \
                  PVFS_ATTR_SYS_CAPABILITY)

/* DIR HINT refers to hints that are not stored in dspace and thus need
 * an additional read on the server.  Other DIR_HINT attributes are in
 * dspace and so come with every getattr.
 */
#define PVFS_ATTR_SYS_NOHINT ~(PVFS_ATTR_SYS_DIR_HINT)

/* There are both a central size, and a distributed size on files.  The central
 * may be out of date.  the distributed requiers sending requsts to all of the
 * dfiles.  This is why we might want to ask for attributes with no size.  Central
 * size always comes.
 */
#define PVFS_ATTR_SYS_NOSIZE ~(PVFS_ATTR_SYS_SIZE)

/* DISTDIR_ATTR refers to attributes that are not in the dspace, particularly
 * lists of handes for dfiles and dirdatas, sids, bitmaps, etc.  These
 * require additional reads on server.
 */
#define PVFS_ATTR_SYS_NODISTDIR_ATTR ~(PVFS_ATTR_SYS_DISTDIR_ATTR)

#define PVFS_ATTR_SYS_ALL_NOHINT \
                 (PVFS_ATTR_SYS_ALL & PVFS_ATTR_SYS_NOHINT)

#define PVFS_ATTR_SYS_ALL_NOSIZE \
                 (PVFS_ATTR_SYS_ALL & PVFS_ATTR_SYS_NOSIZE)

#define PVFS_ATTR_SYS_ALL_NODISTDIR_ATTR \
                 (PVFS_ATTR_SYS_ALL & PVFS_ATTR_SYS_DISTDIR_ATTR)

#define PVFS_ATTR_SYS_ALL_NOHINTSIZE \
                 (PVFS_ATTR_SYS_ALL & PVFS_ATTR_SYS_NOSIZE & \
                  PVFS_ATTR_SYS_NOHINT)

#if 0
#define PVFS_ATTR_SYS_ALL_NOSIZE                   \
(PVFS_ATTR_SYS_COMMON_ALL | PVFS_ATTR_SYS_LNK_TARGET | \
 PVFS_ATTR_SYS_DFILE_COUNT | PVFS_ATTR_SYS_DIRENT_COUNT | \
 PVFS_ATTR_SYS_MIRROR_COPIES_COUNT | \
 PVFS_ATTR_SYS_DISTDIR_ATTR | \
 PVFS_ATTR_SYS_DIRENT_COUNT | \
 PVFS_ATTR_SYS_DIR_HINT | PVFS_ATTR_SYS_BLKSIZE)
#endif

/* these are read only except during a create */
#define PVFS_ATTR_SYS_READ_ONLY \
                  (PVFS_ATTR_SYS_TYPE           | \
                   PVFS_ATTR_SYS_DFILE_COUNT    | \
                   PVFS_ATTR_SYS_SIZE           | \
                   PVFS_ATTR_SYS_LNK_TARGET     | \
                   PVFS_ATTR_SYS_DIRENT_COUNT   | \
                   PVFS_ATTR_SYS_DIR_INIT       | \
                   PVFS_ATTR_SYS_DIR_MAX        | \
                   PVFS_ATTR_SYS_DIR_SPLIT_SIZE | \
                   PVFS_ATTR_SYS_CAPABILITY)

#define PVFS_ATTR_SYS_CREATE_REQUIRED \
                  (PVFS_ATTR_SYS_TYPE                | \
                   PVFS_ATTR_SYS_UID                 | \
                   PVFS_ATTR_SYS_GID                 | \
                   PVFS_ATTR_SYS_PERM                | \
                   PVFS_ATTR_SYS_DFILE_COUNT)    

#define PVFS_ATTR_SYS_MKDIR_REQUIRED \
                  (PVFS_ATTR_SYS_TYPE           | \
                   PVFS_ATTR_SYS_UID            | \
                   PVFS_ATTR_SYS_GID            | \
                   PVFS_ATTR_SYS_PERM           | \
                   PVFS_ATTR_SYS_DIR_INIT       | \
                   PVFS_ATTR_SYS_DIR_MAX        | \
                   PVFS_ATTR_SYS_DIR_SPLIT_SIZE)

#define PVFS_ATTR_SYS_SYMLINK_REQUIRED \
                  (PVFS_ATTR_SYS_TYPE      | \
                   PVFS_ATTR_SYS_UID       | \
                   PVFS_ATTR_SYS_GID       | \
                   PVFS_ATTR_SYS_PERM      | \
                   PVFS_ATTR_SYS_LNK_TARGET)

/* still not really clear on these three attribute groups */

#define PVFS_ATTR_SYS_ALL_SETABLE \
               (PVFS_ATTR_SYS_ALL - PVFS_ATTR_SYS_READ_ONLY) 

#define PVFS_ATTR_SYS_ALL_TIMES           \
          (PVFS_ATTR_SYS_ALL_SETABLE |    \
           PVFS_ATTR_SYS_ATIME_SET | PVFS_ATTR_SYS_MTIME_SET)

#define PVFS_ATTR_SYS_ALL_VALID                         \
     (PVFS_ATTR_SYS_ALL | PVFS_ATTR_SYS_CAPABILITY |    \
      PVFS_ATTR_SYS_FASTEST | PVFS_ATTR_SYS_LATEST)     \

/* typical attributes supplied to a PVFS_sys_mkdir */
#define PVFS_ATTR_SYS_MKDIR                \
                 (PVFS_ATTR_SYS_TYPE     | \
                  PVFS_ATTR_SYS_UID      | \
                  PVFS_ATTR_SYS_GID      | \
                  PVFS_ATTR_SYS_PERM     | \
                  PVFS_ATTR_SYS_DIR_INIT | \
                  PVFS_ATTR_SYS_DIR_MAX  | \
                  PVFS_ATTR_SYS_DIR_SPLIT_SIZE )

/* Extended attribute flags */
#define PVFS_XATTR_CREATE  0x1
#define PVFS_XATTR_REPLACE 0x2

/** statfs and misc. server statistic information. */
typedef struct PVFS_statfs_s
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

/*
 * object reference (uniquely refers to a single file, directory, or
 * symlink) metadata object.
 */
typedef struct PVFS_object_ref_s
{
    PVFS_handle handle;
    PVFS_fs_id  fs_id;
    int32_t     sid_count;
    PVFS_SID    *sid_array;
} PVFS_object_ref;

endecode_fields_2a(
    PVFS_object_ref,
    PVFS_handle, handle,
    PVFS_fs_id,  fs_id,
    int32_t,     sid_count,
    PVFS_SID,    sid_array);

/* a sid_count of -1 indicates the default which should be obtained
 * from the configuration - for the moment stuck at 3
 */
#define PVFS_object_ref_init(oref, sid_count) \
do { \
    if ((oref)->sid_count != 0 || (oref)->sid_array != NULL) \
    { \
        gossip_err("tried to init a non empty PVFS_object_ref\n"); \
        break; \
    } \
    memset((oref), 0, sizeof(PVFS_object_ref)); \
    if (sid_count == -1) \
    { \
        (oref)->sid_count = 3; \
    } \
    else \
    { \
        (oref)->sid_count = sid_count; \
    } \
    (oref)->sid_array = (PVFS_SID *)malloc(SASZ((oref)->sid_count)); \
    if (!(oref)->sid_array) \
    { \
        gossip_err("malloc returned error");\
        break; \
    } \
    ZEROMEM((oref)->sid_array, SASZ((oref)->sid_count)); \
} while (0)

/* dst must be empty (released) */
#define PVFS_object_ref_copy(dst, src) \
do { \
    if ((dst)->sid_count != 0 || (dst)->sid_array != NULL) \
    { \
        gossip_err("tried to copy to a non empty PVFS_object_ref\n"); \
        break; \
    } \
    *(dst) = *(src); \
    if ((src)->sid_count > 0 && (src)->sid_array) \
    { \
        (dst)->sid_array = (PVFS_SID *)malloc(SASZ((src)->sid_count)); \
        if (!(dst)->sid_array) \
        { \
            gossip_err("malloc returned error");\
            break; \
        } \
        ZEROMEM((dst)->sid_array, SASZ((src)->sid_count)); \
        memcpy((dst)->sid_array, (src)->sid_array, SASZ((src)->sid_count)); \
    } \
} while (0)

/* does not free the object_ref, but the resources it holds */
#define PVFS_object_ref_release(oref) \
do { \
    if ((oref)->sid_count > 0 && (oref)->sid_array) \
    { \
        free((oref)->sid_array); \
    } \
    memset((oref), 0, sizeof(PVFS_object_ref)); \
} while (0)

/* kernel compatibility version of a PVFS_handle */
typedef struct PVFS_khandle_s
{
    union
    {
        unsigned char u[16];
        uint32_t slice[4];
    };
} PVFS_khandle __attribute__ (( __aligned__ (8)));


/*
 * kernel version of an object ref.
 */
typedef struct PVFS_object_kref_s
{
    PVFS_khandle khandle;
    int32_t fs_id;
    int32_t __pad1;
} PVFS_object_kref;

/*
 * The kernel module will put the appropriate bytes of the khandle
 * into ihash.u and perceive them as an inode number through ihash.ino.
 * The slices are handy in encode and decode dirents...
 */
struct ihash
{
    union
    {
        unsigned char u[8];
        uint64_t ino;
        uint32_t slice[2];
    };
};

/* max length of BMI style URI's for identifying servers */
#define PVFS_MAX_SERVER_ADDR_LEN  256
/* max length of a list of BMI style URI's for identifying servers */
#define PVFS_MAX_SERVER_ADDR_LIST 2048
/* max length of PVFS filename */
#define PVFS_NAME_MAX             256
/* max len of individual path element */
#define PVFS_SEGMENT_MAX          PVFS_NAME_MAX
/* max len of an entire path */
#define PVFS_PATH_MAX             4096

/* max extended attribute name len as imposed by the VFS and exploited for the
 * upcall request types.
 * NOTE: Please retain them as multiples of 8 even if you wish to change them
 * This is *NECESSARY* for supporting 32 bit user-space binaries on a 64-bit kernel.
 * Due to implementation within DBPF, this really needs to be PVFS_NAME_MAX,
 * which it was the same value as, but no reason to let it break if that
 * changes in the future.
 */
#define PVFS_MAX_XATTR_NAMELEN   PVFS_NAME_MAX /* Not the same as 
                                                  XATTR_NAME_MAX defined
                                                  by <linux/xattr.h> */
#define PVFS_MAX_XATTR_VALUELEN  8192 /* Not the same as XATTR_SIZE_MAX defined
                                        by <linux/xattr.h> */ 
#define PVFS_MAX_XATTR_LISTLEN   16  /* Not the same as XATTR_LIST_MAX
                                          defined by <linux/xattr.h> */

/* This structure is used by the VFS-client interaction alone */
typedef struct PVFS_keyval_pair_s
{
    char key[PVFS_MAX_XATTR_NAMELEN];
    int32_t  key_sz; /* int32_t for portable, fixed-size structures */
    int32_t  val_sz;
    char val[PVFS_MAX_XATTR_VALUELEN];
} PVFS_keyval_pair;

/** Directory entry contents. */
typedef struct PVFS_dirent_s
{
    char d_name[PVFS_NAME_MAX + 1];
    PVFS_handle handle;
} PVFS_dirent;
endecode_fields_2(
    PVFS_dirent,
    here_string, d_name,
    PVFS_handle, handle);

/* Distributed directory attributes struct
 * will be stored in keyval space under DIST_DIR_ATTR
 * NOTE: this has changed for V3 - it is now stored as part
 * of the dspace.  The variable size items are stored as
 * keyval as they were before under DIST_DIR_BITMAP, and DFILE_HANDLES
 * which includes both handles and sids.
 */
typedef struct PVFS_dist_dir_attr_s
{
    /* global info */
    uint32_t tree_height;    /* ceil(log2(dirdata_count)) */
    uint32_t dirdata_min;    /* minimum and initial number if dirdata servers */
    uint32_t dirdata_max;    /* maximum number if dirdata servers */
    uint32_t dirdata_count;  /* current number of dirdata servers */
    uint32_t sid_count;      /* number of copies of each dirdata (bucket) */
    uint32_t bitmap_size;    /* number of PVFS_dist_dir_bitmap_basetype */
                            /* stored under the key DIST_DIR_BITMAP */
    uint32_t split_size;     /* maximum number of entries before a split */

    /* local info */
    int32_t server_no;      /* 0 to dirdata_count-1, indicates */
                                /* which dirdata server is running this code */
    int32_t branch_level;   /* level of branching on this server */
} PVFS_dist_dir_attr;

endecode_fields_9(
    PVFS_dist_dir_attr,
    uint32_t, tree_height,
    uint32_t, dirdata_min,
    uint32_t, dirdata_max,
    uint32_t, dirdata_count,
    uint32_t, sid_count,
    uint32_t, bitmap_size,
    uint32_t, split_size,
    int32_t, server_no,
    int32_t, branch_level);

typedef uint32_t PVFS_dist_dir_bitmap_basetype;
typedef uint32_t *PVFS_dist_dir_bitmap;
typedef uint64_t PVFS_dist_dir_hash_type;

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_dist_dir_bitmap_basetype encode_uint32_t
#define decode_PVFS_dist_dir_bitmap_basetype decode_uint32_t
#define defree_PVFS_dist_dir_bitmap_basetype defree_uint32_t
#define encode_PVFS_dist_dir_hash_type encode_uint64_t
#define decode_PVFS_dist_dir_hash_type decode_uint64_t
#define defree_PVFS_dist_dir_hash_type defree_uint64_t
#endif

/** Predefined server parameters that can be manipulated at run-time
 *  through the mgmt interface.
 */
enum PVFS_server_param
{
    PVFS_SERV_PARAM_INVALID = 0,
    PVFS_SERV_PARAM_GOSSIP_MASK = 1,       /* gossip debugging on or off */
    PVFS_SERV_PARAM_FSID_CHECK = 2,        /* verify that an fsid is ok */
    PVFS_SERV_PARAM_ROOT_CHECK = 3,        /* verify existance of root handle */
    PVFS_SERV_PARAM_MODE = 4,              /* change the current server mode */
    PVFS_SERV_PARAM_PERF_HISTORY = 5,      /* set counter history size */
    PVFS_SERV_PARAM_PERF_INTERVAL = 6,     /* set counter interval time */
    PVFS_SERV_PARAM_EVENT_ENABLE = 7,      /* event enable */
    PVFS_SERV_PARAM_EVENT_DISABLE = 8,     /* event disable */
    PVFS_SERV_PARAM_SYNC_META = 9,         /* metadata sync flags */
    PVFS_SERV_PARAM_SYNC_DATA = 10,        /* file data sync flags */
    PVFS_SERV_PARAM_DROP_CACHES = 11,
    PVFS_SERV_PARAM_TURN_OFF_TIMEOUTS = 12 /* set bypass_timeout_check */
};

enum PVFS_mgmt_param_type
{
    PVFS_MGMT_PARAM_TYPE_UINT64,
    PVFS_MGMT_PARAM_TYPE_STRING,
    PVFS_MGMT_PARAM_TYPE_HANDLE,
    PVFS_MGMT_PARAM_TYPE_MASK
} ;

struct PVFS_mgmt_setparam_value
{
    enum PVFS_mgmt_param_type type;
    union
    {
        uint64_t value;
        PVFS_debug_mask mask_value;
        char *string_value;
        PVFS_handle handle_value;
    } u;
};

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
encode_enum_union_4_struct(
    PVFS_mgmt_setparam_value, type, u,
    uint64_t,                value, PVFS_MGMT_PARAM_TYPE_UINT64,
    PVFS_debug_mask,    mask_value, PVFS_MGMT_PARAM_TYPE_MASK,
    string,           string_value, PVFS_MGMT_PARAM_TYPE_STRING,
    PVFS_handle,      handle_value, PVFS_MGMT_PARAM_TYPE_HANDLE);
#endif

enum PVFS_server_mode
{
    PVFS_SERVER_NORMAL_MODE = 1,      /* default server operating mode */
    PVFS_SERVER_ADMIN_MODE = 2        /* administrative mode */
};

/* PVFS2 ACL structures - Matches Linux ACL EA structures */
/* matches POSIX ACL-XATTR format */
typedef struct
{
    int16_t  p_tag;
    uint16_t p_perm;
    uint32_t p_id;
} pvfs2_acl_entry;

typedef struct
{
    uint32_t p_version;
    pvfs2_acl_entry p_entries[0];
} pvfs2_acl_header;

/* These defines match that of the POSIX defines */
#define PVFS2_ACL_UNDEFINED_ID   (-1)
#define PVFS2_ACL_VERSION      0x0002
#define PVFS2_ACL_ACCESS       "system.posix_acl_access"
#define PVFS2_ACL_DEFAULT      "system.posix_acl_default"

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

/* it is not clear what these "perror" variants bring us that the regular
 * gossip variety do not.  They appear to print error messages on user
 * errors (like file not found).  As such they should NOT be used, system
 * software does not print these messages, applications do.  These can be
 * revived if they are removed from the sysint, server, and library codes.
 * They can then be used as libc's perror ised used, by the programmer.
 * WBL 8/14
 */
/* These are defined in src/common/misc/errno-mapping.c */
int PVFS_strerror_r(int errnum, char *buf, int n);
void PVFS_perror(const char *text, int retcode);
void PVFS_perror_gossip(const char* text, int retcode);
void PVFS_perror_gossip_silent(void);
void PVFS_perror_gossip_verbose(void);
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

/* These are not error codes, they are non-error codes.  Do not put bits
 * on them.  Be aware that it is easy to confuse PVFS_ERROR and
 * PVFS_EPERM.  The first is an integer, the second is PVFS_error type.
 * So it depend on what your function is returning.
 */
#define PVFS_SUCCESS            0   /* successful completion */
#define PVFS_ERROR              -1  /* general error completion */

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
#define PVFS_ERANGE          E(60) /* Math out of range, or buf too small */

/***************** non-errno/pvfs2 specific error codes *****************/
#define PVFS_ECANCEL    (1|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_EDEVINIT   (2|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_EDETAIL    (3|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_EHOSTNTFD  (4|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_EADDRNTFD  (5|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_ENORECVR   (6|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_ETRYAGAIN  (7|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_ENOTPVFS   (8|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_ESECURITY  (9|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))

/* NOTE: PLEASE DO NOT ARBITRARILY ADD NEW ERRNO ERROR CODES!
 *
 * IF YOU CHOOSE TO ADD A NEW ERROR CODE (DESPITE OUR PLEA), YOU ALSO
 * NEED TO INCREMENT PVFS_ERRNO MAX (BELOW) AND ADD A MAPPING TO A
 * UNIX ERRNO VALUE IN THE MACROS BELOW (USED IN
 * src/common/misc/errno-mapping.c and the kernel module)
 */
#define PVFS_ERRNO_MAX          61

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
    ERANGE,                                           \
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
    "Path contains non-PVFS elements",                \
    "Security error",                                 \
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
    PVFS_ENOTPVFS,  /* 8 */                           \
    PVFS_ESECURITY, /* 9 */                           \
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
    return ret;                                            \
}                                                          \
PVFS_error PVFS_errno_to_error(int err)                    \
{                                                          \
    PVFS_error e = 0;                                      \
                                                           \
    for(; e < PVFS_ERRNO_MAX; ++e)                         \
    {                                                      \
        if(PINT_errno_mapping[e] == err)                   \
        {                                                  \
            return e | PVFS_ERROR_BIT;                     \
        }                                                  \
    }                                                      \
    return err;                                            \
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
typedef enum PVFS_io_type
{
    PVFS_IO_READ  = 1,
    PVFS_IO_WRITE = 2
} PVFS_io_type;

typedef enum PVFS_io_class
{
    PVFS_IO_IO  = 1,
    PVFS_IO_SMALL_IO = 2,
    PVFS_IO_METADATA = 3
} PVFS_io_class;

/*
 * Filesystem "magic" number unique to PVFS2 kernel interface.  Used by
 * ROMIO to auto-detect access method given a mounted path.
 */
#define PVFS2_SUPER_MAGIC 0x20030528

/* flag value that can be used with mgmt_iterate_handles to retrieve
 * reserved handle values
 */
#define PVFS_MGMT_RESERVED 1

/* Note: in a C file which uses profiling, include pvfs2-config.h before
 * pvfs2-types.h so ENABLE_PROFILING is declared.
 */
#ifdef ENABLE_PROFILING
/*
 * Structure and macros for timing things for profile-like output.
 *
 */
struct profiler
{
    struct timeval start;
    struct timeval finish;
    double save_timing;
};

#define DECLARE_PROFILER(prof_struct) \
    struct profiler prof_struct

#define DECLARE_PROFILER_EXTERN(prof_struct) \
    extern struct profiler prof_struct

#define INIT_PROFILER(prof_struct) \
    do { \
        prof_struct.save_timing = 0; \
    } while (0)

#define START_PROFILER(prof_struct) \
    do { \
        gettimeofday(&prof_struct.start, NULL); \
    } while (0)

#define FINISH_PROFILER(label, prof_struct, print_timing) \
    do { \
        double t_start, t_finish; \
        gettimeofday(&prof_struct.finish, NULL); \
        t_start = prof_struct.start.tv_sec + \
                  (prof_struct.start.tv_usec / 1000000.0); \
        t_finish = prof_struct.finish.tv_sec + \
                   (prof_struct.finish.tv_usec / 1000000.0); \
        prof_struct.save_timing = t_finish - t_start; \
        if (print_timing) { \
            gossip_err("PROFILING %s: %0.6f\n", label, prof_struct.save_timing); \
        } \
    } while (0)

#define PRINT_PROFILER(label, prof_struct) \
    do { \
        gossip_err("PROFILING %s: %0.6f\n", label, prof_struct.save_timing); \
    } while (0)

#else /* ENABLE_PROFILING */

#define DECLARE_PROFILER(prof_struct)

#define DECLARE_PROFILER_EXTERN(prof_struct)

#define INIT_PROFILER(prof_struct)

#define START_PROFILER(prof_struct)

#define FINISH_PROFILER(label, prof_struct, print_timing)

#define PRINT_PROFILER(label, prof_struct)

#endif /* ENABLE_PROFILING */
/*
 * New types for robust security implementation.
 */
#define PVFS2_DEFAULT_CREDENTIAL_TIMEOUT (3600)   /* 1 hour */
#define PVFS2_DEFAULT_CAPABILITY_TIMEOUT (600)
#define PVFS2_DEFAULT_CREDENTIAL_KEYPATH SYSCONFDIR "/pvfs2-clientkey.pem"
#define PVFS2_DEFAULT_CREDENTIAL_SERVICE_USERS SYSCONFDIR \
        "/orangefs-service-users"
#define PVFS2_SECURITY_TIMEOUT_MIN   5
#define PVFS2_SECURITY_TIMEOUT_MAX   (10*365*24*60*60)   /* ten years */

#define PVFS_SYS_LIMIT_CERT           8192
#define PVFS_SYS_LIMIT_KEY            8192
#define PVFS_SYS_LIMIT_HANDLES_COUNT  1024
#define PVFS_SYS_LIMIT_GROUPS         32
#define PVFS_SYS_LIMIT_ISSUER         128
#define PVFS_SYS_LIMIT_SIGNATURE      512

extern char PVFS2_BLANK_ISSUER[];

typedef unsigned char *PVFS_cert_data;

/* PVFS_certificate simply stores a buffer with the buffer size.
   The buffer can be converted to an OpenSSL X509 struct for use. */
typedef struct PVFS_certificate PVFS_certificate;
struct PVFS_certificate
{
    uint32_t buf_size;
    PVFS_cert_data buf;
};
endecode_fields_1a_struct (
    PVFS_certificate,
    skip4,,
    uint32_t, buf_size,
    PVFS_cert_data, buf);
#define extra_size_PVFS_certificate PVFS_SYS_LIMIT_CERT

/* Buffer and structure for certificate private key */
typedef unsigned char *PVFS_key_data;

typedef struct PVFS_security_key PVFS_security_key;
struct PVFS_security_key
{
    uint32_t buf_size;
    PVFS_key_data buf;
};
endecode_fields_1a_struct (
    PVFS_security_key,
    skip4,,
    uint32_t, buf_size,
    PVFS_key_data, buf);
#define extra_size_PVFS_security_key PVFS_SYS_LIMIT_KEY

typedef unsigned char *PVFS_signature;

/* A capability defines permissions for a set of handles. */
typedef struct PVFS_capability PVFS_capability;
struct PVFS_capability
{
    char *issuer;              /* alias of the issuing server */
    PVFS_fs_id fsid;           /* fsid for which this capability is valid */
    uint32_t sig_size;         /* length of the signature in bytes */
    PVFS_signature signature;  /* digital signature */
    PVFS_time timeout;         /* seconds after epoch to time out */
    uint32_t op_mask;          /* allowed operations mask */
    uint32_t num_handles;      /* number of elements in the handle array */
    PVFS_handle *handle_array; /* handles in this capability */
};
/* should not need the skip4 - really only 2a2a */
endecode_fields_3a2a_struct (
    PVFS_capability,
    string, issuer,
    PVFS_fs_id, fsid,
    skip4,,
    uint32_t, sig_size,
    PVFS_signature, signature,
    PVFS_time, timeout,
    uint32_t, op_mask,
    uint32_t, num_handles,
    PVFS_handle, handle_array);
#define extra_size_PVFS_capability (PVFS_SYS_LIMIT_HANDLES_COUNT * \
                                    sizeof(PVFS_handle)          + \
                                    PVFS_SYS_LIMIT_ISSUER        + \
                                    PVFS_SYS_LIMIT_SIGNATURE)

/* A credential identifies a user and is signed by the client/user 
   private key. */
typedef struct PVFS_credential PVFS_credential;
struct PVFS_credential 
{
    PVFS_uid userid;              /* user id */
    uint32_t num_groups;          /* length of group_array */
    PVFS_gid *group_array;        /* groups for which the user is a member */
    char *issuer;                 /* alias of the issuing server */
    PVFS_time timeout;            /* seconds after epoch to time out */
    uint32_t sig_size;            /* length of the signature in bytes */
    PVFS_signature signature;     /* digital signature */
    PVFS_certificate certificate; /* user certificate buffer */
};
endecode_fields_3a2a1_struct (
    PVFS_credential,
    skip4,,
    skip4,,
    PVFS_uid, userid,
    uint32_t, num_groups,
    PVFS_gid, group_array,
    string, issuer,
    PVFS_time, timeout,
    uint32_t, sig_size,
    PVFS_signature, signature,
    PVFS_certificate, certificate);
#define extra_size_PVFS_credential (PVFS_SYS_LIMIT_GROUPS    * \
                                    sizeof(PVFS_gid)         + \
                                    PVFS_SYS_LIMIT_ISSUER    + \
                                    PVFS_SYS_LIMIT_SIGNATURE + \
                                    extra_size_PVFS_certificate)

/* 
 * NOTE: for backwards compatibility only. 
 * For all new code use PVFS_credential.
 */
typedef PVFS_credential PVFS_credentials;

/*The following two limits pertain to the readdirplus request.  They are
 * exposed here for user programs that want to use the readdirplus count.
 */
#define PVFS_SYS_LIMIT_LISTATTR 60
#define PVFS_SYS_LIMIT_DIRENT_COUNT_READDIRPLUS PVFS_SYS_LIMIT_LISTATTR


#endif /* __PVFS2_TYPES_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
