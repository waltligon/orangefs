/*
 * Copyright 2015 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

typedef struct dbpf_db dbpf_db;

typedef struct dbpf_cursor dbpf_cursor;

struct dbpf_data
{
    void *data;
    size_t len;
};

#define DBPF_DB_COMPARE_DS_ATTR 1
#define DBPF_DB_COMPARE_KEYVAL 2

#define DBPF_DB_CURSOR_NEXT 0
#define DBPF_DB_CURSOR_CURRENT 1
#define DBPF_DB_CURSOR_SET 2
#define DBPF_DB_CURSOR_SET_RANGE 3
#define DBPF_DB_CURSOR_FIRST 4

int dbpf_db_open(char *, int, int, dbpf_db **, int);
int dbpf_db_close(dbpf_db *);
int dbpf_db_sync(dbpf_db *);
int dbpf_db_get(dbpf_db *, struct dbpf_data *, struct dbpf_data *);
int dbpf_db_put(dbpf_db *, struct dbpf_data *, struct dbpf_data *);
int dbpf_db_putonce(dbpf_db *, struct dbpf_data *, struct dbpf_data *);
int dbpf_db_del(dbpf_db *, struct dbpf_data *);
int dbpf_db_cursor(dbpf_db *, dbpf_cursor **);
int dbpf_db_cursor_close(dbpf_cursor *);
int dbpf_db_cursor_get(struct dbpf_cursor *, struct dbpf_data *,
    struct dbpf_data *, int, size_t);
int dbpf_db_cursor_del(dbpf_cursor *);
