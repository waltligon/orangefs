/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup pvfs2linux
 *
 *  Linux VFS Access Control List callbacks.
 *  This owes quite a bit of code to the ext2 acl code
 *  with appropriate modifications necessary for PVFS2.
 */

#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"
#include "pvfs2-internal.h"
#include <linux/posix_acl_xattr.h>
#include "bmi-byteswap.h"
#include <linux/fs_struct.h>

/*
 * Encoding and Decoding the extended attributes so that we can
 * retrieve it properly on any architecture.
 * Should these go any faster by making them as macros?
 * Probably not in the fast-path though...
 */

/**
 * Routines that retrieve and/or set ACLs for PVFS2 files.
 */
struct posix_acl *pvfs2_get_acl(struct inode *inode, int type)
{
    struct posix_acl *acl;
    int ret;
    char *key = NULL, *value = NULL;

    /* Won't work if you don't mount with the right set of options */
    if (get_acl_flag(inode) == 0) 
    {
        gossip_debug(GOSSIP_ACL_DEBUG,
                     "pvfs2_get_acl: ACL options disabled on this FS!\n");
        return NULL;
    }
    switch (type)
    {
        case ACL_TYPE_ACCESS:
            key = PVFS2_XATTR_NAME_ACL_ACCESS;
            break;
        case ACL_TYPE_DEFAULT:
            key = PVFS2_XATTR_NAME_ACL_DEFAULT;
            break;
        default:
            gossip_err("pvfs2_get_acl: bogus value of type %d\n", type);
            return ERR_PTR(-EINVAL);
    }
    /*
     * Rather than incurring a network call just to determine
     * the exact length of
     * the attribute, I just allocate a max length to save on the network call
     * Conceivably, we could pass NULL to pvfs2_inode_getxattr()
     * to probe the length
     * of the value, but I don't do that for now.
     */
    value = (char *) kmalloc(PVFS_MAX_XATTR_VALUELEN, GFP_KERNEL);
    if (value == NULL)
    {
        gossip_err("pvfs2_get_acl: Could not allocate value ptr\n");
        return ERR_PTR(-ENOMEM);
    }
    gossip_debug(GOSSIP_ACL_DEBUG, "inode %llu, key %s, type %d\n", 
                 llu(get_handle_from_ino(inode)), key, type);
    ret = pvfs2_inode_getxattr(inode, "", key, value, PVFS_MAX_XATTR_VALUELEN);
    /* if the key exists, convert it to an in-memory rep */
    if (ret > 0)
    {
        acl = posix_acl_from_xattr(&init_user_ns, value, ret);
    }
    else if (ret == -ENODATA || ret == -ENOSYS)
    {
        acl = NULL;
    }
    else
    {
        gossip_err("inode %llu retrieving acl's failed with error %d\n",
                   llu(get_handle_from_ino(inode)), ret);
        acl = ERR_PTR(ret);
    }
    if (value)
    {
        kfree(value);
    }
    return acl;
}

static int pvfs2_set_acl(struct inode *inode, int type, struct posix_acl *acl)
{
    int error = 0;
    void *value = NULL;
    size_t size = 0;
    const char *name = NULL;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);

    /* We dont't allow this on a symbolic link */
    if (S_ISLNK(inode->i_mode))
    {
        gossip_err("pvfs2_set_acl: disallow on symbolic links\n");
        return -EACCES;
    }
    /* if ACL option is not set, then we return early */
    if (get_acl_flag(inode) == 0)
    {
        gossip_debug(GOSSIP_ACL_DEBUG, "pvfs2_set_acl: ACL options disabled on"
                "this FS!\n");
        return 0;
    }
    switch (type)
    {
        case ACL_TYPE_ACCESS:
        {
            name = PVFS2_XATTR_NAME_ACL_ACCESS;
            if (acl) 
            {
                umode_t mode = inode->i_mode;
                /* can we represent this with the UNIXy permission bits? */
                error = posix_acl_equiv_mode(acl, &mode);
                /* uh oh some error.. */
                if (error < 0) 
                {
                    gossip_err("pvfs2_set_acl: posix_acl_equiv_mode error %d\n", 
                            error);
                    return error;
                }
                else /* okay, go ahead and do just that */
                {
                    if (inode->i_mode != mode)
                    {
                        SetModeFlag(pvfs2_inode);
                    }
                    inode->i_mode = mode;
                    mark_inode_dirty_sync(inode);
                    if (error == 0) /* equivalent. so dont set acl! */
                    {
                        acl = NULL;
                    }
                }
            }
            break;
        }
        case ACL_TYPE_DEFAULT:
        {
            name = PVFS2_XATTR_NAME_ACL_DEFAULT;
            /* Default ACLs cannot be set/modified for non-directory objects! */
            if (!S_ISDIR(inode->i_mode))
            {
                gossip_debug(GOSSIP_ACL_DEBUG, "pvfs2_set_acl: setting default "
                             "ACLs on non-dir object? %s\n",
                             acl ? "disallowed" : "ok");
                return acl ? -EACCES : 0;
            }
            break;
        }
        default:
        {
            gossip_err("pvfs2_set_acl: invalid type %d!\n", type);
            return -EINVAL;
        }
    }
    gossip_debug(GOSSIP_ACL_DEBUG,
                 "pvfs2_set_acl: inode %llu, key %s type %d\n",
                 llu(get_handle_from_ino(inode)), name, type);
    /* If we do have an access control list, then we need to encode that! */
    if (acl) 
    {
        value = (char *) kmalloc(PVFS_MAX_XATTR_VALUELEN, GFP_KERNEL);
        if (IS_ERR(value)) 
        {
            return (int) PTR_ERR(value);
        }
        size = posix_acl_to_xattr(&init_user_ns, 
                                  acl, 
                                  value, 
                                  PVFS_MAX_XATTR_VALUELEN);
        if (size < 0)
        {
            error = size;
            goto errorout;
        }
    }
    gossip_debug(GOSSIP_ACL_DEBUG,
                 "pvfs2_set_acl: name %s, value %p, size %zd, "
                 " acl %p\n", name, value, size, acl);
    /* Go ahead and set the extended attribute now 
     * NOTE: Suppose acl was NULL, then value will be NULL and 
     * size will be 0 and that will xlate to a removexattr.
     * However, we dont want removexattr complain if attributes
     * does not exist.
     */
    error = pvfs2_inode_setxattr(inode, "", name, value, size, 0);

errorout:
    if (value) 
    {
        kfree(value);
    }
    return error;
}

static int pvfs2_xattr_get_acl(struct inode *inode,
                               int type,
                               void *buffer,
                               size_t size)
{
    struct posix_acl *acl;
    int error;

    /* if we have not been mounted with acl option, ignore this */
    if (get_acl_flag(inode) == 0)
    {
        gossip_debug(GOSSIP_ACL_DEBUG, "pvfs2_xattr_get_acl: ACL options "
                     "disabled on this FS!\n");
        return -EOPNOTSUPP;
    }
    acl = pvfs2_get_acl(inode, type);
    if (IS_ERR(acl))
    {
        error = PTR_ERR(acl);
        gossip_err("pvfs2_get_acl failed with error %d\n", error);
        goto out;
    }
    if (acl == NULL)
    {
        error = -ENODATA;
        goto out;
    }
    error = posix_acl_to_xattr(&init_user_ns, acl, buffer, size);
    posix_acl_release(acl);
    gossip_debug(GOSSIP_ACL_DEBUG, "pvfs2_xattr_get_acl: posix_acl_to_xattr "
                 "returned %d\n", error);
out:
    return error;
}

static int pvfs2_xattr_get_acl_access(struct dentry *dentry,
                                      const char *name, 
                                      void *buffer, 
                                      size_t size,
                                      int handler_flag)
{
    gossip_debug(GOSSIP_ACL_DEBUG, "pvfs2_xattr_get_acl_access %s\n", name);
    if (strcmp(name, "") != 0)
    {
        gossip_err("get_acl_access invalid name %s\n", name);
        return -EINVAL;
    }
    return pvfs2_xattr_get_acl(dentry->d_inode, ACL_TYPE_ACCESS, buffer, size);
}

static int pvfs2_xattr_get_acl_default(struct dentry *dentry,
                                       const char *name, 
                                       void *buffer, 
                                       size_t size, 
                                       int handler_flags)
{
    gossip_debug(GOSSIP_ACL_DEBUG, "pvfs2_xattr_get_acl_default %s\n", name);
    if (strcmp(name, "") != 0)
    {
        gossip_err("get_acl_default invalid name %s\n", name);
        return -EINVAL;
    }
    return pvfs2_xattr_get_acl(dentry->d_inode, ACL_TYPE_DEFAULT, buffer, size);
}

static int pvfs2_xattr_set_acl(struct inode *inode,
                               int type,
                               const void *value,
                               size_t size)
{
    struct posix_acl *acl;
    int error;
    int fsuid = current_fsuid();

    gossip_debug(GOSSIP_ACL_DEBUG, "pvfs2_xattr_set_acl called with size %ld\n",
            (long)size);
    /* if we have not been mounted with acl option, ignore this */
    if (get_acl_flag(inode) == 0)
    {
        gossip_debug(GOSSIP_ACL_DEBUG, "pvfs2_xattr_set_acl: ACL options "
                "disabled on this FS!\n");
        return -EOPNOTSUPP;
    }
    /* Are we capable of setting acls on a file for which we should not be? */
    if ((fsuid != inode->i_uid) && !capable(CAP_FOWNER))
    {
        gossip_err("pvfs2_xattr_set_acl: operation not permitted "
                "(current->fsuid %d), (inode->owner %d)\n", 
                fsuid, inode->i_uid);
        return -EPERM;
    }
    if (value) 
    {
        acl = posix_acl_from_xattr(&init_user_ns, value, size);
        if (IS_ERR(acl))
        {
            error = PTR_ERR(acl);
            gossip_err("pvfs2_xattr_set_acl: posix_acl_from_xattr returned "
                    "error %d\n", error);
            goto err;
        }
        else if (acl) 
        {
            error = posix_acl_valid(acl);
            if (error)
            {
                gossip_err("pvfs2_xattr_set_acl: posix_acl_valid returned "
                        "error %d\n", error);
                goto out;
            }
        }
    }
    else
    {
        acl = NULL;
    }
    error = pvfs2_set_acl(inode, type, acl);
    gossip_debug(GOSSIP_ACL_DEBUG, "pvfs2_set_acl returned error %d\n", error);
out:
    posix_acl_release(acl);
err:
    return error;
}

static int pvfs2_xattr_set_acl_access(struct dentry *dentry,
                                      const char *name, 
                                      const void *buffer, 
                                      size_t size, 
                                      int flags, 
                                      int handler_flags)
{
    gossip_debug(GOSSIP_ACL_DEBUG, "pvfs2_xattr_set_acl_access: %s\n", name);
    if (strcmp(name, "") != 0)
    {
        gossip_err("set_acl_access invalid name %s\n", name);
        return -EINVAL;
    }
    return pvfs2_xattr_set_acl(dentry->d_inode, ACL_TYPE_ACCESS, buffer, size);
}

static int pvfs2_xattr_set_acl_default(struct dentry *dentry,
                                       const char *name, 
                                       const void *buffer, 
                                       size_t size, 
                                       int flags, 
                                       int handler_flags)
{
    gossip_debug(GOSSIP_ACL_DEBUG, "pvfs2_xattr_set_acl_default: %s\n", name);
    if (strcmp(name, "") != 0)
    {
        gossip_err("set_acl_default invalid name %s\n", name);
        return -EINVAL;
    }
    return pvfs2_xattr_set_acl(dentry->d_inode, ACL_TYPE_DEFAULT, buffer, size);
}

struct xattr_handler pvfs2_xattr_acl_access_handler = {
    .prefix = PVFS2_XATTR_NAME_ACL_ACCESS,
    .get    = pvfs2_xattr_get_acl_access,
    .set    = pvfs2_xattr_set_acl_access,
};

struct xattr_handler pvfs2_xattr_acl_default_handler = {
    .prefix = PVFS2_XATTR_NAME_ACL_DEFAULT,
    .get    = pvfs2_xattr_get_acl_default,
    .set    = pvfs2_xattr_set_acl_default,
};

/*
 * initialize the ACLs of a new inode.
 * This needs to be called from pvfs2_get_custom_inode.
 * Note that for the root of the PVFS2 file system,
 * dir will be NULL! For all others dir will be non-NULL
 * However, inode cannot be NULL!
 * Returns 0 on success and -ve number on failure.
 */
int pvfs2_init_acl(struct inode *inode, struct inode *dir)
{
    struct posix_acl *acl = NULL;
    int error = 0;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);

    if (dir == NULL)
        dir = inode;
    ClearModeFlag(pvfs2_inode);
    if (!S_ISLNK(inode->i_mode))
    {
        if (get_acl_flag(inode) == 1)
        {
            acl = pvfs2_get_acl(dir, ACL_TYPE_DEFAULT);
            if (IS_ERR(acl)) {
                error = PTR_ERR(acl);
                gossip_err("pvfs2_get_acl (default) failed with error %d\n", error);
                return error;
            }
        }
        if (!acl && dir != inode)
        {
            int old_mode = inode->i_mode;
            inode->i_mode &= ~current->fs->umask;
            gossip_debug(GOSSIP_ACL_DEBUG, "inode->i_mode before %o and "
                    "after %o\n", old_mode, inode->i_mode);
            if (old_mode != inode->i_mode)
                SetModeFlag(pvfs2_inode);
        }
    }
    if (get_acl_flag(inode) == 1 && acl)
    {
        umode_t mode;

        if (S_ISDIR(inode->i_mode)) 
        {
            error = pvfs2_set_acl(inode, ACL_TYPE_DEFAULT, acl);
            if (error) {
                gossip_err("pvfs2_set_acl (default) directory failed with "
                        "error %d\n", error);
                ClearModeFlag(pvfs2_inode);
                goto cleanup;
            }
        }
        error = posix_acl_create(&acl, GFP_KERNEL, &mode);
        if (error >= 0)
        {
            gossip_debug(GOSSIP_ACL_DEBUG, "posix_acl_create changed mode "
                    "from %o to %o\n", inode->i_mode, mode);
            /*
             * Dont do a needless ->setattr() if mode has not changed 
             */
            if (inode->i_mode != mode)
                SetModeFlag(pvfs2_inode);
            inode->i_mode = mode;
            /* 
             * if this is an ACL that cannot be captured by
             * the mode bits, go for the server! 
             */
            if (error > 0)
            {
                error = pvfs2_set_acl(inode, ACL_TYPE_ACCESS, acl);
                gossip_debug(GOSSIP_ACL_DEBUG, "pvfs2_set_acl (access) returned %d\n", error);
            }
        }
    }
    /* If mode of the inode was changed, then do a forcible ->setattr */
    if (ModeFlag(pvfs2_inode))
        pvfs2_flush_inode(inode);
cleanup:
    posix_acl_release(acl);
    return error;
}

/*
 * Handles the case when a chmod is done for an inode that may have an
 * access control list.
 * The inode->i_mode field is updated to the desired value by the caller
 * before calling this function which returns 0 on success and a -ve
 * number on failure.
 */
int pvfs2_acl_chmod(struct inode *inode)
{
    struct posix_acl *acl = NULL;
    int error;

    if (get_acl_flag(inode) == 0)
    {
        gossip_debug(GOSSIP_ACL_DEBUG, "pvfs2_acl_chmod: ACL options "
                "disabled on this FS!\n");
        return 0;
    }
    if (S_ISLNK(inode->i_mode))
    {
        gossip_err("pvfs2_acl_chmod: operation not permitted on symlink!\n");
        error = -EACCES;
        goto out;
    }
    acl = pvfs2_get_acl(inode, ACL_TYPE_ACCESS);
    if (IS_ERR(acl))
    {
        error = PTR_ERR(acl);
        gossip_err("pvfs2_acl_chmod: get acl (access) failed with %d\n", error);
        goto out;
    }
    if(!acl)
    {
        error = 0;
        goto out;
    }
    error = posix_acl_chmod(&acl, GFP_KERNEL, inode->i_mode);
    if (!error)
    {
        error = pvfs2_set_acl(inode, ACL_TYPE_ACCESS, acl);
        gossip_debug(GOSSIP_ACL_DEBUG, "pvfs2_acl_chmod: pvfs2 set acl "
                "(access) returned %d\n", error);
    }

    error = posix_acl_chmod(&acl, GFP_KERNEL, inode->i_mode);
    if (!error)
    {
        error = pvfs2_set_acl(inode, ACL_TYPE_ACCESS, acl);
        gossip_debug(GOSSIP_ACL_DEBUG, "pvfs2_acl_chmod: pvfs2 set acl "
                      "(access) returned %d\n", error);
    }

out:
    posix_acl_release(acl);
    return error;
}

int pvfs2_permission(struct inode *inode, int mask)
{
    int fsuid = current_fsuid();
    int ret;

    ret = generic_permission(inode, mask); 
    if (ret != 0)
    {
        gossip_debug(GOSSIP_ACL_DEBUG,
                     "pvfs2_permission failed: inode: %llu mask = %o"
                     "mode = %o current->fsuid = %d "
                     "inode->i_uid = %d, inode->i_gid = %d "
                     "in_group_p = %d "
                     "(ret = %d)\n",
                     llu(get_handle_from_ino(inode)), mask, inode->i_mode,
                     fsuid, inode->i_uid, inode->i_gid, 
                     in_group_p(inode->i_gid),
                     ret);
        gossip_debug(GOSSIP_ACL_DEBUG,
                     "pvfs2_permission: mode [%o] & mask [%o] "
                     " & S_IRWXO [%o] = %o == mask [%o]?\n", 
                     inode->i_mode, mask, S_IRWXO, 
                     (inode->i_mode & mask & S_IRWXO), mask);
        gossip_debug(GOSSIP_ACL_DEBUG,
                     "pvfs2_permission: did we check ACL's? "
                     "(mode & S_IRWXG = %d)\n",
                     inode->i_mode & S_IRWXG);
    }
    else
    {
        gossip_debug(GOSSIP_ACL_DEBUG,
                     "pvfs2_permission succeeded on inode %llu\n",
                     llu(get_handle_from_ino(inode)));
    }
    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
