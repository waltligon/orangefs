/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include "pvfs2-kernel.h"
#include "pint-dev-shared.h"
#include "pvfs2-dev-proto.h"

extern kmem_cache_t *op_cache;
extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;

extern struct inode_operations pvfs2_file_inode_operations;
extern struct file_operations pvfs2_file_operations;
extern struct inode_operations pvfs2_dir_inode_operations;
extern struct file_operations pvfs2_dir_operations;
extern struct dentry_operations pvfs2_dentry_operations;

extern struct inode *pvfs2_get_custom_inode(
    struct super_block *sb,
    int mode,
    dev_t dev);

static inline int copy_attributes_to_inode(
    struct inode *inode,
    PVFS_sys_attr * attrs)
{
    int ret = -1;
    int perm_mode = 0;

    if (inode && attrs)
    {
	inode->i_uid = attrs->owner;
	inode->i_gid = attrs->group;
	inode->i_atime.tv_sec = (time_t) attrs->atime;
	inode->i_mtime.tv_sec = (time_t) attrs->mtime;
	inode->i_ctime.tv_sec = (time_t) attrs->ctime;

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

	switch (attrs->objtype)
	{
	case PVFS_TYPE_METAFILE:
	    inode->i_mode |= (S_IFREG | perm_mode);
	    inode->i_op = &pvfs2_file_inode_operations;
	    inode->i_fop = &pvfs2_file_operations;
	    ret = 0;
	    break;
	case PVFS_TYPE_DIRECTORY:
	    inode->i_mode |= (S_IFDIR | perm_mode);
	    inode->i_op = &pvfs2_dir_inode_operations;
	    inode->i_fop = &pvfs2_dir_operations;
	    ret = 0;
	    break;
	case PVFS_TYPE_SYMLINK:
	    inode->i_mode |= (S_IFLNK | perm_mode);
	    inode->i_op = &pvfs2_file_inode_operations;
	    inode->i_fop = &pvfs2_file_operations;
	    ret = 0;
	    break;
	default:
	    printk("pvfs2: copy_attributes_to_inode: got invalid "
		   "attribute type %d\n", attrs->objtype);
	}
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
    int ret = -1;
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
	    printk("pvfs2: pvfs2_inode_getattr -- "
		   "kmem_cache_alloc failed!\n");
	    return ret;
	}
	new_op->upcall.type = PVFS2_VFS_OP_GETATTR;
	new_op->upcall.req.getattr.refn = pvfs2_inode->refn;

	/* need to check downcall.status value */
	printk("Trying Getattr on handle %Ld on fsid %d\n",
	       pvfs2_inode->refn.handle, pvfs2_inode->refn.fs_id);

	/* post req and wait for request to be serviced here */
	add_op_to_request_list(new_op);
	if ((ret = wait_for_matching_downcall(new_op)) != 0)
	{
	    /*
	       NOTE: we can't free the op here unless we're SURE
	       it wasn't put on the invalidated list.
	       For now, wait_for_matching_downcall just doesn't
	       put anything on the invalidated list.
	     */
	    printk("pvfs2: pvfs2_inode_getattr -- wait failed (%x). "
		   "op invalidated (not really)\n", ret);
	    goto error_exit;
	}

	/* check what kind of goodies we got */
	if (new_op->downcall.status > -1)
	{
	    /* translate the retrieved attributes into the inode */
	    if (!copy_attributes_to_inode
		(inode, &new_op->downcall.resp.getattr.attributes))
	    {
		/* dentry is good to go! */
		ret = 0;
	    }
	}
      error_exit:
	printk("Op with tag %lu was serviced; freeing\n", new_op->tag);
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
    int ret = -1, perm_mode = 0;
    PVFS_sys_attr *attrs = NULL;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);

    if (inode)
    {
	new_op = kmem_cache_alloc(op_cache, SLAB_KERNEL);
	if (!new_op)
	{
	    return -1;
	}

	new_op->upcall.type = PVFS2_VFS_OP_SETATTR;
        new_op->upcall.req.setattr.refn = pvfs2_inode->refn;

        /* fill in all attributes that we're interested in */
        attrs = &new_op->upcall.req.setattr.attributes;
        memset(attrs, 0, sizeof(PVFS_sys_attr));

        attrs->owner = inode->i_uid;
        attrs->group = inode->i_gid;
        attrs->atime = (PVFS_time)inode->i_atime.tv_sec;
        attrs->mtime = (PVFS_time)inode->i_mtime.tv_sec;
        attrs->ctime = (PVFS_time)inode->i_ctime.tv_sec;

        perm_mode = inode->i_mode;

        if (perm_mode & S_IXOTH)
            attrs->perms |= PVFS_O_EXECUTE;
        if (perm_mode & S_IWOTH)
            attrs->perms |= PVFS_O_WRITE;
        if (perm_mode & S_IROTH)
            attrs->perms |= PVFS_O_READ;

        if (perm_mode & S_IXGRP)
            attrs->perms |= PVFS_G_EXECUTE;
        if (perm_mode & S_IWGRP)
            attrs->perms |= PVFS_G_WRITE;
        if (perm_mode & S_IRGRP)
            attrs->perms |= PVFS_G_READ;

        if (perm_mode & S_IXUSR)
            attrs->perms |= PVFS_U_EXECUTE;
        if (perm_mode & S_IWUSR)
            attrs->perms |= PVFS_U_WRITE;
        if (perm_mode & S_IRUSR)
            attrs->perms |= PVFS_U_READ;

        if (perm_mode & S_IFREG)
        {
            attrs->objtype = PVFS_TYPE_METAFILE;
        }
        else if (perm_mode & S_IFDIR)
        {
            attrs->objtype = PVFS_TYPE_DIRECTORY;
        }
        else if (perm_mode & S_IFLNK)
        {
            attrs->objtype = PVFS_TYPE_SYMLINK;
        }

        attrs->mask = PVFS_ATTR_SYS_ALL_SETABLE;

	/* post req and wait for request to be serviced here */
	add_op_to_request_list(new_op);
	if (wait_for_matching_downcall(new_op) != 0)
	{
	    /*
	       NOTE: we can't free the op here unless we're SURE
	       it wasn't put on the invalidated list.
	       For now, wait_for_matching_downcall just doesn't
	       put anything on the invalidated list.
	     */
	    printk("pvfs2: pvfs2_inode_setattr -- wait failed. "
		   "op invalidated (not really)\n");
	}

        printk("Setattr Got PVFS2 status value of %d\n",
               new_op->downcall.status);

        ret = new_op->downcall.status;

	/* when request is serviced properly, free req op struct */
	printk("Op with tag %lu was serviced; freeing\n", new_op->tag);
	op_release(new_op);
    }
    return ret;
}


static inline struct inode *pvfs2_create_file(
    struct inode *dir,
    struct dentry *dentry,
    int mode)
{
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
	strncpy(new_op->upcall.req.create.d_name,
		dentry->d_name.name, PVFS2_NAME_LEN);

	/* post req and wait for request to be serviced here */
	add_op_to_request_list(new_op);
	if (wait_for_matching_downcall(new_op) != 0)
	{
	    /*
	       NOTE: we can't free the op here unless we're SURE
	       it wasn't put on the invalidated list.
	       For now, wait_for_matching_downcall just doesn't
	       put anything on the invalidated list.
	     */
	    printk("pvfs2: pvfs2_create_file -- wait failed. "
		   "op invalidated (not really)\n");
	    goto cleanup_inode;
	}

	printk("Create Got PVFS2 handle %Ld on fsid %d\n",
	       new_op->downcall.resp.create.refn.handle,
	       new_op->downcall.resp.create.refn.fs_id);

	/*
	   set the inode private data here, and set the
	   inode number here
	 */
	if (new_op->downcall.status > -1)
	{
	    inode->i_ino =
		pvfs2_handle_to_ino(new_op->downcall.resp.create.refn.handle);
	    printk("Assigned inode new number of %d\n", (int) inode->i_ino);

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
	  cleanup_inode:
	    printk("pvfs2_create_file: An error occurred; "
		   "removing created inode\n");
	    iput(inode);
	    inode = NULL;
	}

	/* when request is serviced properly, free req op struct */
	printk("Op with tag %lu was serviced; freeing\n", new_op->tag);
	op_release(new_op);
    }
    return inode;
}

static inline struct inode *pvfs2_create_dir(
    struct inode *dir,
    struct dentry *dentry,
    int mode)
{
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
	    printk("pvfs2: pvfs2_create_dir -- kmem_cache_alloc failed!\n");
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
	strncpy(new_op->upcall.req.mkdir.d_name,
		dentry->d_name.name, PVFS2_NAME_LEN);

	printk("pvfs2: pvfs2_create_dir op initialized with type %d\n",
	       new_op->upcall.type);

	/* post req and wait for request to be serviced here */
	add_op_to_request_list(new_op);
	if (wait_for_matching_downcall(new_op) != 0)
	{
	    /*
	       NOTE: we can't free the op here unless we're SURE
	       it wasn't put on the invalidated list.
	       For now, wait_for_matching_downcall just doesn't
	       put anything on the invalidated list.
	     */
	    printk("pvfs2: pvfs2_create_dir -- wait failed. "
		   "op invalidated (not really)\n");

	    goto cleanup_inode;
	}

	/* check what kind of goodies we got */
	/* need to check downcall.status value */
	printk("Mkdir Got PVFS2 handle %Ld on fsid %d\n",
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
	    printk("Assigned inode new number of %d\n", (int) inode->i_ino);

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
	  cleanup_inode:
	    printk("pvfs2_create_dir: An error occurred; "
		   "removing created inode\n");
	    iput(inode);
	    inode = NULL;
	}

	/* when request is serviced properly, free req op struct */
	printk("Op with tag %lu was serviced; freeing\n", new_op->tag);
	op_release(new_op);
    }
    return inode;
}

/*
  create a pvfs2 entry; returns a properly populated inode
  pointer on success; NULL on failure

  if op_type is PVFS_VFS_OP_CREATE, a file is created
  if op_type is PVFS_VFS_OP_MKDIR, a directory is created

  TODO:
  if op_type is PVFS_VFS_OP_SYMLINK, a symlink is created
*/
struct inode *pvfs2_create_entry(
    struct inode *dir,
    struct dentry *dentry,
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
/*         case PVFS2_VFS_OP_LINK: */
/*         case PVFS2_VFS_OP_SYMLINK: */
	default:
	    printk("pvfs2_create_entry got a bad " "op_type (%d)\n", op_type);
	}
    }
    return NULL;
}

int pvfs2_remove_entry(
    struct inode *dir,
    struct dentry *dentry)
{
    int ret = -EINVAL;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *parent = PVFS2_I(dir);
    struct inode *inode = dentry->d_inode;

    if (inode && parent)
    {
	printk("pvfs2: pvfs2_remove_entry: Parent is %Ld | fs_id %d\n",
	       parent->refn.handle, parent->refn.fs_id);
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

	/* post req and wait for request to be serviced here */
	add_op_to_request_list(new_op);
	if ((ret = wait_for_matching_downcall(new_op)) != 0)
	{
	    /*
	       NOTE: we can't free the op here unless we're SURE
	       it wasn't put on the invalidated list.
	       For now, wait_for_matching_downcall just doesn't
	       put anything on the invalidated list.
	     */
	    printk("pvfs2: pvfs2_unlink -- wait failed (%x).  "
		   "op invalidated (not really)\n", ret);
	    goto error_exit;
	}

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
