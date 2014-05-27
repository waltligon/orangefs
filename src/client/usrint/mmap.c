
/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
/** \file
 *  \ingroup usrint
 *
 *  mmap operations for user interface
 */

#include "usrint.h"
#include "posix-ops.h"
#include "posix-pvfs.h"
#include "openfile-util.h"
#include <quicklist.h>

static struct qlist_head maplist = QLIST_HEAD_INIT(maplist);

/** PVFS mmap
 *
 *  This is a very basic implementation that reads whole mapped
 *  region into memory and writes it back if shared on unmap.
 *
 *  This may not perform well or do all of the neat things mmap
 *  does, but it will let basic stuff work.
 */
void *pvfs_mmap(void *start,
                size_t length,
                int prot,
                int flags,
                int fd,
                off_t offset)
{
    int rc = 0;
    pvfs_descriptor *pd;
    struct pvfs_mmap_s *mlist;
    void *maddr;

    if (flags & MAP_ANONYMOUS)
    {
        void *maddr;
        /* this isn't a file system map - just do it */
        maddr = glibc_ops.mmap(start, length, prot, flags, fd, offset);
        if (maddr == MAP_FAILED)
        {
            return MAP_FAILED;
        }
        /* and done */
        return maddr;
    }
    /* this is a PVFS file system map */
    /* first find the open file */
    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        return MAP_FAILED;
    }
    /* we will map an ANON region and read the file into it */
    maddr = glibc_ops.mmap(start, length, prot, flags & MAP_ANONYMOUS,
                               -1, offset);
    if (maddr == MAP_FAILED)
    {
        return MAP_FAILED;
    }
    rc = pvfs_pread(fd, maddr, length, offset);
    if (rc < 0)
    {
        glibc_ops.munmap(maddr, length);
        return MAP_FAILED;
    }
    /* record this in the open file descriptor */
    mlist = (struct pvfs_mmap_s *)malloc(sizeof(struct pvfs_mmap_s));
    mlist->mst = start;
    mlist->mlen = length;
    mlist->mprot = prot;
    mlist->mflags = flags;
    mlist->mfd = fd;
    mlist->moff = offset;
    qlist_add(&mlist->link, &maplist);
    /* and done */
    return maddr;
}

/** PVFS munmap
 *
 *  for now only unmap whole regions mapped with mmap
 */
int pvfs_munmap(void *start, size_t length)
{
    int rc = 0;
    struct pvfs_mmap_s *mapl, *temp;
    long long pagesize = getpagesize();

#if PVFS2_SIZEOF_VOIDP == 64
    if (((uint64_t)start % pagesize) != 0 || (length % pagesize) != 0)
#else
    if (((uint32_t)start % pagesize) != 0 || (length % pagesize) != 0)
#endif
    {
        errno = EINVAL;
        return -1;
    }
    qlist_for_each_entry_safe(mapl, temp, &maplist, link)
    {
        /* assuming we must unmap something that was mapped */
        /* and not just part of it */
        if (mapl->mst == start && mapl->mlen == length)
        {
            qlist_del(&mapl->link);
            break;
        }
    }
    if (!mapl)
    {
        errno = EINVAL;
        return -1;
    }
    if (mapl->mflags & MAP_SHARED)
    {
        pvfs_pwrite(mapl->mfd, mapl->mst, mapl->mlen, mapl->moff);
    }
    rc = glibc_ops.munmap(start, length);
    free(mapl);
    return rc;
}

/** PVFS msync
 *
 *  We ignore flags for now - only syncronous writebacks
 *  can add async later - but invalidate is not likely
 */
int pvfs_msync(void *start, size_t length, int flags)
{
    int rc = 0;
    struct pvfs_mmap_s *mapl, *temp;
    long long pagesize = getpagesize();

#if PVFS2_SIZEOF_VOIDP == 64
    if (((uint64_t)start % pagesize) != 0 || (length % pagesize) != 0)
#else
    if (((uint32_t)start % pagesize) != 0 || (length % pagesize) != 0)
#endif
    {
        errno = EINVAL;
        return -1;
    }
    qlist_for_each_entry_safe(mapl, temp, &maplist, link)
    {
        if ((u_char *)mapl->mst <= (u_char *)start &&
            (u_char *)mapl->mst + mapl->mlen >= (u_char *)start + length)
        {
            break;
        }
    }
    if (!mapl)
    {
        errno = ENOMEM;
        return -1;
    }
    if (mapl->mflags & MAP_SHARED)
    {
        /* the diff between start and mst is distance from */
        /* start of buffer, and distance from original offset */
        rc = pvfs_pwrite(mapl->mfd,
                         start,
                         length,
                         mapl->moff + ((u_char *)start - (u_char *)mapl->mst));
    }
    return rc;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

