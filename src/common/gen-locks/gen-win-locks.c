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
LPCRITICAL_SECTION cond_test_init_lock = NULL;

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
    *mut = CreateMutex(NULL, FALSE, NULL);
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

__inline int cond_check_need_init(pgen_cond_t *cond)
{
    int result = 0;

    /* initialize critical section if necessary */
    if (cond_test_init_lock == NULL)
    {
        cond_test_init_lock = (LPCRITICAL_SECTION) calloc(1, sizeof(CRITICAL_SECTION));
        InitializeCriticalSection(cond_test_init_lock);
    }

    /* initialize condition variable created with GEN_COND_INITIALIZER */
    EnterCriticalSection(cond_test_init_lock);
    
    if (*cond == GEN_COND_INITIALIZER)
    {
        result = gen_cond_init(cond);
    }
    else if (*cond == NULL)
    {
        result = EINVAL;
    }

    LeaveCriticalSection(cond_test_init_lock);

    return result;
}

int gen_win_cond_destroy(pgen_cond_t *cond)
{
    pgen_cond_t cv;
    int result = 0, result1 = 0, result2 = 0;

    if(!cond || !(*cond))
    {
        return -EINVAL;
    }
    
    if (*cond != GEN_COND_INITIALIZER)
    {
        EnterCriticalSection(cond_list_lock);

        cv = *cond;

        if (WaitForSingleObject(cv->semBlockLock, INFINITE) != WAIT_OBJECT_0)
        {   
            return GetLastError();
        }

        if ((result = gen_mutex_trylock(&(cv->mtxUnblockLock))) != 0)
        {
            ReleaseSemaphore(cv->semBlockLock, 1, NULL);
            return result;
        }

        if (cv->nWaitersBlocked > cv->nWaitersGone)
        {
            if (!ReleaseSemaphore(cv->semBlockLock, 1, NULL))
            {
                result = GetLastError();
            }
            result1 = gen_mutex_unlock(&(cv->mtxUnblockLock));
            result2 = EBUSY;
        }
        else
        {
            /* Now it is safe to destroy */
            *cond = NULL;

            if (CloseHandle(cv->semBlockLock) != 0)
            {
                result = GetLastError();
            }
            if (CloseHandle(cv->semBlockQueue) != 0)
            {
                result1 = GetLastError();
            }
            if ((result2 = gen_mutex_unlock(&(cv->mtxUnblockLock))) == 0)
            {
                result2 = gen_mutex_destroy(&(cv->mtxUnblockLock));
            }

            /* Unlink the CV from the list */
            if (cond_list_head == cv)
            {
                cond_list_head = cv->next;
            }
            else 
            {
                cv->prev->next = cv->next;
            }

            if (cond_list_tail == cv) 
            {
                cond_list_tail = cv->prev;
            }
            else {
                cv->next->prev = cv->prev;
            }

            free(cv);
        }

        LeaveCriticalSection(cond_list_lock);
    }
    else
    {
        EnterCriticalSection(cond_test_init_lock);

        if (*cond == GEN_COND_INITIALIZER) 
        {
            *cond = NULL;
        }
        else 
        {
            result = EBUSY;
        }

        LeaveCriticalSection(cond_test_init_lock);
    }

    return ((result != 0) ? result : ((result1 != 0) ? result1 : result2));
}

typedef struct
{
    gen_mutex_t *mutexPtr;
    pgen_cond_t cv;
    int *resultPtr;
} cond_wait_cleanup_args_t;

static void __cdecl cond_wait_cleanup(void *args)
{
    cond_wait_cleanup_args_t *cleanup_args = (cond_wait_cleanup_args_t *) args;
    pgen_cond_t cv = cleanup_args->cv;
    int *resultPtr = cleanup_args->resultPtr;
    int nSignalsWasLeft;
    int result;

    if ((result = gen_mutex_lock(&(cv->mtxUnblockLock))) != 0) 
    {
        *resultPtr = result;
        return;
    }

    if ((nSignalsWasLeft = cv->nWaitersToUnblock) != 0)
    {
        --(cv->nWaitersToUnblock);
    }
    else if (INT_MAX / 2 == ++(cv->nWaitersGone))
    {
        if (WaitForSingleObject(cv->semBlockLock, INFINITE) != WAIT_OBJECT_0)
        {
            *resultPtr = (int) GetLastError();
            return;
        }
        cv->nWaitersBlocked -= cv->nWaitersGone;
        if (!ReleaseSemaphore(cv->semBlockLock, 1, NULL))
        {
            *resultPtr = (int) GetLastError();
            return;
        }
        cv->nWaitersGone = 0;
    }

    if ((result = gen_mutex_unlock(&(cv->mtxUnblockLock))) != 0)
    {
        *resultPtr = result;
        return;
    }

    if (nSignalsWasLeft == 1) 
    {
        if (!ReleaseSemaphore(cv->semBlockLock, 1, NULL))
        {
            *resultPtr = (int) GetLastError();
            return;
        }
    }

    if ((result = gen_mutex_lock(cleanup_args->mutexPtr)) != 0)
    {
        *resultPtr = result;
    }

}

static __inline int cond_timedwait(pgen_cond_t *cond,
                                   HANDLE *mutex, const struct timespec *abstime)
{
    int result = 0;
    pgen_cond_t cv;
    cond_wait_cleanup_args_t cleanup_args;

    if (cond == NULL || *cond == NULL) 
    {
        return EINVAL;
    }

    if (*cond == GEN_COND_INITIALIZER)
    {
        result = cond_check_need_init(cond);
    }

    if (result != 0 && result != EBUSY) 
    {
        return result;
    }

    cv = *cond;

    if (WaitForSingleObject(cv->semBlockLock, INFINITE) != 0)
    {
        return (int) GetLastError();
    }

    ++(cv->nWaitersBlocked);

    if (!ReleaseSemaphore(cv->semBlockLock, 1, NULL))
    {
        return (int) GetLastError();
    }

    cleanup_args.mutexPtr = mutex;
    cleanup_args.cv = cv;
    cleanup_args.resultPtr = &result;

#pragma inline_depth(0)

    /* Now we can release mutex and... */
    if ((result = gen_mutex_unlock(mutex)) == 0) 
    {
        // convert timespec to milliseconds
        DWORD ms = INFINITE;
        if (abstime)
        {
            ms = abstime->tv_sec * 1000 + abstime->tv_nsec / 1000000L;
        }
        if (WaitForSingleObject(cv->semBlockQueue, ms) != WAIT_OBJECT_0)
        {
            result = GetLastError();
        }
    }

    cond_wait_cleanup(&cleanup_args);

#pragma inline_depth()

    return result;
}

int gen_win_cond_wait(pgen_cond_t *cond, HANDLE *mut)
{    
    return cond_timedwait(cond, mut, NULL);
}

int gen_win_cond_timedwait(pgen_cond_t *cond, HANDLE *mut,
                             const struct timespec *abstime)
{    
    return cond_timedwait(cond, mut, abstime);
}

static __inline int cond_unblock(pgen_cond_t *cond, int unblockAll)
{
    int result;
    pgen_cond_t cv;
    int nSignalsToIssue;

    if (cond == NULL || *cond == NULL)
    {
        return EINVAL;
    }

    cv = *cond;

    /* uninitialized static cv */
    if (cv == GEN_COND_INITIALIZER)
    {
        return 0;
    }

    if ((result = gen_mutex_lock(&(cv->mtxUnblockLock))) != 0)
    {
        return result;
    }

    if (cv->nWaitersToUnblock != 0)
    {
        if (cv->nWaitersBlocked == 0)
        {
            return gen_mutex_unlock(&(cv->mtxUnblockLock));
        }
        if (unblockAll)
        {
            cv->nWaitersToUnblock += (nSignalsToIssue = cv->nWaitersBlocked);
            cv->nWaitersBlocked = 0;
        }
        else 
        {
            nSignalsToIssue = 1;
            cv->nWaitersToUnblock++;
            cv->nWaitersBlocked--;
        }
    }
    else if (cv->nWaitersBlocked > cv->nWaitersGone)
    {
        if (WaitForSingleObject(cv->semBlockLock, INFINITE) != WAIT_OBJECT_0)
        {
            result = GetLastError();
            gen_mutex_unlock(&(cv->mtxUnblockLock));
            return result;
        }
        if (cv->nWaitersGone != 0)
        {
            cv->nWaitersBlocked -= cv->nWaitersGone;
        }
        if (unblockAll)
        {
            nSignalsToIssue = cv->nWaitersToUnblock = cv->nWaitersBlocked;
            cv->nWaitersBlocked = 0;
        }
        else
        {
            nSignalsToIssue = cv->nWaitersToUnblock = 1;
            cv->nWaitersBlocked--;
        }
    }
    else 
    {
        return gen_mutex_unlock(&(cv->mtxUnblockLock));
    }

    if ((result = gen_mutex_unlock(&(cv->mtxUnblockLock))) == 0)
    {
        if (!ReleaseSemaphore(cv->semBlockQueue, nSignalsToIssue, NULL))
        {
            result = GetLastError();
        }
    }

    return result;
}

int gen_win_cond_signal(pgen_cond_t *cond)
{   
    return cond_unblock(cond, FALSE);
}

int gen_win_cond_broadcast(pgen_cond_t *cond)
{    
    return cond_unblock(cond, TRUE);
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
            cond_list_lock = (LPCRITICAL_SECTION) calloc(1, sizeof(CRITICAL_SECTION));
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
