/*
 * Copyright 2015, 2016 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

/* Dbpf_db is a thin abstraction used by Trove/DBPF to access a key-value
 * database. It should be possible to implement a new database backend
 * with no more than a new implementation of this library and some
 * Makefile glue. */

/* These are only used as pointers so that they can be internal to the
 * specific database implementation in use. */
typedef struct dbpf_db dbpf_db;
typedef struct dbpf_cursor dbpf_cursor;

/* This represents a key or a value. */
struct dbpf_data
{
    void *data;
    size_t len;
};

/* These are the compare argument to dbpf_db_open. Trove/DBPF can store
 * two key types and therefore must use two key comparison methods. */
#define DBPF_DB_COMPARE_DS_ATTR 1
#define DBPF_DB_COMPARE_KEYVAL 2

/* A cursor is an object which is used to iterate through the keys in a
 * collection. These are used to control which keys are returned. */
#define DBPF_DB_CURSOR_NEXT 0
#define DBPF_DB_CURSOR_CURRENT 1
#define DBPF_DB_CURSOR_SET 2
#define DBPF_DB_CURSOR_SET_RANGE 3
#define DBPF_DB_CURSOR_FIRST 4

/* dbpf_db_open(name, compare, db, create, cfg): Open the database
 * called *name* which uses key format *compare* and store a pointer
 * in *db*. The *create* argument is true if the database should be
 * created. It is an error to specify *create* when the database already
 * exists. The *cfg* argument is the server configuration and may be
 * used to find database-specific arguments. */
int dbpf_db_open(char *, int, dbpf_db **, int,
    struct server_configuration_s *);

/* dbpf_db_close(db): Close the database *db*. */
int dbpf_db_close(dbpf_db *);

/* dbpf_db_sync(db): Update the on-disk copy of database *db*. */
int dbpf_db_sync(dbpf_db *);

/* dbpf_db_get(db, key, val): Retrieve value for *key* in *db* into
 * *val*. */
int dbpf_db_get(dbpf_db *, struct dbpf_data *, struct dbpf_data *);

/* dbpf_db_put(db, key, val): Put value for *key* in *db* into
 * *val*, overwriting if necessary. */
int dbpf_db_put(dbpf_db *, struct dbpf_data *, struct dbpf_data *);

/* dbpf_db_putonce(db, key, val): Put value for *key* in *db* into
 * *val*, returning an error if it already exists. */
int dbpf_db_putonce(dbpf_db *, struct dbpf_data *, struct dbpf_data *);

/* dbpf_db_del(db, key): Remove value for *key* in *db*. */
int dbpf_db_del(dbpf_db *, struct dbpf_data *);

/* dbpf_db_cursor(db, dbc, rdonly): Open the cursor *dbc* on database
 * *db* which is read-only if *rdonly*. */
int dbpf_db_cursor(dbpf_db *, dbpf_cursor **, int);

/* dbpf_db_cursor_close(dbc): Close the cursor *dbc*. */
int dbpf_db_cursor_close(dbpf_cursor *);

/* dbpf_db_cursor_get(dbc, key, val, op, maxkeylen): Retrieve value for
 * *key* from *dbc* into *val* using *op* to determine which key is
 * next. Since *key* may be written by this operation, the argument
 * *maxkeylen* is used to specify the size of the key buffer while
 * key->len specifies the size of the key currently there. */
int dbpf_db_cursor_get(struct dbpf_cursor *, struct dbpf_data *,
    struct dbpf_data *, int, size_t);

/* dbpf_db_cursor_del(dbc): Delete the current (last returned from get)
 * key from *dbc*. */
int dbpf_db_cursor_del(dbpf_cursor *);
