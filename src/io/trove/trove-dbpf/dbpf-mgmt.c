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
#include <limits.h>

#include "trove.h"
#include "trove-internal.h"
#include "dbpf.h"
#include "dbpf-op-queue.h"
#include "dbpf-bstream.h"
#include "dbpf-keyval.h"
#include "dbpf-dspace.h"
#include "trove-ledger.h"

int dbpf_method_id = -1;
char dbpf_method_name[] = "dbpf";

/* Globals */

struct dbpf_storage *my_storage_p = NULL;

/* Internally used only */
static struct dbpf_storage *dbpf_storage_lookup(char *stoname);
static int dbpf_db_create(char *dbname);
static DB *dbpf_db_open(char *dbname);
static int dbpf_mkpath(char *pathname, mode_t mode);

/* dbpf_collection_getinfo()
 */
static int dbpf_collection_getinfo(TROVE_coll_id coll_id,
				   TROVE_handle handle,
				   int option,
				   void *parameter)
{
    return -1;
}

/* dbpf_collection_setinfo()
 */
static int dbpf_collection_setinfo(TROVE_coll_id coll_id,
				   TROVE_handle handle,
				   int option,
				   void *parameter)
{
    return -1;
}

/* dbpf_collection_seteattr()
 */
static int dbpf_collection_seteattr(TROVE_coll_id coll_id,
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

    coll_p = dbpf_collection_find_registered(coll_id);
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

#if 0
    printf("seteattr done.\n");
#endif
    return 1;
}

/* dbpf_collection_geteattr()
 */
static int dbpf_collection_geteattr(TROVE_coll_id coll_id,
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

    coll_p = dbpf_collection_find_registered(coll_id);
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

    val_p->read_sz = db_data.size; /* return the actual size */

    return 1;
}

/* dbpf_initialize()
 *
 * TODO: what's the method_id good for?
 */
static int dbpf_initialize(char *stoname,
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
    
    dbpf_dspace_dbcache_initialize();
    dbpf_bstream_fdcache_initialize();
    dbpf_keyval_dbcache_initialize();
    
    return 1;
}

/* dbpf_finalize()
 */
static int dbpf_finalize(void)
{
    int ret;

    dbpf_method_id = -1;

    /* TODO: clean up all internally allocated structures */

    dbpf_bstream_fdcache_finalize();
    dbpf_keyval_dbcache_finalize();
    dbpf_dspace_dbcache_finalize();

    dbpf_collection_clear_registered();

    /* manually free the cached storage space info */
    /* always sync to ensure that data made it to the disk */
    if ((ret = my_storage_p->sto_attr_db->sync(my_storage_p->sto_attr_db, 0)) != 0) {
	return -1;
    }
    if ((ret = my_storage_p->sto_attr_db->close(my_storage_p->sto_attr_db, 0)) != 0) {
	fprintf(stderr, "dbpf_finalize: %s\n",
		db_strerror(ret));
	return -1;
    }

    if ((ret = my_storage_p->coll_db->sync(my_storage_p->coll_db, 0)) != 0) {
	return -1;
    }
    if ((ret = my_storage_p->coll_db->close(my_storage_p->coll_db, 0)) != 0) {
	fprintf(stderr, "dbpf_finalize: %s\n",
		db_strerror(ret));
	return -1;
    }
    free(my_storage_p->name);
    free(my_storage_p);
    my_storage_p = NULL;

    return 1;
}

/* dbpf_storage_create()
 *
 * Creates and initializes the databases needed for a dbpf storage
 * space.  This includes:
 * - creating the path to the storage directory
 * - creating storage attribute database, propagating with create time
 * - creating collections database, filling in create time
 */
static int dbpf_storage_create(char *stoname,
			       void *user_ptr,
			       TROVE_op_id *out_op_id_p)
{
    int ret;
    char path_name[PATH_MAX];


    DBPF_GET_STORAGE_DIRNAME(path_name, PATH_MAX, stoname);
    ret = dbpf_mkpath(path_name, 0755);
    if (ret != 0) return -1;

    DBPF_GET_STO_ATTRIB_DBNAME(path_name, PATH_MAX, stoname);
    ret = dbpf_db_create(path_name);
    if (ret != 0) return -1;
    
    DBPF_GET_COLLECTIONS_DBNAME(path_name, PATH_MAX, stoname);
    ret = dbpf_db_create(path_name);
    if (ret != 0) return -1;

    return 1;
}

/* dbpf_storage_remove()
 */
static int dbpf_storage_remove(char *stoname,
			       void *user_ptr,
			       TROVE_op_id *out_op_id_p)
{
    char path_name[PATH_MAX];

    DBPF_GET_STO_ATTRIB_DBNAME(path_name, PATH_MAX, stoname);
    unlink(path_name);
    DBPF_GET_COLLECTIONS_DBNAME(path_name, PATH_MAX, stoname);
    unlink(path_name);

    /* TODO: REMOVE ALL THE OTHER FILES!!! */
#if 0
    printf("databases for storage space removed.\n");
#endif
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
static int dbpf_collection_create(char *collname,
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

    sto_p = my_storage_p;

    if (sto_p == NULL) return -1;
    
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
#if 0
	printf("coll %s already exists with coll_id %d, len = %d.\n",
	       collname, db_data.coll_id, data.size);
#endif
	return -1;
    }
#if 0
    printf("collection %s didn't already exist.\n", collname);
#endif
    
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

#if 0    
    printf("collection db updated, coll_id = %d.\n", new_coll_id);
#endif

    /* Create both the base directory, if necessary, then the new collection
     * directory.
     */
    DBPF_GET_STORAGE_DIRNAME(path_name, PATH_MAX, sto_p->name); /* see dbpf.h */
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
    
    DBPF_GET_COLL_DIRNAME(path_name, PATH_MAX, sto_p->name, new_coll_id);
#if 0
    printf("dirname = %s\n", path_name);
#endif
    ret = mkdir(path_name, 0755);
    if (ret != 0) {
	perror("base collection directory create");
	return -1;
    }
    
    DBPF_GET_COLL_ATTRIB_DBNAME(path_name, PATH_MAX, sto_p->name, new_coll_id);
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
    

    DBPF_GET_DS_ATTRIB_DBNAME(path_name, PATH_MAX, sto_p->name, new_coll_id);
    /* create dataspace attributes database */
    ret = dbpf_db_create(path_name);
    if (ret != 0) return -1;
    
    DBPF_GET_KEYVAL_DIRNAME(path_name, PATH_MAX, sto_p->name, new_coll_id);
    ret = mkdir(path_name, 0755);
    if (ret != 0) {
	perror("keyval directory create");
	return -1;
    }
    
    DBPF_GET_BSTREAM_DIRNAME(path_name, PATH_MAX, sto_p->name, new_coll_id);
    ret = mkdir(path_name, 0755);
    if (ret != 0) {
	perror("bstream directory create");
	return -1;
    }

#if 0
    printf("subdirectories for storing bstreams and keyvals created.\n");
#endif
    return 1;
}

/* dbpf_collection_remove()
 */
static int dbpf_collection_remove(char *collname,
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

    DBPF_GET_DS_ATTRIB_DBNAME(path_name, PATH_MAX, sto_p->name, db_data.coll_id);
    unlink(path_name);

    DBPF_GET_COLL_ATTRIB_DBNAME(path_name, PATH_MAX, sto_p->name, db_data.coll_id);
    unlink(path_name);
    
    /* TODO: REMOVE ALL BSTREAM AND KEYVAL FILES */
    DBPF_GET_BSTREAM_DIRNAME(path_name, PATH_MAX, sto_p->name, db_data.coll_id);
    rmdir(path_name);

    DBPF_GET_KEYVAL_DIRNAME(path_name, PATH_MAX, sto_p->name, db_data.coll_id);
    rmdir(path_name);

#if 0
    printf("databases and directories for collection removed.\n");
#endif

    return 1;
}

/* dbpf_collection_iterate()
 */
static int dbpf_collection_iterate(TROVE_ds_position *inout_position_p,
				   TROVE_keyval_s *name_array,
				   TROVE_coll_id *coll_id_array,
				   int *inout_count_p,
				   TROVE_ds_flags flags,
				   TROVE_vtag_s *vtag,
				   void *user_ptr,
				   TROVE_op_id *out_op_id_p)
{
    int ret, i=0;
    db_recno_t recno;
    DB *db_p;
    DBC *dbc_p;
    DBT key, data;
    struct dbpf_collection_db_entry db_entry;

    /* if they passed in that they are at the end, return 0.
     *
     * this seems silly maybe, but it makes while (count) loops
     * work right.
     */
    if (*inout_position_p == TROVE_ITERATE_END) {
	*inout_count_p = 0;
	return 1;
    }

    /* collection db is stored with storage space info */
    db_p = my_storage_p->coll_db;

    /* get a cursor */
    ret = db_p->cursor(db_p, NULL, &dbc_p, 0);
    if (ret != 0) goto return_error;


    /* see keyval iterate for discussion of this implementation; it was
     * basically copied from there. -- RobR
     */

    if (*inout_position_p != TROVE_ITERATE_START) {
	/* need to position cursor before reading.  note that this will
	 * actually position the cursor over the last thing that was read
	 * on the last call, so we don't need to return what we get back.
	 */

	/* here we make sure that the key is big enough to hold the
	 * position that we need to pass in.
	 */
	memset(&key, 0, sizeof(key));
	if (sizeof(recno) < name_array[0].buffer_sz) {
	    key.data = name_array[0].buffer;
	    key.size = key.ulen = name_array[0].buffer_sz;
	}
	else {
	    key.data = &recno;
	    key.size = key.ulen = sizeof(recno);
	}
	*(TROVE_ds_position *) key.data = *inout_position_p;
	key.flags |= DB_DBT_USERMEM;

	memset(&data, 0, sizeof(data));
	data.data = &db_entry;
	data.size = data.ulen = sizeof(db_entry);
	data.flags |= DB_DBT_USERMEM;

	/* position the cursor and grab the first key/value pair */
	ret = dbc_p->c_get(dbc_p, &key, &data, DB_SET_RECNO);
	if (ret == DB_NOTFOUND) goto return_ok;
	else if (ret != 0) goto return_error;
    }

    for (i=0; i < *inout_count_p; i++)
    {
	memset(&key, 0, sizeof(key));
	key.data = name_array[i].buffer;
	key.size = key.ulen = name_array[i].buffer_sz;
	key.flags |= DB_DBT_USERMEM;

	memset(&data, 0, sizeof(data));
	data.data = &db_entry;
	data.size = data.ulen = sizeof(db_entry);
	data.flags |= DB_DBT_USERMEM;
	
	ret = dbc_p->c_get(dbc_p, &key, &data, DB_NEXT);
	if (ret == DB_NOTFOUND) {
	    goto return_ok;
	}
	else if (ret != 0) goto return_error;

	/* store coll_id for return */
	coll_id_array[i] = db_entry.coll_id;
    }
    
return_ok:
    if (ret == DB_NOTFOUND) {
	*inout_position_p = TROVE_ITERATE_END;
    }
    else {
	char buf[64];
	/* get the record number to return.
	 *
	 * note: key field is ignored by c_get in this case.  sort of.
	 * i'm not actually sure what they mean by "ignored", because
	 * it sure seems to matter what you put in there...
	 *
	 * TODO: FIGURE OUT WHAT IS GOING ON W/KEY AND TRY TO AVOID USING
	 * ANY MEMORY.
	 */
	memset(&key, 0, sizeof(key));
	key.data  = buf;
	key.size  = key.ulen = 64;
	key.dlen  = 64;
	key.doff  = 0;
	key.flags |= DB_DBT_USERMEM | DB_DBT_PARTIAL;

	memset(&data, 0, sizeof(data));
	data.data = &recno;
	data.size = data.ulen = sizeof(recno);
	data.flags |= DB_DBT_USERMEM;

	ret = dbc_p->c_get(dbc_p, &key, &data, DB_GET_RECNO);
	if (ret == DB_NOTFOUND) printf("warning: keyval iterate -- notfound\n");
	else if (ret != 0) printf("warning: keyval iterate -- some other failure @ recno\n");

	assert(recno != TROVE_ITERATE_START && recno != TROVE_ITERATE_END);
	*inout_position_p = recno;
    }
    /* 'position' points us to the record we just read, or is set to END */

    *inout_count_p = i;

    /* sync if requested by user
     */
    if (flags & TROVE_SYNC) {
	if ((ret = db_p->sync(db_p, 0)) != 0) {
	    goto return_error;
	}
    }

    /* free the cursor */
    ret = dbc_p->c_close(dbc_p);
    if (ret != 0) goto return_error;
    return 1;
    
return_error:
    fprintf(stderr, "dbpf_collection_iterate_op_svc: %s\n", db_strerror(ret));
    *inout_count_p = i;
    return -1;
}


/* dbpf_collection_lookup()
 */
static int dbpf_collection_lookup(char *collname,
				  TROVE_coll_id *out_coll_id_p,
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
    
    /* only one fs for now */
    sto_p = my_storage_p;
    
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
#if 0
	printf("lookup didn't find collection\n");
#endif
	return -1;
    }
    else if (ret != 0) {
	/* really an error of some kind */
	sto_p->coll_db->err(sto_p->coll_db, ret, "DB->get");
	printf("lookup got error\n");
	return -1;
    }

#if 0
    printf("found collection %s in database.\n", collname);
    
    printf("coll_id = %d.\n", db_data.coll_id);
#endif

    /* look to see if we have already registered this collection; if so, return */
    coll_p = dbpf_collection_find_registered(db_data.coll_id);
    if (coll_p != NULL) {
	*out_coll_id_p = coll_p->coll_id;
	return 1;
    }

    /* this collection hasn't been registered already (ie. looked up before) */

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
    DBPF_GET_COLL_ATTRIB_DBNAME(path_name, PATH_MAX, sto_p->name, coll_p->coll_id);
    coll_p->coll_attr_db = dbpf_db_open(path_name);
    if (coll_p->coll_attr_db == NULL) return -1;
    
    /* open dataspace database */
    DBPF_GET_DS_ATTRIB_DBNAME(path_name, PATH_MAX, sto_p->name, coll_p->coll_id);
    coll_p->ds_db = dbpf_db_open(path_name);
    if (coll_p->ds_db == NULL) return -1;

    dbpf_collection_register(coll_p);

    /* XXX HARDCODED NAME FOR NOW... */
    coll_p->free_handles = trove_handle_ledger_init(coll_p->coll_id, "admin-foo");

    /* fill in output parameter */
    *out_coll_id_p = coll_p->coll_id;
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
    char path_name[PATH_MAX];

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
    DBPF_GET_STO_ATTRIB_DBNAME(path_name, PATH_MAX, stoname);
    sto_p->sto_attr_db = dbpf_db_open(path_name);
    if (sto_p->sto_attr_db == NULL) return NULL;

    DBPF_GET_COLLECTIONS_DBNAME(path_name, PATH_MAX, stoname);
    sto_p->coll_db = dbpf_db_open(path_name);
    if (sto_p->coll_db == NULL) return NULL;

    my_storage_p = sto_p;
    return sto_p;
}

/* dbpf_mkpath()
 */
static int dbpf_mkpath(char *pathname, mode_t mode)
{
    int ret, len, pos = 0, nullpos = 0, killed_slash;
    struct stat buf;

    len = strlen(pathname);

    /* insist on an absolute path */
    if (pathname[0] != '/') return -1;
    
    while (pos < len) {
	nullpos = pos;
	killed_slash = 0;

	while ((pathname[nullpos] != '\0') && (pathname[nullpos] != '/')) nullpos++;

	/* NOTE: this could be made a little simpler, but it would be less
	 * intuitive I think -- Rob
	 */
	if (nullpos <= pos + 1) {
	    /* extra slash or trailing slash; ignore */
	    nullpos++;
	    pos = nullpos;
	}
	else {
	    if (pathname[nullpos] == '/') {
		killed_slash = 1;
		pathname[nullpos] = 0;
	    }

	    /* TODO: FIX STRING BEFORE RETURNING IN ERROR CASES */

	    ret = stat(pathname, &buf);
	    if (ret == 0 && !S_ISDIR(buf.st_mode)) return -1;
	    if (ret != 0) {
		ret = mkdir(pathname, mode);
		if (ret != 0) return -1;
	    }
	    
	    if (killed_slash) {
		pathname[nullpos] = '/';
	    }

	    nullpos++;
	    pos = nullpos;
	}
    }

    return 0;
}

/* dbpf_db_create()
 *
 * Internal function for creating first instances of the databases for a
 * db plus files storage region.
 */
static int dbpf_db_create(char *dbname)
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
    if ((ret = db_p->open(db_p,
#ifdef HAVE_TXNID_PARAMETER_TO_DB_OPEN
                          NULL,
#endif
                          dbname,
                          NULL,
                          DB_BTREE,
			  DB_CREATE|DB_EXCL,
                          0644)) != 0)
    {
	db_p->err(db_p, ret, "%s", dbname);
	return -1;
    }
    
#if 0
    /* store the time string -- removed because it is dirtying up the space */
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
static DB *dbpf_db_open(char *dbname)
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
    if ((ret = db_p->open(db_p,
#ifdef HAVE_TXNID_PARAMETER_TO_DB_OPEN
                          NULL,
#endif
                          dbname,
                          NULL,
                          DB_BTREE,
                          0,
                          0)) != 0)
    {
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
    dbpf_collection_iterate,
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
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
