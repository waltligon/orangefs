/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup pvfs2linux
 *
 *  Implementation of dentry (directory cache) functions.
 *
 *  The combination of d_revalidate, d_compare and d_delete will
 *  cause each dentry allocated in the kernel to be a one-shot.
 *
 *  This removes the need to keep the PVFS caches and the kernel
 *  cache in sync.
 */

#include "pvfs2-kernel.h"
#include "pvfs2-internal.h"

/* should return 1 if dentry can still be trusted, else 0 */
static int pvfs2_d_revalidate_common(struct dentry* dentry)
{
    gossip_debug(GOSSIP_DCACHE_DEBUG, "pvfs2_d_revalidate_common: invalidating dentry: %p\n", dentry);

    return 0;
}

/* should return 1 if dentry can still be trusted, else 0 */
#ifdef PVFS2_LINUX_KERNEL_2_4
static int pvfs2_d_revalidate(
    struct dentry *dentry,
    int flags)
{
    return(pvfs2_d_revalidate_common(dentry));
}

#else

/** Verify that dentry is valid.
 */
static int pvfs2_d_revalidate(
    struct dentry *dentry,
    struct nameidata *nd)
{

    if (nd && (nd->flags & LOOKUP_FOLLOW) &&
        (!nd->flags & LOOKUP_CREATE))
    {
        gossip_debug(GOSSIP_DCACHE_DEBUG, "\npvfs2_d_revalidate: Trusting intent; "
                    "skipping getattr\n");
        return 1;
    }
    return(pvfs2_d_revalidate_common(dentry));
}

#endif /* PVFS2_LINUX_KERNEL_2_4 */

/*
  to propagate an error, return a value < 0, as this causes
  link_path_walk to pass our error up
*/
static int pvfs2_d_hash(
    struct dentry *parent,
    struct qstr *hash)
{
/*     gossip_debug(GOSSIP_DCACHE_DEBUG, "pvfs2: pvfs2_d_hash called " */
/*                 "(name: %s | len: %d | hash: %d)\n", */
/*                 hash->name, hash->len, hash->hash); */
    return 0;
}

static int pvfs2_d_compare(
    struct dentry *parent,
    struct qstr *d_name,
    struct qstr *name)
{
    gossip_debug(GOSSIP_DCACHE_DEBUG, "pvfs2_d_compare: called on parent %p\n  (name1: %s| "
                "name2: %s)\n", parent, d_name->name, name->name);
    /* force a cache miss every time */
    return 1;
}

static int pvfs2_d_delete (struct dentry * dentry)
{
    return 1;
}

/** PVFS2 implementation of VFS dentry operations */
struct dentry_operations pvfs2_dentry_operations =
{
    .d_revalidate = pvfs2_d_revalidate,
    .d_hash = pvfs2_d_hash,
    .d_compare = pvfs2_d_compare,
    .d_delete = pvfs2_d_delete,
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
