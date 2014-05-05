/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_STORAGE_H
#define __PVFS2_STORAGE_H

#include "pvfs2-internal.h"
#include <time.h>
#include "pvfs2-types.h"

/************************************************************
 * Types and structures specific to the storage interface
 */

enum PVFS_coll_getinfo_options_e
{
    PVFS_COLLECTION_STATFS = 1
};
typedef enum PVFS_coll_getinfo_options_e PVFS_coll_getinfo_options;

struct PVFS_vtag_s
{
    /* undefined */
#ifdef WIN32
    int field;
#endif
};
typedef struct PVFS_vtag_s PVFS_vtag;
/* key/value descriptor definition moved to include/pvfs2-types.h */
#if 0
/* key/value descriptors */
struct PVFS_ds_keyval_s
{
    void *buffer;
    /* size of memory region pointed to by buffer */
    int32_t buffer_sz;
    /* size of data read into buffer (only valid after a read) */
    int32_t read_sz;
};

typedef struct PVFS_ds_keyval_s PVFS_ds_keyval;
#endif

struct PVFS_ds_metadata_attr_s
{
    uint32_t dfile_count;
    uint32_t sid_count;  /* total, must be even multiple of dfile_count */
    uint32_t dist_size;
};

struct PVFS_ds_datafile_attr_s
{
    PVFS_size b_size; /* bstream size */
};

/* this is the same as the clien/memory side attr structure */
struct PVFS_ds_directory_attr_s
{
    /* global info */
    int32_t tree_height;   /* ceil(log2(dirdata_count)) */
    int32_t dirdata_count; /* total number of servers */
    int32_t sid_count;     /* number of SIDs total */
    int32_t bitmap_size;   /* number of PVFS_dist_dir_bitmap_basetype */
                           /* stored under the key DIST_DIR_BITMAP */
    int32_t split_size;    /* maximum number of entries before a split */
    /* local info */
    int32_t server_no;     /* 0 to dirdata_count-1, indicates */
                           /* which server is running this code */
    int32_t branch_level;  /* level of branching on this server */
};

struct PVFS_ds_dirdata_attr_s
{
    uint64_t count; /* number of entries in the dirdata? */
};

/* dataspace attributes that are not explicitly stored within the
 * dataspace itself.
 *
 * Note: this is what is being stored by the trove code as the
 * attributes right now.  this is separate from the attributes as sent
 * across the wire/to the user, so some translation is done.
 *
 * PVFS_object_attr attributes are what the users and the server deal
 * with.  Trove deals with TROVE_ds_attributes
 * (trove on disk and in-memory format).
 *
 * Trove version 0.0.1 and version 0.0.2 differ in this aspect, since
 * many members have been moved, added to make this structure friendlier
 * for 32 and 64 bit users. Consequently, this means we would require a
 * migrate utility that will convert from one to the other by reading from
 * the dspace and writing it out to the new dspace.
 */
struct PVFS_ds_attributes_s
{
    PVFS_ds_type type;
    PVFS_fs_id fs_id;
    PVFS_handle handle;
    PVFS_uid uid;
    PVFS_gid gid;
    PVFS_permissions mode;
    int32_t   __pad1;

    PVFS_time ctime;
    PVFS_time mtime;
    PVFS_time atime;

    union
    {
        struct PVFS_ds_metadata_attr_s metafile;
        struct PVFS_ds_datafile_attr_s datafile;
        struct PVFS_ds_directory_attr_s directory;
        struct PVFS_ds_dirdata_attr_s dirdata;
    } u;
} ;
typedef struct PVFS_ds_attributes_s PVFS_ds_attributes;

#define PVFS_ds_init_time(__dsa)                        \
do {                                                    \
    (__dsa)->ctime = time(NULL);                        \
    (__dsa)->atime = time(NULL);                        \
    (__dsa)->mtime = time(NULL);                        \
} while (0)

#define PVFS_ds_attr_to_object_attr(__dsa, __oa)                       \
do {                                                                   \
    (__oa)->owner = (__dsa)->uid;                                      \
    (__oa)->group = (__dsa)->gid;                                      \
    (__oa)->perms = (__dsa)->mode;                                     \
    (__oa)->ctime = (__dsa)->ctime;                                    \
    (__oa)->mtime = (__dsa)->mtime;                                    \
    (__oa)->atime = (__dsa)->atime;                                    \
    (__oa)->objtype = (__dsa)->type;                                   \
    switch ((__dsa)->type)                                             \
    {                                                                  \
    case PVFS_TYPE_METAFILE :                                          \
        (__oa)->u.meta.dfile_count = (__dsa)->u.metafile.dfile_count;  \
        (__oa)->u.meta.sid_count = (__dsa)->u.metafile.sid_count;      \
        (__oa)->u.meta.dist_size = (__dsa)->u.metafile.dist_size;      \
        break;                                                         \
    case PVFS_TYPE_DATAFILE :                                          \
        (__oa)->u.data.size = (__dsa)->u.datafile.b_size;              \
        break;                                                         \
    case PVFS_TYPE_DIRECTORY :                                         \
        (__oa)->u.dir.dist_dir_attr.tree_height =                      \
                (__dsa)->u.directory.tree_height;                      \
        (__oa)->u.dir.dist_dir_attr.dirdata_count =                    \
                (__dsa)->u.directory.dirdata_count;                    \
        (__oa)->u.dir.dist_dir_attr.sid_count =                        \
                (__dsa)->u.directory.sid_count;                        \
        (__oa)->u.dir.dist_dir_attr.bitmap_size =                      \
                (__dsa)->u.directory.bitmap_size;                      \
        (__oa)->u.dir.dist_dir_attr.split_size =                       \
                (__dsa)->u.directory.split_size;                       \
        (__oa)->u.dir.dist_dir_attr.server_no =                        \
                (__dsa)->u.directory.server_no;                        \
        (__oa)->u.dir.dist_dir_attr.branch_level =                     \
                (__dsa)->u.directory.branch_level;                     \
        break;                                                         \
    case PVFS_TYPE_DIRDATA :                                           \
        (__oa)->u.dirdata.count = (__dsa)->u.dirdata.count;            \
        break;                                                         \
    default :                                                          \
        break;                                                         \
    }                                                                  \
} while(0)

#define PVFS_object_attr_to_ds_attr(__oa, __dsa)                       \
do {                                                                   \
    (__dsa)->uid = (__oa)->owner;                                      \
    (__dsa)->gid = (__oa)->group;                                      \
    (__dsa)->mode = (__oa)->perms;                                     \
    (__dsa)->ctime = (__oa)->ctime;                                    \
    (__dsa)->mtime = (__oa)->mtime;                                    \
    (__dsa)->atime = (__oa)->atime;                                    \
    (__dsa)->type = (__oa)->objtype;                                   \
    switch ((__oa)->objtype)                                           \
    {                                                                  \
    case PVFS_TYPE_METAFILE :                                          \
        (__dsa)->u.metafile.dfile_count = (__oa)->u.meta.dfile_count;  \
        (__dsa)->u.metafile.sid_count = (__oa)->u.meta.sid_count;      \
        (__dsa)->u.metafile.dist_size = (__oa)->u.meta.dist_size;      \
        break;                                                         \
    case PVFS_TYPE_DATAFILE :                                          \
        (__dsa)->u.datafile.b_size = (__oa)->u.data.size;              \
        break;                                                         \
    case PVFS_TYPE_DIRECTORY :                                         \
        (__dsa)->u.directory.tree_height =                             \
                (__oa)->u.dir.dist_dir_attr.tree_height;               \
        (__dsa)->u.directory.dirdata_count =                           \
                (__oa)->u.dir.dist_dir_attr.dirdata_count;             \
        (__dsa)->u.directory.sid_count =                               \
                (__oa)->u.dir.dist_dir_attr.sid_count;                 \
        (__dsa)->u.directory.bitmap_size =                             \
                (__oa)->u.dir.dist_dir_attr.bitmap_size;               \
        (__dsa)->u.directory.split_size =                              \
                (__oa)->u.dir.dist_dir_attr.split_size;                \
        (__dsa)->u.directory.server_no =                               \
                (__oa)->u.dir.dist_dir_attr.server_no;                 \
        (__dsa)->u.directory.branch_level =                            \
                (__oa)->u.dir.dist_dir_attr.branch_level;              \
        break;                                                         \
    case PVFS_TYPE_DIRDATA :                                           \
        (__dsa)->u.dirdata.count = (__oa)->u.dirdata.count;            \
        break;                                                         \
    default :                                                          \
        break;                                                         \
    }                                                                  \
} while(0)

#define PVFS_object_attr_overwrite_setable(dest, src)          \
do {                                                           \
    if ((src)->mask & PVFS_ATTR_COMMON_UID)                    \
        (dest)->owner = (src)->owner;                          \
    if ((src)->mask & PVFS_ATTR_COMMON_GID)                    \
        (dest)->group = (src)->group;                          \
    if ((src)->mask & PVFS_ATTR_COMMON_PERM)                   \
        (dest)->perms = (src)->perms;                          \
    if ((src)->mask & PVFS_ATTR_COMMON_ATIME)                  \
    {                                                          \
        if ((src)->mask & PVFS_ATTR_COMMON_ATIME_SET)          \
        {                                                      \
            (dest)->atime = (src)->atime;                      \
        }                                                      \
        else                                                   \
        {                                                      \
            (dest)->atime = time(NULL);                        \
        }                                                      \
    }                                                          \
    if ((src)->mask & PVFS_ATTR_COMMON_MTIME)                  \
    {                                                          \
        if ((src)->mask & PVFS_ATTR_COMMON_MTIME_SET)          \
        {                                                      \
            (dest)->mtime = (src)->mtime;                      \
        }                                                      \
        else                                                   \
        {                                                      \
            (dest)->mtime = PINT_util_mktime_version(time(NULL)); \
        }                                                      \
    }                                                          \
    if ((src)->mask & PVFS_ATTR_COMMON_CTIME)                  \
    {                                                          \
        (dest)->ctime = time(NULL);                            \
    }                                                          \
    if ((src)->mask & PVFS_ATTR_COMMON_TYPE)                   \
    {                                                          \
        (dest)->objtype = (src)->objtype;                      \
        if (((src)->objtype == PVFS_TYPE_METAFILE) &&          \
            ((src)->mask & PVFS_ATTR_META_DIST))               \
            (dest)->u.meta.dist_size = (src)->u.meta.dist_size;\
        if (((src)->objtype == PVFS_TYPE_METAFILE) &&          \
            ((src)->mask & PVFS_ATTR_META_DFILES))             \
            (dest)->u.meta.dfile_count = (src)->u.meta.dfile_count;\
    }                                                          \
} while(0)

#endif /* __PVFS2_STORAGE_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
