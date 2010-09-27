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

/* Global variables */
/* TODO: may need to init and delete in DLL enter/exit functions */
LPCRITICAL_SECTION cond_list_lock = NULL;

pgen_cond_t cond_list_head = NULL;
pgen_cond_t cond_list_tail = NULL;

/*
 * gen_mutex_init()
 *
 * initializes a previously declared mutex
 *
 * returns 0 on success, -1 and sets errno on failure.
 */
int gen_win_mutex_init(
    HANDLE *mut)
{
    *mut = CreateMutex(NULL, false, NULL);
    return (*mut) ? 0 : -1;
}

/*
 * gen_mutex_lock()
 *
 * blocks until it obtains a mutex lock on the given mutex
 *
 * returns 0 on success, -1 and sets errno on failure.
 */
int gen_win_mutex_lock(
    HANDLE *mut)
{
    DWORD dwWaitResult;

    dwWaitResult = WaitForSingleObject(*mut, INFINITE);

    return (dwWaitResult == WAIT_OBJECT_0 || dwWaitResult == WAIT_ABANDONED) ? 0 : -1;
}


/*
 * gen_mutex_unlock()
 *
 * releases a lock held on a mutex
 *
 * returns 0 on success, -1 and sets errno on failure
 */
int gen_win_mutex_unlock(
    HANDLE *mut)
{
    BOOL rc = ReleaseMutex(*mut);

    return (rc) ? 0 : -1;
}


/*
 * pthread_mutex_trylock()
 *
 * nonblocking attempt to acquire a lock.
 *
 * returns 0 on success, -1 and sets errno on failure, sets errno to EBUSY
 * if it cannot obtain the lock
 */
int gen_win_mutex_trylock(
    HANDLE *mut)
{
    DWORD dwWaitResult;
    int rc;
      
    dwWaitResult = WaitForSingleObject(*mut, 0);
    if (dwWaitResult == WAIT_OBJECT_0 || dwWaitResult == WAIT_ABANDONED)
    {
        rc = 0;
    }
    else
    {
        rc = -1;
        if (dwWaitResult == WAIT_TIMEOUT)
        {
            errno = EBUSY;
        }
        else
        {
            errno = GetLastError();
        }
    }

    return rc;
}

/*
 * gen_mutex_destroy()
 *
 * uninitializes the mutex and frees all memory associated with it.
 *
 * returns 0 on success, -errno on failure.
 */
int gen_win_mutex_destroy(
    HANDLE *mut)
{

    if (!mut || *mut == INVALID_HANDLE_VALUE)
    {
        return (-EINVAL);
    }
    
    CloseHandle(*mut);

    return (0);
}

HANDLE gen_win_thread_self(void)
{
    return GetCurrentThread();
}

int gen_win_cond_destroy(HANDLE cond)
{
    if(!cond)
    {
        return -EINVAL;
    }
    pthread_cond_destroy(cond);
    return 0;
}

int gen_win_cond_wait(HANDLE cond, HANDLE mut)
{
    return pthread_cond_wait(cond, mut);
}

int gen_win_cond_timedwait(HANDLE cond, HANDLE mut,
                             const struct timespec *abstime)
{
    return pthread_cond_timedwait(cond, mut, abstime);
}

int gen_win_cond_signal(HANDLE cond)
{
    return pthread_cond_signal(cond);
}

int gen_win_cond_broadcast(HANDLE cond)
{
    return pthread_cond_broadcast(cond);
}

int gen_win_cond_init(pgen_cond_t *cond)
{
    int rc;
    pgen_cond_t cv = NULL;

    if (!cond)
    {
        return EINVAL;
    }

    /* Allocate condition variable */
    cv = (pgen_cond_t) calloc(1, sizeof(*cv));
    if (cv == NULL)
    {
        rc = ENOMEM;
        goto DONE;
    }

    /* Create locking semaphore */
    cv->semBlockLock = CreateSemaphore(NULL, 1, LONG_MAX, NULL);
    if (cv->semBlockLock == NULL)
    {
        rc = (int) GetLastError();
        goto FAIL0;
    }

    /* Create queue semaphore */
    cv->semBlockQueue = CreateSemaphore(NULL, 0, LONG_MAX, NULL);
    if (cv->semBlockQueue == NULL) 
    {
        rc = (int) GetLastError();
        goto FAIL1;
    }

    /* Create unblock/lock mutex */
    if ((rc = gen_mutex_init(&(cv->mtxUnblockLock))) != 0)
    {
        goto FAIL2;
    }

    rc = 0;

    goto DONE;

    /*
     * Error conditions
     */
FAIL2:
    CloseHandle(cv->semBlockQueue);

FAIL1:
    CloseHandle(cv->semBlockLock);

FAIL0:
    free(cv);
    cv = NULL;

DONE:
    if (rc == 0)
    {
        if (cond_list_lock == NULL)
        {
            InitializeCriticalSection(cond_list_lock);
        }

        EnterCriticalSection(cond_list_lock);

        cv->next = NULL;
        cv->prev = cond_list_tail;

        if (cond_list_tail != NULL) 
        {
            cond_list_tail->next = cv;
        }

        cond_list_tail = cv;

        if (cond_list_head == NULL) 
        {
            cond_list_head = cv;
        }

        LeaveCriticalSection(cond_list_lock);

    }

    *cond = cv;

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
