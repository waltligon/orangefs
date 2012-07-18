#include <db.h>
#include <uuid/uuid.h>
#include <policy.h>
#include "sidcacheval.h"

/* these are defines just to temporarily allow compile */
typedef int BMI_addr;
#define PVFS_LAYOUT_ROUND_ROBIN 0
#define PVFS_LAYOUT_RANDOM 1
/* remove when integrating into main orange code */

/* main SID cache database */
extern DB     *SID_db;

/* Global variable for the database environement */
extern DB_ENV *SID_envp;

/* main SID transaction */
extern DB_TXN *SID_txn;

/* attribute secondary DBs */
extern DB     *SID_attr_index[SID_NUM_ATTR]; 

/* cursor for each secondary DB */
extern DBC    *SID_attr_cursor[SID_NUM_ATTR];

/* type of a SID */
typedef uuid_t SID;


/* <===================== GLOBAL DATABASE DEFINES =====================> */
/* Used to set the in cache memory size for DB environment*/
#define CACHE_SIZE_GB (0)
#define CACHE_SIZE_MB (500)

/* Constant used to store tmp strings in a buffer */
#define TMP_BUFF_SIZE (100)

/* Size of one kilobyte */
#define KILOBYTE (1024)

/* Size of one megabyte */
#define MEGABYTE (KILOBYTE * KILOBYTE)

/* Minimum size of bulk retrieve in bytes */
#define BULK_MIN_SIZE (KILOBYTE * 8)


/* <==================== INITIALIZATION FUNCTIONS =====================> */
/*
 * This function intializes the secondary attribute database array to NULL
 *
 * On success 0 is returned, otherwise the index that is not equal to NULL
 * is returned unless the index is 0, then -1 is returned
*/
int SID_intialize_secondary_dbs(DB *secondary_dbs[]);

/*
 * This function intializes SID_attr_cursor array to NULL
 * 
 * On success 0 is returned, otherwise the index that is not equal to NULL
 * is returned unless the index is 0, then -1 is returned
*/
int SID_initialize_database_cursors(DBC *db_cursors[]);

/*
 * This function initializes a SID_cachevale_t struct to default values
*/
void SID_initialize_SID_cacheval_t(SID_cacheval_t **cacheval_t);


/* <======================= SID CACHE FUNCTIONS =======================> */
/*
 * This function parses the first two lines in the input file and gets
 * the number of attributes per sid, number of sids, and the string representations
 * of the int attributes for the sids. It gets the attributes that each sid has
 * parsing the strings representations of the attributes.
 * This function also sets the attr_positions array to make sure sets up that
 * the attributes in the input file go into their correct positions in the
 * SID_cacheval_t attrs array
 *
 * Returns 0 on success, otherwise returns an error code
*/
int SID_parse_input_file_header(FILE *inpfile, int *records_in_file);

/*
 * This function loads the contents of the sid cache from an input file. The
 * number of sids in the file is returned though the parameter db_records
 * 
 * Returns 0 on success, otherwise returns an error code
*/
int SID_load_sid_cache_from_file(DB **dbp, FILE *inpfile, const char *file_name, int *db_records);

/*
 * This function stores the sid into the sid cache
 *
 * Returns 0 on success, otherwise returns an error code
*/
int SID_store_sid_into_sid_cache(DB **dbp, SID sid_server, SID_cacheval_t *cacheval_t, int *db_records);

/*
 * This function searches for a sid in the sid cache. The sid (uuid_t) value
 * of the SID_sid_t struct must be initialized before this function is
 * used. The SID_sid_t will be set to the values in the database for the
 * sid if it is found in the database
 *
 * Returns 0 on success, otherwise returns an error code
*/
int SID_retrieve_sid_from_sid_cache(DB **dbp, SID sid_server, SID_cacheval_t **cacheval_t);

/*
 * This function searches for a sid in the sid cache, retrieves the struct,
 * malloc's the char * passed in, and copies the bmi address of the retrieved
 * struct into that char *.
 */
int SID_bmi_lookup_from_sid_cache(DB **dbp, SID search_sid, char **bmi_addr);

/*
 * This function packs up the data fOR THE SID_cacheval_t to store in the
 * sid cache
*/
void SID_pack_SID_cacheval_data(SID_cacheval_t *the_sids_attrs, DBT *data);

/*
 * This function unpacks the data recieved from the database and sets
 * the values inside of the SID_cacheval_t struct
*/
void SID_unpack_SID_cacheval_data(SID_cacheval_t **the_sids_attrs, DBT *data);

/*
 * This function updates the sid in the sid cache to all the new values 
 * (attributes, bmi address, and url) that are in the SID_cacheval_t parameter
 * to this function if a sid with a matching uuid_t as the sid_server parameter
 * is found in the sid cache
 *
 * Returns 0 on success, otherwise returns an error 
*/
int SID_update_sid_in_sid_cache(DB **dbp, SID sid_server, SID_cacheval_t *new_attrs);

/*
 * This function updates the attributes in a SID_cacheval_t struct to the
 * attribute values in the parameter new_attrs
*/
int SID_update_attributes_in_sid(DB **dbp, SID *sid_server, SID_cacheval_t *current_sid_attrs, int new_attr[]);

/*
 * This function updates the bmi address in a SID_cacheval_t struct to the
 * bmi address in the parameter new_bmi_addr
*/
int SID_update_bmi_address_in_sid(DB **dbp, SID *sid_server, SID_cacheval_t *current_sid_attrs, BMI_addr new_bmi_addr);

/*
 * This function updates the url address for the SID_cache_t struct to the
 * url in the parameter new_url
 *
 * If the url is updated then all other attributes are updated as well and this
 * function will return 0, otherwise the function returns 1
*/
int SID_update_url_in_sid(DB **dbp, SID *sid_server, SID_cacheval_t **current_sid_attrs, char *new_url);

/*
 * This function deletes a record from the sid cache if a sid with a matching
 * uuid_t as the one in the SID_sid_t struct is found in the sid cache
 *
 * Returns 0 on success, otherwise returns an error code 
*/
int SID_delete_sid_from_sid_cache(DB **dbp, SID sid_server, int *db_records);

/*
 * This function creates a SID_cacheval_t struct with the attributes that are passed to
 * this function by dynamically creating the SID_cacheval_t. If url attribute cannot
 * be null otherwise the SID_cacheval_t is not dynamically created
 *
 * Returns 0 on success, otherwise -1 is returned
*/
int SID_create_SID_cacheval_t(SID_cacheval_t **cacheval_t, int sid_attributes[], BMI_addr sid_bmi, char *sid_url);

/*
 * This function clean up a SID_cacheval_t struct by freeing the dynamically
 * created bmi address.
*/
void SID_clean_up_SID_cacheval_t(SID_cacheval_t **cacheval_t);

/*
 * This function dumps the contents of the sid cache in ASCII to the file
 * specified through the outpfile parameter
 *
 * Returns 0 on success, otherwise returns an error code
*/
int SID_dump_sid_cache(DB **dbp, const char *file_name, FILE *outpfile, int db_records);

/*
 * This function retrieves entries from a primary database and stores them into a
 * bulk buffer DBT. 
 *
 * The minimum amount that can be retrieved is 8 KB of entries.
 * 
 * You can specify a larger size by putting an amount into the two size parameters
 * that will be in addition to the 8KB min.
 *
 * The output DBT is malloc'ed so take care to make sure it is freed, either by
 * using it in a bulk_insert or by manually freeing.
 *
 * If the cache is larger than the buffer, the entry that does not fit is saved in
 * bulk_next_key global variable.
*/
int SID_bulk_retrieve_from_sid_cache(int size_of_retrieve_kb, int size_of_retrieve_mb, DB *dbp, DBC **dbcursorp, DBT *output);

/*
 * This function inserts entries from the input bulk buffer DBT into a database.
 *
 * The function uses the output from SID_bulk_retrieve as its input.
 *
 * The malloc'ed bulk buffer DBT's are freed at the end of this function.
*/
int SID_bulk_insert_into_sid_cache(DB *dbp, DBT *input);


/* <======================== DATABASE FUNCTIONS =======================> */
/* 
 * The functions zeros out the database types key and data values so they
 * are initialized before they are used
*/
void SID_zero_dbt(DBT *key, DBT *data, DBT *pkey);

/*
 * This function creates and opens the environement handle
 *
 * Returns 0 on success, otherwise returns an error code
*/
int SID_create_open_environment(DB_ENV **envp);

/*
 * This function creates and opens the primary database handle, which
 * has the DB_HASH as the access method
 *
 * Returns 0 on success, otherwise returns an error code 
*/
int SID_create_open_sid_cache(DB_ENV **envp, DB **dbp);

/*
 * This function creates, opens, and associates the secondary attribute
 * database handles and sets the database pointers in the secondary_dbs
 * array to point at the correct database. The access method for the 
 * secondary databases is DB_TREE and allows for duplicate records with
 * the same key values
 *
 * Returns 0 on success, otherwise returns an error code
*/
int SID_create_open_assoc_sec_dbs(DB_ENV **envp, DB **dbp, DB *secondary_dbs[], int (* secdbs_callback_functions[])(DB *pri, const DBT *pkey, const DBT *pdata, DBT *skey));

/*
 * This function creates and opens the database cursors set to the primary
 * database in the database cursor pointer array
 *
 * Returns 0 on success, otherwise returns an error code
*/
int SID_create_open_dbcs(DB *secondary_dbs[], DBC *db_cursors[]);

/*
 * This function closes the database cursors in the cursors pointer array
 * 
 * Returns 0 on success, otherwise returns an error code
*/
int SID_close_dbcs(DBC *db_cursors[]);

/*
 * This function closes the primary database, secondary attribute databases,
 * and environment handles
 *
 * Returns 0 on success, otherwise returns an error code
*/
int SID_close_dbs_env(DB_ENV **envp, DB **dbp, DB *secondary_dbs[]);