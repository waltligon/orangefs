/*
 * InfiniBand BMI method, memory allocation and caching.
 *
 * Copyright (C) 2004-6 Pete Wyckoff <pw@osc.edu>
 *
 * See COPYING in top-level directory.
 *
 * $Id: mem.c,v 1.4 2006-03-21 21:00:39 pw Exp $
 */
#include <src/common/gen-locks/gen-locks.h>
#include "ib.h"
#include "pvfs2-internal.h"

#ifdef __PVFS2_SERVER__
#  define ENABLE_MEMCACHE 1
#else
#  define ENABLE_MEMCACHE 1
#endif

list_t memcache __hidden;
static gen_mutex_t memcache_mutex = GEN_MUTEX_INITIALIZER;

#if ENABLE_MEMCACHE
/*
 * Create and link a new memcache entry.  Assumes lock already held.
 */
static memcache_entry_t *
memcache_add(void *buf, bmi_size_t len)
{
    memcache_entry_t *c;

    c = malloc(sizeof(*c));
    if (bmi_ib_likely(c)) {
	c->buf = buf;
	c->len = len;
	c->count = 0;
	qlist_add_tail(&c->list, &memcache);
    }
    return c;
}

/*
 * See if an entry exists that totally covers the request.  Assumes lock
 * already held.  These criteria apply:
 *   1. existing bounds must cover potential new one
 *   2. prefer higest refcnt (hoping for maximal reuse)
 *   3. prefer tightest bounds among matching refcnt
 */
static memcache_entry_t *
memcache_lookup_cover(const void *const buf, bmi_size_t len)
{
    list_t *l;
    const char *end = (const char *) buf + len;
    memcache_entry_t *cbest = 0;

    qlist_for_each(l, &memcache) {
	memcache_entry_t *c = qlist_entry(l, memcache_entry_t, list);
	if (!(c->buf <= buf && end <= (const char *)c->buf + c->len))
	    continue;
	if (!cbest)
	    goto take;
	if (c->count < cbest->count)
	    continue;  /* discard lower refcnt one */
	if (c->count > cbest->count)
	    goto take; /* prefer higher refcnt */
	/* equal refcnt, prefer tighter bounds */
	if (c->len < cbest->len)
	    goto take;
	continue;
      take:
	cbest = c;
    }
    return cbest;
}

/*
 * See if the exact entry exists.  There must never be more than one
 * of the same entry.  Used only for BMI_ib_memfree.
 */
static memcache_entry_t *
memcache_lookup_exact(const void *const buf, bmi_size_t len)
{
    list_t *l;

    qlist_for_each(l, &memcache) {
	memcache_entry_t *c = qlist_entry(l, memcache_entry_t, list);
	if (c->buf == buf && c->len == len)
	    return c;
    }
    return 0;
}
#endif  /* ENABLE_MEMCACHE */

/*
 * BMI malloc and free routines.  If the region is big enough, pin
 * it now to save time later in the actual send or recv routine.
 */
void *
BMI_ib_memalloc(bmi_size_t len, enum bmi_op_type send_recv __unused)
{
    void *buf;

    buf = malloc(len);
#if ENABLE_MEMCACHE
    if (bmi_ib_unlikely(!buf))
	goto out;
    if (len > EAGER_BUF_PAYLOAD) {
	memcache_entry_t *c;

	gen_mutex_lock(&memcache_mutex);
	c = memcache_lookup_cover(buf, len);  /* could be recycled buffer */
	if (c)
	    ++c->count;
	else {
	    c = memcache_add(buf, len);
	    if (bmi_ib_unlikely(!c)) {
		free(buf);
		buf = 0;
	    } else {
		ib_mem_register(c);
		++c->count;
	    }
	}
	gen_mutex_unlock(&memcache_mutex);
    }
  out:
#endif  /* ENABLE_MEMCACHE */
    return buf;
}

int
BMI_ib_memfree(void *buf, bmi_size_t len __unused,
  enum bmi_op_type send_recv __unused)
{
#if ENABLE_MEMCACHE
    memcache_entry_t *c;
    /* okay if not found, just not cached */

    gen_mutex_lock(&memcache_mutex);
    c = memcache_lookup_exact(buf, len);
    if (c) {
	debug(6, "%s: found %p len %lld", __func__, c->buf, lld(c->len));
	assert(c->count == 1, "%s: buf %p len %lld count = %d, expected 1",
	  __func__, c->buf, lld(c->len), c->count);
	ib_mem_deregister(c);
	qlist_del(&c->list);
	free(c);
    }
    gen_mutex_unlock(&memcache_mutex);
#endif
    free(buf);
    return 0;
}

/*
 * Interface for bmi_ib send and recv routines in ib.c.  Takes a buflist
 * and looks up each entry in the memcache, adding it if not yet pinned.
 */
void
memcache_register(ib_buflist_t *buflist)
{
    int i;

    buflist->memcache = Malloc(buflist->num * sizeof(*buflist->memcache));
    gen_mutex_lock(&memcache_mutex);
    for (i=0; i<buflist->num; i++) {
#if ENABLE_MEMCACHE
	memcache_entry_t *c;
	c = memcache_lookup_cover(buflist->buf.send[i], buflist->len[i]);
	if (c) {
	    ++c->count;
	    debug(2, "%s: hit [%d] %p len %lld (via %p len %lld) refcnt now %d",
	      __func__, i, buflist->buf.send[i], lld(buflist->len[i]), c->buf,
	      lld(c->len), c->count);
	} else {
	    debug(2, "%s: miss [%d] %p len %lld", __func__, i,
	      buflist->buf.send[i], lld(buflist->len[i]));
	    c = memcache_add(buflist->buf.recv[i], buflist->len[i]);
	    if (!c)
		error("%s: no memory for cache entry", __func__);
	    c->count = 1;
	    ib_mem_register(c);
	}
	buflist->memcache[i] = c;
#else
	memcache_entry_t cp = Malloc(sizeof(*cp));
	cp->buf = buflist->buf.recv[i];
	cp->len = buflist->len[i];
	cp->type = type;
	ib_mem_register(cp);
	buflist->memcache[i] = cp;
#endif
    }
    gen_mutex_unlock(&memcache_mutex);
}

void
memcache_deregister(ib_buflist_t *buflist)
{
    int i;

    gen_mutex_lock(&memcache_mutex);
    for (i=0; i<buflist->num; i++) {
#if ENABLE_MEMCACHE
	memcache_entry_t *c = buflist->memcache[i];
	--c->count;
	debug(2, "%s: dec refcount [%d] %p len %lld count now %d", __func__, i,
	  buflist->buf.send[i], lld(buflist->len[i]), c->count);
	/* let garbage collection do ib_mem_deregister(c) for refcnt==0 */
#else
	ib_mem_deregister(buflist->memcache[i]);
	free(buflist->memcache[i]);
#endif
    }
    free(buflist->memcache);
    gen_mutex_unlock(&memcache_mutex);
}

/*
 * Remove all mappings in preparation for closing the pd.
 */
void
memcache_shutdown(void)
{
    list_t *l, *lp;

    gen_mutex_lock(&memcache_mutex);
    qlist_for_each_safe(l, lp, &memcache) {
	memcache_entry_t *c = qlist_entry(l, memcache_entry_t, list);
	ib_mem_deregister(c);
	qlist_del(&c->list);
	free(c);
    }
    gen_mutex_unlock(&memcache_mutex);
}

