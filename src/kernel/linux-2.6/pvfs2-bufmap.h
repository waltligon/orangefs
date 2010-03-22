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

/* pvfs_bufmap_size_query is now an inline function because buffer
   sizes are not hardcoded */
int pvfs_bufmap_size_query(void);

int pvfs_bufmap_shift_query(void);

int pvfs_bufmap_initialize(
    struct PVFS_dev_map_desc *user_desc);

void pvfs_bufmap_finalize(void);

int pvfs_bufmap_get(
    int *buffer_index);

void pvfs_bufmap_put(
    int buffer_index);

int readdir_index_get(
    int *buffer_index);

void readdir_index_put(
    int buffer_index);

int pvfs_bufmap_copy_from_user(
    int buffer_index,
    void __user *from,
    size_t size);

int pvfs_bufmap_copy_iovec_from_user(
    int buffer_index,
    const struct iovec *iov,
    unsigned long nr_segs,
    size_t size);

int pvfs_bufmap_copy_iovec_from_kernel(
    int buffer_index,
    const struct iovec *iov,
    unsigned long nr_segs,
    size_t size);

int pvfs_bufmap_copy_to_user(
    void __user *to,
    int buffer_index,
    size_t size);

int pvfs_bufmap_copy_to_user_iovec(
    int buffer_index,
    const struct iovec *iov,
    unsigned long nr_segs,
    size_t size);

int pvfs_bufmap_copy_to_kernel_iovec(
    int buffer_index,
    const struct iovec *iov,
    unsigned long nr_segs,
    size_t size);

int pvfs_bufmap_copy_to_kernel(
    void *to,
    int buffer_index,
    size_t size);

int pvfs_bufmap_copy_to_pages(
    int buffer_index, 
    const struct iovec *vec, 
    unsigned long nr_segs, 
    size_t size);

int pvfs_bufmap_copy_from_pages(
    int buffer_index, 
    const struct iovec *vec, 
    unsigned long nr_segs, 
    size_t size);

#ifdef HAVE_AIO_VFS_SUPPORT
size_t pvfs_bufmap_copy_to_user_task(
        struct task_struct *tsk,
        void __user *to,
        size_t size,
        int buffer_index,
        int *buffer_index_offset);
size_t pvfs_bufmap_copy_to_user_task_iovec(
        struct task_struct *tsk,
        struct iovec *iovec,
        unsigned long nr_segs,
        int buffer_index,
        size_t bytes_to_be_copied);
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
