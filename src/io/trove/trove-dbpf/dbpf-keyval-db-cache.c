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

#include "gossip.h"
#include "trove.h"
#include "trove-internal.h"
#include "dbpf.h"
#include "dbpf-keyval.h"

enum
{
    DBCACHE_ENTRIES = 16
};

struct keyval_dbcache_entry
{
    int ref_ct; /* -1 == not a valid cache entry */
    gen_mutex_t mutex;
    TROVE_coll_id coll_id;
    TROVE_handle handle;
    DB *db_p;
};

static struct keyval_dbcache_entry keyval_db_cache[DBCACHE_ENTRIES];

void dbpf_keyval_dbcache_initialize(void)
{
    int i;

    for (i = 0; i < DBCACHE_ENTRIES; i++)
    {
	gen_mutex_init(&keyval_db_cache[i].mutex);
	keyval_db_cache[i].ref_ct = -1;
	keyval_db_cache[i].db_p   = NULL;
    }
}

void dbpf_keyval_dbcache_finalize(void)
{
    int i, ret;

    for (i = 0; i < DBCACHE_ENTRIES; i++)
    {
	if (keyval_db_cache[i].ref_ct > 0)
        {
	    gossip_debug(GOSSIP_TROVE_DEBUG, "warning: ref_ct = %d "
                         "on handle %Lu in dbcache\n",
                         keyval_db_cache[i].ref_ct,
                         Lu(keyval_db_cache[i].handle));
	}
	if (keyval_db_cache[i].ref_ct >= 0)
        {
	    ret = keyval_db_cache[i].db_p->close(
                keyval_db_cache[i].db_p, 0);
            assert(ret == 0);
	}
    }
}

/* dbpf_keyval_dbcache_try_remove()
 *
 * Returns 0 on success, or one of -TROVE_EBUSY, -TROVE_ENOENT, or
 * -TROVE_EPERM.
 *
 */
int dbpf_keyval_dbcache_try_remove(TROVE_coll_id coll_id,
				   TROVE_handle handle)
{
    int i = 0, ret = -TROVE_EINVAL, keyval_cached = 0;
    char filename[PATH_MAX] = {0};
    DB *db_p = NULL;

    for (i = 0; i < DBCACHE_ENTRIES; i++)
    {
	if (!(ret = gen_mutex_trylock(&keyval_db_cache[i].mutex)) &&
	    (keyval_db_cache[i].ref_ct >= 0) &&
             (keyval_db_cache[i].coll_id == coll_id) &&
              (keyval_db_cache[i].handle == handle))
        {
            break;
        }
	else if (ret == 0)
        {
            gen_mutex_unlock(&keyval_db_cache[i].mutex);
        }
    }

    if (i < DBCACHE_ENTRIES)
    {
	/* found cached DB */
	if (keyval_db_cache[i].ref_ct > 0)
        {
	    gen_mutex_unlock(&keyval_db_cache[i].mutex);
	    return -TROVE_EBUSY;
	}

	if (keyval_db_cache[i].db_p->close(
                keyval_db_cache[i].db_p, 0) != 0)
        {
	    gossip_debug(GOSSIP_TROVE_DEBUG, "db: close error\n");
	}
        keyval_cached = 1;
    }

    DBPF_GET_KEYVAL_DBNAME(filename, PATH_MAX, my_storage_p->name,
                           coll_id, handle);

    ret = db_create(&db_p, NULL, 0);
    assert(ret == 0);

    ret = db_p->remove(db_p, filename, NULL, 0);
    switch (ret)
    {
        case 0:
            break;
        case EINVAL:
            gossip_err("warning: invalid db file!\n");
            ret = -TROVE_EINVAL;
            break;
        case ENOENT:
            ret = -TROVE_ENOENT;
            break;
        default:
            gossip_err("warning: unreliable error value %d\n", ret);
            ret = -TROVE_EPERM;
            break;
    }

    if (keyval_cached)
    {
        keyval_db_cache[i].ref_ct = -1;
        keyval_db_cache[i].db_p = NULL;
        gen_mutex_unlock(&keyval_db_cache[i].mutex);
    }
    return ret;
}

/* dbpf_keyval_dbcache_try_get()
 *
 * Right now we don't place any kind of upper limit on the number of
 * references to the same db, so this will never return BUSY.  That
 * might change at some later time.
 *
 * Returns 0 on success, -TROVE_errno on failure.
 *
 * create_flag - 0 = don't create if doesn't exist; non-zero = create.
 */
int dbpf_keyval_dbcache_try_get(TROVE_coll_id coll_id,
				TROVE_handle handle,
				int create_flag,
				DB **db_pp)
{
    int i = 0, ret = -TROVE_EINVAL, error = 0;
    char filename[PATH_MAX] = {0};
    DB *db_p = NULL;
    int got_db = 0;

    for (i = 0; i < DBCACHE_ENTRIES; i++)
    {
	if (!(ret = gen_mutex_trylock(&keyval_db_cache[i].mutex)) &&
	    (keyval_db_cache[i].ref_ct >= 0) &&
            (keyval_db_cache[i].coll_id == coll_id) &&
            (keyval_db_cache[i].handle == handle))
        {
            break;
        }
	else if (ret == 0)
        {
            gen_mutex_unlock(&keyval_db_cache[i].mutex);
        }
    }

    if (i < DBCACHE_ENTRIES)
    {
	/* found cached DB */
	keyval_db_cache[i].ref_ct++;
	*db_pp = keyval_db_cache[i].db_p;
	gen_mutex_unlock(&keyval_db_cache[i].mutex);
	return 0;
    }

    /* no cached db; open it */
    for (i = 0; i < DBCACHE_ENTRIES; i++)
    {
	if (!(ret = gen_mutex_trylock(&keyval_db_cache[i].mutex)) &&
	    (keyval_db_cache[i].ref_ct == -1))
	{
	    break;
	}
	else if (ret == 0)
        {
            gen_mutex_unlock(&keyval_db_cache[i].mutex);
        }
    }

    if (i == DBCACHE_ENTRIES)
    {
	/* no invalid entries; search for one that isn't in use */
	for (i = 0; i < DBCACHE_ENTRIES; i++)
        {
	    if (!(ret = gen_mutex_trylock(&keyval_db_cache[i].mutex)) &&
		(keyval_db_cache[i].ref_ct == 0))
	    {
                ret = keyval_db_cache[i].db_p->close(
                    keyval_db_cache[i].db_p, 0);
		if (ret != 0)
                {
		    gossip_debug(GOSSIP_TROVE_DEBUG, "db: close error\n");
		}
		keyval_db_cache[i].ref_ct = -1;
		keyval_db_cache[i].db_p   = NULL;
		break;
	    }
	    else if (ret == 0)
            {
                gen_mutex_unlock(&keyval_db_cache[i].mutex);
            }
	}
	assert(i != DBCACHE_ENTRIES);
    }

    DBPF_GET_KEYVAL_DBNAME(filename, PATH_MAX, my_storage_p->name,
                           coll_id, handle);

    ret = db_create(&(keyval_db_cache[i].db_p), NULL, 0);
    if (ret != 0)
    {
        gossip_lerr("dbpf_keyval_dbcache_get: %s\n", db_strerror(ret));
        error = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }
    else
    {
	got_db = 1;
    }

    db_p = keyval_db_cache[i].db_p;
    db_p->set_errpfx(db_p, "pvfs2");
    db_p->set_errcall(db_p, dbpf_error_report);

    /* DB_RECNUM makes it easier to iterate through every key in chunks */
    if ((ret = db_p->set_flags(db_p, DB_RECNUM)))
    {
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
		     TROVE_DB_OPEN_FLAGS,
		     0);

    if ((ret == ENOENT) && (create_flag != 0))
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "About to create new DB "
                     "file %s ... ", filename);
	ret = db_p->open(db_p,
#ifdef HAVE_TXNID_PARAMETER_TO_DB_OPEN
                         NULL,
#endif
			 filename,
                         NULL,
			 TROVE_DB_TYPE,
			 TROVE_DB_CREATE_FLAGS,
			 TROVE_DB_MODE);

        gossip_debug(GOSSIP_TROVE_DEBUG, "done\n");

        /* this can easily happen if the server is out of disk space */
        if (ret)
        {
	    error = -dbpf_db_error_to_trove_error(ret);
	    goto return_error;
        }
    }
    else if (ret == ENOENT)
    {
	error = -TROVE_ENOENT;
	goto failed_open_error;
    }
    else if (ret != 0)
    {
        error = -dbpf_db_error_to_trove_error(ret);
        goto return_error;
    }

    keyval_db_cache[i].ref_ct  = 1;
    keyval_db_cache[i].coll_id = coll_id;
    keyval_db_cache[i].handle  = handle;
    *db_pp = db_p;
    gen_mutex_unlock(&keyval_db_cache[i].mutex);
    return 0;

failed_open_error:
    /* db_create allocates memory -- even if db->open fails -- which
     * can only be freed with db->close */
    if (got_db && (keyval_db_cache[i].db_p != NULL))
    {
	keyval_db_cache[i].db_p->close(keyval_db_cache[i].db_p, 0);
	keyval_db_cache[i].ref_ct = -1;
	keyval_db_cache[i].db_p = NULL;
    }

return_error:
    gen_mutex_unlock(&keyval_db_cache[i].mutex);
    return error;
}

void dbpf_keyval_dbcache_put(TROVE_coll_id coll_id, TROVE_handle handle)
{
    int i = 0;

    for (i = 0; i < DBCACHE_ENTRIES; i++)
    {
	gen_mutex_lock(&keyval_db_cache[i].mutex);
	if ((keyval_db_cache[i].ref_ct >= 0) &&
	    (keyval_db_cache[i].coll_id == coll_id) &&
	    (keyval_db_cache[i].handle  == handle))
        {
            break;
        }
	else
        {
            gen_mutex_unlock(&keyval_db_cache[i].mutex);
        }
    }
    if (i == DBCACHE_ENTRIES)
    {
	gossip_debug(GOSSIP_TROVE_DEBUG, "warning: no matching entry "
                     "for dbcache_put op\n");
 	return;
    }

    keyval_db_cache[i].ref_ct--;

#ifdef DBCACHE_DONT_CACHE
    if (keyval_db_cache[i].ref_ct == 0)
    {
    	int ret = keyval_db_cache[i].db_p->close(db_p, 0);
        assert(ret == 0);

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
