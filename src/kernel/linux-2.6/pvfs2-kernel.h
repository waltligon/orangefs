/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2KERNEL_H
#define __PVFS2KERNEL_H

#include <linux/config.h>
#include <linux/fs.h>

#include "pint-dev-shared.h"
#include "pvfs2-dev-proto.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("PVFS2 Development Team");
MODULE_DESCRIPTION("The Linux Kernel VFS interface to PVFS2");

#ifdef PVFS2_KERNEL_DEBUG
#define pvfs2_print printk
#else
#define pvfs2_print(...)
#endif
#define pvfs2_error printk

#ifdef PVFS2_KERNEL_DEBUG
#define MAX_SERVICE_WAIT_IN_SECONDS       10
#else
#define MAX_SERVICE_WAIT_IN_SECONDS       30
#endif

#define PVFS2_REQDEVICE_NAME          "pvfs2-req"

#define PVFS2_MAGIC                    0x20030528
#define PVFS2_DEVREQ_MAGIC             0x20030529
#define PVFS2_ROOT_INODE_NUMBER        0x00100000
#define PVFS2_LINK_MAX                 0x000000FF
#define PVFS2_OP_RETRY_COUNT           0x00000005

#define MAX_DEV_REQ_UPSIZE (sizeof(int32_t) +   \
sizeof(int64_t) + sizeof(pvfs2_upcall_t))
#define MAX_DEV_REQ_DOWNSIZE (sizeof(int32_t) + \
sizeof(int64_t) + sizeof(pvfs2_downcall_t))

/* borrowed from irda.h */
#ifndef MSECS_TO_JIFFIES
#define MSECS_TO_JIFFIES(ms) (((ms)*HZ+999)/1000)
#endif

/* translates an inode number to a pvfs2 handle */
#define pvfs2_ino_to_handle(ino) (PVFS_handle)ino

/* translates a pvfs2 handle to an inode number */
#define pvfs2_handle_to_ino(handle) (ino_t)handle

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


/************************************
 * pvfs2 kernel memory related flags
 ************************************/

#if ((defined PVFS2_KERNEL_DEBUG) && (defined CONFIG_DEBUG_SLAB))
/* #define PVFS2_CACHE_CREATE_FLAGS \ */
/*         (SLAB_POISON | SLAB_RED_ZONE | SLAB_RECLAIM_ACCOUNT) */
#define PVFS2_CACHE_CREATE_FLAGS (SLAB_POISON | SLAB_RED_ZONE)
#else
/* #define PVFS2_CACHE_CREATE_FLAGS (SLAB_RECLAIM_ACCOUNT) */
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
    PVFS_pinode_reference refn;
    PVFS_ds_position readdir_token;
    char *link_target;
    struct inode vfs_inode;
    sector_t last_failed_block_index_read;
} pvfs2_inode_t;

/* per superblock private pvfs2 info */
typedef struct
{
    PVFS_handle handle;
    PVFS_fs_id fs_id;
} pvfs2_sb_info;

/*
  NOTE: See Documentation/filesystems/porting for information
  on implementing FOO_I and properly accessing fs private data
*/
static inline pvfs2_inode_t *PVFS2_I(
    struct inode *inode)
{
    return container_of(inode, pvfs2_inode_t, vfs_inode);
}

static inline pvfs2_sb_info *PVFS2_SB(
    struct super_block *sb)
{
    return (pvfs2_sb_info *) sb->s_fs_info;
}

/************************************
 * misc convenience macros
 ************************************/
#ifdef DEVREQ_WAITQ_INTERFACE
#define add_op_to_request_list(op)                            \
do {                                                          \
    spin_lock(&pvfs2_request_list_lock);                      \
    list_add_tail(&op->list, &pvfs2_request_list);            \
    spin_unlock(&pvfs2_request_list_lock);                    \
                                                              \
    spin_lock(&op->lock);                                     \
    op->op_state = PVFS2_VFS_STATE_WAITING;                   \
    spin_unlock(&op->lock);                                   \
    wake_up_interruptible(&pvfs2_request_list_waitq);         \
} while(0)
#else
#define add_op_to_request_list(op)                            \
do {                                                          \
    spin_lock(&pvfs2_request_list_lock);                      \
    list_add_tail(&op->list, &pvfs2_request_list);            \
    spin_unlock(&pvfs2_request_list_lock);                    \
                                                              \
    spin_lock(&op->lock);                                     \
    op->op_state = PVFS2_VFS_STATE_WAITING;                   \
    spin_unlock(&op->lock);                                   \
} while(0)
#endif /* DEVREQ_WAITQ_INTERFACE */

#define remove_op_from_request_list(op)                       \
do {                                                          \
    struct list_head *tmp = NULL;                             \
    pvfs2_kernel_op_t *tmp_op = NULL;                         \
                                                              \
    spin_lock(&pvfs2_request_list_lock);                      \
    list_for_each(tmp, &pvfs2_request_list) {                 \
        tmp_op = list_entry(tmp, pvfs2_kernel_op_t, list);    \
        if (tmp_op && (tmp_op == op)) {                       \
            list_del(&tmp_op->list);                          \
            break;                                            \
        }                                                     \
    }                                                         \
    spin_unlock(&pvfs2_request_list_lock);                    \
} while(0)

#define remove_op_from_htable_ops_in_progress(op)             \
do {                                                          \
    qhash_search_and_remove(htable_ops_in_progress,           \
                            &(op->tag));                      \
} while(0)

#define service_operation(op, method)                         \
add_op_to_request_list(op);                                   \
if ((ret = wait_for_matching_downcall(new_op)) != 0)          \
{                                                             \
    pvfs2_error("pvfs2: %s -- wait failed (%x).\n",           \
                method,ret);                                  \
    goto error_exit;                                          \
}

/*
  tries to service the operation and will retry on timeout
  failure up to num times (num MUST be a numeric lvalue).
*/
#define service_operation_with_timeout_retry(op, method, num) \
wait_for_op:                                                  \
 add_op_to_request_list(op);                                  \
 if ((ret = wait_for_matching_downcall(op)) != 0)             \
 {                                                            \
     if ((ret == 1) && (--num))                               \
     {                                                        \
         pvfs2_print("pvfs2: %s -- timeout; requeing op\n",   \
                     method);                                 \
         goto wait_for_op;                                    \
     }                                                        \
     else                                                     \
     {                                                        \
         pvfs2_error("pvfs2: %s -- wait failed (%x).\n",      \
                     method,ret);                             \
         goto error_exit;                                     \
     }                                                        \
 }

/****************************
 * defined in pvfs2-cache.c
 ****************************/
void op_cache_initialize(
    void);
void op_cache_finalize(
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

/****************************
 * defined in inode.c
 ****************************/
int pvfs2_setattr(
    struct dentry *dentry,
    struct iattr *iattr);

/****************************
 * defined in namei.c
 ****************************/
struct dentry *pvfs2_lookup(
    struct inode *dir,
    struct dentry *dentry,
    struct nameidata *nd);

int pvfs2_empty_dir(
    struct dentry *dentry);

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
    int op_type);

int pvfs2_remove_entry(
    struct inode *dir,
    struct dentry *dentry);

int pvfs2_truncate_inode(
    struct inode *inode,
    loff_t size);

#endif /* __PVFS2KERNEL_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
