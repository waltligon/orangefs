/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
/* NOTE: if you make any changes to the code contained in this file, please
 * update the PVFS2_PROTO_VERSION in pvfs2-req-proto.h accordingly
 */

#ifndef __PVFS2_ATTR_H
#define __PVFS2_ATTR_H

#include "pvfs2-internal.h"
#include "pvfs2-types.h"
#include "pvfs2-storage.h"
#include "pint-distribution.h"
#include "pint-security.h"

#ifndef max
#define max(a,b) ((a) < (b) ? (b) : (a))
#endif

/* These defines are for PVFS_object_attr's which are the internal
 * representation of object attributes in the file system.  There is
 * another set of very similar defines PVFS_ATTR_SYS_XXX that are for
 * PVFS_sys_attr's which are passed between the user and the system
 * interface.  They are defined in include/pvfs2-sysint.h and there are
 * two functions (one is a macro) that convert between these the first
 * found in src/common/misc/pvfs2-util.c for sys_to_object_attr and the
 * other in src/common/misc/pint-util.h for PINT_CONVERT_ATTR.  All of
 * these need to be kept up to date.  It is not required that the same
 * bits represent the same thing, but additions and removals from the
 * defines need to be checked each time to see if they affect the other
 * bits of code.
 */

/* Currently we are encoding/decoding all fields regardless of the mask
 * as the totality is not all that large.  Older versions only
 * encoded/decoded if the mask was set.  This is easy enough to
 * implement if we think it is important - WBL
 */

/* 6/18 version - we are adding flags for all attr fields to indicate
 * if the field in question is valid or not.  We have dealt with majic
 * appearing and disapperaing attributes enough to show the need.  We
 * still encode and decode the whole thing - interpretation is on each
 * end
 */

typedef uint64_t PVFS_object_attrmask;
#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_object_attrmask encode_uint64_t
#define decode_PVFS_object_attrmask decode_uint64_t
#define defree_PVFS_object_attrmask defree_uint64_t
#endif

/* New simpler mask system, built on old system */

#define PVFS_ATTR_DEFAULT \
             PVFS_ATTR_READ_ALL_LATEST

#define PVFS_ATTR_READ_ALL \
             PVFS_ATTR_READ_ALL_FASTEST 

#define PVFS_ATTR_READ_ALL_FASTEST \
            (PVFS_ATTR_COMMON_ALL  | PVFS_ATTR_FASTEST | \
             PVFS_ATTR_CAPABILITY  | PVFS_ATTR_META_ALL | \
             PVFS_ATTR_DIR_ALL     | PVFS_ATTR_DATA_ALL | \
             PVFS_ATTR_SYMLNK_ALL  | PVFS_ATTR_DIRDATA_ALL)

#define PVFS_ATTR_READ_FASTEST \
            (PVFS_ATTR_COMMON_ALL  | PVFS_ATTR_FASTEST) \

#define PVFS_ATTR_READ_ALL_LATEST \
            (PVFS_ATTR_COMMON_ALL  | PVFS_ATTR_LATEST | \
             PVFS_ATTR_CAPABILITY  | PVFS_ATTR_META_ALL | \
             PVFS_ATTR_DIR_ALL     | PVFS_ATTR_DATA_ALL | \
             PVFS_ATTR_SYMLNK_ALL  | PVFS_ATTR_DIRDATA_ALL)

#define PVFS_ATTR_READ_LATEST \
            (PVFS_ATTR_COMMON_ALL  | PVFS_ATTR_LATEST) \

#define PVFS_ATTR_FASTEST            (1UL << 62)
#define PVFS_ATTR_LATEST             (1UL << 63)

/* latest and fastest refer to atime, mtime, ctime, file size
 * and dirent_count.  Fastest returns the value stored in the
 * attributes of the target meta or dir, whereas latest reads
 * these values from each of the dfile or dirdata objects
 * referenced by the meta or dir and calculates the global
 * value and updates the "fastest" version.  Presumably a
 * scrubber or some such will update these periodically.  User
 * interfaces can choose to force a "latest" when desired
 */

/* -------------------------
 * COMMON OBJECT ATTRIBUTES
 * -------------------------
 */

/* internal attribute masks, common to all obj types */
#define PVFS_ATTR_COMMON_UID         (1UL << 0)
#define PVFS_ATTR_COMMON_GID         (1UL << 1)
#define PVFS_ATTR_COMMON_PERM        (1UL << 2)
#define PVFS_ATTR_COMMON_ATIME       (1UL << 3)
#define PVFS_ATTR_COMMON_CTIME       (1UL << 4)
#define PVFS_ATTR_COMMON_MTIME       (1UL << 5)
#define PVFS_ATTR_COMMON_NTIME       (1UL << 6)
#define PVFS_ATTR_COMMON_TYPE        (1UL << 7)
#define PVFS_ATTR_COMMON_ATIME_SET   (1UL << 8)
#define PVFS_ATTR_COMMON_CTIME_SET   (1UL << 9)
#define PVFS_ATTR_COMMON_MTIME_SET   (1UL << 10)
#define PVFS_ATTR_COMMON_NTIME_SET   (1UL << 11)
#define PVFS_ATTR_COMMON_PARENT      (1UL << 12)
#define PVFS_ATTR_COMMON_SID_COUNT   (1UL << 13)

#define PVFS_ATTR_COMMON_NOTIME                           \
        (PVFS_ATTR_COMMON_UID  | PVFS_ATTR_COMMON_GID   | \
         PVFS_ATTR_COMMON_PERM | PVFS_ATTR_COMMON_TYPE  | \
         PVFS_ATTR_COMMON_PARENT | PVFS_ATTR_COMMON_SID_COUNT)

/* Time flags do not have a corresponding attribute
 * Rather, they indicate the server should set the given
 * time to the current time on the server.  The TIME_SET
 * flags indicate there is a time in the corresponding attribute
 * sent from the client and that should be used to set the
 * attribute on the server.
 */
#define PVFS_ATTR_TIME_SET \
        (PVFS_ATTR_COMMON_ATIME_SET | PVFS_ATTR_COMMON_NTIME_SET | \
         PVFS_ATTR_COMMON_CTIME_SET | PVFS_ATTR_COMMON_MTIME_SET)

#define PVFS_ATTR_NOTIME_SET \
        ~(PVFS_ATTR_COMMON_ATIME_SET | PVFS_ATTR_COMMON_NTIME_SET | \
          PVFS_ATTR_COMMON_CTIME_SET | PVFS_ATTR_COMMON_MTIME_SET)

#define PVFS_ATTR_TIME \
        (PVFS_ATTR_COMMON_ATIME | PVFS_ATTR_COMMON_NTIME | \
         PVFS_ATTR_COMMON_CTIME | PVFS_ATTR_COMMON_MTIME)

#define PVFS_ATTR_NOTIME \
        ~(PVFS_ATTR_COMMON_ATIME | PVFS_ATTR_COMMON_NTIME | \
          PVFS_ATTR_COMMON_CTIME | PVFS_ATTR_COMMON_MTIME)

#define PVFS_ATTR_TIME_ALL       PVFS_ATTR_TIME

#define PVFS_ATTR_COMMON_ALL                               \
        (PVFS_ATTR_COMMON_NOTIME | PVFS_ATTR_TIME_ALL)


/* -------------------------
 * METAFILE OBJECT ATTRIBUTES
 * -------------------------
 */

/* internal attribute masks for metadata objects - "get or put" 
 * Items that must be get or put of variable side and are stored
 * in the keyval db, thus requiring extra reads to get the data
 */
#define PVFS_ATTR_META_DIST        (1UL << 14) /*** GET the distribution */
#define PVFS_ATTR_META_DIST_SIZE   (1UL << 15)     /* dist size */
#define PVFS_ATTR_META_DFILES      (1UL << 16) /*** GET dfile oids and sids */
#define PVFS_ATTR_META_DFILE_COUNT (1UL << 17)     /* buff size */
#define PVFS_ATTR_META_SID_COUNT   (1UL << 18)     /* buff size */
#define PVFS_ATTR_META_MIRROR_MODE (1UL << 19)   /* writable *???? */
#define PVFS_ATTR_META_SIZE        (1UL << 20)     /* writable */
#define PVFS_ATTR_META_FLAGS       (1UL << 21)   /* writable */

#define PVFS_ATTR_META_ALL                                       \
        (PVFS_ATTR_META_DIST      | PVFS_ATTR_META_DIST_SIZE   | \
         PVFS_ATTR_META_DFILES    | PVFS_ATTR_META_DFILE_COUNT | \
         PVFS_ATTR_META_SID_COUNT | PVFS_ATTR_META_MIRROR_MODE | \
         PVFS_ATTR_META_SIZE      | PVFS_ATTR_META_FLAGS       | \
         PVFS_ATTR_COMMON_ALL)

/* -------------------------
 * DATAFILE OBJECT ATTRIBUTES
 * -------------------------
 */
/* internal attribute masks for datafile objects */
#define PVFS_ATTR_DATA_SIZE          (1UL << 22)   /* size of a DFILE
                                                     * replace with latest value */

#define PVFS_ATTR_DATA_ALL \
             (PVFS_ATTR_DATA_SIZE | PVFS_ATTR_TIME_ALL)

/* -------------------------
 * SYMLINK OBJECT ATTRIBUTES
 * -------------------------
 */

/* internal attribute masks for symlink objects */
#define PVFS_ATTR_SYMLNK_TARGET      (1UL << 23)   /* writable */

#define PVFS_ATTR_SYMLNK_ALL \
             (PVFS_ATTR_SYMLNK_TARGET | PVFS_ATTR_COMMON_ALL)

/* --------------------------
 * DIRECTORY OBJECT ATTRIBUTES
 * --------------------------
 */

/* internal attribute masks for directory objects */
#define PVFS_ATTR_DIR_DIRENT_COUNT         (1UL << 24)  

/* these attributes are for METAs, but they are set on the DIR to
 * create defaults
 */
#define PVFS_ATTR_DIR_HINT_DIST_NAME_LEN   (1UL << 25)   /* buff */
#define PVFS_ATTR_DIR_HINT_DIST_PARAMS_LEN (1UL << 26)   /* buff */
#define PVFS_ATTR_DIR_HINT_DFILE_COUNT     (1UL << 27)
#define PVFS_ATTR_DIR_HINT_SID_COUNT       (1UL << 28)
#define PVFS_ATTR_DIR_HINT_LAYOUT          (1UL << 29)

#define PVFS_ATTR_DIR_HINT_DIRDATA_MIN     (1UL << 30)   /* buff */
#define PVFS_ATTR_DIR_HINT_DIRDATA_MAX     (1UL << 31)   /* buff */
#define PVFS_ATTR_DIR_HINT_SPLIT_SIZE      (1UL << 32)
#define PVFS_ATTR_DIR_HINT_DIR_LAYOUT      (1UL << 33)   /* buff */

#define PVFS_ATTR_DIR_HINT_ALL \
    (PVFS_ATTR_DIR_HINT_DIST_NAME_LEN | PVFS_ATTR_DIR_HINT_DIST_PARAMS_LEN | \
    PVFS_ATTR_DIR_HINT_DFILE_COUNT    | PVFS_ATTR_DIR_HINT_SID_COUNT | \
    PVFS_ATTR_DIR_HINT_DIRDATA_MIN    | PVFS_ATTR_DIR_HINT_DIRDATA_MAX | \
    PVFS_ATTR_DIR_HINT_SPLIT_SIZE     | PVFS_ATTR_DIR_HINT_DIR_LAYOUT | \
    PVFS_ATTR_DIR_HINT_LAYOUT)

/* These are the dist_dir_attr struct which now are in the 
 * the dspace so we are getting away from refering to them as such
 */
#define PVFS_ATTR_DIR_TREE_HEIGHT          (1UL << 34)
#define PVFS_ATTR_DIR_DIRDATA_MIN          (1UL << 35)   /* buff */
#define PVFS_ATTR_DIR_DIRDATA_MAX          (1UL << 36)   /* buff */
#define PVFS_ATTR_DIR_DIRDATA_COUNT        (1UL << 37)   /* buff */
#define PVFS_ATTR_DIR_SID_COUNT            (1UL << 38)
#define PVFS_ATTR_DIR_BITMAP_SIZE          (1UL << 39)   /* buff */
#define PVFS_ATTR_DIR_SPLIT_SIZE           (1UL << 40)
#define PVFS_ATTR_DIR_SERVER_NO            (1UL << 41)
#define PVFS_ATTR_DIR_BRANCH_LEVEL         (1UL << 42)

#define PVFS_ATTR_DIST_DIR_ALL \
    (PVFS_ATTR_DIR_TREE_HEIGHT | PVFS_ATTR_DIR_DIRDATA_COUNT | \
    PVFS_ATTR_DIR_DIRDATA_MIN  | PVFS_ATTR_DIR_DIRDATA_MAX | \
    PVFS_ATTR_DIR_SID_COUNT    | PVFS_ATTR_DIR_BITMAP_SIZE | \
    PVFS_ATTR_DIR_SPLIT_SIZE   | PVFS_ATTR_DIR_SERVER_NO | \
    PVFS_ATTR_DIR_BRANCH_LEVEL)

/* not sure why we have these - should use ALL groups? */
#define PVFS_ATTR_DIR_DIRDATA              (1UL << 43)   /* includes sids */
#define PVFS_ATTR_DIR_HINT                 (1UL << 44)   /* writable */

//dir.dist_dir_attr.dirdata_count

#define PVFS_ATTR_DIR_ALL \
    (PVFS_ATTR_DIR_DIRENT_COUNT | \
     PVFS_ATTR_DIR_HINT_ALL | PVFS_ATTR_DIST_DIR_ALL | \
     PVFS_ATTR_DIR_DIRDATA  | PVFS_ATTR_DIR_HINT)

/* not sure what this is for */
#define PVFS_ATTR_DIR_ALL_COMMON \
             (PVFS_ATTR_DIR_ALL | PVFS_ATTR_COMMON_ALL)

/* -------------------------
 * DIRDATA OBJECT ATTRIBUTES
 * -------------------------
 */

/* internal attribute mask for distributed directory information */
#define PVFS_ATTR_DIRDATA_DIRENT_COUNT   (1UL << 45)

/* These are the same attributes shown abive under dir, but they are
 * also part of dirdata s they are repeated here but with a name change
 */
#define PVFS_ATTR_DIRDATA_TREE_HEIGHT    (1UL << 46)
#define PVFS_ATTR_DIRDATA_DIRDATA_MIN    (1UL << 47) /* min number of servers */
#define PVFS_ATTR_DIRDATA_DIRDATA_MAX    (1UL << 48) /* max number of servers */
#define PVFS_ATTR_DIRDATA_DIRDATA_COUNT  (1UL << 49) /* number of servers */
#define PVFS_ATTR_DIRDATA_SID_COUNT      (1UL << 50)
#define PVFS_ATTR_DIRDATA_BITMAP_SIZE    (1UL << 51)   /* buff */
#define PVFS_ATTR_DIRDATA_SPLIT_SIZE     (1UL << 52)
#define PVFS_ATTR_DIRDATA_SERVER_NO      (1UL << 53)
#define PVFS_ATTR_DIRDATA_BRANCH_LEVEL   (1UL << 54)

#define PVFS_ATTR_DIRDATA_ALL \
    (PVFS_ATTR_DIRDATA_DIRENT_COUNT | \
    PVFS_ATTR_DIRDATA_TREE_HEIGHT  | PVFS_ATTR_DIRDATA_DIRDATA_MIN | \
    PVFS_ATTR_DIRDATA_DIRDATA_MAX  | PVFS_ATTR_DIRDATA_DIRDATA_COUNT | \
    PVFS_ATTR_DIRDATA_SID_COUNT    | PVFS_ATTR_DIRDATA_BITMAP_SIZE | \
    PVFS_ATTR_DIRDATA_SPLIT_SIZE   | PVFS_ATTR_DIRDATA_SERVER_NO | \
    PVFS_ATTR_DIRDATA_BRANCH_LEVEL | PVFS_ATTR_TIME_ALL)

/* -------------------------
 * DISTDIR ATTRIBUTES
 * -------------------------
 */

/* This define exists for managing the dist_dir_attr struct independent
 * of the type of object record it is in.
 * I think we should have two of these, one for DIR and one for DIRDATA
 * and neither should have this name (so we can find places we referred
 * to it
 */

#define PVFS_ATTR_DISTDIR_ATTR  \
        (PVFS_ATTR_DIRDATA_DIRENT_COUNT | PVFS_ATTR_DIRDATA_TREE_HEIGHT | \
        PVFS_ATTR_DIRDATA_SID_COUNT | PVFS_ATTR_DIRDATA_BITMAP_SIZE | \
        PVFS_ATTR_DIRDATA_SPLIT_SIZE | PVFS_ATTR_DIRDATA_SERVER_NO | \
        PVFS_ATTR_DIRDATA_BRANCH_LEVEL)

/* internal attribute mask for capability objects */
#define PVFS_ATTR_CAPABILITY             (1UL << 55)

#if 0
#define PVFS_ATTR_DISTDIR_ATTR           (1UL << 46)    /* writable */
#endif

/* attributes that do not change once set */
/* needs to be renamed */
#define PVFS_STATIC_ATTR_MASK                                   \
        (PVFS_ATTR_COMMON_TYPE | PVFS_ATTR_META_DIST |          \
        PVFS_ATTR_META_DFILES  | PVFS_ATTR_META_MIRROR_DFILES | \
        PVFS_ATTR_META_UNSTUFFED)

/**************************************
 * Helper functions for attribute masks
 **************************************/

/* use generic uint64_t here because this can work on sys_attr and obj_attr */
static inline int PVFS2_attr_any(uint64_t mask, uint64_t attr)
{
    if (mask & attr)
    {
        return 1;
    }
    return 0;
}

static inline int PVFS2_attr_all(uint64_t mask, uint64_t attr)
{
    if ((mask & attr) == attr)
    {
        return 1;
    }
    return 0;
}

/**************************************
 * Code for debugging attribute masks
 **************************************/
static inline void __DEBUG_ATTR_MASK(PVFS_object_attrmask mask, 
                                     char *fn,
                                     int lno);

#define DEBUG_attr_mask(m)                             \
        do {                                           \
            __DEBUG_ATTR_MASK(m, __FILE__, __LINE__);  \
        } while (0)

#if 1
#define DATTRPRINT(fmt) printf(fmt);

#define MASKDEBUG(field,fmt) \
        do { if ((mask & field) == field) DATTRPRINT(fmt) } while (0)

static inline void __DEBUG_ATTR_MASK(PVFS_object_attrmask mask,
                                     char *filename,
                                     int lineno)
{
    DATTRPRINT("DEBUG_attr_mask (src/proto/pvfs2-attr.c) ");
    printf("Called from file %s line %d\n", filename, lineno);

    MASKDEBUG(PVFS_ATTR_COMMON_UID,               "COMMON_UID\n");
    MASKDEBUG(PVFS_ATTR_COMMON_GID,               "COMMON_GID\n");
    MASKDEBUG(PVFS_ATTR_COMMON_PERM,              "COMMON_PERM\n");
    MASKDEBUG(PVFS_ATTR_COMMON_ATIME,             "COMMON_ATIME\n");
    MASKDEBUG(PVFS_ATTR_COMMON_CTIME,             "COMMON_CTIME\n");
    MASKDEBUG(PVFS_ATTR_COMMON_MTIME,             "COMMON_MTIME\n");
    MASKDEBUG(PVFS_ATTR_COMMON_NTIME,             "COMMON_NTIME\n");
    MASKDEBUG(PVFS_ATTR_COMMON_TYPE,              "COMMON_TYPE (OBJ)\n");
    MASKDEBUG(PVFS_ATTR_COMMON_ATIME_SET,         "COMMON_ATIME_SET\n");
    MASKDEBUG(PVFS_ATTR_COMMON_CTIME_SET,         "COMMON_CTIME_SET\n");
    MASKDEBUG(PVFS_ATTR_COMMON_MTIME_SET,         "COMMON_MTIME_SET\n");
    MASKDEBUG(PVFS_ATTR_COMMON_PARENT,            "COMMON_PARENT\n");
    MASKDEBUG(PVFS_ATTR_COMMON_SID_COUNT,         "COMMON_SID_COUNT\n");
    MASKDEBUG(PVFS_ATTR_META_DIST,                "META_DIST\n");
    MASKDEBUG(PVFS_ATTR_META_DIST_SIZE,           "META_DIST_SIZE\n");
/**/MASKDEBUG(PVFS_ATTR_META_DFILES,              "META_DFILES\n");
    MASKDEBUG(PVFS_ATTR_META_DFILE_COUNT,         "META_DFILE_COUNT\n");
    MASKDEBUG(PVFS_ATTR_META_SID_COUNT,           "META_SID_COUNT\n");
    MASKDEBUG(PVFS_ATTR_META_MIRROR_MODE,         "META_MIRROR\n");
    MASKDEBUG(PVFS_ATTR_META_SIZE,                "META_SIZE\n");
    MASKDEBUG(PVFS_ATTR_META_FLAGS,               "META_FLAGS\n");
    MASKDEBUG(PVFS_ATTR_DATA_SIZE,                "DATA_SIZE\n");
/**/MASKDEBUG(PVFS_ATTR_SYMLNK_TARGET,            "SYMLINK_TARGET\n");
    MASKDEBUG(PVFS_ATTR_DIR_DIRENT_COUNT,         "DIR_DIRENT_COUNT\n");
    MASKDEBUG(PVFS_ATTR_DIR_HINT_DIST_NAME_LEN,   "DIR_HINT_DIST_NAME_LEN\n");
    MASKDEBUG(PVFS_ATTR_DIR_HINT_DIST_PARAMS_LEN, "DIR_HINT_DIST_PARAMS_LEN\n");
    MASKDEBUG(PVFS_ATTR_DIR_HINT_DFILE_COUNT,     "DIR_HINT_DFILE_COUNT\n");
    MASKDEBUG(PVFS_ATTR_DIR_HINT_SID_COUNT,       "DIR_HINT_SID_COUNT\n");
    MASKDEBUG(PVFS_ATTR_DIR_HINT_LAYOUT,          "DIR_HINT_LAYOUT\n");
    MASKDEBUG(PVFS_ATTR_DIR_HINT_DIRDATA_MIN,     "DIR_HINT_DIRDATA_MIN\n");
    MASKDEBUG(PVFS_ATTR_DIR_HINT_DIRDATA_MAX,     "DIR_HINT_DIRDATA_MAX\n");
    MASKDEBUG(PVFS_ATTR_DIR_HINT_SPLIT_SIZE,      "DIR_HINT_SPLIT_SIZE\n");
    MASKDEBUG(PVFS_ATTR_DIR_HINT_DIR_LAYOUT,      "DIR_HINT_DIR_LAYOUT\n");
    MASKDEBUG(PVFS_ATTR_DIR_TREE_HEIGHT,          "DIR_TREE_HEIGHT\n");
    MASKDEBUG(PVFS_ATTR_DIR_DIRDATA_MIN,          "DIR_DIRDATA_MIN\n");
    MASKDEBUG(PVFS_ATTR_DIR_DIRDATA_MAX,          "DIR_DIRDATA_MAX\n");
    MASKDEBUG(PVFS_ATTR_DIR_DIRDATA_COUNT,        "DIR_DIRDATA_COUNT\n");
    MASKDEBUG(PVFS_ATTR_DIR_SID_COUNT,            "DIR_SID_COUNT\n");
    MASKDEBUG(PVFS_ATTR_DIR_BITMAP_SIZE,          "DIR_BITMAP_SIZE\n");
    MASKDEBUG(PVFS_ATTR_DIR_SPLIT_SIZE,           "DIR_SPLIT_SIZE\n");
    MASKDEBUG(PVFS_ATTR_DIR_SERVER_NO,            "DIR_SERVER_NO\n");
    MASKDEBUG(PVFS_ATTR_DIR_BRANCH_LEVEL,         "DIR_BRANCH_LEVEL\n");
/**/MASKDEBUG(PVFS_ATTR_DIR_DIRDATA,              "DIR_DIRDATA\n");
    MASKDEBUG(PVFS_ATTR_DIR_HINT,                 "DIR_HINT\n");
    MASKDEBUG(PVFS_ATTR_DIRDATA_DIRENT_COUNT,     "DIRDATA_DIRENT_COUNT\n");
    MASKDEBUG(PVFS_ATTR_DIRDATA_TREE_HEIGHT,      "DIRDATA_TREE_HEIGHT\n");
    MASKDEBUG(PVFS_ATTR_DIRDATA_DIRDATA_MIN,      "DIRDATA_DIRDATA_MIN\n");
    MASKDEBUG(PVFS_ATTR_DIRDATA_DIRDATA_MAX,      "DIRDATA_DIRDATA_MAX\n");
    MASKDEBUG(PVFS_ATTR_DIRDATA_DIRDATA_COUNT,    "DIRDATA_DIRDATA_COUNT\n");
    MASKDEBUG(PVFS_ATTR_DIRDATA_SID_COUNT,        "DIRDATA_SID_COUNT\n");
    MASKDEBUG(PVFS_ATTR_DIRDATA_BITMAP_SIZE,      "DIRDATA_BItMAP_SIZE\n");
    MASKDEBUG(PVFS_ATTR_DIRDATA_SPLIT_SIZE,       "DIRDATA_SPLIT_SIZE\n");
    MASKDEBUG(PVFS_ATTR_DIRDATA_SERVER_NO,        "DIRDATA_SERVER_NO\n");
    MASKDEBUG(PVFS_ATTR_DIRDATA_BRANCH_LEVEL,     "DIRDATA_BRANCH_LEVEL\n");
/**/MASKDEBUG(PVFS_ATTR_CAPABILITY,               "CAPABILITY\n");
    MASKDEBUG(PVFS_ATTR_FASTEST,                  "FASTEST\n");
    MASKDEBUG(PVFS_ATTR_LATEST,                   "LATEST\n");
}

#undef MASKDEBUG
#undef DATTRPRINT
#endif

/* extended hint attributes for a metafile object  V3 */
#if 0
struct PVFS_metafile_hint_s
{
    PVFS_flags flags;
};
typedef struct PVFS_metafile_hint_s PVFS_metafile_hint;
#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
endecode_fields_1(
    PVFS_metafile_hint,
    PVFS_flags, flags);
#endif
#endif

/********************************************************************
 *         METADATA ATTRIBUTES
 ********************************************************************/

/* attributes specific to metadata objects */
struct PVFS_metafile_attr_s
{
    /* distribution */
    PINT_dist *dist;
    uint32_t dist_size;  /* not sent across wire, each side may be diff */

    /* Normally there are (dfile_count * (sid_count + 1)) 
     * oids+sids in one buffer with the oids before the
     * sids.  dfile_array points to the start of the
     * buffer and sid_array points to the address where
     * the sids start.
     */
    /* list of datafiles */
    uint32_t dfile_count;
    PVFS_handle *dfile_array;

    /* list of sids */
    PVFS_SID *sid_array;
    int32_t sid_count;   /* which sids is this conting? */

    uint32_t mirror_mode;

    /* This is a placeholder for volatile file size 64-bit */
    PVFS_size size;        /* file size, volatile, may be wrong */   

    uint64_t flags;        /* bit field for flags such as IMMUTABLE */
};

typedef struct PVFS_metafile_attr_s PVFS_metafile_attr;

/* WBL V3 we now move the SIDs whenever we move the OIDs */

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C

static inline void encode_PVFS_metafile_attr(char **pptr,
                                             const PVFS_metafile_attr *x)
{
    int dfiles_i;
    int sid_i;

    encode_PINT_dist(pptr, (PINT_dist **)&x->dist);
    encode_uint32_t(pptr, &x->mirror_mode);             
    encode_uint32_t(pptr, &x->dfile_count);          
    encode_int32_t(pptr, &x->sid_count);           
    encode_skip4(pptr,);                           

    for (dfiles_i = 0; dfiles_i < x->dfile_count; dfiles_i++)         
    {                                                                   
        encode_PVFS_handle(pptr, &(x->dfile_array[dfiles_i]));          
    }                                                                   

    for (sid_i = 0; sid_i < x->dfile_count * x->sid_count; sid_i++) 
    {                                                                   
        encode_PVFS_SID(pptr, &x->sid_array[sid_i]);                  
    }                                                                   
    encode_uint64_t(pptr, &x->flags);                        
}

/* This decodes OIDs and SIDs into a contiguous array to make it easier
 * to write to the database.
 * Some attrs may not have these filled in, so dfile_count and 
 * sid_count are zero and there is nothing to decode.
 * If there appear to be oids and/or sids in the attr but we
 * have a problem decoding we try to go ahead and decode them
 * in order to make debugging easier.
 */

static inline void decode_PVFS_metafile_attr(char **pptr,
                                             PVFS_metafile_attr *x)
{
    int dfiles_i, sid_i; 
    PVFS_OID scratch_buf; /* only used when we can't allocate mem */

#if 0
    decode_PINT_dist(pptr, &(x)->dist);                                 
    (x)->dist_size = PINT_DIST_PACK_SIZE((x)->dist);                    
#endif
    decode_PINT_dist(pptr, &(x)->dist, &(x)->dist_size);                                 
    decode_uint32_t(pptr, &(x)->mirror_mode);                           
    decode_uint32_t(pptr, &(x)->dfile_count);                           
    decode_int32_t(pptr, &(x)->sid_count);                             
    decode_skip4(pptr,);

    x->dfile_array = decode_malloc(OSASZ(x->dfile_count, x->sid_count)); 

    if (!x->dfile_array)
    {
        if (x->dfile_count)
        {
            /* error allocating memory */
            gossip_err("%s: Error allocating memory for dfile refs\n",
                       __func__);
            gossip_err("%s: dfile_count %d sid_count %d\n", 
                       __func__, x->dfile_count, x->sid_count);
        }
        x->sid_array = NULL;
    }
    else
    {
        if (!x->sid_count)
        {
            gossip_err("%s: sid_count is 0, oids without sids\n", __func__);
            x->sid_array = NULL;
        }
        else
        {
            x->sid_array = (PVFS_SID *)&(x->dfile_array[x->dfile_count]); 
        }
    }

    for (dfiles_i = 0; dfiles_i < x->dfile_count; dfiles_i++)         
    {                                                                   
        if (x->dfile_array)
        {
            decode_PVFS_handle(pptr, &x->dfile_array[dfiles_i]);          
        }
        else /* error condition */
        {
            decode_PVFS_handle(pptr, (PVFS_OID *)&scratch_buf);          
        }
    }                                                                   

    for (sid_i = 0; sid_i < (x->dfile_count * x->sid_count); sid_i++) 
    {                                                                   
        if (x->dfile_array)
        {
            decode_PVFS_SID(pptr, &x->sid_array[sid_i]);                  
        }
        else /* error condition */
        {
            decode_PVFS_SID(pptr, (PVFS_SID *)&scratch_buf);          
        }
    }

    decode_uint64_t(pptr, &(x)->flags);                        
    
    if ((x->dfile_array == NULL) || 
        (x->dfile_count == 0) || 
        (x->sid_count == 0))
    {
        /* indicate there are no oids/sids in this attr record */
        if (x->dfile_array)
        {
            free(x->dfile_array);
        }
        x->dfile_array = NULL;
        x->dfile_count = 0;
        x->sid_array = NULL;
        x->sid_count = 0;
    }
}

#define defree_PVFS_metafile_attr(x) \
do { \
    defree_PINT_dist(&(x)->dist); \
    decode_free((x)->dfile_array); \
} while(0)

#endif

/********************************************************************
 *         DATAFILE ATTRIBUTES
 ********************************************************************/

/* attributes specific to datafile objects */
struct PVFS_datafile_attr_s
{
    /* V3 maybe we should put sequence number info here? */
    PVFS_size size; /* local size of the bstream */
};
typedef struct PVFS_datafile_attr_s PVFS_datafile_attr;
endecode_fields_1(PVFS_datafile_attr, PVFS_size, size);

/********************************************************************
 *         DIRECTORY ATTRIBUTES
 ********************************************************************/

/* this is only for layouts used as directory hints to
 * prevent some of the conversion back and forth between
 * strings and BMI_addr_t that goes on otherwise
 */
struct PVFS_dirhint_server_list_s
{
    uint32_t count;
    uint32_t bufsize;
    char *servers;
};
typedef struct PVFS_dirhint_server_list_s PVFS_dirhint_server_list;

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
endecode_fields_3(PVFS_dirhint_server_list,
        int32_t, count,
        int32_t, bufsize,
        string, servers);
#endif

struct PVFS_dirhint_layout_s
{
    enum PVFS_sys_layout_algorithm algorithm;
    PVFS_dirhint_server_list server_list;
};
typedef struct PVFS_dirhint_layout_s PVFS_dirhint_layout;

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
endecode_fields_2(PVFS_dirhint_layout,
        uint32_t, algorithm,
        PVFS_dirhint_server_list, server_list);
#endif

/* extended hint attributes for a directory object */
/* these are local defaults that control creates */
struct PVFS_directory_hint_s
{
    uint32_t            dist_name_len;     /* size of dist name buffer */
    char               *dist_name;         /* distribution name */
    uint32_t            dist_params_len;   /* size of dist params buffer */
    char               *dist_params;       /* distribution parameters? */
    uint32_t            dfile_count;       /* number of dfiles to be used */
    uint32_t            dfile_sid_count;   /* number of dfile replicas */
    PVFS_dirhint_layout layout;            /* how dfile servers are selected */
    uint32_t            dir_dirdata_min;   /* min number of dirdata to be used */    
    uint32_t            dir_dirdata_max;   /* max number of dirdata to be used */    
    uint32_t            dir_split_size;    /* max number of entries before a split */
    PVFS_dirhint_layout dir_layout;        /* how dirdata servers are selected */

};
typedef struct PVFS_directory_hint_s PVFS_directory_hint;

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
endecode_fields_13(
        PVFS_directory_hint,
        uint32_t, dist_name_len,
        skip4,,
        string,   dist_name,
        uint32_t, dist_params_len,
        skip4,,
        string,   dist_params,
        uint32_t, dfile_count,
        uint32_t,  dfile_sid_count,
        PVFS_dirhint_layout, layout,
        uint32_t, dir_dirdata_min,
        uint32_t, dir_dirdata_max,
        uint32_t, dir_split_size,
        PVFS_dirhint_layout, dir_layout);
#endif

/* attributes specific to directory objects */
struct PVFS_directory_attr_s
{
    /* V3 when we get dist dir we will need sids, mirroring, etc. here */
    PVFS_size dirent_count;  /* num in dir, global, volatile */
    PVFS_directory_hint hint;

    PVFS_dist_dir_attr dist_dir_attr;
    PVFS_dist_dir_bitmap dist_dir_bitmap;
    PVFS_handle *dirdata_handles;
    PVFS_SID *dirdata_sids;
};
typedef struct PVFS_directory_attr_s PVFS_directory_attr;

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C

static inline void encode_PVFS_directory_attr(char **pptr,
                                              const PVFS_directory_attr *x)
{
    int index_i;                                                              

    encode_PVFS_size(pptr, &(x)->dirent_count);                               
    encode_PVFS_directory_hint(pptr, &(x)->hint);                             
    encode_PVFS_dist_dir_attr(pptr, &(x)->dist_dir_attr);                     

    for (index_i = 0; index_i < (x)->dist_dir_attr.bitmap_size; index_i++)    
    {                                                                         
        encode_PVFS_dist_dir_bitmap_basetype(pptr,                            
                                             &(x)->dist_dir_bitmap[index_i]); 
    }                                                                         
    align8(pptr);                                                             

    for (index_i = 0; index_i < (x)->dist_dir_attr.dirdata_count; index_i++)  
    {                                                                         
        encode_PVFS_handle(pptr, &(x)->dirdata_handles[index_i]);             
    }                                                                         

    for (index_i = 0;                                                         
         index_i < (x)->dist_dir_attr.dirdata_count *                         
                   (x)->dist_dir_attr.sid_count;                              
         index_i++)                                                           
    {                                                                         
        encode_PVFS_SID(pptr, &(x)->dirdata_sids[index_i]);                   
    }                                                                        
}

/* This decodes OIDs and SIDs into a contiguous array to make it easier
 * to write to the database
 */

static inline void decode_PVFS_directory_attr(char **pptr,
                                              PVFS_directory_attr *x)
{
    int index_i;                                                              
    decode_PVFS_size(pptr, &(x)->dirent_count);                               
    decode_PVFS_directory_hint(pptr, &(x)->hint);                             
    decode_PVFS_dist_dir_attr(pptr, &(x)->dist_dir_attr);                     

    (x)->dist_dir_bitmap = decode_malloc((x)->dist_dir_attr.bitmap_size *     
                                      sizeof(PVFS_dist_dir_bitmap_basetype)); 

    for(index_i = 0; index_i < (x)->dist_dir_attr.bitmap_size; index_i++)     
    {                                                                         
        decode_PVFS_dist_dir_bitmap_basetype(pptr,                            
                                             &(x)->dist_dir_bitmap[index_i]); 
    }                                                                         
    align8(pptr);                                                             

    (x)->dirdata_handles = decode_malloc(OSASZ(                               
                                        (x)->dist_dir_attr.dirdata_count,     
                                        (x)->dist_dir_attr.sid_count));       

    (x)->dirdata_sids =                                                       
         (PVFS_SID *)&(x)->dirdata_handles[(x)->dist_dir_attr.dirdata_count]; 

    for(index_i = 0; index_i < (x)->dist_dir_attr.dirdata_count; index_i++)   
    {                                                                         
        decode_PVFS_handle(pptr, &(x)->dirdata_handles[index_i]);             
    }                                                                         

    for(index_i = 0; index_i < (x)->dist_dir_attr.dirdata_count *             
                               (x)->dist_dir_attr.sid_count; index_i++)       
    {                                                                         
        decode_PVFS_SID(pptr, &(x)->dirdata_sids[index_i]);                   
    }                                                                         
}

#define defree_PVFS_directory_attr(x)       \
do {                                        \
    defree_PVFS_directory_hint(&(x)->hint); \
    decode_free(&(x)->dist_dir_bitmap);     \
    decode_free(&(x)->dirdata_handles);     \
} while(0)

#endif

/********************************************************************
 *         DIRDATA ATTRIBUTES
 ********************************************************************/

/* attributes specific to dirdata objects */
struct PVFS_dirdata_attr_s
{
    int32_t dirent_count;
    void *__PAD;   /* to match directory structure */

    PVFS_dist_dir_attr    dist_dir_attr;
    PVFS_dist_dir_bitmap  dist_dir_bitmap;
    PVFS_handle          *dirdata_handles;  /* These are siblings for splitting */
    PVFS_SID             *dirdata_sids;
};
typedef struct PVFS_dirdata_attr_s PVFS_dirdata_attr;

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C

static inline void encode_PVFS_dirdata_attr(char **pptr,
                                            const PVFS_dirdata_attr *x)
{
    int index_i;                                                              

    encode_PVFS_size(pptr, &(x)->dirent_count);                               

    /* void ptr not encoded or decoded */                                     
    encode_PVFS_dist_dir_attr(pptr, &(x)->dist_dir_attr);                     


    for (index_i = 0; index_i<(x)->dist_dir_attr.bitmap_size; index_i++)      
    {                                                                         
        encode_PVFS_dist_dir_bitmap_basetype(pptr,                            
                                             &(x)->dist_dir_bitmap[index_i]); 
    }                                                                         
    align8(pptr);                                                             

    for (index_i = 0; index_i < (x)->dist_dir_attr.dirdata_count; index_i++)  
    {                                                                         
        encode_PVFS_handle(pptr, &(x)->dirdata_handles[index_i]);             
    }                                                                         

    for (index_i = 0; index_i < (x)->dist_dir_attr.dirdata_count *            
                                (x)->dist_dir_attr.sid_count; index_i++)      
    {                                                                         
        encode_PVFS_SID(pptr, &(x)->dirdata_sids[index_i]);                   
    }                                                                         
}

/* This decodes OIDs and SIDs into a contiguous array to make it easier
 * to write to the database
 */

static inline void decode_PVFS_dirdata_attr(char **pptr,
                                            PVFS_dirdata_attr *x)
{
    int index_i;                                                              

    decode_PVFS_size(pptr, &(x)->dirent_count);                               
    /* void ptr not encoded or decoded */                                     
    decode_PVFS_dist_dir_attr(pptr, &(x)->dist_dir_attr);                     

    (x)->dist_dir_bitmap = decode_malloc((x)->dist_dir_attr.bitmap_size *     
                                     sizeof(PVFS_dist_dir_bitmap_basetype));  

    for(index_i = 0; index_i < (x)->dist_dir_attr.bitmap_size; index_i++)     
    {                                                                         
        decode_PVFS_dist_dir_bitmap_basetype(pptr,                            
                                             &(x)->dist_dir_bitmap[index_i]); 
    }                                                                         
    align8(pptr);                                                             

    (x)->dirdata_handles = decode_malloc(OSASZ(                               
                                        (x)->dist_dir_attr.dirdata_count,     
                                        (x)->dist_dir_attr.sid_count));       

    (x)->dirdata_sids =                                                       
         (PVFS_SID *)&(x)->dirdata_handles[(x)->dist_dir_attr.dirdata_count]; 

    for(index_i = 0; index_i < (x)->dist_dir_attr.dirdata_count; index_i++)   
    {                                                                         
        decode_PVFS_handle(pptr, &(x)->dirdata_handles[index_i]);             
    }                                                                         

    for(index_i = 0; index_i < (x)->dist_dir_attr.dirdata_count *             
                               (x)->dist_dir_attr.sid_count; index_i++)       
    {                                                                         
        decode_PVFS_SID(pptr, &(x)->dirdata_sids[index_i]);                   
    }                                                                         
}

#define defree_PVFS_dirdata_attr(x)     \
do {                                    \
    decode_free(&(x)->dist_dir_bitmap); \
    decode_free(&(x)->dirdata_handles); \
} while(0)

#endif

/********************************************************************
 *         SYMLINK ATTRIBUTES
 ********************************************************************/

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
        string, target_path);

/********************************************************************
 *         OBJECT ATTRIBUTES         
 ********************************************************************/

/* generic attributes; applies to all objects */
struct PVFS_object_attr
{
    PVFS_ds_type objtype;      /* defined in pvfs2-types.h */
    PVFS_object_attrmask mask; /* indicates which fields are currently valid */
    PVFS_uid owner;            /* uid */
    PVFS_gid group;            /* gid */
    PVFS_permissions perms;    /* mode */
    PVFS_time atime;           /* access (read) time */
    PVFS_time mtime;           /* modify (data) time */
    PVFS_time ctime;           /* change (metadata) time */
    PVFS_time ntime;           /* new (create) time */
    uint32_t meta_sid_count;   /* number of metadata sids in this FS */
    PVFS_capability capability;
    PVFS_handle *parent;       /* handle for parent object */
    PVFS_SID *parent_sids;     /* num parent sids is meta_sid_count */

    union
    {
        PVFS_metafile_attr meta;
        PVFS_datafile_attr data;
        PVFS_directory_attr dir;
        PVFS_dirdata_attr dirdata;
        PVFS_symlink_attr sym;
    }
    u;
};
typedef struct PVFS_object_attr PVFS_object_attr;

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C

static inline void encode_PVFS_object_attr(char **pptr, 
                                           const PVFS_object_attr *x) 
{ 
    int index_i = 0;
    encode_PVFS_ds_type(pptr, &(x)->objtype); 
    encode_PVFS_object_attrmask(pptr, &(x)->mask); 
    encode_PVFS_uid(pptr, &(x)->owner); 
    encode_PVFS_gid(pptr, &(x)->group); 
    encode_PVFS_permissions(pptr, &(x)->perms); 
    encode_skip4(pptr,); 
    encode_PVFS_time(pptr, &(x)->atime); 
    encode_PVFS_time(pptr, &(x)->mtime); 
    encode_PVFS_time(pptr, &(x)->ctime); 
    encode_PVFS_time(pptr, &(x)->ntime); 
    encode_uint32_t(pptr, &(x)->meta_sid_count); 
    encode_skip4(pptr,);
    encode_PVFS_capability(pptr, &(x)->capability); 
    encode_PVFS_handle(pptr, &(x)->parent);             
    align8(pptr);                                                             
    for (index_i = 0; index_i < (x)->meta_sid_count; index_i++)      
    {                                                                         
        encode_PVFS_SID(pptr, &(x)->parent_sids[index_i]);                   
    }                                                                         
    switch ((x)->objtype) 
    { 
    case PVFS_TYPE_METAFILE : 
	encode_PVFS_metafile_attr(pptr, &(x)->u.meta); 
        break; 
    case PVFS_TYPE_DATAFILE : 
	encode_PVFS_datafile_attr(pptr, &(x)->u.data); 
        break; 
    case PVFS_TYPE_DIRECTORY : 
	encode_PVFS_directory_attr(pptr, &(x)->u.dir); 
        break; 
    case PVFS_TYPE_DIRDATA : 
	encode_PVFS_dirdata_attr(pptr, &(x)->u.dirdata); 
        break; 
    case PVFS_TYPE_SYMLINK : 
	encode_PVFS_symlink_attr(pptr, &(x)->u.sym); 
        break; 
    default : 
        break; 
    } 
    align8(pptr); 
}

static inline void decode_PVFS_object_attr(char **pptr, PVFS_object_attr *x) 
{
    int index_i = 0;
    decode_PVFS_ds_type(pptr, &(x)->objtype); 
    decode_PVFS_object_attrmask(pptr, &(x)->mask); 
    decode_PVFS_uid(pptr, &(x)->owner); 
    decode_PVFS_gid(pptr, &(x)->group); 
    decode_PVFS_permissions(pptr, &(x)->perms); 
    decode_skip4(pptr,); 
    decode_PVFS_time(pptr, &(x)->atime); 
    decode_PVFS_time(pptr, &(x)->mtime); 
    decode_PVFS_time(pptr, &(x)->ctime); 
    decode_PVFS_time(pptr, &(x)->ntime); 
    decode_uint32_t(pptr, &(x)->meta_sid_count); 
    decode_skip4(pptr,); 
    decode_PVFS_capability(pptr, &(x)->capability); 
    align8(pptr);                                                             

    (x)->parent = decode_malloc(OSASZ(1, (x)->meta_sid_count));       
    (x)->parent_sids = (PVFS_SID *)&(x)->parent[1]; 

    decode_PVFS_handle(pptr, (x)->parent);             
    for (index_i = 0; index_i < (x)->meta_sid_count; index_i++)      
    {                                                                         
        decode_PVFS_SID(pptr, (x)->parent_sids + index_i);                   
    }                                                                         

    switch ((x)->objtype) 
    { 
    case PVFS_TYPE_METAFILE : 
	decode_PVFS_metafile_attr(pptr, &(x)->u.meta); 
        break; 
    case PVFS_TYPE_DATAFILE : 
	decode_PVFS_datafile_attr(pptr, &(x)->u.data); 
        break; 
    case PVFS_TYPE_DIRECTORY : 
	decode_PVFS_directory_attr(pptr, &(x)->u.dir); 
        break; 
    case PVFS_TYPE_DIRDATA : 
	decode_PVFS_dirdata_attr(pptr, &(x)->u.dirdata); 
        break; 
    case PVFS_TYPE_SYMLINK : 
	decode_PVFS_symlink_attr(pptr, &(x)->u.sym); 
        break; 
    default : 
        break; 
    } 
    align8(pptr); 
}

static inline void defree_PVFS_object_attr(PVFS_object_attr *x) 
{ 
    defree_PVFS_capability(&(x)->capability); 
    decode_free(&(x)->parent);
    switch ((x)->objtype) 
    { 
    case PVFS_TYPE_METAFILE : 
	defree_PVFS_metafile_attr(&(x)->u.meta); 
        break; 
    case PVFS_TYPE_DATAFILE : 
	defree_PVFS_datafile_attr(&(x)->u.data); 
        break; 
    case PVFS_TYPE_DIRECTORY : 
	defree_PVFS_directory_attr(&(x)->u.dir); 
        break; 
    case PVFS_TYPE_DIRDATA : 
	defree_PVFS_dirdata_attr(&(x)->u.dirdata); 
        break; 
    case PVFS_TYPE_SYMLINK : 
	defree_PVFS_symlink_attr(&(x)->u.sym); 
        break; 
    default : 
        break; 
    } 
}

#endif

/* attr buffer needs room for larger of symlink path, meta fields or 
 * dir hints: an attrib structure can never hold information for not more 
 * than a symlink or a metafile or a dir object 
*/
#define extra_size_PVFS_object_attr_dir  (PVFS_REQ_LIMIT_DIST_BYTES + \
  PVFS_REQ_LIMIT_DIST_NAME + roundup8(sizeof(PVFS_directory_attr)))

#define extra_size_PVFS_distdir \
  (PVFS_REQ_LIMIT_HANDLES_COUNT * sizeof(PVFS_handle))
/*TODO: PVFS_REQ_LIMIT_HANDLES_COUNT really needs to change to something
        indicating the max number of servers */

/* room for distribution, stuffed_size, dfile array, and mirror_dfile_array */
#define extra_size_PVFS_object_attr_meta (PVFS_REQ_LIMIT_DIST_BYTES + \
  sizeof(int32_t) +                                                   \
  (PVFS_REQ_LIMIT_DFILE_COUNT * sizeof(PVFS_handle)) +                \
  (PVFS_REQ_LIMIT_MIRROR_DFILE_COUNT * sizeof(PVFS_handle))) 

#define extra_size_PVFS_object_attr_symlink (PVFS_REQ_LIMIT_PATH_NAME_BYTES)

#define extra_size_PVFS_object_attr_capability extra_size_PVFS_capability

#define extra_size_PVFS_object_attr \
                           (extra_size_PVFS_object_attr_capability + \
                                    extra_size_PVFS_distdir + \
                                    max(max(extra_size_PVFS_object_attr_meta, \
                            extra_size_PVFS_object_attr_symlink), \
                            extra_size_PVFS_object_attr_dir))

#endif /* __PVFS2_ATTR_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
