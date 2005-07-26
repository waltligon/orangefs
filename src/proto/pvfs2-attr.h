/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_ATTR_H
#define __PVFS2_ATTR_H

#include "pvfs2-types.h"
#include "pvfs2-storage.h"
#include "pint-distribution.h"

/* internal attribute masks, common to all obj types */
#define PVFS_ATTR_COMMON_UID   (1 << 0)
#define PVFS_ATTR_COMMON_GID   (1 << 1)
#define PVFS_ATTR_COMMON_PERM  (1 << 2)
#define PVFS_ATTR_COMMON_ATIME (1 << 3)
#define PVFS_ATTR_COMMON_CTIME (1 << 4)
#define PVFS_ATTR_COMMON_MTIME (1 << 5)
#define PVFS_ATTR_COMMON_TYPE  (1 << 6)
#define PVFS_ATTR_COMMON_ALL                       \
(PVFS_ATTR_COMMON_UID   | PVFS_ATTR_COMMON_GID   | \
 PVFS_ATTR_COMMON_PERM  | PVFS_ATTR_COMMON_ATIME | \
 PVFS_ATTR_COMMON_CTIME | PVFS_ATTR_COMMON_MTIME | \
 PVFS_ATTR_COMMON_TYPE)

/* internal attribute masks for metadata objects */
#define PVFS_ATTR_META_DIST    (1 << 10)
#define PVFS_ATTR_META_DFILES  (1 << 11)
#define PVFS_ATTR_META_ALL \
(PVFS_ATTR_META_DIST | PVFS_ATTR_META_DFILES)

/* internal attribute masks for datafile objects */
#define PVFS_ATTR_DATA_SIZE            (1 << 15)
#define PVFS_ATTR_DATA_ALL   PVFS_ATTR_DATA_SIZE

/* internal attribute masks for symlink objects */
#define PVFS_ATTR_SYMLNK_TARGET            (1 << 18)
#define PVFS_ATTR_SYMLNK_ALL PVFS_ATTR_SYMLNK_TARGET

/* attributes specific to metadata objects */
struct PVFS_metafile_attr_s
{
    /* distribution */
    PINT_dist *dist;
    uint32_t dist_size;  /* not sent across wire, each side may be diff */

    /* list of datafiles */
    PVFS_handle *dfile_array;
    uint32_t dfile_count;
};
typedef struct PVFS_metafile_attr_s PVFS_metafile_attr;
#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_metafile_attr_dist(pptr,x) do { \
    encode_PINT_dist(pptr, &(x)->dist); \
} while (0)
#define decode_PVFS_metafile_attr_dist(pptr,x) do { \
    decode_PINT_dist(pptr, &(x)->dist); \
    (x)->dist_size = PINT_DIST_PACK_SIZE((x)->dist); \
} while (0)
#define encode_PVFS_metafile_attr_dfiles(pptr,x) do { int dfiles_i; \
    encode_uint32_t(pptr, &(x)->dfile_count); \
    encode_skip4(pptr,); \
    for (dfiles_i=0; dfiles_i<(x)->dfile_count; dfiles_i++) \
	encode_PVFS_handle(pptr, &(x)->dfile_array[dfiles_i]); \
} while (0)
#define decode_PVFS_metafile_attr_dfiles(pptr,x) do { int dfiles_i; \
    decode_uint32_t(pptr, &(x)->dfile_count); \
    decode_skip4(pptr,); \
    (x)->dfile_array = decode_malloc((x)->dfile_count \
      * sizeof(*(x)->dfile_array)); \
    for (dfiles_i=0; dfiles_i<(x)->dfile_count; dfiles_i++) \
	decode_PVFS_handle(pptr, &(x)->dfile_array[dfiles_i]); \
} while (0)
#endif

/* attributes specific to datafile objects */
struct PVFS_datafile_attr_s
{
    PVFS_size size;
};
typedef struct PVFS_datafile_attr_s PVFS_datafile_attr;
endecode_fields_1(PVFS_datafile_attr, PVFS_size, size)

/* attributes specific to directory objects */
struct PVFS_directory_attr_s
{
    PVFS_size dirent_count;
};
typedef struct PVFS_directory_attr_s PVFS_directory_attr;
endecode_fields_1(PVFS_directory_attr, PVFS_size, dirent_count)

/* attributes specific to symlinks */
struct PVFS_symlink_attr_s
{
    uint32_t target_path_len;
    char *target_path;
};
typedef struct PVFS_symlink_attr_s PVFS_symlink_attr;
endecode_fields_3(
  PVFS_symlink_attr,
  uint32_t, target_path_len,
  skip4,,
  string, target_path)

/* generic attributes; applies to all objects */
struct PVFS_object_attr
{
    PVFS_uid owner;
    PVFS_gid group;
    PVFS_permissions perms;
    PVFS_time atime;
    PVFS_time mtime;
    PVFS_time ctime;
    uint32_t mask;     /* indicates which fields are currently valid */
    PVFS_ds_type objtype; /* defined in pvfs2-types.h */
    union
    {
	PVFS_metafile_attr meta;
	PVFS_datafile_attr data;
	PVFS_directory_attr dir;
	PVFS_symlink_attr sym;
    }
    u;
};
typedef struct PVFS_object_attr PVFS_object_attr;
#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_object_attr(pptr,x) do { \
    encode_PVFS_uid(pptr, &(x)->owner); \
    encode_PVFS_gid(pptr, &(x)->group); \
    encode_PVFS_permissions(pptr, &(x)->perms); \
    encode_skip4(pptr,); \
    encode_PVFS_time(pptr, &(x)->atime); \
    encode_PVFS_time(pptr, &(x)->mtime); \
    encode_PVFS_time(pptr, &(x)->ctime); \
    encode_uint32_t(pptr, &(x)->mask); \
    encode_PVFS_ds_type(pptr, &(x)->objtype); \
    if ((x)->mask & PVFS_ATTR_META_DIST) \
	encode_PVFS_metafile_attr_dist(pptr, &(x)->u.meta); \
    if ((x)->mask & PVFS_ATTR_META_DFILES) \
	encode_PVFS_metafile_attr_dfiles(pptr, &(x)->u.meta); \
    if ((x)->mask & PVFS_ATTR_DATA_SIZE) \
	encode_PVFS_datafile_attr(pptr, &(x)->u.data); \
    if ((x)->mask & PVFS_ATTR_SYMLNK_TARGET) \
	encode_PVFS_symlink_attr(pptr, &(x)->u.sym); \
    if ((x)->mask & PVFS_ATTR_DIR_DIRENT_COUNT) \
	encode_PVFS_directory_attr(pptr, &(x)->u.dir); \
} while (0)
#define decode_PVFS_object_attr(pptr,x) do { \
    decode_PVFS_uid(pptr, &(x)->owner); \
    decode_PVFS_gid(pptr, &(x)->group); \
    decode_PVFS_permissions(pptr, &(x)->perms); \
    decode_skip4(pptr,); \
    decode_PVFS_time(pptr, &(x)->atime); \
    decode_PVFS_time(pptr, &(x)->mtime); \
    decode_PVFS_time(pptr, &(x)->ctime); \
    decode_uint32_t(pptr, &(x)->mask); \
    decode_PVFS_ds_type(pptr, &(x)->objtype); \
    if ((x)->mask & PVFS_ATTR_META_DIST) \
	decode_PVFS_metafile_attr_dist(pptr, &(x)->u.meta); \
    if ((x)->mask & PVFS_ATTR_META_DFILES) \
	decode_PVFS_metafile_attr_dfiles(pptr, &(x)->u.meta); \
    if ((x)->mask & PVFS_ATTR_DATA_SIZE) \
	decode_PVFS_datafile_attr(pptr, &(x)->u.data); \
    if ((x)->mask & PVFS_ATTR_SYMLNK_TARGET) \
	decode_PVFS_symlink_attr(pptr, &(x)->u.sym); \
    if ((x)->mask & PVFS_ATTR_DIR_DIRENT_COUNT) \
	decode_PVFS_directory_attr(pptr, &(x)->u.dir); \
} while (0)
#endif
/* attr buffer needs room for larger of symlink path or meta fields: an attrib
 * structure can never hold information for both a symlink and a metafile  */
#define extra_size_PVFS_object_attr_meta (PVFS_REQ_LIMIT_DIST_BYTES + \
  PVFS_REQ_LIMIT_DFILE_COUNT * sizeof(PVFS_handle))
#define extra_size_PVFS_object_attr_symlink (PVFS_REQ_LIMIT_PATH_NAME_BYTES)
#define extra_size_PVFS_object_attr ((extra_size_PVFS_object_attr_meta > \
 extra_size_PVFS_object_attr_symlink) ? extra_size_PVFS_object_attr_meta : \
 extra_size_PVFS_object_attr_symlink)

#endif /* __PVFS2_ATTR_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
