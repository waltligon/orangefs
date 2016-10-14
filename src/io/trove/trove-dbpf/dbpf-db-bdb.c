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
        case DB_KEYEMPTY:
            return TROVE_ENOENT;
        case DB_KEYEXIST:
            return TROVE_EEXIST;
        case DB_LOCK_DEADLOCK:
            return TROVE_EDEADLK;
        case DB_LOCK_NOTGRANTED:
            return TROVE_ENOLCK;
        case DB_RUNRECOVERY:
            gossip_err("Error: DB_RUNRECOVERY encountered.\n");
            return TROVE_EIO;
    }
    return DBPF_ERROR_UNKNOWN; /* return some identifiable value */
}

static int ds_attr_compare(DB *dbp, const DBT *a, const DBT *b)
{
    TROVE_handle *handle_a = (TROVE_handle *)a->data;
    TROVE_handle *handle_b = (TROVE_handle *)b->data;
    if (a->size < sizeof(TROVE_handle) || b->size < sizeof(TROVE_handle))
    {
        gossip_err("DBPF dspace collection corrupt\n");
        abort();
    }
    if (*handle_a == *handle_b)
    {
        return 0;
    }
    return (*handle_a > *handle_b) ? -1 : 1;
}

static int keyval_compare(DB *dbp, const DBT *a, const DBT *b)
{
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
    if (db_entry_a->handle != db_entry_b->handle)
    {
        return (db_entry_a->handle < db_entry_b->handle) ? -1 : 1;
    }

    if (db_entry_a->type != db_entry_b->type)
    {
        return (db_entry_a->type < db_entry_b->type) ? -1 : 1;
    }

    if (a->size > b->size)
    {
        return 1;
    }
    else if (a->size < b->size)
    {
        return -1;
    }

    return memcmp(db_entry_a->key, db_entry_b->key,
            DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(a->size));
}

int dbpf_db_open(char *name, int compare, struct dbpf_db **db,
    int create, struct server_configuration_s *cfg)
{
    int r;
    *db = malloc(sizeof **db);
    if (!*db)
    {
        return errno;
    }
    r = db_create(&(*db)->db, NULL, 0);
    if (r)
    {
        free(*db);
        return db_error(r);
    }
 
    r = (*db)->db->set_flags((*db)->db, 0);
    if (r)
    {
        gossip_err("TROVE:DBPF:Berkeley DB %s failed to set_flags", name);
        (*db)->db->close((*db)->db, 0);
        free(*db);
        return db_error(r);
    }
    if (compare == DBPF_DB_COMPARE_DS_ATTR)
    {
        (*db)->db->set_bt_compare((*db)->db, ds_attr_compare);
    }
    else if (compare == DBPF_DB_COMPARE_KEYVAL)
    {
        (*db)->db->set_bt_compare((*db)->db, keyval_compare);
    }

    if (cfg)
    {
        (*db)->db->set_cachesize((*db)->db, 0, cfg->db_cache_size_bytes, 1);
    }

    if (cfg && strcmp(cfg->db_cache_type, "mmap") == 0)
    {
        r = (*db)->db->open((*db)->db, NULL, name, NULL, DB_BTREE,
            (create ? DB_CREATE : 0)|DB_DIRTY_READ|DB_THREAD, 0600);
    }
    else
    {
        r = (*db)->db->open((*db)->db, NULL, name, NULL, DB_BTREE,
            (create ? DB_CREATE : 0)|DB_DIRTY_READ|DB_THREAD|DB_NOMMAP, 0600);
    }
    if (r)
    {
        gossip_err("TROVE:DBPF:Berkeley DB %s failed to open", name);
        (*db)->db->close((*db)->db, 0);
        free(*db);
        return db_error(r);
    }
    return 0;
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

int dbpf_db_get(struct dbpf_db *db, struct dbpf_data *key,
    struct dbpf_data *val)
{
    DBT db_key, db_data;
    int r;
    db_key.data = key->data;
    db_key.ulen = db_key.size = key->len;
    db_key.flags = DB_DBT_USERMEM;
    db_data.data = val->data;
    db_data.ulen = val->len;
    db_data.flags = DB_DBT_USERMEM;
    r = db->db->get(db->db, NULL, &db_key, &db_data, 0);
    if (r == DB_BUFFER_SMALL)
    {
        db_data.data = malloc(db_data.size);
        if (! db_data.data)
        {
            return -TROVE_ENOMEM;
        }

        db_data.ulen = db_data.size;

        r = db->db->get(db->db, NULL, &db_key, &db_data, 0);

        if (r == 0 && val)
        {
            memcpy(val->data, db_data.data, val->len);
        }
        free(db_data.data);
    }
    else if (r)
    {
        return db_error(r);
    }
    val->len = db_data.size;
    return 0;
}

int dbpf_db_put(struct dbpf_db *db, struct dbpf_data *key,
    struct dbpf_data *val)
{
    DBT db_key, db_data;
    db_key.data = key->data;
    db_key.ulen = db_key.size = key->len;
    db_key.flags = DB_DBT_USERMEM;
    db_data.data = val->data;
    db_data.ulen = db_data.size = val->len;
    db_data.flags = DB_DBT_USERMEM;
    return db_error(db->db->put(db->db, NULL, &db_key, &db_data, 0));
}

int dbpf_db_putonce(struct dbpf_db *db, struct dbpf_data *key,
    struct dbpf_data *val)
{
    DBT db_key, db_data;
    db_key.data = key->data;
    db_key.ulen = db_key.size = key->len;
    db_key.flags = DB_DBT_USERMEM;
    db_data.data = val->data;
    db_data.ulen = db_data.size = val->len;
    db_data.flags = DB_DBT_USERMEM;
    return db_error(db->db->put(db->db, NULL, &db_key, &db_data,
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

int dbpf_db_cursor_get(struct dbpf_cursor *dbc, struct dbpf_data *key,
    struct dbpf_data *val, int op, size_t maxkeylen)
{
    DBT db_key, db_data;
    int r, flags=0;
    db_key.data = key->data;
    db_key.size = key->len;
    db_key.ulen = maxkeylen;
    db_key.flags = DB_DBT_USERMEM;
    db_data.data = val->data;
    db_data.ulen = val->len;
    db_data.flags = DB_DBT_USERMEM;
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
    r = dbc->dbc->c_get(dbc->dbc, &db_key, &db_data, flags);
    if (r == DB_BUFFER_SMALL)
    {
        db_data.data = malloc(db_data.size);
        if (! db_data.data)
        {
            return -TROVE_ENOMEM;
        }

        db_data.ulen = db_data.size;

        r = dbc->dbc->c_get(dbc->dbc, &db_key, &db_data, flags);

        if (r == 0 && val)
        { 
            memcpy(val->data, db_data.data, val->len);
        }
        free(db_data.data);
    }
    if (r)
    {
        return db_error(r);
    }
    key->len = db_key.size;
    val->len = db_data.size;
    return 0;
}

int dbpf_db_cursor_del(struct dbpf_cursor *dbc)
{
    return db_error(dbc->dbc->c_del(dbc->dbc, 0));
}
