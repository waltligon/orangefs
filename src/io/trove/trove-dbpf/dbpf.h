/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __DBPF_H__
#define __DBPF_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <db.h>
#include <aio.h>
#include "trove.h"
#include "gen-locks.h"
#include "pvfs2-internal.h"
#include "dbpf-keyval-pcache.h"
#include "dbpf-open-cache.h"
#include "pint-event.h"

/* For unknown Berkeley DB errors, we return some large value
 */
#define DBPF_ERROR_UNKNOWN 4243

/* Incremental versions are backward compatible with previous releases
 * that have the same major and minor versions.
 * Minor versions are NOT backward compatible.
 * Major versions aren't either, but refer to architectural storage format changes.
 */
#define TROVE_DBPF_VERSION_KEY                       "trove-dbpf-version"
#define TROVE_DBPF_VERSION_VALUE                                  "0.1.4"

#define LAST_HANDLE_STRING                                  "last_handle"

#ifdef HAVE_DB_DIRTY_READ
#define TROVE_DB_DIRTY_READ DB_DIRTY_READ
#else
#define TROVE_DB_DIRTY_READ             0
#endif /* HAVE_DB_DIRTY_READ */

#ifdef __PVFS2_TROVE_THREADED__
#define TROVE_DB_THREAD DB_THREAD
#else
#define TROVE_DB_THREAD         0
#endif /* __PVFS2_TROVE_THREADED__ */

#define TROVE_DB_MODE                                                 0600
#define TROVE_DB_TYPE                                             DB_BTREE
#define TROVE_DB_OPEN_FLAGS        (TROVE_DB_DIRTY_READ | TROVE_DB_THREAD)
#define TROVE_DB_CREATE_FLAGS            (DB_CREATE | TROVE_DB_OPEN_FLAGS)

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
((__handle) % DBPF_BSTREAM_MAX_NUM_BUCKETS)

#define DBPF_GET_STORAGE_DIRNAME(__buf, __path_max, __stoname)          \
do { snprintf(__buf, __path_max, "/%s", __stoname); } while (0)

#define STO_ATTRIB_DBNAME "storage_attributes.db"
#define DBPF_GET_STO_ATTRIB_DBNAME(__buf, __path_max, __stoname)         \
do {                                                                     \
  snprintf(__buf, __path_max, "/%s/%s", __stoname, STO_ATTRIB_DBNAME);   \
} while (0)

#define COLLECTIONS_DBNAME "collections.db"
#define DBPF_GET_COLLECTIONS_DBNAME(__buf, __path_max, __stoname)        \
do {                                                                     \
  snprintf(__buf, __path_max, "/%s/%s", __stoname, COLLECTIONS_DBNAME);  \
} while (0)

#define DBPF_GET_COLL_DIRNAME(__buf, __path_max, __stoname, __collid)    \
do {                                                                     \
  snprintf(__buf, __path_max, "/%s/%08x", __stoname, __collid);          \
} while (0)

#define COLL_ATTRIB_DBNAME "collection_attributes.db"
#define DBPF_GET_COLL_ATTRIB_DBNAME(__buf,__path_max,__stoname,__collid) \
do {                                                                     \
  snprintf(__buf, __path_max, "/%s/%08x/%s", __stoname, __collid,        \
           COLL_ATTRIB_DBNAME);                                          \
} while (0)

#define DS_ATTRIB_DBNAME "dataspace_attributes.db"
#define DBPF_GET_DS_ATTRIB_DBNAME(__buf,__path_max,__stoname,__collid)   \
do {                                                                     \
  snprintf(__buf, __path_max, "/%s/%08x/%s", __stoname, __collid,        \
           DS_ATTRIB_DBNAME);                                            \
} while (0)

#define BSTREAM_DIRNAME "bstreams"
#define DBPF_GET_BSTREAM_DIRNAME(__buf, __path_max, __stoname, __collid) \
do {                                                                     \
  snprintf(__buf, __path_max, "/%s/%08x/%s", __stoname, __collid,        \
           BSTREAM_DIRNAME);                                             \
} while (0)

#define STRANDED_BSTREAM_DIRNAME "stranded-bstreams"
#define DBPF_GET_STRANDED_BSTREAM_DIRNAME(                       \
        __buf, __path_max, __stoname, __collid)                  \
    do {                                                         \
        snprintf(__buf, __path_max, "/%s/%08x/%s",               \
                 __stoname, __collid, STRANDED_BSTREAM_DIRNAME); \
    } while(0)

/* arguments are: buf, path_max, stoname, collid, handle */
#define DBPF_GET_BSTREAM_FILENAME(__b, __pm, __stoname, __cid, __handle)  \
do {                                                                      \
  snprintf(__b, __pm, "/%s/%08x/%s/%.8llu/%08llx.bstream",                \
           __stoname, __cid, BSTREAM_DIRNAME,                             \
           llu(DBPF_BSTREAM_GET_BUCKET(__handle)), llu(__handle));        \
} while (0)

/* arguments are: buf, path_max, stoname, collid, handle */
#define DBPF_GET_STRANDED_BSTREAM_FILENAME(                  \
        __b, __pm, __stoname, __cid, __handle)               \
    do {                                                     \
        snprintf(__b, __pm, "/%s/%08x/%s/%08llx.bstream",    \
                 __stoname, __cid, STRANDED_BSTREAM_DIRNAME, \
                 llu(__handle));                             \
    } while(0)

/* arguments are: buf, path_max, stoname, collid */
#define KEYVAL_DBNAME "keyval.db"
#define DBPF_GET_KEYVAL_DBNAME(__buf,__path_max,__stoname,__collid)      \
do {                                                                     \
  snprintf(__buf, __path_max, "/%s/%08x/%s", __stoname, __collid,        \
           KEYVAL_DBNAME);                                               \
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
    int (* aio_read) (struct aiocb * aiocbp);
    int (* aio_write) (struct aiocb * aiocbp);
    int (* lio_listio) (int mode, struct aiocb * const list[], int nent,
                        struct sigevent *sig);
    int (* aio_error) (const struct aiocb *aiocbp);
    ssize_t (* aio_return) (struct aiocb *aiocbp);
    int (* aio_cancel) (int filedesc, struct aiocb * aiocbp);
    int (* aio_suspend) (const struct aiocb * const list[], int nent,
                         const struct timespec * timeout);
    int (*aio_fsync) (int operation, struct aiocb * aiocbp);
};

typedef int (* PINT_dbpf_keyval_iterate_callback)(
    void *, TROVE_handle handle, TROVE_keyval_s *key, TROVE_keyval_s *val);

int PINT_dbpf_keyval_iterate(
    DB *db_p,
    TROVE_handle handle,
    PINT_dbpf_keyval_pcache *pcache,    
    TROVE_keyval_s *keys_array,
    TROVE_keyval_s *values_array,
    int *count,
    TROVE_ds_position pos,
    PINT_dbpf_keyval_iterate_callback callback);

int PINT_dbpf_dspace_remove_keyval(
    void * args, TROVE_handle handle, TROVE_keyval_s *key, TROVE_keyval_s *val);

struct dbpf_storage
{
    TROVE_ds_flags flags;
    int refct;
    char *name;
    DB *sto_attr_db;
    DB *coll_db;
};

struct dbpf_collection
{
    int refct;
    char *name;
    char *path_name;
    DB *coll_attr_db;
    DB *ds_db;
    DB *keyval_db;
    DB_ENV *coll_env;
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

int PINT_trove_dbpf_keyval_compare(
    DB * dbp, const DBT * a, const DBT * b);
int PINT_trove_dbpf_ds_attr_compare(
    DB * dbp, const DBT * a, const DBT * b);
int PINT_trove_dbpf_ds_attr_compare_reversed(
    DB * dbp, const DBT * a, const DBT * b);

int dbpf_dspace_attr_get(struct dbpf_collection *coll_p,
                         TROVE_object_ref ref,
                         TROVE_ds_attributes *attr);

int dbpf_dspace_attr_set(struct dbpf_collection *coll_p,
                         TROVE_object_ref ref,
                         TROVE_ds_attributes *attr);

struct dbpf_dspace_create_op
{
    TROVE_handle_extent_array extent_array;
    TROVE_handle *out_handle_p;
    TROVE_ds_type type;
    /* hint? */
};

struct dbpf_dspace_create_list_op
{
    TROVE_handle_extent_array extent_array;
    TROVE_handle *out_handle_array_p;
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
    int fd, list_proc_state, opcode;
    int aiocb_array_count, mem_array_count, stream_array_count;
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

#define DBPF_OP_DOES_SYNC(__op)    \
    (__op == KEYVAL_WRITE       || \
     __op == KEYVAL_REMOVE_KEY  || \
     __op == KEYVAL_WRITE_LIST  || \
     __op == DSPACE_CREATE      || \
     __op == DSPACE_CREATE_LIST || \
     __op == DSPACE_REMOVE      || \
     __op == DSPACE_SETATTR)

/*
  a function useful for debugging that returns a human readable
  op_type name given an op_type; returns NULL if no match is found
*/
char *dbpf_op_type_to_str(enum dbpf_op_type op_type);

enum dbpf_op_state
{
    OP_UNITIALIZED = 0,
    OP_NOT_QUEUED,
    OP_QUEUED,
    OP_IN_SERVICE,
    OP_COMPLETED,
    OP_DEQUEUED,
    OP_CANCELED,
    OP_INTERNALLY_DELAYED
};

#define DBPF_OP_CONTINUE 0
#define DBPF_OP_COMPLETE 1

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
void dbpf_collection_register(
    struct dbpf_collection *coll_p);
struct dbpf_collection *dbpf_collection_find_registered(
    TROVE_coll_id coll_id);
void dbpf_collection_clear_registered(void);
void dbpf_collection_deregister(struct dbpf_collection *entry);

/* function for mapping db errors to trove errors */
PVFS_error dbpf_db_error_to_trove_error(int db_error_value);

#define DBPF_OPEN   open
#define DBPF_WRITE  write
#define DBPF_LSEEK  lseek
#define DBPF_READ   read
#define DBPF_CLOSE  close
#define DBPF_UNLINK unlink
#define DBPF_SYNC   fdatasync
#define DBPF_RESIZE ftruncate
#define DBPF_FSTAT  fstat
#define DBPF_ACCESS access
#define DBPF_FCNTL  fcntl

#define DBPF_AIO_SYNC_IF_NECESSARY(dbpf_op_ptr, fd, ret)  \
do {                                                      \
    int tmp_ret, tmp_errno;                               \
    if (dbpf_op_ptr->flags & TROVE_SYNC)                  \
    {                                                     \
        if ((tmp_ret = DBPF_SYNC(fd)) != 0)               \
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
        if ((tmp_ret = DBPF_SYNC(fd)) != 0)              \
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
        if ((tmp_ret = db_ptr->sync(db_ptr, 0)) != 0)         \
        {                                                     \
            gossip_err("db SYNC failed: %s\n",                \
                       db_strerror(tmp_ret));                 \
            ret = -dbpf_db_error_to_trove_error(tmp_ret);     \
        }                                                     \
        gossip_debug(                                         \
          GOSSIP_TROVE_DEBUG, "db SYNC called "               \
          "servicing op type %s\n",                           \
          dbpf_op_type_to_str(dbpf_op_ptr->type));            \
    }                                                         \
} while(0)

#define DBPF_EVENT_START(__coll_p, __q_op_p, __event_type, __event_id, args...) \
    if(__coll_p->immediate_completion)                                          \
    {                                                                           \
        PINT_EVENT_START(__event_type, dbpf_pid, NULL, (__event_id),            \
                         ## args);                                              \
    }                                                                           \
    else                                                                        \
    {                                                                           \
        __q_op_p->event_type = __event_type;                                    \
        PINT_EVENT_START(__event_type, dbpf_pid, NULL, (__event_id),            \
                         ## args);                                              \
        *(__event_id) = __q_op_p->event_id;                                     \
    }

#define DBPF_EVENT_END(__event_type, __event_id) \
    PINT_EVENT_END(__event_type, dbpf_pid, NULL, __event_id)

extern struct dbpf_storage *my_storage_p;

extern int64_t s_dbpf_metadata_writes, s_dbpf_metadata_reads;
#define UPDATE_PERF_METADATA_READ()                         \
do {                                                        \
    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_READ,\
                    ++s_dbpf_metadata_reads, PINT_PERF_SET);\
} while(0)

#define UPDATE_PERF_METADATA_WRITE()                         \
do {                                                         \
    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_WRITE,\
                    ++s_dbpf_metadata_writes, PINT_PERF_SET);\
} while(0)

extern DB_ENV *dbpf_getdb_env(const char *path, unsigned int env_flags, int *err_p);
extern int dbpf_putdb_env(DB_ENV *dbenv, const char *path);
extern int db_open(DB *db_p, const char *dbname, int, int);
extern int db_close(DB *db_p);

int dbpf_dspace_setattr_op_svc(struct dbpf_op *op_p);

struct dbpf_storage *dbpf_storage_lookup(
    char *stoname, int *error_p, TROVE_ds_flags flags);

int dbpf_storage_create(char *stoname,
                        void *user_ptr,
                        TROVE_op_id *out_op_id_p);

int dbpf_storage_remove(char *stoname,
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

int dbpf_collection_iterate(TROVE_ds_position *inout_position_p,
                            TROVE_keyval_s *name_array,
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
