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
