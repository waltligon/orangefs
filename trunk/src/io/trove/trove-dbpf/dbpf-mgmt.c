/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* DB plus files (dbpf) implementation of storage interface.
 *
 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <db.h>
#include <time.h>
#include <stdlib.h>
#include <glob.h>
#include "trove.h"
#include "pint-context.h"
#include "pint-mgmt.h"

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#include <errno.h>
#include <limits.h>
#include <dirent.h>

#include "trove.h"
#include "trove-internal.h"
#include "dbpf.h"
#include "dbpf-op-queue.h"
#include "dbpf-bstream.h"
#include "dbpf-thread.h"
#include "dbpf-attr-cache.h"
#include "trove-ledger.h"
#include "trove-handle-mgmt.h"
#include "gossip.h"
#include "dbpf-open-cache.h"
#include "pint-util.h"
#include "dbpf-sync.h"

PINT_event_group trove_dbpf_event_group;

PINT_event_type trove_dbpf_read_event_id;
PINT_event_type trove_dbpf_write_event_id;
PINT_event_type trove_dbpf_keyval_write_event_id;
PINT_event_type trove_dbpf_keyval_read_event_id;
PINT_event_type trove_dbpf_dspace_create_event_id;
PINT_event_type trove_dbpf_dspace_create_list_event_id;
PINT_event_type trove_dbpf_dspace_getattr_event_id;
PINT_event_type trove_dbpf_dspace_setattr_event_id;

int dbpf_pid;

PINT_manager_t io_thread_mgr;
PINT_worker_id io_worker_id;
PINT_queue_id io_queue_id;
PINT_context_id io_ctx;
static int directio_threads_started = 0;

extern gen_mutex_t dbpf_attr_cache_mutex;

extern int TROVE_db_cache_size_bytes;
extern int TROVE_shm_key_hint;

struct dbpf_storage *my_storage_p = NULL;
static int db_open_count, db_close_count;
static void unlink_db_cache_files(const char* path);
static int start_directio_threads(void);
static int stop_directio_threads(void);

static int trove_directio_threads_num = 30;
static int trove_directio_ops_per_queue = 10;
static int trove_directio_timeout = 1000;

static int PINT_dbpf_io_completion_callback(PINT_context_id ctx_id,
                                     int count,
                                     PINT_op_id *op_ids,
                                     void **user_ptrs,
                                     PVFS_error *errors);

#define COLL_ENV_FLAGS (DB_INIT_MPOOL | DB_CREATE | DB_THREAD)

static void dbpf_db_error_callback(
#ifdef HAVE_DBENV_PARAMETER_TO_DB_ERROR_CALLBACK
    const DB_ENV *dbenv, 
#endif
    const char *errpfx, 
#ifdef HAVE_CONST_THIRD_PARAMETER_TO_DB_ERROR_CALLBACK
    const 
#endif
    char * msg);

DB_ENV *dbpf_getdb_env(const char *path, unsigned int env_flags, int *error)
{
    int ret;
    DB_ENV *dbenv = NULL;

    *error = 0;

    if (path == NULL)
    {
        *error = -EINVAL;
        return NULL;
    }

    /* we start by making sure any old environment remnants are cleaned up */
    if(my_storage_p->flags & TROVE_DB_CACHE_MMAP)
    {
        /* mmap case: use env->remove function */
        ret = db_env_create(&dbenv, 0);
        if (ret != 0) 
        {
            gossip_err("dbpf_env_create: could not create "
                       "any environment handle: %s\n", 
                       db_strerror(ret));
            *error = ret;
            return NULL;
        }

        /* don't check return code here; we don't care if it fails */
        dbenv->remove(dbenv, path, DB_FORCE);
    }
    else
    {
        /* shm case */
        /* destroy any old __db.??? files to make sure we don't accidentially 
         * reuse a shmid and collide with a server process that is already
         * running on this node.  We don't use env->remove because it could
         * interfere with shm regions already allocated by another server 
         * process
         */
        unlink_db_cache_files(path);
    }

retry:
    ret = db_env_create(&dbenv, 0);
    if (ret != 0 || dbenv == NULL)
    {
        gossip_err("dbpf_getdb_env: %s\n", db_strerror(ret));
        *error = ret;
        return NULL;
    }

    /* set the error callback for all databases opened with this environment */
    dbenv->set_errcall(dbenv, dbpf_db_error_callback);

    if(TROVE_db_cache_size_bytes != 0)
    {
        gossip_debug(
            GOSSIP_TROVE_DEBUG, "dbpf using cache size of %d bytes.\n",
            TROVE_db_cache_size_bytes);
        ret = dbenv->set_cachesize(dbenv, 0, TROVE_db_cache_size_bytes, 1);
        if(ret != 0)
        {
            gossip_err("Error: failed to set db cache size: %s\n",
                       db_strerror(ret));
            *error = ret;
            return NULL;
        }
    }
    else
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "dbpf using default db cache size.\n");
    }

    if(my_storage_p && (my_storage_p->flags & TROVE_DB_CACHE_MMAP))
    {
        /* user wants the standard db cache which uses mmap */
        ret = dbenv->open(dbenv, path, 
                          DB_INIT_MPOOL|
                          DB_CREATE|
                          DB_THREAD, 
                          0);
        if(ret != 0)
        {
            gossip_err("dbpf_getdb_env(%s): %s\n", path, db_strerror(ret));
            *error = ret;
            return NULL;
        }
    }
    else
    {
        /* default to using shm style cache */


        gossip_debug(GOSSIP_TROVE_DEBUG, "dbpf using shm key: %d\n",
                     (646567223+TROVE_shm_key_hint));
        ret = dbenv->set_shm_key(dbenv, (646567223+TROVE_shm_key_hint));
        if(ret != 0)
        {
            gossip_err("dbenv->set_shm_key(%s): %s\n",
                       path, db_strerror(ret));
            *error = ret;
            return NULL;
        }

        ret = dbenv->open(dbenv, path, 
                          DB_INIT_MPOOL|
                          DB_CREATE|
                          DB_THREAD|
                          DB_SYSTEM_MEM, 
                          0);
        /* In some cases (oddly configured systems with pvfs2-server running as
         * non-root) DB_SYSTEM_MEM, which uses sysV shared memory, can fail
         * with EAGAIN (resource temporarily unavailable).   berkely DB can use
         * an mmapped file instead of sysv shm, so we will fall back to that
         * (by setting TROVE_DB_CACHE_MMAP and retrying) in
         * the EAGAIN case.  The drawback is a file (__db.002) that takes up
         * space in your storage directory.  We need to go through the whole
         * dbenv setup process again (instead of just calling dbenv->open
         * immediately) because after dbenv->open returns an error, even
         * EAGAIN, berkely db requires that you close and re-create the dbenv
         * before using it again */

        if (ret == EAGAIN) {
            unlink_db_cache_files(path);
            assert(my_storage_p != NULL);
            my_storage_p->flags |= TROVE_DB_CACHE_MMAP;
            goto retry;
        }

        /* berkeley db is sometimes configured without shared memory.  If
         * open returns an EINVAL, retry with DB_PRIVATE.
         */
        if(ret == EINVAL) {
            unlink_db_cache_files(path);
            ret = dbenv->open(dbenv, path, 
                              DB_CREATE|
                              DB_THREAD|
                              DB_PRIVATE, 
                              0);
        }

        if(ret == DB_RUNRECOVERY)
        {
            gossip_err("dbpf_getdb_env(): DB_RUNRECOVERY on environment open.\n");
            gossip_err(
                "\n\n"
                "    Please make sure that you have not chosen a DBCacheSizeBytes\n"
                "    configuration file value that is too large for your machine.\n\n"); 
        }

        if(ret != 0)
        {
            gossip_err("dbpf_getdb_env(%s): %s\n", path, db_strerror(ret));
            *error = ret;
            return NULL;
        }
    }

    return dbenv;
}

int dbpf_putdb_env(DB_ENV *dbenv, const char *path)
{
    int ret;

    if (dbenv == NULL)
    {
        return 0;
    }
    ret = dbenv->close(dbenv, 0);
    if (ret != 0) 
    {
        gossip_err("dbpf_putdb_env: %s\n", db_strerror(ret));
        return -dbpf_db_error_to_trove_error(ret);
    }

    /* Remove any db env backing log etc. 
     * Sadly we cannot make use of the same dbenv for removing stuff */
    ret = db_env_create(&dbenv, 0);
    if (ret != 0) 
    {
        gossip_err("dbpf_putdb_env: could not create "
                   "any environment handle: %s\n", 
                   db_strerror(ret));
        return 0;
    }
    ret = dbenv->remove(dbenv, path, DB_FORCE);
    if (ret != 0) 
    {
        gossip_err("dbpf_putdb_env: could not remove "
                   "environment handle: %s\n", 
                   db_strerror(ret));
    }
    return 0;
}

static int dbpf_db_create(char *dbname, 
                          DB_ENV *envp, 
                          uint32_t flags);
static DB *dbpf_db_open(
    char *dbname, DB_ENV *envp, int *err_p,
    int (*compare_fn) (DB *db, const DBT *dbt1, const DBT *dbt2), uint32_t flags);
static int dbpf_mkpath(char *pathname, mode_t mode);


int dbpf_collection_getinfo(TROVE_coll_id coll_id,
                            TROVE_context_id context_id,
                            TROVE_coll_getinfo_options opt,
                            void *parameter)
{
    struct dbpf_collection *coll_p = NULL;
    struct dbpf_storage *sto_p = NULL;
    int ret = -TROVE_EINVAL;

    sto_p = my_storage_p;
    if (sto_p == NULL)
    {
        return ret;
    }
    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return ret;
    }

    switch(opt)
    {
        case PVFS_COLLECTION_STATFS:
            {
                char path_name[PATH_MAX] = {0};
                PINT_statfs_t tmp_statfs;
                TROVE_statfs *tmp_trove_statfs = (TROVE_statfs *)parameter;

                /* XXX: this is not entirely accurate when data and metadata
		 		* are stored on different devices.
		 		*/
                DBPF_GET_DATA_DIRNAME(path_name, PATH_MAX, sto_p->data_path);
                ret = PINT_statfs_lookup(path_name, &tmp_statfs);
                if (ret < 0)
                {
                    ret = -trove_errno_to_trove_error(errno);
                    return ret;
                }
                tmp_trove_statfs->fs_id = coll_id;

                /*
NOTE: use f_bavail instead of f_bfree here.  see 'man
statfs' for more information.  it would be ideal to pass
both so that the client can properly compute all values.
*/
                tmp_trove_statfs->bytes_available = 
                    (PINT_statfs_bsize(&tmp_statfs) * 
                     PINT_statfs_bavail(&tmp_statfs));
                tmp_trove_statfs->bytes_total =
                    (PINT_statfs_bsize(&tmp_statfs) *
                     (PINT_statfs_blocks(&tmp_statfs) -
                      (PINT_statfs_bfree(&tmp_statfs) -
                       PINT_statfs_bavail(&tmp_statfs))));

                return 1;
            }
    }
    return ret;
}

int dbpf_collection_setinfo(TROVE_method_id method_id,
                            TROVE_coll_id coll_id,
                            TROVE_context_id context_id,
                            int option,
                            void *parameter)
{
    int ret = -TROVE_EINVAL;
    struct dbpf_collection* coll;
    coll = dbpf_collection_find_registered(coll_id);


    switch(option)
    {
        case TROVE_COLLECTION_HANDLE_RANGES:
            gossip_debug(GOSSIP_TROVE_DEBUG, 
                         "dbpf collection %d - Setting collection handle "
                         "ranges to %s\n", 
                         (int) coll_id, (char *)parameter);
            ret = trove_set_handle_ranges(
                coll_id, context_id, (char *)parameter);
            break;
        case TROVE_COLLECTION_HANDLE_TIMEOUT:
            gossip_debug(GOSSIP_TROVE_DEBUG, 
                         "dbpf collection %d - Setting handle timeout to "
                         "%ld microseconds\n",
                         (int) coll_id, 
                         (long)((((struct timeval *)parameter)->tv_sec * 1e6) +
                                ((struct timeval *)parameter)->tv_usec));
            ret = trove_set_handle_timeout(
                coll_id, context_id, (struct timeval *)parameter);
            break;
        case TROVE_COLLECTION_ATTR_CACHE_KEYWORDS:
            gossip_debug(GOSSIP_TROVE_DEBUG, 
                         "dbpf collection %d - Setting cache keywords "
                         "of attribute cache to %s\n",
                         (int) coll_id, (char *)parameter);
            gen_mutex_lock(&dbpf_attr_cache_mutex);
            ret = dbpf_attr_cache_set_keywords((char *)parameter);
            gen_mutex_unlock(&dbpf_attr_cache_mutex);
            break;
        case TROVE_COLLECTION_ATTR_CACHE_SIZE:
            gossip_debug(GOSSIP_TROVE_DEBUG, 
                         "dbpf collection %d - Setting "
                         "cache size of attribute cache to %d\n", 
                         (int) coll_id,*(int *)parameter);
            gen_mutex_lock(&dbpf_attr_cache_mutex);
            ret = dbpf_attr_cache_set_size(*((int *)parameter));
            gen_mutex_unlock(&dbpf_attr_cache_mutex);
            break;
        case TROVE_COLLECTION_ATTR_CACHE_MAX_NUM_ELEMS:
            gossip_debug(GOSSIP_TROVE_DEBUG, 
                         "dbpf collection %d - Setting maximum elements of "
                         "attribute cache to %d\n",
                         (int) coll_id, *(int *)parameter);
            gen_mutex_lock(&dbpf_attr_cache_mutex);
            ret = dbpf_attr_cache_set_max_num_elems(*((int *)parameter));
            gen_mutex_unlock(&dbpf_attr_cache_mutex);
            break;
        case TROVE_COLLECTION_ATTR_CACHE_INITIALIZE:
            gossip_debug(GOSSIP_TROVE_DEBUG, 
                         "dbpf collection %d - Initialize collection attr. "
                         "cache\n", (int) coll_id);
            gen_mutex_lock(&dbpf_attr_cache_mutex);
            ret = dbpf_attr_cache_do_initialize();
            gen_mutex_unlock(&dbpf_attr_cache_mutex);
            break;
        case TROVE_COLLECTION_COALESCING_HIGH_WATERMARK:
            gossip_debug(GOSSIP_TROVE_DEBUG, 
                         "dbpf collection %d - Setting HIGH_WATERMARK to %d\n",
                         (int) coll_id, *(int *)parameter);
            assert(coll);
            dbpf_queued_op_set_sync_high_watermark(*(int *)parameter, coll);
            ret = 0;
            break;
        case TROVE_COLLECTION_COALESCING_LOW_WATERMARK:
            gossip_debug(GOSSIP_TROVE_DEBUG, 
                         "dbpf collection %d - Setting LOW_WATERMARK to %d\n",
                         (int) coll_id, *(int *)parameter);
            assert(coll);
            dbpf_queued_op_set_sync_low_watermark(*(int *)parameter, coll);
            ret = 0;
            break;
        case TROVE_COLLECTION_META_SYNC_MODE:
            gossip_debug(GOSSIP_TROVE_DEBUG, 
                         "dbpf collection %d - %s sync mode\n",
                         (int) coll_id,
                         (*(int *)parameter) ? "Enabling" : "Disabling");
            assert(coll);
            dbpf_queued_op_set_sync_mode(*(int *)parameter, coll);
            ret = 0;
            break;
        case TROVE_COLLECTION_IMMEDIATE_COMPLETION:
            gossip_debug(GOSSIP_TROVE_DEBUG, 
                         "dbpf collection %d - %s immediate completion\n",
                         (int) coll_id,
                         (*(int *)parameter) ? "Enabling" : "Disabling");
            assert(coll);
            coll->immediate_completion = *(int *)parameter;
            ret = 0;
            break;
        case TROVE_DIRECTIO_THREADS_NUM:
            trove_directio_threads_num = *(int *)parameter;
            ret = 0;
            break;
        case TROVE_DIRECTIO_OPS_PER_QUEUE:
            trove_directio_ops_per_queue = *(int *)parameter;
            ret = 0;
            break;
        case TROVE_DIRECTIO_TIMEOUT:
            trove_directio_timeout = *(int *)parameter;
            ret = 0;
            break;
    }
    return ret;
}

int dbpf_collection_seteattr(TROVE_coll_id coll_id,
                             TROVE_keyval_s *key_p,
                             TROVE_keyval_s *val_p,
                             TROVE_ds_flags flags,
                             void *user_ptr,
                             TROVE_context_id context_id,
                             TROVE_op_id *out_op_id_p)
{
    int ret = -TROVE_EINVAL;
    struct dbpf_storage *sto_p = NULL;
    struct dbpf_collection *coll_p = NULL;
    DBT db_key, db_data;

    sto_p = my_storage_p;
    if (sto_p == NULL)
    {
        return ret;
    }
    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return ret;
    }

    memset(&db_key, 0, sizeof(db_key));
    memset(&db_data, 0, sizeof(db_data));
    db_key.data = key_p->buffer;
    db_key.size = key_p->buffer_sz;
    db_data.data = val_p->buffer;
    db_data.size = val_p->buffer_sz;

    ret = coll_p->coll_attr_db->put(coll_p->coll_attr_db,
                                    NULL, &db_key, &db_data, 0);
    if (ret != 0)
    {
        gossip_lerr("dbpf_collection_seteattr: %s\n", db_strerror(ret));
        return -dbpf_db_error_to_trove_error(ret);
    }

    ret = coll_p->coll_attr_db->sync(coll_p->coll_attr_db, 0);
    if (ret != 0)
    {
        gossip_lerr("dbpf_collection_seteattr: %s\n", db_strerror(ret));
        return -dbpf_db_error_to_trove_error(ret);
    }

    return 1;
}

int dbpf_collection_geteattr(TROVE_coll_id coll_id,
                             TROVE_keyval_s *key_p,
                             TROVE_keyval_s *val_p,
                             TROVE_ds_flags flags,
                             void *user_ptr,
                             TROVE_context_id context_id,
                             TROVE_op_id *out_op_id_p)
{
    int ret = -TROVE_EINVAL;
    struct dbpf_storage *sto_p = NULL;
    struct dbpf_collection *coll_p = NULL;
    DBT db_key, db_data;

    sto_p = my_storage_p;
    if (sto_p == NULL)
    {
        return ret;
    }
    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return ret;
    }

    memset(&db_key, 0, sizeof(db_key));
    memset(&db_data, 0, sizeof(db_data));
    db_key.data = key_p->buffer;
    db_key.size = key_p->buffer_sz;
    db_key.flags = DB_DBT_USERMEM;

    db_data.data  = val_p->buffer;
    db_data.ulen  = val_p->buffer_sz;
    db_data.flags = DB_DBT_USERMEM;

    ret = coll_p->coll_attr_db->get(coll_p->coll_attr_db,
                                    NULL, &db_key, &db_data, 0);
    if (ret != 0)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "dbpf_collection_geteattr: %s\n", db_strerror(ret));
        return -dbpf_db_error_to_trove_error(ret);
    }

    val_p->read_sz = db_data.size;
    return 1;
}

int dbpf_collection_deleattr(TROVE_coll_id coll_id,
                             TROVE_keyval_s *key_p,
                             TROVE_ds_flags flags,
                             void *user_ptr,
                             TROVE_context_id context_id,
                             TROVE_op_id *out_op_id_p)
{
    int ret = -TROVE_EINVAL;
    struct dbpf_storage *sto_p = NULL;
    struct dbpf_collection *coll_p = NULL;
    DBT db_key;

    sto_p = my_storage_p;
    if (sto_p == NULL)
    {
        return ret;
    }
    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return ret;
    }

    memset(&db_key, 0, sizeof(db_key));
    db_key.data = key_p->buffer;
    db_key.size = key_p->buffer_sz;

    ret = coll_p->coll_attr_db->del(coll_p->coll_attr_db,
                                    NULL, &db_key, 0);
    if (ret != 0)
    {
        gossip_lerr("%s: %s\n", __func__, db_strerror(ret));
        return -dbpf_db_error_to_trove_error(ret);
    }

    ret = coll_p->coll_attr_db->sync(coll_p->coll_attr_db, 0);
    if (ret != 0)
    {
        gossip_lerr("%s: %s\n", __func__, db_strerror(ret));
        return -dbpf_db_error_to_trove_error(ret);
    }

    return 1;
}

static int dbpf_initialize(char *data_path,
			   char *meta_path,
                           TROVE_ds_flags flags)
{
    int ret = -TROVE_EINVAL;
    struct dbpf_storage *sto_p = NULL;

    /* initialize events */
    PINT_event_define_group("trove_dbpf", &trove_dbpf_event_group);

    /* Define the read event:
     * START:
     * (client_id, request_id, rank, metafile_handle,
     *  datafile_handle, op_id, requested_read_size)
     * STOP: (size_read)
     */
    PINT_event_define_event(&trove_dbpf_event_group,
                            "dbpf_read",
                            "%d%d%d%llu%llu%d%d",
                            "%llu",
                            &trove_dbpf_read_event_id);

    /* Define the write event:
     * START:
     * (client_id, request_id, rank, metafile-handle, datafile-handle, op_id, write size)
     * STOP: (size_written)
     */
    PINT_event_define_event(&trove_dbpf_event_group,
                            "dbpf_write",
                            "%d%d%d%llu%llu%d%d",
                            "%llu",
                            &trove_dbpf_write_event_id);

    /* Define the keyval read event:
     * START: (client_id, request_id, rank, metafile-handle, op_id)
     * STOP: (none)
     */
    PINT_event_define_event(&trove_dbpf_event_group,
                            "dbpf_keyval_read",
                            "%d%d%d%llu%d",
                            "",
                            &trove_dbpf_keyval_read_event_id);

    /* Define the keyval write event:
     * START:
     * (client_id, request_id, rank, metafile-handle, op_id)
     * STOP: (none)
     */
    PINT_event_define_event(&trove_dbpf_event_group,
                            "dbpf_keyval_write",
                            "%d%d%d%llu%d",
                            "",
                            &trove_dbpf_keyval_write_event_id);

    /* Define the dspace create event:
     * START:
     * (client_id, request_id, rank, op_id)
     * STOP: (new-handle)
     */
    PINT_event_define_event(&trove_dbpf_event_group,
                            "dbpf_dspace_create",
                            "%d%d%d%d",
                            "%llu",
                            &trove_dbpf_dspace_create_event_id);

    /* Define the dspace create list event:
     * START:
     * (client_id, request_id, rank, op_id)
     * STOP: (new-handle)
     */
    PINT_event_define_event(&trove_dbpf_event_group,
                            "dbpf_dspace_create_list",
                            "%d%d%d%d",
                            "%llu",
                            &trove_dbpf_dspace_create_list_event_id);

    /* Define the dspace getattr event:
     * START:
     * (client_id, request_id, rank, metafile-handle, op_id)
     * STOP: (none)
     */
    PINT_event_define_event(&trove_dbpf_event_group,
                            "dbpf_dspace_getattr",
                            "%d%d%d%llu%d",
                            "",
                            &trove_dbpf_dspace_getattr_event_id);

    /* Define the dspace setattr event:
     * START:
     * (client_id, request_id, rank, metafile-handle, op_id)
     * STOP: (none)
     */
    PINT_event_define_event(&trove_dbpf_event_group,
                            "dbpf_dspace_setattr",
                            "%d%d%d%llu%d",
                            "",
                            &trove_dbpf_dspace_setattr_event_id);

    dbpf_pid = getpid();

    if (!data_path)
    {
        gossip_err("dbpf_initialize failure: invalid data storage path\n");
        return ret;
    }

    if (!meta_path)
    {
	gossip_err("dbpf_initialize failure: invalid metadata storage path\n");
	return ret;
    }

    sto_p = dbpf_storage_lookup(data_path, meta_path, &ret, flags);
    if (sto_p == NULL)
    {
        gossip_debug(
            GOSSIP_TROVE_DEBUG, "dbpf_initialize failure: storage "
            "lookup failed\n");
        return -TROVE_ENOENT;
    }

    my_storage_p = sto_p;

    dbpf_open_cache_initialize();

    return dbpf_thread_initialize();
}

static int start_directio_threads(void)
{
    int ret;
    PINT_worker_attr_t io_worker_attrs;

    if(directio_threads_started)
    {
        /* already running */
        return(0);
    }

    ret = PINT_open_context(&io_ctx, PINT_dbpf_io_completion_callback);
    if(ret < 0)
    {
        dbpf_finalize();
        return ret;
    }

    ret = PINT_manager_init(&io_thread_mgr, io_ctx);
    if(ret < 0)
    {
        PINT_close_context(io_ctx);
        dbpf_finalize();
        return ret;
    }

    io_worker_attrs.type = PINT_WORKER_TYPE_THREADED_QUEUES;
    io_worker_attrs.u.threaded.thread_count = trove_directio_threads_num;
    io_worker_attrs.u.threaded.ops_per_queue = trove_directio_ops_per_queue;
    io_worker_attrs.u.threaded.timeout = trove_directio_timeout;
    ret = PINT_manager_worker_add(io_thread_mgr, &io_worker_attrs, &io_worker_id);
    if(ret < 0)
    {
        PINT_manager_destroy(io_thread_mgr);
        PINT_close_context(io_ctx);
        dbpf_finalize();
        return ret;
    }

    ret = PINT_queue_create(&io_queue_id, NULL);
    if(ret < 0)
    {
	PINT_manager_destroy(io_thread_mgr);
	PINT_close_context(io_ctx);
        dbpf_finalize();
	return ret;
    }

    ret = PINT_manager_queue_add(io_thread_mgr, io_worker_id, io_queue_id);
    if(ret < 0)
    {
	PINT_queue_destroy(io_queue_id);
	PINT_manager_destroy(io_thread_mgr);
	PINT_close_context(io_ctx);
        dbpf_finalize();
	return ret;
    }

    directio_threads_started = 1;

    return(0);
}

static int stop_directio_threads(void)
{
    if(directio_threads_started != 1)
    {
        return 0;
    }

    PINT_manager_queue_remove(io_thread_mgr, io_queue_id);
    PINT_queue_destroy(io_queue_id);
    PINT_manager_destroy(io_thread_mgr);
    PINT_close_context(io_ctx);
    return 0;
}

static int dbpf_direct_initialize(char *data_path,
				  char *meta_path,
				  TROVE_ds_flags flags)
{
    int ret;

    /* some parts of initialization are shared with other methods */
    ret = dbpf_initialize(data_path, meta_path, flags);
    if(ret < 0)
    {
        return(ret);
    }

    /* fire up the IO threads for direct IO */
    ret = start_directio_threads();
    if(ret < 0)
    {
        dbpf_finalize();
        return(ret);
    }
    
    return(0);
}

static int dbpf_direct_finalize(void)
{
    stop_directio_threads();
    dbpf_finalize();
    return 0;
}

int dbpf_finalize(void)
{
    int ret = -TROVE_EINVAL;

    dbpf_thread_finalize();
    dbpf_open_cache_finalize();
    gen_mutex_lock(&dbpf_attr_cache_mutex);
    dbpf_attr_cache_finalize();
    gen_mutex_unlock(&dbpf_attr_cache_mutex);

    if (my_storage_p)
    {
        if( my_storage_p->sto_attr_db )
        {
            ret = my_storage_p->sto_attr_db->sync(my_storage_p->sto_attr_db, 0);
            if (ret)
            {
                gossip_err("dbpf_finalize attr sync: %s\n", db_strerror(ret));
                return -dbpf_db_error_to_trove_error(ret);
            }

            ret = db_close(my_storage_p->sto_attr_db);
            if (ret)
            {
                gossip_err("dbpf_finalize attr close: %s\n", db_strerror(ret));
                return -dbpf_db_error_to_trove_error(ret);
            }
        }
        else
        {
            gossip_err("dbpf_finalize: attribute database not defined\n");
        }

        if( my_storage_p->coll_db )
        {
            ret = my_storage_p->coll_db->sync(my_storage_p->coll_db, 0);
            if (ret)
            {
                gossip_err("dbpf_finalize collection sync: %s\n", 
                           db_strerror(ret));
                return -dbpf_db_error_to_trove_error(ret);
            }
    
            ret = db_close(my_storage_p->coll_db);
            if (ret)
            {
                gossip_err("dbpf_finalize collection close: %s\n", 
                           db_strerror(ret));
                return -dbpf_db_error_to_trove_error(ret);
            }
        }
        else
        {
            gossip_err("dbpf_finalize: collections database not defined\n");
        } 
        free(my_storage_p->data_path);
	free(my_storage_p->meta_path);
        free(my_storage_p);
        my_storage_p = NULL;
    }

    return 1;
}

/* Creates and initializes the databases needed for a dbpf storage
 * space.  This includes:
 * - creating the path to the storage directory
 * - creating storage attribute database, propagating with create time
 * - creating collections database, filling in create time
 */
int dbpf_storage_create(char *data_path,
			char *meta_path,
                        void *user_ptr,
                        TROVE_op_id *out_op_id_p)
{
    int ret = -TROVE_EINVAL;
    char data_dirname[PATH_MAX] = {0};
    char meta_dirname[PATH_MAX] = {0};
    char sto_attrib_dbname[PATH_MAX] = {0};
    char collections_dbname[PATH_MAX] = {0};

    DBPF_GET_DATA_DIRNAME(data_dirname, PATH_MAX, data_path);
    ret = dbpf_mkpath(data_dirname, 0755);
    if (ret != 0)
    {
        return ret;
    }

    DBPF_GET_META_DIRNAME(meta_dirname, PATH_MAX, meta_path);
    ret = dbpf_mkpath(meta_dirname, 0755);
    if (ret != 0)
    {
	return ret;
    }

    DBPF_GET_STO_ATTRIB_DBNAME(sto_attrib_dbname, PATH_MAX, meta_path);
    ret = dbpf_db_create(sto_attrib_dbname, NULL, 0);
    if (ret != 0)
    {
        return ret;
    }

    DBPF_GET_COLLECTIONS_DBNAME(collections_dbname, PATH_MAX, meta_path);
    ret = dbpf_db_create(collections_dbname, NULL, DB_RECNUM);
    if (ret != 0)
    {
        gossip_lerr("dbpf_storage_create: removing storage attribute database after failed create attempt");
        unlink(sto_attrib_dbname);
        return ret;
    }

    return 1;
}

int dbpf_storage_remove(char *data_path,
			char *meta_path,
            void *user_ptr,
            TROVE_op_id *out_op_id_p)
{
    int ret = -TROVE_EINVAL;
    char path_name[PATH_MAX] = {0};

    if (my_storage_p) {
        db_close(my_storage_p->sto_attr_db);
        db_close(my_storage_p->coll_db);
		free(my_storage_p->meta_path);
		free(my_storage_p->data_path);
        free(my_storage_p);
        my_storage_p = NULL;
    }
    
    DBPF_GET_STO_ATTRIB_DBNAME(path_name, PATH_MAX, meta_path);
    gossip_debug(GOSSIP_TROVE_DEBUG, "Removing %s\n", path_name);

    if (unlink(path_name) != 0)
    {
        ret = -trove_errno_to_trove_error(errno);
        goto storage_remove_failure;
    }

    DBPF_GET_COLLECTIONS_DBNAME(path_name, PATH_MAX, meta_path);
    gossip_debug(GOSSIP_TROVE_DEBUG, "Removing %s\n", path_name);

    if (unlink(path_name) != 0)
    {
        ret = -trove_errno_to_trove_error(errno);
        goto storage_remove_failure;
    }

    DBPF_GET_META_DIRNAME(path_name, PATH_MAX, meta_path);
    gossip_debug(GOSSIP_TROVE_DEBUG, "Removing %s\n", path_name);
    if (rmdir(path_name) != 0)
    {
		perror("failure removing metadata directory");
		ret = -trove_errno_to_trove_error(errno);
		goto storage_remove_failure;
    }

    DBPF_GET_DATA_DIRNAME(path_name, PATH_MAX, data_path);
    gossip_debug(GOSSIP_TROVE_DEBUG, "Removing %s\n", path_name);
    if (rmdir(path_name) != 0)
    {
        perror("failure removing data directory");
        ret = -trove_errno_to_trove_error(errno);
        goto storage_remove_failure;
    }

    gossip_debug(GOSSIP_TROVE_DEBUG,
                 "databases for storage space removed.\n");

    return 1;

storage_remove_failure:
    return ret;
}

/*
   1) check collections database to see if coll_id is already used
   (error out if so)
   2) create collection attribute database
   3) store trove-dbpf version in collection attribute database
   4) store last handle value in collection attribute database
   5) create dataspace attributes database
   6) create keyval and bstream directories
   */
int dbpf_collection_create(char *collname,
                           TROVE_coll_id new_coll_id,
                           void *user_ptr,
                           TROVE_op_id *out_op_id_p)
{
    int ret = -TROVE_EINVAL, error = 0, i = 0;
    TROVE_handle zero = TROVE_HANDLE_NULL;
    struct dbpf_storage *sto_p;
    struct dbpf_collection_db_entry db_data;
    DB *db_p = NULL;
    DBT key, data;
    struct stat dirstat;
    struct stat dbstat;
    char path_name[PATH_MAX] = {0}, dir[PATH_MAX] = {0};

    if (my_storage_p == NULL)
    {
        gossip_err("Invalid storage name specified\n");
        return ret;
    }
    sto_p = my_storage_p;

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));

    key.data = collname;
    key.size = strlen(collname)+1;
    key.flags = DB_DBT_USERMEM;
    data.data = &db_data;
    data.ulen = sizeof(db_data);
    data.flags = DB_DBT_USERMEM;

    /* ensure that the collection record isn't already there */
    ret = sto_p->coll_db->get(sto_p->coll_db, NULL, &key, &data, 0);
    if (ret != DB_NOTFOUND)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "coll %s already exists with "
                     "coll_id %d, len = %d.\n",
                     collname, db_data.coll_id, data.size);
        return -dbpf_db_error_to_trove_error(ret);
    }

    memset(&db_data, 0, sizeof(db_data));
    db_data.coll_id = new_coll_id;

    key.data = collname;
    key.size = strlen(collname)+1;
    data.data = &db_data;
    data.size = sizeof(db_data);

    ret = sto_p->coll_db->put(sto_p->coll_db, NULL, &key, &data, 0);
    if (ret)
    {
        gossip_err("dbpf_collection_create: %s\n", db_strerror(ret));
        return -dbpf_db_error_to_trove_error(ret);
    }

    ret = sto_p->coll_db->sync(sto_p->coll_db, 0);
    if (ret)
    {
        gossip_err("dbpf_collection_create: %s\n", db_strerror(ret));
        return -dbpf_db_error_to_trove_error(ret);
    }

    DBPF_GET_DATA_DIRNAME(path_name, PATH_MAX, sto_p->data_path);
    ret = stat(path_name, &dirstat);
    if (ret < 0 && errno != ENOENT)
    {
        gossip_err("stat failed on data directory %s\n", path_name);
        return -trove_errno_to_trove_error(errno);
    }
    else if (ret < 0)
    {
        ret = mkdir(path_name, 0755);
        if (ret != 0)
        {
            gossip_err("mkdir failed on data directory %s\n", path_name);
            return -trove_errno_to_trove_error(errno);
        }
    }

    DBPF_GET_META_DIRNAME(path_name, PATH_MAX, sto_p->meta_path);
    ret = stat(path_name, &dirstat);
    if (ret < 0 && errno != ENOENT)
    {
		gossip_err("stat failed on metadata directory %s\n", path_name);
		return -trove_errno_to_trove_error(errno);
    }
    else if (ret < 0)
    {
		ret = mkdir(path_name, 0755);
		if (ret != 0)
		{
	    	gossip_err("mkdir failed on metadata directory %s\n", path_name);
	    	return -trove_errno_to_trove_error(errno);
		}
    }


    DBPF_GET_COLL_DIRNAME(path_name, PATH_MAX, sto_p->data_path, new_coll_id);
    ret = mkdir(path_name, 0755);
    if (ret != 0 && strcmp(sto_p->data_path, sto_p->meta_path))
    {
        gossip_err("mkdir failed on data collection directory %s\n", 
		   path_name);
        return -trove_errno_to_trove_error(errno);
    }

    DBPF_GET_COLL_DIRNAME(path_name, PATH_MAX, sto_p->meta_path, new_coll_id);
    ret = mkdir(path_name, 0755);
    if (ret != 0 && strcmp(sto_p->data_path, sto_p->meta_path))
    {
	gossip_err("mkdir failed on metadata collection directory %s\n",
		   path_name);
	return -trove_errno_to_trove_error(errno);
    }

    DBPF_GET_COLL_ATTRIB_DBNAME(path_name, PATH_MAX,
                                sto_p->meta_path, new_coll_id);

    ret = stat(path_name, &dbstat);
    if(ret < 0 && errno != ENOENT)
    {
        gossip_err("failed to stat db file: %s\n", path_name);
        return -trove_errno_to_trove_error(errno);
    }
    else if(ret < 0)
    {
	ret = dbpf_db_create(path_name, NULL, 0);
        if (ret != 0)
        {
            gossip_err("dbpf_db_create failed on attrib db %s\n", path_name);
            return ret;
        }
    }

    db_p = dbpf_db_open(path_name, NULL, &error, NULL, 0);
    if (db_p == NULL)
    {
        gossip_err("dbpf_db_open failed on attrib db %s\n", path_name);
        return error;
    }

    /*
       store trove-dbpf version string in the collection.  this is used
       to know what format the metadata is stored in on disk.
       */
    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data = TROVE_DBPF_VERSION_KEY;
    key.size = strlen(TROVE_DBPF_VERSION_KEY);
    data.data = TROVE_DBPF_VERSION_VALUE;
    data.size = strlen(TROVE_DBPF_VERSION_VALUE);

    ret = db_p->put(db_p, NULL, &key, &data, 0);
    if (ret != 0)
    {
        gossip_err("db_p->put failed writing trove-dbpf version "
                   "string: %s\n", db_strerror(ret));
        return -dbpf_db_error_to_trove_error(ret);
    }

    gossip_debug(
        GOSSIP_TROVE_DEBUG, "wrote trove-dbpf version %s to "
        "collection attribute database\n", TROVE_DBPF_VERSION_VALUE);

    /* store initial handle value */
    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data = LAST_HANDLE_STRING;
    key.size = sizeof(LAST_HANDLE_STRING);
    data.data = &zero;
    data.size = sizeof(zero);

    ret = db_p->put(db_p, NULL, &key, &data, 0);
    if (ret != 0)
    {
        gossip_err("db_p->put failed writing initial handle value: %s\n",
                   db_strerror(ret));
        return -dbpf_db_error_to_trove_error(ret);
    }
    db_p->sync(db_p, 0);
    db_close(db_p);

    DBPF_GET_DS_ATTRIB_DBNAME(path_name, PATH_MAX, sto_p->meta_path, 
			      new_coll_id);
    ret = stat(path_name, &dbstat);
    if(ret < 0 && errno != ENOENT)
    {
        gossip_err("failed to stat ds attrib db: %s\n", path_name);
        return -trove_errno_to_trove_error(errno);
    }
    if(ret < 0)
    {
        ret = dbpf_db_create(path_name, NULL, 0);
        if (ret != 0)
        {
            gossip_err("dbpf_db_create failed on %s\n", path_name);
            return ret;
        }
    }

    DBPF_GET_KEYVAL_DBNAME(path_name, PATH_MAX, sto_p->meta_path, new_coll_id);
    ret = stat(path_name, &dbstat);
    if(ret < 0 && errno != ENOENT)
    {
        gossip_err("failed to stat keyval db: %s\n", path_name);
        return -trove_errno_to_trove_error(errno);
    }
    if(ret < 0)
    {
        ret = dbpf_db_create(path_name, NULL, 0);
        if (ret != 0)
        {
            gossip_err("dbpf_db_create failed on %s\n", path_name);
            return ret;
        }
    }

    DBPF_GET_BSTREAM_DIRNAME(path_name, PATH_MAX, sto_p->data_path,
			     new_coll_id);
    ret = mkdir(path_name, 0755);
    if(ret != 0)
    {
        gossip_err("mkdir failed on bstream directory %s\n", path_name);
        return -trove_errno_to_trove_error(errno);
    }

    for(i = 0; i < DBPF_BSTREAM_MAX_NUM_BUCKETS; i++)
    {
        snprintf(dir, PATH_MAX, "%s/%.8d", path_name, i);
        if ((mkdir(dir, 0755) == -1) && (errno != EEXIST))
        {
            gossip_err("mkdir failed on bstream bucket directory %s\n",
                       dir);
            return -trove_errno_to_trove_error(errno);
        }
    }

    DBPF_GET_STRANDED_BSTREAM_DIRNAME(path_name, PATH_MAX, sto_p->data_path,
                                      new_coll_id);
    ret = mkdir(path_name, 0755);
    if(ret != 0)
    {
        gossip_err("mkdir failed on bstream directory %s\n", path_name);
        return -trove_errno_to_trove_error(errno);
    }

    return 1;
}

int dbpf_collection_remove(char *collname,
                           void *user_ptr,
                           TROVE_op_id *out_op_id_p)
{
    char path_name[PATH_MAX];
    struct dbpf_storage *sto_p = NULL;
    struct dbpf_collection_db_entry db_data;
    DBT key, data;
    int ret = 0, i = 0;
    DIR *current_dir = NULL;
    struct dirent *current_dirent = NULL;
    struct stat file_info;
    char dir[PATH_MAX] = {0}, tmp_path[PATH_MAX] = {0};
    struct dbpf_collection *db_collection = NULL;

    if (!collname)
    {
        return -TROVE_EINVAL;
    }

    sto_p = my_storage_p;
    if (!sto_p)
    {
        return -TROVE_ENOENT;
    }

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));

    key.data = collname;
    key.size = strlen(collname) + 1;
    key.flags = DB_DBT_USERMEM;
    data.data = &db_data;
    data.ulen = sizeof(db_data);
    data.flags = DB_DBT_USERMEM;

    ret = sto_p->coll_db->get(sto_p->coll_db, NULL, &key, &data, 0);
    if (ret != 0)
    {
        sto_p->coll_db->err(sto_p->coll_db, ret, "DB->get collection");
        return -dbpf_db_error_to_trove_error(ret);
    }

    ret = sto_p->coll_db->del(sto_p->coll_db, NULL, &key, 0);
    if (ret != 0)
    {
        sto_p->coll_db->err(sto_p->coll_db, ret, "DB->del");
        return -dbpf_db_error_to_trove_error(ret);
    }

    ret = sto_p->coll_db->sync(sto_p->coll_db, 0);
    if (ret != 0)
    {
        sto_p->coll_db->err(sto_p->coll_db, ret, "DB->sync");
        return -dbpf_db_error_to_trove_error(ret);
    }

    if ((db_collection = dbpf_collection_find_registered(db_data.coll_id)) == NULL) {
        ret = -TROVE_ENOENT;
    }
    else {
        /* Clean up properly by closing all db handles */
        db_close(db_collection->coll_attr_db);
        db_close(db_collection->ds_db);
        db_close(db_collection->keyval_db);
        /* so that environment can also be cleaned up */
        dbpf_putdb_env(db_collection->coll_env, db_collection->meta_path);
        dbpf_collection_deregister(db_collection);
        free(db_collection->name);
        free(db_collection->meta_path);
		free(db_collection->data_path);
        PINT_dbpf_keyval_pcache_finalize(db_collection->pcache);
        free(db_collection);
    }

    DBPF_GET_DS_ATTRIB_DBNAME(path_name, PATH_MAX,
                              sto_p->meta_path, db_data.coll_id);
    if (unlink(path_name) != 0)
    {
        gossip_err("failure removing dataspace attrib db\n");
        ret = -trove_errno_to_trove_error(errno);
    }

    DBPF_GET_KEYVAL_DBNAME(path_name, PATH_MAX,
                           sto_p->meta_path, db_data.coll_id);
    if(unlink(path_name) != 0)
    {
        gossip_err("failure removing keyval db\n");
        ret = -trove_errno_to_trove_error(errno);
    }

    DBPF_GET_COLL_ATTRIB_DBNAME(path_name, PATH_MAX,
                                sto_p->meta_path, db_data.coll_id);
    if (unlink(path_name) != 0)
    {
        gossip_err("failure removing collection attrib db\n");
        ret = -trove_errno_to_trove_error(errno);
    }

    DBPF_GET_BSTREAM_DIRNAME(path_name, PATH_MAX,
                             sto_p->data_path, db_data.coll_id);
    for(i = 0; i < DBPF_BSTREAM_MAX_NUM_BUCKETS; i++)
    {
        snprintf(dir, PATH_MAX, "%s/%.8d", path_name, i);

        /* remove all bstream files in this bucket directory */
        current_dir = opendir(dir);
        if (current_dir)
        {
            while((current_dirent = readdir(current_dir)))
            {
                if ((strcmp(current_dirent->d_name, ".") == 0) ||
                    (strcmp(current_dirent->d_name, "..") == 0))
                {
                    continue;
                }
                snprintf(tmp_path, PATH_MAX, "%s/%s", dir,
                         current_dirent->d_name);
                if (stat(tmp_path, &file_info) < 0)
                {
                    gossip_err("error doing stat on bstream entry\n");
                    ret = -trove_errno_to_trove_error(errno);
                    closedir(current_dir);
                    goto collection_remove_failure;
                }
                assert(S_ISREG(file_info.st_mode));
                if (unlink(tmp_path) != 0)
                {
                    gossip_err("failure removing bstream entry\n");
                    ret = -trove_errno_to_trove_error(errno);
                    closedir(current_dir);
                    goto collection_remove_failure;
                }
            }
            closedir(current_dir);
        }
        rmdir(dir);
    }

    if (rmdir(path_name) != 0)
    {
        gossip_err("failure removing bstream directory %s\n", path_name);
        ret = -trove_errno_to_trove_error(errno);
    }

    DBPF_GET_STRANDED_BSTREAM_DIRNAME(path_name, PATH_MAX,
                                      sto_p->data_path, db_data.coll_id);

    /* remove stranded bstreams directory */
    current_dir = opendir(path_name);
    if(current_dir)
    {
        while((current_dirent = readdir(current_dir)))
        {
            if((strcmp(current_dirent->d_name, ".") == 0) ||
               (strcmp(current_dirent->d_name, "..") == 0))
            {
                continue;
            }
            snprintf(tmp_path, PATH_MAX, "%s/%s", path_name,
                     current_dirent->d_name);
            if(stat(tmp_path, &file_info) < 0)
            {
                gossip_err("error doing stat on bstream entry\n");
                ret = -trove_errno_to_trove_error(errno);
                closedir(current_dir);
                goto collection_remove_failure;
            }
            assert(S_ISREG(file_info.st_mode));
            if(unlink(tmp_path) != 0)
            {
                gossip_err("failure removing bstream entry\n");
                ret = -trove_errno_to_trove_error(errno);
                closedir(current_dir);
                goto collection_remove_failure;
            }
        }
        closedir(current_dir);
    }

    if(rmdir(path_name) != 0)
    {
        gossip_err("failure removing bstream directory %s\n", 
                   path_name);
        ret = -trove_errno_to_trove_error(errno);
        goto collection_remove_failure;
    }

    DBPF_GET_COLL_DIRNAME(path_name, PATH_MAX,
			  sto_p->meta_path, db_data.coll_id);
    if (rmdir(path_name) != 0)
    {
		gossip_err("failure removing metadata collection directory\n");
		ret = -trove_errno_to_trove_error(errno);
		goto collection_remove_failure;
    }

    DBPF_GET_COLL_DIRNAME(path_name, PATH_MAX,
                          sto_p->data_path, db_data.coll_id);
    if (rmdir(path_name) != 0)
    {
        gossip_err("failure removing data collection directory\n");
        ret = -trove_errno_to_trove_error(errno);
    }
collection_remove_failure:
    return ret;
}

int dbpf_collection_iterate(TROVE_ds_position *inout_position_p,
                            TROVE_keyval_s *name_array,
                            TROVE_coll_id *coll_id_array,
                            int *inout_count_p,
                            TROVE_ds_flags flags,
                            TROVE_vtag_s *vtag,
                            void *user_ptr,
                            TROVE_op_id *out_op_id_p)
{
    int ret = -TROVE_EINVAL, i = 0;
    db_recno_t recno = {0};
    DB *db_p = NULL;
    DBC *dbc_p = NULL;
    DBT key, data;
    struct dbpf_collection_db_entry db_entry;

    /* if caller passed that they're are at the end, return 0 */
    if (*inout_position_p == TROVE_ITERATE_END)
    {
        *inout_count_p = 0;
        return 1;
    }

    /* collection db is stored with storage space info */
    db_p = my_storage_p->coll_db;

    /* get a cursor */
    ret = db_p->cursor(db_p, NULL, &dbc_p, 0);
    if (ret != 0)
    {
        ret = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }

    /* see keyval iterate for discussion of this implementation */
    if (*inout_position_p != TROVE_ITERATE_START)
    {
        /* need to position cursor before reading.  note that this
         * will actually position the cursor over the last thing that
         * was read on the last call, so we don't need to return what
         * we get back.  here we make sure that the key is big
         * enough to hold the position that we need to pass in.
         */
       
        memset(&key, 0, sizeof(key));
        key.data = name_array[0].buffer;
        key.ulen = name_array[0].buffer_sz;
        *(db_recno_t *)key.data = (db_recno_t) *inout_position_p;
        key.size = sizeof(db_recno_t);
        key.flags |= DB_DBT_USERMEM;

        memset(&data, 0, sizeof(data));
        data.data = &db_entry;
        data.size = data.ulen = sizeof(db_entry);
        data.flags |= DB_DBT_USERMEM;

        /* position the cursor and grab the first key/value pair */
        ret = dbc_p->c_get(dbc_p, &key, &data, DB_SET_RECNO);
        if (ret == DB_NOTFOUND)
        {
            goto return_ok;
        }
        else if (ret != 0)
        {
            ret = -dbpf_db_error_to_trove_error(ret);
            goto return_error;
        }
    }

    for (i = 0; i < *inout_count_p; i++)
    {
        memset(&key, 0, sizeof(key));
        key.data = name_array[i].buffer;
        key.size = key.ulen = name_array[i].buffer_sz;
        key.flags |= DB_DBT_USERMEM;

        memset(&data, 0, sizeof(data));
        data.data = &db_entry;
        data.size = data.ulen = sizeof(db_entry);
        data.flags |= DB_DBT_USERMEM;

        ret = dbc_p->c_get(dbc_p, &key, &data, DB_NEXT);
        if (ret == DB_NOTFOUND)
        {
            goto return_ok;
        }
        else if (ret != 0)
        {
            ret = -dbpf_db_error_to_trove_error(ret);
            goto return_error;
        }
        coll_id_array[i] = db_entry.coll_id;
    }

return_ok:
    if (ret == DB_NOTFOUND)
    {
        *inout_position_p = TROVE_ITERATE_END;
    }
    else
    {
        char buf[64];
        /* get the record number to return.
         *
         * note: key field is ignored by c_get in this case.  sort of.
         * i'm not actually sure what they mean by "ignored", because
         * it sure seems to matter what you put in there...
         */
        memset(&key, 0, sizeof(key));
        key.data = buf;
        key.size = key.ulen = 64;
        key.dlen = 64;
        key.doff = 0;
        key.flags |= DB_DBT_USERMEM | DB_DBT_PARTIAL;

        memset(&data, 0, sizeof(data));
        data.data = &recno;
        data.size = data.ulen = sizeof(recno);
        data.flags |= DB_DBT_USERMEM;

        ret = dbc_p->c_get(dbc_p, &key, &data, DB_GET_RECNO);
        if (ret == DB_NOTFOUND)
        {
            gossip_debug(GOSSIP_TROVE_DEBUG,
                         "warning: keyval iterate -- notfound\n");
        }
        else if (ret != 0)
        {
            gossip_debug(GOSSIP_TROVE_DEBUG, "warning: keyval iterate -- "
                         "some other failure @ recno\n");
            ret = -dbpf_db_error_to_trove_error(ret);
        }

        assert(recno != TROVE_ITERATE_START &&
               recno != TROVE_ITERATE_END);
        *inout_position_p = recno;
    }
    /*
       'position' points us to the record we just read, or is set to
       END
       */

    *inout_count_p = i;

    ret = dbc_p->c_close(dbc_p);
    if (ret != 0)
    {
        ret = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }
    return 1;

return_error:

    if (dbc_p)
    {
        dbc_p->c_close(dbc_p);
    }

    gossip_lerr("dbpf_collection_iterate_op_svc: %s\n", db_strerror(ret));

    *inout_count_p = i;
    return ret;
}

static int dbpf_direct_collection_clear(TROVE_coll_id coll_id)
{
    stop_directio_threads();
    return dbpf_collection_clear(coll_id);
}

int dbpf_collection_clear(TROVE_coll_id coll_id)
{
    int ret;
    struct dbpf_collection *coll_p = dbpf_collection_find_registered(coll_id);

    dbpf_collection_deregister(coll_p);

    if( coll_p == NULL )
    {
       gossip_err("Trove collection not defined.\n");
       return 0;
    }

    if ( (coll_p->coll_attr_db != NULL ) &&
         (ret = coll_p->coll_attr_db->sync(coll_p->coll_attr_db, 0)) != 0)
    {
        gossip_err("db_sync(coll_attr_db): %s\n", db_strerror(ret));
    }

    if ( (coll_p->coll_attr_db != NULL ) &&
         (ret = db_close(coll_p->coll_attr_db)) != 0) 
    {
        gossip_lerr("db_close(coll_attr_db): %s\n", db_strerror(ret));
    }

    if ( (coll_p->ds_db != NULL ) &&
         (ret = coll_p->ds_db->sync(coll_p->ds_db, 0)) != 0)
    {
        gossip_err("db_sync(coll_ds_db): %s\n", db_strerror(ret));
    }

    if ( (coll_p->ds_db != NULL ) &&
         (ret = db_close(coll_p->ds_db)) != 0) 
    {
        gossip_lerr("db_close(coll_ds_db): %s\n", db_strerror(ret));
    }

    if ( (coll_p->keyval_db != NULL ) &&
         (ret = coll_p->keyval_db->sync(coll_p->keyval_db, 0)) != 0)
    {
        gossip_err("db_sync(coll_keyval_db): %s\n", db_strerror(ret));
    }

    if ( (coll_p->keyval_db != NULL ) &&
         (ret = db_close(coll_p->keyval_db)) != 0) 
    {
        gossip_lerr("db_close(coll_keyval_db): %s\n", db_strerror(ret));
    }

    if( coll_p->coll_env != NULL )
    {
        dbpf_putdb_env(coll_p->coll_env, coll_p->meta_path);
    }
    free(coll_p->name);
    free(coll_p->data_path);
    free(coll_p->meta_path);
    PINT_dbpf_keyval_pcache_finalize(coll_p->pcache);

    free(coll_p);
    return 0;
}

static int dbpf_direct_collection_lookup(char *collname,
                                         TROVE_coll_id *out_coll_id_p,
                                         void *user_ptr,
                                         TROVE_op_id *out_op_id_p)
{
    int ret;

    /* most of this is shared with the other methods */
    ret = dbpf_collection_lookup(collname, out_coll_id_p, 
        user_ptr, out_op_id_p);
    if(ret < 0)
    {
        return(ret);
    }

    /* start directio threads if they aren't already running */
    ret = start_directio_threads();
    if(ret < 0)
    {
        return(ret);
    }

    return(0);
}

int dbpf_collection_lookup(char *collname,
                           TROVE_coll_id *out_coll_id_p,
                           void *user_ptr,
                           TROVE_op_id *out_op_id_p)
{
    int ret = -TROVE_EINVAL;
    struct dbpf_storage *sto_p = NULL;
    struct dbpf_collection *coll_p = NULL;
    struct dbpf_collection_db_entry db_data;
    DBT key, data;
    char path_name[PATH_MAX];
    char trove_dbpf_version[32] = {0};
    int sto_major, sto_minor, sto_inc, major, minor, inc;

    gossip_debug(GOSSIP_TROVE_DEBUG, "dbpf_collection_lookup of coll: %s\n", 
                 collname);
    sto_p = my_storage_p;
    if (!sto_p)
    {
        return ret;
    }

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data = collname;
    key.size = strlen(collname)+1;
    key.flags = DB_DBT_USERMEM;
    data.data = &db_data;
    data.ulen = sizeof(db_data);
    data.flags = DB_DBT_USERMEM;

    ret = sto_p->coll_db->get(sto_p->coll_db, NULL, &key, &data, 0);
    if (ret == DB_NOTFOUND)
    {
        return -TROVE_ENOENT;
    }
    else if (ret != 0)
    {
        sto_p->coll_db->err(sto_p->coll_db, ret, "DB->get collection");
        gossip_debug(GOSSIP_TROVE_DEBUG, "lookup got error (%d)\n", ret);
        return -dbpf_db_error_to_trove_error(ret);
    }

    /*
       look to see if we have already registered this collection; if
       so, return
       */
    coll_p = dbpf_collection_find_registered(db_data.coll_id);
    if (coll_p != NULL)
    {
        *out_coll_id_p = coll_p->coll_id;
        return 1;
    }

    /*
       this collection hasn't been registered already (ie. looked up
       before)
       */
    coll_p = (struct dbpf_collection *)malloc(
        sizeof(struct dbpf_collection));
    if (coll_p == NULL)
    {
        return -TROVE_ENOMEM;
    }
    memset(coll_p, 0, sizeof(struct dbpf_collection));

    coll_p->refct = 0;
    coll_p->coll_id = db_data.coll_id;
    coll_p->storage = sto_p;

    coll_p->name = strdup(collname);
    if (!coll_p->name)
    {
        free(coll_p);
        return -TROVE_ENOMEM;
    }
    /* Path to data collection dir */
    snprintf(path_name, PATH_MAX, "/%s/%08x/", sto_p->data_path, 
	     coll_p->coll_id);
    coll_p->data_path = strdup(path_name);
    if (!coll_p->data_path) 
    {
        free(coll_p->name);
        free(coll_p);
        return -TROVE_ENOMEM;
    }

    snprintf(path_name, PATH_MAX, "/%s/%08x/", 
	     sto_p->meta_path, coll_p->coll_id);
    coll_p->meta_path = strdup(path_name);
    if (!coll_p->meta_path)
    {
	free(coll_p->data_path);
	free(coll_p->name);
	free(coll_p);
	return -TROVE_ENOMEM;
    }

    if ((coll_p->coll_env = dbpf_getdb_env(coll_p->meta_path, COLL_ENV_FLAGS, &ret)) == NULL) 
    {
        free(coll_p->meta_path);
	free(coll_p->data_path);
        free(coll_p->name);
        free(coll_p);
        return -dbpf_db_error_to_trove_error(ret);
    }

    DBPF_GET_COLL_ATTRIB_DBNAME(path_name, PATH_MAX,
                                sto_p->meta_path, coll_p->coll_id);
    
    coll_p->coll_attr_db = dbpf_db_open(path_name, coll_p->coll_env,
                                        &ret, NULL, 0);
    if (coll_p->coll_attr_db == NULL)
    {
        dbpf_putdb_env(coll_p->coll_env, coll_p->meta_path);
        free(coll_p->meta_path);
	free(coll_p->data_path);
        free(coll_p->name);
        free(coll_p);
        return ret;
    }

    /* make sure the version matches the version we understand */
    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data = TROVE_DBPF_VERSION_KEY;
    key.size = strlen(TROVE_DBPF_VERSION_KEY);
    key.flags = DB_DBT_USERMEM;
    data.data = &trove_dbpf_version;
    data.ulen = 32;
    data.flags = DB_DBT_USERMEM;

    ret = coll_p->coll_attr_db->get(
        coll_p->coll_attr_db, NULL, &key, &data, 0);

    if (ret)
    {
        gossip_err("Failed to retrieve collection version: %s\n",
                   db_strerror(ret));
        db_close(coll_p->coll_attr_db);
        dbpf_putdb_env(coll_p->coll_env, coll_p->meta_path);
        free(coll_p->meta_path);
	free(coll_p->data_path);
        free(coll_p->name);
        free(coll_p);
        return -dbpf_db_error_to_trove_error(ret);
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "collection lookup: version is "
                 "%s\n", trove_dbpf_version);

    ret = sscanf(trove_dbpf_version, "%d.%d.%d", 
                 &sto_major, &sto_minor, &sto_inc);
    if(ret < 3)
    {
        gossip_err("Failed to get the version "
                   "components from the storage version: %s\n",
                   trove_dbpf_version);
        return -TROVE_EINVAL;
    }

    ret = sscanf(TROVE_DBPF_VERSION_VALUE, "%d.%d.%d",
                 &major, &minor, &inc);
    if(ret < 3)
    {
        gossip_err("Failed to get the version "
                   "components from the implementation's version: %s\n",
                   TROVE_DBPF_VERSION_VALUE);
        return -TROVE_EINVAL;
    }

    /* before version 0.1.3, no storage formats were compatible.
     * Now 0.1.2 is compatible with 0.1.3, with the caveat that the right
     * dspace db comparison function is specified when its opened.
     *
     * From 0.1.1 to 0.1.2, the storage formats aren't compatible, but
     * in future (> 0.1.3) releases, only incremental version changes
     * means backward compatibility is maintained, 
     * while anything else is incompatible.
     */
    if(sto_major < major || sto_minor < minor ||
       !strcmp(trove_dbpf_version, "0.1.1"))
    {
        db_close(coll_p->coll_attr_db);
        dbpf_putdb_env(coll_p->coll_env, coll_p->meta_path);
        free(coll_p->meta_path);
		free(coll_p->data_path);
        free(coll_p->name);
        free(coll_p);
        gossip_err("Trove-dbpf metadata format version mismatch!\n");
        gossip_err("This collection has version %s\n",
                   trove_dbpf_version);
        gossip_err("This code understands version %s\n",
                   TROVE_DBPF_VERSION_VALUE);
        return -TROVE_EINVAL;
    }

    DBPF_GET_DS_ATTRIB_DBNAME(path_name, PATH_MAX,
                              sto_p->meta_path, coll_p->coll_id);

    if(sto_major == 0 && sto_minor == 1 && sto_inc < 3)
    {
        /* use old comparison function */
        coll_p->ds_db = dbpf_db_open(
            path_name, coll_p->coll_env, &ret,
            &PINT_trove_dbpf_ds_attr_compare_reversed, 0);
    }
    else
    {
        /* new comparison function orders dspace entries so that berkeley
         * DB does page reads in the right order (for handle_iterate)
         */
        coll_p->ds_db = dbpf_db_open(
            path_name, coll_p->coll_env, &ret,
            &PINT_trove_dbpf_ds_attr_compare, 0);
    }

    if (coll_p->ds_db == NULL)
    {
        db_close(coll_p->coll_attr_db);
        dbpf_putdb_env(coll_p->coll_env, coll_p->meta_path);
        free(coll_p->meta_path);
		free(coll_p->data_path);
        free(coll_p->name);
        free(coll_p);
        return ret;
    }

    DBPF_GET_KEYVAL_DBNAME(path_name, PATH_MAX,
                           sto_p->meta_path, coll_p->coll_id);

    coll_p->keyval_db = dbpf_db_open(path_name, coll_p->coll_env,
                                     &ret, PINT_trove_dbpf_keyval_compare, 0);
    if(coll_p->keyval_db == NULL)
    {
        db_close(coll_p->coll_attr_db);
        db_close(coll_p->ds_db);
        dbpf_putdb_env(coll_p->coll_env, coll_p->meta_path);
        free(coll_p->meta_path);
		free(coll_p->data_path);
        free(coll_p->name);
        free(coll_p);
        return ret;
    }

    coll_p->pcache = PINT_dbpf_keyval_pcache_initialize();
    if(!coll_p->pcache)
    {
        db_close(coll_p->coll_attr_db);
        db_close(coll_p->keyval_db);
        db_close(coll_p->ds_db);
        dbpf_putdb_env(coll_p->coll_env, coll_p->meta_path);
        free(coll_p->meta_path);
		free(coll_p->data_path);
        free(coll_p->name);
        free(coll_p);
        return -TROVE_ENOMEM;
    }

    coll_p->next_p = NULL;

    /*
     * Initialize defaults to ensure working
     */
    coll_p->c_high_watermark = 10;
    coll_p->c_low_watermark = 1;
    coll_p->meta_sync_enabled = 1; /* MUST be 1 !*/

    dbpf_collection_register(coll_p);
    *out_coll_id_p = coll_p->coll_id;

    clear_stranded_bstreams(coll_p->coll_id);

    return 1;
}

/* dbpf_storage_lookup()
 *
 * Internal function.
 *
 * Returns pointer to a dbpf_storage structure that refers to the
 * named storage region.  This might involve populating the structure,
 * or it might simply involve returning the pointer (if the structure
 * was already available).
 *
 * It is expected that this function will be called primarily on
 * startup, as this is the point where we are passed a list of storage
 * regions that we are responsible for.  After that point most
 * operations will be referring to a coll_id instead, and the dbpf_storage
 * structure will be found by following the link from the dbpf_coll
 * structure associated with that collection.
 */
struct dbpf_storage *dbpf_storage_lookup(
    char *data_path, char *meta_path, int *error_p, TROVE_ds_flags flags)
{
    char path_name[PATH_MAX] = {0};
    struct dbpf_storage *sto_p = NULL;
    struct stat sbuf;

    if (my_storage_p != NULL)
    {
        return my_storage_p;
    }

    if (stat(data_path, &sbuf) < 0) 
    {
        *error_p = -TROVE_ENOENT;
        return NULL;
    }
    if (!S_ISDIR(sbuf.st_mode))
    {
        *error_p = -TROVE_EINVAL;
        gossip_err("%s is not a directory\n", data_path);
        return NULL;
    }

    if (stat(meta_path, &sbuf) < 0)
    {
	*error_p = -TROVE_ENOENT;
	return NULL;
    }
    if (!S_ISDIR(sbuf.st_mode))
    {
	*error_p = -TROVE_EINVAL;
	gossip_err("%s is not a directory\n", meta_path);
	return NULL;
    }

    sto_p = (struct dbpf_storage *)malloc(sizeof(struct dbpf_storage));
    if (sto_p == NULL)
    {
        *error_p = -TROVE_ENOMEM;
        return NULL;
    }
    memset(sto_p, 0, sizeof(struct dbpf_storage));

    sto_p->data_path = strdup(data_path);
    if (sto_p->data_path == NULL)
    {
        free(sto_p);
        *error_p = -TROVE_ENOMEM;
        return NULL;
    }
    sto_p->meta_path = strdup(meta_path);
    if (sto_p->meta_path == NULL)
    {
	free(sto_p->data_path);
	free(sto_p);
	*error_p = -TROVE_ENOMEM;
	return NULL;
    }
    sto_p->refct = 0;
    sto_p->flags = flags;

    DBPF_GET_STO_ATTRIB_DBNAME(path_name, PATH_MAX, meta_path);

    /* we want to stat the attrib db first in case it doesn't
     * exist but the storage directory does
     */
    if (stat(path_name, &sbuf) < 0) 
    {
        *error_p = -TROVE_ENOENT;
        return NULL;
    }

    sto_p->sto_attr_db = dbpf_db_open(path_name, NULL,
                                      error_p, NULL, 0);
    if (sto_p->sto_attr_db == NULL)
    {
        free(sto_p->meta_path);
		free(sto_p->data_path);
        free(sto_p);
        gossip_err("Failure opening attribute database\n");
                   
        my_storage_p = NULL;
        return NULL;
    }

    DBPF_GET_COLLECTIONS_DBNAME(path_name, PATH_MAX, meta_path);

    sto_p->coll_db = dbpf_db_open(path_name, NULL, 
                                  error_p, NULL, DB_RECNUM);
    if (sto_p->coll_db == NULL)
    {
        db_close(sto_p->sto_attr_db);
        free(sto_p->meta_path);
        free(sto_p->data_path);
        free(sto_p);
        gossip_err("Failure opening collection database\n");

        my_storage_p = NULL;
        return NULL;
    }

    my_storage_p = sto_p;
    return sto_p;
}

static int dbpf_mkpath(char *pathname, mode_t mode)
{
    int ret = -TROVE_EINVAL;
    int len, pos = 0, nullpos = 0, killed_slash;
    struct stat buf;

    len = strlen(pathname);

    /* require an absolute path */
    if (pathname[0] != '/')
    {
        return ret;
    }

    while (pos < len)
    {
        nullpos = pos;
        killed_slash = 0;

        while((pathname[nullpos] != '\0') &&
              (pathname[nullpos] != '/'))
        {
            nullpos++;
        }

        if (nullpos <= (pos + 1) && (nullpos != len))
        {
            /* extra slash or trailing slash; ignore */
            nullpos++;
            pos = nullpos;
        }
        else
        {
            if (pathname[nullpos] == '/')
            {
                killed_slash = 1;
                pathname[nullpos] = 0;
            }

            ret = stat(pathname, &buf);
            if ((ret == 0) && !S_ISDIR(buf.st_mode))
            {
                return -TROVE_ENOTDIR;
            }

            if (ret != 0)
            {
                ret = mkdir(pathname, mode);
                if (ret != 0)
                {
                    return -trove_errno_to_trove_error(errno);
                }
            }

            if (killed_slash)
            {
                pathname[nullpos] = '/';
            }

            nullpos++;
            pos = nullpos;
        }
    }
    return 0;
}

int db_open(DB *db_p, const char *dbname, int flags, int mode)
{
    int ret;

    db_open_count++;
    if ((ret = db_p->open(db_p,
#ifdef HAVE_TXNID_PARAMETER_TO_DB_OPEN
                          NULL,
#endif
                          dbname,
                          NULL,
                          TROVE_DB_TYPE,
                          flags,
                          mode)) != 0)
    {
        if(ret == EINVAL)
        {
            /* assume older db with DB_RECNUM flag set.  Set the flag
             * and try again */
            ret = db_p->set_flags(db_p, DB_RECNUM);
            if(ret != 0)
            {
                /* well, we tried.  ok nothing we can do */
                db_p->err(db_p, ret, "%s", dbname);
                return ret;
            }

            if ((ret = db_p->open(db_p,
#ifdef HAVE_TXNID_PARAMETER_TO_DB_OPEN
                                  NULL,
#endif
                                  dbname,
                                  NULL,
                                  TROVE_DB_TYPE,
                                  flags,
                                  mode)) != 0)
            {
                db_p->err(db_p, ret, "%s", dbname);
                return ret;
            }
        }
    }
    return 0;
}

int db_close(DB *db_p)
{
    int ret;

    db_close_count++;
    if ((ret = db_p->close(db_p, 0)) != 0)
    {
        gossip_lerr("db_close: %s\n", db_strerror(ret));
        return ret;
    }
    return 0;
}

/* Internal function for creating first instances of the databases for
 * a db plus files storage region.
 */
static int dbpf_db_create(char *dbname,
                          DB_ENV *envp,
                          uint32_t flags)
{
    int ret = -TROVE_EINVAL;
    DB *db_p = NULL;

    if ((ret = db_create(&db_p, envp, 0)) != 0)
    {
        gossip_lerr("dbpf_storage_create: %s\n", db_strerror(ret));
        return -dbpf_db_error_to_trove_error(ret);
    }

    if(flags)
    {
        ret = db_p->set_flags(db_p, flags);
    }

    if ((ret = db_open(db_p, dbname, TROVE_DB_CREATE_FLAGS, TROVE_DB_MODE)) != 0)
    {
        db_p->err(db_p, ret, "%s", dbname);
        db_close(db_p);
        return -dbpf_db_error_to_trove_error(ret);
    }

    if ((ret = db_close(db_p)) != 0)
    {
        gossip_lerr("dbpf_storage_create: %s\n", db_strerror(ret));
        return -dbpf_db_error_to_trove_error(ret);
    }
    return 0;
}


/* dbpf_db_open()
 *
 * Internal function for opening the databases that are used to store
 * basic information on a storage region.
 *
 * Returns NULL on error, passing a trove error type back in the
 * integer pointed to by error_p.
 */
static DB *dbpf_db_open(
    char *dbname, DB_ENV *envp, int *error_p,
    int (*compare_fn) (DB *db, const DBT *dbt1, const DBT *dbt2),
    uint32_t flags)
{
    int ret = -TROVE_EINVAL;
    DB *db_p = NULL;

    if ((ret = db_create(&db_p, envp, 0)) != 0)
    {
        *error_p = -dbpf_db_error_to_trove_error(ret);
        return NULL;
    }

    db_p->set_errpfx(db_p, "TROVE:DBPF:Berkeley DB");

    if(compare_fn)
    {
        db_p->set_bt_compare(db_p, compare_fn);
    }

    if (flags && (ret = db_p->set_flags(db_p, flags)) != 0)
    {
        db_p->err(db_p, ret, "%s: set_flags", dbname);
        *error_p = -dbpf_db_error_to_trove_error(ret);
        db_close(db_p);
        return NULL;
    }

    if ((ret = db_open(db_p, dbname, TROVE_DB_OPEN_FLAGS, 0)) != 0) 
    {
        *error_p = -dbpf_db_error_to_trove_error(ret);
        db_close(db_p);
        return NULL;
    }
    return db_p;
}

static void dbpf_db_error_callback(
#ifdef HAVE_DBENV_PARAMETER_TO_DB_ERROR_CALLBACK
    const DB_ENV *dbenv, 
#endif
    const char *errpfx, 
#ifdef HAVE_CONST_THIRD_PARAMETER_TO_DB_ERROR_CALLBACK
    const 
#endif
    char *msg)
{
    gossip_err("%s: %s\n", errpfx, msg);
}

/* dbpf_mgmt_direct_ops
 *
 * Structure holding pointers to all the management operations
 * functions for this storage interface implementation.
 */
struct TROVE_mgmt_ops dbpf_mgmt_direct_ops =
{
    dbpf_direct_initialize,
    dbpf_direct_finalize,
    dbpf_storage_create,
    dbpf_storage_remove,
    dbpf_collection_create,
    dbpf_collection_remove,
    dbpf_direct_collection_lookup,
    dbpf_direct_collection_clear,
    dbpf_collection_iterate,
    dbpf_collection_setinfo,
    dbpf_collection_getinfo,
    dbpf_collection_seteattr,
    dbpf_collection_geteattr,
    dbpf_collection_deleattr
};

/* dbpf_mgmt_ops
 *
 * Structure holding pointers to all the management operations
 * functions for this storage interface implementation.
 */
struct TROVE_mgmt_ops dbpf_mgmt_ops =
{
    dbpf_initialize,
    dbpf_finalize,
    dbpf_storage_create,
    dbpf_storage_remove,
    dbpf_collection_create,
    dbpf_collection_remove,
    dbpf_collection_lookup,
    dbpf_collection_clear,
    dbpf_collection_iterate,
    dbpf_collection_setinfo,
    dbpf_collection_getinfo,
    dbpf_collection_seteattr,
    dbpf_collection_geteattr,
    dbpf_collection_deleattr
};

typedef struct
{
    enum dbpf_op_type op_type;
    char op_type_str[32];
} __dbpf_op_type_str_map_t;

static __dbpf_op_type_str_map_t s_dbpf_op_type_str_map[] =
{
    { BSTREAM_READ_AT, "BSTREAM_READ_AT" },
    { BSTREAM_WRITE_AT, "BSTREAM_WRITE_AT" },
    { BSTREAM_RESIZE, "BSTREAM_RESIZE" },
    { BSTREAM_READ_LIST, "BSTREAM_READ_LIST" },
    { BSTREAM_WRITE_LIST, "BSTREAM_WRITE_LIST" },
    { BSTREAM_VALIDATE, "BSTREAM_VALIDATE" },
    { BSTREAM_FLUSH, "BSTREAM_FLUSH" },
    { KEYVAL_READ, "KEYVAL_READ" },
    { KEYVAL_WRITE, "KEYVAL_WRITE" },
    { KEYVAL_REMOVE_KEY, "KEYVAL_REMOVE_KEY" },
    { KEYVAL_VALIDATE, "KEYVAL_VALIDATE" },
    { KEYVAL_ITERATE, "KEYVAL_ITERATE" },
    { KEYVAL_ITERATE_KEYS, "KEYVAL_ITERATE_KEYS" },
    { KEYVAL_READ_LIST, "KEYVAL_READ_LIST" },
    { KEYVAL_WRITE_LIST, "KEYVAL_WRITE_LIST" },
    { KEYVAL_FLUSH, "KEYVAL_FLUSH" },
    { KEYVAL_GET_HANDLE_INFO, "KEYVAL_GET_HANDLE_INFO" },
    { DSPACE_CREATE, "DSPACE_CREATE" },
    { DSPACE_REMOVE, "DSPACE_REMOVE" },
    { DSPACE_ITERATE_HANDLES, "DSPACE_ITERATE_HANDLES" },
    { DSPACE_VERIFY, "DSPACE_VERIFY" },
    { DSPACE_GETATTR, "DSPACE_GETATTR" },
    { DSPACE_SETATTR, "DSPACE_SETATTR" },
    { DSPACE_GETATTR_LIST, "DSPACE_GETATTR_LIST" },
    { DSPACE_CREATE_LIST, "DSPACE_CREATE_LIST" },
    { DSPACE_REMOVE_LIST, "DSPACE_REMOVE_LIST" }
    /* NOTE: this list should be kept in sync with enum dbpf_op_type 
     * from dbpf.h 
     */ 
};

char *dbpf_op_type_to_str(enum dbpf_op_type op_type)
{
    int i = 0;
    char *ret = NULL;
    static const int num_elems = (sizeof(s_dbpf_op_type_str_map) /
                                  sizeof(__dbpf_op_type_str_map_t));

    for(i = 0; i < num_elems; i++)
    {
        if (op_type == s_dbpf_op_type_str_map[i].op_type)
        {
            ret = s_dbpf_op_type_str_map[i].op_type_str;
            break;
        }
    }
    return ret;
}

static void unlink_db_cache_files(const char* path)
{
    char* db_region_file = NULL;
    glob_t pglob;
    int ret;
    int i;
    
    db_region_file = malloc(PATH_MAX);
    if(!db_region_file)
    {
        return;
    }

    snprintf(db_region_file, PATH_MAX, "%s/__db.???", path);

    ret = glob(db_region_file, 0, NULL, &pglob);
    if(ret == 0)
    {
        for(i=0; i<pglob.gl_pathc; i++)
        {
            gossip_debug(GOSSIP_TROVE_DEBUG, "Unlinking old db cache file: %s\n", pglob.gl_pathv[i]); unlink(pglob.gl_pathv[i]);   
        }
        globfree(&pglob);
    }
    free(db_region_file);

    return;
}

static int PINT_dbpf_io_completion_callback(PINT_context_id ctx_id,
                                     int count,
                                     PINT_op_id *op_ids,
                                     void **user_ptrs,
                                     PVFS_error *errors)
{
    int i;
    dbpf_queued_op_t *qop_p;

    for(i = 0; i < count; ++i)
    {
        if(errors[i] == PINT_MGMT_OP_COMPLETED)
        {
            qop_p = (dbpf_queued_op_t *)(user_ptrs[i]);
            dbpf_queued_op_complete(qop_p, OP_COMPLETED);
        }
    }

    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
