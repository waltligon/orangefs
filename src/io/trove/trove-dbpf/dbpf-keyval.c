/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 * dbpf_keyval_write semantics:
 *
 * Value buffer is supplied by user with size of buffer
 * If size is too small DB returns an error (Cannot allocated memory)
 * and return the size of the buffer needed in val_p->read_sz
 *
 * WBL 6/05
 *
 */

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <db.h>
#include <time.h>
#include <stdlib.h>
#include <ctype.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#include "trove.h"
#include "trove-internal.h"
#include "dbpf.h"
#include "dbpf-op-queue.h"
#include "dbpf-attr-cache.h"
#include "dbpf-keyval-pcache.h"
#include "gossip.h"
#include "pvfs2-internal.h"
#include "pint-perf-counter.h"
#include "pint-cached-config.h"
#include "dbpf-keyval.h"

static uint32_t readdir_session = 0;

extern int synccount;

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

extern gen_mutex_t dbpf_attr_cache_mutex;

static int dbpf_keyval_do_remove(
    DB *db_p, TROVE_handle handle, TROVE_keyval_s *key, TROVE_keyval_s *val);

static int dbpf_keyval_read_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_read_value_path_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_read_value_query_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_read_list_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_write_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_write_list_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_remove_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_remove_list_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_iterate_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_iterate_keys_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_flush_op_svc(struct dbpf_op *op_p);
static int dbpf_keyval_get_handle_info_op_svc(struct dbpf_op *op_p);

#define DBPF_ITERATE_CURRENT_POSITION 1

static int dbpf_keyval_iterate_get_first_entry(
    TROVE_handle handle, 
    DBC * dbc_p);

static int dbpf_keyval_iterate_step_to_position(
    TROVE_handle handle, 
    TROVE_ds_position pos,
    DBC * dbc_p);

static int dbpf_keyval_iterate_skip_to_position(
    TROVE_handle handle, 
    TROVE_ds_position pos, 
    PINT_dbpf_keyval_pcache *pcache,
    DBC * dbc_p);

static int dbpf_keyval_iterate_cursor_get(
    TROVE_handle handle, 
    DBC * dbc_p, 
    TROVE_keyval_s * key, 
    TROVE_keyval_s * data, 
    uint32_t db_flags);

enum dbpf_handle_info_action
{
    DBPF_KEYVAL_HANDLE_COUNT_INCREMENT,
    DBPF_KEYVAL_HANDLE_COUNT_DECREMENT
};

static int dbpf_keyval_handle_info_ops(struct dbpf_op * op_p,
                                       enum dbpf_handle_info_action action);

static int dbpf_result_iterate_selector(char *a, char *b, 
                                        uint32_t query);

static int dbpf_keyval_read(TROVE_coll_id coll_id,
                            TROVE_handle handle,
                            TROVE_keyval_s *key_p,
                            TROVE_keyval_s *val_p,
                            TROVE_ds_flags flags,
                            TROVE_vtag_s *vtag,
                            void *user_ptr,
                            TROVE_context_id context_id,
                            TROVE_op_id *out_op_id_p,
                            PVFS_hint  hints)
{
    int ret;
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    dbpf_attr_cache_elem_t *cache_elem = NULL;
    TROVE_object_ref ref = {handle, coll_id};
    PINT_event_id event_id = 0;
    PINT_event_type event_type;

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "*** Trove KeyVal Read "
                 "of %s\n", (char *)key_p->buffer);

    gen_mutex_lock(&dbpf_attr_cache_mutex);
    cache_elem = dbpf_attr_cache_elem_lookup(ref);
    if (cache_elem && (!(flags & TROVE_BINARY_KEY)))
    {
        dbpf_keyval_pair_cache_elem_t *keyval_pair =
            dbpf_attr_cache_elem_get_data_based_on_key(
                cache_elem, key_p->buffer);
        if (keyval_pair)
        {
            val_p->read_sz = val_p->buffer_sz;
            /* note: dbpf_attr_cache_keyval_pair_fetch_cached_data() will
             * update read_sz appropriately
             */
            ret = dbpf_attr_cache_keyval_pair_fetch_cached_data(
                cache_elem, keyval_pair, val_p->buffer,
                &val_p->read_sz);
            gen_mutex_unlock(&dbpf_attr_cache_mutex);
            if(ret < 0)
            {
                return ret;
            }
            return 1;
        }
    }
    gen_mutex_unlock(&dbpf_attr_cache_mutex);

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_READ,
        coll_p,
        handle,
        dbpf_keyval_read_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }

    event_type = trove_dbpf_keyval_read_event_id;
    DBPF_EVENT_START(coll_p, q_op_p, event_type, &event_id,
                     PINT_HINT_GET_CLIENT_ID(hints),
                     PINT_HINT_GET_REQUEST_ID(hints),
                     PINT_HINT_GET_RANK(hints),
                     handle,
                     PINT_HINT_GET_OP_ID(hints));

    /* initialize the op-specific members */
    op_p->u.k_read.key = key_p;
    op_p->u.k_read.val = val_p;
    op_p->hints = hints;

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p,
                                 event_type, event_id);
}

static int dbpf_keyval_read_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL;
    struct dbpf_keyval_db_entry key_entry;
    DBT key, data;
    TROVE_object_ref ref = {op_p->handle, op_p->coll_p->coll_id};

    memset(&key, 0, sizeof(key));

    key_entry.handle = op_p->handle;
    memcpy(key_entry.key, 
           op_p->u.k_read.key->buffer, 
           op_p->u.k_read.key->buffer_sz);
    key.data = &key_entry;
    key.size = key.ulen = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(
        op_p->u.k_read.key->buffer_sz);
    key.flags = DB_DBT_USERMEM;

    memset(&data, 0, sizeof(data));
    data.data = op_p->u.k_read.val->buffer;
    data.ulen = op_p->u.k_read.val->buffer_sz;
    data.flags = DB_DBT_USERMEM;

    ret = op_p->coll_p->keyval_db->get(
        op_p->coll_p->keyval_db, NULL, &key, &data, 0);
    if (ret != 0)
    {
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                     "warning: keyval read error on handle %llu and "
                     "key=%*s (%s)\n", llu(op_p->handle),
                     op_p->u.k_read.key->buffer_sz,
                     (char *)op_p->u.k_read.key->buffer, 
                     db_strerror(ret));

        /* if data buffer is too small returns a memory error */
        if (data.ulen < data.size)
        {
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                         "warning: Value buffer too small %d < %d\n",
                         data.ulen, data.size);
            /* let the user know */
            op_p->u.k_read.val->read_sz = data.size;
        }

        ret = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }
    op_p->u.k_read.val->read_sz = data.size;

    /* cache this data in the attr cache if we can */
    if(!(op_p->flags & TROVE_BINARY_KEY))
    {
        gen_mutex_lock(&dbpf_attr_cache_mutex);
        if (dbpf_attr_cache_elem_set_data_based_on_key(
                ref, key_entry.key,
                op_p->u.k_read.val->buffer, data.size))
        {
            /*
             * NOTE: this can happen if the keyword isn't registered, or if
             * there is no associated cache_elem for this key
             */
            gossip_debug(
                GOSSIP_DBPF_ATTRCACHE_DEBUG,"** CANNOT cache data retrieved "
                "(key is %s)\n", (char *)key_entry.key);
        }
        else
        {
            gossip_debug(
                GOSSIP_DBPF_ATTRCACHE_DEBUG,"*** cached keyval data "
                "retrieved (key is %s)\n",
                (char *)key_entry.key);
        }
        gen_mutex_unlock(&dbpf_attr_cache_mutex);
    }

    return 1;

return_error:
    return ret;
}

static int dbpf_keyval_read_value_path(TROVE_coll_id coll_id,
                            uint32_t count,
                            PVFS_dirent *dirent_p,
                            TROVE_handle *handle_p,
                            TROVE_ds_flags flags,
                            TROVE_vtag_s *vtag,
                            void *user_ptr,
                            TROVE_context_id context_id,
                            TROVE_op_id *out_op_id_p,
                            PVFS_hint  hints)
{
    int ret=0;
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    PINT_event_id event_id = 0;
    PINT_event_type event_type;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_READ_VALUE,
        coll_p,
        dirent_p[0].handle, 
        dbpf_keyval_read_value_path_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }

    event_type = trove_dbpf_keyval_read_event_id;
    DBPF_EVENT_START(coll_p, q_op_p, event_type, &event_id,
                     PINT_HINT_GET_CLIENT_ID(hints),
                     PINT_HINT_GET_REQUEST_ID(hints),
                     PINT_HINT_GET_RANK(hints),
                     handle_p[0], 
                     PINT_HINT_GET_OP_ID(hints));

    /* initialize the op-specific members */
    op_p->u.v_path.dirent_p = dirent_p;
    op_p->u.v_path.count = count;
    op_p->u.v_path.handle_p = handle_p;
    op_p->hints = hints;

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p,
                                 event_type, event_id);
}


static int dbpf_keyval_read_value_path_op_svc(struct dbpf_op *op_p)
{
    int ret = 0, path_len=0, i=0;
    TROVE_handle k_handle=0, v_handle=0, root_handle=0;
    DBT key, data, pkey;
    DBC *dbc_s=NULL;
    struct dbpf_keyval_db_entry key_entry;
    char * tmp_path=NULL;

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "[DBPF KEYVAL]: "
                 "dbpf_keyval_read_value_path: enter, count: %d\n",
                 op_p->u.v_path.count);

    /* get root handle of collection */
    PINT_cached_config_get_root_handle(op_p->coll_p->coll_id, &root_handle); 

    if( (op_p->coll_p->keyval_secondary_db->cursor(
         op_p->coll_p->keyval_secondary_db, NULL, &dbc_s, 0)) != 0 )
    {
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "[DBPF KEYVAL]: "
                     "dbpf_keyval_read_value_path: Error getting cursor for "
                     "keyval_secondary: %s\n", db_strerror(ret));
        ret = -TROVE_EFAULT;
        return ret;
    }

    for( i=0; i < op_p->u.v_path.count; i++ )
    {
        /* initialize keys */
        memset(&key, 0, sizeof(DBT));
        memset(&data, 0, sizeof(DBT));
        memset(&pkey, 0, sizeof(DBT));
        memset(&key_entry, 0, sizeof(struct dbpf_keyval_db_entry));

        key.data = &(k_handle);
        key.ulen = key.size = sizeof(TROVE_handle);

        data.data = &(v_handle);
        data.size = data.ulen = sizeof(TROVE_handle);

        pkey.data = &key_entry;
        pkey.size = pkey.ulen = sizeof( struct dbpf_keyval_db_entry );

        key.flags = data.flags = pkey.flags = DB_DBT_USERMEM;

        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "[DBPF KEYVAL]: "
                     "dbpf_keyval_read_value_path: looking up path for "
                     "dirent: (%llu)(%s), handle: (%llu)\n",
                     llu(op_p->u.v_path.dirent_p[i].handle),
                     op_p->u.v_path.dirent_p[i].d_name,
                     llu(op_p->u.v_path.handle_p[i]));
        /* if handle pointer is empty, set it to the requested handle */
        if( op_p->u.v_path.handle_p[i] == 0 )
        {
            op_p->u.v_path.handle_p[i] = op_p->u.v_path.dirent_p[i].handle;
        }

        if( op_p->u.v_path.handle_p[i] == root_handle )
        {
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "[DBPF KEYVAL]: "
                         "dbpf_keyval_read_value_path: already at root handle, "
                         "continuing\n");
            continue; /* all done */
        }

        memcpy( key.data, &(op_p->u.v_path.handle_p[i]), 
            sizeof(TROVE_handle));

        ret = dbc_s->c_pget(dbc_s, &key, &pkey, &data, DB_SET);
        while( ret == 0 )
        {
            /* item was found, move 'parent' key to key for next query */
            memcpy( key.data, &(key_entry.handle), sizeof(TROVE_handle));
            memcpy( &(op_p->u.v_path.dirent_p[i].handle), &(key_entry.handle), 
                sizeof(TROVE_handle));
            memcpy( &(op_p->u.v_path.handle_p[i]), &(key_entry.handle), 
                sizeof(TROVE_handle));
   
            key.ulen = key.size = sizeof(TROVE_handle);
    
            /* put path associated with parent into path if not a de */
            if( strncmp("de", key_entry.key, 3) != 0 )
            {
                /* existing path resides in op_p->u.v_path.dirent */
                path_len = strlen(op_p->u.v_path.dirent_p[i].d_name) + 
                    (pkey.size - sizeof(TROVE_handle)) + 1;
                if( (tmp_path = calloc( path_len, sizeof(char))) == 0 )
                {
                    ret = -TROVE_ENOMEM;
                    goto return_error;
                }

                /* copy / and key, prefix to existing path */
                strncpy( tmp_path, "/", 1 );
                memcpy( tmp_path+sizeof(char), key_entry.key,
                    (pkey.size - sizeof(TROVE_handle)));
                memcpy( tmp_path+(pkey.size-sizeof(TROVE_handle)), 
                    op_p->u.v_path.dirent_p[i].d_name, 
                    strlen(op_p->u.v_path.dirent_p[i].d_name));
    
                /* reset string in d_name, copy over tmp_path */
                memset(op_p->u.v_path.dirent_p[i].d_name, 0, PVFS_NAME_MAX+1);
                strncpy(op_p->u.v_path.dirent_p[i].d_name, tmp_path, 
                    path_len);
                free(tmp_path);
            }

            /* if the root handle has been reached, break */
            if( key_entry.handle == root_handle )
            {
                break;
            }

            memset(&key_entry, 0, sizeof(key_entry));
            ret = dbc_s->c_pget(dbc_s, &key, &pkey, &data, DB_SET);
        } /* end building path for local handles, reached one we didn't have */
    
        if ( (ret != DB_NOTFOUND) && (key_entry.handle != root_handle) )
        {
            ret = -dbpf_db_error_to_trove_error(ret);
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "[DBPF KEYVAL]: "
                         "dbpf_keyval_read_value_path: Error looking up "
                         "handle: %s\n", db_strerror(ret));
            goto return_error;
        }

        /* the lookup has gone as far as it can, we need to lookup
         * the parent attribute to set the next handle in the query
         * if we aren't at the root right now! */
        if( key_entry.handle != root_handle )
        {
            memset(&key_entry, 0, sizeof(key_entry));
            pkey.size = pkey.ulen = sizeof(TROVE_handle) + 
                SPECIAL_PARENT_KEYLEN;

            key_entry.handle = op_p->u.v_path.handle_p[i];
            strncpy(key_entry.key, SPECIAL_PARENT_KEYSTR, 
                SPECIAL_PARENT_KEYLEN );

            /* the parent key is held in the keyval database with 
             * <handle>system.pvfs2.parent -> parent handle. 
             * look it up, store in handle_p */
            ret = op_p->coll_p->keyval_db->get( op_p->coll_p->keyval_db, NULL, 
                &pkey, &data, 0);

            if( ret != 0 )
            {
                gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "[DBPF KEYVAL]: "
                             "dbpf_keyval_read_value_path: Error looking up "
                             "parent (%llu): %s\n", op_p->u.v_path.handle_p[i], 
                             db_strerror(ret));
                ret = -dbpf_db_error_to_trove_error(ret);
            }
            else
            {
                op_p->u.v_path.handle_p[i] = *(TROVE_handle *)data.data;
            }
        }

        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "[DBPF KEYVAL]: "
                     "dbpf_keyval_read_value_path: returning dirent_p[%d]: "
                     "(%llu)(%s), handle_p[%d]: (%llu)\n",
                     i, llu(op_p->u.v_path.dirent_p[i].handle),
                     op_p->u.v_path.dirent_p[i].d_name,
                     i, llu(op_p->u.v_path.handle_p[i]));
    } //for
    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "[DBPF KEYVAL]: "
                 "dbpf_keyval_read_value_path: exit\n");
    return 1;

return_error:
    dbc_s->c_close(dbc_s);
    return ret;
}

static int dbpf_keyval_read_value_query(TROVE_coll_id coll_id,
                            TROVE_ds_position *position_p,
                            uint32_t type,
                            TROVE_keyval_s *key_p,
                            TROVE_keyval_s *val_p,
                            PVFS_handle *handle_p,
                            uint32_t *count,
                            TROVE_ds_flags flags,
                            TROVE_vtag_s *vtag,
                            void *user_ptr,
                            TROVE_context_id context_id,
                            TROVE_op_id *out_op_id_p,
                            PVFS_hint  hints)
{
    int ret;
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    PINT_event_id event_id = 0;
    PINT_event_type event_type;

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "[KEYVAL]: start of read_value_query\n");

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_READ_VALUE,
        coll_p,
        handle_p[0], // at least initial element will have a handle
        dbpf_keyval_read_value_query_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }

    event_type = trove_dbpf_keyval_read_event_id;
    DBPF_EVENT_START(coll_p, q_op_p, event_type, &event_id,
                     PINT_HINT_GET_CLIENT_ID(hints),
                     PINT_HINT_GET_REQUEST_ID(hints),
                     PINT_HINT_GET_RANK(hints),
                     dirent_array[0].handle, 
                     PINT_HINT_GET_OP_ID(hints));

    /* initialize the op-specific members */
    op_p->u.v_query.key = key_p;
    op_p->u.v_query.val = val_p;
    op_p->u.v_query.handle_p = handle_p;
    op_p->u.v_query.count = count;
    op_p->u.v_query.position_p = position_p;
    op_p->u.v_query.query_type = type;
    op_p->hints = hints;

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p,
                                 event_type, event_id);
}

static int dbpf_keyval_read_value_query_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL, lookup_key_sz=0, i=0, record_count=0;
    uint32_t cursor_flags = 0, get_flags = 0;
    struct dbpf_keyval_db_entry key_entry;
    void *lookup_key, *val_datum, *original_key;
    TROVE_ds_position local_p = TROVE_ITERATE_START;
    DBT key, data, pkey;
    DBC *dbc_p=NULL, *dbcn_p=NULL, *query_p=NULL;

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    memset(&pkey, 0, sizeof(pkey));
    memset(&key_entry, 0, sizeof(key_entry));

    /* size of key is length of the attr and value (minus 1 null) */
    lookup_key_sz = op_p->u.v_query.key->buffer_sz + 
                    op_p->u.v_query.val->buffer_sz - 1;

    if( (lookup_key = calloc( 2, DBPF_MAX_KEY_LENGTH )) == 0 )
    { 
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "[DBPF KEYVAL]: malloc for "
                     "key_data failed.\n");
        return -TROVE_ENOMEM;
    }

    if( (original_key = calloc( 2, DBPF_MAX_KEY_LENGTH )) == 0 )
    { 
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "[DBPF KEYVAL]: malloc for "
                     "key_data failed.\n");
        free(lookup_key);
        return -TROVE_ENOMEM;
    }

    /* only copy  data into key if buffer is greater than 1 (null-string) */
    if( op_p->u.v_query.key->buffer_sz > 1 )
    { 
        memcpy(lookup_key, op_p->u.v_query.key->buffer, 
            op_p->u.v_query.key->buffer_sz);
        if( op_p->u.v_query.val->buffer_sz > 1 )
        {
            /* copy at the end of the last buffer but over-write the null 
             * terminator */
            memcpy((lookup_key+(op_p->u.v_query.key->buffer_sz-1)), 
                op_p->u.v_query.val->buffer, op_p->u.v_query.val->buffer_sz);
        }
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, 
                     "[DBPF KEYVAL]: lookup_key: [%s]\n", (char *)lookup_key );
    }
    else
    {
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                     "[DBPF KEYVAL]: returning, refusing to do empty lookup\n");
        free(lookup_key);
        free(original_key);
        return -TROVE_EINVAL;
    }
    /* store the original lookup key based on key, val from v_query */
    memcpy(original_key, lookup_key, lookup_key_sz );

    /* malloc for largest possible datum as 'value' portion of query may be
     * partial */
    if( (val_datum = calloc(1, DBPF_MAX_KEY_LENGTH)) == 0)
    {
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "[DBPF KEYVAL]: malloc for "
                   " val_datum failed.\n");
        free(lookup_key);
        free(original_key);
        return -TROVE_ENOMEM;
    }

    key.data = lookup_key;
    key.ulen = (2 * DBPF_MAX_KEY_LENGTH);
    key.size = lookup_key_sz - 1;

    data.data = val_datum;
    data.size = data.ulen = DBPF_MAX_KEY_LENGTH; 

    pkey.data = &key_entry;
    pkey.size = pkey.ulen = sizeof( struct dbpf_keyval_db_entry );
    key.flags = data.flags = pkey.flags = DB_DBT_USERMEM;

    /* store requested count number */
    record_count = (*op_p->u.v_query.count);
    (*op_p->u.v_query.count) = 0; 

    /* duplicates in secondary index require use of cursor */
    if( (op_p->coll_p->keyval_secondary_db->cursor(
         op_p->coll_p->keyval_secondary_db, NULL, &dbc_p, 0)) != 0 )
    {
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                     "[DBPF KEYVAL]: Error getting cursor for "
                     "keyval_secondary: %s\n", db_strerror(ret));
        ret = -TROVE_EFAULT;
        goto return_error;
    }
    query_p = dbc_p;

    if( PVFS_KEYVAL_QUERY_UNMASK_NORM(op_p->u.v_query.query_type) ==
        PVFS_KEYVAL_QUERY_NORM )

    {
        /* if normalized query, open normalized cursor and set pointer */
        if( (op_p->coll_p->keyval_secondary_norm_db->cursor(
           op_p->coll_p->keyval_secondary_norm_db, NULL, &dbcn_p, 0)) != 0 )
        {
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                         "[DBPF KEYVAL]: Error getting cursor for "
                         "keyval_secondary_norm: %s\n", db_strerror(ret));
            ret = -TROVE_EFAULT;
            goto return_error;
        }
        query_p = dbcn_p;
    }


    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "[DBPF KEYVAL]: dbpf_keyval_read_value: "
                 "pget: key: %s/(%d)(%d), "
                 "pkey (%d)(%d), (%d)(%d), initial position: %llu on db "
                 "%s\n", 
                 (char *)key.data, key.ulen, key.size, pkey.ulen, pkey.size,
                 data.ulen, data.size, llu(*op_p->u.v_query.position_p),
                 query_p->dbp->fname);
   
    /* figure out query type and set once */
    if( (PVFS_KEYVAL_QUERY_UNMASK_QUERY(op_p->u.v_query.query_type) == 
            PVFS_KEYVAL_QUERY_LT) ||
        (PVFS_KEYVAL_QUERY_UNMASK_QUERY(op_p->u.v_query.query_type) == 
            PVFS_KEYVAL_QUERY_LE) || 
        (PVFS_KEYVAL_QUERY_UNMASK_QUERY(op_p->u.v_query.query_type) == 
            PVFS_KEYVAL_QUERY_PEQ) )
    {
        cursor_flags = DB_FIRST;
        get_flags = DB_NEXT;
    }
    else if( (PVFS_KEYVAL_QUERY_UNMASK_QUERY(op_p->u.v_query.query_type) == 
                PVFS_KEYVAL_QUERY_GT) || 
             (PVFS_KEYVAL_QUERY_UNMASK_QUERY(op_p->u.v_query.query_type) == 
                PVFS_KEYVAL_QUERY_GE) )
    {
        cursor_flags = DB_SET_RANGE;
        get_flags = DB_NEXT;
    }
    else if( (PVFS_KEYVAL_QUERY_UNMASK_QUERY(op_p->u.v_query.query_type) == 
                PVFS_KEYVAL_QUERY_NT) )
    {
        cursor_flags = DB_FIRST;
        get_flags = DB_NEXT;
    }
    else
    {
        cursor_flags = DB_SET; 
        get_flags = DB_NEXT_DUP;
    }

    /* do initial query to determine if any records exist */
    ret = query_p->c_pget(query_p, &key, &pkey, &data, cursor_flags);
    if( ret == DB_NOTFOUND )  /* no records matching request */
    {
        /* not an error, just set counts and leave. we're done regardless
         * of what the token is */
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "[DBPF KEYVAL]: dbpf_keyval_"
                     "read_value: No matching keys found in secondary index\n");
        (*op_p->u.v_query.position_p) = TROVE_ITERATE_END;
        ret = 1;
        (*op_p->u.v_query.count) = 0;
        goto return_error;
    }

    if(  (*op_p->u.v_query.position_p) != TROVE_ITERATE_START )
    {   /* if request came with position other than start, whip through them */
        local_p = 0;
        while( (ret == 0) && (local_p < (*op_p->u.v_query.position_p)) )
        {
            ret = query_p->c_pget(query_p, &key, &pkey, &data, get_flags);
            if( ret == DB_NOTFOUND )
            {
                op_p->u.v_query.handle_p[i] = 0;
                gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, 
                             "[DBPF KEYVAL]: dbpf_keyval_read_value: can't "
                             "iterate to requested position (%llu)\n",
                              *op_p->u.v_query.position_p);
                *op_p->u.v_query.position_p = TROVE_ITERATE_END;
            }
            local_p++;
        }
    }

    if( (ret != 0) && (ret != DB_NOTFOUND) )
    {
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "[DBPF KEYVAL]: dbpf_keyval_"
                     "read_value: pget error in secondary index: %s\n",
                     db_strerror(ret));
        (*op_p->u.v_query.position_p) = TROVE_ITERATE_END;
        ret = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }

    /* cursor is now in position to return requested number of records */
    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "[DBPF KEYVAL]: dbpf_keyval_read_value: cursor starting at "
                 "%llu/%s -> %s\n", llu(key_entry.handle), key_entry.key, 
                 (char *)data.data);

    if( (*op_p->u.v_query.position_p) == TROVE_ITERATE_START )
    {
        (*op_p->u.v_query.position_p) = 0;
    }

    /* the buffers that values are copied to must be big enough, passed in
    * pointers have buffers set to max allowable size. */
    while( 
            ( (*op_p->u.v_query.count) < record_count ) &&
            ( (*op_p->u.v_query.position_p) != TROVE_ITERATE_END ) && 
            ( ret == 0 )
         )
    {
        ret = dbpf_result_iterate_selector( original_key, key.data, 
                                            op_p->u.v_query.query_type);

        if( ret == 0 ) /* should include record in return set */
        {
            op_p->u.v_query.handle_p[(*op_p->u.v_query.count)] = 
                key_entry.handle;
    
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, 
                 "[DBPF KEYVAL]: dbpf_keyval_read_value: match "
                 "(%u): (%llu)\n",
                 (*op_p->u.v_query.count), 
                 llu(op_p->u.v_query.handle_p[(*op_p->u.v_query.count)]));
            (*op_p->u.v_query.count)++;
        }
        else if( ret == -1 ) /* end of what we need to add */
        { 
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, 
                         "[DBPF_KEYVAL]: dbpf_keyval_read_value: comp "
                         "function breaking on %s\n", (char *)key.data);
            *op_p->u.v_query.position_p = TROVE_ITERATE_END;
            break;
        }
        /* otherwise, it's likely junk (handle as attr) so iterate by it */

        (*op_p->u.v_query.position_p)++; 

        if( get_flags == DB_NEXT )
        {
            memset(key.data, 0, 2 * DBPF_MAX_KEY_LENGTH);
            key.size = 2 * DBPF_MAX_KEY_LENGTH;
        }
        else
        {
            key.size = lookup_key_sz - 1;
        }

        key.ulen = (2 * DBPF_MAX_KEY_LENGTH);
        key.size = lookup_key_sz - 1;
        data.ulen = data.size = DBPF_MAX_KEY_LENGTH; 
        pkey.size = pkey.ulen = sizeof( struct dbpf_keyval_db_entry );
        key.flags = data.flags = pkey.flags = DB_DBT_USERMEM;
        memset(data.data, 0, DBPF_MAX_KEY_LENGTH);
        memset(pkey.data, 0, sizeof( struct dbpf_keyval_db_entry ));
       
        /* if just iterating, clear out the key too */
        if( get_flags == DB_NEXT )
        {
            memset(key.data, 0, 2 * DBPF_MAX_KEY_LENGTH);
            key.size = 2 * DBPF_MAX_KEY_LENGTH;
        }

        ret = query_p->c_pget(query_p, &key, &pkey, &data, get_flags);
        if( ret == DB_NOTFOUND )
        {
            /* trying to get next record ran us out of records, mark the 
             * end, we're out */
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, 
                         "[DBPF KEYVAL]: dbpf_keyval_read_value: reached "
                         "end of records before filling count. "
                         "%d / %d records\n", (*op_p->u.v_query.count), 
                         record_count);
            *op_p->u.v_query.position_p = TROVE_ITERATE_END;
        }
        else if( ret != 0 )
        {
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, 
                         "[DBPF KEYVAL]: dbpf_keyval_read_value: BDB error "
                         "before filling count: %d / %d records: %s\n", 
                         i, *op_p->u.v_query.count, db_strerror(ret));
        }
    }

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, 
                 "[DBPF_KEYVAL]: dbpf_keyval_read_value: exiting: "
                 "token (%llu)\n", llu(*op_p->u.v_query.position_p));
    if( dbcn_p != NULL )
    {
        dbcn_p->c_close(dbcn_p);
    }
    if( dbc_p != NULL )
    {
        dbc_p->c_close(dbc_p);
    }
    free(lookup_key);
    free(original_key);
    free(val_datum);

    return 1;

return_error:
    if( dbcn_p != NULL )
    {
        dbcn_p->c_close(dbcn_p);
    }
    dbc_p->c_close(dbc_p);
    free(lookup_key);
    free(original_key);
    free(val_datum);
    return ret;
}

static int dbpf_keyval_write(TROVE_coll_id coll_id,
                             TROVE_handle handle,
                             TROVE_keyval_s *key_p,
                             TROVE_keyval_s *val_p,
                             TROVE_ds_flags flags,
                             TROVE_vtag_s *vtag,
                             void *user_ptr,
                             TROVE_context_id context_id,
                             TROVE_op_id *out_op_id_p,
                             PVFS_hint  hints)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    int ret;
    PINT_event_id event_id = 0;
    PINT_event_type event_type;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_WRITE,
        coll_p,
        handle,
        dbpf_keyval_write_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }

    event_type = trove_dbpf_keyval_write_event_id;
    DBPF_EVENT_START(coll_p, q_op_p, event_type, &event_id,
                     PINT_HINT_GET_CLIENT_ID(hints),
                     PINT_HINT_GET_REQUEST_ID(hints),
                     PINT_HINT_GET_RANK(hints),
                     handle,
                     PINT_HINT_GET_OP_ID(hints));

   /* initialize the op-specific members */
    op_p->u.k_write.key = *key_p;
    op_p->u.k_write.val = *val_p;
    op_p->hints = hints;

    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_ADD);

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p,
                                 event_type, event_id);
}

static int dbpf_keyval_write_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL;
    DBT key, data;
    dbpf_attr_cache_elem_t *cache_elem = NULL;
    TROVE_object_ref ref = {op_p->handle, op_p->coll_p->coll_id};
    u_int32_t dbflags = 0;
    struct dbpf_keyval_db_entry key_entry;

    if(!(op_p->flags & TROVE_BINARY_KEY))
    {
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                     "dbpf_keyval_write_op_svc: handle: %llu, key: %*s\n",
                     llu(op_p->handle),
                     op_p->u.k_write.key.buffer_sz,
                     (char *)op_p->u.k_write.key.buffer);
    }

    key_entry.handle = op_p->handle;

    assert(op_p->u.k_write.key.buffer_sz <= DBPF_MAX_KEY_LENGTH);
    memcpy(key_entry.key, 
           op_p->u.k_write.key.buffer,
           op_p->u.k_write.key.buffer_sz);

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data = &key_entry;
    key.size = key.ulen = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(
        op_p->u.k_write.key.buffer_sz);
    key.flags = DB_DBT_USERMEM;
    data.data = op_p->u.k_write.val.buffer;
    data.size = op_p->u.k_write.val.buffer_sz;

    /* If TROVE_NOOVERWRITE flag was set, make sure that we don't create the
     * key if it exists */
    if ((op_p->flags & TROVE_NOOVERWRITE))
    {
        dbflags |= DB_NOOVERWRITE;
    }
    /* if TROVE_ONLYOVERWRITE flag was set, make sure that the key exists
     * before overwriting it */
    else if ((op_p->flags & TROVE_ONLYOVERWRITE))
    {
        DBT tmpdata;

        memset(&tmpdata, 0, sizeof(tmpdata));
        tmpdata.ulen = op_p->u.k_write.val.buffer_sz;
        tmpdata.data = (void *) malloc(tmpdata.ulen);
        tmpdata.flags = DB_DBT_USERMEM;
        ret = op_p->coll_p->keyval_db->get(
            op_p->coll_p->keyval_db, NULL, &key, &tmpdata, 0);
        /* A failed get implies that keys possibly did not exist */
        if (ret != 0)
        {
            /* The only case where we are ok is val buffer
             *  is too small */
            if (tmpdata.ulen < tmpdata.size)
            {
                ret = 0;
            }
            else
            {
                if(ret != DB_NOTFOUND)
                {
                    op_p->coll_p->keyval_db->err(
                        op_p->coll_p->keyval_db, ret, "keyval_db->get");
                }
                ret = -dbpf_db_error_to_trove_error(ret);
            }
        }
        free(tmpdata.data);
        /* If there was an error, we need to return right here */
        if (ret != 0)
        {
            goto return_error;
        }
    }

    if(!(op_p->flags & TROVE_BINARY_KEY))
    {
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                     "keyval_db->put(handle= %llu, key= %*s (%d)) size=%d\n",
                     llu(key_entry.handle), 
                     op_p->u.k_write.key.buffer_sz,
                     key_entry.key,
                     op_p->u.k_write.key.buffer_sz,
                     key.size);
    }

    ret = op_p->coll_p->keyval_db->put(
        op_p->coll_p->keyval_db, NULL, &key, &data, dbflags);
    /* Either a put error or key already exists */
    if (ret != 0 )
    {
	gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
		     "keyval_db->put failed. ret=%d\n", ret);

        /*op_p->coll_p->keyval_db->err(
            op_p->coll_p->keyval_db, ret, "keyval_db->put keyval write");
	*/
        ret = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }

    if(!(op_p->flags & TROVE_BINARY_KEY))
    {
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "*** Trove KeyVal Write "
                     "of %s\n", (char *)key_entry.key);
    }

    if(op_p->flags & TROVE_NOOVERWRITE)
    {
        ret = dbpf_keyval_handle_info_ops(
            op_p, DBPF_KEYVAL_HANDLE_COUNT_INCREMENT);
        if(ret != 0)
        {
            goto return_error;
        }
    }

    /*
     * now that the data is written to disk, update the cache if it's
     * an attr keyval we manage.
     */
    if(!(op_p->flags & TROVE_BINARY_KEY))
    {
        gen_mutex_lock(&dbpf_attr_cache_mutex);
        cache_elem = dbpf_attr_cache_elem_lookup(ref);
        if (cache_elem)
        {
            if (dbpf_attr_cache_elem_set_data_based_on_key(
                    ref, key_entry.key,
                    op_p->u.k_write.val.buffer, data.size))
            {
                /*
                 * NOTE: this can happen if the keyword isn't registered,
                 * or if there is no associated cache_elem for this key
                 */
                gossip_debug(
                    GOSSIP_DBPF_ATTRCACHE_DEBUG,"** CANNOT cache data written "
                    "(key is %s)\n", (char *)key_entry.key);
            }
            else
            {
                gossip_debug(
                    GOSSIP_DBPF_ATTRCACHE_DEBUG,"*** cached keyval data "
                    "written (key is %s)\n",
                    (char *)key_entry.key);
            }
        }
        gen_mutex_unlock(&dbpf_attr_cache_mutex);
    }

    ret = DBPF_OP_COMPLETE;
    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_SUB);

return_error:
    return ret;
}

static int dbpf_keyval_remove(TROVE_coll_id coll_id,
                              TROVE_handle handle,
                              TROVE_keyval_s *key_p,
                              TROVE_keyval_s *val_p,
                              TROVE_ds_flags flags,
                              TROVE_vtag_s *vtag,
                              void *user_ptr,
                              TROVE_context_id context_id,
                              TROVE_op_id *out_op_id_p,
                              PVFS_hint  hints)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    int ret;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_REMOVE_KEY,
        coll_p,
        handle,
        dbpf_keyval_remove_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }

    /* initialize op-specific members */
    op_p->hints = hints;
    op_p->u.k_remove.key = *key_p;
    if(val_p)
    {
        op_p->u.k_remove.val = *val_p;
    }
    else
    {
        op_p->u.k_remove.val.buffer = NULL;
    }
      
    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_ADD);

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p, 0, 0);
}

static int dbpf_keyval_remove_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL;

    if(!(op_p->flags & TROVE_BINARY_KEY))
    {
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                     "dbpf_keyval_remove_op_svc: handle: %llu, key: %*s\n",
                     llu(op_p->handle),
                     op_p->u.k_remove.key.buffer_sz,
                     (char *)op_p->u.k_remove.key.buffer);
    }
                 
    ret = dbpf_keyval_do_remove(op_p->coll_p->keyval_db, 
                                op_p->handle,
                                &op_p->u.k_remove.key,
                                &op_p->u.k_remove.val);
    if (ret != 0)
    {
        goto return_error;
    }

    ret = dbpf_keyval_handle_info_ops(op_p, DBPF_KEYVAL_HANDLE_COUNT_DECREMENT);
    if(ret != 0)
    {
        goto return_error;
    }

    ret = DBPF_OP_COMPLETE;
    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_SUB);

return_error:
    return ret;
}

static int dbpf_keyval_remove_list(TROVE_coll_id coll_id,
                                  TROVE_handle handle,
                                  TROVE_keyval_s *key_array,
                                  TROVE_keyval_s *val_array,
                                  int *error_array,
                                  int count,
                                  TROVE_ds_flags flags,
                                  TROVE_vtag_s *vtag,
                                  void *user_ptr,
                                  TROVE_context_id context_id,
                                  TROVE_op_id *out_op_id_p,
                                  PVFS_hint hints)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    int ret;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_WRITE_LIST,
        coll_p,
        handle,
        dbpf_keyval_remove_list_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }

   /* initialize the op-specific members */
    op_p->u.k_remove_list.key_array = key_array;
    op_p->u.k_remove_list.val_array = val_array;
    op_p->u.k_remove_list.error_array = error_array;
    op_p->u.k_remove_list.count = count;

    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_ADD);

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p, 0, 0);
}

static int dbpf_keyval_remove_list_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL;
    DBT key, data;
    int k;
    TROVE_keyval_handle_info info;
    struct dbpf_keyval_db_entry key_entry;
    int remove_count = 0;

    /* read each key to see if it is present */
    for (k = 0; k < op_p->u.k_remove_list.count; k++)
    {
        ret = dbpf_keyval_do_remove(op_p->coll_p->keyval_db,
                                    op_p->handle,
                                    &op_p->u.k_remove_list.key_array[k],
                                    &op_p->u.k_remove_list.val_array[k]);
        if(ret != 0)
        {
            op_p->u.k_remove_list.error_array[k] = ret;
        }
        else
        {
            remove_count++;
        }
    }

    if(op_p->flags & TROVE_KEYVAL_HANDLE_COUNT)
    {
        key_entry.handle = op_p->handle;
        memset(&key, 0, sizeof(key));
        memset(&data, 0, sizeof(data));
        key.flags = DB_DBT_USERMEM;
        key.data = &key_entry;
        key.size = key.ulen = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(0);
        data.data = &info;
        data.ulen = sizeof(TROVE_keyval_handle_info);
        data.flags = DB_DBT_USERMEM;

        ret = op_p->coll_p->keyval_db->get(
            op_p->coll_p->keyval_db, NULL, &key, &data, 0);
        if(ret == DB_NOTFOUND)
        {
            /* doesn't exist yet so we can set to 0 */
            memset(&info, 0, sizeof(TROVE_keyval_handle_info));
            data.size = sizeof(TROVE_keyval_handle_info);
        }
        else if(ret != 0)
        {
            op_p->coll_p->keyval_db->err(
                op_p->coll_p->keyval_db, ret, "DB->get");
            return -dbpf_db_error_to_trove_error(ret);
        }

        info.count -= remove_count;

        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "[DBPF KEYVAL]: handle_info keyval_remove_list: handle: %llu, count: %d\n",
                 llu(op_p->handle), info.count); 

        ret = op_p->coll_p->keyval_db->put(
            op_p->coll_p->keyval_db, NULL, &key, &data, 0);
        if(ret != 0)
        {
            op_p->coll_p->keyval_db->err(
                op_p->coll_p->keyval_db, ret, 
                "keyval_db->put keyval handle info ops");
            return -dbpf_db_error_to_trove_error(ret);
        }
    }

    ret = DBPF_OP_COMPLETE;
    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_SUB);

    return ret;
}

static int dbpf_keyval_validate(TROVE_coll_id coll_id,
                                TROVE_handle handle,
                                TROVE_ds_flags flags,
                                TROVE_vtag_s *vtag,
                                void* user_ptr,
                                TROVE_context_id context_id,
                                TROVE_op_id *out_op_id_p,
                                PVFS_hint  hints)
{
    return -TROVE_ENOSYS;
}

static int dbpf_keyval_iterate(TROVE_coll_id coll_id,
                               TROVE_handle handle,
                               TROVE_ds_position *position_p,
                               TROVE_keyval_s *key_array,
                               TROVE_keyval_s *val_array,
                               int *inout_count_p,
                               TROVE_ds_flags flags,
                               TROVE_vtag_s *vtag,
                               void *user_ptr,
                               TROVE_context_id context_id,
                               TROVE_op_id *out_op_id_p,
                               PVFS_hint  hints)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    int ret;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_ITERATE,
        coll_p,
        handle,
        dbpf_keyval_iterate_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }

     /* initialize op-specific members */
    op_p->u.k_iterate.key_array = key_array;
    op_p->u.k_iterate.val_array = val_array;
    op_p->u.k_iterate.position_p = position_p;
    op_p->u.k_iterate.count_p = inout_count_p;
    op_p->hints = hints;

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p, 0, 0);
}

/* dbpf_keyval_iterate_op_svc()
 *
 * Operation:
 *
 * If position is TROVE_ITERATE_START, we set the position to the
 * start of the database (keyval space) and read, returning the
 * position of the last read keyval.
 *
 * If position is TROVE_ITERATE_END, then we hit the end previously,
 * so we just return that we are done and that there are 0 things
 * read.
 *
 * Otherwise we read and return the position of the last read keyval.
 *
 * In all cases we read using DB_NEXT.  This is ok because it behaves
 * like DB_FIRST (read the first record) when called with an
 * uninitialized cursor (so we just don't initialize the cursor in the
 * TROVE_ITERATE_START case).
 *
 */
static int dbpf_keyval_iterate_op_svc(struct dbpf_op *op_p)
{
    int count, ret;
    uint64_t tmp_pos = 0;
    PINT_dbpf_keyval_iterate_callback tmp_callback = NULL;
    int i;

    assert(*op_p->u.k_iterate.count_p > 0);

    count = *op_p->u.k_iterate.count_p;

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                  "dbpf_keyval_iterate_op_svc: starting: fsid: %u, "
                  "handle: %llu, pos: %llu\n", 
                 op_p->coll_p->coll_id, 
                 llu(op_p->handle),
                 llu(*op_p->u.k_iterate.position_p));
    
    /* if they passed in that they are at the end, return 0.
     * this seems silly maybe, but it makes while (count) loops
     * work right.
     */
    if (*op_p->u.k_iterate.position_p == TROVE_ITERATE_END)
    {
        *op_p->u.k_iterate.count_p = 0;
        return 1;
    }

    if(op_p->flags & TROVE_KEYVAL_ITERATE_REMOVE)
    {
        tmp_callback = PINT_dbpf_dspace_remove_keyval;
    }

    ret = PINT_dbpf_keyval_iterate(op_p->coll_p->keyval_db,
                                   op_p->handle,
                                   op_p->coll_p->pcache,
                                   op_p->u.k_iterate.key_array,
                                   op_p->u.k_iterate.val_array,
                                   &count,
                                   *op_p->u.k_iterate.position_p,
                                   tmp_callback);
    if (ret == -TROVE_ENOENT)
    {
        *op_p->u.k_iterate.position_p = TROVE_ITERATE_END;
    }
    else if(ret != 0)
    {
        return ret;
    }
    else
    {
        if(*op_p->u.k_iterate.position_p == TROVE_ITERATE_START)
        {
            *op_p->u.k_iterate.position_p = count;
            /* store a session identifier in the top 32 bits */
            tmp_pos += readdir_session;
            *op_p->u.k_iterate.position_p += (tmp_pos << 32);
            readdir_session++;
        }
        else
        {
            *op_p->u.k_iterate.position_p += count;
        }

        if(count != 0)
        {
            /* insert the key of the last entry read based on
             * its position
             */
            ret = PINT_dbpf_keyval_pcache_insert(
                op_p->coll_p->pcache, 
                op_p->handle,
                *op_p->u.k_iterate.position_p,
                op_p->u.k_iterate.key_array[count-1].buffer, 
                op_p->u.k_iterate.key_array[count-1].read_sz);
        }

        if(op_p->flags & TROVE_KEYVAL_ITERATE_REMOVE)
        {
            for(i=0; i<count; i++)
            {
                ret = dbpf_keyval_handle_info_ops(op_p, DBPF_KEYVAL_HANDLE_COUNT_DECREMENT);
                if(ret < 0)
                {
                    return(ret);
                }
            }
        }
    }
    
    *op_p->u.k_iterate.count_p = count;

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, 
                 "dbpf_keyval_iterate_op_svc: finished: "
                 "position: %llu, count: %d\n", 
                 llu(*op_p->u.k_iterate.position_p), *op_p->u.k_iterate.count_p);

    return 1;
}

static int dbpf_keyval_iterate_keys(TROVE_coll_id coll_id,
                                    TROVE_handle handle,
                                    TROVE_ds_position *position_p,
                                    TROVE_keyval_s *key_array,
                                    int *inout_count_p,
                                    TROVE_ds_flags flags,
                                    TROVE_vtag_s *vtag,
                                    void *user_ptr,
                                    TROVE_context_id context_id,
                                    TROVE_op_id *out_op_id_p,
                                    PVFS_hint  hints)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    int ret;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_ITERATE_KEYS,
        coll_p,
        handle,
        dbpf_keyval_iterate_keys_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }

 /* initialize op-specific members */
    op_p->u.k_iterate_keys.key_array = key_array;
    op_p->u.k_iterate_keys.position_p = position_p;
    op_p->u.k_iterate_keys.count_p = inout_count_p;
    op_p->hints = hints;

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p, 0, 0);
}

/* dbpf_keyval_iterate_keys_op_svc()
 *
 * Operation:
 *
 * If position is TROVE_ITERATE_START, we set the position to the
 * start of the database (keyval space) and read, returning the
 * position of the last read keyval.
 *
 * If position is TROVE_ITERATE_END, then we hit the end previously,
 * so we just return that we are done and that there are 0 things
 * read.
 *
 * Otherwise we read and return the position of the last read keyval.
 *
 * In all cases we read using DB_NEXT.  This is ok because it behaves
 * like DB_FIRST (read the first record) when called with an
 * uninitialized cursor (so we just don't initialize the cursor in the
 * TROVE_ITERATE_START case).
 *
 */
static int dbpf_keyval_iterate_keys_op_svc(struct dbpf_op *op_p)
{
    int count, ret;
    PINT_dbpf_keyval_iterate_callback tmp_callback = NULL;
    int i;

    count = *op_p->u.k_iterate_keys.count_p;

    /* if they passed in that they are at the end, return 0.
     * this seems silly maybe, but it makes while (count) loops
     * work right.
     */
    if (*op_p->u.k_iterate_keys.position_p == TROVE_ITERATE_END)
    {
        *op_p->u.k_iterate_keys.count_p = 0;
        return 1;
    }

    if(op_p->flags & TROVE_KEYVAL_ITERATE_REMOVE)
    {
        tmp_callback = PINT_dbpf_dspace_remove_keyval;
    }

    ret = PINT_dbpf_keyval_iterate(op_p->coll_p->keyval_db,
                                   op_p->handle,
                                   op_p->coll_p->pcache,
                                   (count != 0) ?
                                   op_p->u.k_iterate_keys.key_array : NULL,
                                   NULL,
                                   &count,
                                   *op_p->u.k_iterate_keys.position_p,
                                   tmp_callback);
    if (ret == -TROVE_ENOENT)
    {
        *op_p->u.k_iterate_keys.position_p = TROVE_ITERATE_END;
    }
    else if(ret != 0)
    {
        return ret;
    }
    else
    {
        if(*op_p->u.k_iterate_keys.position_p == TROVE_ITERATE_START)
        {
            *op_p->u.k_iterate_keys.position_p = count;
        }
        else
        {
            *op_p->u.k_iterate_keys.position_p += count;
        }

        if(count != 0 && *op_p->u.k_iterate_keys.count_p != 0)
        {
            ret = PINT_dbpf_keyval_pcache_insert(
                op_p->coll_p->pcache, 
                op_p->handle, 
                *op_p->u.k_iterate_keys.position_p,
                op_p->u.k_iterate_keys.key_array[count-1].buffer,
                op_p->u.k_iterate_keys.key_array[count-1].read_sz);
        }
        if(op_p->flags & TROVE_KEYVAL_ITERATE_REMOVE)
        {
            for(i=0; i<count; i++)
            {
                ret = dbpf_keyval_handle_info_ops(op_p, DBPF_KEYVAL_HANDLE_COUNT_DECREMENT);
                if(ret < 0)
                {
                    return(ret);
                }
            }
        }
    }

    *op_p->u.k_iterate_keys.count_p = count;
    return 1;
}


static int dbpf_keyval_read_list(TROVE_coll_id coll_id,
                                 TROVE_handle handle,
                                 TROVE_keyval_s *key_array,
                                 TROVE_keyval_s *val_array,
                                 TROVE_ds_state *err_array,
                                 int count,
                                 TROVE_ds_flags flags,
                                 TROVE_vtag_s *vtag,
                                 void *user_ptr,
                                 TROVE_context_id context_id,
                                 TROVE_op_id *out_op_id_p,
                                 PVFS_hint  hints)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    int ret;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_READ_LIST,
        coll_p,
        handle,
        dbpf_keyval_read_list_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }

    /* initialize the op-specific members */
    op_p->u.k_read_list.key_array = key_array;
    op_p->u.k_read_list.val_array = val_array;
    op_p->u.k_read_list.err_array = err_array;
    op_p->u.k_read_list.count = count;
    op_p->hints = hints;

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p, 0, 0);
}

static int dbpf_keyval_read_list_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL, i = 0;
    struct dbpf_keyval_db_entry key_entry;
    DBT key, data;
    int success_count = 0;

    for(i = 0; i < op_p->u.k_read_list.count; i++)
    {
        key_entry.handle = op_p->handle;
        memcpy(key_entry.key, 
               op_p->u.k_read_list.key_array[i].buffer,
               op_p->u.k_read_list.key_array[i].buffer_sz);

        memset(&key, 0, sizeof(key));
        key.data = &key_entry;
        key.size = key.ulen = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(
            op_p->u.k_read_list.key_array[i].buffer_sz);
        key.flags = DB_DBT_USERMEM;

        memset(&data, 0, sizeof(data));
        data.data = op_p->u.k_read_list.val_array[i].buffer;
        data.ulen = op_p->u.k_read_list.val_array[i].buffer_sz;
        data.flags = DB_DBT_USERMEM;

        ret = op_p->coll_p->keyval_db->get(
            op_p->coll_p->keyval_db, NULL, &key, &data, 0);
        if (ret != 0)
        {
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, 
                         "keyval get %s failed with error %s\n",
                         key_entry.key, db_strerror(ret));
            op_p->u.k_read_list.err_array[i] = 
                -dbpf_db_error_to_trove_error(ret);
            op_p->u.k_read_list.val_array[i].read_sz = 0;
        }
        else
        {
            success_count++;
            op_p->u.k_read_list.err_array[i] = 0;
            op_p->u.k_read_list.val_array[i].read_sz = data.size;
        }
    }

    if(success_count)
    {
        /* return success if we read at least one of the requested keys */
        return 1;
    }
    else
    {
        /* if everything failed, then return first error code */
        return(op_p->u.k_read_list.err_array[0]);
    }
}

static int dbpf_keyval_write_list(TROVE_coll_id coll_id,
                                  TROVE_handle handle,
                                  TROVE_keyval_s *key_array,
                                  TROVE_keyval_s *val_array,
                                  int count,
                                  TROVE_ds_flags flags,
                                  TROVE_vtag_s *vtag,
                                  void *user_ptr,
                                  TROVE_context_id context_id,
                                  TROVE_op_id *out_op_id_p,
                                  PVFS_hint  hints)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    int ret;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_WRITE_LIST,
        coll_p,
        handle,
        dbpf_keyval_write_list_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }

   /* initialize the op-specific members */
    op_p->u.k_write_list.key_array = key_array;
    op_p->u.k_write_list.val_array = val_array;
    op_p->u.k_write_list.count = count;
    op_p->hints = hints;

    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_ADD);

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p, 0, 0);
}

static int dbpf_keyval_write_list_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL;
    struct dbpf_keyval_db_entry key_entry;
    DBT key, data;
    dbpf_attr_cache_elem_t *cache_elem = NULL;
    TROVE_object_ref ref = {op_p->handle, op_p->coll_p->coll_id};
    int k;
    char tmpdata[PVFS_NAME_MAX];
    key_entry.handle = op_p->handle;

    /* read each key to see if it is present */
    for (k = 0; k < op_p->u.k_write_list.count; k++)
    {
        memcpy(key_entry.key, op_p->u.k_write_list.key_array[k].buffer,
               op_p->u.k_write_list.key_array[k].buffer_sz);

        memset(&key, 0, sizeof(key));
        memset(&data, 0, sizeof(data));
        key.data = &key_entry;
        key.size = key.ulen = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(
            op_p->u.k_write_list.key_array[k].buffer_sz);
        key.flags |= DB_DBT_USERMEM;

        data.data = tmpdata;
        data.ulen = PVFS_NAME_MAX;
        data.flags |= DB_DBT_USERMEM;

        ret = op_p->coll_p->keyval_db->get(
            op_p->coll_p->keyval_db, NULL, &key, &data, 0);
        /* check for DB_BUFFER_SMALL in case the key is there but the data
         * is simply too big for the temporary data buffer used
         */
#ifdef HAVE_DB_BUFFER_SMALL
        if (ret != 0 && ret != DB_BUFFER_SMALL)
#else
        if (ret != 0 && ret != ENOMEM)
#endif
        {
            if(ret == DB_NOTFOUND && ((op_p->flags & TROVE_NOOVERWRITE) ||
                                      (!(op_p->flags & TROVE_ONLYOVERWRITE))))
            {
                /* this means key is not in DB, which is what we
                 * want for the no-overwrite case - so go to the next key
                 */
                continue;
            }

            op_p->coll_p->keyval_db->err(
                op_p->coll_p->keyval_db, ret, "DB->get");
            ret = -dbpf_db_error_to_trove_error(ret);
            goto return_error;
        }
    }

    for (k = 0; k < op_p->u.k_write_list.count; k++)
    {
        memcpy(key_entry.key, op_p->u.k_write_list.key_array[k].buffer,
               op_p->u.k_write_list.key_array[k].buffer_sz);

        memset(&key, 0, sizeof(key));
        memset(&data, 0, sizeof(data));

        key.data = &key_entry;
        key.size = key.ulen = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(
            op_p->u.k_write_list.key_array[k].buffer_sz);

        data.flags = 0;
        /* allow NULL val array (writes an empty value to each position */
        if(!op_p->u.k_write_list.val_array)
        {
            data.data = NULL;
            data.size = data.ulen = 0;
        }
        else
        {
            data.data = op_p->u.k_write_list.val_array[k].buffer;
            data.size = data.ulen = op_p->u.k_write_list.val_array[k].buffer_sz;
        }

        if(!(op_p->flags & TROVE_BINARY_KEY))
        {
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                         "keyval_db->put(handle= %llu, key= %*s (%d)) size=%d\n",
                         llu(key_entry.handle), 
                         op_p->u.k_write_list.key_array[k].buffer_sz,
                         key_entry.key,
                         op_p->u.k_write_list.key_array[k].buffer_sz,
                         key.size);
        }

        ret = op_p->coll_p->keyval_db->put(
            op_p->coll_p->keyval_db, NULL, &key, &data, 0);
        if (ret != 0)
        {
            op_p->coll_p->keyval_db->err(
                op_p->coll_p->keyval_db, ret, 
                "keyval_db->put keyval write list");
            ret = -dbpf_db_error_to_trove_error(ret);
            goto return_error;
        }

        if(!(op_p->flags & TROVE_BINARY_KEY))
        {
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "*** Trove KeyVal Write "
                         "of %s\n", (char *)op_p->u.k_write_list.key_array[k].buffer);
        }

        if(op_p->flags & TROVE_NOOVERWRITE)
        {
            ret = dbpf_keyval_handle_info_ops(
                op_p, DBPF_KEYVAL_HANDLE_COUNT_INCREMENT);
            if(ret != 0)
            {
                goto return_error;
            }
        }

        /*
           now that the data is written to disk, update the cache if it's
           an attr keyval we manage.
           */
        if(!(op_p->flags & TROVE_BINARY_KEY))
        {
            gen_mutex_lock(&dbpf_attr_cache_mutex);
            cache_elem = dbpf_attr_cache_elem_lookup(ref);
            if (cache_elem)
            {
                if (dbpf_attr_cache_elem_set_data_based_on_key(
                        ref, key_entry.key,
                        data.data, data.size))
                {
                    /*
    NOTE: this can happen if the keyword isn't registered,
    or if there is no associated cache_elem for this key
    */
                    gossip_debug(
                        GOSSIP_DBPF_ATTRCACHE_DEBUG,"** CANNOT cache data written "
                        "(key is %s)\n", 
                        (char *)key_entry.key);
                }
                else
                {
                    gossip_debug(
                        GOSSIP_DBPF_ATTRCACHE_DEBUG,"*** cached keyval data "
                        "written (key is %s)\n",
                        (char *)key_entry.key);
                }
            }
            gen_mutex_unlock(&dbpf_attr_cache_mutex);
        }
    }

    ret = DBPF_OP_COMPLETE;
    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_SUB);

return_error:
    return ret;
}

static int dbpf_keyval_flush(TROVE_coll_id coll_id,
                             TROVE_handle handle,
                             TROVE_ds_flags flags,
                             void *user_ptr,
                             TROVE_context_id context_id,
                             TROVE_op_id *out_op_id_p,
                             PVFS_hint  hints)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    int ret;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_FLUSH,
        coll_p,
        handle,
        dbpf_keyval_flush_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }
    op_p->hints = hints;

    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p, 0, 0);
}

static int dbpf_keyval_flush_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL;

    if ((ret = op_p->coll_p->keyval_db->sync(
                op_p->coll_p->keyval_db, 0)) != 0)
    {
        ret = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }

    return 1;

return_error:
    return ret;
}    

int PINT_dbpf_keyval_iterate(
    DB *db_p,
    TROVE_handle handle,
    PINT_dbpf_keyval_pcache *pcache,    
    TROVE_keyval_s *keys_array,
    TROVE_keyval_s *values_array,
    int *count,
    TROVE_ds_position pos,
    PINT_dbpf_keyval_iterate_callback callback)
{

    int ret = -TROVE_EINVAL, i=0, get_key_count=0;
    DBC *dbc_p = NULL;
    char keybuffer[PVFS_NAME_MAX];
    TROVE_keyval_s skey;
    TROVE_keyval_s *key;
    TROVE_keyval_s *val = NULL;

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "Entered: PINT_dpbf_keyval_iterate\n");

    skey.buffer = keybuffer;
    skey.buffer_sz = PVFS_NAME_MAX;
    key = &skey;

    ret = db_p->cursor(db_p, NULL, &dbc_p, 0);
    if (ret != 0)
    {
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                     "Exited: PINT_dpbf_keyval_iterate\n");

        gossip_lerr("db_p->cursor failed: db error %s\n", db_strerror(ret));
        *count = 0;
        return -dbpf_db_error_to_trove_error(ret);
    }

    if(pos == TROVE_ITERATE_START)
    {
        ret = dbpf_keyval_iterate_get_first_entry(handle, dbc_p);
        if(ret != 0)
        {
            goto return_error;
        }

        ret = DBPF_ITERATE_CURRENT_POSITION; 
    }
    else
    {
        ret = dbpf_keyval_iterate_skip_to_position(
            handle, pos, pcache, dbc_p);
        if(ret != 0 && ret != DBPF_ITERATE_CURRENT_POSITION)
        {
            goto return_error;
        }
    }

    if (*count == 0)
    {
        get_key_count = 1;
    }

    if(ret == DBPF_ITERATE_CURRENT_POSITION)
    {
        if(keys_array)
        {
            key = &keys_array[0];
            if(values_array)
            {
                val = &values_array[0];
            }
        }

        ret = dbpf_keyval_iterate_cursor_get(
            handle, dbc_p,  key, val, DB_CURRENT);
        if(ret != 0)
        {
            goto return_error;
        }

        if(callback)
        {
            key->buffer_sz = key->read_sz;
            ret = callback(dbc_p, handle, key, NULL);
            if(ret != 0)
            {
                goto return_error;
            }
        }
        
        i = 1;
    }

    for(; i < *count || get_key_count; ++i)
    {
        if(keys_array)
        {
            key = &keys_array[i];
            if(values_array)
            {
                val = &values_array[i];
            }
        }
        else
        {
            /* must reset buffer_sz */
            key->buffer_sz = PVFS_NAME_MAX;
        }

        ret = dbpf_keyval_iterate_cursor_get(
            handle, dbc_p, key, val, DB_NEXT);
        if(ret != 0)
        {
            goto return_error;
        }

        #if 0 
        /* not safe to print this if binary keys may be present */
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "iterate key: %*s, val: %llu\n", 
                     key->read_sz, (char *)key->buffer, 
                     (val ? llu(*(PVFS_handle *)val->buffer) : 0));
        #endif

        if(callback)
        {
            key->buffer_sz = key->read_sz;
            ret = callback(dbc_p, handle, key, NULL);
            if(ret != 0)
            {
                goto return_error;
            }
        }
    }

return_error:

    *count = i;

    /* free the cursor */
    if(dbc_p)
    {
        dbc_p->c_close(dbc_p);
    }

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "Exited: PINT_dpbf_keyval_iterate\n");

    return ret;
}

static int dbpf_keyval_do_remove(
    DB *db_p, TROVE_handle handle, TROVE_keyval_s *key, TROVE_keyval_s *val)
{
    int ret;
    struct dbpf_keyval_db_entry key_entry;
    DBT db_key, db_val;

    #if 0
    /* not safe to print this if it may be a binary key */
    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "PINT_dbpf_keyval_remove: handle (%llu), key: (%d) %*s\n",
                 llu(handle), key->buffer_sz, key->buffer_sz, (char *)key->buffer);
    #endif

    key_entry.handle = handle;
    memcpy(key_entry.key, key->buffer, key->buffer_sz);

    memset(&db_key, 0, sizeof(db_key));
    db_key.data = &key_entry;
    db_key.size = db_key.ulen = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(key->buffer_sz);
    db_key.flags = DB_DBT_USERMEM;

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "keyval_db->del(handle= %llu, key= %*s (%d)) size=%d\n",
                 llu(key_entry.handle),
                 key->buffer_sz,
                 key_entry.key,
                 key->buffer_sz,
                 db_key.size);


    if(val && val->buffer)
    {
        memset(&db_val, 0, sizeof(DBT));
        db_val.flags |= DB_DBT_USERMEM;
        db_val.data = val->buffer;
        db_val.ulen = val->buffer_sz;
        ret = db_p->get(db_p, NULL, &db_key, &db_val, 0);
        if(ret != 0)
        {
            ret = -dbpf_db_error_to_trove_error(ret);
        }
        val->read_sz = db_val.size;
    }

    ret = db_p->del(db_p, NULL, &db_key, 0);
    if(ret != 0)
    {
        ret = -dbpf_db_error_to_trove_error(ret);
    }

    return ret;
}

static int dbpf_keyval_iterate_get_first_entry(
    TROVE_handle handle, DBC * dbc_p)
{
    int ret = 0;
    TROVE_keyval_s key;

    key.buffer = "\0";
    key.buffer_sz = 0;

    /* use Berkeley DB's DB_SET_RANGE functionality to move the cursor
     * to the first matching entry after the key with the specified handle.
     * This is done by creating a key that has a null component string.
     */
    ret = dbpf_keyval_iterate_cursor_get(
        handle, dbc_p, &key, NULL, DB_SET_RANGE);
    if(ret != 0)
    {
        return ret;
    }

    if(key.buffer_sz == 0)
    {
        /* skip handle_info */
        ret = dbpf_keyval_iterate_cursor_get(
            handle, dbc_p, &key, NULL, DB_NEXT);
    }

    return 0;
}

static int dbpf_keyval_iterate_skip_to_position(
    TROVE_handle handle,
    TROVE_ds_position pos,
    PINT_dbpf_keyval_pcache *pcache,
    DBC * dbc_p)
{
    int ret = 0;
    TROVE_keyval_s key;

    assert(pos != TROVE_ITERATE_START);

    memset(&key, 0, sizeof(TROVE_keyval_s));

    ret = PINT_dbpf_keyval_pcache_lookup(
        pcache, handle, pos, 
        (const void **)&key.buffer, &key.buffer_sz);
    if(ret == -PVFS_ENOENT)
    {
        /* if the lookup fails (because the server was restarted)
         * we fall back to stepping through all the entries to get
         * to the position
         */
        /* strip the session out of the position; we need to use a true
         * integer offset if we get past the cache
         */
        pos = pos & 0xffffffff;
        return dbpf_keyval_iterate_step_to_position(handle, pos, dbc_p);
    }

    ret = dbpf_keyval_iterate_cursor_get(
        handle, dbc_p, &key, NULL, DB_SET);
    if(ret == -TROVE_ENOENT)
    {
        /* cache lookup succeeded but entry is no longer in the DB, so
         * we assume its been deleted in the interim and we just set to
         * the next one with SET_RANGE
         */

        ret = dbpf_keyval_iterate_cursor_get(
            handle, dbc_p, &key, NULL, DB_SET_RANGE);
        if(ret != 0)
        {
            return ret;
        }

        return DBPF_ITERATE_CURRENT_POSITION;
    }

    return ret;
}

static int dbpf_keyval_iterate_step_to_position(
    TROVE_handle handle,
    TROVE_ds_position pos,
    DBC * dbc_p)
{
    int i = 0;
    int ret;
    TROVE_keyval_s key;

    assert(pos != TROVE_ITERATE_START);

    ret = dbpf_keyval_iterate_get_first_entry(handle, dbc_p);
    if(ret != 0)
    {
        return ret;
    }

    for(i = 0; i < pos; ++i)
    {
        memset(&key, 0, sizeof(TROVE_keyval_s));

        ret = dbpf_keyval_iterate_cursor_get(
            handle, dbc_p, &key, NULL, DB_NEXT);
        if(ret != 0)
        {
            return ret;
        }
    }

    return 0;
}

/**
 * dbpf_keyval_iterate_cursor_get is part of a set of iterate functions
 * that abstact the DB functions so to allow us to iterate over directory
 * entries and xattrs easily.  This function takes the handle and fills in
 * the key and value to get in the iteration step.  The iterate step can
 * be to set with the db_flags parameter to the initial iterate position 
 * (DB_SET_RANGE), get the next position (DB_NEXT), or even get the current 
 * position (DB_CURRENT).
 *
 * The key parameter is filled in up to the space available specified in
 * buffer_sz.  The read_sz value is set to the amount filled in.
 * Finally, if the buffer_sz is less than the size of the available key
 * length, buffer_sz is set to the size of the available key.  Note that
 * buffer_sz may end up being more than it was set to when key was passed
 * in, this is useful for the SET_RANGE flag when checking for the null
 * keyval string (handle info item) which must be skipped over.
 *
 * The data parameter can be null, in which case only the key is filled in.
 */
static int dbpf_keyval_iterate_cursor_get(
    TROVE_handle handle,
    DBC * dbc_p,
    TROVE_keyval_s * key,
    TROVE_keyval_s * data,
    uint32_t db_flags)
{
    int ret;
    struct dbpf_keyval_db_entry key_entry;
    DBT db_key, db_data;
    char dummy_data[PVFS_NAME_MAX];
    int key_sz;

    key_entry.handle = handle;

    assert(key->buffer_sz >= 0);
    if(key->buffer_sz != 0)
    {
        memcpy(key_entry.key, key->buffer, key->buffer_sz);
    }

    memset(&db_key, 0, sizeof(DBT));
    db_key.data = &key_entry;
    db_key.size = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(key->buffer_sz);
    db_key.ulen = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(DBPF_MAX_KEY_LENGTH);
    db_key.flags |= DB_DBT_USERMEM;

    memset(&db_data, 0, sizeof(DBT));
    db_data.flags |= DB_DBT_USERMEM;

    if(data)
    {
        db_data.data = data->buffer;
        db_data.ulen = data->buffer_sz;
    }
    else
    {
        db_data.data = dummy_data;
        db_data.ulen = PVFS_NAME_MAX;
    }

    ret = dbc_p->c_get(dbc_p, &db_key, &db_data, db_flags);
    if (ret == DB_NOTFOUND)
    {
        return -TROVE_ENOENT;
    }
#ifdef HAVE_DB_BUFFER_SMALL
    else if(ret == DB_BUFFER_SMALL) 
#else
    else if(ret == ENOMEM)
#endif
    {
        db_data.data = malloc(db_data.size);
        if(!db_data.data)
        {
            return -TROVE_ENOMEM;
        }

        db_data.ulen = db_data.size;

        ret = dbc_p->c_get(dbc_p, &db_key, &db_data, db_flags);
        if(ret == DB_NOTFOUND)
        {
            return -TROVE_ENOENT;
        }
            
        if(data)
        {
            memcpy(data->buffer, db_data.data, data->buffer_sz);
            free(db_data.data);
            data->read_sz = data->buffer_sz;
        }
    }

    if (ret != 0)
    {
        gossip_lerr("Failed to perform cursor get:"
                    "\n\thandle: %llu\n\ttype: %d\n\tdb error: %s\n",
                    llu(key_entry.handle), db_flags, db_strerror(ret));
        return -dbpf_db_error_to_trove_error(ret);
    }

    if(key_entry.handle != handle)
    {
            return -TROVE_ENOENT;
    }

    key_sz = (DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(db_key.size) > key->buffer_sz) ?
        key->buffer_sz : DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(db_key.size);

    memcpy(key->buffer, key_entry.key, key_sz);

    /* only adjust the buffer size if the key didn't fit into the
     * buffer
     */
    if(key->buffer_sz < DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(db_key.size))
    {
        key->buffer_sz = DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(db_key.size);
    }

    key->read_sz = key_sz;
    
    if(data)
    {
        data->read_sz = (data->buffer_sz < db_data.size) ?
            data->buffer_sz : db_data.size;
    }

    return 0;
}

static int dbpf_keyval_get_handle_info(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_flags flags,
    TROVE_keyval_handle_info *info,
    void * user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id *out_op_id_p,
    PVFS_hint  hints)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_op op;
    struct dbpf_op *op_p;
    struct dbpf_collection *coll_p = NULL;
    int ret;

    coll_p = dbpf_collection_find_registered(coll_id);
    if(coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    ret = dbpf_op_init_queued_or_immediate(
        &op, &q_op_p,
        KEYVAL_GET_HANDLE_INFO,
        coll_p,
        handle,
        dbpf_keyval_get_handle_info_op_svc,
        flags,
        NULL,
        user_ptr,
        context_id,
        &op_p);
    if(ret < 0)
    {
        return ret;
    }

    op_p->u.k_get_handle_info.info = info;
    op_p->hints = hints;

    PINT_perf_count(PINT_server_pc, PINT_PERF_METADATA_KEYVAL_OPS,
                    1, PINT_PERF_ADD);
    return dbpf_queue_or_service(op_p, q_op_p, coll_p, out_op_id_p, 0, 0);
}

static int dbpf_keyval_get_handle_info_op_svc(struct dbpf_op * op_p)
{
    DBT key, data;
    int ret = -TROVE_EINVAL;
    struct dbpf_keyval_db_entry key_entry;

    memset(&key_entry, 0, sizeof(key_entry));
    key_entry.handle = op_p->handle;
    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data = &key_entry;
    key.size = key.ulen = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(0);
    key.flags = DB_DBT_USERMEM;
    data.data = op_p->u.k_get_handle_info.info;
    data.ulen = sizeof(TROVE_keyval_handle_info);
    data.flags = DB_DBT_USERMEM;

    ret = op_p->coll_p->keyval_db->get(
        op_p->coll_p->keyval_db, NULL, &key, &data, 0);
    if(ret != 0)
    {
        if(ret != DB_NOTFOUND)
        {
            op_p->coll_p->keyval_db->err(
                op_p->coll_p->keyval_db, ret, "keyval_db->get (handle info)");
        }

        return -dbpf_db_error_to_trove_error(ret);
    }

    gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                 "[DBPF KEYVAL]: handle_info get: handle: %llu, count: %d\n",
                 llu(op_p->handle), op_p->u.k_get_handle_info.info->count); 

    return 1;
}    

/**
 * keyval attrs are special parameters that can exist as metadata for
 * a keyval or set of keyvals (such as all the keyvals for directory
 * entries).  The keys for these special keyvals are the handle 
 * and a null string.
 */
static int dbpf_keyval_handle_info_ops(struct dbpf_op * op_p, 
                                       enum dbpf_handle_info_action action)
{
    DBT key, data;
    int ret = -TROVE_EINVAL;
    TROVE_keyval_handle_info info;
    struct dbpf_keyval_db_entry key_entry;

    if(op_p->flags & TROVE_KEYVAL_HANDLE_COUNT)
    {
        key_entry.handle = op_p->handle;
        memset(&key, 0, sizeof(key));
        memset(&data, 0, sizeof(data));
        key.flags = DB_DBT_USERMEM;
        key.data = &key_entry;
        key.size = key.ulen = DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(0);
        data.data = &info;
        data.ulen = sizeof(TROVE_keyval_handle_info);
        data.flags = DB_DBT_USERMEM;

        ret = op_p->coll_p->keyval_db->get(
            op_p->coll_p->keyval_db, NULL, &key, &data, 0);
        if(ret == DB_NOTFOUND)
        {
            /* doesn't exist yet so we can set to 0 */
            memset(&info, 0, sizeof(TROVE_keyval_handle_info));
            data.size = sizeof(TROVE_keyval_handle_info);
        }
        else if(ret != 0)
        {
            op_p->coll_p->keyval_db->err(
                op_p->coll_p->keyval_db, ret, "DB->get");
            return -dbpf_db_error_to_trove_error(ret);
        }
       
        if(action == DBPF_KEYVAL_HANDLE_COUNT_INCREMENT)
        {
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                         "[DBPF KEYVAL]: handle_info "
                         "count increment: handle: %llu, value: %d\n",
                         llu(op_p->handle), info.count);
            info.count++;
        }
        else if(action == DBPF_KEYVAL_HANDLE_COUNT_DECREMENT)
        {
            if(info.count <= 0)
            {
                gossip_lerr(
                     "[DBPF KEYVAL]: ERROR: handle_info "
                     "count decrement: handle: %llu, value: %d\n",
                     llu(op_p->handle), info.count);
            }
            assert(info.count > 0);

            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                         "[DBPF KEYVAL]: handle_info "
                         "count decrement: handle: %llu, value: %d\n",
                         llu(op_p->handle), info.count);
            info.count--;

            if(info.count == 0)
            {
                gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                             "[DBPF KEYVAL]: handle_info "
                             "count decremented to zero, removing keyval\n");
                /* special case if we get down to zero remove this
                 * keyval as well
                 */
                op_p->coll_p->keyval_db->del(
                    op_p->coll_p->keyval_db, NULL, &key, 0);
                if(ret != 0)
                {
                    op_p->coll_p->keyval_db->err(
                        op_p->coll_p->keyval_db, ret, "DB->del");
                    return -dbpf_db_error_to_trove_error(ret);
                }

                return 0;
            }
        }

        ret = op_p->coll_p->keyval_db->put(
            op_p->coll_p->keyval_db, NULL, &key, &data, 0);
        if(ret != 0)
        {
            op_p->coll_p->keyval_db->err(
                op_p->coll_p->keyval_db, ret, 
                "keyval_db->put keyval handle info ops");
            return -dbpf_db_error_to_trove_error(ret);
        }
    }

    return 0;
}

/* return 0 or 1 if a is part of the result set for b and query */
static int dbpf_result_iterate_selector(char *a, char *b, 
                                        uint32_t query)
{

    int max_len = (strlen(a)>strlen(b)?strlen(a):strlen(b));
    if( strncmp(b, "user.", 5) != 0 )
    {
        /* if key doesn't begin with user. it's not a valid attribute 
         * if less than, just don't include it. if it's greater we're done */
        if( PVFS_KEYVAL_QUERY_UNMASK_NORM(query)==PVFS_KEYVAL_QUERY_LT ||
            PVFS_KEYVAL_QUERY_UNMASK_NORM(query)==PVFS_KEYVAL_QUERY_LE || 
            PVFS_KEYVAL_QUERY_UNMASK_NORM(query)==PVFS_KEYVAL_QUERY_NT )
        {
            return 1;
        }
        else if( PVFS_KEYVAL_QUERY_UNMASK_QUERY(query)==PVFS_KEYVAL_QUERY_GT ||
                 PVFS_KEYVAL_QUERY_UNMASK_QUERY(query)==PVFS_KEYVAL_QUERY_GE ||
                 PVFS_KEYVAL_QUERY_UNMASK_QUERY(query)==PVFS_KEYVAL_QUERY_EQ ||
                 PVFS_KEYVAL_QUERY_UNMASK_QUERY(query)==PVFS_KEYVAL_QUERY_PEQ )
        {
            return -1;
        }
    }

    if( PVFS_KEYVAL_QUERY_UNMASK_QUERY(query) == PVFS_KEYVAL_QUERY_LT )
    {
        if( memcmp( b, a, max_len ) < 0 )
        {
            return 0;
        }
        else
        {   /* time to stop, we've passed the keys */
            return -1;
        }

    }
    else if( PVFS_KEYVAL_QUERY_UNMASK_QUERY(query) == PVFS_KEYVAL_QUERY_LE )
    {
        if( memcmp( b, a, max_len) <= 0 )
        {
            return 0;
        }
        else
        {   /* time to stop, we've passed the keys */
            return -1;
        }
    }
    else if( PVFS_KEYVAL_QUERY_UNMASK_QUERY(query) == PVFS_KEYVAL_QUERY_EQ )
    {
        if( memcmp( b, a, max_len) == 0 )
        {
            return 0;
        }
        else
        {   /* should only see equal keys in here */
            return -1;
        }
    
    }
    else if( PVFS_KEYVAL_QUERY_UNMASK_QUERY(query) == PVFS_KEYVAL_QUERY_PEQ )
    {
        if( memcmp( b, a, strlen(a)) == 0 )
        {
            return 0;
        }
        else if( memcmp( b, a, strlen(a) ) > 0 )
        { 
            return -1;
        }
        else
        {
            return 1;
        }
    
    }
    else if( PVFS_KEYVAL_QUERY_UNMASK_QUERY(query) == PVFS_KEYVAL_QUERY_GE )
    {
        if( memcmp( b, a, max_len) >= 0 )
        {
            return 0;
        }
        else
        {   /* something funny (or a bug) happened*/
            return -1;
        }
    
    }
    else if( PVFS_KEYVAL_QUERY_UNMASK_QUERY(query) == PVFS_KEYVAL_QUERY_GT )
    {
        /* will be called starting with equal keys */
        if( memcmp( b, a, max_len) == 0 )
        {
            return 1;
        }
        else if( memcmp( b, a, max_len) > 0 )
        {
            return 0;
        }
        else
        {   /* something funny (or a bug) happened*/
            return -1;
        }
    
    }
    else if( PVFS_KEYVAL_QUERY_UNMASK_QUERY(query) == PVFS_KEYVAL_QUERY_NT )
    {
        if( memcmp( b, a, max_len) != 0 )
        {
            return 0;
        }
        else
        {
            return 1;
        }
    }
    return 1;
}

 /* constructs secondary key for keyval_secondary db. the value of the
  * primary data is returned. */
int PINT_trove_dbpf_keyval_secondary_callback(
    DB *secondary, const DBT *pkey, const DBT *pdata, DBT *skey)
{
    struct dbpf_keyval_db_entry *k;
    void *key_data;

    memset( skey, 0, sizeof(DBT));
    k = (struct dbpf_keyval_db_entry *)pkey->data;

    /* for attributes prefixed with user create a secondary key of the form
     * <attribute><value> */
    if( ( ( pkey->size - sizeof(PVFS_handle)) > strlen("user.")) && 
          ( memcmp(k->key, "user.", 5) == 0 ) )
    {
        /* size of new key is length of the attribute plus length of value */
        if( (key_data = calloc( 1, (strlen(k->key) + pdata->size + 1) )) == 0 )
        {
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                         "[DBPF KEYVAL]: malloc for secondary_callback "
                         "for new attribute/value key failed.\n");
            return TROVE_ENOMEM;
        }
    
        /* copy attribute to start of key */
        memcpy(key_data, k->key, strlen(k->key) );
    
        /* copy value directly after key */
        memcpy((key_data + strlen(k->key)), pdata->data, pdata->size);
        skey->ulen = skey->size = strlen(k->key) + pdata->size;
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "[DBPF KEYVAL]: "
                     "PINT_trove_dbpf_keyval_secondary_callback "
                     "adding secondary key (%s) (%d)\n", (char *)key_data, 
                     skey->ulen);
    }
    else if((pdata->size == sizeof(TROVE_handle)) && 
            (strcmp("dh", k->key)!=0) &&
            ! (pkey->size>=sizeof(TROVE_handle)+SPECIAL_PARENT_KEYLEN &&
              strncmp(k->key, SPECIAL_PARENT_KEYSTR, SPECIAL_PARENT_KEYLEN)==0)
           )
    {
        /* for items with a value size of 8 and attribute not "dh" 
         * (an initial pass at only adding<dir handle><filename> -> <handle> )
         * create a key of only the value (handle) */
        if( (key_data = malloc(pdata->size) ) == 0 )
        {
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                         "[DBPF KEYVAL]: malloc for secondary_callback "
                         "failed when creating handle key.\n");
            return TROVE_ENOMEM;
        }
        memset(key_data, 0, (pdata->size));
        memcpy(key_data, pdata->data, pdata->size );
        skey->ulen = skey->size = pdata->size;
        gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG, "[DBPF KEYVAL]: "
                     "PINT_trove_dbpf_keyval_secondary_callback "
                     "adding secondary key (%llu) (%d) for (%llu)(%s) pkey\n", 
                     llu(*(uint64_t *)key_data), skey->ulen, llu(k->handle),
                     k->key);
    }
    else
    {
        return DB_DONOTINDEX;
    }

    skey->data = key_data;
    skey->flags = DB_DBT_APPMALLOC;
    return 0;
}

 /* constructs secondary key for keyval_secondary_norm db. the value of the
  * primary data is returned. */
int PINT_trove_dbpf_keyval_secondary_norm_callback(
    DB *secondary_norm, const DBT *pkey, const DBT *pdata, DBT *skey)
{
    struct dbpf_keyval_db_entry *k;
    char *key_data;
    int i = 0;

    memset( skey, 0, sizeof(DBT));
    k = (struct dbpf_keyval_db_entry *)pkey->data;

    /* for attributes prefixed with user create a secondary key normalized 
     * of the form <attribute><value> */
    if( ( pkey->size > ((sizeof(PVFS_handle) + strlen("user."))) ) &&
        ( memcmp(k->key, "user.", 5) == 0) )
    {
        /* size of new key is length of the attribute plus length of value */
        if( (key_data = malloc(strlen(k->key)+strlen(pdata->data)+1) ) == 0 )
        {
            gossip_debug(GOSSIP_DBPF_KEYVAL_DEBUG,
                         "[DBPF KEYVAL]: malloc for secondary_callback "
                         "for new attribute/value key failed.\n");
            return TROVE_ENOMEM;
        }
        memset(key_data, 0, (strlen(k->key) + strlen(pdata->data)+1));
    
        for( i = 0; i < strlen(k->key); i++ )
        {
            key_data[i] = tolower( k->key[i] );
        }

        for( i = 0; i < strlen(pdata->data); i++ )
        {
            key_data[i+strlen(k->key)] = tolower( ((char *)pdata->data)[i] );
        }
        skey->ulen = skey->size = strlen(key_data) + 1;
    }
    else
    {
        return DB_DONOTINDEX;
    }

    skey->data = key_data;
    skey->flags = DB_DBT_APPMALLOC;
    return 0;
}


int PINT_trove_dbpf_keyval_compare(
    DB * dbp, const DBT * a, const DBT * b)
{
    const struct dbpf_keyval_db_entry * db_entry_a;
    const struct dbpf_keyval_db_entry * db_entry_b;

    db_entry_a = (const struct dbpf_keyval_db_entry *) a->data;
    db_entry_b = (const struct dbpf_keyval_db_entry *) b->data;

    if(db_entry_a->handle != db_entry_b->handle)
    {
        return (db_entry_a->handle < db_entry_b->handle) ? -1 : 1;
    }

    if(a->size > b->size)
    {
        return 1;
    }

    if(a->size < b->size)
    {
        return -1;
    }

    /* must be equal */
    return (memcmp(db_entry_a->key, db_entry_b->key, 
                    DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(a->size)));
}

int PINT_trove_dbpf_keyval_secondary_compare(
    DB * dbp, const DBT * a, const DBT * b)
{
    const struct dbpf_keyval_db_entry * db_entry_a;
    const struct dbpf_keyval_db_entry * db_entry_b;

    db_entry_a = (const struct dbpf_keyval_db_entry *) a->data;
    db_entry_b = (const struct dbpf_keyval_db_entry *) b->data;

    if( a->size > 5 && b->size > 5 )
    {
        if( strncmp(a->data, "user.", 5) == 0 )
        {
            if( strncmp(b->data, "user.", 5) == 0 )
            {
                return strncmp(a->data, b->data, 
                    ( ( a->size > b->size ) ? b->size : a->size) ); 
            }
            else
            {
                return -1; /* a is an attr, b is not (a is less) */
            }
        }
        else 
        {
            if( strncmp(b->data, "user.", 5) == 0 )
            {
                return 1; /* b is an attr, a is not (b is greater) */
            }
        }
    }

    if(db_entry_a->handle != db_entry_b->handle)
    {
        return (db_entry_a->handle < db_entry_b->handle) ? -1 : 1;
    }

    if(a->size > b->size)
    {
        return 1;
    }

    if(a->size < b->size)
    {
        return -1;
    }

    /* must be equal */
    return (memcmp(db_entry_a->key, db_entry_b->key, 
                    DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(a->size)));
}

struct TROVE_keyval_ops dbpf_keyval_ops =
{
    dbpf_keyval_read,
    dbpf_keyval_read_value_path,
    dbpf_keyval_read_value_query,
    dbpf_keyval_write,
    dbpf_keyval_remove,
    dbpf_keyval_remove_list,
    dbpf_keyval_validate,
    dbpf_keyval_iterate,
    dbpf_keyval_iterate_keys,
    dbpf_keyval_read_list,
    dbpf_keyval_write_list,
    dbpf_keyval_flush,
    dbpf_keyval_get_handle_info
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
