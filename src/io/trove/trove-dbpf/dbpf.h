/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __DBPF_H__
#define __DBPF_H__

#include "pvfs2-internal.h"

#if defined(__cplusplus)
extern "C" {
#endif

#include <aio.h>
#include "trove.h"
#include "gen-locks.h"
#include "pvfs2-internal.h"
#include "dbpf-keyval-pcache.h"
#include "dbpf-open-cache.h"
#include "pint-event.h"
#include "dbpf-db.h"

/* For unknown Berkeley DB errors, we return some large value
 */
/* This will return 19, ENOENT, No such device due to the bits in 4243. */
#define DBPF_ERROR_UNKNOWN 4243

/* Incremental versions are backward compatible with previous releases
 * that have the same major and minor versions.
 * Minor versions are NOT backward compatible.
 * Major versions aren't either, but refer to architectural storage format changes.
 */
#define TROVE_DBPF_VERSION_KEY                       "trove-dbpf-version"
#define TROVE_DBPF_VERSION_VALUE                                  "0.1.6"   

#define LAST_HANDLE_STRING                                  "last_handle"

#define TROVE_DB_MODE                                                 0600
#define TROVE_FD_MODE 0600

/*
  for more efficient host filesystem accesses, we have a simple
  DBPF_BSTREAM_MAX_NUM_BUCKETS define that can be thought of more or
  less as buckets for storing bstreams based on a simple hash of the
  handle.

  in practice, this means we spread all bstreams into
  DBPF_BSTREAM_MAX_NUM_BUCKETS directories instead of keeping all bstream
  entries in a flat bstream directory on the host filesystem.
*/
#define DBPF_BSTREAM_MAX_NUM_BUCKETS  64

#define DBPF_BSTREAM_GET_BUCKET(__handle)                                \
    (PVFS_OID_hash32(&(__handle)) % DBPF_BSTREAM_MAX_NUM_BUCKETS)

#define DBPF_GET_DATA_DIRNAME(__buf, __path_max, __base)                 \
    do { snprintf(__buf, __path_max, "/%s", __base); } while (0)

#define DBPF_GET_META_DIRNAME(__buf, __path_max, __base)                 \
    do { snprintf(__buf, __path_max, "/%s", __base); } while (0)

#define DBPF_GET_CONFIG_DIRNAME(__buf, __path_max, __base)               \
    do { snprintf(__buf, __path_max, "/%s", __base); } while (0)

#define STO_ATTRIB_DBNAME "storage_attributes.db"
#define DBPF_GET_STO_ATTRIB_DBNAME(__buf, __path_max, __base)             \
    do {                                                                  \
        snprintf(__buf, __path_max, "/%s/%s", __base, STO_ATTRIB_DBNAME); \
    } while (0)

#define COLLECTIONS_DBNAME "collections.db"
#define DBPF_GET_COLLECTIONS_DBNAME(__buf, __path_max, __base)             \
    do {                                                                   \
        snprintf(__buf, __path_max, "/%s/%s", __base, COLLECTIONS_DBNAME); \
    } while (0)

#define DBPF_GET_COLL_DIRNAME(__buf, __path_max, __base, __collid)       \
    do {                                                                 \
        snprintf(__buf, __path_max, "/%s/%08x", __base, __collid);       \
    } while (0)

#define COLL_ATTRIB_DBNAME "collection_attributes.db"
#define DBPF_GET_COLL_ATTRIB_DBNAME(__buf,__path_max,__base,__collid)    \
    do {                                                                 \
        snprintf(__buf, __path_max, "/%s/%08x/%s", __base, __collid,     \
                 COLL_ATTRIB_DBNAME);                                    \
    } while (0)

#define DS_ATTRIB_DBNAME "dataspace_attributes.db"
#define DBPF_GET_DS_ATTRIB_DBNAME(__buf,__path_max,__base,__collid)      \
    do {                                                                 \
        snprintf(__buf, __path_max, "/%s/%08x/%s", __base, __collid,     \
                 DS_ATTRIB_DBNAME);                                      \
    } while (0)

#define BSTREAM_DIRNAME "bstreams"
#define DBPF_GET_BSTREAM_DIRNAME(__buf, __path_max, __base, __collid)    \
    do {                                                                 \
        snprintf(__buf, __path_max, "/%s/%08x/%s", __base, __collid,     \
                 BSTREAM_DIRNAME);                                       \
    } while (0)

#define STRANDED_BSTREAM_DIRNAME "stranded-bstreams"
#define DBPF_GET_STRANDED_BSTREAM_DIRNAME(                       \
        __buf, __path_max, __base, __collid)                     \
    do {                                                         \
        snprintf(__buf, __path_max, "/%s/%08x/%s",               \
                 __base, __collid, STRANDED_BSTREAM_DIRNAME);    \
    } while(0)

/* arguments are: buf, path_max, base, collid, handle */
#define DBPF_GET_BSTREAM_FILENAME(__b, __pm, __base, __cid, __handle)     \
    do {                                                                  \
      snprintf(__b, __pm, "/%s/%08x/%s/%.8llu/%s.bstream",                \
               __base, __cid, BSTREAM_DIRNAME,                            \
               llu(DBPF_BSTREAM_GET_BUCKET(__handle)),                    \
               PVFS_OID_str(&(__handle)));                                \
    } while (0)

/* arguments are: buf, path_max, base, collid, handle */
#define DBPF_GET_STRANDED_BSTREAM_FILENAME(                  \
        __b, __pm, __base, __cid, __handle)                  \
    do {                                                     \
        snprintf(__b, __pm, "/%s/%08x/%s/%s.bstream",        \
                 __base, __cid, STRANDED_BSTREAM_DIRNAME,    \
                 PVFS_OID_str(&(__handle)));                 \
    } while(0)

/* arguments are: buf, path_max, base, collid */
#define KEYVAL_DBNAME "keyval.db"
#define DBPF_GET_KEYVAL_DBNAME(__buf, __path_max, __base, __collid)      \
    do {                                                                 \
        snprintf(__buf,                                                  \
                 __path_max,                                             \
                 "/%s/%08x/%s",                                          \
                 __base,                                                 \
                 __collid,                                               \
               KEYVAL_DBNAME);                                           \
    } while (0)

inline int dbpf_pread(int fd, void *buf, size_t count, off_t offset);
inline int dbpf_pwrite(int fd, const void *buf, size_t count, off_t offset);

extern struct TROVE_bstream_ops dbpf_bstream_ops;
extern struct TROVE_dspace_ops dbpf_dspace_ops;
extern struct TROVE_keyval_ops dbpf_keyval_ops;
extern struct TROVE_mgmt_ops dbpf_mgmt_ops;

extern PINT_event_group trove_dbpf_event_group;

extern PINT_event_type trove_dbpf_read_event_id;
extern PINT_event_type trove_dbpf_write_event_id;
extern PINT_event_type trove_dbpf_keyval_write_event_id;
extern PINT_event_type trove_dbpf_keyval_read_event_id;
extern PINT_event_type trove_dbpf_dspace_create_event_id;
extern PINT_event_type trove_dbpf_dspace_create_list_event_id;
extern PINT_event_type trove_dbpf_dspace_getattr_event_id;
extern PINT_event_type trove_dbpf_dspace_setattr_event_id;

extern int dbpf_pid;

struct dbpf_aio_ops
{
    int (*aio_read) (struct aiocb *aiocbp);
    int (*aio_write) (struct aiocb *aiocbp);
    int (*lio_listio) (int mode,
                        struct aiocb * const list[],
                        int nent,
                        struct sigevent *sig);
    int (*aio_error) (const struct aiocb *aiocbp);
    ssize_t (*aio_return) (struct aiocb *aiocbp);
    int (*aio_cancel) (int filedesc, struct aiocb *aiocbp);
    int (*aio_suspend) (const struct aiocb * const list[],
                        int nent,
                        const struct timespec *timeout);
    int (*aio_fsync) (int operation, struct aiocb *aiocbp);
};

typedef int (* PINT_dbpf_keyval_iterate_callback)(dbpf_cursor *,
                                                  TROVE_handle handle,
                                                  TROVE_keyval_s *key,
                                                  TROVE_keyval_s *val);

int PINT_dbpf_keyval_iterate(
    dbpf_db *db,
    TROVE_handle handle,
    char type,
    PINT_dbpf_keyval_pcache *pcache,    
    TROVE_keyval_s *keys_array,
    TROVE_keyval_s *values_array,
    int *count,
    TROVE_ds_position pos,
    PINT_dbpf_keyval_iterate_callback callback);

int PINT_dbpf_dspace_remove_keyval(
    dbpf_cursor *, TROVE_handle handle, TROVE_keyval_s *, TROVE_keyval_s *);

/**
 * Structure for key in the keyval DB:
 *
 * The keys in the keyval database are now stored as the following
 * struct (dbpf_keyval_db_entry).  The size of key field (the common
 * name or component name of the key) is not explicitly specified in the
 * struct, instead it is calculated from the DBT->size field of the
 * berkeley db key using the macros below.  Its important that the
 * 'size' and 'ulen' fields of the DBT key struct are set correctly when
 * calling get and put.  'size' should be the actual size of the string, 'ulen'
 * should be the size available for the dbpf_keyval_db_entry struct, most
 * likely sizeof(struct dbpf_keyval_db_entry).
 */

/* Note - DBPF_MAX_KEY_LENGTH is also defined in trove-migrate.c. Any
 * change should be evaluated for its impact there.
 */
#define DBPF_MAX_KEY_LENGTH PVFS_NAME_MAX

struct dbpf_keyval_db_entry
{
    TROVE_handle handle;
    char type; /* will be one of the types enumerated by dbpf_key_type */
    char key[DBPF_MAX_KEY_LENGTH];
};

#define DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(_size) \
    (sizeof(TROVE_handle) + sizeof(char) + _size)

#define DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(_size) \
    (_size - sizeof(TROVE_handle) - sizeof(char))

/**
 * The keyval database contains attributes for pvfs2 handles
 * (files, directories, symlinks, etc.) that are not considered
 * common attributes.  Each key in the keyval database consists of a
 * handle and a string.  The handles can be of different types, and the strings
 * vary based on the type of handle and the data to be stored (the value for that
 * key).  The following table lists all the currently stored keyvals, 
 * based on their handle type:
 *
 * Handle Type   Key Class   Key                       Value    
 * ================================================================
 * 
 * meta-file     COMMON      "mh"                      Datafile Array
 * meta-file     COMMON      "md"                      Distribution
 * symlink       COMMON      "st"                      Target Handle
 * directory     COMMON      "de"                      Entries Handle
 * dir-ent       COMPONENT   <component name>          Entry Handle
 * [metafile, 
 *  symlink, 
 *  directory]   XATTR       <extended attribute name> <extended attribute content>
 *
 * The descriptions for the common keys are:
 *
 * md:  (m)etafile (d)istribution - stores the distribution type
 * mh:  stores the (d)atafile (h)andles that exist for this metafile
 * st:  stores the (s)ymlink (t)arget path that the symlink references
 * de:  stores the handle that manages the (d)irectory (e)ntries for this directory
 *
 * The <component name> strings are the actual object names 
 * (files, directories, etc) in the directory.  They map to the handles for those 
 * objects.
 *
 * There is also now a special 'null' keyval that has a handle and the null
 * string as its key.  This acts as handle info for a particular handle.  This
 * is useful for dir-ent handles, where the number of entries on that handle
 * must be counted.  The null keyval is accessed internally, based on flags
 * passed in through the API.
 */

struct dbpf_storage
{
    TROVE_ds_flags flags;
    int refct;
    char *data_path;   /* path to data storage directory */
    char *meta_path;   /* path to metadata storage directory */
    char *config_path; /* path to config storage directory */
    dbpf_db *sto_attr_db;
    dbpf_db *coll_db;
};

struct dbpf_collection
{
    int refct;
    char *name;
    char *data_path;   /* path to data collection directory */
    char *meta_path;   /* path to metadata collection directory */
    char *config_path; /* path to config collection directory */
    dbpf_db *coll_attr_db;
    dbpf_db *ds_db;
    dbpf_db *keyval_db;
    TROVE_coll_id coll_id;
    TROVE_handle root_dir_handle;
    struct dbpf_storage *storage;
    struct handle_ledger *free_handles;
    PINT_dbpf_keyval_pcache * pcache; /* the position cache for iterators */

    /* used by dbpf_collection.c calls to maintain list of collections */
    struct dbpf_collection *next_p;
    
    int c_low_watermark;
    int c_high_watermark;
    int meta_sync_enabled;
    /*
     * If this option is on we don't queue ops or use threads.
     */
    int immediate_completion;
};

/* Structure stored as data in collections database with collection
 * directory name as key.
 */
struct dbpf_collection_db_entry
{
    TROVE_coll_id coll_id;
};

/* entry types */
#define DBPF_ENTRY_TYPE_CONST      0x01
#define DBPF_ENTRY_TYPE_COMPONENT  0x02

int dbpf_dspace_attr_get(struct dbpf_collection *coll_p,
                         TROVE_object_ref ref,
                         TROVE_ds_attributes *attr);

int dbpf_dspace_attr_set(struct dbpf_collection *coll_p,
                         TROVE_object_ref ref,
                         TROVE_ds_attributes *attr);

struct dbpf_dspace_create_op
{
    TROVE_handle handle;
    TROVE_handle *out_handle;
    TROVE_ds_type type;
    /* hint? */
};

struct dbpf_dspace_create_list_op
{
    TROVE_handle *handle_array;
    TROVE_handle *out_handle_array;
    TROVE_ds_type type;
    int count;
    /* hint? */
};


/* struct dbpf_dspace_remove_op {}; -- nothing belongs in here */

struct dbpf_dspace_iterate_handles_op
{
    TROVE_handle *handle_array;
    TROVE_ds_position *position_p;
    int *count_p;
};

struct dbpf_dspace_verify_op
{
    TROVE_ds_type *type_p;
};

struct dbpf_dspace_setattr_op
{
    TROVE_ds_attributes_s *attr_p;
};

struct dbpf_dspace_getattr_op
{
    TROVE_ds_attributes_s *attr_p;
};

struct dbpf_dspace_remove_list_op
{
    int count;
    TROVE_handle          *handle_array;
    TROVE_ds_state        *error_p;
};

struct dbpf_dspace_getattr_list_op
{
    int count;
    TROVE_handle          *handle_array;
    TROVE_ds_attributes_s *attr_p;
    TROVE_ds_state        *error_p;
};

struct dbpf_keyval_read_op
{
    TROVE_keyval_s *key;
    TROVE_keyval_s *val;
    /* vtag? */
};

struct dbpf_keyval_read_list_op
{
    TROVE_keyval_s *key_array;
    TROVE_keyval_s *val_array;
    TROVE_ds_state *err_array;
    int count; /* TODO: MAKE INOUT? */
};

struct dbpf_keyval_write_op
{
    TROVE_keyval_s key;
    TROVE_keyval_s val;
    /* vtag? */
};

struct dbpf_keyval_write_list_op
{
    TROVE_keyval_s *key_array;
    TROVE_keyval_s *val_array;
    int count; /* TODO: MAKE INOUT? */
};

struct dbpf_keyval_remove_op
{
    TROVE_keyval_s key;
    TROVE_keyval_s val;
    /* vtag? */
};

struct dbpf_keyval_remove_list_op
{
    TROVE_keyval_s *key_array;
    TROVE_keyval_s *val_array;
    int *error_array;
    int count; /* TODO: MAKE INOUT? */
};

struct dbpf_keyval_iterate_op
{
    TROVE_keyval_s *key_array;
    TROVE_keyval_s *val_array;
    TROVE_ds_position *position_p;
    int *count_p;
    /* vtag? */
};

struct dbpf_keyval_iterate_keys_op
{
    TROVE_keyval_s *key_array;
    TROVE_ds_position *position_p;
    int *count_p;
    /* vtag? */
};

struct dbpf_bstream_resize_op
{
    TROVE_size size;
    /* vtag? */
    void *queued_op_ptr;
};

/* Used to maintain state of partial processing of a listio operation
 */
struct bstream_listio_state
{
    int mem_ct, stream_ct;
    TROVE_size cur_mem_size;
    char *cur_mem_off;
    TROVE_size cur_stream_size;
    TROVE_offset cur_stream_off;
};

/* Values for list_proc_state below */
enum
{
    LIST_PROC_INITIALIZED,  /* list state initialized,
                               but no aiocb array */
    LIST_PROC_INPROGRESS,   /* aiocb array allocated, ops in progress */
    LIST_PROC_ALLCONVERTED, /* all list elements converted */
    LIST_PROC_ALLPOSTED     /* all list elements also posted */
};

/* Used for both read and write list
 *
 * list_proc_state is used to retain the status of processing on the
 * list arrays.
 *
 * aiocb_array_count - size of the aiocb_array (nothing to do with #
 * of things in progress)
 */
struct dbpf_bstream_rw_list_op
{
    struct open_cache_ref open_ref;
    int fd;
    int list_proc_state;
    int opcode;
    int aiocb_array_count;
    int mem_array_count;
    int stream_array_count;
    char **mem_offset_array;
    TROVE_size *mem_size_array;
    TROVE_offset *stream_offset_array;
    TROVE_size *stream_size_array;
    TROVE_size *out_size_p;
    struct aiocb *aiocb_array;
    struct sigevent sigev;
    struct dbpf_aio_ops *aio_ops;
    struct bstream_listio_state lio_state;
    void *queued_op_ptr;
};

inline int dbpf_bstream_rw_list(TROVE_coll_id coll_id,
                                TROVE_handle handle,
                                char **mem_offset_array,
                                TROVE_size *mem_size_array,
                                int mem_count,
                                TROVE_offset *stream_offset_array,
                                TROVE_size *stream_size_array,
                                int stream_count,
                                TROVE_size *out_size_p,
                                TROVE_ds_flags flags,
                                TROVE_vtag_s *vtag,
                                void *user_ptr,
                                TROVE_context_id context_id,
                                TROVE_op_id *out_op_id_p,
                                int opcode,
                                struct dbpf_aio_ops * aio_ops,
                                PVFS_hint  hints);

enum dbpf_key_type
{
    DBPF_DIRECTORY_ENTRY_TYPE = 'd',
    DBPF_ATTRIBUTE_TYPE = 'a',
    DBPF_COUNT_TYPE = 'c'
};

struct dbpf_keyval_get_handle_info_op
{
    TROVE_keyval_handle_info *info;
};

/*
 * Keep the various types in one enum, but separate spaces in that
 * for easy comparions.  Reserve the top two bits in an eight-bit
 * space, leaving 64 entries in each.
 */
#define BSTREAM_OP_TYPE (0<<6)
#define KEYVAL_OP_TYPE  (1<<6)
#define DSPACE_OP_TYPE  (2<<6)
#define OP_TYPE_MASK    (3<<6)

/* Why is this so complicated - and what are the 6 bits for?
 * WBLH
 */

#define DBPF_OP_IS_BSTREAM(t) (((t) & OP_TYPE_MASK) == BSTREAM_OP_TYPE)
#define DBPF_OP_IS_KEYVAL(t)  (((t) & OP_TYPE_MASK) == KEYVAL_OP_TYPE)
#define DBPF_OP_IS_DSPACE(t)  (((t) & OP_TYPE_MASK) == DSPACE_OP_TYPE)

/* List of operation types that might be queued */
enum dbpf_op_type
{
    BSTREAM_READ_AT = BSTREAM_OP_TYPE,
    BSTREAM_WRITE_AT,
    BSTREAM_RESIZE,
    BSTREAM_READ_LIST,
    BSTREAM_WRITE_LIST,
    BSTREAM_VALIDATE,
    BSTREAM_FLUSH,
    KEYVAL_READ = KEYVAL_OP_TYPE,
    KEYVAL_WRITE,
    KEYVAL_REMOVE_KEY,
    KEYVAL_VALIDATE,
    KEYVAL_ITERATE,
    KEYVAL_ITERATE_KEYS,
    KEYVAL_READ_LIST,
    KEYVAL_WRITE_LIST,
    KEYVAL_FLUSH,
    KEYVAL_GET_HANDLE_INFO,
    DSPACE_CREATE = DSPACE_OP_TYPE,
    DSPACE_REMOVE,
    DSPACE_ITERATE_HANDLES,
    DSPACE_VERIFY,
    DSPACE_GETATTR,
    DSPACE_SETATTR,
    DSPACE_GETATTR_LIST,
    DSPACE_CREATE_LIST,
    DSPACE_REMOVE_LIST,
    /* NOTE: if you change or add items to this list, please update
     * s_dbpf_op_type_str_map[] accordingly (dbpf-mgmt.c)
     */
};

/* THese are the operations that do a sync */
#define DBPF_OP_DOES_SYNC(__op)    \
    (__op == KEYVAL_WRITE       || \
     __op == KEYVAL_REMOVE_KEY  || \
     __op == KEYVAL_WRITE_LIST  || \
     __op == DSPACE_CREATE      || \
     __op == DSPACE_CREATE_LIST || \
     __op == DSPACE_REMOVE      || \
     __op == DSPACE_SETATTR)

/*
 * a function useful for debugging that returns a human readable
 * op_type name given an op_type; returns NULL if no match is found
 */
char *dbpf_op_type_to_str(enum dbpf_op_type op_type);

/* This is a mirror of TROVE_ds_state in trove.h
 * These are the states of a dbpf op and are kept in
 * a "state" variable of type dbpf_op_state in a function
 * or struct (See the sruct dbpf_op below);
 * These are NOT return codes
 * WBLH
 */
enum dbpf_op_state
{
    OP_UNINITIALIZED = TROVE_OP_UNINITIALIZED,
    OP_NOT_QUEUED,
    OP_QUEUED,
    OP_IN_SERVICE,
    OP_COMPLETED,
    OP_DEQUEUED,
    OP_CANCELED,
    OP_INTERNALLY_DELAYED
};

/* These return codes are for normal function that return SUCCESS or
 * a PVFS_error
 */
#define DBPF_SUCCESS         TROVE_SUCCESS
#define DBPF_ERROR           TROVE_ERROR  

/* These return codes are for comparison functions
 */
#define DBPF_EQ              TROVE_EQ
#define DBPF_LT              TROVE_LT
#define DBPF_GT              TROVE_GT

/* These return codes should be used for functions that are dealing
 * with async operations - that might not be finished yet.  Always
 * use these defines in those functions and those that call them.
 * In case of an error return the error code if there is one or
 * DBPF_OP_ERROR for a general error.
 *
 * I think these are for functions that start an async op which
 * may be finished (COMPLETE) when the function returns, or may
 * leave the op running asynchonously (BUSY) and of course there
 * is ERROR.  Actual function return codes.
 */
#define DBPF_OP_BUSY         TROVE_OP_BUSY
#define DBPF_OP_COMPLETE     TROVE_OP_COMPLETE
#define DBPF_OP_ERROR        TROVE_OP_ERROR

/* Used to store parameters for queued operations */
struct dbpf_op
{
    enum dbpf_op_type type;
    enum dbpf_op_state state;
    TROVE_handle handle;
    TROVE_op_id id;
    struct dbpf_collection *coll_p;
    int (*svc_fn)(struct dbpf_op *op);
    void *user_ptr;
    TROVE_ds_flags flags;
    TROVE_context_id context_id;
    PVFS_hint  hints;
    union
    {
        /* d's: DSPACE b's: BSTREAM and k's: KEYVALS */
        /* all the op types go in here; structs are all
         * defined just below the prototypes for the functions.
         */
        struct dbpf_dspace_create_op d_create;
        struct dbpf_dspace_create_list_op d_create_list;
        struct dbpf_dspace_iterate_handles_op d_iterate_handles;
        struct dbpf_dspace_verify_op d_verify;
        struct dbpf_dspace_getattr_op d_getattr;
        struct dbpf_dspace_setattr_op d_setattr;
        struct dbpf_bstream_rw_list_op b_rw_list;
        struct dbpf_bstream_resize_op b_resize;
        struct dbpf_keyval_read_op k_read;
        struct dbpf_keyval_write_op k_write;
        struct dbpf_keyval_remove_op k_remove;
        struct dbpf_keyval_iterate_op k_iterate;
        struct dbpf_keyval_iterate_keys_op k_iterate_keys;
        struct dbpf_keyval_read_list_op k_read_list;
        struct dbpf_keyval_read_list_op k_write_list;
        struct dbpf_keyval_remove_list_op k_remove_list;
        struct dbpf_dspace_getattr_list_op d_getattr_list;
        struct dbpf_dspace_remove_list_op d_remove_list;
        struct dbpf_keyval_get_handle_info_op k_get_handle_info;
    } u;
};

/* collection registration functions implemented in dbpf-collection.c */
void dbpf_collection_register(struct dbpf_collection *coll_p);
struct dbpf_collection *dbpf_collection_find_registered(TROVE_coll_id coll_id);
void dbpf_collection_clear_registered(void);
void dbpf_collection_deregister(struct dbpf_collection *entry);

/* function for mapping db errors to trove errors */
PVFS_error dbpf_db_error_to_trove_error(int db_error_value);

/* Do these really make sense as macros?  Would they be better as
 * inline or normal functions?  WBLH
 */

#define DBPF_AIO_SYNC_IF_NECESSARY(dbpf_op_ptr, fd, ret)  \
do {                                                      \
    int tmp_ret, tmp_errno;                               \
    if (dbpf_op_ptr->flags & TROVE_SYNC)                  \
    {                                                     \
        if ((tmp_ret = fdatasync(fd)) != 0)               \
        {                                                 \
            tmp_errno = errno;                            \
            ret = -trove_errno_to_trove_error(tmp_errno); \
            gossip_err("aio [fd = %d] SYNC failed: %s\n", \
                       fd, strerror(tmp_errno));          \
        }                                                 \
        gossip_debug(                                     \
          GOSSIP_TROVE_DEBUG, "aio [fd = %d] SYNC called "\
          "servicing op type %s\n", fd,                   \
          dbpf_op_type_to_str(dbpf_op_ptr->type));        \
    }                                                     \
} while(0)

#define DBPF_ERROR_SYNC_IF_NECESSARY(dbpf_op_ptr, fd)    \
do {                                                     \
    int tmp_ret, tmp_errno;                              \
    if (dbpf_op_ptr->flags & TROVE_SYNC)                 \
    {                                                    \
        if ((tmp_ret = fdatasync(fd)) != 0)              \
        {                                                \
            tmp_errno = errno;                           \
            gossip_err("[fd = %d] SYNC failed: %s\n",    \
                       fd, strerror(tmp_errno));         \
            ret = -trove_errno_to_trove_error(tmp_errno);\
            goto return_error;                           \
        }                                                \
        gossip_debug(                                    \
          GOSSIP_TROVE_DEBUG,"[fd = %d] SYNC called "    \
          "servicing op type %s\n", fd,                  \
          dbpf_op_type_to_str(dbpf_op_ptr->type));       \
    }                                                    \
} while(0)

#define DBPF_DB_SYNC_IF_NECESSARY(dbpf_op_ptr, db_ptr, ret)   \
do {                                                          \
    int tmp_ret;                                              \
    ret = 0;                                                  \
    if (dbpf_op_ptr->flags & TROVE_SYNC)                      \
    {                                                         \
        if ((tmp_ret = dbpf_db_sync(db_ptr)) != 0)            \
        {                                                     \
            gossip_err("db SYNC failed: %s\n",                \
                       strerror(tmp_ret));                    \
            ret = -tmp_ret;       \
        }                                                     \
        gossip_debug(GOSSIP_TROVE_DEBUG,                      \
                     "db SYNC called servicing op type %s\n", \
                     dbpf_op_type_to_str(dbpf_op_ptr->type)); \
    }                                                         \
} while(0)

/* Wouldn't this make more sense with the other EVENT
 * functions/macros?  dbpf can have its own version too
 * but I think the logic should be kept together WBLH
 */

#define DBPF_EVENT_START(__coll_p,               \
                         __q_op_p,               \
                         __event_type,           \
                         __event_id,             \
                         args...)                \
    if(__coll_p->immediate_completion)           \
    {                                            \
        PINT_EVENT_START(__event_type,           \
                         dbpf_pid,               \
                         NULL,                   \
                         (__event_id),           \
                         ## args);               \
    }                                            \
    else                                         \
    {                                            \
        __q_op_p->event_type = __event_type;     \
        PINT_EVENT_START(__event_type,           \
                         dbpf_pid,               \
                         NULL,                   \
                         (__event_id),           \
                         ## args);               \
        *(__event_id) = __q_op_p->event_id;      \
    }

#define DBPF_EVENT_END(__event_type, __event_id) \
    PINT_EVENT_END(__event_type, dbpf_pid, NULL, __event_id)

extern struct dbpf_storage *my_storage_p;

/* kinda defeats the built in facility to count and
 * so on.  Why?  Need to look at this WBLH
 */
extern int64_t s_dbpf_metadata_writes, s_dbpf_metadata_reads;
#define UPDATE_PERF_METADATA_READ()            \
do {                                           \
    PINT_perf_count(PINT_server_pc,            \
                    PINT_PERF_METADATA_READ,   \
                    ++s_dbpf_metadata_reads,   \
                    PINT_PERF_SET);            \
} while(0)

#define UPDATE_PERF_METADATA_WRITE()           \
do {                                           \
    PINT_perf_count(PINT_server_pc,            \
                    PINT_PERF_METADATA_WRITE,  \
                    ++s_dbpf_metadata_writes,  \
                    PINT_PERF_SET);            \
} while(0)

int dbpf_dspace_setattr_op_svc(struct dbpf_op *op_p);

struct dbpf_storage *dbpf_storage_lookup(char *data_path,
                                         char *meta_path,
                                         char *config_path,
                                         int *error_p,
                                         TROVE_ds_flags flags);

int dbpf_storage_create(char *data_path,
			char *meta_path,
			char *config_path,
                        void *user_ptr,
                        TROVE_op_id *out_op_id_p);

int dbpf_storage_remove(char *data_path,
			char *meta_path,
			char *config_path,
                        void *user_ptr,
                        TROVE_op_id *out_op_id_p);

int dbpf_collection_create(char *collname,
                           TROVE_coll_id new_coll_id,
                           void *user_ptr,
                           TROVE_op_id *out_op_id_p);

int dbpf_collection_remove(char *collname,
                           void *user_ptr,
                           TROVE_op_id *out_op_id_p);

int dbpf_collection_lookup(char *collname,
                           TROVE_coll_id *out_coll_id_p,
                           void *user_ptr,
                           TROVE_op_id *out_op_id_p);

int dbpf_collection_clear(TROVE_coll_id coll_id);

int dbpf_collection_iterate(TROVE_keyval_s *name_array,
                            TROVE_coll_id *coll_id_array,
                            int *inout_count_p,
                            TROVE_ds_flags flags,
                            TROVE_vtag_s *vtag,
                            void *user_ptr,
                            TROVE_op_id *out_op_id_p);

int dbpf_collection_setinfo(TROVE_method_id method_id,
                            TROVE_coll_id coll_id,
                            TROVE_context_id context_id,
                            int option,
                            void *parameter);

int dbpf_collection_getinfo(TROVE_coll_id coll_id,
                            TROVE_context_id context_id,
                            TROVE_coll_getinfo_options opt,
                            void *parameter);

int dbpf_collection_seteattr(TROVE_coll_id coll_id,
                             TROVE_keyval_s *key_p,
                             TROVE_keyval_s *val_p,
                             TROVE_ds_flags flags,
                             void *user_ptr,
                             TROVE_context_id context_id,
                             TROVE_op_id *out_op_id_p);

int dbpf_collection_geteattr(TROVE_coll_id coll_id,
                             TROVE_keyval_s *key_p,
                             TROVE_keyval_s *val_p,
                             TROVE_ds_flags flags,
                             void *user_ptr,
                             TROVE_context_id context_id,
                             TROVE_op_id *out_op_id_p);

int dbpf_collection_deleattr(TROVE_coll_id coll_id,
                             TROVE_keyval_s *key_p,
                             TROVE_ds_flags flags,
                             void *user_ptr,
                             TROVE_context_id context_id,
                             TROVE_op_id *out_op_id_p);

int dbpf_finalize(void);

int dbpf_bstream_read_at(TROVE_coll_id coll_id,
                         TROVE_handle handle,
                         void *buffer,
                         TROVE_size *inout_size_p,
                         TROVE_offset offset,
                         TROVE_ds_flags flags,
                         TROVE_vtag_s *vtag,
                         void *user_ptr,
                         TROVE_context_id context_id,
                         TROVE_op_id *out_op_id_p,
                         PVFS_hint  hints);

int dbpf_bstream_write_at(TROVE_coll_id coll_id,
                          TROVE_handle handle,
                          void *buffer,
                          TROVE_size *inout_size_p,
                          TROVE_offset offset,
                          TROVE_ds_flags flags,
                          TROVE_vtag_s *vtag,
                          void *user_ptr,
                          TROVE_context_id context_id,
                          TROVE_op_id *out_op_id_p,
                          PVFS_hint  hints);

int dbpf_bstream_flush(TROVE_coll_id coll_id,
                       TROVE_handle handle,
                       TROVE_ds_flags flags,
                       void *user_ptr,
                       TROVE_context_id context_id,
                       TROVE_op_id *out_op_id_p,
                       PVFS_hint hints);

int dbpf_bstream_resize(TROVE_coll_id coll_id,
                        TROVE_handle handle,
                        TROVE_size *inout_size_p,
                        TROVE_ds_flags flags,
                        TROVE_vtag_s *vtag,
                        void *user_ptr,
                        TROVE_context_id context_id,
                        TROVE_op_id *out_op_id_p,
                        PVFS_hint  hints);

int dbpf_bstream_validate(TROVE_coll_id coll_id,
                          TROVE_handle handle,
                          TROVE_ds_flags flags,
                          TROVE_vtag_s *vtag,
                          void *user_ptr,
                          TROVE_context_id context_id,
                          TROVE_op_id *out_op_id_p,
                          PVFS_hint  hints);

#if defined(__cplusplus)
}
#endif

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
