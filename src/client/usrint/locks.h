/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup usrint
 *
 *  PVFS2 user interface routines
 */

#ifndef LOCKS_H

/** This file defines IO locks as used in libc because the standard
 * include files do not export these definitions correctly if at all
 * on newer systems.
 */

typedef struct _PVFS_lock_s
{
    int lock;    /* zero if unlocked, one if locked */
    int cnt;     /* number of users holding the lock */
    void *owner; /* opaque pointer to the owner of the lock or NULL */
} _PVFS_lock_t;   /* replaces _IO_lock_t */

#define _PVFS_lock_initializer { 0, 0, NULL }
#define _PVFS_lock_finalizer { -1, -1, NULL }

/* these export a pointer to a stream */
#define _PVFS_lock_init(_stream_p) \
    do { \
        ((_PVFS_lock_t *)((_stream_p)->_lock))->lock = 0; \
        ((_PVFS_lock_t *)((_stream_p)->_lock))->cnt = 0; \
        ((_PVFS_lock_t *)((_stream_p)->_lock))->owner = NULL; \
    } while (0)

#define _PVFS_lock_fini(_stream_p) \
    do { \
        ((_PVFS_lock_t *)((_stream_p)->_lock))->lock = -1; \
        ((_PVFS_lock_t *)((_stream_p)->_lock))->cnt = -1; \
        ((_PVFS_lock_t *)((_stream_p)->_lock))->owner = NULL; \
    } while (0)

#define _PVFS_lock_lock(_stream_p) stdio_ops.flockfile(_stream_p) \

#define _PVFS_lock_trylock(_stream_p) stdio_ops.ftrylockfile(_stream_p) \

#define _PVFS_lock_unlock(_stream_p) stdio_ops.funlockfile(_stream_p) \


#endif /* LOCKS_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

