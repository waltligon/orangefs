/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"
#include "pvfs2-internal.h"

/* list for storing pvfs2 specific superblocks in use */
LIST_HEAD(pvfs2_superblocks);

DEFINE_SPINLOCK(pvfs2_superblocks_lock);

static char *keywords[] = {"intr", "acl", "suid", "noatime", "nodiratime"};
static int num_possible_keywords = sizeof(keywords)/sizeof(char *);

static int parse_mount_options(
   char *option_str, struct super_block *sb, int silent)
{
    char *ptr = option_str;
    pvfs2_sb_info_t *pvfs2_sb = NULL;
    int i = 0, j = 0, num_keywords = 0, got_device = 0;

    static char options[PVFS2_MAX_NUM_OPTIONS][PVFS2_MAX_MOUNT_OPT_LEN];

    if (!silent)
    {
        if (option_str) 
        {
            gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2: parse_mount_options called with:\n");
            gossip_debug(GOSSIP_SUPER_DEBUG, " %s\n", option_str);
        }
        else 
        {
            /* We need a non-NULL option string */
            goto exit;
        }
    }

    if (sb && PVFS2_SB(sb))
    {
        memset(options, 0,
               (PVFS2_MAX_NUM_OPTIONS * PVFS2_MAX_MOUNT_OPT_LEN));

        pvfs2_sb = PVFS2_SB(sb);
        memset(&pvfs2_sb->mnt_options, 0, sizeof(pvfs2_mount_options_t));

        while(ptr && (*ptr != '\0'))
        {
            options[num_keywords][j++] = *ptr;
    
            if (j == PVFS2_MAX_MOUNT_OPT_LEN)
            {
                gossip_err("Cannot parse mount time options (length "
                            "exceeded)\n");
                got_device = 0;
                goto exit;
            }

            if (*ptr == ',')
            {
                options[num_keywords++][j-1] = '\0';
                if (num_keywords == PVFS2_MAX_NUM_OPTIONS)
                {
                    gossip_err("Cannot parse mount time options (option "
                                "number exceeded)\n");
                    got_device = 0;
                    goto exit;
                }
                j = 0;
            }
            ptr++;
        }
        num_keywords++;

        for(i = 0; i < num_keywords; i++)
        {
            for(j = 0; j < num_possible_keywords; j++)
            {
                if (strcmp(options[i], keywords[j]) == 0)
                {
                    if (strncmp(options[i], "intr", 4) == 0)
                    {
                        if (!silent)
                        {
                            gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2: mount option "
                                        "intr specified\n");
                        }
                        pvfs2_sb->mnt_options.intr = 1;
                        break;
                    }
                    else if (strncmp(options[i], "acl", 3) == 0)
                    {
                        if (!silent)
                        {
                            gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2: mount option "
                                        "acl specified\n");
                        }
                        pvfs2_sb->mnt_options.acl = 1;
                        break;
                    }
                    else if (strncmp(options[i], "suid", 4) == 0)
                    {
                        if (!silent)
                        {
                            gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2: mount option "
                                        "suid specified\n");
                        }
                        pvfs2_sb->mnt_options.suid = 1;
                        break;
                    }
                    else if (strncmp(options[i], "noatime", 7) == 0)
                    {
                        if (!silent)
                        {
                            gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2: mount option "
                                       "noatime specified\n");
                        }
                        pvfs2_sb->mnt_options.noatime = 1;
                    }
                    else if (strncmp(options[i], "nodiratime", 10) == 0)
                    {
                        if (!silent)
                        {
                            gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2: mount option "
                                       "nodiratime specified\n");
                        }
                        pvfs2_sb->mnt_options.nodiratime = 1;
                    }
                }
            }

            /* option string did not match any of the known keywords */
            if (j == num_possible_keywords)
            {
                /* filter out NULL option strings (older 2.6 kernels may leave 
                 * these after parsing out standard options like noatime) 
                 */
                if(options[i][0] != '\0')
                {
                    /* in the 2.6 kernel, we don't pass device name through this
                     * path; we must have gotten an unsupported option.
                     */
                    gossip_err("Error: mount option [%s] is not supported.\n", options[i]);
                    return(-EINVAL);
                }
            }
        }
    }
exit:
    return 0;
}

static struct inode *pvfs2_alloc_inode(struct super_block *sb)
{
    struct inode *new_inode = NULL;
    pvfs2_inode_t *pvfs2_inode = NULL;

    pvfs2_inode = pvfs2_inode_alloc();
    if (pvfs2_inode)
    {
        new_inode = &pvfs2_inode->vfs_inode;
        gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_alloc_inode: allocated %p\n", pvfs2_inode);
        atomic_inc(&(PVFS2_SB(sb)->pvfs2_inode_alloc_count));
        new_inode->i_flags &= ~(S_APPEND|S_IMMUTABLE|S_NOATIME);
    }
    return new_inode;
}

static void pvfs2_destroy_inode(struct inode *inode)
{
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);

    if (pvfs2_inode)
    {
        gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_destroy_inode: deallocated %p destroying inode %llu\n",
                    pvfs2_inode, llu(get_handle_from_ino(inode)));

        atomic_inc(&(PVFS2_SB(inode->i_sb)->pvfs2_inode_dealloc_count));
        pvfs2_inode_finalize(pvfs2_inode);
        pvfs2_inode_release(pvfs2_inode);
    }
}

void pvfs2_read_inode(
    struct inode *inode)
{
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);

    gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_read_inode: %p (inode = %llu | ct = %d)\n",
                pvfs2_inode, llu(get_handle_from_ino(inode)), (int)atomic_read(&inode->i_count));

    /*
      at this point we know the private inode data handle/fs_id can't
      be valid because we've never done a pvfs2 lookup/getattr yet.
      clear it here to allow the pvfs2_inode_getattr to use the inode
      number as the handle instead of whatever junk the private data
      may contain.
    */
    pvfs2_inode_initialize(pvfs2_inode);

    /*
       need to populate the freshly allocated (passed in) inode here.
       this gets called if the vfs can't find this inode in the inode
       cache.  we need to getattr here because d_revalidate isn't
       called after a successful dentry lookup if the inode is not
       present in the inode cache already.  so this is our chance.
    */
    if (pvfs2_inode_getattr(inode, PVFS_ATTR_SYS_ALL_NOHINT) != 0)
    {
        /* assume an I/O error and mark the inode as bad */
        gossip_debug(GOSSIP_SUPER_DEBUG, "%s:%s:%d calling make bad inode - [%p] (inode = %llu | ct = %d)\n",
                __FILE__, __func__, __LINE__, pvfs2_inode, llu(get_handle_from_ino(inode)), (int)atomic_read(&inode->i_count));
        pvfs2_make_bad_inode(inode);
    }
}

/*
  NOTE: information filled in here is typically reflected in the
  output of the system command 'df'
*/
static int pvfs2_statfs(struct dentry *dentry, struct kstatfs *buf)
{
    int ret = -ENOMEM;
    pvfs2_kernel_op_t *new_op = NULL;
    int flags = 0;
    struct super_block *sb = NULL;

    sb = dentry->d_sb;

    gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_statfs: called on sb %p (fs_id is %d)\n",
                sb, (int)(PVFS2_SB(sb)->fs_id));

    new_op = op_alloc(PVFS2_VFS_OP_STATFS);
    if (!new_op)
    {
        return ret;
    }
    new_op->upcall.req.statfs.fs_id = PVFS2_SB(sb)->fs_id;

    if(PVFS2_SB(sb)->mnt_options.intr)
    {
        flags = PVFS2_OP_INTERRUPTIBLE;
    }

    ret = service_operation(
        new_op, "pvfs2_statfs",  flags);

    if (new_op->downcall.status > -1)
    {
        gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_statfs: got %ld blocks available | "
                    "%ld blocks total | %ld block size\n",
                    (long) new_op->downcall.resp.statfs.blocks_avail,
                    (long) new_op->downcall.resp.statfs.blocks_total,
                    (long) new_op->downcall.resp.statfs.block_size);

        buf->f_type = sb->s_magic;
        /* stash the fsid as well */
        memcpy(&buf->f_fsid, &(PVFS2_SB(sb)->fs_id), 
                sizeof(PVFS2_SB(sb)->fs_id));      
        buf->f_bsize = new_op->downcall.resp.statfs.block_size;
        buf->f_namelen = PVFS2_NAME_LEN;

        buf->f_blocks = (sector_t)
            new_op->downcall.resp.statfs.blocks_total;
        buf->f_bfree = (sector_t)
            new_op->downcall.resp.statfs.blocks_avail;
        buf->f_bavail = (sector_t)
            new_op->downcall.resp.statfs.blocks_avail;
        buf->f_files = (sector_t)
            new_op->downcall.resp.statfs.files_total;
        buf->f_ffree = (sector_t)
            new_op->downcall.resp.statfs.files_avail;

        do
        {
            struct statfs tmp_statfs;

            buf->f_frsize = sb->s_blocksize;

            gossip_debug(GOSSIP_SUPER_DEBUG, "sizeof(kstatfs)=%d\n",
                        (int)sizeof(struct kstatfs));
            gossip_debug(GOSSIP_SUPER_DEBUG, "sizeof(kstatfs->f_blocks)=%d\n",
                        (int)sizeof(buf->f_blocks));
            gossip_debug(GOSSIP_SUPER_DEBUG, "sizeof(statfs)=%d\n",
                        (int)sizeof(struct statfs));
            gossip_debug(GOSSIP_SUPER_DEBUG, "sizeof(statfs->f_blocks)=%d\n",
                        (int)sizeof(tmp_statfs.f_blocks));
            gossip_debug(GOSSIP_SUPER_DEBUG, "sizeof(sector_t)=%d\n",
                        (int)sizeof(sector_t));

            if ((sizeof(struct statfs) != sizeof(struct kstatfs)) &&
                (sizeof(tmp_statfs.f_blocks) == 4))
            {
                /*
                  in this case, we need to truncate the values here to
                  be no bigger than the max 4 byte long value because
                  the kernel will return an overflow if it's larger
                  otherwise.  see vfs_statfs_native in open.c for the
                  actual overflow checks made.
                */
                buf->f_blocks &= 0x00000000FFFFFFFFULL;
                buf->f_bfree &= 0x00000000FFFFFFFFULL;
                buf->f_bavail &= 0x00000000FFFFFFFFULL;
                buf->f_files &= 0x00000000FFFFFFFFULL;
                buf->f_ffree &= 0x00000000FFFFFFFFULL;
                
                gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_statfs (T) got %lu files total | %lu "
                            "files_avail\n", (unsigned long)buf->f_files,
                            (unsigned long)buf->f_ffree);
            }
            else
            {
                gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_statfs (N) got %lu files total | %lu "
                            "files_avail\n", (unsigned long)buf->f_files,
                            (unsigned long)buf->f_ffree);
            }
        } while(0);
    }

    op_release(new_op);

    gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_statfs: returning %d\n", ret);
    return ret;
}

/* pvfs2_remount_fs()
 *
 * remount as initiated by VFS layer.  We just need to reparse the mount
 * options, no need to signal pvfs2-client-core about it.
 */
static int pvfs2_remount_fs(
    struct super_block *sb,
    int *flags,
    char *data)
{
    int ret = -EINVAL;

    gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_remount_fs: called\n");

    if (sb && PVFS2_SB(sb))
    {
        if (data && data[0] != '\0')
        {
            ret = parse_mount_options(data, sb, 1);
            if (ret)
            {
                return ret;
            }
/* CONFIG_FS_POSIX_ACL is set in .config */
#if defined(CONFIG_FS_POSIX_ACL)
            /* mark the superblock as whether it supports acl's or not */
            sb->s_flags = ((sb->s_flags & ~MS_POSIXACL) | 
                ((PVFS2_SB(sb)->mnt_options.acl == 1) ? MS_POSIXACL : 0));
            sb->s_xattr = pvfs2_xattr_handlers;
#endif
            sb->s_flags = ((sb->s_flags & ~MS_NOATIME)  |
                ((PVFS2_SB(sb)->mnt_options.noatime == 1) ? MS_NOATIME : 0));
            sb->s_flags = ((sb->s_flags & ~MS_NODIRATIME) |
                ((PVFS2_SB(sb)->mnt_options.nodiratime == 1) ? MS_NODIRATIME : 0));
        }

        if (data)
        {
            strncpy(PVFS2_SB(sb)->data, data, PVFS2_MAX_MOUNT_OPT_LEN);
        }
    }
    return 0;
}

/*
  Remount as initiated by pvfs2-client-core on restart.  This is used to
  repopulate mount information left from previous pvfs2-client-core.

  the idea here is that given a valid superblock, we're
  re-initializing the user space client with the initial mount
  information specified when the super block was first initialized.
  this is very different than the first initialization/creation of a
  superblock.  we use the special service_priority_operation to make
  sure that the mount gets ahead of any other pending operation that
  is waiting for servicing.  this means that the pvfs2-client won't
  fail to start several times for all other pending operations before
  the client regains all of the mount information from us.
  NOTE: this function assumes that the request_semaphore is already acquired!
*/
int pvfs2_remount(
    struct super_block *sb,
    int *flags,
    char *data)
{
    int ret = -EINVAL;
    pvfs2_kernel_op_t *new_op = NULL;

    gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_remount: called\n");

    if (sb && PVFS2_SB(sb))
    {
        if (data && data[0] != '\0')
        {
            ret = parse_mount_options(data, sb, 1);
            if (ret)
            {
                return ret;
            }
/* CONFIG_FS_POSIX_ACL is set in .config */
#if defined(CONFIG_FS_POSIX_ACL)
            /* mark the superblock as whether it supports acl's or not */
            sb->s_flags = ((sb->s_flags & ~MS_POSIXACL) | 
                ((PVFS2_SB(sb)->mnt_options.acl == 1) ? MS_POSIXACL : 0));
            sb->s_xattr = pvfs2_xattr_handlers;
#endif
            sb->s_flags = ((sb->s_flags & ~MS_NOATIME)  |
                ((PVFS2_SB(sb)->mnt_options.noatime == 1) ? MS_NOATIME : 0));
            sb->s_flags = ((sb->s_flags & ~MS_NODIRATIME) |
                ((PVFS2_SB(sb)->mnt_options.nodiratime == 1) ? MS_NODIRATIME : 0));
        }

        new_op = op_alloc(PVFS2_VFS_OP_FS_MOUNT);
        if (!new_op)
        {
            return -ENOMEM;
        }
        strncpy(new_op->upcall.req.fs_mount.pvfs2_config_server,
                PVFS2_SB(sb)->devname, PVFS_MAX_SERVER_ADDR_LEN);

        gossip_debug(GOSSIP_SUPER_DEBUG, "Attempting PVFS2 Remount via host %s\n",
                    new_op->upcall.req.fs_mount.pvfs2_config_server);

        /* we assume that the calling function has already acquire the
         * request_semaphore to prevent other operations from bypassing this
         * one
         */
        ret = service_operation(new_op, "pvfs2_remount", 
            (PVFS2_OP_PRIORITY|PVFS2_OP_NO_SEMAPHORE));

        gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_remount: mount got return value of %d\n", ret);
        if (ret == 0)
        {
            /*
              store the id assigned to this sb -- it's just a short-lived
              mapping that the system interface uses to map this
              superblock to a particular mount entry
            */
            PVFS2_SB(sb)->id = new_op->downcall.resp.fs_mount.id;

            if (data)
            {
                strncpy(PVFS2_SB(sb)->data, data, PVFS2_MAX_MOUNT_OPT_LEN);
            }
            PVFS2_SB(sb)->mount_pending = 0;
        }

        op_release(new_op);
    }
    return ret;
}

int fsid_key_table_initialize(void)
{
    return 0;
}

void fsid_key_table_finalize(void)
{
    return;
}

/* Called whenever the VFS dirties the inode in response to atime updates */
static void pvfs2_dirty_inode(struct inode *inode, int flags)
{
    if (inode)
    {
        pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);
        gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_dirty_inode: %llu\n", llu(get_handle_from_ino(inode)));
        SetAtimeFlag(pvfs2_inode);
    }
    return;
}

struct super_operations pvfs2_s_ops =
{
    .drop_inode = generic_delete_inode,
    .alloc_inode = pvfs2_alloc_inode,
    .destroy_inode = pvfs2_destroy_inode,
    .dirty_inode = pvfs2_dirty_inode,
    .statfs = pvfs2_statfs,
    .remount_fs = pvfs2_remount_fs,
};

struct dentry *pvfs2_fh_to_dentry(struct super_block *sb,
                                  struct fid *fid,
                                  int fh_len, int fh_type)
{
   PVFS_object_ref refn;
   struct inode *inode;
   struct dentry *dentry;

   if (fh_len < 3 || fh_type > 2) 
   {
      return NULL;
   }

   refn.handle = (u64) (fid->raw[0]) << 32;
   refn.handle |= (u32) fid->raw[1];
   refn.fs_id  = (u32) fid->raw[2];
   gossip_debug(GOSSIP_SUPER_DEBUG, "fh_to_dentry: handle %llu, fs_id %d\n",
                refn.handle, refn.fs_id);

   inode = pvfs2_iget(sb, &refn);

   dentry = d_obtain_alias(inode);
   if(dentry == NULL)
   {
       return ERR_PTR(-ENOMEM);
   }

   d_set_d_op(dentry, &pvfs2_dentry_operations);
   return dentry;
}

int pvfs2_encode_fh(struct inode *inode,
                    __u32 *fh,
                    int *max_len,
                    struct inode *parent)
{
   int len = parent ? 6 : 3;
   int type = 1;
   PVFS_object_ref handle;

   if (*max_len < len) {
     gossip_lerr("fh buffer is too small for encoding\n");
     *max_len = len;
     type =  255;
     goto out;
   }

   handle = PVFS2_I(inode)->refn;
   gossip_debug(GOSSIP_SUPER_DEBUG,
                "Encoding fh: handle %llu, fsid %u\n",
                handle.handle, handle.fs_id);

   fh[0] = handle.handle >> 32;
   fh[1] = handle.handle & 0xffffffff;
   fh[2] = handle.fs_id;

   if (parent)
   {
      handle = PVFS2_I(parent)->refn;
      fh[3] = handle.handle >> 32;
      fh[4] = handle.handle & 0xffffffff;
      fh[5] = handle.fs_id;

      type = 2;
      gossip_debug(GOSSIP_SUPER_DEBUG,
                   "Encoding parent: handle %llu, fsid %u\n",
                   handle.handle, handle.fs_id);
   }
   *max_len = len;

out:
   return type;
}

static struct export_operations pvfs2_export_ops =
{
   .encode_fh    = pvfs2_encode_fh,
   .fh_to_dentry = pvfs2_fh_to_dentry,
};

int pvfs2_fill_sb(struct super_block *sb,
                  void *data,
                  int silent)
{
    int ret = -EINVAL;
    struct inode *root = NULL;
    struct dentry *root_dentry = NULL;
    pvfs2_mount_sb_info_t *mount_sb_info = (pvfs2_mount_sb_info_t *)data;
    PVFS_object_ref root_object;

    /* alloc and init our private pvfs2 sb info */
    sb->s_fs_info = kmalloc(sizeof(pvfs2_sb_info_t), PVFS2_GFP_FLAGS);
    if (!PVFS2_SB(sb))
    {
        return -ENOMEM;
    }
    memset(sb->s_fs_info, 0, sizeof(pvfs2_sb_info_t));
    PVFS2_SB(sb)->sb = sb;

    PVFS2_SB(sb)->root_handle = mount_sb_info->root_handle;
    PVFS2_SB(sb)->fs_id = mount_sb_info->fs_id;
    PVFS2_SB(sb)->id = mount_sb_info->id;

    if (mount_sb_info->data)
    {
        ret = parse_mount_options((char *)mount_sb_info->data, sb, silent);
        if (ret)
        {
            return ret;
        }
        /* mark the superblock as whether it supports acl's or not */
        sb->s_flags = ((sb->s_flags & ~MS_POSIXACL) | 
            ((PVFS2_SB(sb)->mnt_options.acl == 1) ? MS_POSIXACL : 0));
        sb->s_flags = ((sb->s_flags & ~MS_NOATIME)  |
            ((PVFS2_SB(sb)->mnt_options.noatime == 1) ? MS_NOATIME : 0));
        sb->s_flags = ((sb->s_flags & ~MS_NODIRATIME) |
            ((PVFS2_SB(sb)->mnt_options.nodiratime == 1) ? MS_NODIRATIME : 0));
    }
    else
    {
        sb->s_flags = (sb->s_flags & ~(MS_POSIXACL | MS_NOATIME | MS_NODIRATIME));
    }

/* CONFIG_FS_POSIX_ACL is defined in .config */
#if defined(CONFIG_FS_POSIX_ACL)
    /* Hang the xattr handlers off the superblock */
    sb->s_xattr = pvfs2_xattr_handlers;
#endif
    sb->s_magic = PVFS2_SUPER_MAGIC;
    sb->s_op = &pvfs2_s_ops;
    sb->s_type = &pvfs2_fs_type;

    sb->s_blocksize = pvfs_bufmap_size_query();
    sb->s_blocksize_bits = pvfs_bufmap_shift_query();
    sb->s_maxbytes = MAX_LFS_FILESIZE;

    root_object.handle = PVFS2_SB(sb)->root_handle;
    root_object.fs_id  = PVFS2_SB(sb)->fs_id;
    gossip_debug(GOSSIP_SUPER_DEBUG, "get inode %llu, fsid %d\n",
                 root_object.handle, root_object.fs_id);
    /* alloc and initialize our root directory inode. be explicit about sticky
     * bit */
    root = pvfs2_get_custom_core_inode(sb,
                                       NULL,
                                       (S_IFDIR | 0755 | S_ISVTX),
                                       0,
                                       root_object);
    if (!root)
    {
        return -ENOMEM;
    }
    gossip_debug(GOSSIP_SUPER_DEBUG,
                 "Allocated root inode [%p] with mode %x\n",
                 root, root->i_mode);

    /* allocates and places root dentry in dcache */
    root_dentry = d_make_root(root);
    if (!root_dentry)
    {
        iput(root);
        return -ENOMEM;
    }
    d_set_d_op(root_dentry, &pvfs2_dentry_operations);

    sb->s_export_op = &pvfs2_export_ops;
    sb->s_root = root_dentry;
    return 0;
}
struct dentry *pvfs2_mount(struct file_system_type *fst,
                           int flags,
                           const char *devname,
                           void *data)
{
    int ret = -EINVAL;
    struct super_block *sb = ERR_PTR(-EINVAL);
    pvfs2_kernel_op_t *new_op;
    pvfs2_mount_sb_info_t mount_sb_info;
    struct dentry *mnt_sb_d = ERR_PTR(-EINVAL);

    gossip_debug(GOSSIP_SUPER_DEBUG,
                 "pvfs2_get_sb: called with devname %s\n", devname);

    if (devname)
    {
        new_op = op_alloc(PVFS2_VFS_OP_FS_MOUNT);
        if (!new_op)
        {
            ret = -ENOMEM;
            return ERR_PTR(ret);
        }
        strncpy(new_op->upcall.req.fs_mount.pvfs2_config_server,
                devname,
                PVFS_MAX_SERVER_ADDR_LEN);

        gossip_debug(GOSSIP_SUPER_DEBUG, "Attempting PVFS2 Mount via host %s\n",
                    new_op->upcall.req.fs_mount.pvfs2_config_server);

        ret = service_operation(new_op, "pvfs2_get_sb", 0);

        gossip_debug(GOSSIP_SUPER_DEBUG,
                     "pvfs2_get_sb: mount got return value of %d\n", ret);
        if (ret)
        {
            goto free_op;
        }

        if ((new_op->downcall.resp.fs_mount.fs_id == PVFS_FS_ID_NULL) ||
            (new_op->downcall.resp.fs_mount.root_handle ==
             PVFS_HANDLE_NULL))
        {
            gossip_err("ERROR: Retrieved null fs_id or root_handle\n");
            ret = -EINVAL;
            goto free_op;
        }

        /* fill in temporary structure passed to fill_sb method */
        mount_sb_info.data = data;
        mount_sb_info.root_handle = new_op->downcall.resp.fs_mount.root_handle;
        mount_sb_info.fs_id = new_op->downcall.resp.fs_mount.fs_id;
        mount_sb_info.id = new_op->downcall.resp.fs_mount.id;

        /*
          the mount_sb_info structure looks odd, but it's used because
          the private sb info isn't allocated until we call
          pvfs2_fill_sb, yet we have the info we need to fill it with
          here.  so we store it temporarily and pass all of the info
          to fill_sb where it's properly copied out
        */
        /* kernels beyond 2.6.38 no longer have get_sb_nodev in favor of
         * mount_nodev. if the kernel still has get_sb_nodev use that in
         * favor of mount_nodev to minimize changes for currently working
         * kernels. */
        mnt_sb_d = mount_nodev(fst,
                               flags,
                               (void *)&mount_sb_info,
                               pvfs2_fill_sb);
        if( !IS_ERR(mnt_sb_d) )
        {
            sb = mnt_sb_d->d_sb;
        }
        else
        {
            sb = ERR_CAST(mnt_sb_d);
            goto free_op;
        }

        if (sb && !IS_ERR(sb) && (PVFS2_SB(sb)))
        {
            /* Older 2.6 kernels pass in NOATIME flag here.  Capture it 
             * if present.
             */
            if(flags & MS_NOATIME)
            {
                sb->s_flags |= MS_NOATIME;
            }
            /* on successful mount, store the devname and data used */
            strncpy(PVFS2_SB(sb)->devname,
                    devname,
                    PVFS_MAX_SERVER_ADDR_LEN);
            if (data)
            {
                strncpy(PVFS2_SB(sb)->data,
                        data,
                        PVFS2_MAX_MOUNT_OPT_LEN);
            }

            /* mount_pending must be cleared */
            PVFS2_SB(sb)->mount_pending = 0;
            /* finally, add this sb to our list of known pvfs2 sb's */
            add_pvfs2_sb(sb);
        }
        else
        {
            ret = -EINVAL;
            gossip_err("Invalid superblock obtained from get_sb_nodev (%p)\n",
                       sb);
        }
        op_release(new_op);
    }
    else
    {
        gossip_err("ERROR: device name not specified.\n");
    }
    return mnt_sb_d;

free_op:
    gossip_err("pvfs2_get_sb: mount request failed with %d\n", ret);
    if (ret == -EINVAL)
    {
        gossip_err("Ensure that all pvfs2-servers have the "
                   "same FS configuration files\n");
        gossip_err("Look at pvfs2-client-core log file "
                   "(typically /tmp/pvfs2-client.log) for more details\n");
    }

    if (new_op)
    {
        op_release(new_op);
    }
    gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_get_sb: returning dentry %p\n", 
        mnt_sb_d);
    return mnt_sb_d;
}

static void pvfs2_flush_sb(struct super_block *sb)
{
    return;
}

void pvfs2_kill_sb( struct super_block *sb)
{
    gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_kill_sb: called\n");

    if (sb && !IS_ERR(sb))
    {
        /*
         * Flush any dirty inodes atimes, mtimes to server
         */
        pvfs2_flush_sb(sb);
        /*
          issue the unmount to userspace to tell it to remove the
          dynamic mount info it has for this superblock
        */
        pvfs2_unmount_sb(sb);

        /* remove the sb from our list of pvfs2 specific sb's */
        remove_pvfs2_sb(sb);

        /* prune dcache based on sb */
        shrink_dcache_sb(sb);

        /* provided sb cleanup */
        kill_litter_super(sb);

        /* release the allocated root dentry */
        if (sb->s_root)
        {
            dput(sb->s_root);
        }

        {
            int count1, count2;
            count1 = atomic_read(&(PVFS2_SB(sb)->pvfs2_inode_alloc_count));
            count2 = atomic_read(&(PVFS2_SB(sb)->pvfs2_inode_dealloc_count));
            if (count1 != count2) 
            {
                gossip_err("pvfs2_kill_sb: (WARNING) number of inode allocs "
                           "(%d) != number of inode deallocs (%d)\n",
                           count1, count2);
            }
            else
            {
                gossip_debug(GOSSIP_SUPER_DEBUG,
                             "pvfs2_kill_sb: (OK) number of inode allocs "
                             "(%d) = number of inode deallocs (%d)\n",
                             count1, count2);
            }
        }
        /* free the pvfs2 superblock private data */
        kfree(PVFS2_SB(sb));
    }
    else
    {
        gossip_debug(GOSSIP_SUPER_DEBUG,
                     "pvfs2_kill_sb: skipping due to invalid sb\n");
    }
    gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_kill_sb: returning normally\n");
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
