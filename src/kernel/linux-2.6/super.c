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

/* used to protect the above superblock list */
#ifdef HAVE_SPIN_LOCK_UNLOCKED
spinlock_t pvfs2_superblocks_lock = SPIN_LOCK_UNLOCKED;
#else
DEFINE_SPINLOCK(pvfs2_superblocks_lock);
#endif /* HAVE_SPIN_LOCK_UNLOCKED */

#ifdef HAVE_GET_FS_KEY_SUPER_OPERATIONS
static void pvfs2_sb_get_fs_key(struct super_block *sb, char **ppkey, int *keylen);
#endif

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
#ifdef PVFS2_LINUX_KERNEL_2_4
            gossip_err("*******************************************\n");
            gossip_err("Please pass the device name in the options "
                        "string of the mount program in 2.4.x kernels\n");
            gossip_err("e.g. mount -t pvfs2 pvfs2 /mnt/pvfs2 "
                        "-o tcp://localhost:3334/pvfs2-fs\n");
            gossip_err("*******************************************\n");
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
#ifdef PVFS2_LINUX_KERNEL_2_4
                /* assume we have a device name */
                if (got_device == 0)
                {
                    if (strlen(options[i]) >= PVFS_MAX_SERVER_ADDR_LEN)
                    {
                        gossip_err("Cannot parse mount time option %s "
                                    "(length exceeded)\n",options[i]);
                        goto exit;
                    }
                    strncpy(PVFS2_SB(sb)->devname, options[i],
                            strlen(options[i]));
                    got_device = 1;
                }
                else
                {
                    gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2: multiple device names specified: "
                                "ignoring %s\n", options[i]);
                }
#else
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
#endif
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
    char *s;

    if (pvfs2_inode)
    {
        s = kzalloc(HANDLESTRINGSIZE, GFP_KERNEL);
        gossip_debug(GOSSIP_SUPER_DEBUG,
                     "pvfs2_destroy_inode: deallocated %p "
                     "destroying inode %s\n",
                     pvfs2_inode,
                     k2s(get_khandle_from_ino(inode),s));
        kfree(s);

        atomic_inc(&(PVFS2_SB(inode->i_sb)->pvfs2_inode_dealloc_count));
        pvfs2_inode_finalize(pvfs2_inode);
        pvfs2_inode_release(pvfs2_inode);
    }
}

void pvfs2_read_inode(
    struct inode *inode)
{
    int ret;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);
    char *s = kzalloc(HANDLESTRINGSIZE, GFP_KERNEL);

    gossip_debug(GOSSIP_SUPER_DEBUG,
                 "pvfs2_read_inode: %p (inode = %s | ct = %d)\n",
                 pvfs2_inode,
                 k2s(get_khandle_from_ino(inode),s),
                 (int)atomic_read(&inode->i_count));

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
    if ((ret=pvfs2_inode_getattr(inode, PVFS_ATTR_SYS_ALL_NOHINT)) != 0)
    {
        /* assume an I/O error and mark the inode as bad */
        memset(s,0,HANDLESTRINGSIZE);
        gossip_debug(GOSSIP_SUPER_DEBUG,
                     "%s:%s:%d calling make bad inode - [%p] "
                     "(inode = %s | ct = %di | ct = %d)\n",
                     __FILE__,
                     __func__,
                     __LINE__,
                     pvfs2_inode,
                     k2s(get_khandle_from_ino(inode),s),
                     (int)atomic_read(&inode->i_count),
                     ret);
        pvfs2_make_bad_inode(inode);
    }
    kfree(s);
}

#else /* !PVFS2_LINUX_KERNEL_2_4 */

void pvfs2_read_inode(
    struct inode *inode)
{
    pvfs2_inode_t *pvfs2_inode = NULL;
    void *ptr = NULL;
    char *s = kzalloc(HANDLESTRINGSIZE, GFP_KERNEL);

#if !defined(HAVE_IGET4_LOCKED) && !defined(HAVE_IGET_LOCKED)
    if (inode->u.generic_ip)
    {
        gossip_err("ERROR! Found an initialized inode in pvfs2_read_inode! "
                    "Should not have been initialized?\n");
        return;
    }
#else
    ptr = inode->u.generic_ip;
#endif

    /* Here we allocate the PVFS2 specific inode structure */
    pvfs2_inode = pvfs2_inode_alloc();
    if (pvfs2_inode)
    {
        pvfs2_inode_initialize(pvfs2_inode);
        inode->u.generic_ip = pvfs2_inode;
        pvfs2_inode->vfs_inode = inode;
        inode->i_flags &= ~(S_APPEND|S_IMMUTABLE|S_NOATIME);
        /* Initialize the handle id to be looked up in the case of iget4_locked
         * and iget_locked functions, since they are not done elsewhere 
         */
#if defined(HAVE_IGET4_LOCKED) || defined(HAVE_IGET_LOCKED)
        if (ptr == NULL) {
            gossip_err("Warning! We don't have the reference to the pvfs2 object handle.. using iget4/iget(locked) interface\n");
        }
        pvfs2_set_inode(inode, ptr);
#endif
        if (pvfs2_inode_getattr(inode, PVFS_ATTR_SYS_ALL_NOHINT) != 0)
        {
            gossip_debug(GOSSIP_SUPER_DEBUG,
                         "%s:%s:%d calling make bad inode - [%p] "
                         "(inode = %s | ct = %d)\n",
                         __FILE__,
                         __func__,
                         __LINE__,
                         pvfs2_inode,
                         k2s(get_khandle_from_ino(inode),s),
                         (int)atomic_read(&inode->i_count));
            pvfs2_make_bad_inode(inode);
        }
        else {
            gossip_debug(GOSSIP_SUPER_DEBUG,
                         "pvfs2: pvfs2_read_inode: allocated %p "
                         "(inode = %s | ct = %d)\n",
                         pvfs2_inode,
                         k2s(get_khandle_from_ino(inode),s),
                         (int)atomic_read(&inode->i_count));
        }
    }
    else
    {
        gossip_err("%s:%s:%d "
                   "Could not allocate pvfs2_inode from pvfs2_inode_cache. "
                   "calling make bad inode - [%p] (inode = %llu | ct = %d)\n",
                   __FILE__,
                   __func__,
                   __LINE__,
                   pvfs2_inode,
                   k2s(get_khandle_from_ino(inode),s),
                   (int)atomic_read(&inode->i_count));
        pvfs2_make_bad_inode(inode);
    }
    kfree(s);
}

static void pvfs2_clear_inode(struct inode *inode)
{
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);
    char *s = kzalloc(HANDLESTRINGSIZE, GFP_KERNEL);

    gossip_debug(GOSSIP_SUPER_DEBUG,
                 "pvfs2_clear_inode: deallocated %p, destroying inode %llu\n",
                 pvfs2_inode,
                 k2s(get_khandle_from_ino(inode),s));
    kfree(s);

    pvfs2_inode_finalize(pvfs2_inode);
    pvfs2_inode_release(pvfs2_inode);
    inode->u.generic_ip = NULL;
}

#endif /* PVFS2_LINUX_KERNEL_2_4 */

#ifdef HAVE_PUT_INODE
/* called when the VFS removes this inode from the inode cache */
static void pvfs2_put_inode(
    struct inode *inode)
{
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);
    char *s = kzalloc(HANDLESTRINGSIZE, GFP_KERNEL);

    gossip_debug(GOSSIP_SUPER_DEBUG,
                 "pvfs2_put_inode: pvfs2_inode: %p "
                 "(inode = %s) = %d (nlink=%d)\n",
                 pvfs2_inode,
                 k2s(get_khandle_from_ino(inode),s),
                 (int)atomic_read(&inode->i_count),
                 (int)inode->i_nlink);
    kfree(s);

    if (atomic_read(&inode->i_count) == 1)
    {
        /* kill dentries associated with this inode */
        d_prune_aliases(inode);
#ifdef PVFS2_LINUX_KERNEL_2_4
        /* if the inode has entered the "bad" state, go ahead and force a
         * delete.  This will allow us a chance to retry access to it later
         * rather than caching the bad state indefinitely
         */
        /* NOTE: 2.6 kernels don't seem to have this problem.  The analogous
         * function if it were needed would be generic_delete_inode() rather
         * than force_delete(), however.
         */
        if(is_bad_inode(inode))
        {
            force_delete(inode);
        }
#endif
    }
}
#endif /* HAVE_PUT_INODE */

#ifdef HAVE_STATFS_LITE_SUPER_OPERATIONS
static int pvfs2_statfs_lite(
        struct super_block *sb,
        struct kstatfs *buf,
        int statfs_mask)
{
    /* 
     * statfs_mask indicates the fields that we are expected to fill up.
     * We do not need a network message for the following masks,
     * STATFS_M_TYPE, STATFS_M_FSID, STATFS_M_NAMELEN, STATFS_M_FRSIZE, STATFS_M_BSIZE.
     * Everything else use the regular statfs call.
     */
    if ((statfs_mask & STATFS_M_BLOCKS)     ||
            (statfs_mask & STATFS_M_BFREE)  ||
            (statfs_mask & STATFS_M_BAVAIL) ||
            (statfs_mask & STATFS_M_FILES)  ||
            (statfs_mask & STATFS_M_FFREE))
        return -ENOSYS;

    if (statfs_mask & STATFS_M_TYPE) {
        buf->f_type = sb->s_magic;
    }
    if (statfs_mask & STATFS_M_FSID) {
        /* stash the fsid as well */
        memcpy(&buf->f_fsid, &(PVFS2_SB(sb)->fs_id), 
               sizeof(PVFS2_SB(sb)->fs_id));      
    }
    if (statfs_mask & STATFS_M_BSIZE) {
        buf->f_bsize = sb->s_blocksize;
    }
    if (statfs_mask & STATFS_M_NAMELEN) {
        buf->f_namelen = PVFS2_NAME_LEN;
    }
    if (statfs_mask & STATFS_M_FRSIZE) {
        buf->f_frsize = sb->s_blocksize;
    }
    return 0;
}
#endif

/*
  NOTE: information filled in here is typically reflected in the
  output of the system command 'df'
*/
#ifdef PVFS2_LINUX_KERNEL_2_4
static int pvfs2_statfs(
    struct super_block *psb,
    struct statfs *buf)
#else
#ifdef HAVE_DENTRY_STATFS_SOP
static int pvfs2_statfs(
    struct dentry *dentry,
    struct kstatfs *buf)
#else
static int pvfs2_statfs(
    struct super_block *psb,
    struct kstatfs *buf)
#endif
#endif
{
    int ret = -ENOMEM;
    pvfs2_kernel_op_t *new_op = NULL;
    int flags = 0;
    struct super_block *sb = NULL;

#if !defined(PVFS2_LINUX_KERNEL_2_4) && defined(HAVE_DENTRY_STATFS_SOP)
    sb = dentry->d_sb;
#else
    sb = psb;
#endif

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

#ifndef PVFS2_LINUX_KERNEL_2_4
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
#endif
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
#if !defined(PVFS2_LINUX_KERNEL_2_4) && defined(HAVE_GENERIC_GETXATTR) && defined(CONFIG_FS_POSIX_ACL)
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
#if !defined(PVFS2_LINUX_KERNEL_2_4) && defined(HAVE_GENERIC_GETXATTR) && defined(CONFIG_FS_POSIX_ACL)
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

#ifdef HAVE_GET_FS_KEY_SUPER_OPERATIONS

static int fskey_table_size = 11;
/* hash table for mapping fsids to keys */
struct qhash_table *fskey_table;

struct fskey_entry {
    PVFS_fs_id      fsid;
    char            *fs_key;
    int             fs_keylen;
    struct list_head list;
};

static struct fskey_entry *fskey_entry_ctor(PVFS_fs_id fsid)
{
    struct fskey_entry * entry;

    entry = kmalloc(sizeof(*entry), PVFS2_GFP_FLAGS);
    if (entry) {
        entry->fsid = fsid;
        entry->fs_keylen = PVFS2_MAX_FSKEY_LEN;
        entry->fs_key = (char *) kmalloc(entry->fs_keylen, PVFS2_GFP_FLAGS);
        if (!entry->fs_key) {
            kfree(entry);
            entry = NULL;
            goto out;
        }
    }
out:
    return entry;
}

static void fskey_entry_dtor(struct fskey_entry * entry)
{
    if (entry) {
        if (entry->fs_key) {
            kfree(entry->fs_key);
            entry->fs_key = NULL;
        }
        entry->fs_keylen = 0;
        kfree(entry);
    }
    return;
}

static int fskey_fsid_func(void *key, int table_size)
{
    int tmp = 0;
    PVFS_fs_id *fsid = (PVFS_fs_id *)key;
    tmp += (int)(*fsid);
    tmp = (tmp % table_size);
    return tmp;
}

static int fskey_fsid_compare(void *key, struct qhash_head *link)
{
    PVFS_fs_id *fsid = (PVFS_fs_id *)key;
    struct fskey_entry *entry = qhash_entry(
            link, struct fskey_entry, list);
    return (entry->fsid == *fsid);
}

int fsid_key_table_initialize(void)
{
    fskey_table = qhash_init(fskey_fsid_compare, fskey_fsid_func, fskey_table_size);
    if (!fskey_table)
    {
        gossip_err("Failed to initialize fsid/fskey hashtable");
        return -ENOMEM;
    }
    return 0;
}

void fsid_key_table_finalize(void)
{
    int i;

    if (fskey_table == NULL)
        return;
    for (i = 0; i < fskey_table_size; i++) {
        struct qhash_head *hash_link;
        do {
            hash_link = qhash_search_and_remove_at_index(
                    fskey_table, i);
            if (hash_link)
            {
                struct fskey_entry *entry;
                entry = qhash_entry(hash_link, struct fskey_entry, list);
                fskey_entry_dtor(entry);
            }
        } while (hash_link);
    }
    qhash_finalize(fskey_table);
    fskey_table = NULL;
    return;
}


/* Issue an upcall in case we dont have the fsid<->key mappings */
static int pvfs2_get_fs_key(PVFS_fs_id fsid, char *fs_key, int *fs_keylen)
{
    pvfs2_kernel_op_t *new_op = NULL;
    int ret;

    gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2: pvfs2_get_fs_key fsid is %d\n", fsid);

    new_op = op_alloc(PVFS2_VFS_OP_FSKEY);
    if (!new_op)
    {
        return -ENOMEM;
    }
    new_op->upcall.req.fs_key.fsid = fsid;

    ret = service_operation(new_op, "pvfs2_get_fs_key", PVFS2_OP_INTERRUPTIBLE);

    /* copy the keys and key lengths */
    if (ret == 0)
    {
        int copy_fskeylen;

        copy_fskeylen = new_op->downcall.resp.fs_key.fs_keylen > *fs_keylen 
                        ? *fs_keylen : new_op->downcall.resp.fs_key.fs_keylen;
        memcpy(fs_key, new_op->downcall.resp.fs_key.fs_key, copy_fskeylen);
        *fs_keylen = copy_fskeylen;
    }

    op_release(new_op);
    return ret;
}

/*
 * VFS requests us to fetch a key for a particular superblock/file-system.
 * We lookup our hash-table to determine if we have cached the keys for the 
 * file system and if not we issue an upcall to retrieve the key.
 */
static void pvfs2_sb_get_fs_key(struct super_block *sb, char **ppkey, int *keylen)
{
    struct qhash_head *hash_link;
    PVFS_fs_id fsid = PVFS2_SB(sb)->fs_id;
    struct fskey_entry *entry;

    if (fskey_table == NULL) {
        gossip_err("fskey_table not initialized?\n");
        if (ppkey) {
            *ppkey = NULL;
        }
        if (keylen) {
            *keylen = 0;
        }
        return;
    }
    gossip_debug(GOSSIP_SUPER_DEBUG, "Search fskey_table for fsid %d\n", fsid);
    hash_link = qhash_search(fskey_table, &fsid);
    if (hash_link)
    {
        /* Found an entry in the hash table */
        entry = qhash_entry(hash_link, struct fskey_entry, list);
        if (entry->fsid != fsid) {
            gossip_err("pvfs2_sb_get_fs_key: fsid did not match!?\n");
            if (ppkey) {
                *ppkey = NULL;
            }
            if (keylen) {
                *keylen = 0;
            }
            return;
        }
        if (ppkey) {
            *ppkey = entry->fs_key;
        }
        if (keylen) {
            *keylen = entry->fs_keylen;
        }
        gossip_debug(GOSSIP_SUPER_DEBUG, "Cached key for FSID %d - %d\n", fsid, entry->fs_keylen);
        return;
    }
    /* Allocate an entry for this fsid */
    if ((entry = fskey_entry_ctor(fsid)) == NULL) {
        if (ppkey) {
            *ppkey = NULL;
        }
        if (keylen) {
            *keylen = 0;
        }
        return;
    }
    /* Send an upcall to retrieve the key for this fsid */
    if (pvfs2_get_fs_key(fsid, entry->fs_key, &entry->fs_keylen) < 0) {
        fskey_entry_dtor(entry);
        if (ppkey) {
            *ppkey = NULL;
        }
        if (keylen) {
            *keylen = 0;
        }
        return;
    }
    /*
     * Finally add it to the hash table. NOTE: struct fskey_entry's are freed
     * only when the fskey_table is finalized at module unload time.
     */
    qhash_add(fskey_table, (void *) &(entry->fsid), &(entry->list));
    if (ppkey) {
        *ppkey  = entry->fs_key;
    }
    /* NOTE: fs_keylen can be 0 in case the FS does not have a secret key */
    if (keylen) {
        *keylen = entry->fs_keylen;
    }
    gossip_debug(GOSSIP_SUPER_DEBUG, "Uncached key for FSID %d - %d\n", entry->fsid, entry->fs_keylen);
    return;
}

#else

int fsid_key_table_initialize(void)
{
    return 0;
}

void fsid_key_table_finalize(void)
{
    return;
}

#endif

/* Called whenever the VFS dirties the inode in response to atime updates */
static void pvfs2_dirty_inode(struct inode *inode
#ifdef HAVE_DIRTY_INODE_FLAGS
                              ,int flags
#endif
                             )
{
    if (inode)
    {
        pvfs2_inode_t *pvfs2_inode = PVFS2_I(inode);
        char *s = kzalloc(HANDLESTRINGSIZE, GFP_KERNEL);

        gossip_debug(GOSSIP_SUPER_DEBUG,
                     "pvfs2_dirty_inode: %s\n",
                     k2s(get_khandle_from_ino(inode),s));
        kfree(s);
        SetAtimeFlag(pvfs2_inode);
    }
    return;
}

struct super_operations pvfs2_s_ops =
{
#ifdef PVFS2_LINUX_KERNEL_2_4
    read_inode : pvfs2_read_inode,
    statfs : pvfs2_statfs,
    remount_fs : pvfs2_remount_fs,
    put_super : pvfs2_kill_sb,
    dirty_inode : pvfs2_dirty_inode,
    clear_inode: pvfs2_clear_inode,
    put_inode: pvfs2_put_inode,
#else
#ifdef HAVE_DROP_INODE
    .drop_inode = generic_delete_inode,
#endif
    .alloc_inode = pvfs2_alloc_inode,
    .destroy_inode = pvfs2_destroy_inode,
#ifdef HAVE_READ_INODE
    .read_inode = pvfs2_read_inode,
#endif
    .dirty_inode = pvfs2_dirty_inode,
#ifdef HAVE_PUT_INODE
    .put_inode = pvfs2_put_inode,
#endif
    .statfs = pvfs2_statfs,
    .remount_fs = pvfs2_remount_fs,
#ifdef HAVE_FIND_INODE_HANDLE_SUPER_OPERATIONS
    .find_inode_handle = pvfs2_sb_find_inode_handle,
#endif
#ifdef HAVE_GET_FS_KEY_SUPER_OPERATIONS
    .get_fs_key = pvfs2_sb_get_fs_key,
#endif
#ifdef HAVE_STATFS_LITE_SUPER_OPERATIONS
    .statfs_lite = pvfs2_statfs_lite,
#endif
#endif
};

#ifdef PVFS2_LINUX_KERNEL_2_4

struct super_block* pvfs2_get_sb(struct super_block *sb,
                                 void *data,
                                 int silent)
{
    struct inode *root = NULL;
    struct dentry *root_dentry = NULL;
    pvfs2_kernel_op_t *new_op = NULL;
    char *dev_name = NULL;
    int ret = -EINVAL;
    PVFS_object_kref root_object;
    char *s;

    if (!data || !sb)
    {
        if (!silent)
        {
            gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_get_sb: no data parameter!\n");
        }
        goto error_exit;
    }
    else
    {
        /* alloc and init our private pvfs2 sb info */
        sb->u.generic_sbp = kmalloc(sizeof(pvfs2_sb_info_t),
                                    PVFS2_GFP_FLAGS);

        if (!PVFS2_SB(sb))
        {
            goto error_exit;
        }
        memset(sb->u.generic_sbp, 0, sizeof(pvfs2_sb_info_t));
        PVFS2_SB(sb)->sb = sb;

        ret = parse_mount_options(data, sb, silent);
        if (ret)
        {
            gossip_err("Failed to parse mount time options\n");
            goto error_exit;
        }
        sb->s_flags = ((sb->s_flags & ~MS_NOATIME)  |
            ((PVFS2_SB(sb)->mnt_options.noatime == 1) ? MS_NOATIME : 0));
        sb->s_flags = ((sb->s_flags & ~MS_NODIRATIME) |
            ((PVFS2_SB(sb)->mnt_options.nodiratime == 1) ? MS_NODIRATIME : 0));
        dev_name = PVFS2_SB(sb)->devname;
    }

    new_op = op_alloc(PVFS2_VFS_OP_FS_MOUNT);
    if (!new_op)
    {
        ret = -ENOMEM;
        goto error_exit;
    }
    strncpy(new_op->upcall.req.fs_mount.pvfs2_config_server,
            dev_name,
            PVFS_MAX_SERVER_ADDR_LEN);

    gossip_debug(GOSSIP_SUPER_DEBUG, "Attempting PVFS2 Mount via host %s\n",
                 new_op->upcall.req.fs_mount.pvfs2_config_server);

    ret = service_operation(new_op, "pvfs2_get_sb", 0);

    gossip_debug(GOSSIP_SUPER_DEBUG,
                 "%s: mount got return value of %d\n", __func__, ret);
    if (ret)
    {
        goto error_exit;
    }

    if ((new_op->downcall.resp.fs_mount.fs_id == PVFS_FS_ID_NULL) ||
        (new_op->downcall.resp.fs_mount.root_khandle.slice[0] +
         new_op->downcall.resp.fs_mount.root_khandle.slice[3] == 0))
    {
        gossip_err("ERROR: retrieved null fs_id or root_handle\n");
        ret = -EINVAL;
        goto error_exit;
    }

    PVFS2_SB(sb)->root_khandle = new_op->downcall.resp.fs_mount.root_khandle;
    PVFS2_SB(sb)->fs_id = new_op->downcall.resp.fs_mount.fs_id;
    PVFS2_SB(sb)->id = new_op->downcall.resp.fs_mount.id;

    sb->s_magic = PVFS2_SUPER_MAGIC;
    sb->s_op = &pvfs2_s_ops;
    sb->s_type = &pvfs2_fs_type;
#ifdef HAVE_D_SET_D_OP
    sb->s_d_op = &pvfs2_dentry_operations;
#endif

    sb->s_blocksize = pvfs_bufmap_size_query();
    sb->s_blocksize_bits = pvfs_bufmap_shift_query();
    sb->s_maxbytes = (unsigned long long) 1 << 63;

    root_object.khandle = PVFS2_SB(sb)->root_khandle;
    root_object.fs_id  = PVFS2_SB(sb)->fs_id;

    s = kzalloc(HANDLESTRINGSIZE, GFP_KERNEL);
    gossip_debug(GOSSIP_SUPER_DEBUG,
                 "get inode %s, fsid %d\n",
                 k2s(&(root_object.khandle),s),
                 root_object.fs_id);
    kfree(s);
    /* alloc and initialize our root directory inode by explicitly requesting
     * the sticky bit to be set */
    root = pvfs2_get_custom_core_inode(sb,
                                       NULL,
                                       (S_IFDIR | 0755 | S_ISVTX),
                                       0,
                                       root_object);
    if (!root)
    {
        ret = -ENOMEM;
        goto error_exit;
    }
    gossip_debug(GOSSIP_SUPER_DEBUG, "%s:Allocated root inode [%p] with mode %o\n",__func__,
                 root, root->i_mode);

    /* allocates and places root dentry in dcache */
    root_dentry = d_alloc_root(root);
    if (!root_dentry)
    {
        iput(root);
        ret = -ENOMEM;
        goto error_exit;
    }
#ifndef HAVE_D_SET_D_OP
    root_dentry->d_op = &pvfs2_dentry_operations;
#endif
    sb->s_root = root_dentry;

    /* finally, add this sb to our list of known pvfs2 sb's */
    add_pvfs2_sb(sb);

    op_release(new_op);
    return sb;

error_exit:
    gossip_err("pvfs2_get_sb: mount request failed with %d\n", ret);
    if (ret == -EINVAL)
    {
        gossip_err("Ensure that all pvfs2-servers have the same FS configuration files\n");
        gossip_err("Look at pvfs2-client-core log file (typically /tmp/pvfs2-client.log) for more details\n");
    }

    if (sb)
    {
        if (sb->u.generic_sbp != NULL)
        {
            kfree(sb->u.generic_sbp);
        }
    }

    if (ret)
    {
        sb = NULL;
    }

    if (new_op)
    {
        op_release(new_op);
    }
    gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_get_sb: returning sb %p\n", sb);
    return sb;
}

#else /* !PVFS2_LINUX_KERNEL_2_4 */

#ifdef HAVE_FHTODENTRY_EXPORT_OPERATIONS
struct dentry *pvfs2_fh_to_dentry(struct super_block *sb,
                                  struct fid *fid,
                                  int fh_len, int fh_type)
{
   PVFS_object_kref refn;
   struct inode *inode;
   struct dentry *dentry;
   char *s = kzalloc(HANDLESTRINGSIZE, GFP_KERNEL);

   if (fh_len < 5 || fh_type > 2) 
   {
      return NULL;
   }

   PVFS_khandle_from(&(refn.khandle), fid->raw, 16);
   refn.fs_id = (u32) fid->raw[4];

   gossip_debug(GOSSIP_SUPER_DEBUG,
                "fh_to_dentry: handle %s, fs_id %d\n",
                k2s(&(refn.khandle),s),
                refn.fs_id);
   kfree(s);

   inode = pvfs2_iget(sb, &refn);

#ifdef HAVE_D_ALLOC_ANON
   if (inode == NULL)
   {
      return ERR_PTR(-ESTALE);
   }
   if (IS_ERR(inode))
   {
      return (void *) inode;
   }
   dentry = d_alloc_anon(inode);
   if (dentry == NULL)
   {
      iput(inode);
      return ERR_PTR(-ENOMEM);
   }
#else
   dentry = d_obtain_alias(inode);
   if(dentry == NULL)
   {
       return ERR_PTR(-ENOMEM);
   }
#endif

#ifndef HAVE_D_SET_D_OP
   dentry->d_op = &pvfs2_dentry_operations;
#endif

   return dentry;
}
#endif /* HAVE_FHTODENTRY_EXPORT_OPERATIONS */

#ifdef HAVE_ENCODEFH_EXPORT_OPERATIONS
#ifdef PVFS_ENCODE_FS_USES_DENTRY
int pvfs2_encode_fh(struct dentry *dentry,
                    __u32 *fh,
                    int *max_len,
                    int connectable)
{
   struct inode *inode = dentry->d_inode;
   int len = *max_len;
   int type = 1;
   PVFS_object_kref refn;
   u32 generation;
   char *s;

   /*
    * if connectable is specified, parent handle identity has to be stashed
    * as well.
    */
   if (len < 5 || (connectable && len < 10))
   {
      gossip_lerr("fh buffer is too small for encoding\n");
      type = 255;
      goto out;
   }

   refn = PVFS2_I(inode)->refn;
   generation = inode->i_generation;
   s = kzalloc(HANDLESTRINGSIZE, GFP_KERNEL);
   gossip_debug(GOSSIP_SUPER_DEBUG,
                "Encoding fh: handle %s, gen %u, fsid %u\n",
                k2s(&(refn.khandle),s),
                generation,
                refn.fs_id);
   kfree(s);

   len = 5;
   PVFS_khandle_to(&refn.khandle, fh, 16);
   fh[4] = refn.fs_id;

   if (connectable && !S_ISDIR(inode->i_mode))
   {
      struct inode *parent;

      spin_lock(&dentry->d_lock);

      parent = dentry->d_parent->d_inode;
      refn = PVFS2_I(parent)->refn;
      generation = parent->i_generation;
      PVFS_khandle_to(&refn.khandle, (char *) fh + 20, 16);
      fh[9] = refn.fs_id;

      spin_unlock(&dentry->d_lock);
      len = 6;
      type = 2;
      memset(s,0,HANDLESTRINGSIZE);
      gossip_debug(GOSSIP_SUPER_DEBUG,
                   "Encoding parent: handle %s, gen %u, fsid %u\n",
                   k2s(&(refn.khandle),s),
                   generation,
                   refn.fs_id);
      kfree(s);
   }
   *max_len = len;

out:
   return type;
}
#else
int pvfs2_encode_fh(struct inode *inode,
                    __u32 *fh,
                    int *max_len,
                    struct inode *parent)
{
   int len = parent ? 10 : 5;
   int type = 1;
   PVFS_object_kref refn;
   char *s = kmalloc(HANDLESTRINGSIZE, GFP_KERNEL);

   if (*max_len < len) {
     gossip_lerr("fh buffer is too small for encoding\n");
     *max_len = len;
     type =  255;
     goto out;
   }

   refn = PVFS2_I(inode)->refn;
   PVFS_khandle_to(&refn.khandle, fh, 16);
   fh[4] = refn.fs_id;

   memset(s,0,HANDLESTRINGSIZE);
   gossip_debug(GOSSIP_SUPER_DEBUG,
                "Encoding fh: handle %s, fsid %u\n",
                k2s(&refn.khandle,s),
                refn.fs_id);

   if (parent)
   {
      refn = PVFS2_I(parent)->refn;
      PVFS_khandle_to(&refn.khandle, (char *) fh + 20, 16);
      fh[9] = refn.fs_id;

      type = 2;
      memset(s,0,HANDLESTRINGSIZE);
      gossip_debug(GOSSIP_SUPER_DEBUG,
                   "Encoding parent: handle %s, fsid %u\n",
                   k2s(&refn.khandle,s),
                   refn.fs_id);
   }
   *max_len = len;

out:
   kfree(s);
   return type;
}
#endif /* PVFS_ENCODE_FS_USES_DENTRY */
#endif /* HAVE_ENCODEFH_EXPORT_OPERATIONS */

static struct export_operations pvfs2_export_ops =
{
#ifdef HAVE_ENCODEFH_EXPORT_OPERATIONS
   .encode_fh    = pvfs2_encode_fh,
#endif
#ifdef HAVE_FHTODENTRY_EXPORT_OPERATIONS
   .fh_to_dentry = pvfs2_fh_to_dentry,
#endif
};

int pvfs2_fill_sb(struct super_block *sb,
                  void *data,
                  int silent)
{
    int ret = -EINVAL;
    struct inode *root = NULL;
    struct dentry *root_dentry = NULL;
    pvfs2_mount_sb_info_t *mount_sb_info = (pvfs2_mount_sb_info_t *)data;
    PVFS_object_kref root_object;
    char *s;

    /* alloc and init our private pvfs2 sb info */
    sb->s_fs_info = kmalloc(sizeof(pvfs2_sb_info_t), PVFS2_GFP_FLAGS);
    if (!PVFS2_SB(sb))
    {
        return -ENOMEM;
    }
    memset(sb->s_fs_info, 0, sizeof(pvfs2_sb_info_t));
    PVFS2_SB(sb)->sb = sb;

    PVFS2_SB(sb)->root_khandle = mount_sb_info->root_khandle;
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

#if defined(HAVE_GENERIC_GETXATTR) && defined(CONFIG_FS_POSIX_ACL)
    /* Hang the xattr handlers off the superblock */
    sb->s_xattr = pvfs2_xattr_handlers;
#endif
    sb->s_magic = PVFS2_SUPER_MAGIC;
    sb->s_op = &pvfs2_s_ops;
#ifdef HAVE_D_SET_D_OP
    sb->s_d_op = &pvfs2_dentry_operations;
#endif
    sb->s_type = &pvfs2_fs_type;

    sb->s_blocksize = pvfs_bufmap_size_query();
    sb->s_blocksize_bits = pvfs_bufmap_shift_query();
    sb->s_maxbytes = MAX_LFS_FILESIZE;

    root_object.khandle = PVFS2_SB(sb)->root_khandle;
    root_object.fs_id  = PVFS2_SB(sb)->fs_id;
    s = kzalloc(HANDLESTRINGSIZE, GFP_KERNEL);
    gossip_debug(GOSSIP_SUPER_DEBUG,
                 "get inode %s, fsid %d\n",
                 k2s(&(root_object.khandle),s),
                 root_object.fs_id);
    kfree(s);
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
                 "%s:Allocated root inode [%p] with mode %x\n",__func__,
                 root, root->i_mode);

    /* allocates and places root dentry in dcache */
#ifdef HAVE_D_ALLOC_ROOT
    root_dentry = d_alloc_root(root);
#else
    root_dentry = d_make_root(root);
#endif
    if (!root_dentry)
    {
        iput(root);
        return -ENOMEM;
    }

    sb->s_export_op = &pvfs2_export_ops;
    sb->s_root = root_dentry;
    return 0;
}
#ifdef HAVE_FSTYPE_MOUNT_ONLY
struct dentry *pvfs2_mount(struct file_system_type *fst,
                           int flags,
                           const char *devname,
                           void *data)
#else
# ifdef HAVE_VFSMOUNT_GETSB
int pvfs2_get_sb(struct file_system_type *fst,
                 int flags,
                 const char *devname,
                 void *data,
                 struct vfsmount *mnt)
# else
struct super_block *pvfs2_get_sb(struct file_system_type *fst,
                                 int flags,
                                 const char *devname,
                                 void *data)
# endif /* HAVE_VFSMOUNT_GETSB */
#endif /* HAVE_FSTYPE_MOUNT_ONLY */
{
    int ret = -EINVAL;
    struct super_block *sb = ERR_PTR(-EINVAL);
    pvfs2_kernel_op_t *new_op;
    pvfs2_mount_sb_info_t mount_sb_info;
#ifdef HAVE_FSTYPE_MOUNT_ONLY 
    struct dentry *mnt_sb_d = ERR_PTR(-EINVAL);
#endif

    gossip_debug(GOSSIP_SUPER_DEBUG,
                 "pvfs2_get_sb: called with devname %s\n", devname);

    if (devname)
    {
        new_op = op_alloc(PVFS2_VFS_OP_FS_MOUNT);
        if (!new_op)
        {
            ret = -ENOMEM;
#if defined(HAVE_VFSMOUNT_GETSB) && !defined(HAVE_FSTYPE_MOUNT_ONLY)
            return ret;
#else
            return ERR_PTR(ret);
#endif
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
            goto error_exit;
        }

        if ((new_op->downcall.resp.fs_mount.fs_id == PVFS_FS_ID_NULL) ||
            (new_op->downcall.resp.fs_mount.root_khandle.slice[0] +
             new_op->downcall.resp.fs_mount.root_khandle.slice[3] == 0))
        {
            gossip_err("ERROR: Retrieved null fs_id or root_handle\n");
            ret = -EINVAL;
            goto error_exit;
        }

        /* fill in temporary structure passed to fill_sb method */
        mount_sb_info.data = data;
        mount_sb_info.root_khandle =
          new_op->downcall.resp.fs_mount.root_khandle;
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
#ifdef HAVE_GETSB_NODEV
# ifdef HAVE_VFSMOUNT_GETSB
        ret = get_sb_nodev(fst,
                           flags,
                           (void *)&mount_sb_info,
                           pvfs2_fill_sb,
                           mnt);
        if (ret)
        {
            goto free_op;
        }
        sb = mnt->mnt_sb;
# else
        sb = get_sb_nodev(fst,
                          flags,
                          (void *)&mount_sb_info,
                          pvfs2_fill_sb);
# endif /* HAVE_VFSMOUNT_GETSB */
#else /* HAVE_GETSB_NODEV */
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
#endif /* HAVE_GETSB_NODEV */

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
#ifdef HAVE_GET_FS_KEY_SUPER_OPERATIONS
            /* Issue an upcall to pre-fetch the fs key so
             * that subsequent calls would be hits */
            pvfs2_sb_get_fs_key(sb, NULL, NULL);
#endif

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
#ifdef HAVE_FSTYPE_MOUNT_ONLY
    return mnt_sb_d;
#else
# ifdef HAVE_VFSMOUNT_GETSB
    return ret;
# else
    return sb;
# endif /* HAVE_VFSMOUNT_GETSB */
#endif /* HAVE_FSTYPE_MOUNT_ONLY */

error_exit:
    if (ret || IS_ERR(sb))
    {
#if !defined(HAVE_FSTYPE_MOUNT_ONLY)
        sb = ERR_PTR(ret);
#endif /* HAVE_FSTYPE_MOUNT_ONLY */
    }
#ifdef HAVE_VFSMOUNT_GETSB
free_op:
#endif
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
#if defined(HAVE_VFSMOUNT_GETSB) && !defined(HAVE_FSTYPE_MOUNT_ONLY)
    gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_get_sb: returning %d\n", ret);
    return ret;
#elif defined(HAVE_FSTYPE_MOUNT_ONLY)
    gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_get_sb: returning dentry %p\n", 
        mnt_sb_d);
    return mnt_sb_d;
#else
    gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_get_sb: returning sb %p\n", sb);
    return sb;
#endif
}

#endif /* PVFS2_LINUX_KERNEL_2_4 */

static void pvfs2_flush_sb(struct super_block *sb)
{
#ifdef HAVE_SB_DIRTY_LIST
    if (!list_empty(&sb->s_dirty))
    {
        struct inode *inode = NULL;
        list_for_each_entry (inode, &sb->s_dirty, i_list)
        {
            pvfs2_flush_inode(inode);
        }
    }
#endif
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
#else
        sb->u.generic_sbp = NULL;
#endif
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
