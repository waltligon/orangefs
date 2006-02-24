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
#include "trove.h"

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

extern gen_mutex_t dbpf_attr_cache_mutex;

int dbpf_method_id = -1;
char dbpf_method_name[] = "dbpf";

extern int dbpf_thread_initialize(void);

struct dbpf_storage *my_storage_p = NULL;

DB_ENV *dbpf_getdb_env(void)
{
    static DB_ENV *dbenv = NULL;
    if (dbenv == NULL)
    {
        int ret;
        ret = db_env_create(&dbenv, 0);
        if (ret != 0)
        {
            gossip_lerr("dbpf_getdb_env: %s\n", db_strerror(ret));
            return NULL;
        }
        dbenv->flags |= DB_ENV_DBLOCAL;
    }
    return dbenv;
}

void dbpf_error_report(
#ifdef HAVE_DBENV_PARAMETER_TO_DB_ERROR_CALLBACK
		       const DB_ENV *dbenv,
#endif
		       const char *errpfx,
#ifdef HAVE_CONST_THIRD_PARAMETER_TO_DB_ERROR_CALLBACK
		       const
#endif
		       char *msg)
{
#ifdef BERKDB_ERROR_REPORTING
    char buf[512] = {0};

    if (errpfx && msg)
    {
        snprintf(buf, 512, "%s: %s\n", errpfx, msg);
        gossip_err(buf);
    }
#endif
}

static struct dbpf_storage *dbpf_storage_lookup(
    char *stoname, int *err_p);
static int dbpf_db_create(char *dbname);
static DB *dbpf_db_open(char *dbname, int *err_p);
static int dbpf_mkpath(char *pathname, mode_t mode);


static int dbpf_collection_getinfo(TROVE_coll_id coll_id,
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

            DBPF_GET_STORAGE_DIRNAME(path_name, PATH_MAX, sto_p->name);
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
        break;
    }
    return ret;
}

static int dbpf_collection_setinfo(TROVE_coll_id coll_id,
                                   TROVE_context_id context_id,
                                   int option,
                                   void *parameter)
{
    int ret = -TROVE_EINVAL;

    switch(option)
    {
        case TROVE_COLLECTION_HANDLE_RANGES:
            ret = trove_set_handle_ranges(
                coll_id, context_id, (char *)parameter);
            break;
        case TROVE_COLLECTION_HANDLE_TIMEOUT:
            ret = trove_set_handle_timeout(
                coll_id, context_id, (struct timeval *)parameter);
            break;
        case TROVE_COLLECTION_ATTR_CACHE_KEYWORDS:
            gen_mutex_lock(&dbpf_attr_cache_mutex);
            ret = dbpf_attr_cache_set_keywords((char *)parameter);
            gen_mutex_unlock(&dbpf_attr_cache_mutex);
            break;
        case TROVE_COLLECTION_ATTR_CACHE_SIZE:
            gen_mutex_lock(&dbpf_attr_cache_mutex);
            ret = dbpf_attr_cache_set_size(*((int *)parameter));
            gen_mutex_unlock(&dbpf_attr_cache_mutex);
            break;
        case TROVE_COLLECTION_ATTR_CACHE_MAX_NUM_ELEMS:
            gen_mutex_lock(&dbpf_attr_cache_mutex);
            ret = dbpf_attr_cache_set_max_num_elems(*((int *)parameter));
            gen_mutex_unlock(&dbpf_attr_cache_mutex);
            break;
        case TROVE_COLLECTION_ATTR_CACHE_INITIALIZE:
            gen_mutex_lock(&dbpf_attr_cache_mutex);
            ret = dbpf_attr_cache_do_initialize();
            gen_mutex_unlock(&dbpf_attr_cache_mutex);
             break;
    }
    return ret;
}

static int dbpf_collection_seteattr(TROVE_coll_id coll_id,
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

static int dbpf_collection_geteattr(TROVE_coll_id coll_id,
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

    db_data.data  = val_p->buffer;
    db_data.ulen  = val_p->buffer_sz;
    db_data.flags = DB_DBT_USERMEM;

    ret = coll_p->coll_attr_db->get(coll_p->coll_attr_db,
                                    NULL, &db_key, &db_data, 0);
    if (ret != 0)
    {
        gossip_lerr("dbpf_collection_geteattr: %s\n", db_strerror(ret));
        return -dbpf_db_error_to_trove_error(ret);
    }

    val_p->read_sz = db_data.size;
    return 1;
}

static int dbpf_initialize(char *stoname,
                           TROVE_ds_flags flags,
                           char **method_name_p,
                           int method_id)
{
    int ret = -TROVE_EINVAL;
    struct dbpf_storage *sto_p = NULL;

    if (!stoname)
    {
        gossip_err("dbpf_initialize failure: invalid storage name\n");
        return ret;
    }

    if (!method_name_p)
    {
        gossip_err("dbpf_initialize failure: invalid method name ptr\n");
        return ret;
    }

    sto_p = dbpf_storage_lookup(stoname, &ret);
    if (sto_p == NULL)
    {
        gossip_debug(
            GOSSIP_TROVE_DEBUG, "dbpf_initialize failure: storage "
            "lookup failed\n");
        return -TROVE_ENOENT;
    }

    my_storage_p = sto_p;
    dbpf_method_id = method_id;
    
    *method_name_p = strdup(dbpf_method_name);
    if (*method_name_p == NULL)
    {
        gossip_err("dbpf_initialize failure: cannot allocate memory\n");
        return -TROVE_ENOMEM;
    }

    dbpf_open_cache_initialize();

    return dbpf_thread_initialize();
}

static int dbpf_finalize(void)
{
    int ret = -TROVE_EINVAL;

    dbpf_method_id = -TROVE_EINVAL;

    dbpf_thread_finalize();
    dbpf_open_cache_finalize();
    gen_mutex_lock(&dbpf_attr_cache_mutex);
    dbpf_attr_cache_finalize();
    gen_mutex_unlock(&dbpf_attr_cache_mutex);

    dbpf_collection_clear_registered();

    ret = my_storage_p->sto_attr_db->sync(my_storage_p->sto_attr_db, 0);
    if (ret)
    {
        gossip_err("dbpf_finalize: %s\n", db_strerror(ret));
        return -dbpf_db_error_to_trove_error(ret);
    }

    ret = my_storage_p->sto_attr_db->close(my_storage_p->sto_attr_db, 0);
    if (ret)
    {
        gossip_err("dbpf_finalize: %s\n", db_strerror(ret));
        return -dbpf_db_error_to_trove_error(ret);
    }

    ret = my_storage_p->coll_db->sync(my_storage_p->coll_db, 0);
    if (ret)
    {
        gossip_err("dbpf_finalize: %s\n", db_strerror(ret));
        return -dbpf_db_error_to_trove_error(ret);
    }

    ret = my_storage_p->coll_db->close(my_storage_p->coll_db, 0);
    if (ret)
    {
        gossip_err("dbpf_finalize: %s\n", db_strerror(ret));
        return -dbpf_db_error_to_trove_error(ret);
    }

    free(my_storage_p->name);
    free(my_storage_p);
    my_storage_p = NULL;

    return 1;
}

/* Creates and initializes the databases needed for a dbpf storage
 * space.  This includes:
 * - creating the path to the storage directory
 * - creating storage attribute database, propagating with create time
 * - creating collections database, filling in create time
 */
static int dbpf_storage_create(char *stoname,
                               void *user_ptr,
                               TROVE_op_id *out_op_id_p)
{
    int ret = -TROVE_EINVAL;
    char storage_dirname[PATH_MAX] = {0};
    char sto_attrib_dbname[PATH_MAX] = {0};
    char collections_dbname[PATH_MAX] = {0};

    DBPF_GET_STORAGE_DIRNAME(storage_dirname, PATH_MAX, stoname);
    ret = dbpf_mkpath(storage_dirname, 0755);
    if (ret != 0)
    {
        return ret;
    }

    DBPF_GET_STO_ATTRIB_DBNAME(sto_attrib_dbname, PATH_MAX, stoname);
    ret = dbpf_db_create(sto_attrib_dbname);
    if (ret != 0)
    {
        return ret;
    }
    
    DBPF_GET_COLLECTIONS_DBNAME(collections_dbname, PATH_MAX, stoname);
    ret = dbpf_db_create(collections_dbname);
    if (ret != 0)
    {
	gossip_lerr("dbpf_storage_create: removing storage attribute database after failed create attempt");
	unlink(sto_attrib_dbname);
        return ret;
    }

    return 1;
}

static int dbpf_storage_remove(char *stoname,
                               void *user_ptr,
                               TROVE_op_id *out_op_id_p)
{
    int ret = -TROVE_EINVAL;
    char path_name[PATH_MAX] = {0};

    DBPF_GET_STO_ATTRIB_DBNAME(path_name, PATH_MAX, stoname);
    gossip_debug(GOSSIP_TROVE_DEBUG, "Removing %s\n", path_name);

    if (unlink(path_name) != 0)
    {
        ret = -trove_errno_to_trove_error(errno);
        goto storage_remove_failure;
    }

    DBPF_GET_COLLECTIONS_DBNAME(path_name, PATH_MAX, stoname);
    gossip_debug(GOSSIP_TROVE_DEBUG, "Removing %s\n", path_name);

    if (unlink(path_name) != 0)
    {
        ret = -trove_errno_to_trove_error(errno);
        goto storage_remove_failure;
    }

    DBPF_GET_STORAGE_DIRNAME(path_name, PATH_MAX, stoname);
    if (rmdir(path_name) != 0)
    {
        perror("failure removing storage space");
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
static int dbpf_collection_create(char *collname,
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

    DBPF_GET_STORAGE_DIRNAME(path_name, PATH_MAX, sto_p->name);
    ret = stat(path_name, &dirstat);
    if (ret < 0 && errno != ENOENT)
    {
        gossip_err("stat failed on storage directory %s\n", path_name);
        return -trove_errno_to_trove_error(errno);
    }
    else if (ret < 0)
    {
        ret = mkdir(path_name, 0755);
        if (ret != 0)
        {
            gossip_err("mkdir failed on storage directory %s\n", path_name);
            return -trove_errno_to_trove_error(errno);
        }
    }
    
    DBPF_GET_COLL_DIRNAME(path_name, PATH_MAX, sto_p->name, new_coll_id);
    ret = mkdir(path_name, 0755);
    if (ret != 0)
    {
        gossip_err("mkdir failed on collection directory %s\n", path_name);
        return -trove_errno_to_trove_error(errno);
    }

    DBPF_GET_COLL_ATTRIB_DBNAME(path_name, PATH_MAX,
                                sto_p->name, new_coll_id);
    db_p = dbpf_db_open(path_name, &error);
    if (db_p == NULL)
    {
        ret = dbpf_db_create(path_name);
        if (ret != 0)
        {
            gossip_err("dbpf_db_create failed on attrib db %s\n", path_name);
            return ret;
        }

        db_p = dbpf_db_open(path_name, &error);
        if (db_p == NULL)
        {
            gossip_err("dbpf_db_open failed on attrib db %s\n", path_name);
            return error;
        }
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
    db_p->close(db_p, 0);

    DBPF_GET_DS_ATTRIB_DBNAME(path_name, PATH_MAX, sto_p->name, new_coll_id);
    db_p = dbpf_db_open(path_name, &error);
    if (db_p == NULL)
    {
        ret = dbpf_db_create(path_name);
        if (ret != 0)
        {
            gossip_err("dbpf_db_create failed on %s\n", path_name);
            return ret;
        }
    }
    else
    {
        db_p->close(db_p, 0);
    }

    DBPF_GET_KEYVAL_DBNAME(path_name, PATH_MAX, sto_p->name, new_coll_id);
    db_p = dbpf_db_open(path_name, &error);
    if (db_p == NULL)
    {
        ret = dbpf_db_create(path_name);
        if (ret != 0)
        {
            gossip_err("dbpf_db_create failed on %s\n", path_name);
            return ret;
        }
    }
    else
    {
        db_p->close(db_p, 0);
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
    return 1;
}

static int dbpf_collection_remove(char *collname,
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
    data.data = &db_data;
    data.ulen = sizeof(db_data);
    data.flags = DB_DBT_USERMEM;

    ret = sto_p->coll_db->get(sto_p->coll_db, NULL, &key, &data, 0);
    if (ret != 0)
    {
        sto_p->coll_db->err(sto_p->coll_db, ret, "DB->get");
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

    DBPF_GET_DS_ATTRIB_DBNAME(path_name, PATH_MAX,
                              sto_p->name, db_data.coll_id);
    if (unlink(path_name) != 0)
    {
        gossip_err("failure removing dataspace attrib db\n");
        ret = -trove_errno_to_trove_error(errno);
    }

    DBPF_GET_KEYVAL_DBNAME(path_name, PATH_MAX,
                           sto_p->name, db_data.coll_id);
    if(unlink(path_name) != 0)
    {
        gossip_err("failure removing keyval db\n");
        ret = -trove_errno_to_trove_error(errno);
    }

    DBPF_GET_COLL_ATTRIB_DBNAME(path_name, PATH_MAX,
                                sto_p->name, db_data.coll_id);
    if (unlink(path_name) != 0)
    {
        gossip_err("failure removing collection attrib db\n");
        ret = -trove_errno_to_trove_error(errno);
        goto collection_remove_failure;
    }

    DBPF_GET_BSTREAM_DIRNAME(path_name, PATH_MAX,
                             sto_p->name, db_data.coll_id);
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
        goto collection_remove_failure;
    }

    DBPF_GET_COLL_DIRNAME(path_name, PATH_MAX,
                          sto_p->name, db_data.coll_id);
    if (rmdir(path_name) != 0)
    {
        gossip_err("failure removing collection directory\n");
        ret = -trove_errno_to_trove_error(errno);
        goto collection_remove_failure;
    }

  collection_remove_failure:

    return ret;
}

static int dbpf_collection_iterate(TROVE_ds_position *inout_position_p,
                                   TROVE_keyval_s *name_array,
                                   TROVE_coll_id *coll_id_array,
                                   int *inout_count_p,
                                   TROVE_ds_flags flags,
                                   TROVE_vtag_s *vtag,
                                   void *user_ptr,
                                   TROVE_op_id *out_op_id_p)
{
    int ret = -TROVE_EINVAL, i = 0;
    db_recno_t recno;
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
        if (sizeof(recno) < name_array[0].buffer_sz)
        {
            key.data = name_array[0].buffer;
            key.size = key.ulen = name_array[0].buffer_sz;
        }
        else
        {
            key.data = &recno;
            key.size = key.ulen = sizeof(recno);
        }
        *(TROVE_ds_position *) key.data = *inout_position_p;
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


static int dbpf_collection_lookup(char *collname,
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
    
    sto_p = my_storage_p;
    if (!sto_p)
    {
        return ret;
    }

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data = collname;
    key.size = strlen(collname)+1;
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
        sto_p->coll_db->err(sto_p->coll_db, ret, "DB->get");
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

    coll_p->refct = 0;
    coll_p->coll_id = db_data.coll_id;
    coll_p->storage = sto_p;

    coll_p->name = strdup(collname);
    if (!coll_p->name)
    {
        free(coll_p);
        return -TROVE_ENOMEM;
    }

    DBPF_GET_DS_ATTRIB_DBNAME(path_name, PATH_MAX,
                              sto_p->name, coll_p->coll_id);
    coll_p->ds_db = dbpf_db_open(path_name, &ret);
    if (coll_p->ds_db == NULL)
    {
        return ret;
    }

    DBPF_GET_KEYVAL_DBNAME(path_name, PATH_MAX,
                           sto_p->name, coll_p->coll_id);
    coll_p->keyval_db = dbpf_db_open(path_name, &ret);
    if(coll_p->keyval_db == NULL)
    {
        return ret;
    }

    DBPF_GET_COLL_ATTRIB_DBNAME(path_name, PATH_MAX,
                                sto_p->name, coll_p->coll_id);
    coll_p->coll_attr_db = dbpf_db_open(path_name, &ret);
    if (coll_p->coll_attr_db == NULL)
    {
        return ret;
    }

    /* make sure the version matches the version we understand */
    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data = TROVE_DBPF_VERSION_KEY;
    key.size = strlen(TROVE_DBPF_VERSION_KEY);
    data.data = &trove_dbpf_version;
    data.ulen = 32;
    data.flags = DB_DBT_USERMEM;

    ret = coll_p->coll_attr_db->get(
        coll_p->coll_attr_db, NULL, &key, &data, 0);

    if (ret)
    {
        gossip_err("Failed to retrieve collection version: %s\n",
                   db_strerror(ret));
        return dbpf_db_error_to_trove_error(ret);
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "collection lookup: version is "
                 "%s\n", trove_dbpf_version);

    if (strcmp(trove_dbpf_version, TROVE_DBPF_VERSION_VALUE) != 0)
    {
        gossip_err("Trove-dbpf metadata format version mismatch!\n");
        gossip_err("This collection has version %s\n",
                   trove_dbpf_version);
        gossip_err("This code understands version %s\n",
                   TROVE_DBPF_VERSION_VALUE);
        return -TROVE_EINVAL;
    }

    dbpf_collection_register(coll_p);
    *out_coll_id_p = coll_p->coll_id;
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
static struct dbpf_storage *dbpf_storage_lookup(
    char *stoname, int *error_p)
{
    char path_name[PATH_MAX] = {0};
    struct dbpf_storage *sto_p = NULL;

    if (my_storage_p != NULL)
    {
        return my_storage_p;
    }

    sto_p = (struct dbpf_storage *)malloc(sizeof(struct dbpf_storage));
    if (sto_p == NULL)
    {
        *error_p = -TROVE_ENOMEM;
        return NULL;
    }

    sto_p->name = strdup(stoname);
    if (sto_p->name == NULL)
    {
        free(sto_p);
        *error_p = -TROVE_ENOMEM;
        return NULL;
    }

    sto_p->refct = 0;

    DBPF_GET_STO_ATTRIB_DBNAME(path_name, PATH_MAX, stoname);

    sto_p->sto_attr_db = dbpf_db_open(path_name, error_p);
    if (sto_p->sto_attr_db == NULL)
    {
        return NULL;
    }

    DBPF_GET_COLLECTIONS_DBNAME(path_name, PATH_MAX, stoname);

    sto_p->coll_db = dbpf_db_open(path_name, error_p);
    if (sto_p->coll_db == NULL)
    {
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

        if (nullpos <= (pos + 1))
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
    int ret = -TROVE_EINVAL;
    DB *db_p = NULL;
    DB_ENV *envp = NULL;

    if ((envp = dbpf_getdb_env()) == NULL)
    {
        return TROVE_ENOMEM;
    }
    if ((ret = db_create(&db_p, envp, 0)) != 0)
    {
        gossip_lerr("dbpf_storage_create: %s\n", db_strerror(ret));
        return -dbpf_db_error_to_trove_error(ret);
    }

    /* DB_RECNUM makes it easier to iterate through every key in chunks */
    if ((ret = db_p->set_flags(db_p, DB_RECNUM)) != 0)
    {
        db_p->err(db_p, ret, "%s: set_flags", dbname);
        return -dbpf_db_error_to_trove_error(ret);
    }

    if ((ret = db_p->open(db_p,
#ifdef HAVE_TXNID_PARAMETER_TO_DB_OPEN
                          NULL,
#endif
                          dbname,
                          NULL,
                          TROVE_DB_TYPE,
                          TROVE_DB_CREATE_FLAGS,
                          TROVE_DB_MODE)) != 0)
    {
        db_p->err(db_p, ret, "%s", dbname);
        return -dbpf_db_error_to_trove_error(ret);
    }
    
    if ((ret = db_p->close(db_p, 0)) != 0)
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
static DB *dbpf_db_open(char *dbname, int *error_p)
{
    int ret = -TROVE_EINVAL;
    DB *db_p = NULL;
    DB_ENV *envp = NULL;

    if ((envp = dbpf_getdb_env()) == NULL)
    {
        *error_p = TROVE_ENOMEM;
        return NULL;
    }
    if ((ret = db_create(&db_p, envp, 0)) != 0)
    {
        *error_p = dbpf_db_error_to_trove_error(ret);
        return NULL;
    }

    db_p->set_errfile(db_p, stderr);
    db_p->set_errpfx(db_p, "pvfs2-trove-dbpf");

    /* DB_RECNUM makes it easier to iterate through every key in chunks */
    if ((ret = db_p->set_flags(db_p, DB_RECNUM)) != 0)
    {
        db_p->err(db_p, ret, "%s: set_flags", dbname);
        *error_p = dbpf_db_error_to_trove_error(ret);
        return NULL;
    }

    if ((ret = db_p->open(db_p,
#ifdef HAVE_TXNID_PARAMETER_TO_DB_OPEN
                          NULL,
#endif
                          dbname,
                          NULL,
                          TROVE_DB_TYPE,
                          TROVE_DB_OPEN_FLAGS,
                          0)) != 0)
    {
        *error_p = dbpf_db_error_to_trove_error(ret);
        return NULL;
    }
    return db_p;
}

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
    dbpf_collection_iterate,
    dbpf_collection_setinfo,
    dbpf_collection_getinfo,
    dbpf_collection_seteattr,
    dbpf_collection_geteattr
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
    { DSPACE_CREATE, "DSPACE_CREATE" },
    { DSPACE_REMOVE, "DSPACE_REMOVE" },
    { DSPACE_ITERATE_HANDLES, "DSPACE_ITERATE_HANDLES" },
    { DSPACE_VERIFY, "DSPACE_VERIFY" },
    { DSPACE_GETATTR, "DSPACE_GETATTR" },
    { DSPACE_SETATTR, "DSPACE_SETATTR" }
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

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
