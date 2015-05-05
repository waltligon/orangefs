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

static int ds_attr_compare(DB *dbp, const DBT *a, const DBT *b)
{
    TROVE_handle handle_a, handle_b;

    memcpy(&handle_a, a->data, sizeof(TROVE_handle));
    memcpy(&handle_b, b->data, sizeof(TROVE_handle));

    if (handle_a == handle_b)
    {
        return 0;
    }
    return (handle_a > handle_b) ? -1 : 1;
}

static int keyval_compare(DB *dbp, const DBT *a, const DBT *b)
{
    struct dbpf_keyval_db_entry db_entry_a, db_entry_b;

    memcpy(&db_entry_a, a->data, sizeof(struct dbpf_keyval_db_entry));
    memcpy(&db_entry_b, b->data, sizeof(struct dbpf_keyval_db_entry));

    if (db_entry_a.handle != db_entry_b.handle)
    {
        return (db_entry_a.handle < db_entry_b.handle) ? -1 : 1;
    }

    if (db_entry_a.type != db_entry_b.type)
    {
        return (db_entry_a.type < db_entry_b.type) ? -1 : 1;
    }

    if (a->size > b->size)
    {
        return 1;
    }
    else if (a->size < b->size)
    {
        return -1;
    }
    /* else must be equal */
    return (memcmp(db_entry_a.key, db_entry_b.key,
        DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(a->size)));
}

int dbpf_db_open(char *name, int flags, int compare, struct dbpf_db **db)
{
    int r;
    *db = malloc(sizeof **db);
    if (!db)
    {
        return errno;
    }
    r = db_create(&(*db)->db, NULL, 0);
    if (r)
    {
        free(db);
        return r;
    }
 
    r = (*db)->db->set_flags((*db)->db, flags);
    if (r)
    {
        gossip_err("TROVE:DBPF:Berkeley DB %s failed to set_flags", name);
        (*db)->db->close((*db)->db, 0);
        free(db);
        return r;
    }
    if (compare == DBPF_DB_COMPARE_DS_ATTR)
    {
        (*db)->db->set_bt_compare((*db)->db, ds_attr_compare);
    }
    else if (compare == DBPF_DB_COMPARE_KEYVAL)
    {
        (*db)->db->set_bt_compare((*db)->db, keyval_compare);
    }
    r = (*db)->db->open((*db)->db, NULL, name, NULL, TROVE_DB_TYPE,
        TROVE_DB_OPEN_FLAGS, 0);
    if (r)
    {
        gossip_err("TROVE:DBPF:Berkeley DB %s failed to open", name);
        (*db)->db->close((*db)->db, 0);
        free(db);
        return r;
    }
    return 0;
}

int dbpf_db_close(struct dbpf_db *db)
{
    int r;
    r = db->db->close(db->db, 0);
    free(db);
    return r;
}

int dbpf_db_sync(struct dbpf_db *db)
{
    return db->db->sync(db->db, 0);
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
    if (r != 0)
    {
        switch (r) {
        case DB_BUFFER_SMALL:
            return ERANGE;
        case DB_KEYEMPTY:
            return ENOENT;
        case DB_NOTFOUND:
            return ENOENT;
        }
        return r;
    }
    if (val->len < db_data.size)
    {
        val->len = db_data.size;
        return ERANGE;
    }
    val->len = db_data.size;
    return 0;
}

int dbpf_db_put(struct dbpf_db *db, struct dbpf_data *key,
    struct dbpf_data *val)
{
    DBT db_key, db_data;
    int r;
    db_key.data = key->data;
    db_key.ulen = db_key.size = key->len;
    db_key.flags = DB_DBT_USERMEM;
    db_data.data = val->data;
    db_data.ulen = db_data.size = val->len;
    db_data.flags = DB_DBT_USERMEM;
    r = db->db->put(db->db, NULL, &db_key, &db_data, 0);
    if (r != 0)
    {
        switch (r) {
        case DB_NOTFOUND:
            return ENOENT;
        }
    }
    return r;
}

int dbpf_db_putonce(struct dbpf_db *db, struct dbpf_data *key,
    struct dbpf_data *val)
{
    DBT db_key, db_data;
    int r;
    db_key.data = key->data;
    db_key.ulen = db_key.size = key->len;
    db_key.flags = DB_DBT_USERMEM;
    db_data.data = val->data;
    db_data.ulen = db_data.size = val->len;
    db_data.flags = DB_DBT_USERMEM;
    r = db->db->put(db->db, NULL, &db_key, &db_data, DB_NOOVERWRITE);
    if (r != 0)
    {
        switch (r) {
        case DB_NOTFOUND:
            return ENOENT;
        }
    }
    return r;
}

int dbpf_db_del(struct dbpf_db *db, struct dbpf_data *key)
{
    DBT db_key;
    int r;
    db_key.data = key->data;
    db_key.ulen = db_key.size = key->len;
    db_key.flags = DB_DBT_USERMEM;
    r = db->db->del(db->db, NULL, &db_key, 0);
    if (r != 0)
    {
        switch (r) {
        case DB_NOTFOUND:
            return ENOENT;
        }
    }
    return r;
}
