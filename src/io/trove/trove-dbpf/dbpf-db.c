/*
 * Copyright 2015 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <pvfs2-internal.h>

#include <db.h>

#include "dbpf-db.h"

int dbpf_db_open(DB *bdb, struct dbpf_db **db)
{
    *db = malloc(sizeof **db);
    if (!db)
    {
        return errno;
    }
    (*db)->db = bdb;
    return 0;
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
