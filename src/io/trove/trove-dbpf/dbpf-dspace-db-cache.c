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
    DBCACHE_ENTRIES = 2
};

#undef DBCACHE_DONT_CACHE

struct dspace_dbcache_entry {
    int ref_ct; /* -1 == not a valid cache entry */
    TROVE_coll_id coll_id;
    DB *db_p;
};

static struct dspace_dbcache_entry dspace_db_cache[DBCACHE_ENTRIES];

/* dbpf_dspace_dbcache_initialize()
 */
void dbpf_dspace_dbcache_initialize(void)
{
    int i;
    for (i=0; i < DBCACHE_ENTRIES; i++) {
	dspace_db_cache[i].ref_ct = -1;
	dspace_db_cache[i].db_p   = NULL;
    }
}

/* dbpf_dspace_dbcache_finalize()
 */
void dbpf_dspace_dbcache_finalize(void)
{
    int i, ret;

    for (i=0; i < DBCACHE_ENTRIES; i++) {
	if (dspace_db_cache[i].ref_ct > 0) {
	    printf("warning: ref_ct = %d on coll_id %Lx in dspace dbcache\n",
		   dspace_db_cache[i].ref_ct,
		   (int64_t) dspace_db_cache[i].coll_id);
	}
	if (dspace_db_cache[i].ref_ct >= 0) {
	    /* close DB */
	    ret = dspace_db_cache[i].db_p->close(dspace_db_cache[i].db_p, 0);
	    if (ret != 0) assert(0);
	}
    }
}

/* dbpf_dspace_dbcache_get()
 *
 * create_flag - 0 = don't create if doesn't exist; non-zero = create.
 */
DB *dbpf_dspace_dbcache_get(TROVE_coll_id coll_id,
			    int create_flag)
{
    int i, ret;
    char filename[PATH_MAX];
    DB *db_p;

    for (i=0; i < DBCACHE_ENTRIES; i++) {
	if (dspace_db_cache[i].ref_ct  >= 0 &&
	    dspace_db_cache[i].coll_id == coll_id) break;
    }

    if (i < DBCACHE_ENTRIES) {
	/* found cached DB */
	printf("dspace dbcache: found cached db at index %d\n", i);
	dspace_db_cache[i].ref_ct++;
	return dspace_db_cache[i].db_p;
    }

    /* no cached db; open it */
    for (i=0; i < DBCACHE_ENTRIES; i++) {
	if (dspace_db_cache[i].ref_ct == -1) {
	    printf("dspace dbcache: found empty entry at %d\n", i);
	    break;
	}
    }

    if (i == DBCACHE_ENTRIES) {
	/* no invalid entries; search for one that isn't in use */
	for (i=0; i < DBCACHE_ENTRIES; i++) {
	    if (dspace_db_cache[i].ref_ct == 0) {
		printf("dspace dbcache: no empty entries; found unused entry at %d\n", i);
		
		ret = dspace_db_cache[i].db_p->close(dspace_db_cache[i].db_p, 0);
		if (ret != 0) {
		    printf("dspace db: close error\n");
		}
		dspace_db_cache[i].ref_ct = -1;
		dspace_db_cache[i].db_p   = NULL;
		break;
	    }
	}
	if (i == DBCACHE_ENTRIES) assert(0);
    }

    snprintf(filename, PATH_MAX, "/%s/%08x/%s", TROVE_DIR, coll_id, DS_ATTRIB_DBNAME);
    printf("file name = %s\n", filename);

    ret = db_create(&(dspace_db_cache[i].db_p), NULL, 0);
    if (ret != 0) {
	    fprintf(stderr, "dbpf_dspace_dbcache_get: %s\n", db_strerror(ret));
	    assert(0);
    }

    db_p = dspace_db_cache[i].db_p;
    db_p->set_errfile(db_p, stderr);
    db_p->set_errpfx(db_p, "xxx");
    /* DB_RECNUM makes it easier to iterate through every key in chunks */
    if (( ret =  db_p->set_flags(db_p, DB_RECNUM)) ) {
	    db_p->err(db_p, ret, "%s: set_flags", filename);
	    assert(0);
    }
    ret = dspace_db_cache[i].db_p->open(dspace_db_cache[i].db_p,
					filename,
					NULL,
					DB_UNKNOWN,
					0,
					0);
    if (ret == ENOENT && create_flag != 0) {
	/* if no such DB and create_flag is set, try to create the DB */
	ret = dspace_db_cache[i].db_p->open(dspace_db_cache[i].db_p,
					    filename,
					    NULL,
					    DB_BTREE,
					    DB_CREATE|DB_EXCL,
					    0644);
	if (ret != 0) assert(0);
    }
    else if (ret != 0) {
	    perror("dpbf_dspace_dbcache_get");
	    assert(0);
    }

    dspace_db_cache[i].ref_ct  = 1;
    dspace_db_cache[i].coll_id = coll_id;
    return dspace_db_cache[i].db_p;
}

/* dbpf_dspace_dbcache_put()
 */
void dbpf_dspace_dbcache_put(TROVE_coll_id coll_id)
{
    int i;

    for (i=0; i < DBCACHE_ENTRIES; i++) {
	if (dspace_db_cache[i].ref_ct  >= 0 &&
	    dspace_db_cache[i].coll_id == coll_id) break;
    }
    if (i == DBCACHE_ENTRIES) {
	printf("warning: no matching entry for dspace dbcache_put op\n");
	return;
    }

    dspace_db_cache[i].ref_ct--;

#ifdef DBCACHE_DONT_CACHE
    if (dspace_db_cache[i].ref_ct == 0) {
	int ret;

	ret = dspace_db_cache[i].db_p->close(db_p, 0);
	if (ret != 0) assert(0);

	dspace_db_cache[i].ref_ct = -1;
	dspace_db_cache[i].db_p   = NULL;
    }
#endif
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 */
