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

#include <trove.h>
#include <trove-internal.h>
#include <dbpf.h>

#include <limits.h>

#define DBPF_OPEN open
#define DBPF_WRITE write
#define DBPF_LSEEK lseek
#define DBPF_READ read
#define DBPF_CLOSE close

enum {
    FDCACHE_ENTRIES = 16
};

#undef FDCACHE_DONT_CACHE

struct bstream_fdcache_entry {
    int ref_ct; /* -1 == not a valid cache entry */
    TROVE_coll_id coll_id;
    TROVE_handle handle;
    int fd;
};

static struct bstream_fdcache_entry bstream_fd_cache[FDCACHE_ENTRIES];

void dbpf_bstream_fdcache_initialize(void)
{
    int i;
    for (i=0; i < FDCACHE_ENTRIES; i++) {
	bstream_fd_cache[i].ref_ct = -1;
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
	if (bstream_fd_cache[i].ref_ct >= 0) DBPF_CLOSE(bstream_fd_cache[i].fd);
    }
}

/* dbpf_bstream_fdcache_get()
 */
int dbpf_bstream_fdcache_get(TROVE_coll_id coll_id,
			     TROVE_handle handle,
			     int create_flag)
{
    int i, fd;
    char filename[PATH_MAX];

    /* look to see if we already have an FD */
    for (i=0; i < FDCACHE_ENTRIES; i++) {
	if (bstream_fd_cache[i].ref_ct  >= 0 && 
	    bstream_fd_cache[i].coll_id == coll_id &&
	    bstream_fd_cache[i].handle  == handle) break;
    }

    if (i < FDCACHE_ENTRIES) {
	/* found cached FD */
	printf("fdcache: found cached fd at index %d\n", i);
	bstream_fd_cache[i].ref_ct++;
	return bstream_fd_cache[i].fd;
    }
    
    /* no cached FD; open the file and cache the FD */
    for (i=0; i < FDCACHE_ENTRIES; i++) {
	if (bstream_fd_cache[i].ref_ct == -1) {
	    printf("fdcache: found empty entry at %d\n", i);
	    break;
	}
    }
    if (i == FDCACHE_ENTRIES) {
	/* no invalid entries; search for one that isn't in use */
	for (i=0; i< FDCACHE_ENTRIES; i++) {
	    if (bstream_fd_cache[i].ref_ct == 0) {
		printf("fdcache: no empty entries; found unused entry at %d\n", i);
		DBPF_CLOSE(bstream_fd_cache[i].fd);
		bstream_fd_cache[i].ref_ct = -1;
		bstream_fd_cache[i].fd     = -1;
		break;
	    }
	}
	if (i == FDCACHE_ENTRIES) assert(0);
    }

    /* TODO: create the name of the bstream file more correctly */
    snprintf(filename, PATH_MAX, "/%s/%08x/%s/%08Lx.bstream", 
		    TROVE_DIR, coll_id, BSTREAM_DIRNAME, handle);
    printf("file name = %s\n", filename);
    
    /* note: we don't really need the coll_id for this operation,
     * but it doesn't really hurt anything...
     */
    
    fd = DBPF_OPEN(filename, O_RDWR, 0);
    if (fd < 0 && errno == ENOENT && create_flag) {
	printf("creating new dataspace\n");
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
    bstream_fd_cache[i].ref_ct  = 1;
    bstream_fd_cache[i].coll_id = coll_id;
    bstream_fd_cache[i].handle  = handle;
    bstream_fd_cache[i].fd      = fd;
    return fd;

 return_error:
    assert(0);
    return -1;
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
	if (bstream_fd_cache[i].ref_ct  >= 0 &&
	    bstream_fd_cache[i].coll_id == coll_id &&
	    bstream_fd_cache[i].handle  == handle) break;
    }
    if (i == FDCACHE_ENTRIES) {
	printf("warning: no matching entry for fdcache_put op\n");
	return;
    }

    bstream_fd_cache[i].ref_ct--;

#ifdef FDCACHE_DONT_CACHE
    if (bstream_fd_cache[i].ref_ct == 0) {
	DBPF_CLOSE(bstream_fd_cache[i].fd);
	bstream_fd_cache[i].ref_ct = -1;
	bstream_fd_cache[i].fd     = -1;
    }
#endif
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 */






