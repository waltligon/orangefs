/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/pagemap.h>
#include "pvfs2-types.h"
#include "pvfs2-kernel.h"
#include "pint-dev-shared.h"
#include "pvfs2-dev-proto.h"
#include "pvfs2-bufmap.h"

extern kmem_cache_t *op_cache;
extern kmem_cache_t *pvfs2_inode_cache;
extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;
extern wait_queue_head_t pvfs2_request_list_waitq;

extern struct inode_operations pvfs2_file_inode_operations;
extern struct file_operations pvfs2_file_operations;
extern struct inode_operations pvfs2_symlink_inode_operations;
extern struct inode_operations pvfs2_dir_inode_operations;
extern struct file_operations pvfs2_dir_operations;
extern struct dentry_operations pvfs2_dentry_operations;

extern struct inode *pvfs2_get_custom_inode(
    struct super_block *sb,
    int mode,
    dev_t dev);


int pvfs2_gen_credentials(
    PVFS_credentials *credentials)
{
    int ret = -1;

    if (credentials)
    {
        memset(credentials, 0, sizeof(PVFS_credentials));
        credentials->uid = current->uid;
        credentials->gid = current->gid;

        ret = 0;
    }
    return ret;
}

/* NOTE: symname is ignored unless the inode is a sym link */
static inline int copy_attributes_to_inode(
    struct inode *inode,
    PVFS_sys_attr *attrs,
    char *symname)
{
    int ret = -1;
    int perm_mode = 0;
    pvfs2_inode_t *pvfs2_inode = NULL;
    loff_t inode_size = 0, rounded_up_size = 0;

    if (inode && attrs)
    {
        pvfs2_inode = PVFS2_I(inode);

        /*
          arbitrarily set the inode block size;
          FIXME: we need to resolve the difference
          between the reported inode blocksize and
          the PAGE_CACHE_SIZE, since our block count
          will always be wrong.

          For now, we're setting the block count to
          be the proper number assuming the block size
          is 512 bytes, and the size is rounded up
          to the nearest 4K.  This is apparently
          required to get proper size reports from
          the 'du' shell utility.

          changing the inode->i_blkbits to something
          other than PAGE_CACHE_SHIFT breaks mmap/execution
          as we depend on that.
        */
        inode->i_blksize = pvfs_bufmap_size_query();
        inode->i_blkbits = PAGE_CACHE_SHIFT;

        if ((attrs->objtype == PVFS_TYPE_METAFILE) &&
            (attrs->mask & PVFS_ATTR_SYS_SIZE))
        {
            inode_size = (loff_t)attrs->size;
            rounded_up_size =
                (inode_size + (4096 - (inode_size % 4096)));

            spin_lock(&inode->i_lock);
            inode->i_bytes = inode_size;
            inode->i_blocks = (unsigned long)(rounded_up_size / 512);
            spin_unlock(&inode->i_lock);

            /*
              NOTE: make sure all the places we're called from have
              the inode->i_sem lock.  we're fine in 99% of the cases
              since we're mostly called from a lookup.
            */
            inode->i_size = inode_size;
        }
        else if ((attrs->objtype == PVFS_TYPE_SYMLINK) &&
                 (symname != NULL))
        {
            inode->i_size = (loff_t)strlen(symname);
        }
        else
        {
            spin_lock(&inode->i_lock);
            inode->i_bytes = PAGE_CACHE_SIZE;
            inode->i_blocks = (unsigned long)(PAGE_CACHE_SIZE / 512);
            spin_unlock(&inode->i_lock);

            inode->i_size = PAGE_CACHE_SIZE;
        }

        inode->i_uid = attrs->owner;
        inode->i_gid = attrs->group;
        inode->i_atime.tv_sec = (time_t)attrs->atime;
        inode->i_mtime.tv_sec = (time_t)attrs->mtime;
        inode->i_ctime.tv_sec = (time_t)attrs->ctime;

        inode->i_mode = 0;

        if (attrs->perms & PVFS_O_EXECUTE)
            perm_mode |= S_IXOTH;
        if (attrs->perms & PVFS_O_WRITE)
            perm_mode |= S_IWOTH;
        if (attrs->perms & PVFS_O_READ)
            perm_mode |= S_IROTH;

        if (attrs->perms & PVFS_G_EXECUTE)
            perm_mode |= S_IXGRP;
        if (attrs->perms & PVFS_G_WRITE)
            perm_mode |= S_IWGRP;
        if (attrs->perms & PVFS_G_READ)
            perm_mode |= S_IRGRP;

        if (attrs->perms & PVFS_U_EXECUTE)
            perm_mode |= S_IXUSR;
        if (attrs->perms & PVFS_U_WRITE)
            perm_mode |= S_IWUSR;
        if (attrs->perms & PVFS_U_READ)
            perm_mode |= S_IRUSR;

        inode->i_mode |= perm_mode;

        switch (attrs->objtype)
        {
            case PVFS_TYPE_METAFILE:
                inode->i_mode |= S_IFREG;
                inode->i_op = &pvfs2_file_inode_operations;
                inode->i_fop = &pvfs2_file_operations;
                ret = 0;
                break;
            case PVFS_TYPE_DIRECTORY:
                inode->i_mode |= S_IFDIR;
                inode->i_op = &pvfs2_dir_inode_operations;
                inode->i_fop = &pvfs2_dir_operations;
                ret = 0;
                break;
            case PVFS_TYPE_SYMLINK:
                inode->i_mode |= S_IFLNK;
                inode->i_op = &pvfs2_symlink_inode_operations;
                inode->i_fop = NULL;

                /* copy the link target string to the inode private data */
                if (pvfs2_inode && symname)
                {
                    if (pvfs2_inode->link_target)
                    {
                        kfree(pvfs2_inode->link_target);
                        pvfs2_inode->link_target = NULL;
                    }
                    pvfs2_inode->link_target = kmalloc(
                        (strlen(symname) + 1), PVFS2_GFP_FLAGS);
                    if (pvfs2_inode->link_target)
                    {
                        strcpy(pvfs2_inode->link_target, symname);
                    }
                    pvfs2_print("Copied attr link target %s\n",
                                pvfs2_inode->link_target);
                }
                ret = 0;
                break;
            default:
                pvfs2_error("pvfs2: copy_attributes_to_inode: got invalid "
                            "attribute type %d\n", attrs->objtype);
        }
    }
    return ret;
}

static inline void convert_attribute_mode_to_pvfs_sys_attr(
    int mode,
    PVFS_sys_attr *attrs)
{
    if (mode & S_IXOTH)
        attrs->perms |= PVFS_O_EXECUTE;
    else
        attrs->perms &= ~PVFS_O_EXECUTE;
    if (mode & S_IWOTH)
        attrs->perms |= PVFS_O_WRITE;
    else
        attrs->perms &= ~PVFS_O_WRITE;
    if (mode & S_IROTH)
        attrs->perms |= PVFS_O_READ;
    else
        attrs->perms &= ~PVFS_O_READ;

    if (mode & S_IXGRP)
        attrs->perms |= PVFS_G_EXECUTE;
    else
        attrs->perms &= ~PVFS_G_EXECUTE;
    if (mode & S_IWGRP)
        attrs->perms |= PVFS_G_WRITE;
    else
        attrs->perms &= ~PVFS_G_WRITE;
    if (mode & S_IRGRP)
        attrs->perms |= PVFS_G_READ;
    else
        attrs->perms &= ~PVFS_G_READ;

    if (mode & S_IXUSR)
        attrs->perms |= PVFS_U_EXECUTE;
    else
        attrs->perms &= ~PVFS_U_EXECUTE;
    if (mode & S_IWUSR)
        attrs->perms |= PVFS_U_WRITE;
    else
        attrs->perms &= ~PVFS_U_WRITE;
    if (mode & S_IRUSR)
        attrs->perms |= PVFS_U_READ;
    else
        attrs->perms &= ~PVFS_U_READ;

    attrs->mask |= PVFS_ATTR_SYS_PERM;

    if ((mode & S_IFMT) == S_IFREG)
    {
        attrs->objtype = PVFS_TYPE_METAFILE;
        attrs->mask |= PVFS_ATTR_SYS_TYPE;
    }
    else if ((mode & S_IFMT) == S_IFDIR)
    {
        attrs->objtype = PVFS_TYPE_DIRECTORY;
        attrs->mask |= PVFS_ATTR_SYS_TYPE;
    }
    else if ((mode & S_IFMT) == S_IFLNK)
    {
        attrs->objtype = PVFS_TYPE_SYMLINK;
        attrs->mask |= PVFS_ATTR_SYS_TYPE;
    }
}

/*
  NOTE: in kernel land, we never use the
  sys_attr->link_target for anything, so don't bother
  copying it into the sys_attr object here.
*/
static inline int copy_attributes_from_inode(
    struct inode *inode,
    PVFS_sys_attr *attrs,
    struct iattr *iattr)
{
    int ret = -1;

    if (inode && attrs)
    {
        /*
          if we got a non-NULL iattr structure, we need to be
          careful to only copy the attributes out of the iattr
          object that we know are valid
        */
        if (iattr && (iattr->ia_valid & ATTR_UID))
            attrs->owner = iattr->ia_uid;
        else
            attrs->owner = inode->i_uid;
        attrs->mask |= PVFS_ATTR_SYS_UID;

        if (iattr && (iattr->ia_valid & ATTR_GID))
            attrs->group = iattr->ia_gid;
        else
            attrs->group = inode->i_gid;
        attrs->mask |= PVFS_ATTR_SYS_GID;

        if (iattr && (iattr->ia_valid & ATTR_ATIME))
            attrs->atime = (PVFS_time)iattr->ia_atime.tv_sec;
        else
            attrs->atime = (PVFS_time)inode->i_atime.tv_sec;
        attrs->mask |= PVFS_ATTR_SYS_ATIME;

        if (iattr && (iattr->ia_valid & ATTR_MTIME))
            attrs->mtime = (PVFS_time)iattr->ia_mtime.tv_sec;
        else
            attrs->mtime = (PVFS_time)inode->i_mtime.tv_sec;
        attrs->mask |= PVFS_ATTR_SYS_MTIME;

        if (iattr && (iattr->ia_valid & ATTR_CTIME))
            attrs->ctime = (PVFS_time)iattr->ia_ctime.tv_sec;
        else
            attrs->ctime = (PVFS_time)inode->i_ctime.tv_sec;
        attrs->mask |= PVFS_ATTR_SYS_CTIME;

        if (iattr && (iattr->ia_valid & ATTR_SIZE))
            attrs->size = iattr->ia_size;
        else
            attrs->size = inode->i_size;
        attrs->mask |= PVFS_ATTR_SYS_SIZE;

        if (iattr && (iattr->ia_valid & ATTR_MODE))
            convert_attribute_mode_to_pvfs_sys_attr(
                iattr->ia_mode, attrs);
        else
            convert_attribute_mode_to_pvfs_sys_attr(
                inode->i_mode, attrs);
        attrs->mask = PVFS_ATTR_SYS_ALL_SETABLE;

        ret = 0;
    }
    return ret;
}

/*
  issues a pvfs2 getattr request and fills in the
  appropriate inode attributes if successful.

  returns 0 on success; -errno otherwise
*/
int pvfs2_inode_getattr(
    struct inode *inode)
{
    int ret = -EINVAL, retries = PVFS2_OP_RETRY_COUNT, error_exit = 0;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *pvfs2_inode = NULL;

    if (inode)
    {
        pvfs2_inode = PVFS2_I(inode);
        if (!pvfs2_inode)
        {
            return ret;
        }

        /*
           in the case of being called from s_op->read_inode,
           the pvfs2_inode private data hasn't been initialized
           yet, so we need to use the inode number as the handle
           and query the superblock for the fs_id.  Further, we
           assign that private data here.

           that call flow looks like:
           lookup --> iget --> read_inode --> here

           if the inode were already in the inode cache, it looks like:
           lookup --> revalidate --> here
        */
        if (pvfs2_inode->refn.handle == 0)
        {
            pvfs2_inode->refn.handle = pvfs2_ino_to_handle(inode->i_ino);
        }
        if (pvfs2_inode->refn.fs_id == 0)
        {
            pvfs2_inode->refn.fs_id = PVFS2_SB(inode->i_sb)->fs_id;
        }

        /*
           post a getattr request here;
           make dentry valid if getattr passes
        */
        new_op = kmem_cache_alloc(op_cache, PVFS2_CACHE_ALLOC_FLAGS);
        if (!new_op)
        {
            pvfs2_error("pvfs2: pvfs2_inode_getattr -- "
                        "kmem_cache_alloc failed!\n");
            ret = -ENOMEM;
            return ret;
        }
        new_op->upcall.type = PVFS2_VFS_OP_GETATTR;
        new_op->upcall.req.getattr.refn = pvfs2_inode->refn;

        /* need to check downcall.status value */
        pvfs2_print("Trying Getattr on handle %Lu on fsid %d "
                    "(inode ct = %d)\n", pvfs2_inode->refn.handle,
                    pvfs2_inode->refn.fs_id,
                    (int)atomic_read(&inode->i_count));

        service_error_exit_op_with_timeout_retry(
            new_op, "pvfs2_inode_getattr", retries, error_exit,
            get_interruptible_flag(inode));

        /* check what kind of goodies we got */
        if (new_op->downcall.status > -1)
        {
            if (copy_attributes_to_inode
                (inode, &new_op->downcall.resp.getattr.attributes,
                 new_op->downcall.resp.getattr.link_target))
            {
                pvfs2_error("pvfs2: pvfs2_inode_getattr -- failed "
                            "to copy attributes\n");
            }
        }
        ret = pvfs2_kernel_error_code_convert(new_op->downcall.status);

      error_exit:
        pvfs2_print(error_exit ? "*** warning: getattr error_exit\n" : "");
        op_release(new_op);
    }
    return ret;
}

/*
  issues a pvfs2 setattr request to make sure the
  new attribute values take effect if successful.

  returns 0 on success; -1 otherwise
*/
int pvfs2_inode_setattr(
    struct inode *inode,
    struct iattr *iattr)
{
    int ret = -1, retries = PVFS2_OP_RETRY_COUNT;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *pvfs2_inode = NULL;

    if (inode)
    {
        pvfs2_inode = PVFS2_I(inode);

        new_op = kmem_cache_alloc(op_cache, PVFS2_CACHE_ALLOC_FLAGS);
        if (!new_op)
        {
            return ret;
        }

        new_op->upcall.type = PVFS2_VFS_OP_SETATTR;
        new_op->upcall.req.setattr.refn = pvfs2_inode->refn;
        if ((new_op->upcall.req.setattr.refn.handle == (PVFS_handle)0) &&
            (new_op->upcall.req.setattr.refn.fs_id == (PVFS_fs_id)0))
        {
            struct super_block *sb = inode->i_sb;
            new_op->upcall.req.lookup.parent_refn.handle =
                PVFS2_SB(sb)->root_handle;
            new_op->upcall.req.lookup.parent_refn.fs_id =
                PVFS2_SB(sb)->fs_id;
        }
        copy_attributes_from_inode(
            inode, &new_op->upcall.req.setattr.attributes, iattr);

        service_operation_with_timeout_retry(
            new_op, "pvfs2_inode_setattr", retries,
            get_interruptible_flag(inode));

        pvfs2_print("Setattr Got PVFS2 status value of %d\n",
                    new_op->downcall.status);

      error_exit:
        ret = new_op->downcall.status;

        /* when request is serviced properly, free req op struct */
        op_release(new_op);
    }
    return ret;
}

static inline struct inode *pvfs2_create_file(
    struct inode *dir,
    struct dentry *dentry,
    int mode,
    int *error_code)
{
    int ret = -1, retries = PVFS2_OP_RETRY_COUNT;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *parent = PVFS2_I(dir);
    pvfs2_inode_t *pvfs2_inode = NULL;
    struct inode *inode =
        pvfs2_get_custom_inode(dir->i_sb, (S_IFREG | mode), 0);

    if (inode)
    {
        new_op = kmem_cache_alloc(op_cache, PVFS2_CACHE_ALLOC_FLAGS);
        if (!new_op)
        {
            pvfs2_error("pvfs2: pvfs2_create_file -- "
                        "kmem_cache_alloc failed!\n");
            iput(inode);
            *error_code = -ENOMEM;
            return NULL;
        }
        new_op->upcall.type = PVFS2_VFS_OP_CREATE;
        if (parent && parent->refn.handle && parent->refn.fs_id)
        {
            new_op->upcall.req.create.parent_refn = parent->refn;
        }
        else
        {
            new_op->upcall.req.create.parent_refn.handle =
                pvfs2_ino_to_handle(dir->i_ino);
            new_op->upcall.req.create.parent_refn.fs_id =
                PVFS2_SB(dir->i_sb)->fs_id;
        }
        copy_attributes_from_inode(
            inode, &new_op->upcall.req.create.attributes, NULL);
        strncpy(new_op->upcall.req.create.d_name,
                dentry->d_name.name, PVFS2_NAME_LEN);

        service_operation_with_timeout_retry(
            new_op, "pvfs2_create_file", retries,
            get_interruptible_flag(inode));

        pvfs2_print("Create Got PVFS2 handle %Lu on fsid %d\n",
                    new_op->downcall.resp.create.refn.handle,
                    new_op->downcall.resp.create.refn.fs_id);

        /*
           set the inode private data here, and set the inode number
           here
         */
        if (new_op->downcall.status > -1)
        {
            inode->i_ino = pvfs2_handle_to_ino(
                new_op->downcall.resp.create.refn.handle);

            pvfs2_print("Assigned inode new number of %d\n",
                        (int)inode->i_ino);

            pvfs2_inode = PVFS2_I(inode);
            pvfs2_inode->refn = new_op->downcall.resp.create.refn;

            /*
               set up the dentry operations to make sure that our
               pvfs2 specific dentry operations take effect.

               this is exploited by defining a revalidate method to be
               called each time a lookup is done to avoid the natural
               caching effect of the vfs.  unfortunately, client side
               caching isn't good for consistency across nodes.  this
               is also done in the create_dir and create_symlink
               methods below.

               NOTE: when adding negative dentries, we set the pvfs2
               specific dentry operations already, so this is more
               just a sanity re-set.
             */
            dentry->d_op = &pvfs2_dentry_operations;

            /* finally, add dentry with this new inode to the dcache */
            pvfs2_print("pvfs2_create_file: Instantiating *negative* "
                        "dentry for %s\n", dentry->d_name.name);
            d_instantiate(dentry, inode);
        }
        else
        {
          error_exit:
            pvfs2_print("pvfs2_create_file: An error occurred; "
                        "removing created inode\n");
            iput(inode);
            inode = NULL;

            *error_code = pvfs2_kernel_error_code_convert(
                new_op->downcall.status);
        }
        op_release(new_op);
    }
    return inode;
}

static inline struct inode *pvfs2_create_dir(
    struct inode *dir,
    struct dentry *dentry,
    int mode,
    int *error_code)
{
    int ret = -1, retries = PVFS2_OP_RETRY_COUNT;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *parent = PVFS2_I(dir);
    pvfs2_inode_t *pvfs2_inode = NULL;
    struct inode *inode =
        pvfs2_get_custom_inode(dir->i_sb, (S_IFDIR | mode), 0);

    if (inode)
    {
        new_op = kmem_cache_alloc(op_cache, PVFS2_CACHE_ALLOC_FLAGS);
        if (!new_op)
        {
            pvfs2_error("pvfs2: pvfs2_create_dir -- "
                        "kmem_cache_alloc failed!\n");
            iput(inode);
            *error_code = -ENOMEM;
            return NULL;
        }
        new_op->upcall.type = PVFS2_VFS_OP_MKDIR;
        if (parent && parent->refn.handle && parent->refn.fs_id)
        {
            new_op->upcall.req.mkdir.parent_refn = parent->refn;
        }
        else
        {
            new_op->upcall.req.mkdir.parent_refn.handle =
                pvfs2_ino_to_handle(dir->i_ino);
            new_op->upcall.req.mkdir.parent_refn.fs_id =
                PVFS2_SB(dir->i_sb)->fs_id;
        }
        copy_attributes_from_inode(
            inode, &new_op->upcall.req.mkdir.attributes, NULL);
        strncpy(new_op->upcall.req.mkdir.d_name,
                dentry->d_name.name, PVFS2_NAME_LEN);

        pvfs2_print("pvfs2: pvfs2_create_dir op initialized "
                    "with type %d\n", new_op->upcall.type);

        service_operation_with_timeout_retry(
            new_op, "pvfs2_create_dir", retries,
            get_interruptible_flag(inode));

        pvfs2_print("Mkdir Got PVFS2 handle %Lu on fsid %d\n",
                    new_op->downcall.resp.mkdir.refn.handle,
                    new_op->downcall.resp.mkdir.refn.fs_id);

        /*
           set the inode private data here, and set the
           inode number here
         */
        if (new_op->downcall.status > -1)
        {
            inode->i_ino = pvfs2_handle_to_ino(
                new_op->downcall.resp.mkdir.refn.handle);

            pvfs2_print("Assigned inode new number of %d\n",
                        (int) inode->i_ino);

            pvfs2_inode = PVFS2_I(inode);
            pvfs2_inode->refn = new_op->downcall.resp.mkdir.refn;

            dentry->d_op = &pvfs2_dentry_operations;

            /* finally, add dentry with this new inode to the dcache */
            pvfs2_print("pvfs2_create_dir: Instantiating *negative* "
                        "dentry for %s\n", dentry->d_name.name);
            d_instantiate(dentry, inode);
        }
        else
        {
          error_exit:
            pvfs2_error("pvfs2_create_dir: An error occurred; "
                        "removing created inode\n");
            iput(inode);
            inode = NULL;

            *error_code = pvfs2_kernel_error_code_convert(
                new_op->downcall.status);
        }
        op_release(new_op);
    }
    return inode;
}

static inline struct inode *pvfs2_create_symlink(
    struct inode *dir,
    struct dentry *dentry,
    const char *symname,
    int mode,
    int *error_code)
{
    int ret = -1, retries = PVFS2_OP_RETRY_COUNT;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *parent = PVFS2_I(dir);
    pvfs2_inode_t *pvfs2_inode = NULL;
    struct inode *inode =
        pvfs2_get_custom_inode(dir->i_sb, (S_IFLNK | mode), 0);

    if (inode)
    {
        new_op = kmem_cache_alloc(op_cache, PVFS2_CACHE_ALLOC_FLAGS);
        if (!new_op)
        {
            pvfs2_error("pvfs2: pvfs2_create_symlink -- "
                        "kmem_cache_alloc failed!\n");
            iput(inode);
            *error_code = -ENOMEM;
            return NULL;
        }
        new_op->upcall.type = PVFS2_VFS_OP_SYMLINK;
        if (parent && parent->refn.handle && parent->refn.fs_id)
        {
            new_op->upcall.req.sym.parent_refn = parent->refn;
        }
        else
        {
            new_op->upcall.req.sym.parent_refn.handle =
                pvfs2_ino_to_handle(dir->i_ino);
            new_op->upcall.req.sym.parent_refn.fs_id =
                PVFS2_SB(dir->i_sb)->fs_id;
        }
        copy_attributes_from_inode(
            inode, &new_op->upcall.req.sym.attributes, NULL);
        strncpy(new_op->upcall.req.sym.entry_name,
                dentry->d_name.name, PVFS2_NAME_LEN);
        strncpy(new_op->upcall.req.sym.target,
                symname, PVFS2_NAME_LEN);

        service_operation_with_timeout_retry(
            new_op, "pvfs2_symlink_file", retries,
            get_interruptible_flag(inode));

        pvfs2_print("Symlink Got PVFS2 handle %Lu on fsid %d\n",
                    new_op->downcall.resp.sym.refn.handle,
                    new_op->downcall.resp.sym.refn.fs_id);

        /*
           set the inode private data here, and set the
           inode number here
         */
        if (new_op->downcall.status > -1)
        {
            inode->i_ino = pvfs2_handle_to_ino(
                new_op->downcall.resp.sym.refn.handle);

            pvfs2_print("Assigned inode new number of %d\n",
                        (int)inode->i_ino);

            pvfs2_inode = PVFS2_I(inode);
            pvfs2_inode->refn = new_op->downcall.resp.sym.refn;

            dentry->d_op = &pvfs2_dentry_operations;

            /* finally, add dentry with this new inode to the dcache */
            pvfs2_print("pvfs2_create_symlink: Instantiating *negative* "
                        "dentry for %s\n", dentry->d_name.name);
            d_instantiate(dentry, inode);
        }
        else
        {
          error_exit:
            pvfs2_error("pvfs2_symlink_file: An error occurred; "
                        "removing created inode\n");
            iput(inode);
            inode = NULL;

            *error_code = pvfs2_kernel_error_code_convert(
                new_op->downcall.status);
        }
        op_release(new_op);
    }
    return inode;
}

/*
  create a pvfs2 entry; returns a properly populated inode
  pointer on success; NULL on failure.

  the required error_code value will contain an error code ONLY if an
  error occurs (i.e. NULL is returned) and is unmodified otherwise.

  if op_type is PVFS_VFS_OP_CREATE, a file is created
  if op_type is PVFS_VFS_OP_MKDIR, a directory is created
  if op_type is PVFS_VFS_OP_SYMLINK, a symlink is created

  symname should be null unless mode is PVFS_VFS_OP_SYMLINK
*/
struct inode *pvfs2_create_entry(
    struct inode *dir,
    struct dentry *dentry,
    const char *symname,
    int mode,
    int op_type,
    int *error_code)
{
    if (dir && dentry && error_code)
    {
        switch (op_type)
        {
            case PVFS2_VFS_OP_CREATE:
                return pvfs2_create_file(dir, dentry, mode, error_code);
            case PVFS2_VFS_OP_MKDIR:
                return pvfs2_create_dir(dir, dentry, mode, error_code);
            case PVFS2_VFS_OP_SYMLINK:
                return pvfs2_create_symlink(
                    dir, dentry, symname, mode, error_code);
            default:
                pvfs2_error("pvfs2_create_entry got a bad "
                            "op_type (%d)\n", op_type);
        }
    }
    return NULL;
}

int pvfs2_remove_entry(
    struct inode *dir,
    struct dentry *dentry)
{
    int ret = -EINVAL, retries = PVFS2_OP_RETRY_COUNT;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *parent = PVFS2_I(dir);
    struct inode *inode = dentry->d_inode;

    if (inode && parent && dentry)
    {
        pvfs2_print("pvfs2: pvfs2_remove_entry on %s (inode %d): "
                    "Parent is %Lu | fs_id %d\n", dentry->d_name.name,
                    (int)inode->i_ino, parent->refn.handle,
                    parent->refn.fs_id);
        new_op = kmem_cache_alloc(op_cache, PVFS2_CACHE_ALLOC_FLAGS);
        if (!new_op)
        {
            return -ENOMEM;
        }
        new_op->upcall.type = PVFS2_VFS_OP_REMOVE;

        if (parent && parent->refn.handle && parent->refn.fs_id)
        {
            new_op->upcall.req.remove.parent_refn = parent->refn;
        }
        else
        {
            new_op->upcall.req.remove.parent_refn.handle =
                pvfs2_ino_to_handle(dir->i_ino);
            new_op->upcall.req.remove.parent_refn.fs_id =
                PVFS2_SB(dir->i_sb)->fs_id;
        }
        strncpy(new_op->upcall.req.remove.d_name,
                dentry->d_name.name, PVFS2_NAME_LEN);

        service_operation_with_timeout_retry(
            new_op, "pvfs2_remove_entry", retries,
            get_interruptible_flag(inode));

        /*
           the remove has no downcall members to retrieve, but
           the status value tells us if it went through ok or not
         */
        if (new_op->downcall.status == 0)
        {
            /*
              adjust the readdir token if in fact we're
              in the middle of a readdir for this directory
            */
            parent->readdir_token_adjustment++;

            pvfs2_print("token adjustment is %d\n",
                        parent->readdir_token_adjustment);
        }
        ret = pvfs2_kernel_error_code_convert(new_op->downcall.status);

      error_exit:
        /* when request is serviced properly, free req op struct */
        op_release(new_op);
    }
    return ret;
}

int pvfs2_truncate_inode(
    struct inode *inode,
    loff_t size)
{
    int ret = -1, retries = 5;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);
    pvfs2_kernel_op_t *new_op = NULL;

    pvfs2_print("pvfs2: pvfs2_truncate_inode %d: "
                "Handle is %Lu | fs_id %d | size is %lu\n",
                (int)inode->i_ino, pvfs2_inode->refn.handle,
                pvfs2_inode->refn.fs_id, (unsigned long)size);

    new_op = kmem_cache_alloc(op_cache, PVFS2_CACHE_ALLOC_FLAGS);
    if (!new_op)
    {
        return -ENOMEM;
    }
    new_op->upcall.type = PVFS2_VFS_OP_TRUNCATE;
    new_op->upcall.req.truncate.refn = pvfs2_inode->refn;
    new_op->upcall.req.truncate.size = (PVFS_size)size;

    service_operation_with_timeout_retry(
        new_op, "pvfs2_truncate_inode", retries,
        get_interruptible_flag(inode));

    /*
      the truncate has no downcall members to retrieve, but
      the status value tells us if it went through ok or not
    */
    ret = new_op->downcall.status;

    pvfs2_print("pvfs2: pvfs2_truncate got return value of %d\n",ret);

  error_exit:
    op_release(new_op);

    return ret;
}

#ifdef USE_MMAP_RA_CACHE
int pvfs2_flush_mmap_racache(struct inode *inode)
{
    int ret = -EINVAL;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);
    pvfs2_kernel_op_t *new_op = NULL;

    pvfs2_print("pvfs2: pvfs2_flush_mmap_racache %d: "
                "Handle is %Lu | fs_id %d\n",(int)inode->i_ino,
                pvfs2_inode->refn.handle, pvfs2_inode->refn.fs_id);

    new_op = kmem_cache_alloc(op_cache, PVFS2_CACHE_ALLOC_FLAGS);
    if (!new_op)
    {
        return -ENOMEM;
    }
    new_op->upcall.type = PVFS2_VFS_OP_MMAP_RA_FLUSH;
    new_op->upcall.req.ra_cache_flush.refn = pvfs2_inode->refn;

    service_operation(new_op, "pvfs2_flush_mmap_racache",
                      get_interruptible_flag(inode));

    ret = new_op->downcall.status;

    pvfs2_print("pvfs2: pvfs2_flush_mmap_racache got "
                "return value of %d\n",ret);

  error_exit:
    op_release(new_op);

    return ret;
}
#endif

int pvfs2_unmount_sb(struct super_block *sb)
{
    int ret = -1;
    pvfs2_kernel_op_t *new_op = NULL;

    pvfs2_print("pvfs2_unmount_sb called on sb %p\n", sb);

    new_op = kmem_cache_alloc(op_cache, PVFS2_CACHE_ALLOC_FLAGS);
    if (!new_op)
    {
        return -ENOMEM;
    }
    new_op->upcall.type = PVFS2_VFS_OP_FS_UMOUNT;
    new_op->upcall.req.fs_umount.id = PVFS2_SB(sb)->id;
    new_op->upcall.req.fs_umount.fs_id = PVFS2_SB(sb)->fs_id;
    strncpy(new_op->upcall.req.fs_umount.pvfs2_config_server,
            PVFS2_SB(sb)->devname, PVFS_MAX_SERVER_ADDR_LEN);

    pvfs2_print("Attempting PVFS2 Unmount via host %s\n",
                new_op->upcall.req.fs_umount.pvfs2_config_server);

    service_operation(new_op, "pvfs2_fs_umount", 0);
    ret = pvfs2_kernel_error_code_convert(new_op->downcall.status);

    pvfs2_print("pvfs2_unmount: got return value of %d\n", ret);
    if (ret)
    {
        sb = ERR_PTR(ret);
        goto error_exit;
    }

    /*
      the unmount has no downcall members to retrieve, but
      the status value tells us if it went through ok or not
    */
    ret = new_op->downcall.status;

    pvfs2_print("pvfs2_unmount got return value of %d\n",ret);

  error_exit:
    op_release(new_op);

    return ret;
}

/* macro defined in include/pvfs2-types.h */
DECLARE_ERRNO_MAPPING_AND_FN();

int pvfs2_kernel_error_code_convert(
    int pvfs2_error_code)
{
    return (int)PVFS_get_errno_mapping((int32_t)pvfs2_error_code);
}

void pvfs2_inode_initialize(pvfs2_inode_t *pvfs2_inode)
{
    pvfs2_inode->refn.handle = 0;
    pvfs2_inode->refn.fs_id = 0;
    pvfs2_inode->link_target = NULL;
    pvfs2_inode->last_failed_block_index_read = 0;
    pvfs2_inode->readdir_token_adjustment = 0;
}

void pvfs2_op_initialize(pvfs2_kernel_op_t *op)
{
    op->io_completed = 0;

    op->upcall.type = PVFS2_VFS_OP_INVALID;
    op->downcall.type = PVFS2_VFS_OP_INVALID;
    op->downcall.status = -1;

    op->op_state = PVFS2_VFS_STATE_UNKNOWN;
    op->tag = 0;
}

void pvfs2_make_bad_inode(struct inode *inode)
{
    if (pvfs2_handle_to_ino(PVFS2_SB(inode->i_sb)->root_handle) ==
        inode->i_ino)
    {
        /*
          if this occurs, the pvfs2-client-core was killed but we
          can't afford to lose the inode operations and such
          associated with the root handle in any case
        */
        pvfs2_print("*** NOT making bad root inode %lu\n", inode->i_ino);
    }
    else
    {
        pvfs2_print("*** making bad inode %lu\n", inode->i_ino);
        make_bad_inode(inode);
    }
}

/* this code is based on linux/net/sunrpc/clnt.c:rpc_clnt_sigmask */
void mask_blocked_signals(sigset_t *orig_sigset)
{
    unsigned long sigallow = sigmask(SIGKILL);
    unsigned long irqflags = 0;
    struct k_sigaction *action = current->sighand->action;

    sigallow |= ((action[SIGINT-1].sa.sa_handler == SIG_DFL) ?
                 sigmask(SIGINT) : 0);
    sigallow |= ((action[SIGQUIT-1].sa.sa_handler == SIG_DFL) ?
                 sigmask(SIGQUIT) : 0);

    spin_lock_irqsave(&current->sighand->siglock, irqflags);
    *orig_sigset = current->blocked;
    siginitsetinv(&current->blocked, sigallow & ~orig_sigset->sig[0]);
    recalc_sigpending();
    spin_unlock_irqrestore(&current->sighand->siglock, irqflags);
}

/* this code is based on linux/net/sunrpc/clnt.c:rpc_clnt_sigunmask */
void unmask_blocked_signals(sigset_t *orig_sigset)
{
    unsigned long irqflags = 0;

    spin_lock_irqsave(&current->sighand->siglock, irqflags);
    current->blocked = *orig_sigset;
    recalc_sigpending();
    spin_unlock_irqrestore(&current->sighand->siglock, irqflags);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
