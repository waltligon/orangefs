/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup pvfs2linux
 *
 *  Implementation of dentry (directory cache) functions.
 */

#include "pvfs2-kernel.h"

/* should return 1 if dentry can still be trusted, else 0 */
#ifdef PVFS2_LINUX_KERNEL_2_4
int pvfs2_d_revalidate(
    struct dentry *dentry,
    int flags)
{
    int ret = 0;
    struct inode *inode = (dentry ? dentry->d_inode : NULL);

    pvfs2_print("pvfs2_d_revalidate: called on dentry %p", dentry);
    if (inode)
    {
        pvfs2_print(" (inode %Lu)\n",
                    Lu(pvfs2_ino_to_handle(inode->i_ino)));
        ret = pvfs2_internal_revalidate(inode);
    }
    else
    {
        pvfs2_print("\n");
    }
    return ret;
}

#else

/** Verify that dentry is valid.
 */
int pvfs2_d_revalidate(
    struct dentry *dentry,
    struct nameidata *nd)
{
    int ret = 0;
    struct inode *inode = (dentry ? dentry->d_inode : NULL);

    pvfs2_print("pvfs2_d_revalidate: called on dentry %p", dentry);
    if (nd && (nd->flags & LOOKUP_FOLLOW) &&
        (!nd->flags & LOOKUP_CREATE))
    {
        pvfs2_print("\npvfs2_d_revalidate: Trusting intent; "
                    "skipping getattr\n");
        ret = 1;
    }
    else if (inode)
    {
        pvfs2_print(" (inode %Lu)\n",
                    Lu(pvfs2_ino_to_handle(inode->i_ino)));
        ret = pvfs2_internal_revalidate(inode);
    }
    else
    {
        pvfs2_print("\n");
    }
    return ret;
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
/*     pvfs2_print("pvfs2: pvfs2_d_hash called " */
/*                 "(name: %s | len: %d | hash: %d)\n", */
/*                 hash->name, hash->len, hash->hash); */
    return 0;
}

static int pvfs2_d_compare(
    struct dentry *parent,
    struct qstr *d_name,
    struct qstr *name)
{
    pvfs2_print("pvfs2_d_compare: called on parent %p\n  (name1: %s| "
                "name2: %s)\n", parent, d_name->name, name->name);

    /* if we have a match, return 0 (normally called from __d_lookup) */
    return !((d_name->len == name->len) &&
             (d_name->hash == name->hash) &&
             (memcmp(d_name->name, name->name, d_name->len) == 0));
}

/** PVFS2 implementation of VFS dentry operations */
struct dentry_operations pvfs2_dentry_operations =
{
    .d_revalidate = pvfs2_d_revalidate,
    .d_hash = pvfs2_d_hash,
    .d_compare = pvfs2_d_compare,
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
