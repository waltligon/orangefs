/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2KERNEL_H
#define __PVFS2KERNEL_H

#include <linux/config.h>

#ifdef PVFS2_LINUX_KERNEL_2_4

#define __NO_VERSION__
#include <linux/version.h>
#ifdef CONFIG_MODVERSIONS
#include <linux/modversions.h>
#endif

#ifndef HAVE_SECTOR_T
typedef unsigned long sector_t;
#endif

#else /* !(PVFS2_LINUX_KERNEL_2_4) */

#include <linux/moduleparam.h>
#include <linux/vermagic.h>
#include <linux/statfs.h>
#include <linux/buffer_head.h>
#include <linux/backing-dev.h>
#include <linux/mpage.h>
#include <linux/namei.h>

#endif /* PVFS2_LINUX_KERNEL_2_4 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/uio.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/atomic.h>
#include <linux/smp_lock.h>
#include <linux/wait.h>
#include <linux/dcache.h>
#include <linux/pagemap.h>

/* taken from include/linux/fs.h from 2.4.19 or later kernels */
#ifndef MAX_LFS_FILESIZE
#if BITS_PER_LONG == 32
#define MAX_LFS_FILESIZE     (((u64)PAGE_CACHE_SIZE << (BITS_PER_LONG))-1)
#elif BITS_PER_LONG == 64
#define MAX_LFS_FILESIZE     0x7fffffffffffffff
#endif
#endif /* MAX_LFS_FILESIZE */

#include "pint-dev-shared.h"
#include "pvfs2-dev-proto.h"

#ifndef PVFS2_LINUX_KERNEL_2_4
MODULE_LICENSE("GPL");
MODULE_AUTHOR("PVFS2 Development Team");
MODULE_DESCRIPTION("The Linux Kernel VFS interface to PVFS2");
#endif

#define pvfs2_error printk

#ifdef PVFS2_KERNEL_DEBUG
#define pvfs2_print printk
#define pvfs2_panic(msg)                                       \
do {                                                           \
    pvfs2_error("BUG! Please contact pvfs2-developers@beowulf-"\
                "underground.org\n");                          \
    panic(msg);                                                \
} while(0)
#else
#define pvfs2_print(...)
#define pvfs2_panic(msg)                                       \
do {                                                           \
    pvfs2_error("BUG! Please contact pvfs2-developers@beowulf-"\
                "underground.org\n");                          \
    pvfs2_error(msg);                                          \
} while(0)
#endif

#ifdef PVFS2_KERNEL_DEBUG
#define MAX_SERVICE_WAIT_IN_SECONDS       10
#else
#define MAX_SERVICE_WAIT_IN_SECONDS       30
#endif

#define PVFS2_REQDEVICE_NAME          "pvfs2-req"

#define PVFS2_MAGIC                    0x20030528
#define PVFS2_DEVREQ_MAGIC             0x20030529
#define PVFS2_LINK_MAX                 0x000000FF
#define PVFS2_OP_RETRY_COUNT           0x00000005
#define PVFS2_SEEK_END                 0x00000002
#define PVFS2_MAX_NUM_OPTIONS          0x00000004
#define PVFS2_MAX_MOUNT_OPT_LEN        0x00000080

#define MAX_DEV_REQ_UPSIZE (sizeof(int32_t) +   \
sizeof(int64_t) + sizeof(pvfs2_upcall_t))
#define MAX_DEV_REQ_DOWNSIZE (sizeof(int32_t) + \
sizeof(int64_t) + sizeof(pvfs2_downcall_t))

#define BITS_PER_LONG_DIV_8 (BITS_PER_LONG >> 3)

#define MAX_ALIGNED_DEV_REQ_UPSIZE                  \
(MAX_DEV_REQ_UPSIZE +                               \
((((MAX_DEV_REQ_UPSIZE / (BITS_PER_LONG_DIV_8)) *   \
   (BITS_PER_LONG_DIV_8)) +                         \
    (BITS_PER_LONG_DIV_8)) - MAX_DEV_REQ_UPSIZE))
#define MAX_ALIGNED_DEV_REQ_DOWNSIZE                \
(MAX_DEV_REQ_DOWNSIZE +                             \
((((MAX_DEV_REQ_DOWNSIZE / (BITS_PER_LONG_DIV_8)) * \
   (BITS_PER_LONG_DIV_8)) +                         \
    (BITS_PER_LONG_DIV_8)) - MAX_DEV_REQ_DOWNSIZE))


/* borrowed from irda.h */
#ifndef MSECS_TO_JIFFIES
#define MSECS_TO_JIFFIES(ms) (((ms)*HZ+999)/1000)
#endif

/* translates an inode number to a pvfs2 handle */
#define pvfs2_ino_to_handle(ino) (PVFS_handle)ino

/* translates a pvfs2 handle to an inode number */
#define pvfs2_handle_to_ino(handle) (ino_t)pvfs2_handle_l32(handle)

#define pvfs2_handle_l32(handle) (__u32)(handle)
#define pvfs2_handle_h32(handle) (__u32)(handle >> 32)

/************************************
 * valid pvfs2 kernel operation states
 *
 * unknown  - op was just initialized
 * waiting  - op is on request_list (upward bound)
 * inprogr  - op is in progress (waiting for downcall)
 * serviced - op has matching downcall; ok
 ************************************/
#define PVFS2_VFS_STATE_UNKNOWN        0x00FF0000
#define PVFS2_VFS_STATE_WAITING        0x00FF0001
#define PVFS2_VFS_STATE_INPROGR        0x00FF0002
#define PVFS2_VFS_STATE_SERVICED       0x00FF0003

/* defines used for wait_for_matching_downcall return values */
#define PVFS2_WAIT_ERROR               0xFFFFFFFF
#define PVFS2_WAIT_SUCCESS             0x00000000
#define PVFS2_WAIT_TIMEOUT_REACHED     0x00000001
#define PVFS2_WAIT_SIGNAL_RECVD        0x00000002

/************************************
 * pvfs2 kernel memory related flags
 ************************************/

#if ((defined PVFS2_KERNEL_DEBUG) && (defined CONFIG_DEBUG_SLAB))
#define PVFS2_CACHE_CREATE_FLAGS (SLAB_POISON | SLAB_RED_ZONE)
#else
#define PVFS2_CACHE_CREATE_FLAGS 0
#endif /* ((defined PVFS2_KERNEL_DEBUG) && (defined CONFIG_DEBUG_SLAB)) */

#define PVFS2_CACHE_ALLOC_FLAGS (SLAB_KERNEL)
#define PVFS2_GFP_FLAGS (GFP_KERNEL)

#ifdef CONFIG_HIGHMEM
#define PVFS2_BUFMAP_GFP_FLAGS (GFP_ATOMIC)
#else
#define PVFS2_BUFMAP_GFP_FLAGS (GFP_KERNEL)
#endif /* CONFIG_HIGHMEM */


/************************************
 * pvfs2 data structures
 ************************************/
typedef struct
{
    int op_state;
    unsigned long tag;

    pvfs2_upcall_t upcall;
    pvfs2_downcall_t downcall;

    wait_queue_head_t waitq;
    spinlock_t lock;

    int io_completed;
    wait_queue_head_t io_completion_waitq;

    struct list_head list;
} pvfs2_kernel_op_t;

/* per inode private pvfs2 info */
typedef struct
{
    PVFS_object_ref refn;
    PVFS_ds_position readdir_token_adjustment;
    char *link_target;
#ifdef PVFS2_LINUX_KERNEL_2_4
    struct inode *vfs_inode;
#else
    struct inode vfs_inode;
#endif
    sector_t last_failed_block_index_read;
} pvfs2_inode_t;

/*
  these are the available mount options that we accept:

  the intr option (if set) is inspired by the nfs intr option that
  interrupts the operation in progress if a signal is received, and
  ignores the signal otherwise (if not set).
*/
typedef struct
{
    int intr;
} pvfs2_mount_options_t;

/* per superblock private pvfs2 info */
typedef struct
{
    PVFS_handle root_handle;
    PVFS_fs_id fs_id;
    int id;
    pvfs2_mount_options_t mnt_options;
    char data[PVFS2_MAX_MOUNT_OPT_LEN];
    char devname[PVFS_MAX_SERVER_ADDR_LEN];
    struct super_block *sb;

    struct list_head list;
} pvfs2_sb_info_t;

/*
  a temporary structure used only for sb mount time that groups the
  mount time data provided along with a private superblock structure
  that is allocated before a 'kernel' superblock is allocated.
*/
typedef struct
{
    void *data;
    PVFS_handle root_handle;
    PVFS_fs_id fs_id;
    int id;
} pvfs2_mount_sb_info_t;

/*
  NOTE: See Documentation/filesystems/porting for information
  on implementing FOO_I and properly accessing fs private data
*/
static inline pvfs2_inode_t *PVFS2_I(
    struct inode *inode)
{
#ifdef PVFS2_LINUX_KERNEL_2_4
    return (pvfs2_inode_t *)inode->u.generic_ip;
#else
    return container_of(inode, pvfs2_inode_t, vfs_inode);
#endif
}

static inline pvfs2_sb_info_t *PVFS2_SB(
    struct super_block *sb)
{
#ifdef PVFS2_LINUX_KERNEL_2_4
    return (pvfs2_sb_info_t *)sb->u.generic_sbp;
#else
    return (pvfs2_sb_info_t *)sb->s_fs_info;
#endif
}

/****************************
 * defined in pvfs2-cache.c
 ****************************/
void op_cache_initialize(
    void);
void op_cache_finalize(
    void);
pvfs2_kernel_op_t *op_alloc(
    void);
void op_release(
    void *op);
void dev_req_cache_initialize(
    void);
void dev_req_cache_finalize(
    void);
void pvfs2_inode_cache_initialize(
    void);
void pvfs2_inode_cache_finalize(
    void);

/****************************
 * defined in waitqueue.c
 ****************************/
int wait_for_matching_downcall(
    pvfs2_kernel_op_t * op);
int wait_for_cancellation_downcall(
    pvfs2_kernel_op_t * op);

/****************************
 * defined in super.c
 ****************************/
#ifdef PVFS2_LINUX_KERNEL_2_4
struct super_block* pvfs2_get_sb(
    struct super_block *sb,
    void *data,
    int silent);
#else
struct super_block *pvfs2_get_sb(
    struct file_system_type *fst, int flags,
    const char *devname, void *data);
#endif

int pvfs2_remount(
    struct super_block *sb,
    int *flags,
    char *data);

/****************************
 * defined in inode.c
 ****************************/
struct inode *pvfs2_get_custom_inode(
    struct super_block *sb,
    int mode,
    dev_t dev,
    unsigned long ino);

int pvfs2_setattr(
    struct dentry *dentry,
    struct iattr *iattr);

#ifdef PVFS2_LINUX_KERNEL_2_4
int pvfs2_revalidate(
    struct dentry *dentry);
#else
int pvfs2_getattr(
    struct vfsmount *mnt,
    struct dentry *dentry,
    struct kstat *kstat);
#endif

/****************************
 * defined in namei.c
 ****************************/
#ifdef PVFS2_LINUX_KERNEL_2_4
struct dentry *pvfs2_lookup(
    struct inode *dir,
    struct dentry *dentry);
#else
struct dentry *pvfs2_lookup(
    struct inode *dir,
    struct dentry *dentry,
    struct nameidata *nd);
#endif

/****************************
 * defined in pvfs2-utils.c
 ****************************/
int pvfs2_gen_credentials(
    PVFS_credentials *credentials);

int pvfs2_inode_getattr(
    struct inode *inode);

int pvfs2_inode_setattr(
    struct inode *inode,
    struct iattr *iattr);

struct inode *pvfs2_create_entry(
    struct inode *dir,
    struct dentry *dentry,
    const char *symname,
    int mode,
    int op_type,
    int *error_code);

int pvfs2_remove_entry(
    struct inode *dir,
    struct dentry *dentry);

int pvfs2_truncate_inode(
    struct inode *inode,
    loff_t size);

int pvfs2_kernel_error_code_convert(
    int pvfs2_error_code);

void pvfs2_inode_initialize(
    pvfs2_inode_t *pvfs2_inode);

void pvfs2_inode_finalize(
    pvfs2_inode_t *pvfs2_inode);

void pvfs2_op_initialize(
    pvfs2_kernel_op_t *op);

void pvfs2_make_bad_inode(
    struct inode *inode);

void mask_blocked_signals(
    sigset_t *orig_sigset);

void unmask_blocked_signals(
    sigset_t *orig_sigset);

#ifdef USE_MMAP_RA_CACHE
int pvfs2_flush_mmap_racache(
    struct inode *inode);
#endif

int pvfs2_unmount_sb(
    struct super_block *sb);

int pvfs2_cancel_op_in_progress(
    unsigned long tag);

/************************************
 * misc convenience macros
 ************************************/
extern struct semaphore request_semaphore;

#define add_op_to_request_list(op)                           \
do {                                                         \
    spin_lock(&op->lock);                                    \
    op->op_state = PVFS2_VFS_STATE_WAITING;                  \
                                                             \
    spin_lock(&pvfs2_request_list_lock);                     \
    list_add_tail(&op->list, &pvfs2_request_list);           \
    spin_unlock(&pvfs2_request_list_lock);                   \
                                                             \
    spin_unlock(&op->lock);                                  \
    wake_up_interruptible(&pvfs2_request_list_waitq);        \
} while(0)

#define add_priority_op_to_request_list(op)                  \
do {                                                         \
    spin_lock(&op->lock);                                    \
    op->op_state = PVFS2_VFS_STATE_WAITING;                  \
                                                             \
    spin_lock(&pvfs2_request_list_lock);                     \
    list_add(&op->list, &pvfs2_request_list);                \
    spin_unlock(&pvfs2_request_list_lock);                   \
                                                             \
    spin_unlock(&op->lock);                                  \
    wake_up_interruptible(&pvfs2_request_list_waitq);        \
} while(0)

#define remove_op_from_request_list(op)                      \
do {                                                         \
    struct list_head *tmp = NULL;                            \
    pvfs2_kernel_op_t *tmp_op = NULL;                        \
                                                             \
    spin_lock(&pvfs2_request_list_lock);                     \
    list_for_each(tmp, &pvfs2_request_list) {                \
        tmp_op = list_entry(tmp, pvfs2_kernel_op_t, list);   \
        if (tmp_op && (tmp_op == op)) {                      \
            list_del(&tmp_op->list);                         \
            break;                                           \
        }                                                    \
    }                                                        \
    spin_unlock(&pvfs2_request_list_lock);                   \
} while(0)

#define remove_op_from_htable_ops_in_progress(op)            \
do {                                                         \
    qhash_search_and_remove(htable_ops_in_progress,          \
                            &(op->tag));                     \
} while(0)

#define service_operation(op, method, intr)                  \
do {                                                         \
    sigset_t orig_sigset;                                    \
    if (!intr) mask_blocked_signals(&orig_sigset);           \
    down_interruptible(&request_semaphore);                  \
    add_op_to_request_list(op);                              \
    up(&request_semaphore);                                  \
    ret = wait_for_matching_downcall(op);                    \
    if (!intr) unmask_blocked_signals(&orig_sigset);         \
    if (ret != PVFS2_WAIT_SUCCESS)                           \
    {                                                        \
        if (ret == PVFS2_WAIT_TIMEOUT_REACHED)               \
        {                                                    \
            pvfs2_error("pvfs2: %s -- wait timed out (%x).  "\
                        "aborting attempt.\n", method,ret);  \
        }                                                    \
        goto error_exit;                                     \
    }                                                        \
} while(0)

#define service_cancellation_operation(op)                   \
do {                                                         \
    down_interruptible(&request_semaphore);                  \
    add_op_to_request_list(op);                              \
    up(&request_semaphore);                                  \
    ret = wait_for_cancellation_downcall(op);                \
    if (ret != PVFS2_WAIT_SUCCESS)                           \
    {                                                        \
        if (ret == PVFS2_WAIT_TIMEOUT_REACHED)               \
        {                                                    \
            pvfs2_error("pvfs2_op_cancel: wait timed out  "  \
                        "(%x). aborting attempt.\n", ret);   \
        }                                                    \
        goto error_exit;                                     \
    }                                                        \
} while(0)

#define service_priority_operation(op, method, intr)         \
do {                                                         \
    sigset_t orig_sigset;                                    \
    if (!intr) mask_blocked_signals(&orig_sigset);           \
    add_priority_op_to_request_list(op);                     \
    ret = wait_for_matching_downcall(op);                    \
    if (!intr) unmask_blocked_signals(&orig_sigset);         \
    if (ret != PVFS2_WAIT_SUCCESS)                           \
    {                                                        \
        if (ret == PVFS2_WAIT_TIMEOUT_REACHED)               \
        {                                                    \
            pvfs2_error("pvfs2: %s -- wait timed out (%x).  "\
                        "aborting attempt.\n", method,ret);  \
        }                                                    \
        goto error_exit;                                     \
    }                                                        \
} while(0)

/*
  tries to service the operation and will retry on timeout
  failure up to num times (num MUST be a numeric lvalue).
*/
#define service_operation_with_timeout_retry(op, method, num, intr)\
do {                                                               \
    sigset_t orig_sigset;                                          \
    if (!intr) mask_blocked_signals(&orig_sigset);                 \
  wait_for_op:                                                     \
    down_interruptible(&request_semaphore);                        \
    add_op_to_request_list(op);                                    \
    up(&request_semaphore);                                        \
    ret = wait_for_matching_downcall(op);                          \
    if (!intr) unmask_blocked_signals(&orig_sigset);               \
    if (ret != PVFS2_WAIT_SUCCESS)                                 \
    {                                                              \
        if ((ret == PVFS2_WAIT_TIMEOUT_REACHED) && (--num))        \
        {                                                          \
            pvfs2_print("pvfs2: %s -- timeout; requeing op\n",     \
                        method);                                   \
            goto wait_for_op;                                      \
        }                                                          \
        else                                                       \
        {                                                          \
            if (ret == PVFS2_WAIT_TIMEOUT_REACHED)                 \
            {                                                      \
                pvfs2_error("pvfs2: %s -- wait timed out (%x).  "  \
                            "aborting retry attempts.\n",          \
                            method,ret);                           \
            }                                                      \
            goto error_exit;                                       \
         }                                                         \
     }                                                             \
} while(0)

/*
  tries to service the operation and will retry on timeout
  failure up to num times (num MUST be a numeric lvalue).

  this allows us to know if we've reached the error_exit code path
  from here or elsewhere

  NOTE: used in namei.c:lookup, file.c:pvfs2_inode_read, and
  file.c:pvfs2_file_write
*/
#define service_error_exit_op_with_timeout_retry(op,meth,num,e,intr)\
do {                                                                \
    sigset_t orig_sigset;                                           \
    if (!intr) mask_blocked_signals(&orig_sigset);                  \
  wait_for_op:                                                      \
    down_interruptible(&request_semaphore);                         \
    add_op_to_request_list(op);                                     \
    up(&request_semaphore);                                         \
    ret = wait_for_matching_downcall(op);                           \
    if (!intr) unmask_blocked_signals(&orig_sigset);                \
    if (ret != PVFS2_WAIT_SUCCESS)                                  \
    {                                                               \
        if ((ret == PVFS2_WAIT_TIMEOUT_REACHED) && (--num))         \
        {                                                           \
            pvfs2_print("pvfs2: %s -- timeout; requeing op\n",      \
                        meth);                                      \
            goto wait_for_op;                                       \
        }                                                           \
        else                                                        \
        {                                                           \
            if (ret == PVFS2_WAIT_TIMEOUT_REACHED)                  \
            {                                                       \
                pvfs2_error("pvfs2: %s -- wait timed out (%x).  "   \
                            "aborting retry attempts.\n",           \
                            meth,ret);                              \
            }                                                       \
            e = 1;                                                  \
            goto error_exit;                                        \
        }                                                           \
    }                                                               \
} while(0)

/*
  by design, our vfs i/o errors need to be handled in one of two ways,
  depending on where the error occured.

  if the error happens in the waitqueue code because we either timed
  out or a signal was raised while waiting, we need to cancel the
  userspace i/o operation and free the op manually.  this is done to
  avoid having the device start writing application data to our shared
  bufmap pages without us expecting it.

  if a pvfs2 sysint level error occured and i/o has been completed,
  there is no need to cancel the operation, as the user has finished
  using the bufmap page and so there is no danger in this case.  in
  this case, we wake up the device normally so that it may free the
  op, as normal.

  this macro handles both of these cases, depending on which error
  happened based on information known in context.  the only reason
  this is a macro is because both read and write cases need the exact
  same handling code.
*/
#define handle_io_error()                                 \
do {                                                      \
    if (error_exit)                                       \
    {                                                     \
        ret = pvfs2_cancel_op_in_progress(new_op->tag);   \
        op_release(new_op);                               \
    }                                                     \
    else                                                  \
    {                                                     \
        ret = pvfs2_kernel_error_code_convert(            \
                 new_op->downcall.status);                \
        *offset = original_offset;                        \
        wake_up_device_for_return(new_op);                \
    }                                                     \
    *offset = original_offset;                            \
} while(0)

#define get_interruptible_flag(inode)                     \
(PVFS2_SB(inode->i_sb)->mnt_options.intr)

#ifdef USE_MMAP_RA_CACHE
#define clear_inode_mmap_ra_cache(inode)                  \
do {                                                      \
  pvfs2_print("calling clear_inode_mmap_ra_cache on %d\n",\
              (int)inode->i_ino);                         \
  pvfs2_flush_mmap_racache(inode);                        \
  pvfs2_print("clear_inode_mmap_ra_cache finished\n");    \
} while(0)
#else
#define clear_inode_mmap_ra_cache(inode)
#endif /* USE_MMAP_RA_CACHE */

#define add_pvfs2_sb(sb)                                             \
do {                                                                 \
    pvfs2_print("Adding SB %p to pvfs2 superblocks\n", PVFS2_SB(sb));\
    spin_lock(&pvfs2_superblocks_lock);                              \
    list_add_tail(&PVFS2_SB(sb)->list, &pvfs2_superblocks);          \
    spin_unlock(&pvfs2_superblocks_lock);                            \
} while(0)

#define remove_pvfs2_sb(sb)                                          \
do {                                                                 \
    struct list_head *tmp = NULL;                                    \
    pvfs2_sb_info_t *pvfs2_sb = NULL;                                \
                                                                     \
    spin_lock(&pvfs2_superblocks_lock);                              \
    list_for_each(tmp, &pvfs2_superblocks) {                         \
        pvfs2_sb = list_entry(tmp, pvfs2_sb_info_t, list);           \
        if (pvfs2_sb && (pvfs2_sb->sb == sb)) {                      \
            pvfs2_print("Removing SB %p from pvfs2 superblocks\n",   \
                        pvfs2_sb);                                   \
            list_del(&pvfs2_sb->list);                               \
            break;                                                   \
        }                                                            \
    }                                                                \
    spin_unlock(&pvfs2_superblocks_lock);                            \
} while(0)

#ifdef PVFS2_LINUX_KERNEL_2_4
#define get_block_block_type long
#define pvfs2_lock_inode(inode) do {} while(0)
#define pvfs2_unlock_inode(inode) do {} while(0)
#define pvfs2_current_signal_lock current->sigmask_lock
#define pvfs2_current_sigaction current->sig->action
#define pvfs2_recalc_sigpending() recalc_sigpending(current)
#define pvfs2_d_splice_alias(dentry, inode) d_add(dentry, inode)
#define pvfs2_kernel_readpage block_read_full_page

#define fill_default_sys_attrs(sys_attr,type,mode)\
do                                                \
{                                                 \
    sys_attr.owner = current->fsuid;              \
    sys_attr.group = current->fsgid;              \
    sys_attr.atime = (PVFS_time)CURRENT_TIME;     \
    sys_attr.mtime = (PVFS_time)CURRENT_TIME;     \
    sys_attr.ctime = (PVFS_time)CURRENT_TIME;     \
    sys_attr.size = 0;                            \
    sys_attr.perms = pvfs2_translate_mode(mode);  \
    sys_attr.objtype = type;                      \
    sys_attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;    \
} while(0)

#else /* !(PVFS2_LINUX_KERNEL_2_4) */

#define get_block_block_type sector_t
#define pvfs2_lock_inode(inode) spin_lock(&inode->i_lock)
#define pvfs2_unlock_inode(inode) spin_unlock(&inode->i_lock)
#define pvfs2_current_signal_lock current->sighand->siglock
#define pvfs2_current_sigaction current->sighand->action
#define pvfs2_recalc_sigpending recalc_sigpending
#define pvfs2_d_splice_alias(dentry, inode) d_splice_alias(inode, dentry)
#define pvfs2_kernel_readpage mpage_readpage

#define fill_default_sys_attrs(sys_attr,type,mode)\
do                                                \
{                                                 \
    struct timespec tspec = CURRENT_TIME;         \
    sys_attr.owner = current->fsuid;              \
    sys_attr.group = current->fsgid;              \
    sys_attr.atime = (PVFS_time)tspec.tv_sec;     \
    sys_attr.mtime = (PVFS_time)tspec.tv_sec;     \
    sys_attr.ctime = (PVFS_time)tspec.tv_sec;     \
    sys_attr.size = 0;                            \
    sys_attr.perms = pvfs2_translate_mode(mode);  \
    sys_attr.objtype = type;                      \
    sys_attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;    \
} while(0)

#endif /* PVFS2_LINUX_KERNEL_2_4 */


/************************************
 * misc convenience functions
 ************************************/
static inline int pvfs2_translate_mode(int mode)
{
    int ret = 0, i = 0;
    static int modes[9] =
    {
        S_IXOTH, S_IWOTH, S_IROTH,
        S_IXGRP, S_IWGRP, S_IRGRP,
        S_IXUSR, S_IWUSR, S_IRUSR
    };
    static int pvfs2_modes[9] =
    {
        PVFS_O_EXECUTE, PVFS_O_WRITE, PVFS_O_READ,
        PVFS_G_EXECUTE, PVFS_G_WRITE, PVFS_G_READ,
        PVFS_U_EXECUTE, PVFS_U_WRITE, PVFS_U_READ,
    };

    for(i = 0; i < 9; i++)
    {
        if (mode & modes[i])
        {
            ret |= pvfs2_modes[i];
        }
        else
        {
            ret &= ~pvfs2_modes[i];
        }
    }
    return ret;
}

static inline int pvfs2_internal_revalidate(
    struct inode *inode)
{
    int ret = 0;
    if (inode)
    {
        ret = ((pvfs2_inode_getattr(inode) == 0) ? 1 : 0);
        if (ret == 0)
        {
            pvfs2_make_bad_inode(inode);
        }
    }
    return ret;
}

#ifdef PVFS2_LINUX_KERNEL_2_4
/*
  based on code from 2.6.x's fs/libfs.c with required macro support
  from include/linux/list.h
*/
static inline int simple_positive(struct dentry *dentry)
{
    return dentry->d_inode && !d_unhashed(dentry);
}

#define list_for_each_entry(pos, head, member)             \
for (pos = list_entry((head)->next, typeof(*pos), member), \
  prefetch(pos->member.next);                              \
  &pos->member != (head);                                  \
  pos = list_entry(pos->member.next, typeof(*pos), member),\
  prefetch(pos->member.next))

static inline int simple_empty(struct dentry *dentry)
{
    struct dentry *child;
    int ret = 0;
    spin_lock(&dcache_lock);
    list_for_each_entry(child, &dentry->d_subdirs, d_child)
        if (simple_positive(child))
            goto out;
    ret = 1;
out:
    spin_unlock(&dcache_lock);
    return ret;
}

#if (PVFS2_LINUX_KERNEL_2_4_MINOR_VER < 19)
static inline int dcache_dir_open(struct inode *inode, struct file *file)
{
    static struct qstr cursor_name = {.len = 1, .name = "."};

    file->private_data = d_alloc(file->f_dentry, &cursor_name);

    return file->private_data ? 0 : -ENOMEM;
}

static inline int dcache_dir_close(struct inode *inode, struct file *file)
{
    dput(file->private_data);
    return 0;
}
#endif /* PVFS2_LINUX_KERNEL_2_4_MINOR_VER */

/* based on linux kernel 2.6.x fs.h routines not included in 2.4.x */
static inline void i_size_write(struct inode *inode, loff_t i_size)
{
    inode->i_size = i_size;
}

static inline loff_t i_size_read(struct inode *inode)
{
    return inode->i_size;
}

static inline ino_t parent_ino(struct dentry *dentry)
{
    return dentry->d_parent->d_inode->i_ino;
}
#endif /* PVFS2_LINUX_KERNEL_2_4 */


#endif /* __PVFS2KERNEL_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
