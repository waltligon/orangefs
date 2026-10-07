/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
 */

/* Same kind of layer as src/io/trove/trove-dbpf/dbpf-db.h.
 * SID cache code calls these functions. The LMDB calls live in
 * sidcache-db-lmdb.c. A Berkeley DB backend would be another
 * implementation of this header, the way dbpf-db-bdb.c is for Trove.
 * That file has not been written. configure leaves this cache on
 * sidcache-db-lmdb.c. Trove's --with-db-backend switch does not
 * select a SID cache backend.
 *
 * One environment holds every named database so a primary record and
 * its indexes commit together. Trove opens a separate environment
 * per database. The SID cache uses one environment, because those
 * updates are one transaction.
 */

#ifndef SIDCACHE_DB_H
#define SIDCACHE_DB_H 1

#include <stddef.h>

typedef struct sid_env sid_env;
typedef struct sid_db sid_db;
typedef struct sid_cursor sid_cursor;

/* A key or a value. Same fields as struct dbpf_data. */
struct sid_data
{
    void *data;
    size_t len;
};

/* Cursor positions. The numeric values match dbpf-db.h. */
#define SID_DB_CURSOR_NEXT      0
#define SID_DB_CURSOR_CURRENT   1
#define SID_DB_CURSOR_SET       2
#define SID_DB_CURSOR_SET_RANGE 3
#define SID_DB_CURSOR_FIRST     4
#define SID_DB_CURSOR_NEXT_DUP  5
#define SID_DB_CURSOR_GET_BOTH  6

#define SID_DB_COMPARE_MEM  0
#define SID_DB_COMPARE_TYPE 1
#define SID_DB_DUP          1

/* Open or close the process-private environment at path. */
int sid_db_env_open(const char *path, sid_env **env);
int sid_db_env_close(sid_env *env);

/* Open the named database. compare is SID_DB_COMPARE_*.
 * flags is 0 or SID_DB_DUP. */
int sid_db_open(sid_env *env, const char *name, int compare, int flags,
                sid_db **db);

/* Retrieve *key* into *val*. If val->data is NULL, it is malloc'd
 * and the caller frees it. */
int sid_db_get(sid_db *db, struct sid_data *key, struct sid_data *val);

/* Put, overwriting an existing key. */
int sid_db_put(sid_db *db, struct sid_data *key, struct sid_data *val);

/* Put only when the key is absent. Returns -PVFS_EEXIST if it is there. */
int sid_db_putonce(sid_db *db, struct sid_data *key, struct sid_data *val);

/* Put a duplicate. An existing duplicate is success. */
int sid_db_putdup(sid_db *db, struct sid_data *key, struct sid_data *val);

/* Remove *key*. If val is non-NULL, remove only that duplicate. */
int sid_db_del(sid_db *db, struct sid_data *key, struct sid_data *val);

/* Hold one transaction across several databases. */
int sid_db_txn_begin(sid_env *env, int rdonly);
int sid_db_txn_commit(sid_env *env);
int sid_db_txn_abort(sid_env *env);

int sid_db_cursor(sid_db *db, sid_cursor **dbc, int rdonly);
int sid_db_cursor_close(sid_cursor *dbc);

/* FIRST, NEXT, NEXT_DUP, SET, and SET_RANGE malloc val->data.
 * FIRST, NEXT, and SET_RANGE also malloc key->data.
 * SET and GET_BOTH use the key the caller passed.
 * GET_BOTH uses both buffers as the lookup pair and mallocs nothing.
 * The caller frees every buffer this function mallocs.
 */
int sid_db_cursor_get(sid_cursor *dbc, struct sid_data *key,
                      struct sid_data *val, int op);

int sid_db_cursor_count(sid_cursor *dbc, size_t *count);

#endif
