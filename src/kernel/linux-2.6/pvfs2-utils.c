/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#define  __PINT_PROTO_ENCODE_OPAQUE_HANDLE
#include "pvfs2-kernel.h"
#include "pvfs2-types.h"
#include "pint-dev-shared.h"
#include "pvfs2-dev-proto.h"
#include "pvfs2-bufmap.h"
#include "pvfs2-internal.h"

int pvfs2_gen_credentials(
    PVFS_credentials *credentials)
{
    int ret = -1;

    if (credentials)
    {
        memset(credentials, 0, sizeof(PVFS_credentials));
#ifdef HAVE_CURRENT_FSUID
        credentials->uid = current_fsuid();
        credentials->gid = current_fsgid();
#else
        credentials->uid = current->fsuid;
        credentials->gid = current->fsgid;
#endif

        ret = 0;
    }
    return ret;
}

PVFS_fs_id fsid_of_op(pvfs2_kernel_op_t *op)
{
    PVFS_fs_id fsid = PVFS_FS_ID_NULL;
    if (op)
    {
        switch (op->upcall.type)
        {
            case PVFS2_VFS_OP_FILE_IO:
                fsid = op->upcall.req.io.refn.fs_id;
                break;
            case PVFS2_VFS_OP_LOOKUP:
                fsid = op->upcall.req.lookup.parent_refn.fs_id;
                break;
            case PVFS2_VFS_OP_CREATE:
                fsid = op->upcall.req.create.parent_refn.fs_id;
                break;
            case PVFS2_VFS_OP_GETATTR:
                fsid = op->upcall.req.getattr.refn.fs_id;
                break;
            case PVFS2_VFS_OP_REMOVE:
                fsid = op->upcall.req.remove.parent_refn.fs_id;
                break;
            case PVFS2_VFS_OP_MKDIR:
                fsid = op->upcall.req.mkdir.parent_refn.fs_id;
                break;
            case PVFS2_VFS_OP_READDIR:
                fsid = op->upcall.req.readdir.refn.fs_id;
                break;
            case PVFS2_VFS_OP_SETATTR:
                fsid = op->upcall.req.setattr.refn.fs_id;
                break;
            case PVFS2_VFS_OP_SYMLINK:
                fsid = op->upcall.req.sym.parent_refn.fs_id;
                break;
            case PVFS2_VFS_OP_RENAME:
                fsid = op->upcall.req.rename.old_parent_refn.fs_id;
                break;
            case PVFS2_VFS_OP_STATFS:
                fsid = op->upcall.req.statfs.fs_id;
                break;
            case PVFS2_VFS_OP_TRUNCATE:
                fsid = op->upcall.req.truncate.refn.fs_id;
                break;
            case PVFS2_VFS_OP_MMAP_RA_FLUSH:
                fsid = op->upcall.req.ra_cache_flush.refn.fs_id;
                break;
            case PVFS2_VFS_OP_FS_UMOUNT:
                fsid = op->upcall.req.fs_umount.fs_id;
                break;
            case PVFS2_VFS_OP_GETXATTR:
                fsid = op->upcall.req.getxattr.refn.fs_id;
                break;
            case PVFS2_VFS_OP_SETXATTR:
                fsid = op->upcall.req.setxattr.refn.fs_id;
                break;
            case PVFS2_VFS_OP_LISTXATTR:
                fsid = op->upcall.req.listxattr.refn.fs_id;
                break;
            case PVFS2_VFS_OP_REMOVEXATTR:
                fsid = op->upcall.req.removexattr.refn.fs_id;
                break;
            case PVFS2_VFS_OP_FSYNC:
                fsid = op->upcall.req.fsync.refn.fs_id;
                break;
            default:
                break;
        }
    }
    return fsid;
}

static void pvfs2_set_inode_flags(struct inode *inode, 
        PVFS_sys_attr *attrs)
{
    if (attrs->flags & PVFS_IMMUTABLE_FL) {
        inode->i_flags |= S_IMMUTABLE;
    }
    else {
        inode->i_flags &= ~S_IMMUTABLE;
    }
    if (attrs->flags & PVFS_APPEND_FL) {
        inode->i_flags |= S_APPEND;
    }
    else {
        inode->i_flags &= ~S_APPEND;
    }
    if (attrs->flags & PVFS_NOATIME_FL) {
        inode->i_flags |= S_NOATIME;
    }
    else {
        inode->i_flags &= ~S_NOATIME;
    }
    return;
}

/* NOTE: symname is ignored unless the inode is a sym link */
int copy_attributes_to_inode(
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
          arbitrarily set the inode block size; FIXME: we need to
          resolve the difference between the reported inode blocksize
          and the PAGE_CACHE_SIZE, since our block count will always
          be wrong.

          For now, we're setting the block count to be the proper
          number assuming the block size is 512 bytes, and the size is
          rounded up to the nearest 4K.  This is apparently required
          to get proper size reports from the 'du' shell utility.

          changing the inode->i_blkbits to something other than
          PAGE_CACHE_SHIFT breaks mmap/execution as we depend on that.
        */
#ifdef HAVE_I_BLKSIZE_IN_STRUCT_INODE
        inode->i_blksize = pvfs_bufmap_size_query();
#endif
        inode->i_blkbits = PAGE_CACHE_SHIFT;
        gossip_debug(GOSSIP_UTILS_DEBUG, "attrs->mask = %x (objtype = %s)\n", 
                attrs->mask, 
                attrs->objtype == PVFS_TYPE_METAFILE ? "file" :
                attrs->objtype == PVFS_TYPE_DIRECTORY ? "directory" :
                attrs->objtype == PVFS_TYPE_SYMLINK ? "symlink" :
                 "invalid/unknown");
                
        if (attrs->objtype == PVFS_TYPE_METAFILE)
        {
            pvfs2_set_inode_flags(inode, attrs);
            if (attrs->mask & PVFS_ATTR_SYS_SIZE)
            {
                inode_size = (loff_t)attrs->size;
                rounded_up_size =
                    (inode_size + (4096 - (inode_size % 4096)));

                pvfs2_lock_inode(inode);
#ifdef PVFS2_LINUX_KERNEL_2_4
#if (PVFS2_LINUX_KERNEL_2_4_MINOR_VER > 21)
                inode->i_bytes = inode_size;
#endif
#else
                /* this is always ok for 2.6.x */
                inode->i_bytes = inode_size;
#endif
                inode->i_blocks = (unsigned long)(rounded_up_size / 512);
                pvfs2_unlock_inode(inode);

                /*
                  NOTE: make sure all the places we're called from have
                  the inode->i_sem lock.  we're fine in 99% of the cases
                  since we're mostly called from a lookup.
                */
                inode->i_size = inode_size;
            }
        }
        else if ((attrs->objtype == PVFS_TYPE_SYMLINK) &&
                 (symname != NULL))
        {
            inode->i_size = (loff_t)strlen(symname);
        }
        else
        {
            pvfs2_lock_inode(inode);
#ifdef PVFS2_LINUX_KERNEL_2_4
#if (PVFS2_LINUX_KERNEL_2_4_MINOR_VER > 21)
            inode->i_bytes = PAGE_CACHE_SIZE;
#endif
#else
            /* always ok for 2.6.x */
            inode->i_bytes = PAGE_CACHE_SIZE;
#endif
            inode->i_blocks = (unsigned long)(PAGE_CACHE_SIZE / 512);
            pvfs2_unlock_inode(inode);

            inode->i_size = PAGE_CACHE_SIZE;
        }

        inode->i_uid = attrs->owner;
        inode->i_gid = attrs->group;
#ifdef PVFS2_LINUX_KERNEL_2_4
        inode->i_atime = (time_t)attrs->atime;
        inode->i_mtime = (time_t)attrs->mtime;
        inode->i_ctime = (time_t)attrs->ctime;
#else
        inode->i_atime.tv_sec = (time_t)attrs->atime;
        inode->i_mtime.tv_sec = (time_t)attrs->mtime;
        inode->i_ctime.tv_sec = (time_t)attrs->ctime;
        inode->i_atime.tv_nsec = 0;
        inode->i_mtime.tv_nsec = 0;
        inode->i_ctime.tv_nsec = 0;
#endif
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

        if (attrs->perms & PVFS_G_SGID)
            perm_mode |= S_ISGID;
        /* Should we honor the suid bit of the file? */
        if (get_suid_flag(inode) == 1 && (attrs->perms & PVFS_U_SUID))
            perm_mode |= S_ISUID;

        inode->i_mode = perm_mode;

        if (is_root_handle(inode))
        {
            /* special case: mark the root inode as sticky */
            inode->i_mode |= S_ISVTX;
            gossip_debug(GOSSIP_UTILS_DEBUG, "Marking inode %llu as sticky\n", 
                    llu(get_handle_from_ino(inode)));
        }

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
                /* NOTE: we have no good way to keep nlink consistent for 
                 * directories across clients; keep constant at 1.  Why 1?  If
                 * we go with 2, then find(1) gets confused and won't work
                 * properly withouth the -noleaf option */
                inode->i_nlink = 1;
                ret = 0;
                break;
            case PVFS_TYPE_SYMLINK:
                inode->i_mode |= S_IFLNK;
                inode->i_op = &pvfs2_symlink_inode_operations;
                inode->i_fop = NULL;

                /* copy link target to inode private data */
                if (pvfs2_inode && symname)
                {
                    strncpy(pvfs2_inode->link_target, symname, PVFS_NAME_MAX);
                    gossip_debug(GOSSIP_UTILS_DEBUG, "Copied attr link target %s\n",
                                pvfs2_inode->link_target);
                }
                gossip_debug(GOSSIP_UTILS_DEBUG, "symlink mode %o\n", inode->i_mode);
                ret = 0;
                break;
            default:
                gossip_err("pvfs2:copy_attributes_to_inode: got invalid "
                            "attribute type %x\n", attrs->objtype);
        }
        gossip_debug(GOSSIP_UTILS_DEBUG, "pvfs2: copy_attributes_to_inode: setting i_mode to %o, i_size to %lu\n",
                inode->i_mode, (unsigned long)pvfs2_i_size_read(inode));
    }
    return ret;
}

static inline void convert_attribute_mode_to_pvfs_sys_attr(
    int mode,
    PVFS_sys_attr *attrs,
    int suid)
{
    attrs->perms = PVFS_util_translate_mode(mode, suid);
    attrs->mask |= PVFS_ATTR_SYS_PERM;

    gossip_debug(GOSSIP_UTILS_DEBUG, "mode is %o | translated perms is %o\n", mode,
                attrs->perms);

    /* NOTE: this function only called during setattr.  Setattr must not mess
     * with object type */
}

/*
  NOTE: in kernel land, we never use the sys_attr->link_target for
  anything, so don't bother copying it into the sys_attr object here.
*/
static inline int copy_attributes_from_inode(
    struct inode *inode,
    PVFS_sys_attr *attrs,
    struct iattr *iattr)
{
    umode_t tmp_mode;

    if (!iattr || !inode || !attrs)
    {
        gossip_err("NULL iattr (%p), inode (%p), attrs (%p) in copy_attributes_from_inode!\n",
                iattr, inode, attrs);
        return -EINVAL;
    }
    /*
      we need to be careful
      to only copy the attributes out of the iattr object that we
      know are valid
    */
    attrs->mask = 0;
    if (iattr->ia_valid & ATTR_UID) 
    {
        attrs->owner = iattr->ia_uid;
        attrs->mask |= PVFS_ATTR_SYS_UID;
        gossip_debug(GOSSIP_UTILS_DEBUG, "(UID) %d\n", attrs->owner);
    }
    if (iattr->ia_valid & ATTR_GID)
    {
        attrs->group = iattr->ia_gid; 
        attrs->mask |= PVFS_ATTR_SYS_GID;
        gossip_debug(GOSSIP_UTILS_DEBUG, "(GID) %d\n", attrs->group);
    }

    if (iattr->ia_valid & ATTR_ATIME)
    {
        attrs->mask |= PVFS_ATTR_SYS_ATIME;
        if (iattr->ia_valid & ATTR_ATIME_SET)
        {
            attrs->atime = pvfs2_convert_time_field((void *)&iattr->ia_atime);
            attrs->mask |= PVFS_ATTR_SYS_ATIME_SET;
        }
    }
    if (iattr->ia_valid & ATTR_MTIME)
    {
        attrs->mask |= PVFS_ATTR_SYS_MTIME;
        if (iattr->ia_valid & ATTR_MTIME_SET)
        {
            attrs->mtime = pvfs2_convert_time_field((void *)&iattr->ia_mtime);
            attrs->mask |= PVFS_ATTR_SYS_MTIME_SET;
        }
    }
    if (iattr->ia_valid & ATTR_CTIME)
    {
        attrs->mask |= PVFS_ATTR_SYS_CTIME;
    }
    /* PVFS2 cannot set size with a setattr operation.  Probably not likely
     * to be requested through the VFS, but just in case, don't worry about
     * ATTR_SIZE */

    if (iattr->ia_valid & ATTR_MODE)
    {
        tmp_mode = iattr->ia_mode;
        if (tmp_mode & (S_ISVTX))
        {
            if (is_root_handle(inode))
            {
                /* allow sticky bit to be set on root (since it shows up that
                 * way by default anyhow), but don't show it to
                 * the server
                 */
                tmp_mode -= S_ISVTX;
            }
            else
            {
                gossip_debug(GOSSIP_UTILS_DEBUG, "User attempted to set sticky bit"
                        "on non-root directory; returning EINVAL.\n");
                return(-EINVAL);
            }
        }

        if (tmp_mode & (S_ISUID))
        {
            gossip_debug(GOSSIP_UTILS_DEBUG, "Attempting to set setuid bit "
                    "(not supported); returning EINVAL.\n");
            return(-EINVAL);
        }

        convert_attribute_mode_to_pvfs_sys_attr(
            tmp_mode, attrs, get_suid_flag(inode));
    }

    return 0;
}

/*
  issues a pvfs2 getattr request and fills in the appropriate inode
  attributes if successful.  returns 0 on success; -errno otherwise
*/
int pvfs2_inode_getattr(struct inode *inode, uint32_t getattr_mask)
{
    int ret = -EINVAL;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *pvfs2_inode = NULL;

    gossip_debug(GOSSIP_UTILS_DEBUG, "pvfs2_inode_getattr: called on inode %llu\n",
                llu(get_handle_from_ino(inode)));

    if (inode)
    {
        pvfs2_inode = PVFS2_I(inode);
        if (!pvfs2_inode)
        {
            gossip_debug(GOSSIP_UTILS_DEBUG, "%s:%s:%d failed to resolve to pvfs2_inode\n", __FILE__, __func__, __LINE__);
            return ret;
        }

        /*
           in the case of being called from s_op->read_inode, the
           pvfs2_inode private data hasn't been initialized yet, so we
           need to use the inode number as the handle and query the
           superblock for the fs_id.  Further, we assign that private
           data here.

           that call flow looks like:
           lookup --> iget --> read_inode --> here

           In the case we are doing an iget4 or an iget5_locked, there
           is no call made to read_inode so we actually have valid fields
           in pvfs2_inode->refn

           if the inode were already in the inode cache, it looks like:
           lookup --> revalidate --> here
        */
        if (pvfs2_inode->refn.handle == PVFS_HANDLE_NULL)
        {
#if defined(HAVE_IGET4_LOCKED) || defined(HAVE_IGET5_LOCKED)
            gossip_lerr("Critical error: Invalid handle despite using iget4/iget5\n");
            return -EINVAL;
#endif
            pvfs2_inode->refn.handle = get_handle_from_ino(inode);
        }
        if (pvfs2_inode->refn.fs_id == PVFS_FS_ID_NULL)
        {
#if defined(HAVE_IGET4_LOCKED) || defined(HAVE_IGET5_LOCKED)
            gossip_lerr("Critical error: Invalid fsid despite using iget4/iget5\n");
            return -EINVAL;
#endif
            pvfs2_inode->refn.fs_id = PVFS2_SB(inode->i_sb)->fs_id;
        }

        /*
           post a getattr request here; make dentry valid if getattr
           passes
        */
        new_op = op_alloc(PVFS2_VFS_OP_GETATTR);
        if (!new_op)
        {
            return -ENOMEM;
        }
        new_op->upcall.req.getattr.refn = pvfs2_inode->refn;
        new_op->upcall.req.getattr.mask = getattr_mask;

        ret = service_operation(
            new_op, "pvfs2_inode_getattr",  
            get_interruptible_flag(inode));

        /* check what kind of goodies we got */
        if (ret == 0)
        {
            if (copy_attributes_to_inode
                (inode, &new_op->downcall.resp.getattr.attributes,
                 new_op->downcall.resp.getattr.link_target))
            {
                gossip_err("pvfs2_inode_getattr: failed to copy "
                            "attributes\n");
                ret = -ENOENT;
                goto copy_attr_failure;
            }

            /* store blksize in pvfs2 specific part of inode structure; we
             * are only going to use this to report to stat to make sure it
             * doesn't perturb any inode related code paths
             */
            if(new_op->downcall.resp.getattr.attributes.objtype 
                == PVFS_TYPE_METAFILE)
            {
                pvfs2_inode->blksize = 
                   new_op->downcall.resp.getattr.attributes.blksize;
            }
            else
            {
                /* mimic behavior of generic_fillattr() for other types */
                pvfs2_inode->blksize = (1 << inode->i_blkbits);
            }
        }

      copy_attr_failure:
        gossip_debug(GOSSIP_UTILS_DEBUG, "Getattr on handle %llu, fsid %d\n  (inode ct = %d) "
                    "returned %d\n",
                    llu(pvfs2_inode->refn.handle), pvfs2_inode->refn.fs_id,
                    (int)atomic_read(&inode->i_count), ret);
        /* store error code in the inode so that we can retrieve it later if
         * needed
         */
        if(ret < 0)
        {
            pvfs2_inode->error_code = ret;
        }

        op_release(new_op);
    }
    return ret;
}

/*
  issues a pvfs2 setattr request to make sure the new attribute values
  take effect if successful.  returns 0 on success; -errno otherwise
*/
int pvfs2_inode_setattr(
    struct inode *inode,
    struct iattr *iattr)
{
    int ret = -ENOMEM;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *pvfs2_inode = NULL;

    if (inode)
    {
        pvfs2_inode = PVFS2_I(inode);

        new_op = op_alloc(PVFS2_VFS_OP_SETATTR);
        if (!new_op)
        {
            return ret;
        }

        new_op->upcall.req.setattr.refn = pvfs2_inode->refn;
        if ((new_op->upcall.req.setattr.refn.handle == PVFS_HANDLE_NULL) &&
            (new_op->upcall.req.setattr.refn.fs_id == PVFS_FS_ID_NULL))
        {
            struct super_block *sb = inode->i_sb;
            new_op->upcall.req.setattr.refn.handle =
                PVFS2_SB(sb)->root_handle;
            new_op->upcall.req.setattr.refn.fs_id =
                PVFS2_SB(sb)->fs_id;
        }
        ret = copy_attributes_from_inode(
            inode, &new_op->upcall.req.setattr.attributes, iattr);
        if(ret < 0)
        {
            op_release(new_op);
            return(ret);
        }

        ret = service_operation(
            new_op, "pvfs2_inode_setattr", 
            get_interruptible_flag(inode));

        gossip_debug(GOSSIP_UTILS_DEBUG, "pvfs2_inode_setattr: returning %d\n", ret);

        /* when request is serviced properly, free req op struct */
        op_release(new_op);

        /* successful setattr should clear the atime, mtime and ctime flags */
        if (ret == 0) {
            ClearAtimeFlag(pvfs2_inode);
            ClearMtimeFlag(pvfs2_inode);
            ClearCtimeFlag(pvfs2_inode);
            ClearModeFlag(pvfs2_inode);
        }
    }
    return ret;
}

int pvfs2_flush_inode(struct inode *inode)
{
    /*
     * If it is a dirty inode, this function gets called.
     * Gather all the information that needs to be setattr'ed
     * Right now, this will only be used for mode, atime, mtime 
     * and/or ctime.
     */
    struct iattr wbattr;
    int ret;
    int mtime_flag, ctime_flag, atime_flag, mode_flag;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);
    memset(&wbattr, 0, sizeof(wbattr));

    /* check inode flags up front, and clear them if they are set.  This
     * will prevent multiple processes from all trying to flush the same
     * inode if they call close() simultaneously
     */
    mtime_flag = MtimeFlag(pvfs2_inode);
    ClearMtimeFlag(pvfs2_inode);
    ctime_flag = CtimeFlag(pvfs2_inode);
    ClearCtimeFlag(pvfs2_inode);
    atime_flag = AtimeFlag(pvfs2_inode);
    ClearAtimeFlag(pvfs2_inode);
    mode_flag = ModeFlag(pvfs2_inode);
    ClearModeFlag(pvfs2_inode);

    /*  -- Lazy atime,mtime and ctime update --
     * Note: all times are dictated by server in the new scheme 
     * and not by the clients
     *
     * Also mode updates are being handled now..
     */

    if (mtime_flag)
        wbattr.ia_valid |= ATTR_MTIME;
    if (ctime_flag)
        wbattr.ia_valid |= ATTR_CTIME;
    /*
     * We do not need to honor atime flushes if
     * a) object has a noatime marker
     * b) object is a directory and has a nodiratime marker on the fs
     * c) entire file system is mounted with noatime option
     */

    if (!((inode->i_flags & S_NOATIME)
            || (inode->i_sb->s_flags & MS_NOATIME)
            || ((inode->i_sb->s_flags & MS_NODIRATIME) && S_ISDIR(inode->i_mode))) && atime_flag)
    {
        wbattr.ia_valid |= ATTR_ATIME;
    }
    if (mode_flag) 
    {
        wbattr.ia_mode = inode->i_mode;
        wbattr.ia_valid |= ATTR_MODE;
    }

    gossip_debug(GOSSIP_UTILS_DEBUG, "*********** pvfs2_flush_inode: %llu "
            "(ia_valid %d)\n", llu(get_handle_from_ino(inode)), wbattr.ia_valid);
    if (wbattr.ia_valid == 0)
    {
        gossip_debug(GOSSIP_UTILS_DEBUG, "pvfs2_flush_inode skipping setattr()\n");
        return 0;
    }
        
    gossip_debug(GOSSIP_UTILS_DEBUG, "pvfs2_flush_inode (%llu) writing mode %o\n",
        llu(get_handle_from_ino(inode)), inode->i_mode);

    ret = pvfs2_inode_setattr(inode, &wbattr);
    return ret;
}

/* metafile distribution */
#define DIST_KEY    "system.pvfs2." METAFILE_DIST_KEYSTR
/* datafile handles */
#define DFILE_KEY   "system.pvfs2." DATAFILE_HANDLES_KEYSTR
/* symlink */
#define SYMLINK_KEY "system.pvfs2." SYMLINK_TARGET_KEYSTR
/* root handle */  
#define ROOT_KEY    "system.pvfs2." ROOT_HANDLE_KEYSTR
/* directory entry key */
#define DIRENT_KEY  "system.pvfs2." DIRECTORY_ENTRY_KEYSTR

/* Extended attributes helper functions */
static char *xattr_non_zero_terminated[] = {
    DFILE_KEY,
    DIST_KEY,
    ROOT_KEY,
};

/* Extended attributes helper functions */

/*
 * this function returns 
 * 0 if the val corresponding to name is known to be not terminated with an explicit \0
 * 1 if the val corresponding to name is known to be \0 terminated
 */
static int xattr_zero_terminated(const char *name)
{
    int i;
    static int xattr_count = sizeof(xattr_non_zero_terminated)/sizeof(char *);
    for (i = 0;i < xattr_count; i++)
    {
        if (strcmp(name, xattr_non_zero_terminated[i]) == 0)
            return 0;
    }
    return 1;
}

static char *xattr_resvd_keys[] = {
    DFILE_KEY,
    DIST_KEY,
    DIRENT_KEY,
    SYMLINK_KEY,
    ROOT_KEY,
};
/*
 * this function returns
 * 0 if the key corresponding to name is not meant to be printed as part of a listxattr
 * 1 if the key corresponding to name is meant to be returned as part of a listxattr.
 * Currently xattr_resvd_keys[] is the array that holds the reserved entries
 */
static int is_reserved_key(const char *key, size_t size)
{
    int i;
    static int resv_count = sizeof(xattr_resvd_keys)/sizeof(char *);
    for (i = 0; i < resv_count; i++) 
    {
        if (strncmp(key, xattr_resvd_keys[i], size) == 0)
            return 1;
    }
    return 0;
}

/*
 * Tries to get a specified key's attributes of a given
 * file into a user-specified buffer. Note that the getxattr
 * interface allows for the users to probe the size of an
 * extended attribute by passing in a value of 0 to size.
 * Thus our return value is always the size of the attribute
 * unless the key does not exist for the file and/or if
 * there were errors in fetching the attribute value.
 */
ssize_t pvfs2_inode_getxattr(struct inode *inode, const char* prefix,
    const char *name, void *buffer, size_t size)
{
    ssize_t ret = -ENOMEM;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *pvfs2_inode = NULL;
    ssize_t length = 0;
    int fsuid, fsgid;

    if (name == NULL || (size > 0 && buffer == NULL))
    {
        gossip_err("pvfs2_inode_getxattr: bogus NULL pointers\n");
        return -EINVAL;
    }
    if (size < 0 || (strlen(name)+strlen(prefix)) >= PVFS_MAX_XATTR_NAMELEN)
    {
        gossip_err("Invalid size (%d) or key length (%d)\n", 
                (int) size, (int)(strlen(name)+strlen(prefix)));
        return -EINVAL;
    }
    if (inode)
    {
#ifdef HAVE_CURRENT_FSUID
        fsuid = current_fsuid();
        fsgid = current_fsgid();
#else
        fsuid = current->fsuid;
        fsgid = current->fsgid;
#endif

        gossip_debug(GOSSIP_XATTR_DEBUG, "getxattr on inode %llu, name %s (uid %o, gid %o)\n", 
                llu(get_handle_from_ino(inode)), name, fsuid, fsgid);
        pvfs2_inode = PVFS2_I(inode);
        /* obtain the xattr semaphore */
        down_read(&pvfs2_inode->xattr_sem);

        new_op = op_alloc(PVFS2_VFS_OP_GETXATTR);
        if (!new_op)
        {
            up_read(&pvfs2_inode->xattr_sem);
            return ret;
        }

        new_op->upcall.req.getxattr.refn = pvfs2_inode->refn;
        ret = snprintf((char*)new_op->upcall.req.getxattr.key,
            PVFS_MAX_XATTR_NAMELEN, "%s%s", prefix, name);
        /* 
         * NOTE: Although keys are meant to be NULL terminated textual strings,
         * I am going to explicitly pass the length just in case we change this
         * later on...
         */
        new_op->upcall.req.getxattr.key_sz = ret + 1;

        ret = service_operation(
            new_op, "pvfs2_inode_getxattr",  
            get_interruptible_flag(inode));

        /* Upon success, we need to get the value length
         * from downcall and return that.
         * and also copy the value out to the requester
         */
        if (ret == 0)
        {
            ssize_t new_length;
            length = new_op->downcall.resp.getxattr.val_sz;
            /*
             * if the xattr corresponding to name was not terminated with a \0
             * then we return the entire response length
             */
            if (xattr_zero_terminated(name) == 0)
            {
                new_length = length;
            }
            /*
             * if it was terminated by a \0 then we return 1 less for the getfattr
             * programs to play nicely with displaying it
             */
            else {
                new_length = length - 1;
            }
            /* Just return the length of the queried attribute after
             * subtracting the \0 thingie */
            if (size == 0)
            {
                ret = new_length;
            }
            else
            {
                /* check to see if key length is > provided buffer size */
                if (new_length > size)
                {
                    ret = -ERANGE;
                }
                else
                {
                    /* No size problems */
                    memset(buffer, 0, size);
                    memcpy(buffer, new_op->downcall.resp.getxattr.val, 
                            new_length);
                    ret = new_length;
                    gossip_debug(GOSSIP_XATTR_DEBUG, "pvfs2_inode_getxattr: inode %llu key %s "
                            " key_sz %d, val_length %d\n", 
                        llu(get_handle_from_ino(inode)),
                        (char*)new_op->upcall.req.getxattr.key, 
                        (int) new_op->upcall.req.getxattr.key_sz, (int) ret);
                }
            }
        }
        else if (ret == -ENOENT)
        {
            ret = -ENODATA; /* if no such keys exists we set this to be errno */
            gossip_debug(GOSSIP_XATTR_DEBUG, "pvfs2_inode_getxattr: inode %llu key %s does not exist!\n",
                    llu(get_handle_from_ino(inode)), (char *) new_op->upcall.req.getxattr.key);
        }

        /* when request is serviced properly, free req op struct */
        op_release(new_op);
        up_read(&pvfs2_inode->xattr_sem);
    }
    return ret;
}

/*
 * tries to set an attribute for a given key on a file.
 * Returns a -ve number on error and 0 on success.
 * Key is text, but value can be binary!
 */
int pvfs2_inode_setxattr(struct inode *inode, const char* prefix, 
    const char *name, const void *value, size_t size, int flags)
{
    int ret = -ENOMEM;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *pvfs2_inode = NULL;

    if (size < 0 || size >= PVFS_MAX_XATTR_VALUELEN || flags < 0)
    {
        gossip_err("pvfs2_inode_setxattr: bogus values of size(%d), flags(%d)\n", 
                (int) size, flags);
        return -EINVAL;
    }
    if (name == NULL || (size > 0 && value == NULL))
    {
        gossip_err("pvfs2_inode_setxattr: bogus NULL pointers!\n");
        return -EINVAL;
    }

    if (prefix)
    {
        if(strlen(name)+strlen(prefix) >= PVFS_MAX_XATTR_NAMELEN)
        {
		gossip_err("pvfs2_inode_setxattr: bogus key size (%d)\n", 
				(int)(strlen(name)+strlen(prefix)));
		return -EINVAL;
	}
    }
    else
    {
        if(strlen(name) >= PVFS_MAX_XATTR_NAMELEN)
        {
		gossip_err("pvfs2_inode_setxattr: bogus key size (%d)\n",
			   (int)(strlen(name)));
                return -EINVAL;
        }
    }

    /* This is equivalent to a removexattr */
    if (size == 0 && value == NULL)
    {
        gossip_debug(GOSSIP_XATTR_DEBUG, "removing xattr (%s%s)\n", prefix, name);
        return pvfs2_inode_removexattr(inode, prefix, name, flags);
    }
    if (inode)
    {
        gossip_debug(GOSSIP_XATTR_DEBUG, "setxattr on inode %llu, name %s\n", 
                llu(get_handle_from_ino(inode)), name);
        if (IS_RDONLY(inode))
        {
            gossip_err("pvfs2_inode_setxattr: Read-only file system\n");
            return -EROFS;
        }
        if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
        {
            gossip_err("pvfs2_inode_setxattr: Immutable inode or append-only "
                    "inode; operation not permitted\n");
            return -EPERM;
        }
        pvfs2_inode = PVFS2_I(inode);

        down_write(&pvfs2_inode->xattr_sem);
        new_op = op_alloc(PVFS2_VFS_OP_SETXATTR);
        if (!new_op)
        {
            up_write(&pvfs2_inode->xattr_sem);
            return ret;
        }

        new_op->upcall.req.setxattr.refn = pvfs2_inode->refn;
        new_op->upcall.req.setxattr.flags = flags;
        /* 
         * NOTE: Although keys are meant to be NULL terminated textual strings,
         * I am going to explicitly pass the length just in case we change this
         * later on...
         */
        ret = snprintf((char*)new_op->upcall.req.setxattr.keyval.key,
            PVFS_MAX_XATTR_NAMELEN, "%s%s", prefix, name);
        new_op->upcall.req.setxattr.keyval.key_sz = 
            ret + 1;
        memcpy(new_op->upcall.req.setxattr.keyval.val, value, size);
        new_op->upcall.req.setxattr.keyval.val[size] = '\0';
        /* For some reason, val_sz should include the \0 at the end as well */
        new_op->upcall.req.setxattr.keyval.val_sz = size + 1;

        gossip_debug(GOSSIP_XATTR_DEBUG, "pvfs2_inode_setxattr: key %s, key_sz %d "
                " value size %zd\n", 
                 (char*)new_op->upcall.req.setxattr.keyval.key, 
                 (int) new_op->upcall.req.setxattr.keyval.key_sz,
                 size + 1);

        ret = service_operation(
            new_op, "pvfs2_inode_setxattr", 
            get_interruptible_flag(inode));

        gossip_debug(GOSSIP_XATTR_DEBUG, "pvfs2_inode_setxattr: returning %d\n", ret);

        /* when request is serviced properly, free req op struct */
        op_release(new_op);
        up_write(&pvfs2_inode->xattr_sem);
    }
    return ret;
}

int pvfs2_inode_removexattr(struct inode *inode, const char* prefix, 
    const char *name, int flags)
{
    int ret = -ENOMEM;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *pvfs2_inode = NULL;

    if(!name)
    {
	gossip_err("pvfs2_inode_removexattr: xattr key is NULL\n");
        return -EINVAL;
    }

    if (prefix)
    {
        if((strlen(name)+strlen(prefix)) >= PVFS_MAX_XATTR_NAMELEN)
        {
		gossip_err("pvfs2_inode_removexattr: Invalid key length(%d)\n", 
				(int)(strlen(name)+strlen(prefix)));
		return -EINVAL;
	}
    }
    else
    {
        if(strlen(name) >= PVFS_MAX_XATTR_NAMELEN)
        {
		gossip_err("pvfs2_inode_removexattr: Invalid key length(%d)\n",
			   (int)(strlen(name)));
                return -EINVAL;
        }
    }

    if (inode)
    {
        pvfs2_inode = PVFS2_I(inode);

        down_write(&pvfs2_inode->xattr_sem);
        new_op = op_alloc(PVFS2_VFS_OP_REMOVEXATTR);
        if (!new_op)
        {
            up_write(&pvfs2_inode->xattr_sem);
            return ret;
        }

        new_op->upcall.req.removexattr.refn = pvfs2_inode->refn;
        /* 
         * NOTE: Although keys are meant to be NULL terminated textual strings,
         * I am going to explicitly pass the length just in case we change this
         * later on...
         */
        ret = snprintf((char*)new_op->upcall.req.removexattr.key,
            PVFS_MAX_XATTR_NAMELEN, "%s%s", 
            (prefix ? prefix : ""), name);
        new_op->upcall.req.removexattr.key_sz = ret + 1;

        gossip_debug(GOSSIP_XATTR_DEBUG, "pvfs2_inode_removexattr: key %s, key_sz %d\n", 
                (char*)new_op->upcall.req.removexattr.key, 
                (int) new_op->upcall.req.removexattr.key_sz);

        ret = service_operation(
            new_op, "pvfs2_inode_removexattr", 
            get_interruptible_flag(inode));

        if (ret == -ENOENT)
        {
            /* Request to replace a non-existent attribute is an error */
            if (flags & XATTR_REPLACE)
                ret = -ENODATA;
            else
                ret = 0;
        }
        gossip_debug(GOSSIP_XATTR_DEBUG, "pvfs2_inode_removexattr: returning %d\n",
                ret);

        /* when request is serviced properly, free req op struct */
        op_release(new_op);
        up_write(&pvfs2_inode->xattr_sem);
    }
    return ret;
}

/*
 * Tries to get a specified object's keys into a user-specified
 * buffer of a given size.
 * Note that like the previous instances of xattr routines,
 * this also allows you to pass in a NULL pointer and 0 size
 * to probe the size for subsequent memory allocations.
 * Thus our return value is always the size of all the keys
 * unless there were errors in fetching the keys!
 */
int pvfs2_inode_listxattr(struct inode *inode, char *buffer, size_t size)
{
    ssize_t ret = -ENOMEM, total = 0;
    int i = 0, count_keys = 0;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *pvfs2_inode = NULL;
    ssize_t length = 0;

    if (size > 0 && buffer == NULL)
    {
        gossip_err("pvfs2_inode_listxattr: bogus NULL pointers\n");
        return -EINVAL;
    }
    if (size < 0)
    {
        gossip_err("Invalid size (%d)\n", (int) size);
        return -EINVAL;
    }
    if (inode)
    {
        PVFS_ds_position token = PVFS_ITERATE_START;

        pvfs2_inode = PVFS2_I(inode);
        /* obtain the xattr semaphore */
        down_read(&pvfs2_inode->xattr_sem);

        new_op = op_alloc(PVFS2_VFS_OP_LISTXATTR);
        if (!new_op)
        {
            up_read(&pvfs2_inode->xattr_sem);
            return ret;
        }
        if (buffer && size > 0)
        {
            memset(buffer, 0, size);
        }
    try_again:
        new_op->upcall.req.listxattr.refn = pvfs2_inode->refn;
        new_op->upcall.req.listxattr.token = token;
        new_op->upcall.req.listxattr.requested_count = (size == 0) ? 0 : PVFS_MAX_XATTR_LISTLEN;
        ret = service_operation(
                new_op, "pvfs2_inode_listxattr", 
                get_interruptible_flag(inode));
        if (ret == 0)
        {
            if (size == 0)
            {
                /*
                 * This is a bit of a big upper limit, but I did not want to spend too 
                 * much time getting this correct, since users end up allocating memory
                 * rather than us...
                 */
                total = new_op->downcall.resp.listxattr.returned_count * PVFS_MAX_XATTR_NAMELEN;
                goto done;
            }
            length = new_op->downcall.resp.listxattr.keylen;
            if (length == 0)
            {
                goto done;
            }
            else 
            {
                int key_size = 0;
                /* check to see how much can be fit in the buffer. fit only whole keys */
                for (i = 0; i < new_op->downcall.resp.listxattr.returned_count; i++)
                {
                    if (total + new_op->downcall.resp.listxattr.lengths[i] <= size)
                    {
                        /* Since many dumb programs try to setxattr() on our reserved xattrs
                         * this is a feeble attempt at defeating those by not listing them
                         * in the output of listxattr.. sigh
                         */

                        if (is_reserved_key(new_op->downcall.resp.listxattr.key + key_size, 
                                            new_op->downcall.resp.listxattr.lengths[i]) == 0)
                        {
                            gossip_debug(GOSSIP_XATTR_DEBUG, "Copying key %d -> %s\n", 
                                    i, new_op->downcall.resp.listxattr.key + key_size);
                            memcpy(buffer + total, new_op->downcall.resp.listxattr.key + key_size,
                                    new_op->downcall.resp.listxattr.lengths[i]);
                            total += new_op->downcall.resp.listxattr.lengths[i];
                            count_keys++;
                        }
                        else {
                            gossip_debug(GOSSIP_XATTR_DEBUG, "[RESERVED] key %d -> %s\n", 
                                    i, new_op->downcall.resp.listxattr.key + key_size);
                        }
                        key_size += new_op->downcall.resp.listxattr.lengths[i];
                    }
                    else {
                        goto done;
                    }
                }
                /* Since the buffer was large enough, we might have to continue fetching more keys! */
                token = new_op->downcall.resp.listxattr.token;
                if (token != PVFS_ITERATE_END)
                    goto try_again;
            }
        }
    done:
        gossip_debug(GOSSIP_XATTR_DEBUG, "pvfs2_inode_listxattr: returning %d [size of buffer %ld] "
                "(filled in %d keys)\n",
                ret ? (int) ret : (int) total, (long) size, count_keys);
        /* when request is serviced properly, free req op struct */
        op_release(new_op);
        up_read(&pvfs2_inode->xattr_sem);
        if (ret == 0)
            ret = total;
    }
    return ret;
}

static inline struct inode *pvfs2_create_file(
    struct inode *dir,
    struct dentry *dentry,
    int mode,
    int *error_code)
{
    int ret = -1;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *parent = PVFS2_I(dir);
    struct inode *inode = NULL;

    new_op = op_alloc(PVFS2_VFS_OP_CREATE);
    if (!new_op)
    {
        *error_code = -ENOMEM;
        return NULL;
    }

    if (parent && parent->refn.handle != PVFS_HANDLE_NULL && parent->refn.fs_id != PVFS_FS_ID_NULL)
    {
        new_op->upcall.req.create.parent_refn = parent->refn;
    }
    else
    {
#if defined(HAVE_IGET5_LOCKED) || defined(HAVE_IGET4_LOCKED)
        gossip_lerr("Critical error: i_ino cannot be relied on when using iget4/5\n");
        *error_code = -EINVAL;
        op_release(new_op);
        return NULL;
#endif
        new_op->upcall.req.create.parent_refn.handle =
            get_handle_from_ino(dir);
        new_op->upcall.req.create.parent_refn.fs_id =
            PVFS2_SB(dir->i_sb)->fs_id;
    }

    /* macro defined in pvfs2-kernel.h */
    fill_default_sys_attrs(new_op->upcall.req.create.attributes,
                           PVFS_TYPE_METAFILE, mode);

    strncpy(new_op->upcall.req.create.d_name,
            dentry->d_name.name, PVFS2_NAME_LEN);

    ret = service_operation(
        new_op, "pvfs2_create_file", 
        get_interruptible_flag(dir));

    gossip_debug(GOSSIP_UTILS_DEBUG, "Create Got PVFS2 handle %llu on fsid %d (ret=%d)\n",
                llu(new_op->downcall.resp.create.refn.handle),
                new_op->downcall.resp.create.refn.fs_id, ret);

    if (ret > -1)
    {
        inode = pvfs2_get_custom_inode(
            dir->i_sb, dir, (S_IFREG | mode), 0, new_op->downcall.resp.create.refn);
        if (!inode)
        {
            gossip_err("*** Failed to allocate pvfs2 file inode\n");
            op_release(new_op);
            *error_code = -ENOMEM;
            return NULL;
        }

        gossip_debug(GOSSIP_UTILS_DEBUG, "Assigned file inode new number of %llu\n",
                    llu(get_handle_from_ino(inode)));
        /* finally, add dentry with this new inode to the dcache */
        gossip_debug(GOSSIP_UTILS_DEBUG, "pvfs2_create_file: Instantiating\n *negative* "
                    "dentry %p for %s\n", dentry,
                    dentry->d_name.name);

        dentry->d_op = &pvfs2_dentry_operations;
        d_instantiate(dentry, inode);
        gossip_debug(GOSSIP_UTILS_DEBUG, "Inode (Regular File) %llu -> %s\n",
                llu(get_handle_from_ino(inode)), dentry->d_name.name);
    }
    else
    {
        *error_code = ret;

        gossip_debug(GOSSIP_UTILS_DEBUG, "pvfs2_create_file: failed with error code %d\n",
                    *error_code);
    }

    op_release(new_op);
    return inode;
}

static inline struct inode *pvfs2_create_dir(
    struct inode *dir,
    struct dentry *dentry,
    int mode,
    int *error_code)
{
    int ret = -1;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *parent = PVFS2_I(dir);
    struct inode *inode = NULL;

    new_op = op_alloc(PVFS2_VFS_OP_MKDIR);
    if (!new_op)
    {
        *error_code = -ENOMEM;
        return NULL;
    }

    if (parent && parent->refn.handle != PVFS_HANDLE_NULL && parent->refn.fs_id != PVFS_FS_ID_NULL)
    {
        new_op->upcall.req.mkdir.parent_refn = parent->refn;
    }
    else
    {
#if defined(HAVE_IGET5_LOCKED) || defined(HAVE_IGET4_LOCKED)
        gossip_lerr("Critical error: i_ino cannot be relied on when using iget4/5\n");
        *error_code = -EINVAL;
        op_release(new_op);
        return NULL;
#endif
        new_op->upcall.req.mkdir.parent_refn.handle =
            get_handle_from_ino(dir);
        new_op->upcall.req.mkdir.parent_refn.fs_id =
            PVFS2_SB(dir->i_sb)->fs_id;
    }

    /* macro defined in pvfs2-kernel.h */
    fill_default_sys_attrs(new_op->upcall.req.mkdir.attributes,
                           PVFS_TYPE_DIRECTORY, mode);

    strncpy(new_op->upcall.req.mkdir.d_name,
            dentry->d_name.name, PVFS2_NAME_LEN);

    ret = service_operation(
        new_op, "pvfs2_create_dir", 
        get_interruptible_flag(dir));

    gossip_debug(GOSSIP_UTILS_DEBUG, "Mkdir Got PVFS2 handle %llu on fsid %d\n",
                llu(new_op->downcall.resp.mkdir.refn.handle),
                new_op->downcall.resp.mkdir.refn.fs_id);

    if (ret > -1)
    {
        inode = pvfs2_get_custom_inode(
            dir->i_sb, dir, (S_IFDIR | mode), 0, new_op->downcall.resp.mkdir.refn);
        if (!inode)
        {
            gossip_err("*** Failed to allocate pvfs2 dir inode\n");
            op_release(new_op);
            *error_code = -ENOMEM;
            return NULL;
        }

        gossip_debug(GOSSIP_UTILS_DEBUG, "Assigned dir inode new number of %llu\n",
                    llu(get_handle_from_ino(inode)));
        /* finally, add dentry with this new inode to the dcache */
        gossip_debug(GOSSIP_UTILS_DEBUG, "pvfs2_create_dir: Instantiating\n  *negative* "
                    "dentry %p for %s\n", dentry,
                    dentry->d_name.name);

        dentry->d_op = &pvfs2_dentry_operations;
        d_instantiate(dentry, inode);
        gossip_debug(GOSSIP_UTILS_DEBUG, "Inode (Directory) %llu -> %s\n",
                llu(get_handle_from_ino(inode)), dentry->d_name.name);
    }
    else
    {
        *error_code = ret;

        gossip_debug(GOSSIP_UTILS_DEBUG, "pvfs2_create_dir: failed with error code %d\n",
                    *error_code);
    }

    op_release(new_op);
    return inode;
}

static inline struct inode *pvfs2_create_symlink(
    struct inode *dir,
    struct dentry *dentry,
    const char *symname,
    int mode,
    int *error_code)
{
    int ret = -1;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *parent = PVFS2_I(dir);
    struct inode *inode = NULL;

    if(!symname)
    {
	*error_code = -EINVAL;
        return NULL;
    }

    new_op = op_alloc(PVFS2_VFS_OP_SYMLINK);
    if (!new_op)
    {
        *error_code = -ENOMEM;
        return NULL;
    }

    if (parent && parent->refn.handle != PVFS_HANDLE_NULL && parent->refn.fs_id != PVFS_FS_ID_NULL)
    {
        new_op->upcall.req.sym.parent_refn = parent->refn;
    }
    else
    {
#if defined(HAVE_IGET5_LOCKED) || defined(HAVE_IGET4_LOCKED)
        gossip_lerr("Critical error: i_ino cannot be relied on when using iget4/5\n");
        *error_code = -EINVAL;
        op_release(new_op);
        return NULL;
#endif
        new_op->upcall.req.sym.parent_refn.handle =
            get_handle_from_ino(dir);
        new_op->upcall.req.sym.parent_refn.fs_id =
            PVFS2_SB(dir->i_sb)->fs_id;
    }

    /* macro defined in pvfs2-kernel.h */
    fill_default_sys_attrs(new_op->upcall.req.sym.attributes,
                           PVFS_TYPE_SYMLINK, mode);

    strncpy(new_op->upcall.req.sym.entry_name, dentry->d_name.name,
            PVFS2_NAME_LEN);
    strncpy(new_op->upcall.req.sym.target, symname, PVFS2_NAME_LEN);

    ret = service_operation(
        new_op, "pvfs2_symlink_file", 
        get_interruptible_flag(dir));

    gossip_debug(GOSSIP_UTILS_DEBUG, "Symlink Got PVFS2 handle %llu on fsid %d (ret=%d)\n",
                llu(new_op->downcall.resp.sym.refn.handle),
                new_op->downcall.resp.sym.refn.fs_id, ret);

    if (ret > -1)
    {
        inode = pvfs2_get_custom_inode(
            dir->i_sb, dir, (S_IFLNK | mode), 0, new_op->downcall.resp.sym.refn);
        if (!inode)
        {
            gossip_err("*** Failed to allocate pvfs2 symlink inode\n");
            op_release(new_op);
            *error_code = -ENOMEM;
            return NULL;
        }

        gossip_debug(GOSSIP_UTILS_DEBUG, "Assigned symlink inode new number of %llu\n",
                    llu(get_handle_from_ino(inode)));

        /* finally, add dentry with this new inode to the dcache */
        gossip_debug(GOSSIP_UTILS_DEBUG, "pvfs2_create_symlink: Instantiating\n  "
                    "*negative* dentry %p for %s\n", dentry,
                    dentry->d_name.name);

        dentry->d_op = &pvfs2_dentry_operations;
        d_instantiate(dentry, inode);
        gossip_debug(GOSSIP_UTILS_DEBUG, "Inode (Symlink) %llu -> %s\n",
                llu(get_handle_from_ino(inode)), dentry->d_name.name);
    }
    else
    {
        *error_code = ret;

        gossip_debug(GOSSIP_UTILS_DEBUG, "pvfs2_create_symlink: failed with error code %d\n",
                    *error_code);
    }

    op_release(new_op);
    return inode;
}

/*
  create a pvfs2 entry; returns a properly populated inode
  pointer on success; NULL on failure.

  the required error_code value will contain an error code ONLY if an
  error occurs (i.e. NULL is returned) and is set to 0 otherwise.

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
        if(strlen(dentry->d_name.name) > (PVFS2_NAME_LEN - 1))
        {
            *error_code = -ENAMETOOLONG;
            return(NULL);
        }

        switch (op_type)
        {
            case PVFS2_VFS_OP_CREATE:
                return pvfs2_create_file(
                    dir, dentry, mode, error_code);
            case PVFS2_VFS_OP_MKDIR:
                return pvfs2_create_dir(
                    dir, dentry, mode, error_code);
            case PVFS2_VFS_OP_SYMLINK:
                return pvfs2_create_symlink(
                    dir, dentry, symname, mode, error_code);
        }
    }

    if (error_code)
    {
        gossip_err("pvfs2_create_entry: invalid op_type %d\n", op_type);
        *error_code = -EINVAL;
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

    if (inode && parent && dentry)
    {
        gossip_debug(GOSSIP_UTILS_DEBUG, "pvfs2_remove_entry: called on %s\n  (inode %llu): "
                    "Parent is %llu | fs_id %d\n", dentry->d_name.name,
                    llu(get_handle_from_ino(inode)), llu(parent->refn.handle),
                    parent->refn.fs_id);

        new_op = op_alloc(PVFS2_VFS_OP_REMOVE);
        if (!new_op)
        {
            return -ENOMEM;
        }

        if (parent && parent->refn.handle != PVFS_HANDLE_NULL && parent->refn.fs_id != PVFS_FS_ID_NULL)
        {
            new_op->upcall.req.remove.parent_refn = parent->refn;
        }
        else
        {
#if defined(HAVE_IGET5_LOCKED) || defined(HAVE_IGET4_LOCKED)
            gossip_lerr("Critical error: i_ino cannot be relied on when using iget4/5\n");
            op_release(new_op);
            return -ENOMEM;
#endif
            new_op->upcall.req.remove.parent_refn.handle =
                get_handle_from_ino(dir);
            new_op->upcall.req.remove.parent_refn.fs_id =
                PVFS2_SB(dir->i_sb)->fs_id;
        }
        strncpy(new_op->upcall.req.remove.d_name,
                dentry->d_name.name, PVFS2_NAME_LEN);

        ret = service_operation(
            new_op, "pvfs2_remove_entry", 
            get_interruptible_flag(inode));

        /* when request is serviced properly, free req op struct */
        op_release(new_op);
    }
    return ret;
}

int pvfs2_truncate_inode(
    struct inode *inode,
    loff_t size)
{
    int ret = -EINVAL;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);
    pvfs2_kernel_op_t *new_op = NULL;

    gossip_debug(GOSSIP_UTILS_DEBUG, "pvfs2: pvfs2_truncate_inode %llu: "
                "Handle is %llu | fs_id %d | size is %lu\n",
                llu(get_handle_from_ino(inode)), llu(pvfs2_inode->refn.handle),
                pvfs2_inode->refn.fs_id, (unsigned long)size);

    new_op = op_alloc(PVFS2_VFS_OP_TRUNCATE);
    if (!new_op)
    {
        return -ENOMEM;
    }
    new_op->upcall.req.truncate.refn = pvfs2_inode->refn;
    new_op->upcall.req.truncate.size = (PVFS_size)size;

    ret = service_operation(
        new_op, "pvfs2_truncate_inode",  
        get_interruptible_flag(inode));

    /*
      the truncate has no downcall members to retrieve, but
      the status value tells us if it went through ok or not
    */
    gossip_debug(GOSSIP_UTILS_DEBUG, "pvfs2: pvfs2_truncate got return value of %d\n",ret);

    op_release(new_op);

    return ret;
}

#ifdef HAVE_FIND_INODE_HANDLE_SUPER_OPERATIONS

typedef enum {
    HANDLE_CHECK_LENGTH = 1,
    HANDLE_CHECK_MAGIC  = 2,
    HANDLE_CHECK_FSID   = 4,
} handle_check_t;

/* Perform simple sanity checks on the obtained handle */
static inline int perform_handle_checks(const struct file_handle *fhandle,
        handle_check_t check, void *p)
{
    if (!fhandle)
    {
        return -EINVAL;
    }
    /* okay good. now check if magic_nr matches */
    if (check & HANDLE_CHECK_LENGTH)
    {
        /* Make sure that handle length matches our opaque handle structure */
        if (fhandle->fh_private_length != sizeof(pvfs2_opaque_handle_t))
        {
            gossip_err("perform_handle_checks: length mismatch (%ld) "
                    " instead of (%ld)\n", (unsigned long) fhandle->fh_private_length,
                    (unsigned long) sizeof(pvfs2_opaque_handle_t));
            return 0;
        }
    }
    if (check & HANDLE_CHECK_MAGIC)
    {
        u32 magic;

        get_fh_field(&fhandle->fh_generic, magic, magic);

        if (magic != PVFS2_SUPER_MAGIC)
        {
            gossip_err("perform_handle_checks: mismatched magic number "
                    " (%x) instead of (%x)\n",
                    magic, PVFS2_SUPER_MAGIC);
            return 0;
        }
    }
    if (check & HANDLE_CHECK_FSID)
    {
        pvfs2_sb_info_t *pvfs2_sbp = NULL;
        struct super_block *sb = (struct super_block *) p;
        u32 fsid;

        if (!sb)
            return 0;
        pvfs2_sbp = PVFS2_SB(sb);

        get_fh_field(&fhandle->fh_generic, fsid, fsid);

        if (fsid != pvfs2_sbp->fs_id)
        {
            gossip_err("perform_handle_checks: FSID did not match "
                    " (%d) instead of (%d)\n",
                    fsid, pvfs2_sbp->fs_id);
            return 0;
        }
        gossip_debug(GOSSIP_UTILS_DEBUG, "perform_handle_checks : fsid = %d\n", fsid);
    }
    return 1;
}

/*
 * convert an opaque handle to a PVFS_sys_attr structure so that we could
 * call copy_attributes_to_inode() to initialize the VFS inode structure.
 */
static void convert_opaque_handle_to_sys_attr(
        PVFS_sys_attr *dst, pvfs2_opaque_handle_t *src)
{
    dst->owner = src->owner;
    dst->group = src->group;
    dst->perms = src->perms;
    dst->atime = src->atime;
    dst->mtime = src->mtime;
    dst->ctime = src->ctime;
    dst->size  = src->size;
    dst->link_target = NULL;
    dst->dfile_count = 0;
    dst->dirent_count = 0;
    dst->objtype = src->objtype;
    dst->mask = src->mask;
    return;
}

static inline void do_decode_opaque_handle(pvfs2_opaque_handle_t *h, char *src)
{
    char *ptr = src;
    char **pptr = &ptr;

    memset(h, 0, sizeof(pvfs2_opaque_handle_t));
    /* Deserialize the buffer */
    decode_pvfs2_opaque_handle_t(pptr, h);
    return;
}

static int get_opaque_handle(struct super_block *sb,
        const struct file_handle *fhandle,
        pvfs2_opaque_handle_t *opaque_handle)
{
    /* Make sure that we actually get a valid handle */
    if (perform_handle_checks(fhandle,
            HANDLE_CHECK_LENGTH | HANDLE_CHECK_MAGIC 
            | HANDLE_CHECK_FSID, sb) == 0)
    {
        gossip_err("get_handle: got invalid handle buffer!? "
                "Impossible happened\n");
        return -EINVAL;
    }

    do_decode_opaque_handle(opaque_handle, (char *) fhandle->fh_private);
    /* make sure that fsid in private buffer also matches */
    if (opaque_handle->fsid != PVFS2_SB(sb)->fs_id) {
        gossip_err("get_handle: invalid fsid in private buffer "
                " (%d) instead of (%d)\n",
                opaque_handle->fsid, PVFS2_SB(sb)->fs_id);
        return -EINVAL;
    }
    gossip_debug(GOSSIP_UTILS_DEBUG, "get_handle: decoded fsid %d handle %lu\n",
            opaque_handle->fsid, (unsigned long) opaque_handle->handle);
    return 0;
}

/*
 * called by openfh() system call.
 * Given a handle that ostensibly belongs to this PVFS2 superblock,
 * we either find an inode
 * in the icache already matching the given handle or we allocate 
 * and place a new struct inode in the icache and fill it up based on
 * the buffer that we obtained from user. Presumably enough checks
 * at the upper-level (VFS) has been done to make sure that this is
 * indeed a buffer filled upon a successful openg().
 * Returns ERR_PTR(-errno) in case of error
 *         valid pointer to struct inode in case it was a success
 */
struct inode *pvfs2_sb_find_inode_handle(struct super_block *sb,
        const struct file_handle *fhandle)
{
    struct inode *inode = NULL;
    int err = 0;
    pvfs2_opaque_handle_t opaque_handle;
    PVFS_sys_attr attrs;
    PVFS_object_ref ref;

    /* Decode the buffer */
    err = get_opaque_handle(sb, fhandle, &opaque_handle);
    if (err)
        return ERR_PTR(err);

    /* and convert the opaque handle structure to the PVFS_sys_attr structure */
    convert_opaque_handle_to_sys_attr(&attrs, &opaque_handle);

    ref.handle = opaque_handle.handle;
    ref.fs_id  = opaque_handle.fsid;
    gossip_debug(GOSSIP_UTILS_DEBUG, "pvfs2_sb_find_inode_handle: obtained inode number %llu\n",
            llu(opaque_handle.handle));
    /* 
     * NOTE: Locate the inode number in the icache if possible.
     * If not allocate a new inode that is returned locked and
     * hashed. Since, we don't issue a getattr/read_inode() callback
     * the pvfs2 specific inode is almost guaranteed to be
     * uninitialized or invalid. Therefore, we need to
     * fill it up based on the information in opaque_handle!
     * Consequently, this approach should scale well since openfh()
     * does not require any network messages.
     */
    inode = pvfs2_iget_locked(sb, &ref);

    if (!inode) {
        gossip_err("Could not allocate inode\n");
        return ERR_PTR(-ENOMEM);
    }
    else {
        if (is_bad_inode(inode)) {
            iput(inode);
            gossip_err("bad inode obtained from iget_locked\n");
            return ERR_PTR(-EINVAL);
        }
        /* Initialize and/or verify struct inode as well as pvfs2_inode */
        if ((err = copy_attributes_to_inode(inode, &attrs, NULL)) < 0) {
            gossip_err("copy_attributes_to_inode failed with err %d\n", err);
            iput(inode);
            return ERR_PTR(err);
        }

        /* this inode was allocated afresh */
        if (inode->i_state & I_NEW) {
            pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);

            pvfs2_inode_initialize(pvfs2_inode);
            pvfs2_inode->refn.handle = opaque_handle.handle;
            pvfs2_inode->refn.fs_id  = opaque_handle.fsid;
            inode->i_mapping->host   = inode;
            inode->i_rdev            = 0;
            inode->i_bdev            = NULL;
            inode->i_cdev            = NULL;
            inode->i_mapping->a_ops  = &pvfs2_address_operations;
            inode->i_mapping->backing_dev_info = &pvfs2_backing_dev_info;
            /* Make sure that we unlock the inode */
            unlock_new_inode(inode);
        }
        return inode;
    }
}

#endif

#ifdef HAVE_FILL_HANDLE_INODE_OPERATIONS

/*
 * dst would be encoded 
 */
static int do_encode_opaque_handle(char *dst, struct inode *inode)
{
    char *ptr = dst;
    char **pptr = &ptr;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);
    pvfs2_opaque_handle_t h;

    /* only metafile allowed */
    if (!S_ISREG(inode->i_mode)) 
        return -EINVAL;
    memset(&h, 0, sizeof(h));
    h.handle = pvfs2_inode->refn.handle;
    h.fsid   = pvfs2_inode->refn.fs_id;
    h.owner  = inode->i_uid;
    h.group  = inode->i_gid;
    h.perms  = PVFS_util_translate_mode(inode->i_mode, 0);
    h.mask   |= PVFS_ATTR_SYS_PERM;
    h.atime  = pvfs2_convert_time_field(&inode->i_atime);
    h.mtime  = pvfs2_convert_time_field(&inode->i_mtime);
    h.ctime  = pvfs2_convert_time_field(&inode->i_ctime);
    h.size   = pvfs2_i_size_read(inode);
    h.mask   |= PVFS_ATTR_SYS_SIZE;
    h.objtype = PVFS_TYPE_METAFILE;
    /* Serialize into the buffer */
    gossip_debug(GOSSIP_UTILS_DEBUG, "encoded fsid %d handle %lu\n",
            h.fsid, (unsigned long) h.handle);
    encode_pvfs2_opaque_handle_t(pptr, &h);
    return 0;
}

static void *pvfs2_fh_ctor(void)
{
    void *buf;

    buf = kmalloc(sizeof(pvfs2_opaque_handle_t), 
                  PVFS2_BUFMAP_GFP_FLAGS);
    return buf;
}

static void pvfs2_fh_dtor(void *buf)
{
    if (buf)
        kfree(buf);
    return;
}

/*
 * This routine is called by openg() system call.
 * Given an inode (which has been looked up previously),
 * we fill in the attributes of the inode in an opaque buffer
 * and hand it back to user. 
 * Note: We need to make it a fixed
 * endian ordering so that it would work on all homogenous platforms.
 * Hence the need to encode the handle buffer.
 */
int pvfs2_fill_handle(struct inode *inode, struct file_handle *fhandle) 
{
    size_t pvfs2_opaque_handle_size = sizeof(pvfs2_opaque_handle_t);

    if (!inode || !fhandle)
    {
        return -EINVAL;
    }
    /* querying the size of PVFS2 specific opaque handle buffer */
    if (fhandle->fh_private_length == 0)
    {
        fhandle->fh_private_length = pvfs2_opaque_handle_size;
        return 0;
    }
    else if (fhandle->fh_private_length < pvfs2_opaque_handle_size)
    {
        return -ERANGE; /* too small a buffer length */
    }
    else
    {
        fhandle->fh_private = pvfs2_fh_ctor();
        if (fhandle->fh_private == NULL)
        {
            return -ENOMEM;
        }
        /* encode the opaque handle information */
        if (do_encode_opaque_handle((char *) fhandle->fh_private, inode) < 0)
        {
            pvfs2_fh_dtor(fhandle->fh_private);
            fhandle->fh_private = NULL;
            return -EINVAL;
        }
        /* Set a destructor function for the fh_private */
        fhandle->fh_private_dtor = pvfs2_fh_dtor;
        /* and the length */
        fhandle->fh_private_length = pvfs2_opaque_handle_size;
        gossip_debug(GOSSIP_UTILS_DEBUG, "Returning handle length %ld\n",
                (unsigned long) pvfs2_opaque_handle_size);
        return 0;
    }
}

#endif /* HAVE_FILL_HANDLE_INODE_OPERATIONS */

#ifdef USE_MMAP_RA_CACHE
int pvfs2_flush_mmap_racache(struct inode *inode)
{
    int ret = -EINVAL;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);
    pvfs2_kernel_op_t *new_op = NULL;

    gossip_debug(GOSSIP_UTILS_DEBUG, "pvfs2_flush_mmap_racache %llu: Handle is %llu "
                "| fs_id %d\n", llu(get_handle_from_ino(inode)),
                pvfs2_inode->refn.handle, pvfs2_inode->refn.fs_id);

    new_op = op_alloc(PVFS2_VFS_OP_MMAP_RA_FLUSH);
    if (!new_op)
    {
        return -ENOMEM;
    }
    new_op->upcall.req.ra_cache_flush.refn = pvfs2_inode->refn;

    ret = service_operation(new_op, "pvfs2_flush_mmap_racache",
                      get_interruptible_flag(inode));

    gossip_debug(GOSSIP_UTILS_DEBUG, "pvfs2_flush_mmap_racache got return "
                "value of %d\n",ret);

    op_release(new_op);
    return ret;
}
#endif

int pvfs2_unmount_sb(struct super_block *sb)
{
    int ret = -EINVAL;
    pvfs2_kernel_op_t *new_op = NULL;

    gossip_debug(GOSSIP_UTILS_DEBUG, "pvfs2_unmount_sb called on sb %p\n", sb);

    new_op = op_alloc(PVFS2_VFS_OP_FS_UMOUNT);
    if (!new_op)
    {
        return -ENOMEM;
    }
    new_op->upcall.req.fs_umount.id = PVFS2_SB(sb)->id;
    new_op->upcall.req.fs_umount.fs_id = PVFS2_SB(sb)->fs_id;
    strncpy(new_op->upcall.req.fs_umount.pvfs2_config_server,
            PVFS2_SB(sb)->devname, PVFS_MAX_SERVER_ADDR_LEN);

    gossip_debug(GOSSIP_UTILS_DEBUG, "Attempting PVFS2 Unmount via host %s\n",
                new_op->upcall.req.fs_umount.pvfs2_config_server);

    ret = service_operation(new_op, "pvfs2_fs_umount", 0);

    gossip_debug(GOSSIP_UTILS_DEBUG, "pvfs2_unmount: got return value of %d\n", ret);
    if (ret)
    {
        sb = ERR_PTR(ret);
    }
    else {
        PVFS2_SB(sb)->mount_pending = 1;
    }

    op_release(new_op);
    return ret;
}

/*
  NOTE: on successful cancellation, be sure to return -EINTR, as
  that's the return value the caller expects
*/
int pvfs2_cancel_op_in_progress(unsigned long tag)
{
    int ret = -EINVAL;
    pvfs2_kernel_op_t *new_op = NULL;

    gossip_debug(GOSSIP_UTILS_DEBUG, "pvfs2_cancel_op_in_progress called on tag %lu\n", tag);

    new_op = op_alloc(PVFS2_VFS_OP_CANCEL);
    if (!new_op)
    {
        return -ENOMEM;
    }
    new_op->upcall.req.cancel.op_tag = tag;

    gossip_debug(GOSSIP_UTILS_DEBUG, "Attempting PVFS2 operation cancellation of tag %llu\n",
                llu(new_op->upcall.req.cancel.op_tag));

    ret = service_operation(new_op, "pvfs2_cancel", PVFS2_OP_CANCELLATION);

    gossip_debug(GOSSIP_UTILS_DEBUG, "pvfs2_cancel_op_in_progress: got return "
                "value of %d\n", ret);

    op_release(new_op);
    return (ret);
}

void pvfs2_inode_initialize(pvfs2_inode_t *pvfs2_inode)
{
    if (!InitFlag(pvfs2_inode))
    {
        pvfs2_inode->refn.handle = PVFS_HANDLE_NULL;
        pvfs2_inode->refn.fs_id = PVFS_FS_ID_NULL;
        pvfs2_inode->last_failed_block_index_read = 0;
        memset(pvfs2_inode->link_target, 0, sizeof(pvfs2_inode->link_target));
        pvfs2_inode->error_code = 0;
        SetInitFlag(pvfs2_inode);
    }
}

/*
  this is called from super:pvfs2_destroy_inode.
*/
void pvfs2_inode_finalize(pvfs2_inode_t *pvfs2_inode)
{
    pvfs2_inode->refn.handle = PVFS_HANDLE_NULL;
    pvfs2_inode->refn.fs_id = PVFS_FS_ID_NULL;
    pvfs2_inode->last_failed_block_index_read = 0;
    pvfs2_inode->error_code = 0;
}

void pvfs2_op_initialize(pvfs2_kernel_op_t *op)
{
    op->io_completed = 0;

    op->upcall.type = PVFS2_VFS_OP_INVALID;
    op->downcall.type = PVFS2_VFS_OP_INVALID;
    op->downcall.status = -1;

    op->op_state = OP_VFS_STATE_UNKNOWN;
    op->tag = 0;
}

void pvfs2_make_bad_inode(struct inode *inode)
{
    if (is_root_handle(inode))
    {
        /*
          if this occurs, the pvfs2-client-core was killed but we
          can't afford to lose the inode operations and such
          associated with the root handle in any case
        */
        gossip_debug(GOSSIP_UTILS_DEBUG, "*** NOT making bad root inode %llu\n", llu(get_handle_from_ino(inode)));
    }
    else
    {
        gossip_debug(GOSSIP_UTILS_DEBUG, "*** making bad inode %llu\n", llu(get_handle_from_ino(inode)));
        make_bad_inode(inode);
    }
}

/* this code is based on linux/net/sunrpc/clnt.c:rpc_clnt_sigmask */
void mask_blocked_signals(sigset_t *orig_sigset)
{
    unsigned long sigallow = sigmask(SIGKILL);
    unsigned long irqflags = 0;
    struct k_sigaction *action = pvfs2_current_sigaction;

    sigallow |= ((action[SIGINT-1].sa.sa_handler == SIG_DFL) ?
                 sigmask(SIGINT) : 0);
    sigallow |= ((action[SIGQUIT-1].sa.sa_handler == SIG_DFL) ?
                 sigmask(SIGQUIT) : 0);

    spin_lock_irqsave(&pvfs2_current_signal_lock, irqflags);
    *orig_sigset = current->blocked;
    siginitsetinv(&current->blocked, sigallow & ~orig_sigset->sig[0]);
    pvfs2_recalc_sigpending();
    spin_unlock_irqrestore(&pvfs2_current_signal_lock, irqflags);
}

/* this code is based on linux/net/sunrpc/clnt.c:rpc_clnt_sigunmask */
void unmask_blocked_signals(sigset_t *orig_sigset)
{
    unsigned long irqflags = 0;

    spin_lock_irqsave(&pvfs2_current_signal_lock, irqflags);
    current->blocked = *orig_sigset;
    pvfs2_recalc_sigpending();
    spin_unlock_irqrestore(&pvfs2_current_signal_lock, irqflags);
}

PVFS_time pvfs2_convert_time_field(void *time_ptr)
{
    PVFS_time pvfs2_time;
#ifdef PVFS2_LINUX_KERNEL_2_4
    pvfs2_time = (PVFS_time)(*(time_t *)time_ptr);
#else
    struct timespec *tspec = (struct timespec *)time_ptr;
    pvfs2_time = (PVFS_time)((time_t)tspec->tv_sec);
#endif
    return pvfs2_time;
}

/* macro defined in include/pvfs2-types.h */
DECLARE_ERRNO_MAPPING_AND_FN();

int pvfs2_normalize_to_errno(PVFS_error error_code)
{
    if(error_code > 0)
    {
        gossip_err("pvfs2: error status receieved.\n");
        gossip_err("pvfs2: assuming error code is inverted.\n");
        error_code = -error_code;
    }

    /* convert any error codes that are in pvfs2 format */
    if(IS_PVFS_NON_ERRNO_ERROR(-error_code))
    {
        if(PVFS_NON_ERRNO_ERROR_CODE(-error_code) == PVFS_ECANCEL)
        {
            /* cancellation error codes generally correspond to a timeout
             * from the client's perspective
             */
            error_code = -ETIMEDOUT;
        }
        else
        {
            /* assume a default error code */
            gossip_err("pvfs2: warning: "
                "got error code without errno equivalent: %d.\n", error_code);
            error_code = -EINVAL;
        }
    }
    else if(IS_PVFS_ERROR(-error_code))
    {
        error_code = -PVFS_ERROR_TO_ERRNO(-error_code);
    }
    return(error_code);
}

int32_t PVFS_util_translate_mode(int mode, int suid)
{
    int ret = 0, i = 0;
#define NUM_MODES 11
    static int modes[NUM_MODES] =
    {
        S_IXOTH, S_IWOTH, S_IROTH,
        S_IXGRP, S_IWGRP, S_IRGRP,
        S_IXUSR, S_IWUSR, S_IRUSR,
        S_ISGID, S_ISUID
    };
    static int pvfs2_modes[NUM_MODES] =
    {
        PVFS_O_EXECUTE, PVFS_O_WRITE, PVFS_O_READ,
        PVFS_G_EXECUTE, PVFS_G_WRITE, PVFS_G_READ,
        PVFS_U_EXECUTE, PVFS_U_WRITE, PVFS_U_READ,
        PVFS_G_SGID,    PVFS_U_SUID
    };

    for(i = 0; i < NUM_MODES; i++)
    {
        if (mode & modes[i])
        {
            ret |= pvfs2_modes[i];
        }
    }
    if (suid == 0 && (ret & PVFS_U_SUID))
    {
         ret &= ~PVFS_U_SUID;
    }
    return ret;
#undef NUM_MODES
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
