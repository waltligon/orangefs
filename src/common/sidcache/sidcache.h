/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
 *
 * SID cache interface. The function and variable names are the ones
 * from the Berkeley DB version of this header. DB*, DBC*, DB_ENV*,
 * and DBT are sid_db*, sid_cursor*, sid_env*, and struct sid_data.
 * SID_txn keeps its name and stays NULL. Transactions are begun inside
 * the backend. SID_txn is not a second lock.
 *
 * Storage calls go through sidcache-db.h. The LMDB calls are in
 * sidcache-db-lmdb.c. Trove has a Berkeley file and an LMDB file, and
 * --with-db-backend picks one (the default is LMDB). This cache has
 * only the LMDB file. A Berkeley implementation of sidcache-db.h is
 * future work. It would be another file behind the same header, and
 * it would not change the names here. The build that was tested is
 * the LMDB one.
 *
 * One environment holds every named database (sid, attr0..attr7,
 * type, typesid) so a primary record and its indexes commit together.
 * The environment directory is /tmp/pvfs2-sidcache.<pid>. LMDB needs
 * a directory. That directory is not the cache of record. SID_load
 * and SID_save are the text dump. The disabled plan in those two
 * functions is <meta_path>/SIDcache.
 *
 * Still to do:
 * - Type lines do not carry an fsid. SID_type_load forces fsid 0
 *   (V3 NEEDS TO BE DONE), so a loaded type applies to every file
 *   system until the config supplies an fsid.
 * - SID_cache_load reads the ATTRS header. SID_cache_store writes
 *   <ServerDefines>. The two texts do not round-trip. The older
 *   numeric dump stays under #if 0, marked "V3 old version".
 * - SID_create_dbcs clears the cursor arrays. A cursor holds the
 *   environment lock, so the old process-lifetime cursors stay closed
 *   until a cursor can exist without holding that lock. SID_do_join
 *   and SID_type_store open a cursor for the call.
 * - CACHE_SIZE_MB is the old Berkeley cache setting. It does not size
 *   the LMDB map. The map is SID_DB_MAPSIZE in sidcache-db-lmdb.c.
 * - SID_type_sid_extractor stays removed. The old file marked it
 *   "V3 remove this". The type table is ordered by fsid, then server
 *   type. It is not an associate of the primary database.
 */

#ifndef SIDCACHE_H
#define SIDCACHE_H 1

#include <stdio.h>
#include "pvfs2-types.h"
#include "pvfs3-handle.h"
#include "sidcacheval.h"
#include "policyeval.h"
#include "sidcache-db.h"

/* these are defines just to temporarily allow compile */
#define PVFS_LAYOUT_ROUND_ROBIN 0
#define PVFS_LAYOUT_RANDOM 1

/* main SID cache database */
extern sid_db *SID_db;

/* Global variable for the database environment */
extern sid_env *SID_envp;

/* attribute secondary DBs */
extern sid_db *SID_attr_index[SID_NUM_ATTR];

/* Cursor for each secondary DB. These were #if 0 in the old header
 * and live in the .c. They stay NULL. See SID_create_dbcs.
 */
extern sid_cursor *SID_attr_cursor[SID_NUM_ATTR];

extern sid_db *SID_type_db;            /* database for server type */
extern sid_cursor *SID_type_cursor;    /* cursor for server type db */
/* The old header called these SID_type_sid_index and
 * SID_type_sid_cursor. The .c has always used these two names.
 */
extern sid_db *SID_type_index;         /* index on sid for server type db */
extern sid_cursor *SID_index_cursor;   /* cursor for server type sid index */

extern void *SID_txn;

/* <===================== GLOBAL DATABASE DEFINES =====================> */
/* Old in-memory cache size for the Berkeley environment.
 * The LMDB map size is SID_DB_MAPSIZE, not these two defines.
 */
#define CACHE_SIZE_GB (0)
#define CACHE_SIZE_MB (500)

/* Constant used to store tmp strings in a buffer */
#define TMP_BUFF_SIZE (100)

/* Size of one kilobyte */
#define KILOBYTE (1024)

/* Size of one megabyte */
#define MEGABYTE (KILOBYTE * KILOBYTE)

/* Minimum size of bulk retrieve in bytes. Must be page size. */
#define BULK_MIN_SIZE (KILOBYTE * 8)

/* Size of string representation of SID */
#define SID_STR_LEN (37)

/* <==================== INITIALIZATION FUNCTIONS =====================> */
/*
 * This function initializes a SID_cacheval_t struct to default values
 */
extern void SID_cacheval_init(SID_cacheval_t **cacheval_t);

/*
 * This function creates a SID_cacheval_t struct with the attributes that
 * are passed to this function by dynamically creating the SID_cacheval_t.
 * The url attribute cannot be null otherwise the SID_cacheval_t is not
 * dynamically created.
 *
 * Returns 0 on success, otherwise -1 is returned
 */
extern int SID_cacheval_alloc(SID_cacheval_t **cacheval_t,
                              const int sid_attributes[],
                              const BMI_addr sid_bmi,
                              const char *sid_url);

/*
 * This function cleans up a SID_cacheval_t struct by freeing the
 * dynamically created SID_cacheval_t struct
 */
extern void SID_cacheval_free(SID_cacheval_t **cacheval_t);

/*
 * This function packs up the data for the SID_cacheval_t to store in the
 * sid cache. The url bytes follow the struct in the same buffer.
 */
extern void SID_cacheval_pack(const SID_cacheval_t *the_sids_attrs,
                              struct sid_data *data);

/*
 * This function unpacks the data received from the database, mallocs the
 * SID_cacheval_t struct, and sets the values inside of the SID_cacheval_t
 * struct with the data retrieved from the database. The url pointer is
 * fixed up to the bytes that follow the struct.
 */
extern void SID_cacheval_unpack(SID_cacheval_t **the_sids_attrs,
                                struct sid_data *data);

/* <======================= SID CACHE FUNCTIONS =======================> */
/*
 * This function loads the contents of the sid cache from an input file.
 * The file is the ATTRS header (attribute names, then one record per
 * line). It does not read the <ServerDefines> text that SID_cache_store
 * writes. The number of sids in the file is returned through the
 * parameter num_db_records.
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_cache_load(sid_db *dbp, FILE *inpfile, int *num_db_records);

/*
 * This function dumps the contents of the sid cache in ASCII to the file
 * specified through the outpfile parameter. The text is <ServerDefines>.
 * The older ATTRS/SIDS numeric dump is the #if 0 block in the .c.
 * When cursorp is NULL the function opens its own read cursor. The
 * primary cursor and the type cursor share one read transaction.
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_cache_store(sid_cursor *cursorp,
                           FILE *outpfile,
                           int db_records,
                           SID_server_list_t *sid_list);

/*
 * This function stores the sid into the sid cache.
 * If db_records is NULL, assumes we are updating an existing record.
 * If db_records is not NULL, we are adding a new record, and dups will
 * cause an error.
 *
 * The attribute databases used to be filled by DB->associate. LMDB has
 * no associate. This function calls the same extractor callbacks and
 * writes the attribute records itself.
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_cache_put(sid_db *dbp,
                         const PVFS_SID *sid_server,
                         const SID_cacheval_t *cacheval_t,
                         int *db_records);

/*
 * This function searches for a sid in the sid cache. The sid value
 * (sid_server parameter) must be initialized before this function is
 * used. The SID_cacheval_t is malloced and set to the values of the
 * attributes in the database for the sid if it is found in the database.
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_cache_get(sid_db *dbp,
                         const PVFS_SID *sid_server,
                         SID_cacheval_t **cacheval_t);

/*
 * This function searches for a sid in the sid cache, retrieves the struct,
 * malloc's the char * passed in, and copies the bmi address of the
 * retrieved struct into that char *.
 */
extern int SID_cache_lookup_bmi(sid_db *dbp,
                                const PVFS_SID *search_sid,
                                char **bmi_addr);

/*
 * This function updates the sid in the sid cache to all the new values
 * (attributes, bmi address, and url) that are in the SID_cacheval_t
 * parameter to this function if a sid matching the sid_server parameter
 * is found in the sid cache.
 *
 * Returns 0 on success, otherwise returns an error
 */
extern int SID_cache_update_server(sid_db *dbp,
                                   const PVFS_SID *sid_server,
                                   SID_cacheval_t *new_attrs,
                                   struct SID_type_s *sid_types);

/*
 * This function updates the attributes for a sid in the database if a sid
 * with a matching sid_server parameter is found in the database.
 *
 * Returns 0 on success, otherwise returns an error code
 */
/* Now static */
#if 0
extern int SID_cache_update_attrs(sid_db *dbp,
                                  const PVFS_SID *sid_server,
                                  int new_attr[]);
#endif

extern int SID_cache_copy_attrs(SID_cacheval_t *current_sid_attrs,
                                int new_attr[]);

/*
 * This function updates the bmi address for a sid in the database if a sid
 * with a matching sid_server parameter is found in the database.
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_cache_update_bmi(sid_db *dbp,
                                const PVFS_SID *sid_server,
                                BMI_addr new_bmi_addr);

extern int SID_cache_copy_bmi(SID_cacheval_t *current_sid_attrs,
                              BMI_addr new_bmi_addr);

/*
 * This function updates the url address for a sid in the database if a sid
 * with a matching sid_server parameter is found in the database.
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_cache_update_url(sid_db *dbp,
                                const PVFS_SID *sid_server,
                                char *new_url);

extern int SID_cache_copy_url(SID_cacheval_t **current_sid_attrs,
                              char *new_url);

/* V3 now static */
#if 0
extern int SID_cache_update_type(const PVFS_SID *sid_server,
                                 struct SID_type_s *new_type_val);
#endif

/*
 * This function deletes a record from the sid cache if a sid with a
 * matching sid_server parameter is found in the database.
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_cache_delete_server(sid_db *dbp,
                                   const PVFS_SID *sid_server,
                                   int *db_records);

/*
 * This function retrieves entries from the primary database and stores
 * them into a bulk buffer.
 *
 * The minimum amount that can be retrieved is 8 KB of entries.
 *
 * You must specify a size larger than 8 KB by putting an amount into the
 * two size parameters.
 *
 * The output buffer is malloc'ed so take care to make sure it is freed,
 * either by using it in a bulk_insert or by manually freeing.
 *
 * If the cache is larger than the buffer, the entry that does not fit is
 * saved in the bulk_next_key global variable.
 *
 * The old buffer was a DB_MULTIPLE DBT. Each record is now key length,
 * key bytes, value length, value bytes. Nothing in the tree calls this.
 */
extern int SID_bulk_retrieve_from_sid_cache(int size_of_retrieve_kb,
                                            int size_of_retrieve_mb,
                                            sid_db *dbp,
                                            sid_cursor **dbcursorp,
                                            struct sid_data *output);

/*
 * This function inserts entries from the input bulk buffer into a database.
 *
 * The function uses the output from SID_bulk_retrieve as its input.
 * Each record is one sid_db_put. The old path was one DB->put of a
 * DB_MULTIPLE buffer.
 *
 * The malloc'ed bulk buffer is freed at the end of this function.
 * Nothing in the tree calls this.
 */
extern int SID_bulk_insert_into_sid_cache(sid_db *dbp,
                                          struct sid_data *input);

/* <======================== DATABASE FUNCTIONS =======================> */
/*
 * Zeros the key and data values so they are initialized before they
 * are used. Any of the three pointers may be NULL.
 */
extern void SID_zero_dbt(struct sid_data *key,
                         struct sid_data *data,
                         struct sid_data *pkey);

/*
 ***********************************************************************
 * The following is the order in which the functions should be called to
 * open the sidcache:
 * 1. SID_create_environment
 * 2. SID_create_sid_cache
 * 3. SID_create_secondary_dbs
 * 4. SID_create_dbcs
 *
 * The environment is required. The named databases live inside it.
 * The old comment said the environment could be skipped and passed as
 * NULL. That was the DB_PRIVATE in-memory environment.
 ***********************************************************************
 */
/*
 * This function creates and opens the environment handle. The path is
 * /tmp/pvfs2-sidcache.<pid>. See the note at the top of this file.
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_create_environment(sid_env **envp);

/*
 * This function creates and opens the primary database handle. The old
 * access method was DB_HASH. The database is the named database "sid".
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_create_sid_cache(sid_env *envp, sid_db **dbp);

/*
 * This function creates and opens the secondary attribute database
 * handles and sets the database pointers in the secondary_dbs array to
 * point at the correct database. The old access method was DB_BTREE
 * with duplicate keys, filled by DB->associate. Each database is now
 * "attrN" with duplicates. The callback array is still the associate
 * shape. SID_cache_put calls those callbacks. This function opens the
 * databases and does not call the callbacks.
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_create_secondary_dbs(sid_env *envp,
                                    sid_db *dbp,
                                    sid_db *secondary_dbs[],
                                    int (*secdbs_callback_functions[])(
                                        sid_db *pri,
                                        const struct sid_data *pkey,
                                        const struct sid_data *pdata,
                                        struct sid_data *skey));

/*
 * This function used to create and open the database cursors set to the
 * secondary attribute databases. It now clears db_cursors, SID_type_cursor,
 * and SID_index_cursor. See the note at the top of this file.
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_create_dbcs(sid_db *secondary_dbs[],
                           sid_cursor *db_cursors[]);

/************************************************************************
 * The following is the order in which the functions should be called to
 * close the sidcache:
 * 1. SID_close_dbcs
 * 2. SID_close_dbs_env
 ************************************************************************/
/*
 * This function closes the database cursors in the cursors pointer array.
 * The slots are normally NULL. A non-NULL slot is closed.
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_close_dbcs(sid_cursor *db_cursors[]);

/*
 * This function closes the primary database, secondary attribute
 * databases, and environment handles.
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_close_dbs_env(sid_env *envp,
                             sid_db *dbp,
                             sid_db *secondary_dbs[]);

#endif /* SIDCACHE_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
