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
 *  Currently works only for 2.6 kernels. No reason why it should
 *  not work for 2.4 kernels, but I am way too lazy to add that right now.
 */

#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"

#if !defined(PVFS2_LINUX_KERNEL_2_4) && defined(HAVE_GENERIC_GETXATTR)

#ifdef HAVE_POSIX_ACL_H
#include <linux/posix_acl.h>
#endif
#ifdef HAVE_LINUX_POSIX_ACL_XATTR_H
#include <linux/posix_acl_xattr.h>
#endif
#include <linux/xattr.h>
#ifdef HAVE_LINUX_XATTR_ACL_H
#include <linux/xattr_acl.h>
#endif
#include "bmi-byteswap.h"

extern int debug;

/*
 * Encoding and Decoding the extended attributes so that we can
 * retrieve it properly on any architecture.
 * Should these go any faster by making them as macros?
 * Probably not in the fast-path though...
 */

/*
 * PVFS2 ACL decode
 */
static struct posix_acl *
pvfs2_acl_decode(const void *value, size_t size)
{
    int n, count;
    struct posix_acl *acl;

    /* badness! */
    if (!value) 
    {
        pvfs2_error("pvfs2_acl_decode: NULL buffers\n");
        return NULL;
    }
    /* even more badness */
    if (size < 0 || (size  % sizeof(pvfs2_acl_entry)) != 0)
    {
        pvfs2_error("pvfs2_acl_decode: Invalid value of size %d [should be a multiple of %d]\n",
                size, sizeof(pvfs2_acl_entry));
        return ERR_PTR(-EINVAL);
    }
    count = size / sizeof(pvfs2_acl_entry);
    /* No ACLs */
    if (count == 0)
    {
        pvfs2_print("pvfs2_acl_decode: no acls!\n");
        return NULL;
    }
    /* Allocate a posix acl structure */
    acl = posix_acl_alloc(count, GFP_KERNEL);
    if (!acl) 
    {
        pvfs2_error("pvfs2_acl_decode: Could not allocate acl!\n");
        return ERR_PTR(-ENOMEM);
    }
    for (n = 0; n < count; n++) 
    {
        pvfs2_acl_entry *entry = (pvfs2_acl_entry *)value;
        
        acl->a_entries[n].e_tag = bmitoh32(entry->p_tag);
        acl->a_entries[n].e_perm = bmitoh32(entry->p_perm);
        switch(acl->a_entries[n].e_tag) 
        {
            case ACL_USER_OBJ:
            case ACL_GROUP_OBJ:
            case ACL_MASK:
            case ACL_OTHER:
                acl->a_entries[n].e_id = ACL_UNDEFINED_ID;
                value += sizeof(pvfs2_acl_entry);
                break;

            case ACL_USER:
            case ACL_GROUP:
                acl->a_entries[n].e_id =
                        bmitoh32(entry->p_id);
                value += sizeof(pvfs2_acl_entry);
                break;

            default:
                pvfs2_error("pvfs2_acl_decode: bogus value of e_tag obtained %d\n",
                        acl->a_entries[n].e_tag);
                goto fail;
        }
    }
    return acl;

fail:
    posix_acl_release(acl);
    return ERR_PTR(-EINVAL);
}

/*
 * PVFS2 ACL encode 
 * What this does is encode the posix_acl structure
 * into little-endian bytefirst using the htobmi* macros
 * and stuffs it into a buffer for storage.
 */
static void *
pvfs2_acl_encode(const struct posix_acl *acl, size_t *size)
{
    char *e, *ptr;
    size_t n;

    *size = acl->a_count * sizeof(pvfs2_acl_entry);
    e = (char *)kmalloc(*size, GFP_KERNEL);
    if (!e) 
    {
        pvfs2_error("pvfs2_acl_encode: Could not allocate %d bytes for acl encode\n", 
                *size);
        return ERR_PTR(-ENOMEM);
    }
    ptr = e;
    for (n = 0; n < acl->a_count; n++) 
    {
        pvfs2_acl_entry *entry = (pvfs2_acl_entry *)e;
        if (acl->a_entries[n].e_tag != ACL_USER
                && acl->a_entries[n].e_tag != ACL_GROUP
                && acl->a_entries[n].e_tag != ACL_USER_OBJ
                && acl->a_entries[n].e_tag != ACL_GROUP_OBJ
                && acl->a_entries[n].e_tag != ACL_MASK
                && acl->a_entries[n].e_tag != ACL_OTHER)
        {
            pvfs2_error("pvfs2_acl_encode: bogus value of e_tag %d\n",
                    acl->a_entries[n].e_tag);
            goto fail;
        }
        entry->p_tag  = htobmi32(acl->a_entries[n].e_tag);
        entry->p_perm = htobmi32(acl->a_entries[n].e_perm);
        entry->p_id   = htobmi32(acl->a_entries[n].e_id);
        e += sizeof(pvfs2_acl_entry);
    }
    return (char *) ptr;
fail:
    kfree(ptr);
    return ERR_PTR(-EINVAL);
}

/**
 * Routines that retrieve and/or set ACLs for PVFS2 files.
 */
static struct posix_acl *pvfs2_get_acl(struct inode *inode, int type)
{
    struct posix_acl *acl;
    int ret;
    char *key = NULL, *value = NULL;

    /* Won't work if you don't mount with the right set of options */
    if (get_acl_flag(inode) == 0) 
    {
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
            pvfs2_error("pvfs2_get_acl: bogus value of type %d\n", type);
            return ERR_PTR(-EINVAL);
    }
    /*
     * Rather than incurring a network call just to determine the exact length of
     * the attribute, I just allocate a max length to save on the network call
     * Conceivably, we could pass NULL to pvfs2_inode_getxattr() to probe the length
     * of the value, but I don't do that for now.
     */
    value = (char *) kmalloc(PVFS_MAX_XATTR_VALUELEN, GFP_KERNEL);
    if (value == NULL)
    {
        pvfs2_error("pvfs2_get_acl: Could not allocate value ptr\n");
        return ERR_PTR(-ENOMEM);
    }
    ret = pvfs2_inode_getxattr(inode, key, value, PVFS_MAX_XATTR_VALUELEN);
    /* if the key exists, convert it to an in-memory rep */
    if (ret > 0)
    {
        acl = pvfs2_acl_decode(value, ret);
    }
    else if (ret == -ENODATA || ret == -ENOSYS)
    {
        acl = NULL;
    }
    else {
        acl = ERR_PTR(ret);
    }
    if (value) {
        kfree(value);
    }
    return acl;
}

static int
pvfs2_set_acl(struct inode *inode, int type, struct posix_acl *acl)
{
    int error;
    void *value = NULL;
    size_t size;
    const char *name = NULL;

    /* We dont't allow this on a symbolic link */
    if (S_ISLNK(inode->i_mode))
    {
        return -EOPNOTSUPP;
    }
    /* if ACL option is not set, then we return early */
    if (get_acl_flag(inode) == 0)
    {
        return 0;
    }
    switch (type)
    {
        case ACL_TYPE_ACCESS:
            name = PVFS2_XATTR_NAME_ACL_ACCESS;
            if (acl) 
            {
                mode_t mode = inode->i_mode;
                pvfs2_print("pvfs2_set_acl: original mode %d\n", mode);
                /* can we represent this with the UNIXy permission bits? */
                error = posix_acl_equiv_mode(acl, &mode);
                /* uh oh some error.. */
                if (error < 0) 
                {
                    pvfs2_error("pvfs2_set_acl: posix_acl_equiv_mode error %d\n", error);
                    return error;
                }
                else /* okay, go ahead and do just that */
                {

                    pvfs2_print("pvfs2_set_acl: after posix_acl_equiv_mode mode %d\n", mode);
                    inode->i_mode = mode;
                    if (error == 0) /* not equivalent */
                        acl = NULL;
                }
            }
            break;

        case ACL_TYPE_DEFAULT:
            name = PVFS2_XATTR_NAME_ACL_DEFAULT;
            /* Default ACLs cannot be set/modified for non-directory objects! */
            if (!S_ISDIR(inode->i_mode))
            {
                return acl ? -EACCES : 0;
            }
            break;

        default:
            return -EINVAL;
    }
    /* If we do have an access control list, then we need to encode that! */
    if (acl) 
    {
        value = pvfs2_acl_encode(acl, &size);
        if (IS_ERR(value))
        {
            return (int) PTR_ERR(value);
        }
    }
    /* Go ahead and set the extended attribute now */
    error = pvfs2_inode_setxattr(inode, name, value, size, 0);
    if (value) 
    {
        kfree(value);
    }
    return error;
}

static int
pvfs2_xattr_get_acl(struct inode *inode, int type, void *buffer, size_t size)
{
    struct posix_acl *acl;
    int error;

    /* if we have not been mounted with acl option, ignore this */
    if (get_acl_flag(inode) == 0)
    {
        return -EOPNOTSUPP;
    }
    acl = pvfs2_get_acl(inode, type);
    if (IS_ERR(acl))
    {
        return PTR_ERR(acl);
    }
    if (acl == NULL)
    {
        return -ENODATA;
    }
    error = posix_acl_to_xattr(acl, buffer, size);
    posix_acl_release(acl);
    return error;
}

static int pvfs2_xattr_get_acl_access(struct inode *inode,
        const char *name, void *buffer, size_t size)
{
    pvfs2_print("pvfs2_xattr_get_acl_access %s\n", name);
    if (strcmp(name, "") != 0)
    {
        return -EINVAL;
    }
    return pvfs2_xattr_get_acl(inode, ACL_TYPE_ACCESS, buffer, size);
}

static int pvfs2_xattr_get_acl_default(struct inode *inode,
        const char *name, void *buffer, size_t size)
{
    pvfs2_print("pvfs2_xattr_get_acl_default %s\n", name);
    if (strcmp(name, "") != 0)
    {
        return -EINVAL;
    }
    return pvfs2_xattr_get_acl(inode, ACL_TYPE_DEFAULT, buffer, size);
}

static int
pvfs2_xattr_set_acl(struct inode *inode, int type, const void *value,
        size_t size)
{
    struct posix_acl *acl;
    int error;

    /* if we have not been mounted with acl option, ignore this */
    if (get_acl_flag(inode) == 0)
    {
        return -EOPNOTSUPP;
    }
    /* Are we capable of setting acls on a file for which we should not be? */
    if ((current->fsuid != inode->i_uid) && !capable(CAP_FOWNER))
    {
        return -EPERM;
    }
    if (value) 
    {
        acl = posix_acl_from_xattr(value, size);
        if (IS_ERR(acl))
        {
            return PTR_ERR(acl);
        }
        else if (acl) 
        {
            error = posix_acl_valid(acl);
            if (error)
            {
                goto out;
            }
        }
    }
    else {
        acl = NULL;
    }
    error = pvfs2_set_acl(inode, type, acl);
out:
    posix_acl_release(acl);
    return error;
}

static int pvfs2_xattr_set_acl_access(struct inode *inode, 
        const char *name, const void *buffer, size_t size, int flags)
{
    pvfs2_print("pvfs2_xattr_set_acl_access: %s\n", name);
    if (strcmp(name, "") != 0)
    {
        return -EINVAL;
    }
    return pvfs2_xattr_set_acl(inode, ACL_TYPE_ACCESS, buffer, size);
}

static int pvfs2_xattr_set_acl_default(struct inode *inode, 
        const char *name, const void *buffer, size_t size, int flags)
{
    pvfs2_print("pvfs2_xattr_set_acl_default: %s\n", name);
    if (strcmp(name, "") != 0)
    {
        return -EINVAL;
    }
    return pvfs2_xattr_set_acl(inode, ACL_TYPE_DEFAULT, buffer, size);
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

    if (dir == NULL)
        dir = inode;
    if (!S_ISLNK(inode->i_mode))
    {
        if (get_acl_flag(inode) == 1)
        {
            acl = pvfs2_get_acl(dir, ACL_TYPE_DEFAULT);
            if (IS_ERR(acl))
                return PTR_ERR(acl);
        }
        if (!acl)
        {
            inode->i_mode &= ~current->fs->umask;
        }
    }
    if (get_acl_flag(inode) == 1 && acl)
    {
        struct posix_acl *clone;
        mode_t mode;

        if (S_ISDIR(inode->i_mode)) 
        {
            error = pvfs2_set_acl(inode, ACL_TYPE_DEFAULT, acl);
            if (error)
                goto cleanup;
        }
        clone = posix_acl_clone(acl, GFP_KERNEL);
        error = -ENOMEM;
        if (!clone)
            goto cleanup;
        mode = inode->i_mode;
        error = posix_acl_create_masq(clone, &mode);
        if (error >= 0)
        {
            inode->i_mode = mode;
            /* 
             * if this is an ACL that cannot be captured by
             * the mode bits, go for the server! 
             */
            if (error > 0)
            {
                error = pvfs2_set_acl(inode, ACL_TYPE_ACCESS, clone);
            }
        }
        posix_acl_release(clone);
    }
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
    struct posix_acl *acl, *clone;
    int error;

    if (get_acl_flag(inode) == 0)
    {
        return 0;
    }
    if (S_ISLNK(inode->i_mode))
    {
        return -EOPNOTSUPP;
    }
    acl = pvfs2_get_acl(inode, ACL_TYPE_ACCESS);
    if (IS_ERR(acl) || !acl)
    {
        return PTR_ERR(acl);
    }
    clone = posix_acl_clone(acl, GFP_KERNEL);
    posix_acl_release(acl);
    if (!clone)
    {
        return -ENOMEM;
    }
    error = posix_acl_chmod_masq(clone, inode->i_mode);
    if (!error)
    {
        error = pvfs2_set_acl(inode, ACL_TYPE_ACCESS, clone);
    }
    posix_acl_release(clone);
    return error;
}

static int pvfs2_check_acl(struct inode *inode, int mask)
{
    struct posix_acl *acl = NULL;

    acl = pvfs2_get_acl(inode, ACL_TYPE_ACCESS);
    if (acl) 
    {
        int error = posix_acl_permission(inode, acl, mask);
        posix_acl_release(acl);
        return error;
    }
    return -EAGAIN;
}

int pvfs2_permission(struct inode *inode, int mask, struct nameidata *nd)
{
#ifdef HAVE_GENERIC_PERMISSION
    return generic_permission(inode, mask, pvfs2_check_acl);
#else
    /* We sort of duplicate the code below from generic_permission */
    int mode = inode->i_mode;
    /* No write access on a rdonly FS */
    if ((mask & MAY_WRITE) && IS_RDONLY(inode) &&
            (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)))
        return -EROFS;
    /* No write access to any immutable files */
    if ((mask & MAY_WRITE) && IS_IMMMUTABLE(inode))
        return -EACCES;
    if (current->fsuid == inode->i_uid) {
        mode >>= 6;
    }
    else if (get_acl_flag(inode) == 1) {
        /*
         * Access ACL won't work if we don't have group permission bits
         * set on the file!
         */
        if (((mode >> 3) & mask & S_IRWXO) != mask)
            goto check_groups;
        error = pvfs2_check_acl(inode, mask);
        /* ACL disallows access */
        if (error == -EACCES)
            goto check_capabilities;
        /* No ACLs present? */
        else if (error == -EAGAIN)
            goto check_groups;
        /* Any other error */
        return error;
    }
check_groups:
    if (in_group_p(inode->i_gid))
        mode >>= 3;
    if ((mode & mask & S_IRWXO) == mask)
        return 0;
check_capabilities:
    /* Are we allowed to override DAC */
    if ((mask & (MAY_READ|MAY_WRITE)) || (inode->i_mode & S_IXUGO))
    {
        if (capable(CAP_DAC_OVERRIDE))
            return 0;
    }
    /* Allow read/search if CAP_DAC_READ_SEARCH */
    if (capable(CAP_DAC_READ_SEARCH) && 
            ((mask == MAY_READ) || (S_ISDIR(inode->i_mode) && !(mask && MAY_WRITE))))
    {
        return 0;
    }
    return -EACCES;
#endif
}

#endif
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
