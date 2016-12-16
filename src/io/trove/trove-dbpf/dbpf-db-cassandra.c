#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "server-config.h"

#include "cassandra.h"

#include <gossip.h>
#include "dbpf.h"
#define debug1(format,...) gossip_debug(GOSSIP_TROVE_DEBUG,"%s: "format,__FUNCTION__)
#define debug(format,...) gossip_debug(GOSSIP_TROVE_DEBUG,"%s: "format,__FUNCTION__,__VA_ARGS__)
#define debug2(...)
#define error1(format,...) gossip_err("%s: "format,__FUNCTION__)
#define error(format,...) gossip_err("%s: "format,__FUNCTION__,__VA_ARGS__)
#define log gossip_err

#define cerror(err) if (err != CASS_OK) { error("Cassandra error=%s(0x%x)\n",cass_error_desc(err),err); }
/**
 * DB enum
 */
typedef enum
{
  STORAGE_ATTRIBUTES,
  COLLECTIONS,
  COLLECTION_ATTRIBUTES,
  DATASPACE_ATTRIBUTES,
  KEYVAL_DB
} DB_TYPE;
/**
 * dbpf_db struct
 */
struct dbpf_db
{
  char* name;
  size_t name_len;

  char hostname[255];
  size_t hostname_len;

  char* collname;
  size_t collname_len;

  DB_TYPE _db_type;
  const CassPrepared* _put;
  const CassPrepared* _put_once;
  const CassPrepared* _get;
  const CassPrepared* _del;

  const CassPrepared* _get_all;
  const CassPrepared* _get_range;

  size_t value_offset;
};
/**
 * Iterator
 */
typedef struct
{
  const CassResult* result;
  CassIterator* rows;
} iterator_t;

static void iterator_init(iterator_t* iterator)
{
  iterator->rows = 0;
  iterator->result = 0;
}

static void iterator_free(iterator_t* iterator)
{
  if (iterator->rows) cass_iterator_free(iterator->rows);
  if (iterator->result) cass_result_free(iterator->result);
  iterator_init(iterator);
}

//static size_t iterator_count(iterator_t* iterator)
//{
//  if (iterator->result)
//  {
//    return cass_result_row_count(iterator->result);
//  }
//  return 0;
//}

static int iterator_is_valid(iterator_t* iterator)
{
  return (iterator->result != 0) && (iterator->rows != 0);
}

static int iterator_next(iterator_t* iterator)
{
  if (iterator_is_valid(iterator))
  {
    return cass_iterator_next(iterator->rows);
  }
  return -1;
}
/**
 * Cursor
 */
struct dbpf_cursor
{
  struct dbpf_db* db;
  int needs_init;
  iterator_t iterator;
};

/**
 * Globals
 */
static int open_dbs;
static CassCluster* g_cluster = 0;
static CassSession* g_session = 0;
static char* g_keyspace = "orangefs_dbs";

static const char* q_create_keyspace = "CREATE KEYSPACE IF NOT EXISTS %s WITH REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : %d }";

static char* storage_attributes = "storage_attributes";
static char* collections = "collections";
static const char* q_create_collections = "CREATE TABLE IF NOT EXISTS %s.%s (host ascii, key ascii, value blob, PRIMARY KEY((host), key))";

// collection_attributes
static char* collection_attributes = "collection_attributes";
static const char* q_create_collection_attributes = "CREATE TABLE IF NOT EXISTS %s.%s (host ascii, collname ascii, key ascii, value blob, PRIMARY KEY((host, collname), key))";

// dataspace_attributes -- keys are sorted in descending order
static char* dataspace_attributes = "dataspace_attributes";
static const char* q_create_dataspace_attributes = "CREATE TABLE IF NOT EXISTS %s.%s (host ascii, collname ascii, key bigint, value blob, PRIMARY KEY((host, collname), key)) WITH CLUSTERING ORDER BY (key DESC)";

// keyval_db
static char* keyval_db = "keyval";
static const char* q_create_keyval_type = "CREATE TYPE IF NOT EXISTS %s.keyval_clustering_key (handle bigint, type ascii, key blob)";
static const char* q_create_keyval_db = "CREATE TABLE IF NOT EXISTS %s.%s (host ascii, collname ascii, key frozen<keyval_clustering_key>, value blob, PRIMARY KEY((host, collname), key))";

/**
 * Logging callback for Cassandra CPP driver; logs only Cassandra interop errors
 */
void cass_log_callback(const CassLogMessage* message, void* data)
{
  log("[CASS]%s:%d:%s:%s\n", message->file, message->line, message->function, message->message);
}

/**
 * To be expanded
 */
static int db_error(CassError e)
{
  // stub
  if (e == CASS_OK)
  {
    return 0;
  }
  return DBPF_ERROR_UNKNOWN;
}

/**
 * Safe snprintf
 */
static char* snprintf_ext(char* dst, size_t* size, const char* fmt, ...)
{
  va_list ap;
  size_t rs = *size;
  va_start(ap, fmt);
  rs = vsnprintf(dst, rs, fmt, ap);
  while (rs >= *size)
  {
    *size = rs + 1;
    rs = *size;
    free(dst);
    dst = (char*)malloc(rs);
    va_start(ap, fmt);
    rs = vsnprintf(dst, rs, fmt, ap);
  }
  debug2("%s\n", dst);
  return dst;
}

/**
 * Basic query, runs and frees statement
 */
static int get_future(CassStatement* statement, CassFuture** result_future)
{
  CassError err;

  *result_future = cass_session_execute(g_session, statement);

  err = cass_future_error_code(*result_future);

  if (err != CASS_OK)
  {
    cerror(err);
    cass_future_free(*result_future);
    *result_future = 0;
  }

  cass_statement_free(statement);

  return db_error(err);
}

/**
 * Executes a statement
 */
static int execute(CassStatement* statement)
{
  CassFuture* result_future = 0;
  int r;

  r = get_future(statement, &result_future);

  if (r == 0)
  {
    cass_future_free(result_future);
  }

  return r;
}

/**
 * Executes a statement and returns result iterator
 */
static int execute_get_iterator(CassStatement* statement, iterator_t* iterator)
{
  CassFuture* result_future = 0;
  int r;

  r = get_future(statement, &result_future);

  if (r == 0)
  {
    iterator->result = cass_future_get_result(result_future);
    cass_future_free(result_future);

    if (cass_result_row_count(iterator->result))
    {
      iterator->rows = cass_iterator_from_result(iterator->result);
      if (iterator->rows)
      {
        if (cass_iterator_next(iterator->rows) == 0)
        {
          cass_iterator_free(iterator->rows);
          cass_result_free(iterator->result);
          iterator->result = 0;
          iterator->rows = 0;
          r = TROVE_ENOENT;
        }
      }
    }
    else
    {
      cass_result_free(iterator->result);
      iterator->rows = 0;
      iterator->result = 0;
      r = TROVE_ENOENT;
    }
  }

  return r;
}

/**
 * Binds value in prepared statement
 */
static int bind_value(CassStatement* statement, struct dbpf_db* db, struct dbpf_data* val)
{
  CassError err;
  err = cass_statement_bind_bytes(statement, db->value_offset, val->data, val->len);
  cerror(err);
  if (err != CASS_OK)
  {
    cass_statement_free(statement);
  }
  return db_error(err);
}

/**
 * Binds key in prepared statement
 */
static int bind_key(CassStatement* statement, struct dbpf_db* db, struct dbpf_data* key, int cluster_part_only)
{
  CassError err;

  switch (db->_db_type)
  {
    case STORAGE_ATTRIBUTES:
    case COLLECTIONS:
    {
      err = cass_statement_bind_string_n(statement, 0, db->hostname, db->hostname_len);
      if (err != CASS_OK) goto emergency;
      err = cass_statement_bind_string_n(statement, 1, key->data, key->len);
      if (err != CASS_OK) goto emergency;
    }
      break;

    case COLLECTION_ATTRIBUTES:
    {
      err = cass_statement_bind_string_n(statement, 0, db->hostname, db->hostname_len);
      if (err != CASS_OK) goto emergency;
      err = cass_statement_bind_string_n(statement, 1, db->collname, db->collname_len);
      if (err != CASS_OK) goto emergency;
      err = cass_statement_bind_string_n(statement, 2, key->data, key->len);
      if (err != CASS_OK) goto emergency;
    }
      break;

    case DATASPACE_ATTRIBUTES:
    {
      if (key->len != sizeof (int64_t))
      {
        error("wrong key size for DATASPACE_ATTRIBUTES, %zu vs %zu\n", key->len, sizeof (int64_t));
        cass_statement_free(statement);
        return TROVE_ERANGE;
      }

      err = cass_statement_bind_string_n(statement, 0, db->hostname, db->hostname_len);
      if (err != CASS_OK) goto emergency;
      err = cass_statement_bind_string_n(statement, 1, db->collname, db->collname_len);
      if (err != CASS_OK) goto emergency;
      err = cass_statement_bind_int64(statement, 2, *(int64_t*)key->data);
      if (err != CASS_OK) goto emergency;
    }
      break;

    case KEYVAL_DB:
    {
      struct dbpf_keyval_db_entry* k = (struct dbpf_keyval_db_entry*)key->data;
      err = cass_statement_bind_string_n(statement, 0, db->hostname, db->hostname_len);
      if (err != CASS_OK) goto emergency;
      err = cass_statement_bind_string_n(statement, 1, db->collname, db->collname_len);
      if (err != CASS_OK) goto emergency;
      err = cass_statement_bind_int64(statement, 2, k->handle);
      if (err != CASS_OK) goto emergency;
      err = cass_statement_bind_string_n(statement, 3, &k->type, 1);
      if (err != CASS_OK) goto emergency;
      err = cass_statement_bind_bytes(statement, 4, (const cass_byte_t*)k->key, DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(key->len));
      if (err != CASS_OK) goto emergency;
    }
      break;
  }

  return 0;

emergency:
  cass_statement_free(statement);
  cerror(err);
  return db_error(err);
}

/**
 * Gets key+value from result row
 */
static int get_key_value(struct dbpf_db* db, const CassRow* row, struct dbpf_data* key, struct dbpf_data* val, size_t maxkeylen)
{
  const cass_byte_t* out;
  size_t out_size;
  CassError err;

  if (val)
  {
    err = cass_value_get_bytes(cass_row_get_column(row, 0), &out, &out_size);
    if (err != CASS_OK) goto emergency;

//    if (out_size <= val->len)
//    {
//      memcpy(val->data, out, out_size);
//      val->len = out_size;
//    }
//    else
//    {
//      error("\treturn value doesn't fit the buffer: %zu > %zu\n", out_size, val->len);
//      val->len = out_size;
//      return TROVE_ERANGE;
//    }

    /// changed according to bdb/lmdb backends contract
    memcpy(val->data, out, (out_size > val->len) ? val->len : out_size);
    val->len = out_size;
  }

  if (key)
  {
    switch (db->_db_type)
    {
      case STORAGE_ATTRIBUTES:
      case COLLECTIONS:
      case COLLECTION_ATTRIBUTES:
      {
        const char* k;
        err = cass_value_get_string(cass_row_get_column(row, 1), &k, &out_size);
        if (err != CASS_OK) goto emergency;
        if (out_size > maxkeylen) return TROVE_ERANGE;
        memcpy(key->data, k, out_size);
        key->len = out_size;
      }
        break;

      case DATASPACE_ATTRIBUTES:
      {
        cass_int64_t h;
        err = cass_value_get_int64(cass_row_get_column(row, 1), &h);
        if (err != CASS_OK) goto emergency;
        if (sizeof (h) > maxkeylen) return TROVE_ERANGE;
        memcpy(key->data, &h, sizeof (h));
        key->len = sizeof (h);
      }
        break;

      case KEYVAL_DB:
      {
        cass_int64_t h;
        const char* t;
        const char* k;
        struct dbpf_keyval_db_entry* p = (struct dbpf_keyval_db_entry*)(key->data);

        err = cass_value_get_bytes(cass_row_get_column(row, 3), (const cass_byte_t**)&k, &out_size);
        if (err != CASS_OK) goto emergency;
        if (out_size > DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(maxkeylen)) return TROVE_ERANGE;

        memcpy(p->key, k, out_size);
        key->len = out_size;

        err = cass_value_get_int64(cass_row_get_column(row, 1), &h);
        if (err != CASS_OK) goto emergency;
        memcpy(&p->handle, &h, sizeof (h));
        key->len += sizeof (h);

        err = cass_value_get_string(cass_row_get_column(row, 2), &t, &out_size);
        if (err != CASS_OK) goto emergency;
        if (out_size != 1) return TROVE_ERANGE; // we expect single char here
        p->type = t[0];
        key->len += sizeof (t[0]);
      }
        break;
    }

  }

  return 0;

emergency:

  cerror(err);
  return db_error(err);

}

/**
 * Translates db name to db type
 */
static DB_TYPE get_db_type(const char* path)
{

  if (strstr(path, storage_attributes))
  {
    debug2("%s->STORAGE_ATTRIBUTES\n", path);
    return STORAGE_ATTRIBUTES;
  }
  else if (strstr(path, collections))
  {
    debug2("%s->COLLECTIONS\n", path);
    return COLLECTIONS;
  }
  else if (strstr(path, collection_attributes))
  {
    debug2("%s->COLLECTION_ATTRIBUTES\n", path);
    return COLLECTION_ATTRIBUTES;
  }
  else if (strstr(path, dataspace_attributes))
  {
    debug2("%s->DATASPACE_ATTRIBUTES\n", path);
    return DATASPACE_ATTRIBUTES;
  }
  else if (strstr(path, keyval_db))
  {
    debug2("%s->KEYVAL_DB\n", path);
    return KEYVAL_DB;
  }

  error("%s->nothing\n", path);

  return 0;
}

/**
 * Creates a prepared statement
 */
static CassError prepare_statement(CassSession* session, const char* query, const CassPrepared** prepared)
{
  CassError err;
  CassFuture* future = 0;

  future = cass_session_prepare(session, query);

  err = cass_future_error_code(future);
  if (err == CASS_OK)
  {
    *prepared = cass_future_get_prepared(future);
  }

  cass_future_free(future);

  return err;
}

/**
 * Creates prepared statements for keyval_db
 */
static int keyvaldb_prepareds(char* name, struct dbpf_db* db)
{
  CassError err;
  size_t size = 512;
  char* query = (char*)malloc(size);

  query = snprintf_ext(query, &size, "SELECT value FROM %s.%s WHERE host=? AND collname=? AND key={handle:?,type:?,key:?}", g_keyspace, name);
  err = prepare_statement(g_session, query, &db->_get);
  if (err != CASS_OK) goto emergency;

  query = snprintf_ext(query, &size, "INSERT INTO %s.%s (host, collname, key, value) VALUES (?,?,{handle:?,type:?,key:?},?)", g_keyspace, name);
  err = prepare_statement(g_session, query, &db->_put);
  if (err != CASS_OK) goto emergency;

  query = snprintf_ext(query, &size, "INSERT INTO %s.%s (host, collname, key, value) VALUES (?,?,{handle:?,type:?,key:?},?) IF NOT EXISTS", g_keyspace, name);
  err = prepare_statement(g_session, query, &db->_put_once);
  if (err != CASS_OK) goto emergency;

  db->value_offset = 5;

  query = snprintf_ext(query, &size, "DELETE FROM %s.%s WHERE host=? AND collname=? AND key={handle:?,type:?,key:?}", g_keyspace, name);
  err = prepare_statement(g_session, query, &db->_del);
  if (err != CASS_OK) goto emergency;

  query = snprintf_ext(query, &size, "SELECT value, key.handle, key.type, key.key FROM %s.%s WHERE host=? AND collname=?", g_keyspace, name);
  err = prepare_statement(g_session, query, &db->_get_all);
  if (err != CASS_OK) goto emergency;

  query = snprintf_ext(query, &size, "SELECT value, key.handle, key.type, key.key FROM %s.%s WHERE host=? AND collname=? AND key>={handle:?,type:?,key:?}", g_keyspace, name);
  err = prepare_statement(g_session, query, &db->_get_range);
  if (err != CASS_OK) goto emergency;

  free(query);

  return 0;

emergency:
  free(query);
  cerror(err);
  return -1;
}

/**
 *  Creates prepared statements for collection db
 */
static int collection_prepareds(const char* name, struct dbpf_db* db)
{
  CassError err;
  size_t size = 512;
  char* query = (char*)malloc(size);

  query = snprintf_ext(query, &size, "SELECT value FROM %s.%s WHERE host=? AND collname=? AND key=?", g_keyspace, name);
  err = prepare_statement(g_session, query, &db->_get);
  if (err != CASS_OK) goto emergency;

  query = snprintf_ext(query, &size, "INSERT INTO %s.%s (host, collname, key, value) VALUES (?,?,?,?)", g_keyspace, name);
  err = prepare_statement(g_session, query, &db->_put);
  if (err != CASS_OK) goto emergency;

  query = snprintf_ext(query, &size, "INSERT INTO %s.%s (host, collname, key, value) VALUES (?,?,?,?) IF NOT EXISTS", g_keyspace, name);
  err = prepare_statement(g_session, query, &db->_put_once);
  if (err != CASS_OK) goto emergency;

  db->value_offset = 3;

  query = snprintf_ext(query, &size, "DELETE FROM %s.%s WHERE host=? AND collname=? AND key=?", g_keyspace, name);
  err = prepare_statement(g_session, query, &db->_del);
  if (err != CASS_OK) goto emergency;

  query = snprintf_ext(query, &size, "SELECT value, key FROM %s.%s WHERE host=? AND collname=?", g_keyspace, name);
  err = prepare_statement(g_session, query, &db->_get_all);
  if (err != CASS_OK) goto emergency;

  query = snprintf_ext(query, &size, "SELECT value, key FROM %s.%s WHERE host=? AND collname=? AND key>=?", g_keyspace, name);
  err = prepare_statement(g_session, query, &db->_get_range);
  if (err != CASS_OK) goto emergency;

  free(query);
  return 0;

emergency:

  free(query);
  cerror(err);
  return -1;
}

/**
 * Creates prepared statements for storage attributes db
 */
static int storage_prepareds(const char* name, struct dbpf_db* db)
{
  CassError err;
  size_t size = 512;
  char* query = (char*)malloc(size);

  query = snprintf_ext(query, &size, "SELECT value FROM %s.%s WHERE host=? AND key=?", g_keyspace, name);
  err = prepare_statement(g_session, query, &db->_get);
  if (err != CASS_OK) goto emergency;

  query = snprintf_ext(query, &size, "INSERT INTO %s.%s (host, key, value) VALUES (?,?,?)", g_keyspace, name);
  err = prepare_statement(g_session, query, &db->_put);
  if (err != CASS_OK) goto emergency;

  query = snprintf_ext(query, &size, "INSERT INTO %s.%s (host, key, value) VALUES (?,?,?) IF NOT EXISTS", g_keyspace, name);
  err = prepare_statement(g_session, query, &db->_put_once);
  if (err != CASS_OK) goto emergency;

  db->value_offset = 2;

  query = snprintf_ext(query, &size, "DELETE FROM %s.%s WHERE host=? AND key=?", g_keyspace, name);
  err = prepare_statement(g_session, query, &db->_del);
  if (err != CASS_OK) goto emergency;

  query = snprintf_ext(query, &size, "SELECT value, key FROM %s.%s WHERE host=?", g_keyspace, name);
  err = prepare_statement(g_session, query, &db->_get_all);
  if (err != CASS_OK) goto emergency;

  query = snprintf_ext(query, &size, "SELECT value, key FROM %s.%s WHERE host=? AND key>=?", g_keyspace, name);
  err = prepare_statement(g_session, query, &db->_get_range);
  if (err != CASS_OK) goto emergency;

  free(query);
  return 0;

emergency:
  free(query);
  cerror(err);
  return -1;

}

/**
 * Creates keyspace with specified replication factor
 */
static int create_keyspace_if_needed(int replication_factor)
{
  size_t size = 512;
  char* query = (char*)malloc(size);

  query = snprintf_ext(query, &size, q_create_keyspace, g_keyspace, replication_factor);

  debug2("%s\n", query);
  CassStatement* statement = cass_statement_new(query, 0);

  free(query);

  return execute(statement);
}

/**
 * Initializes connection with cluster, sets up the keyspace
 */
static int initialize(char* points)
{
  CassFuture* connect_future = NULL;
  CassError err;
  int r;

  debug("points=%s\n", points);

  cass_log_set_callback(cass_log_callback, 0);

  if (g_cluster || g_session)
  {
    error1("double connect !!!\n");
    abort();
  }

  /* Setup and connect to cluster */
  g_cluster = cass_cluster_new();
  if (g_cluster == 0)
  {
    return DBPF_ERROR_UNKNOWN;
  }

  g_session = cass_session_new();
  if (g_session == 0)
  {
    cass_cluster_free(g_cluster);
    g_cluster = 0;
    return DBPF_ERROR_UNKNOWN;
  }

  /* Add contact points */
  err = cass_cluster_set_contact_points(g_cluster, points);
  if (err != CASS_OK) {
    cerror(err);
    cass_session_free(g_session);
    g_session = 0;
    cass_cluster_free(g_cluster);
    g_cluster = 0;
    return DBPF_ERROR_UNKNOWN;
  }

  /* Provide the cluster object as configuration to connect the session */
  connect_future = cass_session_connect(g_session, g_cluster);

  err = cass_future_error_code(connect_future);
  if (err != CASS_OK) {
    cerror(err);
    cass_session_free(g_session);
    g_session = 0;
    cass_cluster_free(g_cluster);
    g_cluster = 0;
    return DBPF_ERROR_UNKNOWN;
  }

  cass_future_free(connect_future);

  debug("successfully connected to cassandra cluster at %s\n", points);

  r = create_keyspace_if_needed(1);
  if (r) {
    cass_session_free(g_session);
    g_session = 0;
    cass_cluster_free(g_cluster);
    g_cluster = 0;
  }

  return r;
}

/**
 * Disconnects from cluster
 */
static void finalize(void)
{
  debug1("disconnecting from cluster\n");

  if (g_cluster && g_session)
  {
    if (g_session)
    {
      CassFuture* close_future = NULL;

      /* Close the session */
      close_future = cass_session_close(g_session);
      cass_future_wait(close_future);
      cass_future_free(close_future);
      cass_session_free(g_session);

      g_session = 0;
    }

    if (g_cluster)
    {
      cass_cluster_free(g_cluster);
      g_cluster = 0;
    }
  }

//  if (g_keyspace)
//  {
//    free(g_keyspace);
//    g_keyspace = 0;
//  }
}

/**
 * Opens a db
 */
int dbpf_db_open(char* name, int compare, struct dbpf_db** db, int create, struct server_configuration_s* sc, char* collname)
{
  struct dbpf_db* tdb;
  int r = 0;

  if (!open_dbs) {
    r = initialize(sc->db_points);
    if (r)
      return r;
  }
  open_dbs++;

  debug("name=%s, collname=%s, compare=%d, create=%d, start...\n", name, collname, compare, create);

  tdb = malloc(sizeof **db);
  if (tdb == 0)
  {
    open_dbs--;
    if (!open_dbs) {
      finalize();
    }
    return TROVE_ENOMEM;
  }

  *db = tdb;
  tdb->_db_type = get_db_type(name);
  tdb->name = strdup(name);
  tdb->name_len = strlen(tdb->name);
  tdb->collname = 0;
  gethostname(tdb->hostname, 255);
  tdb->hostname_len = strlen(tdb->hostname);

  switch (tdb->_db_type)
  {
    case STORAGE_ATTRIBUTES:
    case COLLECTIONS:
      if (collname)
      {
        error1("collname provided where it is not intended to\n");
        free(tdb->name);
        tdb->name = 0;
        free(tdb);
        *db = 0;
        r = TROVE_EINVAL;
        goto fail;
      }
      break;

    case COLLECTION_ATTRIBUTES:
    case DATASPACE_ATTRIBUTES:
    case KEYVAL_DB:
      if (collname)
      {
        tdb->collname = strdup(collname);
        tdb->collname_len = strlen(tdb->collname);
      }
      else
      {
        error1("collname is not provided for collections database\n");
        free(tdb->name);
        tdb->name = 0;
        free(tdb);
        *db = 0;
        r = TROVE_EINVAL;
        goto fail;
      }
      break;
  }

  if (create)
  {
    size_t size = 512;
    char* query = (char*)malloc(size);

    CassStatement* statement;
    switch (tdb->_db_type)
    {
      case STORAGE_ATTRIBUTES:
        query = snprintf_ext(query, &size, q_create_collections, g_keyspace, storage_attributes);
        break;

      case COLLECTIONS:
        query = snprintf_ext(query, &size, q_create_collections, g_keyspace, collections);
        break;

      case COLLECTION_ATTRIBUTES:
        query = snprintf_ext(query, &size, q_create_collection_attributes, g_keyspace, collection_attributes);
        break;

      case DATASPACE_ATTRIBUTES:
        query = snprintf_ext(query, &size, q_create_dataspace_attributes, g_keyspace, dataspace_attributes);
        break;

      case KEYVAL_DB:
        query = snprintf_ext(query, &size, q_create_keyval_type, g_keyspace);
        statement = cass_statement_new(query, 0);
        r = execute(statement);

        query = snprintf_ext(query, &size, q_create_keyval_db, g_keyspace, keyval_db);
        break;

      default:
        break;
    }

    if (r == 0)
    {
      statement = cass_statement_new(query, 0);
      r = execute(statement);
    }

    free(query);
  }

  if (r == 0)
  {
    switch (tdb->_db_type)
    {
      case STORAGE_ATTRIBUTES:
        r = storage_prepareds(storage_attributes, tdb);
        break;

      case COLLECTIONS:
        r = storage_prepareds(collections, tdb);
        break;

      case COLLECTION_ATTRIBUTES:
        r = collection_prepareds(collection_attributes, tdb);
        break;

      case DATASPACE_ATTRIBUTES:
        r = collection_prepareds(dataspace_attributes, tdb);
        break;

      case KEYVAL_DB:
        r = keyvaldb_prepareds(keyval_db, tdb);
        break;

      default:
        break;
    }
  }

  debug("db=%p\n", *db);

fail:
  if (r) {
    open_dbs--;
    if (!open_dbs) {
      finalize();
    }
  }
  return r;
}

/**
 * Closes a db
 */
int dbpf_db_close(struct dbpf_db * db)
{

  debug("db=%p\n", db);

  if (db)
  {
    cass_prepared_free(db->_put);
    cass_prepared_free(db->_put_once);
    cass_prepared_free(db->_get);
    cass_prepared_free(db->_del);
    cass_prepared_free(db->_get_all);
    cass_prepared_free(db->_get_range);

    if (db->collname)
    {
      free(db->collname);
    }

    free(db->name);
    free(db);
  }
  open_dbs--;
  if (!open_dbs) {
    finalize();
  }
  return 0;
}

/**
 * No-op for sync operation
 */
int dbpf_db_sync(struct dbpf_db *db)
{
  debug2("db=%p\n", db);
  return 0;
}

/**
 * Gets a value by key
 */
int dbpf_db_get(struct dbpf_db* db, struct dbpf_data* key, struct dbpf_data* val)
{
  int r;
  const CassRow* row;
  iterator_t iterator;

  CassStatement* statement = cass_prepared_bind(db->_get);

  r = bind_key(statement, db, key, 0);

  if (r == 0)
  {
    r = execute_get_iterator(statement, &iterator);
  }

  if (r == 0)
  {
    row = cass_iterator_get_row(iterator.rows);
    r = get_key_value(db, row, 0, val, 0); // do not fetch key
    iterator_free(&iterator);
  }


  return r;
}

/**
 * Puts a value
 */
static int dbpf_db_put_internal(struct dbpf_db *db, struct dbpf_data *key, struct dbpf_data *val, int once)
{
  int r;
  const CassPrepared* prepared = (once) ? db->_put_once : db->_put;

  CassStatement* statement = cass_prepared_bind(prepared);

  r = bind_key(statement, db, key, 0);

  if (r == 0)
  {
    r = bind_value(statement, db, val);
  }

  if (r == 0)
  {
    execute(statement);
  }

  return r;
}

/**
 * Puts a value
 */
int dbpf_db_put(struct dbpf_db *db, struct dbpf_data *key, struct dbpf_data *val)
{
  int r = dbpf_db_put_internal(db, key, val, 0);

  return r;
}

/**
 * Puts a value once
 */
int dbpf_db_putonce(struct dbpf_db *db, struct dbpf_data *key, struct dbpf_data *val)
{
  int r = dbpf_db_put_internal(db, key, val, 1);

  return r;
}

/**
 * Deletes a value by key
 */
int dbpf_db_del(struct dbpf_db * db, struct dbpf_data *key)
{
  int r;

  CassStatement* statement = cass_prepared_bind(db->_del);

  r = bind_key(statement, db, key, 0);

  if (r == 0)
  {
    r = execute(statement);
  }


  return r;
}

/**
 * Inits a cursor
 */
int dbpf_db_cursor(struct dbpf_db* db, struct dbpf_cursor** dbc, int ro)
{


  *dbc = malloc(sizeof (struct dbpf_cursor));
  if (*dbc)
  {
    (*dbc)->db = db;
    (*dbc)->iterator.rows = 0;
    (*dbc)->iterator.result = 0;
    (*dbc)->needs_init = 1;
  }

  // uninitialized cursor shall point to first element of the query
  // doing it here may lead to extra traffic

  debug("db=%p, ro=%d; result dbc=%p\n", db, ro, dbc);

  return 0;
}

/**
 * Closes a cursor
 */
int dbpf_db_cursor_close(struct dbpf_cursor* dbc)
{

  debug("dbc=%p\n", dbc);

  iterator_free(&dbc->iterator);
  free(dbc);

  return 0;
}

/**
 * Inits cursor if needed
 */
int cursor_init_if_needed(struct dbpf_cursor* dbc)
{
  int r = 0;
  CassError err;
  CassStatement* statement;

  if (dbc->needs_init == 1)
  {

    iterator_free(&dbc->iterator);
    dbc->needs_init = 0;

    // get new iterator
    statement = cass_prepared_bind(dbc->db->_get_all);
    err = cass_statement_bind_string_n(statement, 0, dbc->db->hostname, dbc->db->hostname_len);
    if (err != CASS_OK) goto emergency;
    if (dbc->db->collname)
    {
      err = cass_statement_bind_string_n(statement, 1, dbc->db->collname, dbc->db->collname_len);
      if (err != CASS_OK) goto emergency;
    }

    r = execute_get_iterator(statement, &dbc->iterator);
  }
  return r;

emergency:
  cerror(err);
  cass_statement_free(statement);
  return db_error(r);
}

/**
 * Cursor ops
 */
int dbpf_db_cursor_get(struct dbpf_cursor* dbc, struct dbpf_data* key, struct dbpf_data* val, int op, size_t maxkeylen)
{
  int r = 0;
  const CassRow* row = 0;

  switch (op)
  {
    case DBPF_DB_CURSOR_FIRST:
    {
      // request init and fall-thru
      dbc->needs_init = 1;
    }

      // deliberate fall-thru

    case DBPF_DB_CURSOR_CURRENT:
    {
      // simply get current value
      r = cursor_init_if_needed(dbc);
    }
      break;

    case DBPF_DB_CURSOR_NEXT:
    {
      if (dbc->needs_init)
      {
        // if called uninitialized, return first value
        r = cursor_init_if_needed(dbc);
      }
      else if (iterator_is_valid(&dbc->iterator))
      {
        if (iterator_next(&dbc->iterator) == 0)
        {
          iterator_free(&dbc->iterator);
          r = TROVE_ENOENT;
        }
      }
    }
      break;

    case DBPF_DB_CURSOR_SET:
    {
      CassStatement* statement;
      iterator_free(&dbc->iterator);

      // if DBPF_DB_CURSOR_SET returns nothing -- return error
      // if DBPF_DB_CURSOR_SET returns a value, run DBPF_DB_CURSOR_SET_RANGE -- and return current
      // this way it'll be compatible with lmdb cursor

      statement = cass_prepared_bind(dbc->db->_get);
      r = bind_key(statement, dbc->db, key, 0);

      if (r == 0)
      {
        r = execute_get_iterator(statement, &dbc->iterator);

        if (r)
        {
          // error, no fall-thru
          break;
        }
      }
    }

      // deliberate fall-through

    case DBPF_DB_CURSOR_SET_RANGE:
    {
      CassStatement* statement;
      iterator_free(&dbc->iterator);

      statement = cass_prepared_bind(dbc->db->_get_range);
      r = bind_key(statement, dbc->db, key, 1);

      if (r == 0)
      {
        r = execute_get_iterator(statement, &dbc->iterator);
      }
    }
      break;
  }

  if (r == 0)
  {
    if (iterator_is_valid(&dbc->iterator))
    {
      row = cass_iterator_get_row(dbc->iterator.rows);
      if (row)
      {
        // get a result from a row
        r = get_key_value(dbc->db, row, key, val, maxkeylen);
      }
      else
      {
        r = TROVE_ENOENT;
      }
    }
    else
    {
      r = TROVE_ENOENT;
    }
  }

  if (r)
  {
    debug2("dbc=%p, key=%p, val=%p, op=%d, maxkeylen=%zu; result=%d\n", dbc, key, val, op, maxkeylen, r);
  }

  dbc->needs_init = 0;

  return r;
}

/**
 * Deletes a value under cursor
 */
int dbpf_db_cursor_del(struct dbpf_cursor * dbc)
{
  int r;
  struct dbpf_data key;

  key.data = malloc(DBPF_MAX_KEY_LENGTH);
  key.len = DBPF_MAX_KEY_LENGTH;

  r = dbpf_db_cursor_get(dbc, &key, 0, DBPF_DB_CURSOR_CURRENT, DBPF_MAX_KEY_LENGTH);

  if (r == 0)
  {
    CassStatement* statement = cass_prepared_bind(dbc->db->_del);
    r = bind_key(statement, dbc->db, &key, 0);
    if (r == 0)
    {
      r = execute(statement);
    }
    if (r == 0)
    {
      // make this iterator to point to next available element to make it valid for next calls
      // ignore return code, as there maybe no records left
      dbpf_db_cursor_get(dbc, &key, 0, DBPF_DB_CURSOR_SET_RANGE, DBPF_MAX_KEY_LENGTH);
    }
  }

  free(key.data);

  debug2("dbc=%p; result=%d\n", dbc, r);

  return r;
}
