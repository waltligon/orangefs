/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_STORAGE_H
#define __PVFS2_STORAGE_H

#include "pvfs2-types.h"

/************************************************************
 * Types and structures specific to the storage interface
 */

enum PVFS_coll_getinfo_options_e
{
    PVFS_COLLECTION_STATFS = 1
};
typedef enum PVFS_coll_getinfo_options_e PVFS_coll_getinfo_options;

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

/* vtag; contents not yet defined */
struct PVFS_vtag_s
{
    /* undefined */
};
typedef struct PVFS_vtag_s PVFS_vtag;

/* dataspace attributes that are not explicitly stored within the
 * dataspace itself.
 *
 * Note: this is what is being stored by the trove code as the
 * attributes right now.  this is separate from the attributes as sent
 * across the wire/to the user, so some translation is done.
 *
 * PVFS_object_attr attributes are what the users and the server deal
 * with.  Trove only deals with *_ds_storedattr objects (trove on disk
 * formats) and *_ds_attributes (trove in memory format).
 */
struct PVFS_ds_attributes_s
{
    PVFS_fs_id fs_id;
    PVFS_handle handle;
    PVFS_ds_type type;
    PVFS_uid uid;
    PVFS_gid gid;
    PVFS_permissions mode;
    PVFS_time ctime;
    PVFS_time mtime;
    PVFS_time atime;
    uint32_t dfile_count;
    uint32_t dist_size;

    /* non-stored attributes need to be below here */
    PVFS_size b_size; /* bstream size */
    PVFS_size k_size; /* keyval size; # of keys */
};
typedef struct PVFS_ds_attributes_s PVFS_ds_attributes;

struct PVFS_ds_storedattr_s
{
    PVFS_fs_id fs_id;
    PVFS_handle handle;
    PVFS_ds_type type;
    PVFS_uid uid;
    PVFS_gid gid;
    PVFS_permissions mode;
    PVFS_time ctime;
    PVFS_time mtime;
    PVFS_time atime;
    uint32_t dfile_count;
    uint32_t dist_size;
};
typedef struct PVFS_ds_storedattr_s PVFS_ds_storedattr;

#define PVFS_ds_attr_to_stored(__from, __to)	        \
do {						        \
    (__to) = * ((PVFS_ds_storedattr *) &(__from));	\
} while (0)

#define PVFS_ds_stored_to_attr(__from, __to, __b_size, __k_size)\
do {                                                            \
    memcpy(&__to, &__from, sizeof(PVFS_ds_storedattr));         \
    (__to).b_size = (__b_size);                                 \
    (__to).k_size = (__k_size);                                 \
} while (0)

#define PVFS_ds_attr_to_object_attr(__dsa, __oa)           \
do {                                                       \
    __oa->owner = __dsa->uid; __oa->group = __dsa->gid;    \
    __oa->perms = __dsa->mode; __oa->ctime = __dsa->ctime; \
    __oa->mtime = __dsa->mtime; __oa->atime = __dsa->atime;\
    __oa->objtype = __dsa->type;                           \
    __oa->u.meta.dfile_count = __dsa->dfile_count;         \
    __oa->u.meta.dist_size = __dsa->dist_size;             \
} while(0)

#define PVFS_object_attr_to_ds_attr(__oa, __dsa)           \
do {                                                       \
    __dsa->uid = __oa->owner; __dsa->gid = __oa->group;    \
    __dsa->mode = __oa->perms; __dsa->ctime = __oa->ctime; \
    __dsa->mtime = __oa->mtime; __dsa->atime = __oa->atime;\
    __dsa->type = __oa->objtype;                           \
    __dsa->dfile_count = __oa->u.meta.dfile_count;         \
    __dsa->dist_size = __oa->u.meta.dist_size;             \
} while(0)

#define PVFS_object_attr_overwrite_setable(dest, src)          \
do {                                                           \
    if (src->mask & PVFS_ATTR_COMMON_UID)                      \
        dest->owner = src->owner;                              \
    if (src->mask & PVFS_ATTR_COMMON_GID)                      \
        dest->group = src->group;                              \
    if (src->mask & PVFS_ATTR_COMMON_PERM)                     \
        dest->perms = src->perms;                              \
    if (src->mask & PVFS_ATTR_COMMON_ATIME)                    \
        dest->atime = src->atime;                              \
    if (src->mask & PVFS_ATTR_COMMON_CTIME)                    \
        dest->ctime = src->ctime;                              \
    if (src->mask & PVFS_ATTR_COMMON_MTIME)                    \
        dest->mtime = src->mtime;                              \
    if (src->mask & PVFS_ATTR_COMMON_TYPE)                     \
    {                                                          \
        dest->objtype = src->objtype;                          \
        if ((src->objtype == PVFS_TYPE_METAFILE) &&            \
            (src->mask & PVFS_ATTR_META_DIST))                 \
            dest->u.meta.dist_size = src->u.meta.dist_size;    \
        if ((src->objtype == PVFS_TYPE_METAFILE) &&            \
            (src->mask & PVFS_ATTR_META_DFILES))               \
            dest->u.meta.dfile_count = src->u.meta.dfile_count;\
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
