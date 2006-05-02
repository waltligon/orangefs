/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "server-config.h"
#include "dbpf-version-buffer.h"
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

/* NOTE: none of this code is thread-safe.  Namely, we assume that any
 * of these functions are only called from the dbpf thread.
 */

size_t PINT_dbpf_version_allowed_buffer_size = (1024*1024*16);
static TROVE_size memory_used = 0;

static int dbpf_version_buffer_is_mem_full(
    TROVE_coll_id coll_id, TROVE_size total)
{
    if(total + memory_used > PINT_dbpf_version_allowed_buffer_size)
    {
        return 1;
    }
    return 0;
}

int dbpf_version_buffer_create(TROVE_coll_id coll_id,
                               TROVE_handle handle,
                               uint32_t version,
                               char ** mem_regions,
                               TROVE_size * mem_sizes,
                               int mem_count,
                               dbpf_version_buffer_ref * refp)
{
    dbpf_version_buffer_ref ref;
    int total_mem = 0;
    int i = 0;

    for(i = 0; i < mem_count; ++i)
    {
        total_mem += mem_sizes[i];
    }

    ref.size = total_mem;
    ref.version = version;

    if(dbpf_version_buffer_is_mem_full(coll_id, total_mem))
    {
        /* use file */
        char filename[PATH_MAX];
        DBPF_GET_VERSIONED_BSTREAM_FILENAME(
            filename, PATH_MAX, my_storage_p->name, 
            coll_id, handle, ref.version);

        ref.u.fd = DBPF_OPEN(filename, O_RDWR|O_CREAT|O_EXCL, TROVE_DB_MODE);
        if(ref.u.fd < 0)
        {
            return -(PVFS_get_errno_mapping(errno) | PVFS_ERROR_TROVE);
        }

        ref.type = DBPF_VERSION_BUFFER_FD;
    
        for(i = 0; i < mem_count; ++i)
        {
            int b;
            
            b = DBPF_WRITE(ref.u.fd, mem_regions[i], mem_sizes[i]);
            if(b < 0)
            {
                DBPF_CLOSE(ref.u.fd);
                return -(PVFS_get_errno_mapping(errno) | PVFS_ERROR_TROVE);
            }
        }
    }
    else
    {
        /* use memory */

        ref.u.memp = malloc(total_mem);
        if(!ref.u.memp)
        {
            return -TROVE_ENOMEM;
        }

        for(i = 0; i < mem_count; ++i)
        {
            memcpy(ref.u.memp, mem_regions[i], mem_sizes[i]);
        }

        memory_used += total_mem;
    }

    return 0;
}

int dbpf_version_buffer_get(dbpf_version_buffer_ref * ref,
                            char ** mem,
                            TROVE_size * size)
{
    if(ref->type == DBPF_VERSION_BUFFER_MEM)
    {
        *mem = ref->u.memp;
        *size = ref->size;
    }
    else
    {
        int b;

        *mem = malloc(ref->size);
        if(!*mem)
        {
            return -TROVE_ENOMEM;
        }

        b = DBPF_READ(ref->u.fd, *mem, ref->size); 
        if(b < 0)
        {
            free(*mem);
            return -(PVFS_get_errno_mapping(errno) | PVFS_ERROR_TROVE);
        }

        if(b != ref->size)
        {
            return -TROVE_EINVAL;
        }

        *size = ref->size;
    }

    return 0;
}

int dbpf_version_buffer_destroy(
        TROVE_coll_id coll_id,
        TROVE_handle handle,
        dbpf_version_buffer_ref * ref)
{
    if(ref->type == DBPF_VERSION_BUFFER_MEM)
    {
        free(ref->u.memp);
        memory_used -= ref->size;
    }
    else
    {
        char filename[PATH_MAX];

        DBPF_CLOSE(ref->u.fd);

        DBPF_GET_VERSIONED_BSTREAM_FILENAME(
            filename, PATH_MAX, my_storage_p->name, 
            coll_id, handle, ref->version);

        if(DBPF_UNLINK(filename) < 0)
        {
            return -(PVFS_get_errno_mapping(errno) | PVFS_ERROR_TROVE);
        }
    }

    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
