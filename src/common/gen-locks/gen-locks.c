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
#include <pthread.h>

#include "gen-locks.h"

/***************************************************************
 * visible functions
 */

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
 * gen_mutex_build()
 *
 * allocates storage for and initializes a new pthread_mutex_t
 *
 * returns a pointer to the new mutex on success, NULL on failure.
 */
pthread_mutex_t *gen_posix_mutex_build(
    void)
{

    pthread_mutex_t *mutex_p = NULL;

    mutex_p = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
    if (!mutex_p)
    {
	return (NULL);
    }
    if ((pthread_mutex_init(mutex_p, NULL)) < 0)
    {
	free(mutex_p);
	return (NULL);
    }
    return (mutex_p);
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
    free(mut);

    return (0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
