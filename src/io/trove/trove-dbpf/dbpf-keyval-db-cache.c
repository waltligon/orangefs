/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <db.h>
#include <string.h>
#include <limits.h>

#include "trove.h"
#include "trove-internal.h"
#include "dbpf.h"
#include "dbpf-keyval.h"

/* NOTE: THIS IS ALMOST CERTAINLY BROKEN FOR MULTITHREADED APPS!!!
 */

enum {
    DBCACHE_ENTRIES = 16
};

struct keyval_dbcache_entry {
    int ref_ct; /* -1 == not a valid cache entry */
    gen_mutex_t mutex;
    TROVE_coll_id coll_id;
    TROVE_handle handle;
    DB *db_p;
};

static struct keyval_dbcache_entry keyval_db_cache[DBCACHE_ENTRIES];

/* dbpf_keyval_dbcache_initialize()
 */
void dbpf_keyval_dbcache_initialize(void)
{
    int i;

    for (i=0; i < DBCACHE_ENTRIES; i++) {
	gen_mutex_init(&keyval_db_cache[i].mutex);
	keyval_db_cache[i].ref_ct = -1;
	keyval_db_cache[i].db_p   = NULL;
    }
}

/* dbpf_keyval_dbcache_finalize()
 */
void dbpf_keyval_dbcache_finalize(void)
{
    int i, ret;

    for (i=0; i < DBCACHE_ENTRIES; i++) {
	if (keyval_db_cache[i].ref_ct > 0) {
	    printf("warning: ref_ct = %d on handle %Lx in dbcache\n",
		   keyval_db_cache[i].ref_ct,
		   keyval_db_cache[i].handle);
	}
	if (keyval_db_cache[i].ref_ct >= 0) {
	    /* close DB */
	    ret = keyval_db_cache[i].db_p->close(keyval_db_cache[i].db_p, 0);
	    if (ret != 0) assert(0);
	}
    }
}

/* dbpf_keyval_dbcache_try_remove()
 *
 * Returns one of DBPF_KEYVAL_DBCACHE_ERROR, DBPF_KEYVAL_DBCACHE_BUSY,
 * DBPF_KEYVAL_DBCACHE_SUCCESS.
 */
int dbpf_keyval_dbcache_try_remove(TROVE_coll_id coll_id,
				   TROVE_handle handle)
{
    int i, ret;
    char filename[PATH_MAX];
    DB *db_p;

    for (i=0; i < DBCACHE_ENTRIES; i++) {
	if (!(ret = gen_mutex_trylock(&keyval_db_cache[i].mutex)) &&
	    keyval_db_cache[i].ref_ct  >= 0 &&
	    keyval_db_cache[i].coll_id == coll_id &&
	    keyval_db_cache[i].handle  == handle) break;
	else if (ret == 0) gen_mutex_unlock(&keyval_db_cache[i].mutex);
    }

    if (i < DBCACHE_ENTRIES) {
	/* found cached DB */

	if (keyval_db_cache[i].ref_ct > 0) {
	    gen_mutex_unlock(&keyval_db_cache[i].mutex);
	    return DBPF_KEYVAL_DBCACHE_BUSY;
	}

	/* close it */
	ret = keyval_db_cache[i].db_p->close(keyval_db_cache[i].db_p, 0);
	if (ret != 0) {
	    printf("db: close error\n");
	}
	keyval_db_cache[i].ref_ct = -1;
	keyval_db_cache[i].db_p   = NULL;
	gen_mutex_unlock(&keyval_db_cache[i].mutex);
    }

    DBPF_GET_KEYVAL_DBNAME(filename, PATH_MAX, my_storage_p->name, coll_id, handle);
#if 0
    printf("file name = %s\n", filename);
#endif

    ret = DBPF_UNLINK(filename);
    if (ret != 0 && errno != ENOENT) return DBPF_KEYVAL_DBCACHE_ERROR;

    return DBPF_KEYVAL_DBCACHE_SUCCESS;
}

/* dbpf_keyval_dbcache_try_get()
 *
 * Right now we don't place any kind of upper limit on the number of
 * references to the same db, so this will never return BUSY.  That might
 * change at some later time.
 *
 * Returns one of DBPF_KEYVAL_DBCACHE_ERROR, DBPF_KEYVAL_DBCACHE_BUSY,
 * DBPF_KEYVAL_DBCACHE_SUCCESS.
 *
 * create_flag - 0 = don't create if doesn't exist; non-zero = create.
 */
int dbpf_keyval_dbcache_try_get(TROVE_coll_id coll_id,
				TROVE_handle handle,
				int create_flag,
				DB **db_pp)
{
    int i, ret;
    char filename[PATH_MAX];
    DB *db_p;

    for (i=0; i < DBCACHE_ENTRIES; i++) {
	if (!(ret = gen_mutex_trylock(&keyval_db_cache[i].mutex)) &&
	    keyval_db_cache[i].ref_ct  >= 0 &&
	    keyval_db_cache[i].coll_id == coll_id &&
	    keyval_db_cache[i].handle  == handle) break;
	else if (ret == 0) gen_mutex_unlock(&keyval_db_cache[i].mutex);
    }

    if (i < DBCACHE_ENTRIES) {
	/* found cached DB */
#if 0
	printf("dbcache: found cached db at index %d\n", i);
#endif
	keyval_db_cache[i].ref_ct++;
	*db_pp = keyval_db_cache[i].db_p;
	gen_mutex_unlock(&keyval_db_cache[i].mutex);
	return DBPF_KEYVAL_DBCACHE_SUCCESS;
    }

    /* no cached db; open it */
    for (i=0; i < DBCACHE_ENTRIES; i++) {
	if (!(ret = gen_mutex_trylock(&keyval_db_cache[i].mutex)) &&
	    keyval_db_cache[i].ref_ct == -1)
	{
#if 0
	    printf("dbcache: found empty entry at %d\n", i);
#endif
	    break;
	}
	else if (ret == 0) gen_mutex_unlock(&keyval_db_cache[i].mutex);
    }

    if (i == DBCACHE_ENTRIES) {
	/* no invalid entries; search for one that isn't in use */
	for (i=0; i < DBCACHE_ENTRIES; i++) {
	    if (!(ret = gen_mutex_trylock(&keyval_db_cache[i].mutex)) &&
		keyval_db_cache[i].ref_ct == 0)
	    {
#if 0
		printf("dbcache: no empty entries; found unused entry at %d\n", i);
#endif
		
		ret = keyval_db_cache[i].db_p->close(keyval_db_cache[i].db_p, 0);
		if (ret != 0) {
		    printf("db: close error\n");
		}
		keyval_db_cache[i].ref_ct = -1;
		keyval_db_cache[i].db_p   = NULL;
		break;
	    }
	    else if (ret == 0) gen_mutex_unlock(&keyval_db_cache[i].mutex);
	}
	if (i == DBCACHE_ENTRIES) assert(0);
    }

    /* have lock on an entry, open/create */

    DBPF_GET_KEYVAL_DBNAME(filename, PATH_MAX, my_storage_p->name, coll_id, handle);
#if 0
    printf("file name = %s\n", filename);
#endif

    ret = db_create(&(keyval_db_cache[i].db_p), NULL, 0);
    if (ret != 0) {
	    fprintf(stderr, "dbpf_keyval_dbcache_get: %s\n", db_strerror(ret));
	    assert(0);
    }

    db_p = keyval_db_cache[i].db_p; /* for simplicity */
    db_p->set_errfile(db_p, stderr);
    db_p->set_errpfx(db_p, "xxx");
    /* DB_RECNUM makes it easier to iterate through every key in chunks */
    if (( ret =  db_p->set_flags(db_p, DB_RECNUM)) ) {
	    db_p->err(db_p, ret, "%s: set_flags", filename);
	    assert(0);
    }
    ret = db_p->open(db_p,
#ifdef HAVE_TXNID_PARAMETER_TO_DB_OPEN
                     NULL,
#endif
		     filename,
		     NULL,
		     DB_UNKNOWN,
		     0,
		     0);
    if (ret == ENOENT && create_flag != 0) {
	/* if no such DB and create_flag is set, try to create the DB */
	ret = db_p->open(db_p,
#ifdef HAVE_TXNID_PARAMETER_TO_DB_OPEN
                         NULL,
#endif
			 filename,
			 NULL,
			 DB_BTREE,
			 DB_CREATE|DB_EXCL,
			 0644);
	if (ret != 0) assert(0);
    }
    else if (ret != 0) {
#if 0
	    perror("dpbf_keyval_dbcache_get");
#endif
	    goto return_error;
    }

    keyval_db_cache[i].ref_ct  = 1;
    keyval_db_cache[i].coll_id = coll_id;
    keyval_db_cache[i].handle  = handle;
    *db_pp = db_p;
    gen_mutex_unlock(&keyval_db_cache[i].mutex);
    return DBPF_KEYVAL_DBCACHE_SUCCESS;

return_error:
    gen_mutex_unlock(&keyval_db_cache[i].mutex);
    return DBPF_KEYVAL_DBCACHE_ERROR;
}

/* dbpf_keyval_dbcache_put()
 */
void dbpf_keyval_dbcache_put(TROVE_coll_id coll_id,
			     TROVE_handle handle)
{
    int i;

    for (i=0; i < DBCACHE_ENTRIES; i++) {
	gen_mutex_lock(&keyval_db_cache[i].mutex);
	if (keyval_db_cache[i].ref_ct  >= 0 &&
	    keyval_db_cache[i].coll_id == coll_id &&
	    keyval_db_cache[i].handle  == handle) break;
	else gen_mutex_unlock(&keyval_db_cache[i].mutex);
    }
    if (i == DBCACHE_ENTRIES) {
	printf("warning: no matching entry for dbcache_put op\n");
	return;
    }

    keyval_db_cache[i].ref_ct--;

#ifdef DBCACHE_DONT_CACHE
    if (keyval_db_cache[i].ref_ct == 0) {
    	int ret;

	ret = keyval_db_cache[i].db_p->close(db_p, 0);
	if (ret != 0) assert(0);

	keyval_db_cache[i].ref_ct = -1;
	keyval_db_cache[i].db_p   = NULL;
    }
#endif
    gen_mutex_unlock(&keyval_db_cache[i].mutex);
}



/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
