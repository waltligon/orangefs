/*
 * Copyright 2015 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

/* XXX: This belongs in dbpf-db.c. */
#include <db.h>
typedef struct dbpf_db
{
    DB *db;
} dbpf_db;

typedef struct dbpf_db dbpf_db;

struct dbpf_data
{
    void *data;
    size_t len;
};

int dbpf_db_open(DB *, struct dbpf_db **);
int dbpf_db_get(struct dbpf_db *, struct dbpf_data *, struct dbpf_data *);
