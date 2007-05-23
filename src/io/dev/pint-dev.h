/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#ifndef __PINT_DEV_H
#define __PINT_DEV_H

#include "pvfs2-types.h"
#include "pint-dev-shared.h"

enum pvfs_bufmap_type {
    BM_IO = 0,
    BM_READDIR = 1,
};

/* describes unexpected messages coming out of the device */
struct PINT_dev_unexp_info
{
    void *buffer;
    int size;
    PVFS_id_gen_t tag;
};

/* types of memory buffers accepted in the write calls */
enum PINT_dev_buffer_type
{
    PINT_DEV_PRE_ALLOC = 1,
    PINT_DEV_EXT_ALLOC = 2
};

struct PINT_dev_params 
{
    uint32_t dev_buffer_count;
    uint64_t dev_buffer_size;
};

int PINT_dev_initialize(
    const char* dev_name,
    int flags);

int PINT_dev_get_mapped_regions(
    int ndesc,
    struct PVFS_dev_map_desc *desc,
    struct PINT_dev_params *params);

void PINT_dev_put_mapped_regions(
    int ndesc,
    struct PVFS_dev_map_desc *desc);

void *PINT_dev_get_mapped_buffer(
    enum pvfs_bufmap_type bm_type,
    struct PVFS_dev_map_desc *desc,
    int buffer_index);

int PINT_dev_test_unexpected(
    int incount,
    int *outcount,
    struct PINT_dev_unexp_info *info_array,
    int max_idle_time);

int PINT_dev_release_unexpected(
    struct PINT_dev_unexp_info* info);

int PINT_dev_write_list(
    void **buffer_list,
    int *size_list,
    int list_count,
    int total_size,
    enum PINT_dev_buffer_type buffer_type,
    PVFS_id_gen_t tag);

int PINT_dev_write(
    void *buffer,
    int size,
    enum PINT_dev_buffer_type buffer_type,
    PVFS_id_gen_t tag);

int PINT_dev_remount(void);
void *PINT_dev_memalloc(int size);
void PINT_dev_memfree(void* buffer, int size);
void PINT_dev_finalize(void);

#endif /* __PINT_DEV_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
