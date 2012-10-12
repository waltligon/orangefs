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
int gen_posix_mutex_init(
    pthread_mutex_t * mut)
{
    return (pthread_mutex_init(mut, NULL));
}

/*
 * gen_mutex_lock()
 *
 * blocks until it obtains a mutex lock on the given mutex
 *
 * returns 0 on success, -1 and sets errno on failure.
 */
int gen_posix_mutex_lock(
    pthread_mutex_t * mut)
{
    return (pthread_mutex_lock(mut));
}


/*
 * gen_mutex_unlock()
 *
 * releases a lock held on a mutex
 *
 * returns 0 on success, -1 and sets errno on failure
 */
int gen_posix_mutex_unlock(
    pthread_mutex_t * mut)
{
    return (pthread_mutex_unlock(mut));
}


/*
 * pthread_mutex_trylock()
 *
 * nonblocking attempt to acquire a lock.
 *
 * returns 0 on success, -1 and sets errno on failure, sets errno to EBUSY
 * if it cannot obtain the lock
 */
int gen_posix_mutex_trylock(
    pthread_mutex_t * mut)
{
    return (pthread_mutex_trylock(mut));
}

/*
 * gen_mutex_destroy()
 *
 * uninitializes the mutex and frees all memory associated with it.
 *
 * returns 0 on success, -errno on failure.
 */
int gen_posix_mutex_destroy(
    pthread_mutex_t * mut)
{

    if (!mut)
    {
	return (-EINVAL);
    }
    pthread_mutex_destroy(mut);

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

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
