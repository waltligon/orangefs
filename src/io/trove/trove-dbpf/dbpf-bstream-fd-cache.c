/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* NOTE: Prototypes etc. in dbpf-bstream.h */

/* Notes on locking:
 *
 * Right now we implement locks on fdcache entries by a mutex for each entry.
 * There is no overall "lock" on the entire cache.  This has at least one
 * important implication: THE INITIALIZE AND FINALIZE FUNCTIONS MUST BE CALLED
 * BY A SINGLE THREAD ONLY AND CALLED BEFORE/AFTER ALL OTHER THREADS ARE
 * STARTED/FINISHED.
 *
 * NOTE: THIS STUFF IS RELATIVELY BROKEN FOR THE MULTITHREADED CASE.  SHOULDN'T
 * BE USED FOR THAT AT THIS TIME!!!
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <limits.h>

#include "trove.h"
#include "trove-internal.h"
#include "dbpf.h"
#include "dbpf-bstream.h"

enum {
    FDCACHE_ENTRIES = 16
};

#undef FDCACHE_DONT_CACHE

struct bstream_fdcache_entry {
    int valid; /* I was thinking I wanted a separate valid field (rather than ref_ct == -1), but not so sure now. -- rob */
    int ref_ct;
    gen_mutex_t mutex;
    TROVE_coll_id coll_id;
    TROVE_handle handle;
    int fd;
};

static struct bstream_fdcache_entry bstream_fd_cache[FDCACHE_ENTRIES];

void dbpf_bstream_fdcache_initialize(void)
{
    int i;

    for (i=0; i < FDCACHE_ENTRIES; i++) {
	bstream_fd_cache[i].valid = 0;
	gen_mutex_init(&bstream_fd_cache[i].mutex);
	bstream_fd_cache[i].fd = -1;
    }
}

void dbpf_bstream_fdcache_finalize(void)
{
    int i;

    for (i=0; i < FDCACHE_ENTRIES; i++) {
	if (bstream_fd_cache[i].ref_ct > 0) {
	    printf("warning: ref_ct = %d on handle %Lx in fdcache\n",
		   bstream_fd_cache[i].ref_ct,
		   bstream_fd_cache[i].handle);
	}

	/* warning or no, close the FD */
	if (bstream_fd_cache[i].valid) DBPF_CLOSE(bstream_fd_cache[i].fd);
    }
}

/* dbpf_bstream_fdcache_try_remove()
 *
 * Returns one of DBPF_BSTREAM_FDCACHE_ERROR, DBPF_BSTREAM_FDCACHE_BUSY,
 * DBPF_BSTREAM_FDCACHE_SUCCESS.
 *
 * This function attempts to remove any associated file holding the bstream
 * for a given {coll_id, handle} pair.  This file not existing to begin with
 * is considered a success.
 *
 * NOTE: ASSUMING SINGLE THREAD FOR NOW, SINCE THIS IS ALL BROKEN FOR MULTI-
 * THREADED CASE ANYWAY!!!
 */
int dbpf_bstream_fdcache_try_remove(TROVE_coll_id coll_id,
				    TROVE_handle handle)
{
    int i, ret, fd;
    char filename[PATH_MAX];

    /* NOTE: NEED TO DO SOMETHING HERE TO ENSURE THAT NOTHING IS ADDED TO 
     * THE CACHE WHILE WE ARE WORKING!!!
     */

    /* look to see if we already have an FD */
    for (i=0; i < FDCACHE_ENTRIES; i++) {
	if (!(ret = gen_mutex_trylock(&bstream_fd_cache[i].mutex)) &&
	    bstream_fd_cache[i].valid && 
	    bstream_fd_cache[i].coll_id == coll_id &&
	    bstream_fd_cache[i].handle  == handle) break;
	else if (ret == 0) gen_mutex_unlock(&bstream_fd_cache[i].mutex);
    }
    /* NOTE: in multithreaded case we could have just skipped over the entry we want. */

    if (i < FDCACHE_ENTRIES) {
	/* found cached FD, and have the lock */

	if (bstream_fd_cache[i].ref_ct > 0) {
	    gen_mutex_unlock(&bstream_fd_cache[i].mutex);
	    return DBPF_BSTREAM_FDCACHE_BUSY;
	}

	DBPF_CLOSE(bstream_fd_cache[i].fd);
	bstream_fd_cache[i].fd    = -1;
	bstream_fd_cache[i].valid = 0;
	gen_mutex_unlock(&bstream_fd_cache[i].mutex);
    }

    /* TODO: create the name of the bstream file more correctly */
    snprintf(filename, PATH_MAX, "/%s/%08x/%s/%08Lx.bstream", 
		    TROVE_DIR, coll_id, BSTREAM_DIRNAME, handle);
#if 0
    printf("file name = %s\n", filename);
#endif

    ret = DBPF_UNLINK(filename);
    if (ret != 0 && errno != ENOENT) return DBPF_BSTREAM_FDCACHE_ERROR;

    /* TODO: UNDO WHATEVER WE DID TO KEEP THINGS FROM BEING ADDED */
    return DBPF_BSTREAM_FDCACHE_SUCCESS;
}

/* dbpf_bstream_fdcache_get()
 *
 * Right now we don't place any kind of upper limit on the number of
 * references to the same fd, so this will never return BUSY.  That might
 * change at some later time.
 *
 * Returns one of DBPF_BSTREAM_FDCACHE_ERROR, DBPF_BSTREAM_FDCACHE_BUSY,
 * DBPF_BSTREAM_FDCACHE_SUCCESS.
 *
 * NOTE: THIS IS BROKEN; TWO THREADS TRYING TO OPEN THE SAME (NONEXISTANT)
 * BSTREAM COULD END UP WITH TWO SEPARATE ENTRIES IN THE CACHE...
 *
 * NOTE: EVEN MORE BROKEN; WE SKIP OVER ENTRIES THAT MIGHT HAVE BEEN MATCHES
 * IF WE CAN'T GET A LOCK ON THEM...
 *
 * Passes back fd in fd_p.
 */
int dbpf_bstream_fdcache_try_get(TROVE_coll_id coll_id,
				 TROVE_handle handle,
				 int create_flag,
				 int *fd_p)
{
    int i, ret, fd;
    char filename[PATH_MAX];


    /* look to see if we already have an FD */
    for (i=0; i < FDCACHE_ENTRIES; i++) {
	if (!(ret = gen_mutex_trylock(&bstream_fd_cache[i].mutex)) &&
	    bstream_fd_cache[i].valid && 
	    bstream_fd_cache[i].coll_id == coll_id &&
	    bstream_fd_cache[i].handle  == handle) break;
	else if (ret == 0) gen_mutex_unlock(&bstream_fd_cache[i].mutex);
    }

    /* NOTE: we're going to use a mutex here when all we really need is
     * an atomic increment.  We should grab that code from MPICH2 some
     * time or something...
     */

    if (i < FDCACHE_ENTRIES) {
	/* found cached FD, and have the lock */
#if 0
	printf("fdcache: found cached fd at index %d\n", i);
#endif
	bstream_fd_cache[i].ref_ct++;
	*fd_p = bstream_fd_cache[i].fd;
	gen_mutex_unlock(&bstream_fd_cache[i].mutex);
	return DBPF_BSTREAM_FDCACHE_SUCCESS;
    }
    
    /* no cached FD; open the file and cache the FD */
    for (i=0; i < FDCACHE_ENTRIES; i++) {
	if (!(ret = gen_mutex_trylock(&bstream_fd_cache[i].mutex)) && 
	    !bstream_fd_cache[i].valid) 
	{
#if 0
	    printf("fdcache: found empty entry at %d\n", i);
#endif
	    break;
	}
	else if (ret == 0) gen_mutex_unlock(&bstream_fd_cache[i].mutex);
    }
    if (i == FDCACHE_ENTRIES) {
	/* no invalid entries; search for one that isn't in use */
	for (i=0; i< FDCACHE_ENTRIES; i++) {
	    if (!(ret = gen_mutex_trylock(&bstream_fd_cache[i].mutex)) &&
		bstream_fd_cache[i].ref_ct == 0)
	    {
#if 0
		printf("fdcache: no empty entries; found unused entry at %d\n", i);
#endif
		DBPF_CLOSE(bstream_fd_cache[i].fd);
		bstream_fd_cache[i].valid = 0;
		bstream_fd_cache[i].fd    = -1;
		break;
	    }
	    else if (ret == 0) gen_mutex_unlock(&bstream_fd_cache[i].mutex);
	}
	if (i == FDCACHE_ENTRIES) assert(0);
    }

    /* have a lock on an entry */

    /* TODO: create the name of the bstream file more correctly */
    snprintf(filename, PATH_MAX, "/%s/%08x/%s/%08Lx.bstream", 
		    TROVE_DIR, coll_id, BSTREAM_DIRNAME, handle);
#if 0
    printf("file name = %s\n", filename);
#endif
    
    /* note: we don't really need the coll_id for this operation,
     * but it doesn't really hurt anything...
     */
    
    fd = DBPF_OPEN(filename, O_RDWR, 0);
    if (fd < 0 && errno == ENOENT && create_flag) {
#if 0
	printf("creating new dataspace\n");
#endif
	if ((fd = DBPF_OPEN(filename, O_RDWR|O_CREAT|O_EXCL, 0644)) < 0)
	{
	    printf("error trying to create!\n");
	    goto return_error;
	}
    }
    else if (fd < 0) {
	goto return_error;
    }

    /* cache the FD */
    bstream_fd_cache[i].valid   = 1;
    bstream_fd_cache[i].ref_ct  = 1;
    bstream_fd_cache[i].coll_id = coll_id;
    bstream_fd_cache[i].handle  = handle;
    bstream_fd_cache[i].fd      = fd;
    *fd_p = fd;
    gen_mutex_unlock(&bstream_fd_cache[i].mutex);
    return DBPF_BSTREAM_FDCACHE_SUCCESS;

 return_error:
    gen_mutex_unlock(&bstream_fd_cache[i].mutex);
    return DBPF_BSTREAM_FDCACHE_ERROR;
}

/* dbpf_bstream_fdcache_put()
 *
 * I think that it's more convenient to reference based on [coll_id, handle]
 * than on FD?
 */
void dbpf_bstream_fdcache_put(TROVE_coll_id coll_id,
			      TROVE_handle handle)
{
    int i;

    for (i=0; i < FDCACHE_ENTRIES; i++) {
	/* NOTE: sure would be nice if we could atomically read these... */
	gen_mutex_lock(&bstream_fd_cache[i].mutex);
	if (bstream_fd_cache[i].valid &&
	    bstream_fd_cache[i].coll_id == coll_id &&
	    bstream_fd_cache[i].handle  == handle) break;
	else gen_mutex_unlock(&bstream_fd_cache[i].mutex);
    }
    if (i == FDCACHE_ENTRIES) {
	printf("warning: no matching entry for fdcache_put op\n");
	return;
    }

    bstream_fd_cache[i].ref_ct--;

#ifdef FDCACHE_DONT_CACHE
    if (bstream_fd_cache[i].ref_ct == 0) {
	DBPF_CLOSE(bstream_fd_cache[i].fd);
	bstream_fd_cache[i].valid  = 0;
	bstream_fd_cache[i].ref_ct = -1;
	bstream_fd_cache[i].fd     = -1;
    }
#endif
    gen_mutex_unlock(&bstream_fd_cache[i].mutex);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
