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
    uint32_t dist_size;
};

struct PVFS_ds_datafile_attr_s
{
    PVFS_size b_size; /* bstream size */
};

struct PVFS_ds_dirdata_attr_s
{
    uint64_t count;
};

/* dataspace attributes that are not explicitly stored within the
 * dataspace itself.
 *
 * Note: this is what is being stored by the trove code as the
 * attributes right now.  this is separate from the attributes as sent
 * across the wire/to the user, so some translation is done.
 *
 * PVFS_object_attr attributes are what the users and the server deal
 * with.  Trove deals with TROVE_ds_attributes (trove on disk and in-memory format).
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

#define PVFS_ds_attr_to_object_attr(__dsa, __oa)                   \
do {                                                               \
    (__oa)->owner = (__dsa)->uid;                                  \
    (__oa)->group = (__dsa)->gid;                                  \
    (__oa)->perms = (__dsa)->mode;                                 \
    (__oa)->ctime = (__dsa)->ctime;                                \
    (__oa)->mtime = (__dsa)->mtime;                                \
    (__oa)->atime = (__dsa)->atime;                                \
    (__oa)->objtype = (__dsa)->type;                               \
    (__oa)->u.meta.dfile_count = (__dsa)->u.metafile.dfile_count;  \
    (__oa)->u.meta.dist_size = (__dsa)->u.metafile.dist_size;      \
} while(0)

#define PVFS_object_attr_to_ds_attr(__oa, __dsa)                      \
    do {                                                              \
        (__dsa)->uid = (__oa)->owner;                                 \
        (__dsa)->gid = (__oa)->group;                                 \
        (__dsa)->mode = (__oa)->perms;                                \
        (__dsa)->ctime = (__oa)->ctime;                               \
        (__dsa)->mtime = (__oa)->mtime;                               \
        (__dsa)->atime = (__oa)->atime;                               \
        (__dsa)->type = (__oa)->objtype;                              \
        (__dsa)->u.metafile.dfile_count = (__oa)->u.meta.dfile_count; \
        (__dsa)->u.metafile.dist_size = (__oa)->u.meta.dist_size;     \
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
