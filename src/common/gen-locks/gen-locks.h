/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* this is a header for a customizable set of locking primitives.  They can
 * be turned on or off through compile time defines.  Application
 * programmers should use the following interface for portability rather
 * than directly calling specific lock implementations:
 *
 * int gen_mutex_lock(gen_mutex_t* mut);
 * int gen_mutex_unlock(gen_mutex_t* mut);
 * int gen_mutex_trylock(gen_mutex_t* mut);
 * int gen_mutex_destroy(gen_mutex_t* mut); 
 *
 * See the examples directory for details.
 */

#ifndef __GEN_LOCKS_H
#define __GEN_LOCKS_H

#include <stdlib.h>

/* we will make posix locks the default unless turned off for now. */
/* this is especially important for development in which case the locks
 * should really be enabled in order to verify proper operation 
 */
#if !defined(__GEN_NULL_LOCKING__) && !defined(__GEN_POSIX_LOCKING__)
#define __GEN_POSIX_LOCKING__
#endif

#ifdef __GEN_POSIX_LOCKING__
#include <pthread.h>

	/* function prototypes for specific locking implementations */
int gen_posix_mutex_lock(pthread_mutex_t * mut);
int gen_posix_mutex_unlock(pthread_mutex_t * mut);
int gen_posix_mutex_trylock(pthread_mutex_t * mut);
pthread_mutex_t *gen_posix_mutex_build(void);
int gen_posix_mutex_destroy(pthread_mutex_t * mut);
int gen_posix_mutex_init(pthread_mutex_t * mut);
pthread_t gen_posix_thread_self(void);

int gen_posix_cond_init(pthread_cond_t *cond, pthread_condattr_t *attr);
int gen_posix_cond_destroy(pthread_cond_t *cond);
int gen_posix_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mut);
int gen_posix_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mut,
                             const struct timespec *abstime);
int gen_posix_cond_signal(pthread_cond_t *cond);
int gen_posix_cond_broadcast(pthread_cond_t *cond);

typedef pthread_mutex_t gen_mutex_t;
typedef pthread_t       gen_thread_t;
typedef pthread_cond_t  gen_cond_t;
#define GEN_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER;
#define GEN_COND_INITIALIZER PTHREAD_COND_INITIALIZER;
#define gen_mutex_lock(m) gen_posix_mutex_lock(m)
#define gen_mutex_unlock(m) gen_posix_mutex_unlock(m)
#define gen_mutex_trylock(m) gen_posix_mutex_trylock(m)
#define gen_mutex_destroy(m) gen_posix_mutex_destroy(m)
#define gen_mutex_init(m) gen_posix_mutex_init(m)
#define gen_thread_self() gen_posix_thread_self()

#define gen_cond_init(c) gen_posix_cond_init(c, NULL)
#define gen_cond_destroy(c) gen_posix_cond_destroy(c)
#define gen_cond_wait(c, m) gen_posix_cond_wait(c, m)
#define gen_cond_timedwait(c, m, s) gen_posix_cond_timedwait(c, m, s)
#define gen_cond_signal(c) gen_posix_cond_signal(c)
#define gen_cond_broadcast(c) gen_posix_cond_broadcast(c)

#elif defined (__GEN_NULL_LOCKING__)
	/* this stuff messes around just enough to prevent warnings */
typedef int gen_mutex_t;
typedef unsigned long gen_thread_t;
typedef int gen_cond_t;

#define GEN_MUTEX_INITIALIZER 0
#define GEN_COND_INITIALIZER 0

static inline int gen_mutex_lock(
    gen_mutex_t * mutex_p)
{
    (void) mutex_p;
    return 0;
}
static inline int gen_mutex_unlock(
    gen_mutex_t * mutex_p)
{
    (void) mutex_p;
    return 0;
}
static inline int gen_mutex_trylock(
    gen_mutex_t * mutex_p)
{
    (void) mutex_p;
    return 0;
}
static inline gen_thread_t gen_thread_self(void)
{
    return 0;
}
#define gen_mutex_init(m) do{}while(0)
#define gen_mutex_destroy(m) do{}while(0)

#define gen_cond_init(c) do{}while(0)
#define gen_cond_destroy(c) do{}while(0)

static inline int gen_cond_wait(gen_cond_t *cond, gen_mutex_t *mut)
{
    (void) cond;
    return 0;
}

static inline int gen_cond_timedwait(gen_cond_t *cond, gen_mutex_t *mut,
                                     const struct timespec *abstime)
{
    (void) cond;
    return 0;
}

static inline int gen_cond_signal(gen_cond_t *cond)
{
    (void) cond;
    return 0;
}

static inline int gen_cond_broadcast(gen_cond_t *cond)
{
    (void) cond;
    return 0;
}

#else /* __GEN_NULL_LOCKING__ */
#error "Must define either POSIX or NULL locking"
#endif

#endif /* __GEN_LOCKS_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
