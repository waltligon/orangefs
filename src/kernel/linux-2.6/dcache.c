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

extern int debug;
extern kmem_cache_t *pvfs2_inode_cache;
extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;
extern wait_queue_head_t pvfs2_request_list_waitq;

/* should return 1 if dentry can still be trusted, else 0 */
static int pvfs2_d_revalidate_common(struct dentry* dentry)
{
    int ret = 0;
    struct inode *inode = (dentry ? dentry->d_inode : NULL);
    struct inode *parent_inode = NULL; 
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *parent = NULL;
    int retries = PVFS2_OP_RETRY_COUNT, error_exit = 0;

    pvfs2_print("pvfs2_d_revalidate: called on dentry %p.\n", dentry);

    /* find parent inode */
    if(dentry && dentry->d_parent)
    {
        pvfs2_print("pvfs2_d_revalidate: parent found.\n");
        parent_inode = dentry->d_parent->d_inode;
    }
    else
    {
        pvfs2_print("pvfs2_d_revalidate: parent not found.\n");
    }
    
    if (inode && parent_inode)
    {
        /* first perform a lookup to make sure that the object not only
         * exists, but is still in the expected place in the name space 
         */
        if(!(PVFS2_SB(inode->i_sb)->root_handle ==
            pvfs2_ino_to_handle(inode->i_ino)))
        {
            pvfs2_print("pvfs2_d_revalidate: attempting lookup.\n");
            new_op = op_alloc();
            if (!new_op)
            {
                return 0;
            }
            new_op->upcall.type = PVFS2_VFS_OP_LOOKUP;
            new_op->upcall.req.lookup.sym_follow = PVFS2_LOOKUP_LINK_NO_FOLLOW;
            parent = PVFS2_I(parent_inode);
            if (parent && parent->refn.handle && parent->refn.fs_id)
            {
                new_op->upcall.req.lookup.parent_refn = parent->refn;
            }
            else
            {
                new_op->upcall.req.lookup.parent_refn.handle =
                    pvfs2_ino_to_handle(parent_inode->i_ino);
                new_op->upcall.req.lookup.parent_refn.fs_id =
                    PVFS2_SB(parent_inode->i_sb)->fs_id;
            }
            strncpy(new_op->upcall.req.lookup.d_name,
                    dentry->d_name.name, PVFS2_NAME_LEN);

            service_error_exit_op_with_timeout_retry(
                new_op, "pvfs2_lookup", retries, error_exit,
                PVFS2_SB(parent_inode->i_sb)->mnt_options.intr);

            if((new_op->downcall.status != 0) ||
               (new_op->downcall.resp.lookup.refn.handle !=
               pvfs2_ino_to_handle(inode->i_ino)))
            {
                pvfs2_print("pvfs2_d_revalidate: lookup failure or no match.\n");
                op_release(new_op);
                return(0);
            }
            
            op_release(new_op);
        }
        else
        {
            pvfs2_print("pvfs2_d_revalidate: root handle, lookup skipped.\n");
        }

        /* now perform revalidation */
        pvfs2_print(" (inode %llu)\n",
                    llu(pvfs2_ino_to_handle(inode->i_ino)));
        pvfs2_print("pvfs2_d_revalidate: calling pvfs2_internal_revalidate().\n");
        ret = pvfs2_internal_revalidate(inode);
    }
    else
    {
        pvfs2_print("\n");
    }
    return ret;

error_exit:
    pvfs2_print("pvfs2_d_revalidate: error_exit path.\n");
    op_release(new_op);
    return 0;
}

/* should return 1 if dentry can still be trusted, else 0 */
#ifdef PVFS2_LINUX_KERNEL_2_4
int pvfs2_d_revalidate(
    struct dentry *dentry,
    int flags)
{
    return(pvfs2_d_revalidate_common(dentry));
}

#else

/** Verify that dentry is valid.
 */
int pvfs2_d_revalidate(
    struct dentry *dentry,
    struct nameidata *nd)
{

    if (nd && (nd->flags & LOOKUP_FOLLOW) &&
        (!nd->flags & LOOKUP_CREATE))
    {
        pvfs2_print("\npvfs2_d_revalidate: Trusting intent; "
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
