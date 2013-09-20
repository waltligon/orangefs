/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */


/* This code implements generic locking that can be turned on or off at
 * compile time.
 */

#include <stdlib.h>
#include <errno.h>

#include "gen-locks.h"

/***************************************************************
 * visible functions
 */

#ifndef __GEN_NULL_LOCKING__
/*
 * gen_mutex_init()
 *
 * initializes a previously declared mutex
 *
 * returns 0 on success, -1 and sets errno on failure.
 */
int gen_posix_mutex_init(pthread_mutex_t *mutex)
{
    return (pthread_mutex_init(mutex, NULL));
}

int gen_posix_recursive_mutex_init(pthread_mutex_t *mutex)
{
    int rc;
    pthread_mutexattr_t mattr;
    rc = pthread_mutexattr_init(&mattr);
    if (rc < 0)
    {
        return rc;
    }
    rc = pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
    if (rc < 0)
    {
        return rc;
    }
    rc = (pthread_mutex_init(mutex, &mattr));
    pthread_mutexattr_destroy(&mattr);
    /* if this fails it fails silently */
    return rc;
}

int gen_posix_shared_mutex_init(pthread_mutex_t *mutex)
{
    int rc;
    pthread_mutexattr_t mattr;
    rc = pthread_mutexattr_init(&mattr);
    if (rc < 0)
    {
        return rc;
    }
    rc = pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    if (rc < 0)
    {
        return rc;
    }
    rc = (pthread_mutex_init(mutex, &mattr));
    pthread_mutexattr_destroy(&mattr);
    /* if this fails it fails silently */
    return rc;
}

/*
 * gen_mutex_lock()
 *
 * blocks until it obtains a mutex lock on the given mutex
 *
 * returns 0 on success, -1 and sets errno on failure.
 */
int gen_posix_mutex_lock(pthread_mutex_t *mutex)
{
    return (pthread_mutex_lock(mutex));
}


/*
 * gen_mutex_unlock()
 *
 * releases a lock held on a mutex
 *
 * returns 0 on success, -1 and sets errno on failure
 */
int gen_posix_mutex_unlock(pthread_mutex_t *mutex)
{
    return (pthread_mutex_unlock(mutex));
}


/*
 * pthread_mutex_trylock()
 *
 * nonblocking attempt to acquire a lock.
 *
 * returns 0 on success, -1 and sets errno on failure, sets errno to EBUSY
 * if it cannot obtain the lock
 */
int gen_posix_mutex_trylock(pthread_mutex_t *mutex)
{
    return (pthread_mutex_trylock(mutex));
}

/*
 * gen_mutex_destroy()
 *
 * uninitializes the mutex and frees all memory associated with it.
 *
 * returns 0 on success, -errno on failure.
 */
int gen_posix_mutex_destroy(pthread_mutex_t *mutex)
{

    if (!mutex)
    {
	return (-EINVAL);
    }
    pthread_mutex_destroy(mutex);

    return (0);
}

pthread_t gen_posix_thread_self(void)
{
    return pthread_self();
}

int gen_posix_cond_destroy(pthread_cond_t *cond)
{
    if(!cond)
    {
        return -EINVAL;
    }
    pthread_cond_destroy(cond);
    return 0;
}

int gen_posix_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mut)
{
    return pthread_cond_wait(cond, mut);
}

int gen_posix_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mut,
                             const struct timespec *abstime)
{
    return pthread_cond_timedwait(cond, mut, abstime);
}

int gen_posix_cond_signal(pthread_cond_t *cond)
{
    return pthread_cond_signal(cond);
}

int gen_posix_cond_broadcast(pthread_cond_t *cond)
{
    return pthread_cond_broadcast(cond);
}

int gen_posix_cond_init(pthread_cond_t *cond, pthread_condattr_t *attr)
{
    return pthread_cond_init(cond, attr);
}

int gen_posix_shared_cond_init(pthread_cond_t *cond, pthread_condattr_t *attr)
{
    int rc, destroy_attr = 0;
    pthread_condattr_t cattr;
    if (!attr)
    {
        rc = pthread_condattr_init(&cattr);
        if (rc < 0)
        {
            return rc;
        }
        attr = &cattr;
        destroy_attr = 1;
    }
    rc = pthread_condattr_setpshared(attr, PTHREAD_PROCESS_SHARED);
    if (rc < 0)
    {
        return rc;
    }
    rc = pthread_cond_init(cond, attr);
    if (destroy_attr)
    {
        pthread_condattr_destroy(&cattr);
        /* if this fails it fails silently */
    }
    return rc;
}

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
