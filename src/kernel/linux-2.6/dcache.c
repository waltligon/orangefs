/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include "pvfs2-kernel.h"

/* should return 1 if dentry can still be trusted, else 0 */
int pvfs2_d_revalidate(
    struct dentry *dentry,
    struct nameidata *nd)
{
    int ret = 0;
    struct inode *inode = (dentry ? dentry->d_inode : NULL);

    pvfs2_print("pvfs2: pvfs2_d_revalidate called\n");

    if (nd && (nd->flags == LOOKUP_FOLLOW))
    {
        pvfs2_print("pvfs2_d_revlidate: Trusting intent; "
                    "skipping getattr\n");
        ret = 1;
    }
    else if (inode)
    {
	ret = ((pvfs2_inode_getattr(inode) == 0) ? 1 : 0);
    }
    return ret;
}

/*
  to propagate an error, return a value < 0, as this causes
  link_path_walk to pass our error up
*/
static int pvfs2_d_hash(
    struct dentry *parent,
    struct qstr *hash)
{
    pvfs2_print("pvfs2: pvfs2_d_hash called "
		"(name: %s | len: %d | hash: %d)\n",
		hash->name, hash->len, hash->hash);
    return 0;
}

static int pvfs2_d_compare(
    struct dentry *parent,
    struct qstr *d_name,
    struct qstr *name)
{
    pvfs2_print("pvfs2: pvfs2_d_compare called (name1: %s | name2: %s)\n",
		d_name->name, name->name);

    /* if we have a match, return 0 (normally called from __d_lookup) */
    return !((d_name->len == name->len) &&
	     (d_name->hash == name->hash) &&
	     (memcmp(d_name->name, name->name, d_name->len) == 0));
}

struct dentry_operations pvfs2_dentry_operations = {
    .d_revalidate = pvfs2_d_revalidate,
    .d_hash = pvfs2_d_hash,
    .d_compare = pvfs2_d_compare,
/*     .d_delete = pvfs2_d_delete, */
/*     .d_release = pvfs2_d_release, */
/*     .d_iput = pvfs2_d_iput */
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
