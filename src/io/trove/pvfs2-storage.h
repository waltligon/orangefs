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
#include "security-util.h"

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

struct PVFS_ds_metadata_attr_s
{
    PVFS_size size;        /* global file size, volatile */
    uint32_t dfile_count;  /* dfiles for this file */
    uint32_t dist_size;    /* size of the string coding distribution */
    int32_t  sid_count;    /* sids per dfile */
    uint32_t mirror_mode;  /* sync model for replication */
    uint64_t flags;        /* various hint modes */
};

struct PVFS_ds_datafile_attr_s
{
    PVFS_size b_size; /* bstream size */
};

/* this is the same as the client/memory side attr structure */
struct PVFS_ds_directory_attr_s
{
    uint64_t    dirent_count;  /* number of dirents in this dir - volatile */
    /* global info */
    uint32_t    tree_height;   /* ceil(log2(dirdata_count)) */
    uint32_t    dirdata_min;   /* min number of servers that are part of this dir */
    uint32_t    dirdata_max;   /* max of servers that are part of this dir */
    uint32_t    dirdata_count; /* current number of servers that are part of this dir */
    uint32_t    bitmap_size;   /* number of PVFS_dist_dir_bitmap_basetype */
                               /*     stored under the key DIST_DIR_BITMAP */
    uint32_t    split_size;    /* maximum number of entries before a split */
    /* local info */
    uint32_t    branch_level;  /* level of branching on this server */
    /* FILE HINTS */
    uint32_t    hint_dist_name_len;        /* size of dist name buffer */
    uint32_t    hint_dist_params_len;      /* size of dist params buffer */
    uint32_t    hint_dfile_count;          /* number of dfiles to be used */
    uint32_t    hint_dfile_sid_count;      /* number of dfile replicas */
    uint32_t    hint_layout_algorithm;     /* how servers are selected */
    uint32_t    hint_layout_list_cnt;      /* servers in list */
    /* DIR HINTS */
    uint32_t    hint_dirdata_min;          /* number of dfiles to be used */
    uint32_t    hint_dirdata_max;          /* number of dfiles to be used */
    uint32_t    hint_split_size;           /* max number of entries before a split */
    uint32_t    hint_dir_layout_algorithm; /* how servers are selected */
    uint32_t    hint_dir_layout_list_cnt;  /* servers in list */
};

struct PVFS_ds_dirdata_attr_s
{
    uint64_t dirent_count;  /* number of dirents in this dirdata */
    /* global info */
    uint32_t tree_height;   /* ceil(log2(dirdata_count)) */
    uint32_t dirdata_count; /* total number of servers */
    uint32_t bitmap_size;   /* number of PVFS_dist_dir_bitmap_basetype */
                            /* stored under the key DIST_DIR_BITMAP */
    uint32_t split_size;    /* maximum number of entries before a split */
    /* local info */
    uint32_t server_no;     /* 0 to dirdata_count-1, indicates */
                            /* which server is running this code */
    uint32_t branch_level;  /* level of branching on this server */
    uint32_t __pad1;
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
    PVFS_ds_type     type;
    PVFS_fs_id       fs_id;
    PVFS_handle      handle;     /* 128 bit */
    PVFS_uid         uid;
    PVFS_gid         gid;
    PVFS_permissions mode;
    int32_t          __pad1;

    PVFS_time ctime; /* change (metadata) time */
    PVFS_time mtime; /* modify (data) time */
    PVFS_time atime; /* access (read) time */
    PVFS_time ntime; /* new (create) time */

    uint32_t meta_sid_count; /* FS wide count of metadata replication */
    int32_t  __pad2;

    union
    {
        struct PVFS_ds_metadata_attr_s  metafile;
        struct PVFS_ds_datafile_attr_s  datafile;
        struct PVFS_ds_directory_attr_s directory;
        struct PVFS_ds_dirdata_attr_s   dirdata;
    } u;
} ;
typedef struct PVFS_ds_attributes_s PVFS_ds_attributes;

#define PVFS_ds_init_time(__dsa)                        \
do {                                                    \
    (__dsa)->ctime = time(NULL);                        \
    (__dsa)->atime = time(NULL);                        \
    (__dsa)->mtime = time(NULL);                        \
    (__dsa)->ntime = time(NULL);                        \
} while (0)

/* convenience and back compatibility */
#define PVFS_object_attr_to_ds_attr(__oa, __dsa)        \
        PVFS_ds_attr_from_object_attr((__dsa), (__oa))

#define PVFS_ds_attr_to_object_attr(__dsa, __oa)        \
        PVFS_object_attr_from_ds_attr((__oa), (__dsa))

/* We really do not have the ability to know what attributes in the DS
 * record are valid, so we set all flags based on the object type
 */
#define PVFS_object_attr_from_ds_attr(__oa, __dsa)                     \
do {                                                                   \
    (__oa)->owner = (__dsa)->uid;                                      \
    (__oa)->group = (__dsa)->gid;                                      \
    (__oa)->perms = (__dsa)->mode;                                     \
    (__oa)->ctime = (__dsa)->ctime;                                    \
    (__oa)->mtime = (__dsa)->mtime;                                    \
    (__oa)->atime = (__dsa)->atime;                                    \
    (__oa)->ntime = (__dsa)->ntime;                                    \
    (__oa)->objtype = (__dsa)->type;                                   \
    (__oa)->meta_sid_count = (__dsa)->meta_sid_count;                  \
    /* force a null capability */                                      \
    PINT_null_capability(&(__oa)->capability);                         \
    (__oa)->mask = PVFS_ATTR_COMMON_ALL;                               \
    switch ((__dsa)->type)                                             \
    {                                                                  \
    case PVFS_TYPE_METAFILE :                                          \
        (__oa)->u.meta.dfile_count = (__dsa)->u.metafile.dfile_count;  \
        (__oa)->u.meta.sid_count = (__dsa)->u.metafile.sid_count;      \
        (__oa)->u.meta.dist_size = (__dsa)->u.metafile.dist_size;      \
        (__oa)->u.meta.size = (__dsa)->u.metafile.size;                \
        (__oa)->u.meta.mirror_mode = (__dsa)->u.metafile.mirror_mode;  \
        (__oa)->u.meta.flags = (__dsa)->u.metafile.flags;              \
        (__oa)->mask |= PVFS_ATTR_META_ALL; /*includes COMMON_ALL */   \
        break;                                                         \
    case PVFS_TYPE_DATAFILE :                                          \
        (__oa)->u.data.size = (__dsa)->u.datafile.b_size;              \
        (__oa)->mask |= PVFS_ATTR_DATA_ALL; /*includes COMMON_ALL */   \
        break;                                                         \
    case PVFS_TYPE_DIRECTORY :                                         \
        (__oa)->u.dir.dirent_count =                                   \
                (__dsa)->u.directory.dirent_count;                     \
        (__oa)->u.dir.dist_dir_attr.tree_height =                      \
                (__dsa)->u.directory.tree_height;                      \
        (__oa)->u.dir.dist_dir_attr.dirdata_min =                      \
                (__dsa)->u.directory.dirdata_min;                      \
        (__oa)->u.dir.dist_dir_attr.dirdata_max =                      \
                (__dsa)->u.directory.dirdata_max;                      \
        (__oa)->u.dir.dist_dir_attr.dirdata_count =                    \
                (__dsa)->u.directory.dirdata_count;                    \
        (__oa)->u.dir.dist_dir_attr.sid_count =                        \
                (__dsa)->meta_sid_count;                               \
        (__oa)->u.dir.dist_dir_attr.bitmap_size =                      \
                (__dsa)->u.directory.bitmap_size;                      \
        (__oa)->u.dir.dist_dir_attr.split_size =                       \
                (__dsa)->u.directory.split_size;                       \
        (__oa)->u.dir.dist_dir_attr.server_no =                        \
                -1;  /* dir is not a numbered server */                \
        (__oa)->u.dir.dist_dir_attr.branch_level =                     \
                (__dsa)->u.directory.branch_level;                     \
        /* FILE HINTS */                                               \
        (__oa)->u.dir.hint.dist_name_len =                             \
                (__dsa)->u.directory.hint_dist_name_len;               \
        (__oa)->u.dir.hint.dist_params_len =                           \
                (__dsa)->u.directory.hint_dist_params_len;             \
        (__oa)->u.dir.hint.dfile_count =                               \
                (__dsa)->u.directory.hint_dfile_count;                 \
        (__oa)->u.dir.hint.dfile_sid_count =                           \
                (__dsa)->u.directory.hint_dfile_sid_count;             \
        (__oa)->u.dir.hint.layout.algorithm =                          \
                (__dsa)->u.directory.hint_layout_algorithm;            \
        (__oa)->u.dir.hint.layout.server_list.count =                  \
                (__dsa)->u.directory.hint_layout_list_cnt;             \
        /* DIR HINTS */                                                \
        (__oa)->u.dir.hint.dir_dirdata_min =                           \
                (__dsa)->u.directory.hint_dirdata_min;                 \
        (__oa)->u.dir.hint.dir_dirdata_max =                           \
                (__dsa)->u.directory.hint_dirdata_max;                 \
        (__oa)->u.dir.hint.dir_split_size =                            \
                (__dsa)->u.directory.hint_split_size;                  \
        (__oa)->u.dir.hint.dir_layout.algorithm =                      \
                (__dsa)->u.directory.hint_dir_layout_algorithm;        \
        (__oa)->u.dir.hint.dir_layout.server_list.count =              \
                (__dsa)->u.directory.hint_dir_layout_list_cnt;         \
        (__oa)->mask |= PVFS_ATTR_DIR_ALL; /*includes COMMON_ALL */    \
        break;                                                         \
    case PVFS_TYPE_DIRDATA :                                           \
        (__oa)->u.dirdata.dirent_count =                               \
                (__dsa)->u.dirdata.dirent_count;                       \
        (__oa)->u.dirdata.dist_dir_attr.tree_height =                  \
                (__dsa)->u.dirdata.tree_height;                        \
        (__oa)->u.dirdata.dist_dir_attr.dirdata_count =                \
                (__dsa)->u.dirdata.dirdata_count;                      \
        (__oa)->u.dirdata.dist_dir_attr.sid_count =                    \
                (__dsa)->meta_sid_count;                               \
        (__oa)->u.dirdata.dist_dir_attr.bitmap_size =                  \
                (__dsa)->u.dirdata.bitmap_size;                        \
        (__oa)->u.dirdata.dist_dir_attr.split_size =                   \
                (__dsa)->u.dirdata.split_size;                         \
        (__oa)->u.dirdata.dist_dir_attr.server_no =                    \
                (__dsa)->u.dirdata.server_no;                          \
        (__oa)->u.dirdata.dist_dir_attr.branch_level =                 \
                (__dsa)->u.dirdata.branch_level;                       \
        (__oa)->mask |= PVFS_ATTR_DIRDATA_ALL; /*includes COMMON_ALL */\
        break;                                                         \
    default :                                                          \
        break;                                                         \
    }                                                                  \
} while(0)

#define PVFS_ds_attr_from_object_attr(__dsa, __oa)                     \
do {                                                                   \
    (__dsa)->uid = (__oa)->owner;                                      \
    (__dsa)->gid = (__oa)->group;                                      \
    (__dsa)->mode = (__oa)->perms;                                     \
    (__dsa)->ctime = (__oa)->ctime;                                    \
    (__dsa)->mtime = (__oa)->mtime;                                    \
    (__dsa)->atime = (__oa)->atime;                                    \
    (__dsa)->ntime = (__oa)->ntime;                                    \
    (__dsa)->type = (__oa)->objtype;                                   \
    (__dsa)->meta_sid_count = (__oa)->meta_sid_count;                  \
    switch ((__oa)->objtype)                                           \
    {                                                                  \
    case PVFS_TYPE_METAFILE :                                          \
        (__dsa)->u.metafile.dfile_count = (__oa)->u.meta.dfile_count;  \
        (__dsa)->u.metafile.sid_count = (__oa)->u.meta.sid_count;      \
        (__dsa)->u.metafile.dist_size = (__oa)->u.meta.dist_size;      \
        (__dsa)->u.metafile.size = (__oa)->u.meta.size;                \
        (__dsa)->u.metafile.mirror_mode = (__oa)->u.meta.mirror_mode;  \
        (__dsa)->u.metafile.flags = (__oa)->u.meta.flags;              \
        break;                                                         \
    case PVFS_TYPE_DATAFILE :                                          \
        (__dsa)->u.datafile.b_size = (__oa)->u.data.size;              \
        break;                                                         \
    case PVFS_TYPE_DIRECTORY :                                         \
        (__dsa)->u.directory.dirent_count =                            \
                (__oa)->u.dir.dirent_count;                            \
        (__dsa)->u.directory.tree_height =                             \
                (__oa)->u.dir.dist_dir_attr.tree_height;               \
        (__dsa)->u.directory.dirdata_max =                             \
                (__oa)->u.dir.dist_dir_attr.dirdata_max;               \
        (__dsa)->u.directory.dirdata_min =                             \
                (__oa)->u.dir.dist_dir_attr.dirdata_min;               \
        (__dsa)->u.directory.dirdata_count =                           \
                (__oa)->u.dir.dist_dir_attr.dirdata_count;             \
        (__dsa)->u.directory.bitmap_size =                             \
                (__oa)->u.dir.dist_dir_attr.bitmap_size;               \
        (__dsa)->u.directory.split_size =                              \
                (__oa)->u.dir.dist_dir_attr.split_size;                \
        (__dsa)->u.directory.branch_level =                            \
                (__oa)->u.dir.dist_dir_attr.branch_level;              \
        /* FILE HINTS */                                               \
        (__dsa)->u.directory.hint_dist_name_len =                      \
                (__oa)->u.dir.hint.dist_name_len;                      \
        (__dsa)->u.directory.hint_dist_params_len =                    \
                (__oa)->u.dir.hint.dist_params_len;                    \
        (__dsa)->u.directory.hint_dfile_count =                        \
                (__oa)->u.dir.hint.dfile_count;                        \
        (__dsa)->u.directory.hint_dfile_sid_count =                    \
                (__oa)->u.dir.hint.dfile_sid_count;                    \
        (__dsa)->u.directory.hint_layout_algorithm =                   \
                (__oa)->u.dir.hint.layout.algorithm;                   \
        (__dsa)->u.directory.hint_layout_list_cnt =                    \
                (__oa)->u.dir.hint.layout.server_list.count;           \
        /* DIR HINTS */                                                \
        (__dsa)->u.directory.hint_dirdata_min =                        \
                (__oa)->u.dir.hint.dir_dirdata_min;                    \
        (__dsa)->u.directory.hint_dirdata_max =                        \
                (__oa)->u.dir.hint.dir_dirdata_max;                    \
        (__dsa)->u.directory.hint_split_size =                         \
                (__oa)->u.dir.hint.dir_split_size;                     \
        (__dsa)->u.directory.hint_dir_layout_algorithm =               \
                (__oa)->u.dir.hint.dir_layout.algorithm;               \
        (__dsa)->u.directory.hint_dir_layout_list_cnt =                \
                (__oa)->u.dir.hint.dir_layout.server_list.count;       \
        /*(__dsa)->mask |= PVFS_ATTR_DIR_ALL;*/ /*includes COMMON_ALL */    \
        break;                                                         \
    case PVFS_TYPE_DIRDATA :                                           \
        (__dsa)->u.dirdata.dirent_count =                              \
                (__oa)->u.dirdata.dirent_count;                        \
        (__dsa)->u.dirdata.tree_height =                               \
                (__oa)->u.dirdata.dist_dir_attr.tree_height;           \
        (__dsa)->u.dirdata.dirdata_count =                             \
                (__oa)->u.dirdata.dist_dir_attr.dirdata_count;         \
        (__dsa)->u.dirdata.bitmap_size =                               \
                (__oa)->u.dirdata.dist_dir_attr.bitmap_size;           \
        (__dsa)->u.dirdata.split_size =                                \
                (__oa)->u.dirdata.dist_dir_attr.split_size;            \
        (__dsa)->u.dirdata.server_no =                                 \
                (__oa)->u.dirdata.dist_dir_attr.server_no;             \
        (__dsa)->u.dirdata.branch_level =                              \
                (__oa)->u.dirdata.dist_dir_attr.branch_level;          \
        break;                                                         \
    default :                                                          \
        break;                                                         \
    }                                                                  \
} while(0)

/* This is OA to OA which should not be defined here but with the
 * definition of OA - Also, should we not also set the dest mask with
 * fields that have been copied?  I know they should be present already
 * but should we assume that?  Should we assert that?
 */
/* This macro copies fields from one OA to another only if the src mask
 * indicates to do so.  This can be used to update specific fields and
 * to "merge" contents together.  This only looks at basic fields.
 * times are either set from local time or from the src if the corresponding
 * _SET flag is set in the src mask.  We tend to favor using local time
 * as it makes consistency issues easier - though it may not resolve
 * them all.
 */
#define PVFS_object_attr_overwrite_setable(dest, src)          \
do {                                                           \
    if ((src)->mask & PVFS_ATTR_COMMON_UID)                    \
        (dest)->owner = (src)->owner;                          \
    if ((src)->mask & PVFS_ATTR_COMMON_GID)                    \
        (dest)->group = (src)->group;                          \
    if ((src)->mask & PVFS_ATTR_COMMON_PERM)                   \
        (dest)->perms = (src)->perms;                          \
    if ((src)->mask & PVFS_ATTR_COMMON_SID_COUNT)              \
        (dest)->meta_sid_count = (src)->meta_sid_count;        \
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
            (dest)->mtime = time(NULL);                        \
        }                                                      \
    }                                                          \
    if ((src)->mask & PVFS_ATTR_COMMON_NTIME)                  \
    {                                                          \
        (dest)->ntime = time(NULL);                            \
        if ((src)->mask & PVFS_ATTR_COMMON_NTIME_SET)          \
        {                                                      \
            (dest)->ntime = (src)->ntime;                      \
        }                                                      \
        else                                                   \
        {                                                      \
            (dest)->ntime = time(NULL);                        \
        }                                                      \
    }                                                          \
    if ((src)->mask & PVFS_ATTR_COMMON_CTIME)                  \
    {                                                          \
        (dest)->ctime = time(NULL);                            \
        if ((src)->mask & PVFS_ATTR_COMMON_CTIME_SET)          \
        {                                                      \
            (dest)->ctime = (src)->ctime;                      \
        }                                                      \
        else                                                   \
        {                                                      \
            (dest)->ctime = time(NULL);                        \
        }                                                      \
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
