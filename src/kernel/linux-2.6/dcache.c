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
#include "pvfs2-internal.h"

static void print_dentry(struct dentry *entry, int ret);

/* should return 1 if dentry can still be trusted, else 0 */
static int pvfs2_d_revalidate_common(struct dentry* dentry)
{
    int ret = 0;
    struct inode *inode;
    struct inode *parent_inode = NULL; 
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *parent = NULL;

    gossip_debug(GOSSIP_DCACHE_DEBUG, "%s: called on dentry %p.\n",
                 __func__, dentry);

    /* find inode from dentry */
    if(!dentry || !dentry->d_inode)
    {
        gossip_debug(GOSSIP_DCACHE_DEBUG, "%s: inode not valid.\n", __func__);
        goto invalid_exit;
    }

    gossip_debug(GOSSIP_DCACHE_DEBUG, "%s: inode valid.\n", __func__);
    inode = dentry->d_inode;

    /* find parent inode */
    if(!dentry || !dentry->d_parent)
    {
        gossip_debug(GOSSIP_DCACHE_DEBUG, "%s: parent not found.\n", __func__);
        goto invalid_exit;
    }

    gossip_debug(GOSSIP_DCACHE_DEBUG, "%s: parent found.\n", __func__);
    parent_inode = dentry->d_parent->d_inode;

    /* first perform a lookup to make sure that the object not only
     * exists, but is still in the expected place in the name space 
     */
    if (!is_root_handle(inode))
    {
        gossip_debug(GOSSIP_DCACHE_DEBUG, "%s: attempting lookup.\n", __func__);
        new_op = op_alloc(PVFS2_VFS_OP_LOOKUP);
        if (!new_op)
        {
            goto invalid_exit;
        }
        new_op->upcall.req.lookup.sym_follow = PVFS2_LOOKUP_LINK_NO_FOLLOW;
        parent = PVFS2_I(parent_inode);
        if (parent && parent->refn.handle != PVFS_HANDLE_NULL &&
            parent->refn.fs_id != PVFS_FS_ID_NULL)
        {
            new_op->upcall.req.lookup.parent_refn = parent->refn;
        }
        else
        {
#if defined(HAVE_IGET4_LOCKED) || defined(HAVE_IGET5_LOCKED)
            gossip_lerr("Critical error: i_ino cannot be relied "
                        "upon when using iget5/iget4\n");
            op_release(new_op);
            goto invalid_exit;
#endif
            new_op->upcall.req.lookup.parent_refn.handle =
                get_handle_from_ino(parent_inode);
            new_op->upcall.req.lookup.parent_refn.fs_id =
                PVFS2_SB(parent_inode->i_sb)->fs_id;
        }
        strncpy(new_op->upcall.req.lookup.d_name,
                dentry->d_name.name, PVFS2_NAME_LEN);

        gossip_debug(GOSSIP_DCACHE_DEBUG, "%s:%s:%d interrupt flag [%d]\n", 
            __FILE__, __func__, __LINE__, get_interruptible_flag(parent_inode));

        ret = service_operation(
            new_op, "pvfs2_lookup", 
            get_interruptible_flag(parent_inode));

        if((new_op->downcall.status != 0) || 
           !match_handle(new_op->downcall.resp.lookup.refn.handle, inode))
        {
            gossip_debug(
                GOSSIP_DCACHE_DEBUG,
                "%s:%s:%d lookup failure |%s| or no match |%s|.\n", 
                __FILE__, __func__, __LINE__,
                (new_op->downcall.status != 0) ? "true" : "false",
                (!match_handle(new_op->downcall.resp.lookup.refn.handle, inode)) ? "true" : "false");
            op_release(new_op);

            /* Avoid calling make_bad_inode() in this situation.  On 2.4
             * (RHEL3) kernels, it can cause bogus permission denied errors
             * on path elements after interrupt signals.  On later 2.6
             * kernels this causes a kernel oops rather than a permission
             * error.
             */
#if 0
            /* mark the inode as bad so that d_delete will be aggressive
             * about dropping the dentry
             */
            pvfs2_make_bad_inode(inode);
#endif
            gossip_debug(GOSSIP_DCACHE_DEBUG, "%s:%s:%d setting revalidate_failed = 1\n", __FILE__, __func__, __LINE__);
            /* set a flag that we can detect later in d_delete() */
            PVFS2_I(inode)->revalidate_failed = 1;
            d_drop(dentry);

            goto invalid_exit;
        }

        op_release(new_op);
    }
    else
    {
        gossip_debug(GOSSIP_DCACHE_DEBUG,
                     "%s: root handle, lookup skipped.\n", __func__);
    }

    /* now perform getattr */
    gossip_debug(GOSSIP_DCACHE_DEBUG,
                 "%s: doing getattr: inode: %p, handle: %llu)\n",
                 __func__, inode, llu(get_handle_from_ino(inode)));
    ret = pvfs2_inode_getattr(inode, PVFS_ATTR_SYS_ALL_NOHINT);
    gossip_debug(GOSSIP_DCACHE_DEBUG,
                 "%s: getattr %s (ret = %d), returning %s for dentry i_count=%d\n",
                 __func__,
                 (ret == 0 ? "succeeded" : "failed"),
                 ret,
                 (ret == 0 ? "valid" : "INVALID"),
                 atomic_read(&inode->i_count));
    if(ret != 0)
    {
        goto invalid_exit;
    }

    /* dentry is valid! */
    return 1;

invalid_exit:
    return 0;
}

static int pvfs2_d_delete (struct dentry * dentry)
{
    gossip_debug(GOSSIP_DCACHE_DEBUG,
                 "%s: called on dentry %p.\n", __func__, dentry);
#if 0
    if(dentry->d_inode && is_bad_inode(dentry->d_inode))
#endif
    if(dentry->d_inode && PVFS2_I(dentry->d_inode)->revalidate_failed == 1)
    {
        gossip_debug(GOSSIP_DCACHE_DEBUG,
                     "%s: returning 1 (bad inode).\n", __func__);
        return 1;
    }
    else
    {
        gossip_debug(GOSSIP_DCACHE_DEBUG,
                     "%s: returning 0 (inode looks ok).\n", __func__);
        return 0;
    }
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
        gossip_debug(GOSSIP_DCACHE_DEBUG,
                     "\n%s: Trusting intent; skipping getattr\n", __func__);
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
    .d_delete = pvfs2_d_delete,
};

/* print_dentry()
 *
 * Available for debugging purposes.  Please remove the unused attribute
 * before invoking
 */
static void __attribute__ ((unused)) print_dentry(struct dentry *entry, int ret)
{
  if(!entry)
  {
    printk("--- dentry %p: no entry, ret: %d\n", entry, ret);
    return;
  }

  if(!entry->d_inode)
  {
    printk("--- dentry %p: no d_inode, ret: %d\n", entry, ret);
    return;
  }

  if(!entry->d_parent)
  {
    printk("--- dentry %p: no d_parent, ret: %d\n", entry, ret);
    return;
  }

  printk("--- dentry %p: d_count: %d, name: %s, parent: %p, parent name: %s, ret: %d\n",
        entry,
        atomic_read(&entry->d_count),
        entry->d_name.name,
        entry->d_parent,
        entry->d_parent->d_name.name,
        ret);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
