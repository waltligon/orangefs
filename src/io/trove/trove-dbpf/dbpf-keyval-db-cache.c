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

#include <trove.h>
#include <trove-internal.h>
#include <dbpf.h>

#include <limits.h>

enum {
    DBCACHE_ENTRIES = 16
};

#undef DBCACHE_DONT_CACHE

struct keyval_dbcache_entry {
    int ref_ct; /* -1 == not a valid cache entry */
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

/* dbpf_keyval_dbcache_get()
 *
 * create_flag - 0 = don't create if doesn't exist; non-zero = create.
 */
DB *dbpf_keyval_dbcache_get(TROVE_coll_id coll_id,
			    TROVE_handle handle,
			    int create_flag)
{
    int i, ret;
    char filename[PATH_MAX];
    DB *db_p;

    for (i=0; i < DBCACHE_ENTRIES; i++) {
	if (keyval_db_cache[i].ref_ct  >= 0 &&
	    keyval_db_cache[i].coll_id == coll_id &&
	    keyval_db_cache[i].handle  == handle) break;
    }

    if (i < DBCACHE_ENTRIES) {
	/* found cached DB */
	printf("dbcache: found cached db at index %d\n", i);
	keyval_db_cache[i].ref_ct++;
	return keyval_db_cache[i].db_p;
    }

    /* no cached db; open it */
    for (i=0; i < DBCACHE_ENTRIES; i++) {
	if (keyval_db_cache[i].ref_ct == -1) {
	    printf("dbcache: found empty entry at %d\n", i);
	    break;
	}
    }

    if (i == DBCACHE_ENTRIES) {
	/* no invalid entries; search for one that isn't in use */
	for (i=0; i < DBCACHE_ENTRIES; i++) {
	    if (keyval_db_cache[i].ref_ct == 0) {
		printf("dbcache: no empty entries; found unused entry at %d\n", i);
		
		ret = keyval_db_cache[i].db_p->close(keyval_db_cache[i].db_p, 0);
		if (ret != 0) {
		    printf("db: close error\n");
		}
		keyval_db_cache[i].ref_ct = -1;
		keyval_db_cache[i].db_p   = NULL;
		break;
	    }
	}
	if (i == DBCACHE_ENTRIES) assert(0);
    }

    snprintf(filename, PATH_MAX, "/%s/%08x/%s/%08Lx.keyval", TROVE_DIR, coll_id, KEYVAL_DIRNAME, handle);
    printf("file name = %s\n", filename);

    ret = db_create(&(keyval_db_cache[i].db_p), NULL, 0);
    if (ret != 0) {
	    fprintf(stderr, "dbpf_keyval_dbcache_get: %s\n", db_strerror(ret));
	    assert(0);
    }

    db_p = keyval_db_cache[i].db_p;
    db_p->set_errfile(db_p, stderr);
    db_p->set_errpfx(db_p, "xxx");
    /* DB_RECNUM makes it easier to iterate through every key in chunks */
    if (( ret =  db_p->set_flags(db_p, DB_RECNUM)) ) {
	    db_p->err(db_p, ret, "%s: set_flags", filename);
	    assert(0);
    }
    ret = keyval_db_cache[i].db_p->open(keyval_db_cache[i].db_p,
					filename,
					NULL,
					DB_UNKNOWN,
					0,
					0);
    if (ret == ENOENT && create_flag != 0) {
	/* if no such DB and create_flag is set, try to create the DB */
	ret = keyval_db_cache[i].db_p->open(keyval_db_cache[i].db_p,
					    filename,
					    NULL,
					    DB_BTREE,
					    DB_CREATE|DB_EXCL,
					    0644);
	if (ret != 0) assert(0);
    }
    else if (ret != 0) {
	    perror("dpbf_keyval_dbcache_get");
	    assert(0);
    }

    keyval_db_cache[i].ref_ct  = 1;
    keyval_db_cache[i].coll_id = coll_id;
    keyval_db_cache[i].handle  = handle;
    return keyval_db_cache[i].db_p;
}

/* dbpf_keyval_dbcache_put()
 */
void dbpf_keyval_dbcache_put(TROVE_coll_id coll_id,
			     TROVE_handle handle)
{
    int i, ret;

    for (i=0; i < DBCACHE_ENTRIES; i++) {
	if (keyval_db_cache[i].ref_ct  >= 0 &&
	    keyval_db_cache[i].coll_id == coll_id &&
	    keyval_db_cache[i].handle  == handle) break;
    }
    if (i == DBCACHE_ENTRIES) {
	printf("warning: no matching entry for dbcache_put op\n");
	return;
    }

    keyval_db_cache[i].ref_ct--;

#ifdef DBCACHE_DONT_CACHE
    if (keyval_db_cache[i].ref_ct == 0) {
	ret = keyval_db_cache[i].db_p->close(db_p, 0);
	if (ret != 0) assert(0);

	keyval_db_cache[i].ref_ct = -1;
	keyval_db_cache[i].db_p   = NULL;
    }
#endif
}



/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 */
