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

#define TROVE_DBPF_VERSION_KEY                       "trove-dbpf-version"
#define TROVE_DBPF_VERSION_VALUE                                  "0.0.1"
#define LAST_HANDLE_STRING                                  "last_handle"
#define ROOT_HANDLE_STRING                                  "root_handle"

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

#define TROVE_DB_MODE                                                 0644
#define TROVE_DB_TYPE                                             DB_BTREE
#define TROVE_DB_OPEN_FLAGS        (TROVE_DB_DIRTY_READ | TROVE_DB_THREAD)
#define TROVE_DB_CREATE_FLAGS  (DB_CREATE | DB_EXCL | TROVE_DB_OPEN_FLAGS)

/*
  for more efficient host filesystem accesses, we have
  a simple *_MAX_NUM_BUCKETS define that can be thought of more
  or less as buckets for storing bstreams and keyvals based
  on a simple hash of the unique handle/coll_id combination.

  in practice, this means we spread all bstreams and keyvals
  into *_MAX_NUM_BUCKETS directories instead of keeping all
  bstream entries in a flat bstream directory, and all keyval
  entries in a flat keyval directory on the host filesystem.
*/
#define DBPF_KEYVAL_MAX_NUM_BUCKETS   32
#define DBPF_BSTREAM_MAX_NUM_BUCKETS  64

#define DBPF_KEYVAL_GET_BUCKET(__handle, __id)                           \
(((__id << ((sizeof(__id) - 1) * 8)) | __handle) %                       \
 DBPF_KEYVAL_MAX_NUM_BUCKETS)

#define DBPF_BSTREAM_GET_BUCKET(__handle, __id)                          \
(((__id << ((sizeof(__id) - 1) * 8)) | __handle) %                       \
 DBPF_BSTREAM_MAX_NUM_BUCKETS)

#define DBPF_EVENT_START(__op, __id)                                     \
 PINT_event_timestamp(PVFS_EVENT_API_TROVE, __op, 0, __id,               \
 PVFS_EVENT_FLAG_START)

#define DBPF_EVENT_END(__op, __id)                                       \
 PINT_event_timestamp(PVFS_EVENT_API_TROVE, __op, 0, __id,               \
 PVFS_EVENT_FLAG_END)

#define DBPF_GET_STORAGE_DIRNAME(__buf, __path_max, __stoname)	         \
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

#define KEYVAL_DIRNAME "keyvals"
#define DBPF_GET_KEYVAL_DIRNAME(__buf, __path_max, __stoname, __collid)  \
do {                                                                     \
  snprintf(__buf, __path_max, "/%s/%08x/%s", __stoname, __collid,        \
           KEYVAL_DIRNAME);                                              \
} while (0)

#define BSTREAM_DIRNAME "bstreams"
#define DBPF_GET_BSTREAM_DIRNAME(__buf, __path_max, __stoname, __collid) \
do {                                                                     \
  snprintf(__buf, __path_max, "/%s/%08x/%s", __stoname, __collid,        \
           BSTREAM_DIRNAME);                                             \
} while (0)

/* arguments are: buf, path_max, stoname, collid, handle */
#define DBPF_GET_BSTREAM_FILENAME(__b, __pm, __stoname, __cid, __handle) \
do {                                                                     \
  snprintf(__b, __pm, "/%s/%08x/%s/%.8Lu/%08Lx.bstream",                 \
           __stoname, __cid, BSTREAM_DIRNAME,                            \
           DBPF_BSTREAM_GET_BUCKET(__handle, __cid), Lu(__handle));      \
} while (0)

/* arguments are: buf, path_max, stoname, collid, handle */
#define DBPF_GET_KEYVAL_DBNAME(__b, __pm, __stoname, __cid, __handle)    \
do {                                                                     \
  snprintf(__b, __pm, "/%s/%08x/%s/%.8Lu/%08Lx.keyval", __stoname,       \
           __cid, KEYVAL_DIRNAME,                                        \
           DBPF_KEYVAL_GET_BUCKET(__handle, __cid), Lu(__handle));       \
} while (0)

extern struct TROVE_bstream_ops dbpf_bstream_ops;
extern struct TROVE_dspace_ops dbpf_dspace_ops;
extern struct TROVE_keyval_ops dbpf_keyval_ops;
extern struct TROVE_mgmt_ops dbpf_mgmt_ops;

struct dbpf_storage
{
    int refct;
    char *name;
    DB *sto_attr_db;
    DB *coll_db;
};

struct dbpf_collection
{
    int refct;
    char *name;
    DB *coll_attr_db;
    DB *ds_db;
    TROVE_coll_id coll_id;
    TROVE_handle root_dir_handle;
    struct dbpf_storage *storage;
    struct handle_ledger *free_handles;

    /* used by dbpf_collection.c calls to maintain list of collections */
    struct dbpf_collection *next_p;
};

/* Structure stored as data in collections database with collection
 * directory name as key.
 */
struct dbpf_collection_db_entry
{
    TROVE_coll_id coll_id;
};

struct dbpf_dspace_create_op
{
    TROVE_handle_extent_array extent_array;
    TROVE_handle *out_handle_p;
    TROVE_ds_type type;
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

struct dbpf_keyval_read_op
{
    TROVE_keyval_s key;
    TROVE_keyval_s val;
    /* vtag? */
};

struct dbpf_keyval_read_list_op
{
    TROVE_keyval_s *key_array;
    TROVE_keyval_s *val_array;
    int count; /* TODO: MAKE INOUT? */
};

struct dbpf_keyval_write_op
{
    TROVE_keyval_s key;
    TROVE_keyval_s val;
    /* vtag? */
};

struct dbpf_keyval_remove_op
{
    TROVE_keyval_s key;
    TROVE_keyval_s val;
    /* vtag? */
};

struct dbpf_keyval_iterate_op
{
    TROVE_keyval_s *key_array;
    TROVE_keyval_s *val_array;
    TROVE_ds_position *position_p;
    int *count_p;
    /* vtag? */
};

/* used for both read and write at */
struct dbpf_bstream_rw_at_op
{
    TROVE_offset offset;
    TROVE_size size;
    void *buffer;
    /* vtag? */
};

struct dbpf_bstream_resize_op
{
    TROVE_size size;
    /* vtag? */
};

/* Used to maintain state of partial processing of a listio operation
 */
struct bstream_listio_state
{
    int mem_ct, stream_ct, cur_mem_size;
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
 * list_proc_state is used to retain the status of processing on the list
 * arrays.
 *
 * aiocb_array_count - size of the aiocb_array (nothing to do
 * with # of things in progress)
 */
struct dbpf_bstream_rw_list_op
{
    int fd, list_proc_state, opcode;
    int aiocb_array_count, mem_array_count, stream_array_count;
    char **mem_offset_array;
    TROVE_size *mem_size_array;
    TROVE_offset *stream_offset_array;
    TROVE_size *stream_size_array;
    struct aiocb *aiocb_array;
    struct sigevent sigev;
    struct bstream_listio_state lio_state;
};

/* List of operation types that might be queued */
enum dbpf_op_type
{
    BSTREAM_READ_AT = 1,
    BSTREAM_WRITE_AT,
    BSTREAM_RESIZE,
    BSTREAM_READ_LIST,
    BSTREAM_WRITE_LIST,
    BSTREAM_VALIDATE,
    BSTREAM_FLUSH,
    KEYVAL_READ,
    KEYVAL_WRITE,
    KEYVAL_REMOVE_KEY,
    KEYVAL_VALIDATE,
    KEYVAL_ITERATE,
    KEYVAL_ITERATE_KEYS,
    KEYVAL_READ_LIST,
    KEYVAL_WRITE_LIST,
    KEYVAL_FLUSH,
    DSPACE_CREATE,
    DSPACE_REMOVE,
    DSPACE_ITERATE_HANDLES,
    DSPACE_VERIFY,
    DSPACE_GETATTR,
    DSPACE_SETATTR
};

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
    OP_CANCELED
};

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
    union
    {
	/* all the op types go in here; structs are all
	 * defined just below the prototypes for the functions.
	 */
	struct dbpf_dspace_create_op          d_create;
	/* struct dbpf_dspace_remove_op d_remove; -- EMPTY */
	struct dbpf_dspace_iterate_handles_op d_iterate_handles;
	struct dbpf_dspace_verify_op          d_verify;
	struct dbpf_dspace_getattr_op         d_getattr;
	struct dbpf_dspace_setattr_op         d_setattr;
	struct dbpf_bstream_rw_at_op          b_read_at;
	struct dbpf_bstream_rw_at_op          b_write_at;
	struct dbpf_bstream_rw_list_op        b_rw_list;
	struct dbpf_bstream_resize_op         b_resize;
	struct dbpf_keyval_read_op            k_read;
	struct dbpf_keyval_write_op           k_write;
	struct dbpf_keyval_remove_op          k_remove;
	struct dbpf_keyval_iterate_op         k_iterate;
	struct dbpf_keyval_read_list_op       k_read_list;
    } u;
};

/* collection registration functions implemented in dbpf-collection.c */
void dbpf_collection_register(struct dbpf_collection *coll_p);
struct dbpf_collection *dbpf_collection_find_registered(TROVE_coll_id coll_id);
void dbpf_collection_clear_registered(void);

/* function for mapping db errors to trove errors */
PVFS_error dbpf_db_error_to_trove_error(int db_error_value);

/* db error reporting callback function; defined in dbpf-mgmt.c */
void dbpf_error_report(const char *errpfx, char *msg);

#define DBPF_OPEN   open
#define DBPF_WRITE  write
#define DBPF_LSEEK  lseek
#define DBPF_READ   read
#define DBPF_CLOSE  close
#define DBPF_UNLINK unlink
#define DBPF_SYNC   fsync
#define DBPF_RESIZE ftruncate

#define DBPF_DB_SYNC_IF_NECESSARY(dbpf_op_ptr, db_ptr) \
do {                                                   \
    if (dbpf_op_ptr->flags & TROVE_SYNC)               \
    {                                                  \
	if ((ret = db_ptr->sync(db_ptr, 0)) != 0)      \
        {                                              \
            gossip_err("db_p->sync failed: %s\n",      \
                       db_strerror(ret));              \
	    error = -dbpf_db_error_to_trove_error(ret);\
	    goto return_error;                         \
	}                                              \
        gossip_debug(                                  \
          GOSSIP_TROVE_DEBUG,"db_p->sync called "      \
          "servicing op type %s\n",                    \
          dbpf_op_type_to_str(dbpf_op_ptr->type));     \
    }                                                  \
} while(0)


extern struct dbpf_storage *my_storage_p;

extern int64_t s_dbpf_metadata_writes, s_dbpf_metadata_reads;
#define UPDATE_PERF_METADATA_READ()                         \
do {                                                        \
    PINT_perf_count(PINT_PERF_METADATA_READ,                \
                    ++s_dbpf_metadata_reads, PINT_PERF_SET);\
} while(0)

#define UPDATE_PERF_METADATA_WRITE()                         \
do {                                                         \
    PINT_perf_count(PINT_PERF_METADATA_WRITE,                \
                    ++s_dbpf_metadata_writes, PINT_PERF_SET);\
} while(0)

#if defined(__cplusplus)
}
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif
