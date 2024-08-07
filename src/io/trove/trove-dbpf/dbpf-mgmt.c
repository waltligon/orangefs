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
#include "gossip.h"
#include "dbpf-open-cache.h"
#include "pint-util.h"
#include "dbpf-sync.h"

#include "server-config.h"

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

struct dbpf_storage *my_storage_p = NULL; /* what is this - should be global? */
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

static int dbpf_mkpath(char *pathname, mode_t mode);

static int dbpf_db_create(char *dbname);

struct server_configuration_s *server_cfg=NULL;
struct filesystem_configuration_s *cfg_fs=NULL;

#define COLL_ENV_FLAGS (DB_INIT_MPOOL | DB_CREATE | DB_THREAD)

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

                //return 1;
                return 0;
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
/* NEXT */
#if 0
            ret = trove_set_handle_ranges(
                coll_id, context_id, (char *)parameter);
#endif 
            break;
        case TROVE_COLLECTION_HANDLE_TIMEOUT:
            gossip_debug(GOSSIP_TROVE_DEBUG, 
                         "dbpf collection %d - Setting handle timeout to "
                         "%ld microseconds\n",
                         (int) coll_id, 
                         (long)((((struct timeval *)parameter)->tv_sec * 1e6) +
                                ((struct timeval *)parameter)->tv_usec));
/* NEXT */
#if 0
            ret = trove_set_handle_timeout(
                coll_id, context_id, (struct timeval *)parameter);
#endif
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

static int dbpf_collection_set_fs_config(TROVE_method_id method_id,
        TROVE_coll_id coll_id, struct server_configuration_s *cfg)
{
    PINT_llist *cur = cfg->file_systems;
    struct filesystem_configuration_s *cur_fs = PINT_llist_head(cur);

    /* set filesystem configuration pointer */
    cfg_fs = NULL;
    do
    {
       if (cur_fs->coll_id == coll_id)
       {
          cfg_fs = cur_fs;
          break;
       }
       cur = PINT_llist_next(cur);
       cur_fs = PINT_llist_head(cur);
    }
    while(cur_fs != NULL);

    server_cfg = cfg;

    return 0;
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
    struct dbpf_data key, val;

    sto_p = my_storage_p;
    if (sto_p == NULL)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: my_storage_p is NULL\n", __func__);
        return ret;
    }

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG,
        "%s: collection_find_registered returns NULL\n", __func__);
        return ret;
    }

    key.data = key_p->buffer;
    key.len = key_p->buffer_sz;
    val.data = val_p->buffer;
    val.len = val_p->buffer_sz;

    ret = dbpf_db_put(coll_p->coll_attr_db, &key, &val);
    if (ret != 0)
    {
        gossip_err("%s: db_put fail: %s\n", __func__, strerror(ret));
        return ret;
    }

    ret = dbpf_db_sync(coll_p->coll_attr_db);
    if (ret != 0)
    {
        gossip_err("%s: db_sync fail: %s\n", __func__, strerror(ret));
        return ret;
    }

    /* 1 indicates a complete op, 0 is busy, <0 is error */
    /* this always completes */
    return 1;
    //return 0;
}

int dbpf_collection_geteattr(TROVE_coll_id coll_id,
                             TROVE_keyval_s *key_p,
                             TROVE_keyval_s *val_p,
                             TROVE_ds_flags flags,
                             void *user_ptr,
                             TROVE_context_id context_id,
                             TROVE_op_id *out_op_id_p)
{
    struct dbpf_collection *coll_p;
    struct dbpf_data key, data;
    int ret;

    if (!my_storage_p)
    {
        return -TROVE_EINVAL;
    }

    coll_p = dbpf_collection_find_registered(coll_id);
    if (!coll_p)
    {
        return -TROVE_EINVAL;
    }

    key.data = key_p->buffer;
    key.len = key_p->buffer_sz;

    data.data = val_p->buffer;
    data.len = val_p->buffer_sz;

    ret = dbpf_db_get(coll_p->coll_attr_db, &key, &data);
    if (ret != 0)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG,
            "dbpf_collection_geteattr: %s\n", strerror(ret));
        return -ret;
    }

    val_p->read_sz = data.len;
    //return 1;
    return 0;
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
    struct dbpf_data key;

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

    key.data = key_p->buffer;
    key.len = key_p->buffer_sz;

    ret = dbpf_db_del(coll_p->coll_attr_db, &key);
    if (ret != 0)
    {
        gossip_lerr("%s: %s\n", __func__, strerror(ret));
        return -ret;
    }

    ret = dbpf_db_sync(coll_p->coll_attr_db);
    if (ret != 0)
    {
        gossip_lerr("%s: %s\n", __func__, strerror(ret));
        return -ret;
    }

    //return 1;
    return 0;
}

static int dbpf_initialize(char *data_path,
			   char *meta_path,
			   char *config_path,
                           TROVE_ds_flags flags)
{
    int ret = -TROVE_EINVAL;
    struct dbpf_storage *sto_p = NULL;

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: initializing events\n", __func__);
    /* initialize events */
/* V3 move all of this event def stuff into its own function
 * suggest PINT_event_define_init
 * just to clean this function up a bit
 */
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
    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: checking path vars exist\n", __func__);

    if (!data_path)
    {
        gossip_err("%s: invalid data storage path\n", __func__);
        return ret;
    }

    if (!meta_path)
    {
	gossip_err("%s: invalid metadata storage path\n", __func__);
	return ret;
    }

    if (!config_path)
    {
	gossip_err("%s: invalid config storage path\n", __func__);
	return ret;
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: calling dbpf_storage_lookup\n", __func__);

    sto_p = dbpf_storage_lookup(data_path, meta_path, config_path, &ret, flags);
    if (sto_p == NULL)
    {
        char emsg[256];

        PVFS_strerror_r(ret, emsg, 256);
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: dbpf_storage_lookup error ret:%d(%s)\n",
                     __func__, ret, emsg);
        return -TROVE_ENOENT;
    }

    my_storage_p = sto_p;

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: initialize open cache\n", __func__);
    dbpf_open_cache_initialize();

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: initialize thread\n", __func__);
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
				  char *config_path,
				  TROVE_ds_flags flags)
{
    int ret;

    /* some parts of initialization are shared with other methods */
    ret = dbpf_initialize(data_path, meta_path, config_path, flags);
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
            ret = dbpf_db_sync(my_storage_p->sto_attr_db);
            if (ret)
            {
                gossip_err("dbpf_finalize attr sync: %s\n", strerror(ret));
                return -ret;
            }

            ret = dbpf_db_close(my_storage_p->sto_attr_db);
            if (ret)
            {
                gossip_err("dbpf_finalize attr close: %s\n", strerror(ret));
                return -ret;
            }
        }
        else
        {
            gossip_err("dbpf_finalize: attribute database not defined\n");
        }

        if( my_storage_p->coll_db )
        {
            ret = dbpf_db_sync(my_storage_p->coll_db);
            if (ret)
            {
                gossip_err("dbpf_finalize collection sync: %s\n", 
                           strerror(ret));
                return -ret;
            }
    
            ret = dbpf_db_close(my_storage_p->coll_db);
            if (ret)
            {
                gossip_err("dbpf_finalize collection close: %s\n", 
                           strerror(ret));
                return -ret;
            }
        }
        else
        {
            gossip_err("dbpf_finalize: collections database not defined\n");
        } 
        free(my_storage_p->data_path);
        free(my_storage_p->meta_path);
        free(my_storage_p->config_path);
        free(my_storage_p);
        my_storage_p = NULL;
    }

    //return 1;
    return 0;
}

/* Creates and initializes the databases needed for a dbpf storage
 * space.  This includes:
 * - creating the path to the storage directory
 * - creating storage attribute database, propagating with create time
 * - creating collections database, filling in create time
 */
int dbpf_storage_create(char *data_path,
			char *meta_path,
			char *config_path,
                        void *user_ptr,
                        TROVE_op_id *out_op_id_p)
{
    int ret = -TROVE_EINVAL;
    char data_dirname[PATH_MAX] = {0};
    char meta_dirname[PATH_MAX] = {0};
    char config_dirname[PATH_MAX] = {0};
    char sto_attrib_dbname[PATH_MAX] = {0};
    char collections_dbname[PATH_MAX] = {0};

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: checking for paths\n", __func__);

    DBPF_GET_DATA_DIRNAME(data_dirname, PATH_MAX, data_path);
    ret = dbpf_mkpath(data_dirname, 0755);
    if (ret != 0)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: error - data path\n", __func__);
        return ret;
    }

    DBPF_GET_META_DIRNAME(meta_dirname, PATH_MAX, meta_path);
    ret = dbpf_mkpath(meta_dirname, 0755);
    if (ret != 0)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: error - meta path\n", __func__);
	return ret;
    }

    DBPF_GET_CONFIG_DIRNAME(config_dirname, PATH_MAX, config_path);
    ret = dbpf_mkpath(config_dirname, 0755);
    if (ret != 0)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: error - config path\n", __func__);
	return ret;
    }

    DBPF_GET_STO_ATTRIB_DBNAME(sto_attrib_dbname, PATH_MAX, meta_path);
    ret = dbpf_db_create(sto_attrib_dbname);
    if (ret != 0)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: error - meta attribs\n", __func__);
        return ret;
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: calling dbpf_db_create\n", __func__);
    DBPF_GET_COLLECTIONS_DBNAME(collections_dbname, PATH_MAX, meta_path);
    ret = dbpf_db_create(collections_dbname);
    if (ret != 0)
    {
        gossip_lerr("%s: removing storage attribute database after failed create attempt",
                    __func__);
        unlink(sto_attrib_dbname);
        return ret;
    }

    //return 1;
    return 0;
}

int dbpf_storage_remove(char *data_path,
                        char *meta_path,
                        char *config_path,
                        void *user_ptr,
                        TROVE_op_id *out_op_id_p)
{
    int ret = -TROVE_EINVAL;
    char path_name[PATH_MAX] = {0};

    if (my_storage_p) {
        dbpf_db_close(my_storage_p->sto_attr_db);
        dbpf_db_close(my_storage_p->coll_db);
        free(my_storage_p->config_path);
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

    DBPF_GET_CONFIG_DIRNAME(path_name, PATH_MAX, config_path);
    gossip_debug(GOSSIP_TROVE_DEBUG, "Removing %s\n", path_name);
    if (rmdir(path_name) != 0)
    {
        perror("failure removing config directory");
        ret = -trove_errno_to_trove_error(errno);
#if 0
        ret = -trove_errno_to_trove_error(errno);
        goto storage_remove_failure;
#endif
    }

    DBPF_GET_META_DIRNAME(path_name, PATH_MAX, meta_path);
    gossip_debug(GOSSIP_TROVE_DEBUG, "Removing %s\n", path_name);
    if (rmdir(path_name) != 0)
    {
        perror("failure removing metadata directory");
        ret = -trove_errno_to_trove_error(errno);
#if 0
        ret = -trove_errno_to_trove_error(errno);
        goto storage_remove_failure;
#endif
    }

    DBPF_GET_DATA_DIRNAME(path_name, PATH_MAX, data_path);
    gossip_debug(GOSSIP_TROVE_DEBUG, "Removing %s\n", path_name);
    if (rmdir(path_name) != 0)
    {
        perror("failure removing data directory");
        ret = -trove_errno_to_trove_error(errno);
#if 0
        ret = -trove_errno_to_trove_error(errno);
        goto storage_remove_failure;
#endif
    }

    if (ret < DBPF_SUCCESS)
    {
        return ret;
    }

    gossip_debug(GOSSIP_TROVE_DEBUG,
                 "All databases for storage space removed.\n");

    //return 1;
    return DBPF_SUCCESS;

storage_remove_failure:
    return ret;
}

/*
 * 1) check collections database to see if coll_id is already used
 *    (error out if so)
 * 2) create collection attribute database
 * 3) store trove-dbpf version in collection attribute database
 * 4) store last handle value in collection attribute database
 * 5) create dataspace attributes database
 * 6) create keyval and bstream directories
 */
int dbpf_collection_create(char *collname,
                           TROVE_coll_id new_coll_id,
                           void *user_ptr,
                           TROVE_op_id *out_op_id_p)
{
    int ret = -TROVE_EINVAL, i = 0;
    /* V3 no longer needed */
    /* TROVE_handle zero = TROVE_HANDLE_NULL; */
    struct dbpf_storage *sto_p;
    struct dbpf_collection_db_entry db_data;
    dbpf_db *db_p = NULL;
    struct dbpf_data key, data;
    struct stat dirstat;
    struct stat dbstat;
    char path_name[PATH_MAX] = {0}, dir[PATH_MAX] = {0};

    if (my_storage_p == NULL)
    {
        gossip_err("%s: Invalid storage name specified\n", __func__);
        return ret;
    }
    sto_p = my_storage_p;

    memset(&key, 0, sizeof(struct dbpf_data));
    memset(&data, 0, sizeof(struct dbpf_data));
    memset(&db_data, 0, sizeof(struct dbpf_collection_db_entry));

    key.data = collname;
    key.len = strlen(collname)+1;
    data.data = &db_data;
    data.len = sizeof(struct dbpf_collection_db_entry);

    /* ensure that the collection record isn't already there */
    /* ===>we did this already in dbpf_collection_lookup<=== */
    //gossip_debug(GOSSIP_TROVE_DEBUG, "%s: calling dbpf_db_get to ensure "
    //             "collection does not exist\n", __func__);
    //ret = dbpf_db_get(sto_p->coll_db, &key, &data);
    //if (ret != TROVE_ENOENT)
    //if (ret != -TROVE_ENOENT)
    //{
    //    gossip_debug(GOSSIP_TROVE_DEBUG,
    //          "%s: collection %s already exists with coll_id %d, len = %zu.\n",
    //          __func__, collname, db_data.coll_id, data.len);
    //    //return -ret;
    //    return ret;
    //}

    memset(&key, 0, sizeof(struct dbpf_data));
    memset(&data, 0, sizeof(struct dbpf_data));
    memset(&db_data, 0, sizeof(struct dbpf_collection_db_entry));

    db_data.coll_id = new_coll_id;

    key.data = collname;
    key.len = strlen(collname)+1;
    data.data = &db_data;
    data.len = sizeof(struct dbpf_collection_db_entry);

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: calling dbpf_db_put collection\n",
                __func__);

    ret = dbpf_db_put(sto_p->coll_db, &key, &data);

    if (ret != DBPF_SUCCESS)
    {
        gossip_err("%s: dbpf_db_put failed:%s\n", __func__, strerror(ret));
        //return -ret;
        return ret;
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: calling dbpf_db_sync collection\n",
                 __func__);
    ret = dbpf_db_sync(sto_p->coll_db);
    if (ret != DBPF_SUCCESS)
    {
        gossip_err("%s: db_db_sync failed:%s\n", __func__, strerror(ret));
        //return -ret;
        return ret;
    }

    DBPF_GET_DATA_DIRNAME(path_name, PATH_MAX, sto_p->data_path);

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: stat data_path(%s)\n", __func__, path_name);
    ret = stat(path_name, &dirstat);
    if (ret < DBPF_SUCCESS && errno != ENOENT)
    {
        gossip_err("%s: stat failed on data directory %s\n",
                   __func__, path_name);
        return -trove_errno_to_trove_error(errno);
    }
    else if (ret < DBPF_SUCCESS)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: mkdir data_path (%s)\n", __func__, path_name);
        ret = mkdir(path_name, 0755);
        if (ret != DBPF_SUCCESS)
        {
            gossip_err("%s: mkdir failed on data directory %s\n",
                       __func__, path_name);
            return -trove_errno_to_trove_error(errno);
        }
    }

    DBPF_GET_META_DIRNAME(path_name, PATH_MAX, sto_p->meta_path);

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: stat meta_path (%s)\n", __func__, path_name);
    ret = stat(path_name, &dirstat);
    if (ret < DBPF_SUCCESS && errno != ENOENT)
    {
        gossip_err("%s: stat failed on metadata directory %s\n",
                   __func__, path_name);
        return -trove_errno_to_trove_error(errno);
    }
    else if (ret < DBPF_SUCCESS)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: mkdir meta_path (%s)\n", __func__, path_name);
        ret = mkdir(path_name, 0755);
        if (ret != DBPF_SUCCESS)
        {
            gossip_err("%s: mkdir failed on metadata directory %s\n",
                       __func__, path_name);
            return -trove_errno_to_trove_error(errno);
        }
    }

    DBPF_GET_CONFIG_DIRNAME(path_name, PATH_MAX, sto_p->config_path);

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: stat config_path (%s)\n", __func__, path_name);
    ret = stat(path_name, &dirstat);
    if (ret < DBPF_SUCCESS && errno != ENOENT)
    {
        gossip_err("%s: stat failed on config directory %s\n",
                   __func__, path_name);
        return -trove_errno_to_trove_error(errno);
    }
    else if (ret < DBPF_SUCCESS)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: mkdir config_path (%s)\n", __func__, path_name);
        ret = mkdir(path_name, 0755);
        if (ret != DBPF_SUCCESS)
        {
            gossip_err("%s: mkdir failed on config directory %s\n",
                       __func__, path_name);
            return -trove_errno_to_trove_error(errno);
        }
    }

    DBPF_GET_COLL_DIRNAME(path_name, PATH_MAX, sto_p->data_path, new_coll_id);

    gossip_debug(GOSSIP_TROVE_DEBUG,
                 "%s: mkdir on data directory %s\n", __func__, path_name);
    ret = mkdir(path_name, 0755);
    /* does strcmp make sense here? */
    if (ret != DBPF_SUCCESS /*&& strcmp(sto_p->data_path, sto_p->meta_path)*/)
    {
        gossip_err("%s: mkdir failed on data collection directory %s\n", 
                   __func__, path_name);
        return -trove_errno_to_trove_error(errno);
    }

    DBPF_GET_COLL_DIRNAME(path_name, PATH_MAX, sto_p->meta_path, new_coll_id);

    gossip_debug(GOSSIP_TROVE_DEBUG,
                 "%s: mkdir on meta directory %s\n", __func__, path_name);
    ret = mkdir(path_name, 0755);
    if (ret != DBPF_SUCCESS /*&& strcmp(sto_p->meta_path, sto_p->data_path)*/)
    {
	gossip_err("%s: mkdir failed on metadata collection directory %s\n",
                   __func__, path_name);
	return -trove_errno_to_trove_error(errno);
    }

    DBPF_GET_COLL_DIRNAME(path_name, PATH_MAX, sto_p->config_path, new_coll_id);

    gossip_debug(GOSSIP_TROVE_DEBUG,
                 "%s: mkdir on config directory %s\n", __func__, path_name);
    ret = mkdir(path_name, 0755);
    if (ret != DBPF_SUCCESS /*&& strcmp(sto_p->config_path, sto_p->meta_path)*/)
    {
	gossip_err("%s: mkdir failed on config collection directory %s\n",
                   __func__, path_name);
	return -trove_errno_to_trove_error(errno);
    }

    DBPF_GET_COLL_ATTRIB_DBNAME(path_name,
                                PATH_MAX,
                                sto_p->meta_path,
                                new_coll_id);

    ret = stat(path_name, &dbstat);
    if(ret < DBPF_SUCCESS && errno != ENOENT)
    {
        gossip_err("%s: failed to stat db file: %s\n", __func__, path_name);
        return -trove_errno_to_trove_error(errno);
    }
    if(ret < DBPF_SUCCESS)
    {
        /* errno should equal ENOENT */
	ret = dbpf_db_create(path_name);
        if (ret != 0)
        {
            gossip_err("%s: dbpf_db_create failed on attrib db %s\n", __func__, path_name);
            return ret;
        }
    }

    ret = dbpf_db_open(path_name, 0, &db_p, 0, server_cfg);
    if (ret)
    {
        gossip_err("%s: dbpf_db_open failed on attrib db %s\n", __func__, path_name);
        return ret;
    }

    /*
     * store trove-dbpf version string in the collection.  this is used
     * to know what format the metadata is stored in on disk.
     */
    key.data = TROVE_DBPF_VERSION_KEY;
    key.len = strlen(TROVE_DBPF_VERSION_KEY) +1;
    data.data = TROVE_DBPF_VERSION_VALUE;
    data.len = strlen(TROVE_DBPF_VERSION_VALUE) +1;

    ret = dbpf_db_put(db_p, &key, &data);
    if (ret != 0)
    {
        char emsg[256];
        PVFS_strerror_r(ret, emsg, 256);
        gossip_err("%s: dbpf_db_put failed writing trove-dbpf version string "
                   "ret = %d(%s)\n", __func__, ret, emsg);
        //return -ret;
        return ret;
    }

    gossip_debug(GOSSIP_TROVE_DEBUG,
        "%s: wrote trove-dbpf version %s to collection attribute database\n",
        __func__, TROVE_DBPF_VERSION_VALUE);

/* V3 no longer need or want last handle field */
/* figure out how to get rid of this but leave version */

#if 0
    /* store initial handle value */
    key.data = LAST_HANDLE_STRING;
    key.len = sizeof(LAST_HANDLE_STRING);
    data.data = &zero;
    data.len = sizeof(zero);

    ret = dbpf_db_put(db_p, &key, &data);
    if (ret != 0)
    {
        gossip_err("db_p->put failed writing initial handle value: %s\n",
                   strerror(ret));
        return -ret;
    }
#endif
    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: calling dbpf_db_sync\n", __func__);
    dbpf_db_sync(db_p);

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: calling dbpf_db_close\n", __func__);
    dbpf_db_close(db_p);

    DBPF_GET_DS_ATTRIB_DBNAME(path_name,
                              PATH_MAX,
                              sto_p->meta_path, 
			      new_coll_id);

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: calling stat meta_path 1\n", __func__);
    ret = stat(path_name, &dbstat);
    if(ret < DBPF_SUCCESS && errno != ENOENT)
    {
        gossip_err("%s: failed to stat ds attrib db: %s\n", __func__, path_name);
        return -trove_errno_to_trove_error(errno);
    }
    if(ret < 0)
    {
        /* errno should equal ENOENT */
        ret = dbpf_db_create(path_name);
        if (ret != DBPF_SUCCESS)
        {
            gossip_err("%s: dbpf_db_create failed on %s\n", __func__, path_name);
            return ret;
        }
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: calling DBPF_GET_KEYVAL_DBNAME\n", __func__);
    DBPF_GET_KEYVAL_DBNAME(path_name, PATH_MAX, sto_p->meta_path, new_coll_id);

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: calling stat meta_path 2\n", __func__);
    ret = stat(path_name, &dbstat);
    if(ret < DBPF_SUCCESS && errno != ENOENT)
    {
        gossip_err("%s: failed to stat keyval db: %s\n", __func__, path_name);
        return -trove_errno_to_trove_error(errno);
    }
    if(ret < DBPF_SUCCESS)
    {
        ret = dbpf_db_create(path_name);
        if (ret != DBPF_SUCCESS)
        {
            gossip_err("%s: dbpf_db_create failed on %s\n", __func__, path_name);
            return ret;
        }
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: calling DBPF_GET_BSTREAM_DIRNAME\n", __func__);
    DBPF_GET_BSTREAM_DIRNAME(path_name,
                             PATH_MAX,
                             sto_p->data_path,
			     new_coll_id);

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: calling mkdir data_path 1\n", __func__);
    ret = mkdir(path_name, 0755);
    if(ret != DBPF_SUCCESS)
    {
        gossip_err("%s: mkdir failed on bstream directory %s\n", __func__, path_name);
        return -trove_errno_to_trove_error(errno);
    }

    for(i = 0; i < DBPF_BSTREAM_MAX_NUM_BUCKETS; i++)
    {
        ret = snprintf(dir, PATH_MAX, "%s/%.8d", path_name, i);
        if (ret < DBPF_SUCCESS)
        {
           gossip_err("%s: snprintf output failure on dir, i:%d:\n",
                      __func__, i);
           return(ret);
        }
        if ((mkdir(dir, 0755) == -1) && (errno != EEXIST))
        {
            gossip_err("%s: mkdir failed on bstream bucket directory %s\n",
                       __func__, dir);
            return -trove_errno_to_trove_error(errno);
        }
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: calling DBPF_GET_STRANDED_BSTREAM_DIRNAME\n", __func__);
    DBPF_GET_STRANDED_BSTREAM_DIRNAME(path_name,
                                      PATH_MAX,
                                      sto_p->data_path,
                                      new_coll_id);

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: calling mkdir data_path 2\n", __func__);
    ret = mkdir(path_name, 0755);
    if(ret != 0)
    {
        gossip_err("%s: mkdir failed on bstream directory %s\n", __func__, path_name);
        return -trove_errno_to_trove_error(errno);
    }

    /* return success */
    gossip_debug(GOSSIP_TROVE_DEBUG,"%s: exiting with error status 0\n", __func__);
    //return 1;
    return DBPF_SUCCESS;
}

int dbpf_collection_remove(char *collname,
                           void *user_ptr,
                           TROVE_op_id *out_op_id_p)
{
    char path_name[PATH_MAX];
    struct dbpf_storage *sto_p = NULL;
    struct dbpf_collection_db_entry db_data;
    struct dbpf_data key, data;
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
    key.len = strlen(collname) + 1;
    data.data = &db_data;
    data.len = sizeof(db_data);

    ret = dbpf_db_get(sto_p->coll_db, &key, &data);
    if (ret != DBPF_SUCCESS)
    {
        gossip_err("TROVE:DBPF: dbpf_db_get collection\n");
        return -ret;
    }

    ret = dbpf_db_del(sto_p->coll_db, &key);
    if (ret != DBPF_SUCCESS)
    {
        gossip_err("TROVE:DBPF: dbpf_db_del");
        return -ret;
    }

    ret = dbpf_db_sync(sto_p->coll_db);
    if (ret != DBPF_SUCCESS)
    {
        gossip_err("TROVE:DBPF: dbpf_db_sync");
        return -ret;
    }

    if ((db_collection = dbpf_collection_find_registered(db_data.coll_id)) == NULL) 
    {
        ret = -TROVE_ENOENT;
    }
    else {
        /* Clean up properly by closing all db handles */
        dbpf_db_close(db_collection->coll_attr_db);
        dbpf_db_close(db_collection->ds_db);
        dbpf_db_close(db_collection->keyval_db);
        dbpf_collection_deregister(db_collection);
        free(db_collection->name);
        free(db_collection->meta_path);
        free(db_collection->data_path);
        PINT_dbpf_keyval_pcache_finalize(db_collection->pcache);
        free(db_collection);
    }

    DBPF_GET_DS_ATTRIB_DBNAME(path_name,
                              PATH_MAX,
                              sto_p->meta_path,
                              db_data.coll_id);

    if (unlink(path_name) != DBPF_SUCCESS)
    {
        gossip_err("failure removing dataspace attrib db\n");
        ret = -trove_errno_to_trove_error(errno);
    }

    DBPF_GET_KEYVAL_DBNAME(path_name,
                           PATH_MAX,
                           sto_p->meta_path,
                           db_data.coll_id);

    if(unlink(path_name) != DBPF_SUCCESS)
    {
        gossip_err("failure removing keyval db\n");
        ret = -trove_errno_to_trove_error(errno);
    }

    DBPF_GET_COLL_ATTRIB_DBNAME(path_name,
                                PATH_MAX,
                                sto_p->meta_path,
                                db_data.coll_id);

    if (unlink(path_name) != DBPF_SUCCESS)
    {
        gossip_err("failure removing collection attrib db\n");
        ret = -trove_errno_to_trove_error(errno);
    }

    DBPF_GET_BSTREAM_DIRNAME(path_name,
                             PATH_MAX,
                             sto_p->data_path,
                             db_data.coll_id);

    for(i = 0; i < DBPF_BSTREAM_MAX_NUM_BUCKETS; i++)
    {
        ret = snprintf(dir, PATH_MAX, "%s/%.8d", path_name, i);
        if (ret < 0)
        {
           gossip_err("%s: snprintf output failure on dir, i:%d:\n",
                      __func__, i);
           return(ret);
        }

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
                ret = snprintf(tmp_path, PATH_MAX, "%s/%s", dir,
                               current_dirent->d_name);
                if (ret < 0)
                {
                   gossip_err("%s: snprintf output failure on dir, i:%d:\n",
                              __func__, i);
                   return(ret);
                }
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

    DBPF_GET_STRANDED_BSTREAM_DIRNAME(path_name,
                                      PATH_MAX,
                                      sto_p->data_path,
                                      db_data.coll_id);

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

            ret = snprintf(tmp_path,
                           PATH_MAX,
                           "%s/%s",
                           path_name,
                           current_dirent->d_name);

            if (ret == DBPF_SUCCESS)
            {
               gossip_err("%s: snprintf output failure on dir, i:%d:\n",
                          __func__, i);
               return(ret);
            }

            if(stat(tmp_path, &file_info) < DBPF_SUCCESS)
            {
                gossip_err("error doing stat on bstream entry\n");
                ret = -trove_errno_to_trove_error(errno);
                closedir(current_dir);
                goto collection_remove_failure;
            }

            assert(S_ISREG(file_info.st_mode));
            if(unlink(tmp_path) != DBPF_SUCCESS)
            {
                gossip_err("failure removing bstream entry\n");
                ret = -trove_errno_to_trove_error(errno);
                closedir(current_dir);
                goto collection_remove_failure;
            }
        }
        closedir(current_dir);
    }

    if(rmdir(path_name) != DBPF_SUCCESS)
    {
        gossip_err("failure removing bstream directory %s\n", 
                   path_name);

        ret = -trove_errno_to_trove_error(errno);
        goto collection_remove_failure;
    }

    DBPF_GET_COLL_DIRNAME(path_name,
                          PATH_MAX,
                          sto_p->meta_path,
                          db_data.coll_id);

    if (rmdir(path_name) != DBPF_SUCCESS)
    {
	gossip_err("failure removing metadata collection directory\n");

	ret = -trove_errno_to_trove_error(errno);
	goto collection_remove_failure;
    }

    DBPF_GET_COLL_DIRNAME(path_name,
                          PATH_MAX,
                          sto_p->config_path,
                          db_data.coll_id);

    if (rmdir(path_name) != DBPF_SUCCESS)
    {
	gossip_err("failure removing configuration collection directory\n");

	ret = -trove_errno_to_trove_error(errno);
	goto collection_remove_failure;
    }

    DBPF_GET_COLL_DIRNAME(path_name,
                          PATH_MAX,
                          sto_p->data_path,
                          db_data.coll_id);

    if (rmdir(path_name) != DBPF_SUCCESS)
    {
        gossip_err("failure removing data collection directory\n");

        ret = -trove_errno_to_trove_error(errno);
    }
collection_remove_failure:
    return ret;
}

int dbpf_collection_iterate(TROVE_keyval_s *name_array,
                            TROVE_coll_id *coll_id_array,
                            int *inout_count_p,
                            TROVE_ds_flags flags,
                            TROVE_vtag_s *vtag,
                            void *user_ptr,
                            TROVE_op_id *out_op_id_p)
{
    int ret = -TROVE_EINVAL, i = 0;
    dbpf_cursor *dbc = NULL;
    struct dbpf_data key, data;
    struct dbpf_collection_db_entry db_entry;

    /* get a cursor */
    ret = dbpf_db_cursor(my_storage_p->coll_db, &dbc, 1);
    if (ret != DBPF_SUCCESS)
    {
        //ret = -ret;
        goto return_error;
    }

    for (i = 0; i < *inout_count_p; i++)
    {
        key.data = name_array[i].buffer;
        key.len = name_array[i].buffer_sz;

        data.data = &db_entry;
        data.len = sizeof(db_entry);

        ret = dbpf_db_cursor_get(dbc,
                                 &key,
                                 &data,
                                 DBPF_DB_CURSOR_NEXT,
                                 name_array[i].buffer_sz);
        if (ret == TROVE_ENOENT)
        {
            goto return_ok;
        }
        else if (ret != DBPF_SUCCESS)
        {
            //ret = -ret;
            goto return_error;
        }
        coll_id_array[i] = db_entry.coll_id;
    }

return_ok:

    *inout_count_p = i;
    ret = dbpf_db_cursor_close(dbc);

    if (ret != DBPF_SUCCESS)
    {
        //ret = -ret;
        goto return_error;
    }
    //return 1;
    return 0;

return_error:

    *inout_count_p = i;
    dbpf_db_cursor_close(dbc);

    gossip_lerr("dbpf_collection_iterate_op_svc: %s\n", strerror(ret));

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

    if (coll_p == NULL)
    {
       gossip_err("%s: Trove collection not defined.\n", __func__);
       return DBPF_SUCCESS;
    }

    if ((coll_p->coll_attr_db != NULL ) &&
        ((ret = dbpf_db_sync(coll_p->coll_attr_db)) != DBPF_SUCCESS))
    {
        char emsg[256];
        PVFS_strerror_r(ret, emsg, 256);
        gossip_err("%s: db_sync(coll_attr_db): %d(%s)\n", __func__, ret, emsg);
    }

    if ((coll_p->coll_attr_db != NULL ) &&
        ((ret = dbpf_db_close(coll_p->coll_attr_db)) != DBPF_SUCCESS))
    {
        char emsg[256];
        PVFS_strerror_r(ret, emsg, 256);
        gossip_err("%s: db_close(coll_attr_db): %d(%s)\n", __func__, ret, emsg);
    }

    if ((coll_p->ds_db != NULL ) &&
        ((ret = dbpf_db_sync(coll_p->ds_db)) != DBPF_SUCCESS))
    {
        char emsg[256];
        PVFS_strerror_r(ret, emsg, 256);
        gossip_err("%s: db_sync(coll_ds_db): %d(%s)\n", __func__, ret, emsg);
    }

    if ((coll_p->ds_db != NULL ) &&
        ((ret = dbpf_db_close(coll_p->ds_db)) != DBPF_SUCCESS))
    {
        char emsg[256];
        PVFS_strerror_r(ret, emsg, 256);
        gossip_err("%s: db_close(coll_ds_db): %d(%s)\n", __func__, ret, emsg);
    }

    if ((coll_p->keyval_db != NULL ) &&
        ((ret = dbpf_db_sync(coll_p->keyval_db)) != DBPF_SUCCESS))
    {
        char emsg[256];
        PVFS_strerror_r(ret, emsg, 256);
        gossip_err("%s: db_sync(coll_keyval_db): %d(%s)\n", __func__, ret, emsg);
    }

    if ((coll_p->keyval_db != NULL ) &&
        ((ret = dbpf_db_close(coll_p->keyval_db)) != DBPF_SUCCESS))
    {
        char emsg[256];
        PVFS_strerror_r(ret, emsg, 256);
        gossip_err("%s: db_close(coll_keyval_db): %d(%s)\n", __func__, ret, emsg);
    }

    free(coll_p->name);
    free(coll_p->data_path);
    free(coll_p->meta_path);
    PINT_dbpf_keyval_pcache_finalize(coll_p->pcache);

    free(coll_p);
    return DBPF_SUCCESS;
}

static int dbpf_direct_collection_lookup(char *collname,
                                         TROVE_coll_id *out_coll_id_p,
                                         void *user_ptr,
                                         TROVE_op_id *out_op_id_p)
{
    int ret;

    /* most of this is shared with the other methods */
    ret = dbpf_collection_lookup(collname,
                                 out_coll_id_p, 
                                 user_ptr,
                                 out_op_id_p);
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
    struct dbpf_collection *coll_p = NULL;
    struct dbpf_collection_db_entry db_data;
    struct dbpf_data dbpf_key, dbpf_val;
    char path_name[PATH_MAX];
    char trove_dbpf_version[32] = {0};
    int sto_major, sto_minor, sto_inc, major, minor, inc;

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: lookup of collection: %s\n", __func__, collname);

    if (!my_storage_p)
    {
        return -TROVE_EINVAL;
    }

    dbpf_key.data = collname;
    dbpf_key.len = strlen(collname)+1;
    dbpf_val.data = &db_data;
    dbpf_val.len = sizeof(db_data);

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: calling dbpf_db_get\n", __func__);

    ret = dbpf_db_get(my_storage_p->coll_db, &dbpf_key, &dbpf_val);

    if (ret != DBPF_SUCCESS)
    {
        if (ret != TROVE_ENOENT)
        {
            char emsg[256];
            PVFS_strerror_r(ret, emsg, 256);
            gossip_debug(GOSSIP_TROVE_DEBUG,
                         "%s: dbpf_db_get error %d(%s)\n", __func__, ret, emsg);
        }
        else
        {
            gossip_err("%s: dbpf_db_get collection gets ENOENT\n", __func__);
        }
        //return -ret;
        return ret;
    }

    /*
     * Look to see if we have already registered this collection; if so, return
     */
    gossip_debug(GOSSIP_TROVE_DEBUG,
                 "%s: dbpf_collection_find_registered\n", __func__);
    coll_p = dbpf_collection_find_registered(db_data.coll_id);

    if (coll_p != NULL)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "%s: dbpf_collection_find_registered found existing coll_id\n",
                     __func__);
        *out_coll_id_p = coll_p->coll_id;
        //return 1;
        return DBPF_OP_ERROR;
    }

    /*
     * This collection hasn't been registered already (ie. looked up before)
     */
    coll_p = (struct dbpf_collection *)malloc(sizeof(struct dbpf_collection));
    if (coll_p == NULL)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: Out of memory error 1\n",
                     __func__);
        return -TROVE_ENOMEM;
    }
    memset(coll_p, 0, sizeof(struct dbpf_collection));

    coll_p->refct = 0;
    coll_p->coll_id = db_data.coll_id;
    coll_p->storage = my_storage_p;

    coll_p->name = strdup(collname);
    if (!coll_p->name)
    {
        free(coll_p);
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: Out of memory error 2\n",
                     __func__);
        return -TROVE_ENOMEM;
    }

    /* Path to data collection dir */
    snprintf(path_name, PATH_MAX, "/%s/%08x/", my_storage_p->data_path, coll_p->coll_id);
    coll_p->data_path = strdup(path_name);
    if (!coll_p->data_path) 
    {
        free(coll_p->name);
        free(coll_p);
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: Out of memory error 3\n",
                     __func__);
        return -TROVE_ENOMEM;
    }

    snprintf(path_name, PATH_MAX, "/%s/%08x/", my_storage_p->meta_path, coll_p->coll_id);
    coll_p->meta_path = strdup(path_name);
    if (!coll_p->meta_path)
    {
	free(coll_p->data_path);
	free(coll_p->name);
	free(coll_p);
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: Out of memory error 4\n",
                     __func__);
	return -TROVE_ENOMEM;
    }

    snprintf(path_name, PATH_MAX, "/%s/%08x/", my_storage_p->config_path, coll_p->coll_id);
    coll_p->config_path = strdup(path_name);
    if (!coll_p->config_path)
    {
	free(coll_p->data_path);
	free(coll_p->meta_path);
	free(coll_p->name);
	free(coll_p);
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: Out of memory error 5\n",
                     __func__);
	return -TROVE_ENOMEM;
    }

    DBPF_GET_COLL_ATTRIB_DBNAME(path_name,
                                PATH_MAX,
                                my_storage_p->meta_path,
                                coll_p->coll_id);
    
    gossip_debug(GOSSIP_TROVE_DEBUG,
                 "%s: calling dbpf_db_open on %s\n", __func__, path_name);

    ret = dbpf_db_open(path_name, 0, &coll_p->coll_attr_db, 0, server_cfg);

    if (ret != DBPF_SUCCESS)
    {
        gossip_err("%s: error from dbpf_db_open\n", __func__);
        free(coll_p->meta_path);
        free(coll_p->data_path);
        free(coll_p->config_path);
        free(coll_p->name);
        free(coll_p);
        return ret;
    }

    /* make sure the version matches the version we understand */
    dbpf_key.data = TROVE_DBPF_VERSION_KEY;
    dbpf_key.len = strlen(TROVE_DBPF_VERSION_KEY) + 1;
    dbpf_val.data = &trove_dbpf_version;
    dbpf_val.len = 32; /* what is this? why a const not used WBLH */

    gossip_debug(GOSSIP_TROVE_DEBUG,
                 "%s: calling dbpf_db_get of dbpf version\n", __func__);

    ret = dbpf_db_get(coll_p->coll_attr_db, &dbpf_key, &dbpf_val);

    if (ret != DBPF_SUCCESS)
    {
        gossip_err("%s: Failed to retrieve collection version: %s\n",
                   __func__, strerror(ret));
        dbpf_db_close(coll_p->coll_attr_db);
        free(coll_p->meta_path);
        free(coll_p->data_path);
        free(coll_p->config_path);
        free(coll_p->name);
        free(coll_p);
        //return -ret;
        return ret;
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: collection lookup: version is "
                 "%s\n", __func__, trove_dbpf_version);

    ret = sscanf(trove_dbpf_version,
                 "%d.%d.%d", 
                 &sto_major,
                 &sto_minor,
                 &sto_inc);
    if(ret < 3)
    {
        gossip_err("%s: Failed to get the version "
                   "components from the storage version: %s\n",
                   __func__, trove_dbpf_version);
        return -TROVE_EINVAL;
    }

    ret = sscanf(TROVE_DBPF_VERSION_VALUE,
                 "%d.%d.%d",
                 &major,
                 &minor,
                 &inc);
    if(ret < 3)
    {
        gossip_err("%s: Failed to get the version "
                   "components from the implementation's version: %s\n",
                   __func__, TROVE_DBPF_VERSION_VALUE);
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
        dbpf_db_close(coll_p->coll_attr_db);
        free(coll_p->meta_path);
        free(coll_p->data_path);
        free(coll_p->config_path);
        free(coll_p->name);
        free(coll_p);
        gossip_err("%s: Trove-dbpf metadata format version mismatch!\n", __func__);
        gossip_err("This collection has version %s\n",
                   trove_dbpf_version);
        gossip_err("This code understands version %s\n",
                   TROVE_DBPF_VERSION_VALUE);
        return -TROVE_EINVAL;
    }

    DBPF_GET_DS_ATTRIB_DBNAME(path_name,
                              PATH_MAX,
                              my_storage_p->meta_path,
                              coll_p->coll_id);

    gossip_debug(GOSSIP_TROVE_DEBUG,
                 "%s: calling dbpf_db_open on %s\n", __func__, path_name);

    ret = dbpf_db_open(path_name,
                       DBPF_DB_COMPARE_DS_ATTR,
                       &coll_p->ds_db,
                       0,
                       server_cfg);

    if (ret != DBPF_SUCCESS)
    {
        gossip_err("%s: error from dbpf_db_open\n", __func__);
        dbpf_db_close(coll_p->coll_attr_db);
        free(coll_p->meta_path);
        free(coll_p->data_path);
        free(coll_p->config_path);
        free(coll_p->name);
        free(coll_p);
        return ret;
    }

    DBPF_GET_KEYVAL_DBNAME(path_name,
                           PATH_MAX,
                           my_storage_p->meta_path,
                           coll_p->coll_id);

    gossip_debug(GOSSIP_TROVE_DEBUG,
                 "%s: calling dbpf_db_open on %s\n", __func__, path_name);

    ret = dbpf_db_open(path_name,
                       DBPF_DB_COMPARE_KEYVAL,
                       &coll_p->keyval_db,
                       0,
                       server_cfg);

    if (ret != DBPF_SUCCESS)
    {
        gossip_err("%s: error from dbpf_db_open\n", __func__);
        dbpf_db_close(coll_p->coll_attr_db);
        dbpf_db_close(coll_p->ds_db);
        free(coll_p->meta_path);
        free(coll_p->data_path);
        free(coll_p->config_path);
        free(coll_p->name);
        free(coll_p);
        return ret;
    }

    coll_p->pcache = PINT_dbpf_keyval_pcache_initialize();

    if(!coll_p->pcache)
    {
        gossip_err("%s: error from dbpf_keyvak_pcache_init\n", __func__);
        dbpf_db_close(coll_p->coll_attr_db);
        dbpf_db_close(coll_p->keyval_db);
        dbpf_db_close(coll_p->ds_db);
        free(coll_p->meta_path);
        free(coll_p->data_path);
        free(coll_p->config_path);
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

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: dbpf_collection_register\n",
                 __func__);
    dbpf_collection_register(coll_p);
    *out_coll_id_p = coll_p->coll_id;

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: clear_stranded_bstreams\n",
                 __func__);
    clear_stranded_bstreams(coll_p->coll_id);

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: returning success\n", __func__);
    //return 1;
    return DBPF_SUCCESS;
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
struct dbpf_storage *dbpf_storage_lookup(char *data_path,
                                         char *meta_path,
                                         char *config_path,
                                         int *error_p,
                                         TROVE_ds_flags flags)
{
    char path_name[PATH_MAX] = {0};
    struct dbpf_storage *sto_p = NULL;
    struct stat sbuf;
    int ret;

    if (my_storage_p != NULL)
    {
        return my_storage_p;
    }

    if (stat(data_path, &sbuf) < 0) 
    {
        *error_p = -TROVE_ENOENT;
	gossip_debug(GOSSIP_TROVE_DEBUG, "%s: %s does not stat\n", __func__, data_path);
        return NULL;
    }
    if (!S_ISDIR(sbuf.st_mode))
    {
        *error_p = -TROVE_EINVAL;
        gossip_err("%s: %s is not a directory\n", __func__, data_path);
        return NULL;
    }

    if (stat(meta_path, &sbuf) < 0)
    {
	*error_p = -TROVE_ENOENT;
	gossip_debug(GOSSIP_TROVE_DEBUG, "%s: %s does not stat\n", __func__, meta_path);
	return NULL;
    }
    if (!S_ISDIR(sbuf.st_mode))
    {
	*error_p = -TROVE_EINVAL;
	gossip_err("%s: %s is not a directory\n", __func__, meta_path);
	return NULL;
    }

    if (stat(config_path, &sbuf) < 0)
    {
	*error_p = -TROVE_ENOENT;
	gossip_debug(GOSSIP_TROVE_DEBUG, "%s: %s does not stat\n", __func__, config_path);
	return NULL;
    }
    if (!S_ISDIR(sbuf.st_mode))
    {
	*error_p = -TROVE_EINVAL;
	gossip_err("%s: %s is not a directory\n", __func__, config_path);
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

    sto_p->config_path = strdup(config_path);
    if (sto_p->config_path == NULL)
    {
	free(sto_p->meta_path);
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
	gossip_err("%s: %s does not stat\n", __func__, path_name);
        return NULL;
    }

    ret = dbpf_db_open(path_name, 0, &sto_p->sto_attr_db, 0, server_cfg);
    if (ret)
    {
        *error_p = ret;
        free(sto_p->config_path);
        free(sto_p->meta_path);
        free(sto_p->data_path);
        free(sto_p);
        gossip_err("%s: Failure opening attribute database\n", __func__);
                   
        my_storage_p = NULL;
        return NULL;
    }

    DBPF_GET_COLLECTIONS_DBNAME(path_name, PATH_MAX, meta_path);

    ret = dbpf_db_open(path_name, 0, &sto_p->coll_db, 0, server_cfg);
    if (ret)
    {
        *error_p = ret;
        dbpf_db_close(sto_p->sto_attr_db);
        free(sto_p->config_path);
        free(sto_p->meta_path);
        free(sto_p->data_path);
        free(sto_p);
        gossip_err("%s: Failure opening collection database\n", __func__);

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

/* Internal function for creating first instances of the databases for
 * a db plus files storage region.
 */
static int dbpf_db_create(char *dbname)
{
    dbpf_db *db;
    int ret;

    /* this returns PVFS/TROVE errors */
    ret = dbpf_db_open(dbname, 0, &db, 1, server_cfg);
    if (ret)
    {
        return ret;
    }
    ret = dbpf_db_close(db);
    if (ret)
    {
        return ret;
    }
    return 0;
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
    dbpf_collection_deleattr,
    dbpf_collection_set_fs_config
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
    dbpf_collection_deleattr,
    dbpf_collection_set_fs_config
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
