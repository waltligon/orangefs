#ifndef __PVFS2KERNEL_H
#define __PVFS2KERNEL_H

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

#define PVFS2_REQDEVICE_NAME          "pvfs2-req"
#define PVFS2_FLOWDEVICE_NAME        "pvfs2-flow"

#define PVFS2_MAGIC                    0x20030528
#define PVFS2_DEVREQ_MAGIC             0x20030529
#define PVFS2_DEVFLOW_MAGIC            0x2003052A
#define PVFS2_ROOT_INODE_NUMBER        0x00100000
#define PVFS2_LINK_MAX                 0x000000FF

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
 * invalid  - op was cancelled and is now on the invalidated htable
 * dead     - op should be freed upon return
 ************************************/
#define PVFS2_VFS_STATE_UNKNOWN        0x00FF0000
#define PVFS2_VFS_STATE_WAITING        0x00FF0001
#define PVFS2_VFS_STATE_INPROGR        0x00FF0002
#define PVFS2_VFS_STATE_SERVICED       0x00FF0003
#define PVFS2_VFS_STATE_INVALID        0x00FF0004
#define PVFS2_VFS_STATE_DEAD           0x00FF0005


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

    struct list_head list;
} pvfs2_kernel_op_t;

/* per inode private pvfs2 info */
typedef struct
{
    PVFS_pinode_reference refn;
    PVFS_ds_position readdir_token; /* REMOVE ME; unused */
    struct inode vfs_inode;
} pvfs2_inode_t; /* RENAME THIS TO pvfs2_inode_info */

/* per superblock private pvfs2 info */
typedef struct
{
    PVFS_fs_id fs_id;
} pvfs2_sb_info;

/*
  NOTE: See Documentation/filesystems/porting for information
  on implementing FOO_I and properly accessing fs private data
*/
static inline pvfs2_inode_t *PVFS2_I(struct inode *inode)
{
    return container_of(inode, pvfs2_inode_t, vfs_inode);
}

static inline pvfs2_sb_info *PVFS2_SB(struct super_block *sb)
{
    return (pvfs2_sb_info *)sb->s_fs_info;
}

/************************************
 * misc convenience macros
 ************************************/
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

#define remove_op_from_request_list(op)                       \
do {                                                          \
    struct list_head *tmp = NULL;                             \
    pvfs2_kernel_op_t *tmp_op = NULL;                         \
                                                              \
    spin_lock(&pvfs2_request_list_lock);                      \
    list_for_each(tmp, &pvfs2_request_list) {                 \
        tmp_op = list_entry(tmp, pvfs2_kernel_op_t, list);    \
        if (tmp_op && (tmp_op == op)) {                       \
            printk("Removing op with tag %lu\n",tmp_op->tag); \
            list_del(&tmp_op->list);                          \
            break;                                            \
        }                                                     \
    }                                                         \
    spin_unlock(&pvfs2_request_list_lock);                    \
} while(0)

#define invalidate_op(op, get_lock)                           \
do {                                                          \
    if (get_lock) spin_lock(&op->lock);                       \
    op->op_state = PVFS2_VFS_STATE_INVALID;                   \
    if (get_lock) spin_unlock(&op->lock);                     \
    qhash_add(htable_ops_invalidated,                         \
              (void *)&(op->tag),&op->list);                  \
    printk("invalidate_op: adding op %p (NOW INVALID)\n",op); \
} while(0)

#define remove_op_from_htable_ops_in_progress(op)             \
do {                                                          \
    qhash_search_and_remove(htable_ops_in_progress,           \
                            &(op->tag));                      \
} while(0)

#define remove_op_from_htable_ops_invalidated(op)             \
do {                                                          \
    qhash_search_and_remove(htable_ops_invalidated,           \
                            &(op->tag));                      \
} while(0)


/****************************
 * defined in pvfs2-cache.c
 ****************************/
void op_cache_initialize(void);
void op_cache_finalize(void);
void op_release(
    void *op);
void dev_req_cache_initialize(void);
void dev_req_cache_finalize(void);
void pvfs2_inode_cache_initialize(void);
void pvfs2_inode_cache_finalize(void);

/****************************
 * defined in waitqueue.c
 ****************************/
int wait_for_matching_downcall(
    pvfs2_kernel_op_t *op);

/****************************
 * defined in pvfs2-utils.c
 ****************************/
int pvfs2_inode_getattr(
    struct inode *inode);

struct inode *pvfs2_create_entry(
    struct inode *dir,
    struct dentry *dentry,
    int mode,
    int op_type);

int pvfs2_remove_entry(
    struct inode *dir,
    struct dentry *dentry);


#endif /* __PVFS2KERNEL_H */
