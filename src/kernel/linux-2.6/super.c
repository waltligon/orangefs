/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-kernel.h"

extern struct file_system_type pvfs2_fs_type;
extern struct dentry_operations pvfs2_dentry_operations;

extern kmem_cache_t *pvfs2_inode_cache;

extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;
extern wait_queue_head_t pvfs2_request_list_waitq;

extern void pvfs2_kill_sb(struct super_block *sb);

/* list for storing pvfs2 specific superblocks in use */
LIST_HEAD(pvfs2_superblocks);

/* used to protect the above superblock list */
spinlock_t pvfs2_superblocks_lock = SPIN_LOCK_UNLOCKED;

static int parse_mount_options(
   char *option_str, struct super_block *sb, int silent)
{
    char *ptr = option_str;
    pvfs2_sb_info_t *pvfs2_sb = NULL;
    int i = 0, j = 0, num_keywords = 0, got_device = 0;

    static char *keywords[] = {"intr"};
    static int num_possible_keywords = 1;
    static char options[PVFS2_MAX_NUM_OPTIONS][PVFS2_MAX_MOUNT_OPT_LEN];

    if (!silent)
    {
        if (option_str) 
        {
            pvfs2_print("pvfs2: parse_mount_options called with:\n");
            pvfs2_print(" %s\n", option_str);
        }
        else 
        {
            /* We need a non-NULL option string */
#ifdef PVFS2_LINUX_KERNEL_2_4
            pvfs2_error("*******************************************\n");
            pvfs2_error("Please pass the device name in the options "
                        "string of the mount program in 2.4.x kernels\n");
            pvfs2_error("e.g. mount -t pvfs2 pvfs2 /mnt/pvfs2 "
                        "-o tcp://localhost:3334/pvfs2-fs\n");
            pvfs2_error("*******************************************\n");
#endif
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
                pvfs2_error("Cannot parse mount time options (length "
                            "exceeded)\n");
                got_device = 0;
                goto exit;
            }

            if (*ptr == ',')
            {
                options[num_keywords++][j-1] = '\0';
                if (num_keywords == PVFS2_MAX_NUM_OPTIONS)
                {
                    pvfs2_error("Cannot parse mount time options (option "
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
                            pvfs2_print("pvfs2: mount option "
                                        "intr specified\n");
                        }
                        pvfs2_sb->mnt_options.intr = 1;
                        break;
                    }
                }
            }

            /* option string did not match any of the known keywords */
            if (j == num_possible_keywords)
            {
                /* assume we have a device name */
                if (got_device == 0)
                {
                    if (strlen(options[i]) >= PVFS_MAX_SERVER_ADDR_LEN)
                    {
                        pvfs2_error("Cannot parse mount time option %s "
                                    "(length exceeded)\n",options[i]);
                        goto exit;
                    }
                    strncpy(PVFS2_SB(sb)->devname, options[i],
                            strlen(options[i]));
                    got_device = 1;
                }
                else
                {
                    pvfs2_error("pvfs2: multiple device names specified: "
                                "ignoring %s\n", options[i]);
                }
            }
        }
    }
/*
  in 2.4.x, we require a devname in the options; in 2.6.x, parsed
  mount options are optional; always return success
*/
exit:
#ifdef PVFS2_LINUX_KERNEL_2_4
    return (got_device ? 0 : -EINVAL);
#else
    return 0;
#endif
}

#ifndef PVFS2_LINUX_KERNEL_2_4
static struct inode *pvfs2_alloc_inode(struct super_block *sb)
{
    struct inode *new_inode = NULL;
    pvfs2_inode_t *pvfs2_inode = NULL;

    /*
       this allocator has an associated constructor that fills in the
       internal vfs inode structure.  this initialization is extremely
       important and is required since we're allocating the inodes
       ourselves (rather than letting the system inode allocator
       initialize them for us); see inode.c/inode_init_once()
     */
    pvfs2_inode = kmem_cache_alloc(pvfs2_inode_cache,
                                   PVFS2_CACHE_ALLOC_FLAGS);
    if (pvfs2_inode)
    {
        new_inode = &pvfs2_inode->vfs_inode;
    }
    return new_inode;
}

static void pvfs2_destroy_inode(struct inode *inode)
{
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);

    pvfs2_print("pvfs2_destroy_inode: destroying inode %d\n",
                (int)inode->i_ino);

    
    pvfs2_inode_finalize(pvfs2_inode);
    kmem_cache_free(pvfs2_inode_cache, pvfs2_inode);
}

static void pvfs2_read_inode(
    struct inode *inode)
{
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);

    pvfs2_print("pvfs2_read_inode: (inode = %lu | ct = %d)\n",
                inode->i_ino, (int)atomic_read(&inode->i_count));

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
    if (pvfs2_inode_getattr(inode) != 0)
    {
        /* assume an I/O error and mark the inode as bad */
        pvfs2_make_bad_inode(inode);
    }
}

#else /* !PVFS2_LINUX_KERNEL_2_4 */

static void pvfs2_read_inode(
    struct inode *inode)
{
    pvfs2_inode_t *pvfs2_inode = NULL;

    pvfs2_print("pvfs2: pvfs2_read_inode called (inode = %lu | "
                "ct = %d)\n", inode->i_ino,
                (int)atomic_read(&inode->i_count));

    if (inode->u.generic_ip)
    {
        pvfs2_panic("Found an initialized inode in pvfs2_read_inode! "
                    "Should not have been initialized?\n");
        return;
    }

    /* Here we allocate the PVFS2 specific inode structure */
    pvfs2_inode = kmem_cache_alloc(pvfs2_inode_cache,
                                   PVFS2_CACHE_ALLOC_FLAGS);
    if (pvfs2_inode)
    {
        pvfs2_inode_initialize(pvfs2_inode);
        inode->u.generic_ip = pvfs2_inode;
        pvfs2_inode->vfs_inode = inode;
    }
    else
    {
        pvfs2_error("Could not allocate pvfs2_inode from "
                    "pvfs2_inode_cache\n");
        pvfs2_make_bad_inode(inode);
        return;
    }

    /* Need to do a getattr() on the inode */
    if (pvfs2_inode_getattr(inode) != 0)
    {
        /* flag any I/O errors */
        pvfs2_make_bad_inode(inode);
    }
}

static void pvfs2_clear_inode(struct inode *inode)
{
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);

    pvfs2_print("pvfs2_clear_inode called (ino %u | ct = %d | "
                "nlink = %d | pvfs2_inode = %p\n", (unsigned int)
                inode->i_ino, (int)atomic_read(&inode->i_count),
                (int)inode->i_nlink, pvfs2_inode);

    if (pvfs2_inode)
    {
        pvfs2_inode_finalize(pvfs2_inode);
        kmem_cache_free(pvfs2_inode_cache, pvfs2_inode);
    }
    inode->u.generic_ip = NULL;

    pvfs2_print("pvfs2_clear_inode finished\n");
}

#endif /* PVFS2_LINUX_KERNEL_2_4 */

/* called when the VFS removes this inode from the inode cache */
static void pvfs2_put_inode(
    struct inode *inode)
{
    pvfs2_print("pvfs2_put_inode: (%d | ct=%d | nlink=%d)\n",
                (int)inode->i_ino, (int)atomic_read(&inode->i_count),
                (int)inode->i_nlink);

    if (atomic_read(&inode->i_count) == 1)
    {
        /* kill dentries associated with this inode */
        d_prune_aliases(inode);
    }
}

/* called on sync ; make sure data is safe */
static void pvfs2_write_inode(
    struct inode *inode,
    int do_sync)
{
    pvfs2_print("pvfs2_write_inode: called (inode = %d)\n",
                (int)inode->i_ino);
}

/*
  NOTE: information filled in here is typically reflected in the
  output of the system command 'df'
*/
#ifdef PVFS2_LINUX_KERNEL_2_4
static int pvfs2_statfs(
    struct super_block *sb,
    struct statfs *buf)
#else
static int pvfs2_statfs(
    struct super_block *sb,
    struct kstatfs *buf)
#endif
{
    int ret = -ENOMEM, retries = 5;
    pvfs2_kernel_op_t *new_op = NULL;

    pvfs2_print("pvfs2_statfs: called on sb %p (fs_id is %d)\n",
                sb, (int)(PVFS2_SB(sb)->fs_id));

    new_op = op_alloc();
    if (!new_op)
    {
        return ret;
    }
    new_op->upcall.type = PVFS2_VFS_OP_STATFS;
    new_op->upcall.req.statfs.fs_id = PVFS2_SB(sb)->fs_id;

    service_operation_with_timeout_retry(
        new_op, "pvfs2_statfs", retries,
        PVFS2_SB(sb)->mnt_options.intr);

    if (new_op->downcall.status > -1)
    {
        pvfs2_print("pvfs2_statfs: got %ld blocks available | "
                    "%ld blocks total\n",
                    new_op->downcall.resp.statfs.blocks_avail,
                    new_op->downcall.resp.statfs.blocks_total);

        buf->f_type = sb->s_magic;
        buf->f_bsize = sb->s_blocksize;
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

#ifndef PVFS2_LINUX_KERNEL_2_4
        do
        {
            struct statfs tmp_statfs;

            buf->f_frsize = 1024;

            pvfs2_print("sizeof(kstatfs)=%d\n",sizeof(struct kstatfs));
            pvfs2_print("sizeof(kstatfs->f_blocks)=%d\n",
                        sizeof(buf->f_blocks));
            pvfs2_print("sizeof(statfs)=%d\n",sizeof(struct statfs));
            pvfs2_print("sizeof(statfs->f_blocks)=%d\n",
                        sizeof(tmp_statfs.f_blocks));
            pvfs2_print("sizeof(sector_t)=%d\n",sizeof(sector_t));

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
                
                pvfs2_print("pvfs2_statfs (T) got %ld files total | "
                            "%ld files_avail\n", buf->f_files,
                            buf->f_ffree);
            }
            else
            {
                pvfs2_print("pvfs2_statfs (N) got %lu files total | "
                            "%lu files_avail\n", buf->f_files,
                            buf->f_ffree);
            }
        } while(0);
#endif
        ret = pvfs2_kernel_error_code_convert(new_op->downcall.status);
    }

  error_exit:
    translate_error_if_wait_failed(ret, -ENOENT, 0);
    op_release(new_op);

    pvfs2_print("pvfs2_statfs: returning %d\n", ret);
    return ret;
}

/*
  the idea here is that given a valid superblock, we're
  re-initializing the user space client with the initial mount
  information specified when the super block was first initialized.
  this is very different than the first initialization/creation of a
  superblock.  we use the special service_priority_operation to make
  sure that the mount gets ahead of any other pending operation that
  is waiting for servicing.  this means that the pvfs2-client won't
  fail to start several times for all other pending operations before
  the client regains all of the mount information from us.
*/
int pvfs2_remount(
    struct super_block *sb,
    int *flags,
    char *data)
{
    int ret = -EINVAL;
    pvfs2_kernel_op_t *new_op = NULL;

    pvfs2_print("pvfs2_remount: called\n");

    if (sb && PVFS2_SB(sb))
    {
        if (data)
        {
            ret = parse_mount_options(data, sb, 1);
            if (ret)
            {
                return ret;
            }
        }

        new_op = op_alloc();
        if (!new_op)
        {
            return -ENOMEM;
        }
        new_op->upcall.type = PVFS2_VFS_OP_FS_MOUNT;
        strncpy(new_op->upcall.req.fs_mount.pvfs2_config_server,
                PVFS2_SB(sb)->devname, PVFS_MAX_SERVER_ADDR_LEN);

        pvfs2_print("Attempting PVFS2 Remount via host %s\n",
                    new_op->upcall.req.fs_mount.pvfs2_config_server);

        service_priority_operation(new_op, "pvfs2_remount", 0);
        ret = pvfs2_kernel_error_code_convert(new_op->downcall.status);

        pvfs2_print("pvfs2_remount: mount got return value of %d\n", ret);
        if (ret)
        {
            goto error_exit;
        }

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

      error_exit:
        translate_error_if_wait_failed(ret, 0, 0);
        op_release(new_op);
    }
    return ret;
}

struct super_operations pvfs2_s_ops =
{
#ifdef PVFS2_LINUX_KERNEL_2_4
    read_inode : pvfs2_read_inode,
    statfs : pvfs2_statfs,
    write_inode : pvfs2_write_inode,
    remount_fs : pvfs2_remount,
    put_super : pvfs2_kill_sb,
    clear_inode: pvfs2_clear_inode,
    put_inode: pvfs2_put_inode,
#else
    .drop_inode = generic_delete_inode,
    .alloc_inode = pvfs2_alloc_inode,
    .destroy_inode = pvfs2_destroy_inode,
    .read_inode = pvfs2_read_inode,
    .write_inode = pvfs2_write_inode,
    .put_inode = pvfs2_put_inode,
    .statfs = pvfs2_statfs,
    .remount_fs = pvfs2_remount
#endif
};

#ifdef PVFS2_LINUX_KERNEL_2_4

struct super_block* pvfs2_get_sb(
    struct super_block *sb,
    void *data,
    int silent)
{
    struct inode *root = NULL;
    struct dentry *root_dentry = NULL;
    pvfs2_kernel_op_t *new_op = NULL;
    char *dev_name = NULL;
    int ret = -EINVAL;

    if (!data || !sb)
    {
        if (!silent)
        {
            pvfs2_print("pvfs2_get_sb: no data parameter!\n");
        }
        goto error_exit;
    }
    else
    {
        /* alloc and init our private pvfs2 sb info */
        sb->u.generic_sbp = kmalloc(
            sizeof(pvfs2_sb_info_t), PVFS2_GFP_FLAGS);

        if (!PVFS2_SB(sb))
        {
            goto error_exit;
        }
        memset(sb->u.generic_sbp, 0, sizeof(pvfs2_sb_info_t));
        PVFS2_SB(sb)->sb = sb;

        ret = parse_mount_options(data, sb, silent);
        if (ret)
        {
            pvfs2_error("Failed to parse mount time options\n");
            goto error_exit;
        }
        dev_name = PVFS2_SB(sb)->devname;
    }

    new_op = op_alloc();
    if (!new_op)
    {
        ret = -ENOMEM;
        goto error_exit;
    }
    new_op->upcall.type = PVFS2_VFS_OP_FS_MOUNT;
    strncpy(new_op->upcall.req.fs_mount.pvfs2_config_server,
            dev_name, PVFS_MAX_SERVER_ADDR_LEN);

    pvfs2_print("Attempting PVFS2 Mount via host %s\n",
                new_op->upcall.req.fs_mount.pvfs2_config_server);

    service_operation(new_op, "pvfs2_get_sb", 0);
    ret = pvfs2_kernel_error_code_convert(new_op->downcall.status);

    pvfs2_print("pvfs2_remount: mount got return value of %d\n", ret);
    if (ret)
    {
        goto error_exit;
    }

    if ((new_op->downcall.resp.fs_mount.fs_id == PVFS_FS_ID_NULL) ||
        (new_op->downcall.resp.fs_mount.root_handle ==
         PVFS_HANDLE_NULL))
    {
        pvfs2_error("ERROR: Retrieved null fs_id or root_handle\n");
        ret = -EINVAL;
        goto error_exit;
    }

    PVFS2_SB(sb)->root_handle =
        new_op->downcall.resp.fs_mount.root_handle;
    PVFS2_SB(sb)->fs_id = new_op->downcall.resp.fs_mount.fs_id;
    PVFS2_SB(sb)->id = new_op->downcall.resp.fs_mount.id;

    sb->s_magic = PVFS2_MAGIC;
    sb->s_op = &pvfs2_s_ops;
    sb->s_type = &pvfs2_fs_type;

    sb->s_blocksize = PVFS2_BUFMAP_DEFAULT_DESC_SIZE;
    sb->s_blocksize_bits = PVFS2_BUFMAP_DEFAULT_DESC_SHIFT;
    sb->s_maxbytes = MAX_LFS_FILESIZE;

    /* alloc and initialize our root directory inode */
    root = pvfs2_get_custom_inode(
        sb, (S_IFDIR | 0755), 0, PVFS2_SB(sb)->root_handle);
    if (!root)
    {
        ret = -ENOMEM;
        goto error_exit;
    }
    PVFS2_I(root)->refn.fs_id = PVFS2_SB(sb)->fs_id;

    /* allocates and places root dentry in dcache */
    root_dentry = d_alloc_root(root);
    if (!root_dentry)
    {
        iput(root);
        ret = -ENOMEM;
        goto error_exit;
    }
    root_dentry->d_op = &pvfs2_dentry_operations;
    sb->s_root = root_dentry;

    /* finally, add this sb to our list of known pvfs2 sb's */
    add_pvfs2_sb(sb);

    op_release(new_op);
    return sb;

  error_exit:
    pvfs2_error("pvfs2_get_sb: mount request failed with %d\n", ret);

    if (sb)
    {
        if (sb->u.generic_sbp != NULL)
        {
            kfree(sb->u.generic_sbp);
        }
    }

    translate_error_if_wait_failed(ret, 0, 0);
    if (ret)
    {
        sb = NULL;
    }

    if (new_op)
    {
        op_release(new_op);
    }
    pvfs2_print("pvfs2_get_sb: returning sb %p\n", sb);
    return sb;
}

#else /* !PVFS2_LINUX_KERNEL_2_4 */

static struct export_operations pvfs2_export_ops = {};

int pvfs2_fill_sb(
    struct super_block *sb,
    void *data,
    int silent)
{
    int ret = -EINVAL;
    struct inode *root = NULL;
    struct dentry *root_dentry = NULL;
    pvfs2_mount_sb_info_t *mount_sb_info = (pvfs2_mount_sb_info_t *)data;

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
        ret = parse_mount_options(
            (char *)mount_sb_info->data, sb, silent);
        if (ret)
        {
            return ret;
        }
    }

    sb->s_magic = PVFS2_MAGIC;
    sb->s_op = &pvfs2_s_ops;
    sb->s_type = &pvfs2_fs_type;

    sb->s_blocksize = PVFS2_BUFMAP_DEFAULT_DESC_SIZE;
    sb->s_blocksize_bits = PVFS2_BUFMAP_DEFAULT_DESC_SHIFT;
    sb->s_maxbytes = MAX_LFS_FILESIZE;

    /* alloc and initialize our root directory inode */
    root = pvfs2_get_custom_inode(sb, (S_IFDIR | 0755),
                                  0, PVFS2_SB(sb)->root_handle);
    if (!root)
    {
        return -ENOMEM;
    }
    PVFS2_I(root)->refn.handle = PVFS2_SB(sb)->root_handle;
    PVFS2_I(root)->refn.fs_id = PVFS2_SB(sb)->fs_id;

    /* allocates and places root dentry in dcache */
    root_dentry = d_alloc_root(root);
    if (!root_dentry)
    {
        iput(root);
        return -ENOMEM;
    }
    root_dentry->d_op = &pvfs2_dentry_operations;

    sb->s_export_op = &pvfs2_export_ops;
    sb->s_root = root_dentry;
    return 0;
}

struct super_block *pvfs2_get_sb(
    struct file_system_type *fst,
    int flags,
    const char *devname,
    void *data)
{
    int ret = -EINVAL;
    struct super_block *sb = ERR_PTR(-EINVAL);
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_mount_sb_info_t mount_sb_info;

    pvfs2_print("pvfs2_get_sb: called with devname %s\n", devname);

    if (devname)
    {
        new_op = op_alloc();
        if (!new_op)
        {
            return ERR_PTR(-ENOMEM);
        }
        new_op->upcall.type = PVFS2_VFS_OP_FS_MOUNT;
        strncpy(new_op->upcall.req.fs_mount.pvfs2_config_server,
                devname, PVFS_MAX_SERVER_ADDR_LEN);

        pvfs2_print("Attempting PVFS2 Mount via host %s\n",
                    new_op->upcall.req.fs_mount.pvfs2_config_server);

        service_operation(new_op, "pvfs2_get_sb", 0);
        ret = pvfs2_kernel_error_code_convert(new_op->downcall.status);

        pvfs2_print("pvfs2_get_sb: mount got return value of %d\n", ret);
        if (ret)
        {
            goto error_exit;
        }

        if ((new_op->downcall.resp.fs_mount.fs_id == PVFS_FS_ID_NULL) ||
            (new_op->downcall.resp.fs_mount.root_handle ==
             PVFS_HANDLE_NULL))
        {
            pvfs2_error("ERROR: Retrieved null fs_id or root_handle\n");
            ret = -EINVAL;
            goto error_exit;
        }

        /* fill in temporary structure passed to fill_sb method */
        mount_sb_info.data = data;
        mount_sb_info.root_handle =
            new_op->downcall.resp.fs_mount.root_handle;
        mount_sb_info.fs_id = new_op->downcall.resp.fs_mount.fs_id;
        mount_sb_info.id = new_op->downcall.resp.fs_mount.id;

        /*
          the mount_sb_info structure looks odd, but it's used because
          the private sb info isn't allocated until we call
          pvfs2_fill_sb, yet we have the info we need to fill it with
          here.  so we store it temporarily and pass all of the info
          to fill_sb where it's properly copied out
        */
        sb = get_sb_nodev(
            fst, flags, (void *)&mount_sb_info, pvfs2_fill_sb);

        if (sb && !IS_ERR(sb) && (PVFS2_SB(sb)))
        {
            /* on successful mount, store the devname and data used */
            strncpy(PVFS2_SB(sb)->devname, devname,
                    PVFS_MAX_SERVER_ADDR_LEN);
            if (data)
            {
                strncpy(PVFS2_SB(sb)->data, data,
                        PVFS2_MAX_MOUNT_OPT_LEN);
            }

            /* finally, add this sb to our list of known pvfs2 sb's */
            add_pvfs2_sb(sb);
        }
    }
    else
    {
        pvfs2_error("ERROR: device name not specified.\n");
    }

    op_release(new_op);
    return sb;

  error_exit:
    pvfs2_error("pvfs2_get_sb: mount request failed with %d\n", ret);

    translate_error_if_wait_failed(ret, 0, 0);
    if (ret || IS_ERR(sb))
    {
        sb = ERR_PTR(ret);
    }

    if (new_op)
    {
        op_release(new_op);
    }
    pvfs2_print("pvfs2_get_sb: returning sb %p\n", sb);
    return sb;
}
#endif /* PVFS2_LINUX_KERNEL_2_4 */

void pvfs2_kill_sb(
    struct super_block *sb)
{
    pvfs2_print("pvfs2_kill_sb: called\n");

    if (sb && !IS_ERR(sb))
    {
        /*
          issue the unmount to userspace to tell it to remove the
          dynamic mount info it has for this superblock
        */
        pvfs2_unmount_sb(sb);

        /* remove the sb from our list of pvfs2 specific sb's */
        remove_pvfs2_sb(sb);

#ifndef PVFS2_LINUX_KERNEL_2_4
        /* prune dcache based on sb */
        shrink_dcache_sb(sb);

        /* provided sb cleanup */
        kill_litter_super(sb);

        /* release the allocated root dentry */
        if (sb->s_root)
        {
            dput(sb->s_root);
        }

        /* free the pvfs2 superblock private data */
        kfree(PVFS2_SB(sb));
#else
        sb->u.generic_sbp = NULL;
#endif
    }
    else
    {
        pvfs2_print("pvfs2_kill_sb: skipping due to invalid sb\n");
    }
    pvfs2_print("pvfs2_kill_sb: returning normally\n");
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
