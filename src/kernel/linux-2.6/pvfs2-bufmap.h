/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_BUFMAP_H
#define __PVFS2_BUFMAP_H

/* used to describe mapped buffers */
struct pvfs_bufmap_desc
{
    void* u_ptr;	/* user space address pointer */
};

int pvfs_bufmap_initialize(struct PVFS_dev_map_desc* user_desc);

void pvfs_bufmap_finalize(void);

int pvfs_bufmap_get(struct pvfs_bufmap_desc** desc);

void pvfs_bufmap_put(struct pvfs_bufmap_desc* desc);

int pvfs_bufmap_size_query(void);

int pvfs_bufmap_copy_from_user(struct pvfs_bufmap_desc* to, void* from,
    int size);

int pvfs_bufmap_copy_to_user(void* to, struct pvfs_bufmap_desc* from,
    int size);

#endif /* __PVFS2_BUFMAP_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
