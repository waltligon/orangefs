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
#include "pvfs2-kernel.h"
#include "pint-dev-shared.h"
#include "pvfs2-dev-proto.h"

extern kmem_cache_t *op_cache;
extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;

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

    if (inode && attrs)
    {
        pvfs2_inode = PVFS2_I(inode);

        if ((attrs->objtype == PVFS_TYPE_METAFILE) &&
            (attrs->mask & PVFS_ATTR_SYS_SIZE))
            inode->i_size = (off_t)attrs->size;
        else
            inode->i_size = 0;

	inode->i_uid = attrs->owner;
	inode->i_gid = attrs->group;
	inode->i_atime.tv_sec = (time_t) attrs->atime;
	inode->i_mtime.tv_sec = (time_t) attrs->mtime;
	inode->i_ctime.tv_sec = (time_t) attrs->ctime;

        inode->i_mode &= ~S_IXOTH;
        inode->i_mode &= ~S_IWOTH;
        inode->i_mode &= ~S_IROTH;
	if (attrs->perms & PVFS_O_EXECUTE)
	    perm_mode |= S_IXOTH;
	if (attrs->perms & PVFS_O_WRITE)
	    perm_mode |= S_IWOTH;
	if (attrs->perms & PVFS_O_READ)
	    perm_mode |= S_IROTH;

        inode->i_mode &= ~S_IXGRP;
        inode->i_mode &= ~S_IWGRP;
        inode->i_mode &= ~S_IRGRP;
	if (attrs->perms & PVFS_G_EXECUTE)
	    perm_mode |= S_IXGRP;
	if (attrs->perms & PVFS_G_WRITE)
	    perm_mode |= S_IWGRP;
	if (attrs->perms & PVFS_G_READ)
	    perm_mode |= S_IRGRP;

        inode->i_mode &= ~S_IXUSR;
        inode->i_mode &= ~S_IWUSR;
        inode->i_mode &= ~S_IRUSR;
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
                    (strlen(symname) + 1), GFP_KERNEL);
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

    if (mode & S_IFREG)
    {
        attrs->objtype = PVFS_TYPE_METAFILE;
        attrs->mask |= PVFS_ATTR_SYS_TYPE;
    }
    else if (mode & S_IFDIR)
    {
        attrs->objtype = PVFS_TYPE_DIRECTORY;
        attrs->mask |= PVFS_ATTR_SYS_TYPE;
    }
    else if (mode & S_IFLNK)
    {
        attrs->objtype = PVFS_TYPE_SYMLINK;
        attrs->mask |= PVFS_ATTR_SYS_TYPE;
    }
}

/*
  NOTE: in kernel land, we never use the
  sys_attr->link_target for anything, so don't bother
  copying it into the sys_attr object here
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

  returns 0 on success; -1 otherwise
*/
int pvfs2_inode_getattr(
    struct inode *inode)
{
    int ret = -1, retries = PVFS2_OP_RETRY_COUNT;
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
	new_op = kmem_cache_alloc(op_cache, SLAB_KERNEL);
	if (!new_op)
	{
	    pvfs2_error("pvfs2: pvfs2_inode_getattr -- "
                        "kmem_cache_alloc failed!\n");
	    return ret;
	}
	new_op->upcall.type = PVFS2_VFS_OP_GETATTR;
	new_op->upcall.req.getattr.refn = pvfs2_inode->refn;

	/* need to check downcall.status value */
	pvfs2_print("Trying Getattr on handle %Lu on fsid %d\n",
                    pvfs2_inode->refn.handle, pvfs2_inode->refn.fs_id);

        service_operation_with_timeout_retry(
            new_op, "pvfs2_inode_getattr", retries);

	/* check what kind of goodies we got */
	if (new_op->downcall.status > -1)
	{
            if (!copy_attributes_to_inode
		(inode, &new_op->downcall.resp.getattr.attributes,
                 new_op->downcall.resp.getattr.link_target))
	    {
                pvfs2_print("got good attributes (perms %d); "
                            "inode is good to go\n",
                            new_op->downcall.resp.getattr.attributes.perms);
	    }
            else
            {
                pvfs2_error("pvfs2: pvfs2_inode_getattr -- failed "
                            "to copy attributes\n");
            }
	}
        ret = new_op->downcall.status;

      error_exit:
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

	new_op = kmem_cache_alloc(op_cache, SLAB_KERNEL);
	if (!new_op)
	{
	    return -1;
	}

	new_op->upcall.type = PVFS2_VFS_OP_SETATTR;
        new_op->upcall.req.setattr.refn = pvfs2_inode->refn;
        copy_attributes_from_inode(
            inode, &new_op->upcall.req.setattr.attributes, iattr);

        service_operation_with_timeout_retry(
            new_op, "pvfs2_inode_setattr", retries);

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
    int mode)
{
    int ret = -1, retries = PVFS2_OP_RETRY_COUNT;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *parent = PVFS2_I(dir);
    pvfs2_inode_t *pvfs2_inode = NULL;
    struct inode *inode =
	pvfs2_get_custom_inode(dir->i_sb, (S_IFREG | mode), 0);

    if (inode)
    {
	new_op = kmem_cache_alloc(op_cache, SLAB_KERNEL);
	if (!new_op)
	{
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
            new_op, "pvfs2_create_file", retries);

	pvfs2_print("Create Got PVFS2 handle %Lu on fsid %d\n",
                    new_op->downcall.resp.create.refn.handle,
                    new_op->downcall.resp.create.refn.fs_id);

	/*
	   set the inode private data here, and set the
	   inode number here
	 */
	if (new_op->downcall.status > -1)
	{
	    inode->i_ino =
		pvfs2_handle_to_ino(
                    new_op->downcall.resp.create.refn.handle);
	    pvfs2_print("Assigned inode new number of %d\n",
                        (int)inode->i_ino);

	    pvfs2_inode = PVFS2_I(inode);
	    pvfs2_inode->refn = new_op->downcall.resp.create.refn;

	    /*
	       set up the dentry operations to make sure that our
	       pvfs2 specific dentry operations take effect.

	       this is exploited by defining a revalidate method to
	       be called each time a lookup is done to avoid the
	       natural caching effect of the vfs.  unfortunately,
	       client side caching isn't good for consistency across
	       nodes ;-)
	     */
	    dentry->d_op = &pvfs2_dentry_operations;

	    /* finally, add dentry with this new inode to the dcache */
	    d_add(dentry, inode);
	}
	else
	{
	  error_exit:
	    pvfs2_error("pvfs2_create_file: An error occurred; "
                        "removing created inode\n");
	    iput(inode);
	    inode = NULL;
	}

	/* when request is serviced properly, free req op struct */
	op_release(new_op);
    }
    return inode;
}

static inline struct inode *pvfs2_create_dir(
    struct inode *dir,
    struct dentry *dentry,
    int mode)
{
    int ret = -1, retries = PVFS2_OP_RETRY_COUNT;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *parent = PVFS2_I(dir);
    pvfs2_inode_t *pvfs2_inode = NULL;
    struct inode *inode =
	pvfs2_get_custom_inode(dir->i_sb, (S_IFDIR | mode), 0);

    if (inode)
    {
	new_op = kmem_cache_alloc(op_cache, SLAB_KERNEL);
	if (!new_op)
	{
	    pvfs2_error("pvfs2: pvfs2_create_dir -- "
                        "kmem_cache_alloc failed!\n");
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
            new_op, "pvfs2_create_dir", retries);

	/* check what kind of goodies we got */
	pvfs2_print("Mkdir Got PVFS2 handle %Lu on fsid %d\n",
                    new_op->downcall.resp.mkdir.refn.handle,
                    new_op->downcall.resp.mkdir.refn.fs_id);

	/*
	   set the inode private data here, and set the
	   inode number here
	 */
	if (new_op->downcall.status > -1)
	{
	    inode->i_ino =
		pvfs2_handle_to_ino(new_op->downcall.resp.mkdir.refn.handle);
	    pvfs2_print("Assigned inode new number of %d\n",
                        (int) inode->i_ino);

	    pvfs2_inode = PVFS2_I(inode);
	    pvfs2_inode->refn = new_op->downcall.resp.mkdir.refn;

	    /*
	       set up the dentry operations to make sure that our
	       pvfs2 specific dentry operations take effect.

	       this is exploited by defining a revalidate method to
	       be called each time a lookup is done to avoid the
	       natural caching effect of the vfs.  unfortunately,
	       client side caching isn't good for consistency across
	       nodes ;-)
	     */
	    dentry->d_op = &pvfs2_dentry_operations;

	    /* finally, add dentry with this new inode to the dcache */
	    d_add(dentry, inode);
	}
	else
	{
          error_exit:
	    pvfs2_error("pvfs2_create_dir: An error occurred; "
                        "removing created inode\n");
	    iput(inode);
	    inode = NULL;
	}

	/* when request is serviced properly, free req op struct */
	op_release(new_op);
    }
    return inode;
}

static inline struct inode *pvfs2_create_symlink(
    struct inode *dir,
    struct dentry *dentry,
    const char *symname,
    int mode)
{
    int ret = -1, retries = PVFS2_OP_RETRY_COUNT;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *parent = PVFS2_I(dir);
    pvfs2_inode_t *pvfs2_inode = NULL;
    struct inode *inode =
	pvfs2_get_custom_inode(dir->i_sb, (S_IFLNK | mode), 0);

    if (inode)
    {
	new_op = kmem_cache_alloc(op_cache, SLAB_KERNEL);
	if (!new_op)
	{
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
            new_op, "pvfs2_symlink_file", retries);

	pvfs2_print("Symlink Got PVFS2 handle %Lu on fsid %d\n",
                    new_op->downcall.resp.sym.refn.handle,
                    new_op->downcall.resp.sym.refn.fs_id);

	/*
	   set the inode private data here, and set the
	   inode number here
	 */
	if (new_op->downcall.status > -1)
	{
	    inode->i_ino =
		pvfs2_handle_to_ino(
                    new_op->downcall.resp.sym.refn.handle);
	    pvfs2_print("Assigned inode new number of %d\n",
                        (int)inode->i_ino);

	    pvfs2_inode = PVFS2_I(inode);
	    pvfs2_inode->refn = new_op->downcall.resp.sym.refn;

	    /*
	       set up the dentry operations to make sure that our
	       pvfs2 specific dentry operations take effect.

	       this is exploited by defining a revalidate method to
	       be called each time a lookup is done to avoid the
	       natural caching effect of the vfs.  unfortunately,
	       client side caching isn't good for consistency across
	       nodes ;-)
	     */
	    dentry->d_op = &pvfs2_dentry_operations;

	    /* finally, add dentry with this new inode to the dcache */
	    d_add(dentry, inode);
	}
	else
	{
	  error_exit:
	    pvfs2_error("pvfs2_symlink_file: An error occurred; "
                        "removing created inode\n");
	    iput(inode);
	    inode = NULL;
	}

	/* when request is serviced properly, free req op struct */
	op_release(new_op);
    }
    return inode;
}

/*
  create a pvfs2 entry; returns a properly populated inode
  pointer on success; NULL on failure

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
    int op_type)
{
    if (dir && dentry)
    {
	switch (op_type)
	{
	case PVFS2_VFS_OP_CREATE:
	    return pvfs2_create_file(dir, dentry, mode);
	case PVFS2_VFS_OP_MKDIR:
	    return pvfs2_create_dir(dir, dentry, mode);
        case PVFS2_VFS_OP_SYMLINK:
            return pvfs2_create_symlink(dir, dentry, symname, mode);
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

    if (inode && parent)
    {
	pvfs2_print("pvfs2: pvfs2_remove_entry on inode %d: "
                    "Parent is %Lu | fs_id %d\n",
                    (int)inode->i_ino, parent->refn.handle,
                    parent->refn.fs_id);
	new_op = kmem_cache_alloc(op_cache, SLAB_KERNEL);
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
            new_op, "pvfs2_remove_entry", retries);

	/*
	   the remove has no downcall members to retrieve, but
	   the status value tells us if it went through ok or not
	 */
	ret = new_op->downcall.status;

      error_exit:
	/* when request is serviced properly, free req op struct */
	op_release(new_op);
    }
    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
