/*
 * Copyright 2015 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <gossip.h>

#include <db.h>

#include "dbpf.h"

#include "server-config.h"

struct dbpf_db {
    DB *db;
};

struct dbpf_cursor {
    DBC *dbc;
};

static PVFS_error db_error(int db_error_value)
{
    /* values greater than zero are errno values */
    if (db_error_value > 0)
    {
        return trove_errno_to_trove_error(db_error_value);
    }

    switch (db_error_value)
    {
        case 0:
            return 0;
        case DB_NOTFOUND:
            return -TROVE_ENOENT;
        case DB_KEYEMPTY:
            return -TROVE_EINVAL;
        case DB_KEYEXIST:
            return -TROVE_EEXIST;
        case DB_LOCK_DEADLOCK:
            return -TROVE_EDEADLK;
        case DB_LOCK_NOTGRANTED:
            return -TROVE_ENOLCK;
        case DB_RUNRECOVERY:
            gossip_err("Error: DB_RUNRECOVERY encountered.\n");
            return -TROVE_EIO;
    }
    return DBPF_ERROR_UNKNOWN; /* return some identifiable value */
}

/* WARNING: his function does NOT compare two ds_attr as the name suggests
 * it appears to compare two handles in DBT structures.
 */
static int ds_attr_compare(DB *dbp, const DBT *a, const DBT *b)
{
    int cmpval;
    TROVE_handle *handle_a = (TROVE_handle *)a->data;
    TROVE_handle *handle_b = (TROVE_handle *)b->data;

    if (a->size < sizeof(TROVE_handle) || b->size < sizeof(TROVE_handle))
    {
        gossip_err("DBPF dspace collection corrupt\n");
        abort();
    }

    if (!(cmpval = PVFS_OID_cmp(handle_a, handle_b)))
    {
        return 0;
    }

    return (cmpval > 0) ? DBPF_LT : 0;
    //return (cmpval > 0) ? -1 : 1;
}

static int keyval_compare(DB *dbp, const DBT *a, const DBT *b)
{
    int cmpval;
    struct dbpf_keyval_db_entry *db_entry_a;
    struct dbpf_keyval_db_entry *db_entry_b;

    db_entry_a = (struct dbpf_keyval_db_entry *)a->data;
    db_entry_b = (struct dbpf_keyval_db_entry *)b->data;

    /* If the key is so small that it cannot contain the struct, our database
     * is corrupt. Then compare the handle, type, and size. Finally if only the
     * key data itself differs, compare it. */

    if (a->size < DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(0) ||
            b->size < DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(0))
    {
        gossip_err("DBPF keyval collection corrupt\n");
        abort();
    }

    if ((cmpval = PVFS_OID_cmp(&db_entry_a->handle, &db_entry_b->handle)))
    {
        return (cmpval < 0) ? DBPF_ERROR : 0;
        //return (cmpval < 0) ? -1 : 1;
    }

    if (db_entry_a->type != db_entry_b->type)
    {
        return (db_entry_a->type < db_entry_b->type) ? DBPF_LT : DBPF_EQ;
        //return (db_entry_a->type < db_entry_b->type) ? -1 : 1;
    }

    if (a->size > b->size)
    {
        return DBPF_GT;
        //return 1;
    }
    else if (a->size < b->size)
    {
        return DBPF_LT;
        //return -1;
    }

    return memcmp(db_entry_a->key,
                  db_entry_b->key,
                  DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(a->size));
}

int dbpf_db_open(char *name,
                 int compare,
                 struct dbpf_db **db,
                 int create,
                 struct server_configuration_s *cfg)
{
    int r;

    *db = malloc(sizeof **db);
    if (!*db)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: malloc failed\n", __func__);
        return -TROVE_ENOMEM;
        //return errno;
    }

    r = db_create(&(*db)->db, NULL, 0);

    if (r)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: db_create failed\n", __func__);
        free(*db);
        return db_error(r);
    }
 
    r = (*db)->db->set_flags((*db)->db, 0);

    if (r)
    {
        gossip_err("%s: Berkeley DB %s failed to set_flags\n", __func__, name);
        (*db)->db->close((*db)->db, 0);
        free(*db);
        return db_error(r);
    }
    if (compare == DBPF_DB_COMPARE_DS_ATTR)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: compare ds_attr\n", __func__);
        (*db)->db->set_bt_compare((*db)->db, ds_attr_compare);
    }
    else if (compare == DBPF_DB_COMPARE_KEYVAL)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: compare keyval\n", __func__);
        (*db)->db->set_bt_compare((*db)->db, keyval_compare);
    }

    if (cfg)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: set cache size\n", __func__);
        (*db)->db->set_cachesize((*db)->db, 0, cfg->db_cache_size_bytes, 1);
    }

    if (cfg && strcmp(cfg->db_cache_type, "mmap") == 0)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: open mmap\n", __func__);
        r = (*db)->db->open((*db)->db,
                            NULL,
                            name,
                            NULL,
                            DB_BTREE,
                            (create ? DB_CREATE : 0) |
                                    DB_DIRTY_READ | DB_THREAD,
                            0600);
    }
    else
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: open nommap\n", __func__);
        r = (*db)->db->open((*db)->db,
                            NULL,
                            name,
                            NULL,
                            DB_BTREE,
                            (create ? DB_CREATE : 0) | DB_DIRTY_READ |
                                    DB_THREAD | DB_NOMMAP,
                            0600);
    }
    if (r)
    {
        gossip_err("%s: Berkeley DB %s failed to open\n", __func__, name);
        (*db)->db->close((*db)->db, 0);
        free(*db);
        return db_error(r);
    }
    return db_error(r);
}

int dbpf_db_close(struct dbpf_db *db)
{
    int r;
    r = db->db->close(db->db, 0);
    free(db);
    return db_error(r);
}

int dbpf_db_sync(struct dbpf_db *db)
{
    return db_error(db->db->sync(db->db, 0));
}

int dbpf_db_get(struct dbpf_db *db,
                struct dbpf_data *key,
                struct dbpf_data *val)
{
    DBT db_key, db_val;
    int r, ret;

    /* debug prints key and val info */
    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: key->data(%p) (%d) : val->data(%p) (%d)\n", 
                 __func__, key->data, (int)key->len, val->data, (int)val->len);
    if (key->len >= sizeof(TROVE_handle))
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: key handle %s\n", __func__,
                     PVFS_OID_str((TROVE_handle *)&(((struct dbpf_keyval_db_entry *)key->data)->handle)));
    }
    else
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: key buffer %s\n", __func__,
                     (char *)&(((struct dbpf_keyval_db_entry *)key->data)->handle));
    }

    /* clear out DBT structures */
    memset(&db_key, 0, sizeof(DBT));
    memset(&db_val, 0, sizeof(DBT));

    /* allocate and set DBT structures */
    /* db_key first */
    db_key.ulen = key->len;
    db_key.data = (void *)malloc(db_key.ulen);
    //gossip_debug(GOSSIP_TROVE_DEBUG, "%s: db_key %p db_key.data %p\n", __func__, &db_key, db_key.data);
    if (!db_key.data)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s:malloc failed for db_key.data\n", __func__);
        ret = -TROVE_ENOMEM;
        goto errorout;
    }
    db_key.size = db_key.ulen;
    db_key.flags = DB_DBT_USERMEM;
    memset(db_key.data, 0, key->len);
    memcpy(db_key.data, key->data, key->len);

    /* then db_val */
    db_val.ulen = val->len;
    db_val.data = (void *)malloc(db_val.ulen);
    if (!db_val.data)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s:malloc failed for db_val.data\n", __func__);
        ret = -TROVE_ENOMEM;
        goto errorout;
    }
    db_val.size = 0; /* no user data in here yet */
    db_val.flags = DB_DBT_USERMEM;
    memset(db_val.data, 0, db_val.ulen);

    /* debug prints db_key and db_val info */
    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: db_key.data(%p) (%d, %d) : db_val.data(%p) (%d)\n", 
                 __func__, db_key.data, (int)db_key.size, (int)db_key.ulen, db_val.data, (int)db_val.ulen);
    if (db_key.ulen >= sizeof(TROVE_handle))
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: db_key handle %s\n", __func__,
                     PVFS_OID_str((TROVE_handle *)&(((struct dbpf_keyval_db_entry *)db_key.data)->handle)));
    }
    else
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: db_key buffer %s\n", __func__,
                     (char *)&(((struct dbpf_keyval_db_entry *)db_key.data)->handle));
    }
    /* done with print */

    r = db->db->get(db->db, NULL, &db_key, &db_val, 0);
    gossip_debug(GOSSIP_TROVE_DEBUG,
                 "%s: return from db->db->get r = %d\n", __func__, r);
    ret = db_error(r);

    /* debug prints db_key and db_val info */
    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: db_key.data(%p) (%d, %d) : db_val.data(%p) (%d)\n", 
                 __func__, db_key.data, (int)db_key.size, (int)db_key.ulen, db_val.data, (int)db_val.ulen);
    if (db_key.ulen >= sizeof(TROVE_handle))
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: db_key handle %s\n", __func__,
                     PVFS_OID_str((TROVE_handle *)&(((struct dbpf_keyval_db_entry *)db_key.data)->handle)));
    }
    else
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: db_key buffer %s\n", __func__,
                     (char *)&(((struct dbpf_keyval_db_entry *)db_key.data)->handle));
    }
    /* done with print */

    if (r == DB_BUFFER_SMALL)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "%s: db->db->get returns DB_BUFFER_SMALL\n", __func__);

        /* db_key */
        if (db_key.size > db_key.ulen)
        {
            db_key.ulen = db_key.size; /* set new buffer size */
            free(db_key.data);
            db_key.data = malloc(db_key.ulen);
        }
        else
        {
            db_key.ulen = key->len;
        }
        db_key.size = db_key.ulen;
        db_key.flags = DB_DBT_USERMEM;
        memset(db_key.data, 0, db_key.ulen);
        memcpy(db_key.data, key->data, key->len);
        /* db_val */
        if (db_val.size > db_val.ulen)
        {
            db_val.ulen = db_val.size; /* set new buffer size */
            free(db_val.data);
            db_val.data = malloc(db_val.ulen);
        }
        else
        {
            db_val.ulen = val->len;
        }
        db_val.size = db_val.ulen;
        db_val.flags = DB_DBT_USERMEM;
        memset(db_val.data, 0, db_val.ulen);

        /* ready to retry */
        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "%s: RETRY db->db->get ksize=%d, kulen=%d, vsize=%d, vulen=%d\n",
                     __func__, db_key.size, db_key.ulen, db_val.size, db_val.ulen);

        r = db->db->get(db->db, NULL, &db_key, &db_val, 0);
        ret = db_error(r);

        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "%s: RETRY return from db->db->get r = %d\n", __func__, r);

        if (r == 0 && val)
        {
            if (val->len < db_val.size)
            {
                gossip_err("%s: returned data too big for buffer passed in\n",
                             __func__);
                goto errorout;
            }
            // done below
            //memcpy(val->data, db_val.data, db_val.size);
            ret = 0;
        }
        goto returning;
    }
    else if (ret == -TROVE_ENOENT)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "%s: db->db->get returns ENOENT\n", __func__);
    }
    else if (r)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "%s: db->db->get returns other error\n", __func__);
    }

returning:
    assert(db_val.size <= val->len);
    memcpy(val->data, db_val.data, db_val.size);
    val->len = db_val.size;
    {
        char emsg[256];
        PVFS_strerror_r(ret, emsg, 256);
        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "%s: returning %d(%s)\n", __func__, ret, emsg);
    }
errorout:
    free(db_val.data);
    db_val.data = NULL;
    free(db_key.data);
    db_key.data = NULL;
    //return 0;
    return ret;
}

int dbpf_db_put(struct dbpf_db *db,
                struct dbpf_data *key,
                struct dbpf_data *val)
{
    int ret;
    DBT db_key, db_val;

    memset(&db_key, 0, sizeof(DBT));
    memset(&db_val, 0, sizeof(DBT));
    db_key.data = key->data;
    db_key.ulen = db_key.size = key->len;
    db_key.flags = DB_DBT_USERMEM;
    db_val.data = val->data;
    db_val.ulen = db_val.size = val->len;
    db_val.flags = DB_DBT_USERMEM;

    ret = db->db->put(db->db, NULL, &db_key, &db_val, 0);

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: return (%d) key (%p) %s (%d, %d) (%d, %d)\n", 
                 __func__,  ret,
                 (void *)&(((struct dbpf_keyval_db_entry *)db_key.data)->handle),
                 db_key.size >= sizeof(TROVE_handle) ?
                     PVFS_OID_str((TROVE_handle *)&(((struct dbpf_keyval_db_entry *)db_key.data)->handle)) :
                     (char *)&(((struct dbpf_keyval_db_entry *)db_key.data)->handle),
                 db_key.size, db_key.ulen, db_val.size, db_val.ulen);

    return db_error(ret);
}

int dbpf_db_putonce(struct dbpf_db *db,
                    struct dbpf_data *key,
                    struct dbpf_data *val)
{
    DBT db_key, db_val;

    db_key.data = key->data;
    db_key.ulen = db_key.size = key->len;
    db_key.flags = DB_DBT_USERMEM;
    db_val.data = val->data;
    db_val.ulen = db_val.size = val->len;
    db_val.flags = DB_DBT_USERMEM;

    return db_error(db->db->put(db->db,
                                NULL,
                                &db_key,
                                &db_val,
                                DB_NOOVERWRITE));
}

int dbpf_db_del(struct dbpf_db *db, struct dbpf_data *key)
{
    DBT db_key;

    db_key.data = key->data;
    db_key.ulen = db_key.size = key->len;
    db_key.flags = DB_DBT_USERMEM;

    return db_error(db->db->del(db->db, NULL, &db_key, 0));
}

int dbpf_db_cursor(struct dbpf_db *db, struct dbpf_cursor **dbc, int rdonly)
{
    int r;
    *dbc = malloc(sizeof **dbc);
    if (!*dbc)
    {
        return errno;
    }
    r = db->db->cursor(db->db, NULL, &(*dbc)->dbc, 0);
    if (r)
    {
        free(*dbc);
        return db_error(r);
    }
    return 0;
}

int dbpf_db_cursor_close(struct dbpf_cursor *dbc)
{
    int r;
    r = dbc->dbc->c_close(dbc->dbc);
    free(dbc);
    return db_error(r);
}

int dbpf_db_cursor_get(struct dbpf_cursor *dbc,
                       struct dbpf_data *key,
                       struct dbpf_data *val,
                       int op,
                       size_t maxkeylen)
{
    DBT db_key, db_val;
    int r, flags = 0;

    db_key.data = key->data;
    db_key.size = key->len;
    db_key.ulen = maxkeylen;
    db_key.flags = DB_DBT_USERMEM;
    db_val.data = val->data;
    db_val.ulen = val->len;
    db_val.flags = DB_DBT_USERMEM;

    if (op == DBPF_DB_CURSOR_NEXT)
    {
        flags = DB_NEXT;
    }
    else if (op == DBPF_DB_CURSOR_CURRENT)
    {
        flags = DB_CURRENT;
    }
    else if (op == DBPF_DB_CURSOR_SET)
    {
        flags = DB_SET;
    }
    else if (op == DBPF_DB_CURSOR_SET_RANGE)
    {
        flags = DB_SET_RANGE;
    }
    else if (op == DBPF_DB_CURSOR_FIRST)
    {
        flags = DB_FIRST;
    }

    r = dbc->dbc->c_get(dbc->dbc, &db_key, &db_val, flags);

    if (r == DB_BUFFER_SMALL)
    {
        db_val.data = malloc(db_val.size);
        if (! db_val.data)
        {
            return -TROVE_ENOMEM;
        }

        db_val.ulen = db_val.size;

        r = dbc->dbc->c_get(dbc->dbc, &db_key, &db_val, flags);

        if (r == 0 && val)
        { 
            memcpy(val->data, db_val.data, val->len);
        }
        free(db_val.data);
    }
    if (r)
    {
        return db_error(r);
    }
    key->len = db_key.size;
    val->len = db_val.size;
    return 0;
}

int dbpf_db_cursor_del(struct dbpf_cursor *dbc)
{
    return db_error(dbc->dbc->c_del(dbc->dbc, 0));
}
