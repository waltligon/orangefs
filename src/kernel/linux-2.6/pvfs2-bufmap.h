/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_BUFMAP_H
#define __PVFS2_BUFMAP_H

#include "pint-dev-shared.h"

/* used to describe mapped buffers */
struct pvfs_bufmap_desc
{
    void *uaddr;                /* user space address pointer */
    struct page **page_array;   /* array of mapped pages */
    int array_count;            /* size of above arrays */
    struct list_head list_link;
};

/* this would be a function call if the buffer sizes weren't hard coded */
#define pvfs_bufmap_size_query() PVFS2_BUFMAP_DEFAULT_DESC_SIZE

int pvfs_bufmap_initialize(
    struct PVFS_dev_map_desc *user_desc);

void pvfs_bufmap_finalize(void);

int pvfs_bufmap_get(
    int *buffer_index);

void pvfs_bufmap_put(
    int buffer_index);

int pvfs_bufmap_copy_from_user(
    int buffer_index,
    void __user *from,
    int size);

int pvfs_bufmap_copy_iovec_from_user(
    int buffer_index,
    const struct iovec *iov,
    unsigned long nr_segs,
    int size);

int pvfs_bufmap_copy_to_user(
    void __user *to,
    int buffer_index,
    int size);

int pvfs_bufmap_copy_to_user_iovec(
    int buffer_index,
    const struct iovec *iov,
    unsigned long nr_segs,
    int size);

int pvfs_bufmap_copy_to_kernel(
    void *to,
    int buffer_index,
    int size);

#ifndef PVFS2_LINUX_KERNEL_2_4
int pvfs_bufmap_copy_to_user_task(
        struct task_struct *tsk,
        void __user *to,
        int buffer_index, 
        int size);
#endif

#endif /* __PVFS2_BUFMAP_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
