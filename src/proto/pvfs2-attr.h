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

#define PVFS_ATTR_FASTEST            (1 << 15)
#define PVFS_ATTR_LATEST             (1 << 16)

/* latest and fastest refer to atime, mtime, ctime, file size
 * and dirent_count.  Fastest returns the value stored in the
 * attributes of the target meta or dir, whereas latest reads
 * these values from each of the dfile or dirdata objects
 * referenced by the meta or dir and calculates the global
 * value and updates the "fastest" version.  Presumably a
 * scrubber or some such will update these periodically.  User
 * interfaces can choose to force a "latest" when desired
 */

/* internal attribute masks, common to all obj types */
#define PVFS_ATTR_COMMON_UID         (1 << 0)
#define PVFS_ATTR_COMMON_GID         (1 << 1)
#define PVFS_ATTR_COMMON_PERM        (1 << 2)
#define PVFS_ATTR_COMMON_ATIME       (1 << 3)
#define PVFS_ATTR_COMMON_CTIME       (1 << 4)
#define PVFS_ATTR_COMMON_MTIME       (1 << 5)
#define PVFS_ATTR_COMMON_NTIME       (1 << 6)
#define PVFS_ATTR_COMMON_TYPE        (1 << 7)
#define PVFS_ATTR_COMMON_ATIME_SET   (1 << 8)
#define PVFS_ATTR_COMMON_CTIME_SET   (1 << 9)
#define PVFS_ATTR_COMMON_MTIME_SET   (1 << 10)
#define PVFS_ATTR_COMMON_NTIME_SET   (1 << 11)

#define PVFS_ATTR_COMMON_NOTIME                           \
        (PVFS_ATTR_COMMON_UID  | PVFS_ATTR_COMMON_GID   | \
         PVFS_ATTR_COMMON_PERM | PVFS_ATTR_COMMON_TYPE)

#define PVFS_ATTR_COMMON_ALL                                \
        (PVFS_ATTR_COMMON_NOTIME | PVFS_ATTR_COMMON_ATIME | \
         PVFS_ATTR_COMMON_CTIME  | PVFS_ATTR_COMMON_MTIME | \
         PVFS_ATTR_COMMON_NTIME)

#define PVFS_ATTR_NOTIME_SET \
        ~(PVFS_ATTR_COMMON_ATIME_SET | PVFS_ATTR_COMMON_NTIME_SET | \
          PVFS_ATTR_COMMON_CTIME_SET | PVFS_ATTR_COMMON_MTIME_SET)

#define PVFS_ATTR_TIME_ALL \
        (PVFS_ATTR_COMMON_ATIME | PVFS_ATTR_COMMON_NTIME | \
         PVFS_ATTR_COMMON_CTIME | PVFS_ATTR_COMMON_MTIME)

/* internal attribute masks for metadata objects */
#define PVFS_ATTR_META_DIST          (1 << 12)
#define PVFS_ATTR_META_DFILES        (1 << 25)   /* includes sids */
#define PVFS_ATTR_META_MIRROR_MODE   (1 << 17)   /* writable */
#define PVFS_ATTR_META_FLAGS         (1 << 13)   /* writable */

#define PVFS_ATTR_META_ALL                             \
        (PVFS_ATTR_META_DIST | PVFS_ATTR_META_DFILES | \
        PVFS_ATTR_META_FLAGS | PVFS_ATTR_COMMON_ALL)

/* internal attribute masks for datafile objects */
#define PVFS_ATTR_DATA_SIZE          (1 << 20)   /* size of a DFILE
                                                  * replace with latest bit */

#define PVFS_ATTR_DATA_ALL \
             (PVFS_ATTR_DATA_SIZE | PVFS_ATTR_TIME_ALL)

/* internal attribute masks for symlink objects */
#define PVFS_ATTR_SYMLNK_TARGET      (1 << 19)   /* writable */

#define PVFS_ATTR_SYMLNK_ALL \
             (PVFS_ATTR_SYMLNK_TARGET | PVFS_ATTR_COMMON_ALL)

/* internal attribute masks for directory objects */
#define PVFS_ATTR_DIR_DIRENT_COUNT   (1 << 26)   /* replace with latest bit */
#define PVFS_ATTR_DIR_DIRDATA        (1 << 14)   /* includes sids */
#define PVFS_ATTR_DIR_HINT           (1 << 22)   /* writable */

#define PVFS_ATTR_DIR_ALL \
             (PVFS_ATTR_DIR_DIRENT_COUNT | PVFS_ATTR_DIR_HINT | \
              PVFS_ATTR_DIR_DIRDATA | PVFS_ATTR_DISTDIR_ATTR | \
              PVFS_ATTR_COMMON_ALL)

#define PVFS_ATTR_DIRDATA_ALL \
             (PVFS_ATTR_DIR_DIRENT_COUNT | PVFS_ATTR_TIME_ALL)

/* internal attribute mask for distributed directory information */
/* this may be in the meta or dirdata area depending on objtype */
/* This includes the bitmap and dirdata handles/sids info */
#define PVFS_ATTR_DISTDIR_ATTR       (1 << 21)    /* writable */

/* internal attribute mask for capability objects */
#define PVFS_ATTR_CAPABILITY         (1 << 18)

/* attributes that do not change once set */
/* needs to be renamed */
#define PVFS_STATIC_ATTR_MASK                                   \
        (PVFS_ATTR_COMMON_TYPE | PVFS_ATTR_META_DIST |          \
        PVFS_ATTR_META_DFILES  | PVFS_ATTR_META_MIRROR_DFILES | \
        PVFS_ATTR_META_UNSTUFFED)

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
    uint32_t sid_count;

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
    encode_uint32_t(pptr, &x->sid_count);           
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

    decode_PINT_dist(pptr, &x->dist, &x->dist_size);
    decode_uint32_t(pptr, &x->mirror_mode);                           
    decode_uint32_t(pptr, &x->dfile_count);                           
    decode_uint32_t(pptr, &x->sid_count);                             
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

/* attributes specific to datafile objects */
struct PVFS_datafile_attr_s
{
    /* V3 maybe we should put sequence number info here? */
    PVFS_size size; /* local size of the bstream */
};
typedef struct PVFS_datafile_attr_s PVFS_datafile_attr;
endecode_fields_1(PVFS_datafile_attr, PVFS_size, size);

/* this is only for layouts used as directory hints to
 * prevent some of the conversion back and forth between
 * strings and BMI_addr_t that goes on otherwise
 */
struct PVFS_dirhint_server_list_s
{
    int32_t count;
    int32_t bufsize;
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
    uint32_t            dist_name_len;
    /* what is the distribution name? */
    char               *dist_name;
    /* what are the distribution parameters? */
    uint32_t            dist_params_len;
    char               *dist_params;
    /* how many dfiles ought to be used */
    uint32_t            dfile_count;
    uint32_t            dfile_sid_count;
    /* how servers are selected */
    PVFS_dirhint_layout layout;
};
typedef struct PVFS_directory_hint_s PVFS_directory_hint;

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
endecode_fields_9(
        PVFS_directory_hint,
        uint32_t, dist_name_len,
        skip4,,
        string,   dist_name,
        uint32_t, dist_params_len,
        skip4,,
        string,   dist_params,
        uint32_t, dfile_count,
        uint32_t, dfile_sid_count,
        PVFS_dirhint_layout, layout);
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

/* attributes specific to dirdata objects */
struct PVFS_dirdata_attr_s
{
    int32_t dirent_count;
    void *__PAD;   /* to match directory structure */

    PVFS_dist_dir_attr dist_dir_attr;
    PVFS_dist_dir_bitmap dist_dir_bitmap;
    PVFS_handle *dirdata_handles;  /* These are siblings for splitting */
    PVFS_SID *dirdata_sids;
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

/* generic attributes; applies to all objects */
struct PVFS_object_attr
{
    PVFS_ds_type objtype; /* defined in pvfs2-types.h */
    uint32_t mask;          /* indicates which fields are currently valid */
    PVFS_uid owner;         /* uid */
    PVFS_gid group;         /* gid */
    PVFS_permissions perms; /* mode */
    PVFS_time atime;        /* access (read) time */
    PVFS_time mtime;        /* modify (data) time */
    PVFS_time ctime;        /* change (metadata) time */
    PVFS_time ntime;        /* new (create) time */
    PVFS_capability capability;

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
    encode_PVFS_ds_type(pptr, &(x)->objtype); 
    encode_uint32_t(pptr, &(x)->mask); 
    encode_PVFS_uid(pptr, &(x)->owner); 
    encode_PVFS_gid(pptr, &(x)->group); 
    encode_PVFS_permissions(pptr, &(x)->perms); 
    encode_skip4(pptr,); 
    encode_PVFS_time(pptr, &(x)->atime); 
    encode_PVFS_time(pptr, &(x)->mtime); 
    encode_PVFS_time(pptr, &(x)->ctime); 
    encode_PVFS_time(pptr, &(x)->ntime); 
    encode_PVFS_capability(pptr, &(x)->capability); 
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
    decode_PVFS_ds_type(pptr, &(x)->objtype); 
    decode_uint32_t(pptr, &(x)->mask); 
    decode_PVFS_uid(pptr, &(x)->owner); 
    decode_PVFS_gid(pptr, &(x)->group); 
    decode_PVFS_permissions(pptr, &(x)->perms); 
    decode_skip4(pptr,); 
    decode_PVFS_time(pptr, &(x)->atime); 
    decode_PVFS_time(pptr, &(x)->mtime); 
    decode_PVFS_time(pptr, &(x)->ctime); 
    decode_PVFS_time(pptr, &(x)->ntime); 
    decode_PVFS_capability(pptr, &(x)->capability); 
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
