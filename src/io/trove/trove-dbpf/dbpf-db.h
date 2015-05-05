/*
 * Copyright 2015 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

typedef struct dbpf_db dbpf_db;

/* XXX: This belongs in dbpf-db.c. */
#include <db.h>
typedef struct dbpf_db
{
    DB *db;
} dbpf_db;

struct dbpf_data
{
    void *data;
    size_t len;
};

#define DBPF_DB_COMPARE_DS_ATTR 1
#define DBPF_DB_COMPARE_KEYVAL 2

int dbpf_db_open(char *, int, int, struct dbpf_db **);
int dbpf_db_close(struct dbpf_db *);
int dbpf_db_sync(struct dbpf_db *);
int dbpf_db_get(struct dbpf_db *, struct dbpf_data *, struct dbpf_data *);
int dbpf_db_put(struct dbpf_db *, struct dbpf_data *, struct dbpf_data *);
int dbpf_db_putonce(struct dbpf_db *, struct dbpf_data *, struct dbpf_data *);
int dbpf_db_del(struct dbpf_db *, struct dbpf_data *);
