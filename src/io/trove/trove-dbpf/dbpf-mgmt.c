/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* DB plus files (dbpf) implementation of storage interface.
 *
 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <db.h>
#include <time.h>
#include <malloc.h>
#include <errno.h>

#include <trove.h>
#include <trove-internal.h>
#include <dbpf.h>
#include <dbpf-bstream.h>
#include <dbpf-keyval.h>
#include <trove-ledger.h>

#include <limits.h>

int dbpf_method_id = -1;
char dbpf_method_name[] = "dbpf";

/* Globals */

struct dbpf_collection *my_coll_p = NULL;
struct dbpf_storage *my_storage_p = NULL;
TROVE_handle dbpf_last_handle = 0;

/* Internally used only */
static struct dbpf_storage *dbpf_storage_lookup(char *stoname);
static int dbpf_db_create(char *dbname);
static DB *dbpf_db_open(char *dbname);

/* dbpf_collection_getinfo()
 */
static int dbpf_collection_getinfo(
				   TROVE_coll_id coll_id,
				   TROVE_handle handle,
				   int option,
				   void *parameter)
{
    return 0;
}

/* dbpf_collection_setinfo()
 */
static int dbpf_collection_setinfo(
				   TROVE_coll_id coll_id,
				   TROVE_handle handle,
				   int option,
				   void *parameter)
{
    return 0;
}

/* dbpf_collection_seteattr()
 */
static int dbpf_collection_seteattr(
				    TROVE_coll_id coll_id,
				    TROVE_keyval_s *key_p,
				    TROVE_keyval_s *val_p,
				    TROVE_ds_flags flags,
				    void *user_ptr,
				    TROVE_op_id *out_op_id_p)
{
    int ret;
    struct dbpf_storage *sto_p;
    struct dbpf_collection *coll_p;
    DBT db_key, db_data;

    sto_p = my_storage_p;
    if (sto_p == NULL) return -1;

    coll_p = my_coll_p;
    if (coll_p == NULL) return -1;

    memset(&db_key, 0, sizeof(db_key));
    memset(&db_data, 0, sizeof(db_data));
    db_key.data = key_p->buffer;
    db_key.size = key_p->buffer_sz;
    db_data.data = val_p->buffer;
    db_data.size = val_p->buffer_sz;
    ret = coll_p->coll_attr_db->put(coll_p->coll_attr_db, NULL, &db_key, &db_data, 0);
    if (ret != 0) {
	fprintf(stderr, "dbpf_collection_seteattr: %s\n", db_strerror(ret));
	return -1;
    }

    ret = coll_p->coll_attr_db->sync(coll_p->coll_attr_db, 0);
    if (ret != 0) {
	fprintf(stderr, "dbpf_collection_seteattr: %s\n", db_strerror(ret));
	return -1;
    }

    printf("seteattr done.\n");
    return 1;
}

/* dbpf_collection_geteattr()
 */
static int dbpf_collection_geteattr(
				    TROVE_coll_id coll_id,
				    TROVE_keyval_s *key_p,
				    TROVE_keyval_s *val_p,
				    TROVE_ds_flags flags,
				    void *user_ptr,
				    TROVE_op_id *out_op_id_p)
{
    int ret;
    struct dbpf_storage *sto_p;
    struct dbpf_collection *coll_p;
    DBT db_key, db_data;

    sto_p = my_storage_p;
    if (sto_p == NULL) return -1;

    coll_p = my_coll_p;
    if (coll_p == NULL) return -1;

    memset(&db_key, 0, sizeof(db_key));
    memset(&db_data, 0, sizeof(db_data));
    db_key.data = key_p->buffer;
    db_key.size = key_p->buffer_sz;

    db_data.data  = val_p->buffer;
    db_data.ulen  = val_p->buffer_sz;
    db_data.flags = DB_DBT_USERMEM; /* put the data in our buffer */

    ret = coll_p->coll_attr_db->get(coll_p->coll_attr_db, NULL, &db_key, &db_data, 0);
    if (ret != 0) {
	fprintf(stderr, "dbpf_collection_geteattr: %s\n", db_strerror(ret));
	return -1;
    }

    /* TODO: return the actual size somehow */

    return 1;
}

/* dbpf_initialize()
 *
 * TODO: what's the method_id good for?
 */
static int dbpf_initialize(
			   char *stoname,
			   TROVE_ds_flags flags,
			   char **method_name_p,
			   int method_id)
{
    char *new_method_name;
    struct dbpf_storage *sto_p;
    
    sto_p = dbpf_storage_lookup(stoname);
    if (sto_p == NULL) return -1;
    
    my_storage_p = sto_p;
    
    dbpf_method_id = method_id;
    
    new_method_name = (char *) malloc(sizeof(dbpf_method_name));
    if (new_method_name == NULL) return -1;
    
    strcpy(new_method_name, dbpf_method_name);
    
    *method_name_p = new_method_name;
    
    dbpf_bstream_fdcache_initialize();
    dbpf_keyval_dbcache_initialize();
    
    return 1;
    
}

/* dbpf_finalize()
 */
static int dbpf_finalize(void)
{
    dbpf_method_id = -1;

    /* TODO: clean up all internally allocated structures */

    dbpf_bstream_fdcache_finalize();
    dbpf_keyval_dbcache_finalize();

    return 1;
}

/* dbpf_storage_create()
 *
 * Creates and initializes the databases needed for a dbpf storage
 * space.  This includes:
 * - creating storage attribute database, propagating with create time
 * - creating collections database, filling in create time
 */
static int dbpf_storage_create(
			       char *stoname,
			       void *user_ptr,
			       TROVE_op_id *out_op_id_p)
{
    int ret;

    ret = dbpf_db_create(COLLECTIONS_DBNAME);
    if (ret != 0) return -1;
    ret = dbpf_db_create(STO_ATTRIB_DBNAME);
    if (ret != 0) return -1;
    
    return 1;
}

/* dbpf_storage_remove()
 */
static int dbpf_storage_remove(
			       char *stoname,
			       void *user_ptr,
			       TROVE_op_id *out_op_id_p)
{
    unlink(STO_ATTRIB_DBNAME);
    unlink(COLLECTIONS_DBNAME);
    
    printf("databases for storage space removed.\n");
    return 1;
}

/* dbpf_collection_create()
 *
 * 1) Look in collections database to see if coll_id is already used
 *    (error out if so).
 * 2) Create collection attribute database.
 * 3) Store last handle value in collection attribute database.
 * 4) Create dataspace attributes database.
 * 5) Create keyval and bstream directories.
 */
static int dbpf_collection_create(
				  /* char *stoname, */
				  char *collname,
				  TROVE_coll_id new_coll_id,
				  void *user_ptr,
				  TROVE_op_id *out_op_id_p)
{
    int ret;
    TROVE_handle zero = 0;
    struct dbpf_storage *sto_p;
    struct dbpf_collection_db_entry db_data;
    DB *db_p;
    DBT key, data;
    char path_name[PATH_MAX];
    struct stat dirstat;

#if 0
    sto_p = dbpf_storage_lookup(stoname);
#else
    sto_p = my_storage_p;
#endif
    if (sto_p == NULL) return -1;
    
    printf("storage region found.\n");
    
    /* TODO: we need to look through all storage regions to see
     * if the coll_id is previously used.
     */
    
    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    
    key.data = collname;
    key.size = strlen(collname)+1;
    data.data = &db_data;
    data.ulen = sizeof(db_data);
    data.flags = DB_DBT_USERMEM; /* put the data in our space */
    
    /* ensure that the collection record isn't already there */
    ret = sto_p->coll_db->get(sto_p->coll_db, NULL, &key, &data, 0);
    if (ret != DB_NOTFOUND) {
	printf("coll %s already exists with coll_id %d, len = %d.\n",
	       collname, db_data.coll_id, data.size);
	return -1;
    }
    printf("collection %s didn't already exist.\n", collname);
    
    key.data = collname;
    key.size = strlen(collname)+1;
    data.data = &db_data;
    data.size = sizeof(db_data);
    
    memset(&db_data, 0, sizeof(db_data));
    db_data.coll_id = new_coll_id;
    
    ret = sto_p->coll_db->put(sto_p->coll_db, NULL, &key, &data, 0);
    if (ret != 0) {
	return -1;
    }
    
    /* always sync to ensure that data made it to the disk */
    if ((ret = sto_p->coll_db->sync(sto_p->coll_db, 0)) != 0) {
	return -1;
    }
    
    printf("collection db updated, coll_id = %d.\n", new_coll_id);

    /* Create both the base directory, if necessary, then the new collection
     * directory.
     */
    snprintf(path_name, PATH_MAX, "/%s", TROVE_DIR);
    ret = stat(path_name, &dirstat);
    if (ret < 0 && errno != ENOENT) {
	perror("trove collection directory create");
	return -1;
    }
    else if (ret < 0) {
	ret = mkdir(path_name, 0755);
	if (ret != 0) {
	    perror("base collection directory create");
	    return -1;
	}
    }
    
    snprintf(path_name, PATH_MAX, "/%s/%08x", TROVE_DIR, new_coll_id);

    printf("dirname = %s\n", path_name);
    ret = mkdir(path_name, 0755);
    if (ret != 0) {
	perror("base collection directory create");
	return -1;
    }
    
    snprintf(path_name, PATH_MAX, "/%s/%08x/%s", TROVE_DIR, new_coll_id, COLL_ATTRIB_DBNAME);

    /* create collection attributes database, drop in last handle */
    ret = dbpf_db_create(path_name);
    if (ret != 0) return -1;
    
    /* Note: somewhat inefficient, but we don't create collections
     * often enough to matter
     */
    db_p = dbpf_db_open(path_name);
    
    /* store initial handle value */
    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data = LAST_HANDLE_STRING;
    key.size = sizeof(LAST_HANDLE_STRING);
    data.data = &zero;
    data.size = sizeof(zero);
    ret = db_p->put(db_p, NULL, &key, &data, 0);
    if (ret != 0) return -1;
    

    snprintf(path_name, PATH_MAX, "/%s/%08x/%s", TROVE_DIR, new_coll_id, DS_ATTRIB_DBNAME);
    /* create dataspace attributes database */
    ret = dbpf_db_create(path_name);
    if (ret != 0) return -1;
    
    snprintf(path_name, PATH_MAX, "/%s/%08x/%s", TROVE_DIR, new_coll_id, KEYVAL_DIRNAME);
    ret = mkdir(path_name, 0755);
    if (ret != 0) {
	perror("keyval directory create");
	return -1;
    }
    
    snprintf(path_name, PATH_MAX, "/%s/%08x/%s", TROVE_DIR, new_coll_id, BSTREAM_DIRNAME);
    ret = mkdir(path_name, 0755);
    if (ret != 0) {
	perror("bstream directory create");
	return -1;
    }
    
    printf("subdirectories for storing bstreams and keyvals created.\n");
    return 1;
}

/* dbpf_collection_remove()
 */
static int dbpf_collection_remove(
				  /* char *stoname, */
				  char *collname,
				  void *user_ptr,
				  TROVE_op_id *out_op_id_p)
{
    char path_name[PATH_MAX];

    struct dbpf_storage *sto_p;
    struct dbpf_collection_db_entry db_data;
    DBT key, data;
    int ret;

    /* to get the path, need to get handle from name */

    /* only one storage space for now */
    sto_p = my_storage_p;

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));

    key.data = collname;
    key.size = strlen(collname) + 1;
    data.data = &db_data;
    data.ulen = sizeof(db_data);
    data.flags = DB_DBT_USERMEM;

    ret = sto_p->coll_db->get(sto_p->coll_db, NULL, &key, &data, 0);
    if (ret != 0) {
	    sto_p->coll_db->err(sto_p->coll_db, ret, "DB->get");
	    return -1;
    }

    snprintf(path_name, PATH_MAX, "/%s/%08x/%s", TROVE_DIR, db_data.coll_id, DS_ATTRIB_DBNAME);
    unlink(path_name);

    snprintf(path_name, PATH_MAX, "/%s/%08x/%s", TROVE_DIR, db_data.coll_id, COLL_ATTRIB_DBNAME);
    unlink(path_name);
    
    /* TODO: REMOVE ALL BSTREAM AND KEYVAL FILES */
    snprintf(path_name, PATH_MAX, "/%s/%08x/%s", TROVE_DIR, db_data.coll_id, BSTREAM_DIRNAME);
    rmdir(path_name);

    snprintf(path_name, PATH_MAX, "/%s/%08x/%s", TROVE_DIR, db_data.coll_id, KEYVAL_DIRNAME);
    rmdir(path_name);

    printf("databases and directories for collection removed.\n");

    return 1;
}

/* dbpf_collection_lookup()
 */
static int dbpf_collection_lookup(
				  /* char *stoname, */
				  char *collname,
				  TROVE_coll_id *coll_id_p,
				  void *user_ptr,
				  TROVE_op_id *out_op_id_p)
{
    int ret;
    size_t slen;
    struct dbpf_storage *sto_p;
    struct dbpf_collection *coll_p;
    struct dbpf_collection_db_entry db_data;
    DBT key, data;
    char path_name[PATH_MAX];
    
    /* look in cached values to see if it is in memory already */
    if (my_coll_p != NULL) {
	    *coll_id_p = my_coll_p->coll_id;
	    return 1;
    }
    
    /* only one fs for now */
    sto_p = my_storage_p;
    
    printf("collection %s is part of storage space %s.\n", collname,
	   sto_p->name);
    
    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data = collname;
    key.size = strlen(collname)+1;
    data.data = &db_data;
    data.ulen = sizeof(db_data);
    data.flags = DB_DBT_USERMEM; /* put the data in our space */
    
    ret = sto_p->coll_db->get(sto_p->coll_db, NULL, &key, &data, 0);
    if (ret == DB_NOTFOUND) {
	/* not an error per se */
	printf("lookup didn't find collection\n");
	return -1;
    }
    else if (ret != 0) {
	/* really an error of some kind */
	sto_p->coll_db->err(sto_p->coll_db, ret, "DB->get");
	printf("lookup got error\n");
	return -1;
    }
    
    printf("found collection %s in database.\n", collname);
    
    printf("coll_id = %d.\n", db_data.coll_id);
    
    coll_p = (struct dbpf_collection *)malloc(sizeof(struct dbpf_collection));
    if (coll_p == NULL) {
	return -1;
    }
    
    coll_p->refct = 0;
    coll_p->coll_id = db_data.coll_id;
    coll_p->storage = sto_p;
    
    slen = strlen(collname)+1;
    if ((coll_p->name = (char *) malloc(slen)) == NULL) {
		return -1;
    }
    strncpy(coll_p->name, collname, slen);

    /* open collection attribute database */
    snprintf(path_name, PATH_MAX, "/%s/%08x/%s", TROVE_DIR, coll_p->coll_id, COLL_ATTRIB_DBNAME); 
    coll_p->coll_attr_db = dbpf_db_open(path_name);
    if (coll_p->coll_attr_db == NULL) return -1;
    
    /* open dataspace database */
    snprintf(path_name, PATH_MAX, "/%s/%08x/%s", TROVE_DIR, coll_p->coll_id, DS_ATTRIB_DBNAME); 
    coll_p->ds_db = dbpf_db_open(path_name);
    if (coll_p->ds_db == NULL) return -1;

    my_coll_p = coll_p;

    /* XXX HARDCODED NAME FOR NOW... */
    coll_p->free_handles = trove_handle_ledger_init(coll_p->coll_id, "admin-foo");

    /* fill in output parameter */
    *coll_id_p = coll_p->coll_id;
    return 1;
}

/* dbpf_storage_lookup()
 *
 * Internal function.
 *
 * Returns pointer to a dbpf_storage structure that refers to the
 * named storage region.  This might involve populating the structure,
 * or it might simply involve returning the pointer (if the structure
 * was already available).
 *
 * It is expected that this function will be called primarily on
 * startup, as this is the point where we are passed a list of storage
 * regions that we are responsible for.  After that point most
 * operations will be referring to a coll_id instead, and the dbpf_storage
 * structure will be found by following the link from the dbpf_coll
 * structure associated with that collection.
 */
static struct dbpf_storage *dbpf_storage_lookup(char *stoname)
{
    size_t slen;
    struct dbpf_storage *sto_p;

    if (my_storage_p != NULL) return my_storage_p;

    /* at the moment we just allocate a region and return it.  later
     * we will build a data structure to keep up with these, etc.
     */

    sto_p = (struct dbpf_storage *) malloc(sizeof(struct dbpf_storage));
    if (sto_p == NULL) return NULL;

    /* TODO: could do one malloc and some pointer math */
    slen = strlen(stoname)+1;
    sto_p->name = (char *) malloc(slen);
    if (sto_p->name == NULL) {
	free(sto_p);
	return NULL;
    }
    strncpy(sto_p->name, stoname, slen);

    /* TODO: make real names based on paths... */
    sto_p->refct = 0;
    sto_p->sto_attr_db = dbpf_db_open(STO_ATTRIB_DBNAME);
    if (sto_p->sto_attr_db == NULL) return NULL;

    sto_p->coll_db = dbpf_db_open(COLLECTIONS_DBNAME);
    if (sto_p->coll_db == NULL) return NULL;

    my_storage_p = sto_p;
    return sto_p;
}


/* dbpf_db_create()
 *
 * Internal function for creating first instances of the databases for a
 * db plus files storage region.
 */
static int dbpf_db_create(
			  char *dbname)
{
    int ret;
    DB *db_p;
    DBT key, data;
    struct tm *tm_p;
    time_t cur_time;
    char keystring[] = "create_time";
    char datastring[64];

    /* set up create string */
    time(&cur_time);
    tm_p = localtime(&cur_time);
    strftime(datastring, 64, "%Y-%m-%d", tm_p);

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));

    key.data = keystring;
    key.size = strlen(keystring)+1;
    data.data = datastring;
    data.size = strlen(datastring)+1;

    if ((ret = db_create(&db_p, NULL, 0)) != 0) {
	fprintf(stderr, "dbpf_storage_create: %s\n",
		db_strerror(ret));
	return -1;
    }

/* DB_RECNUM makes it easier to iterate through every key in chunks */
    if (( ret =  db_p->set_flags(db_p, DB_RECNUM)) ) {
	    db_p->err(db_p, ret, "%s: set_flags", dbname);
	    return -1;
    }
    if ((ret = db_p->open(db_p, dbname, NULL, DB_BTREE,
			  DB_CREATE|DB_EXCL, 0644)) != 0)
    {
	db_p->err(db_p, ret, "%s", dbname);
	return -1;
    }
    
#if 0
    if ((ret = db_p->put(db_p, NULL, &key, &data, 0)) == 0)
	printf("db: %s: key stored.\n", (char *)key.data);
    else {
	db_p->err(db_p, ret, "DB->put");
	return -1;
    }
#endif

    if ((ret = db_p->close(db_p, 0)) != 0) {
	fprintf(stderr, "dbpf_storage_create: %s\n",
		db_strerror(ret));
	return -1;
    }

    return 0;
}

/* dbpf_db_open()
 *
 * Internal function for opening the databases that are used to store
 * basic information on a storage region.
 */
static DB *dbpf_db_open(
			char *dbname)
{
    int ret;
    DB *db_p;

    if ((ret = db_create(&db_p, NULL, 0)) != 0) {
	return NULL;
    }

    db_p->set_errfile(db_p, stderr);
    db_p->set_errpfx(db_p, "xxx");

    /* DB_RECNUM makes it easier to iterate through every key in chunks */
    if (( ret =  db_p->set_flags(db_p, DB_RECNUM)) ) {
	    db_p->err(db_p, ret, "%s: set_flags", dbname);
	    return NULL;
    }
    if ((ret = db_p->open(db_p, dbname, NULL, DB_BTREE, 0, 0)) != 0)
    {
	printf("open failed on database %s.\n", dbname);
	return NULL;
    }

    return db_p;
}

/* dbpf_mgmt_ops
 *
 * Structure holding pointers to all the management operations functions
 * for this storage interface implementation.
 */
struct TROVE_mgmt_ops dbpf_mgmt_ops =
{
    dbpf_initialize,
    dbpf_finalize,
    dbpf_storage_create,
    dbpf_storage_remove,
    dbpf_collection_create,
    dbpf_collection_remove,
    dbpf_collection_lookup,
    dbpf_collection_setinfo,
    dbpf_collection_getinfo,
    dbpf_collection_seteattr,
    dbpf_collection_geteattr
};


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sw=4 noexpandtab
 */
