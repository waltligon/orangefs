/*
 * (C) 2001-2011 Clemson University, The University of Chicago and
 *               Omnibond LLC
 *
 * See COPYING in top-level directory.
 */


/* This code implements generic locking that can be turned on or off at
 * compile time.
 */

#ifndef _WIN64
#define _USE_32BIT_TIME_T
#endif

#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/timeb.h>

#include "gen-locks.h"

/***************************************************************
 * visible functions
 */

#ifndef __GEN_NULL_LOCKING__

/* Global variables */
/* TODO: may need to init and delete in DLL enter/exit functions */
LPCRITICAL_SECTION cond_list_lock = NULL;
LPCRITICAL_SECTION cond_test_init_lock = NULL;
LPCRITICAL_SECTION mutex_test_init_lock = NULL;

gen_cond_t cond_list_head = NULL;
gen_cond_t cond_list_tail = NULL;

/* This macro sets the value of errno
 * based on the Windows error code.
 */
#define SET_ERROR(winerr)    switch(winerr) { \
                                 case ERROR_SUCCESS: errno = 0; \
                                                     break; \
                                 case ERROR_NOT_ENOUGH_MEMORY: \
                                 case ERROR_OUTOFMEMORY: errno = ENOMEM; \
                                                         break; \
                                 case ERROR_ACCESS_DENIED: errno = EPERM; \
                                                           break; \
                                 case ERROR_INVALID_HANDLE: \
                                 case ERROR_INVALID_PARAMETER: errno = EINVAL; \
                                                               break; \
                                 case WAIT_TIMEOUT: errno = ETIMEDOUT; \
                                                    break; \
                                 default: errno = winerr; \
                             }

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
    if (mut == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    
    *mut = CreateMutex(NULL, FALSE, NULL);
    if (*mut == NULL)
    {
        DWORD err = GetLastError();
        SET_ERROR(err)
    }
    
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
    int result = 0;

    if (*mut == GEN_MUTEX_INITIALIZER)
    {
        /* initialize default mutex */
        if (mutex_test_init_lock == NULL)
        {
            mutex_test_init_lock = (LPCRITICAL_SECTION) calloc(1, sizeof(CRITICAL_SECTION));
            InitializeCriticalSection(mutex_test_init_lock);
        }

        EnterCriticalSection(mutex_test_init_lock);

        gen_mutex_init(mut);

        LeaveCriticalSection(mutex_test_init_lock);
    }

    if (mut == NULL || *mut == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    dwWaitResult = WaitForSingleObject(*mut, INFINITE);

    if (dwWaitResult != WAIT_OBJECT_0 && dwWaitResult != WAIT_ABANDONED)
    {
        DWORD err = GetLastError();
        result = -1;        
        SET_ERROR(err)
    }

    return result;
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
    BOOL rc;

    if (mut == NULL || *mut == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    rc = ReleaseMutex(*mut);
    if (!rc)
    {
        DWORD err = GetLastError();
        SET_ERROR(err)
    }

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

    if (*mut == GEN_MUTEX_INITIALIZER)
    {
        /* initialize default mutex */
        if (mutex_test_init_lock == NULL)
        {
            mutex_test_init_lock = (LPCRITICAL_SECTION) calloc(1, sizeof(CRITICAL_SECTION));
            InitializeCriticalSection(mutex_test_init_lock);
        }

        EnterCriticalSection(mutex_test_init_lock);

        gen_mutex_init(mut);

        LeaveCriticalSection(mutex_test_init_lock);
    }

    if (mut == NULL || *mut == NULL)
    {
        errno = EINVAL;
        return -1;
    }

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
            DWORD err = GetLastError();
            SET_ERROR(err);
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

    if (mut == NULL || *mut == NULL)
    {
        errno = EINVAL;
        return (-EINVAL);
    }
    
    CloseHandle(*mut);

    /* set mutex back to initializer value */
    *mut = GEN_MUTEX_INITIALIZER;

    return 0;
}

HANDLE gen_win_thread_self(void)
{
    return GetCurrentThread();
}

_inline int cond_check_need_init(gen_cond_t *cond)
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

int gen_win_cond_destroy(gen_cond_t *cond)
{
    gen_cond_t cv;
    int result = 0, result1 = 0, result2 = 0;

    if(!cond || !(*cond))
    {
        return EINVAL;
    }
    
    if (*cond != GEN_COND_INITIALIZER)
    {
        EnterCriticalSection(cond_list_lock);

        cv = *cond;

        if (WaitForSingleObject(cv->semBlockLock, INFINITE) != WAIT_OBJECT_0)
        {               
            return errno;
        }

        if ((result = gen_mutex_trylock(&(cv->mtxUnblockLock))) != 0)
        {
            ReleaseSemaphore(cv->semBlockLock, 1, NULL);
            return errno;
        }

        if (cv->nWaitersBlocked > cv->nWaitersGone)
        {
            if (!ReleaseSemaphore(cv->semBlockLock, 1, NULL))
            {
                result = GetLastError();
                SET_ERROR(result)
            }
            result1 = gen_mutex_unlock(&(cv->mtxUnblockLock));
            result2 = EBUSY;
        }
        else
        {
            /* Now it is safe to destroy */
            *cond = NULL;

            if (!CloseHandle(cv->semBlockLock))
            {
                DWORD err = GetLastError();
                SET_ERROR(err)
                result = errno;                
            }
            if (!CloseHandle(cv->semBlockQueue))
            {
                DWORD err = GetLastError();
                SET_ERROR(err)
                result1 = errno;                
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
    gen_cond_t cv;
    int *resultPtr;
} cond_wait_cleanup_args_t;

static void __cdecl cond_wait_cleanup(void *args)
{
    cond_wait_cleanup_args_t *cleanup_args = (cond_wait_cleanup_args_t *) args;
    gen_cond_t cv = cleanup_args->cv;
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

static _inline int cond_timedwait(gen_cond_t *cond,
                                   HANDLE *mutex, const struct timespec *abstime)
{
    int result = 0;
    gen_cond_t cv;
    cond_wait_cleanup_args_t cleanup_args;
    struct _timeb curtime;
    unsigned int nano_ms, ms_diff;

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

    if ((result = WaitForSingleObject(cv->semBlockLock, INFINITE)) != 0)
    {
        SET_ERROR(result)
        return errno;
    }

    ++(cv->nWaitersBlocked);

    if (!ReleaseSemaphore(cv->semBlockLock, 1, NULL))
    {
        DWORD err = GetLastError();
        SET_ERROR(err)
        return errno;
    }

    cleanup_args.mutexPtr = mutex;
    cleanup_args.cv = cv;
    cleanup_args.resultPtr = &result;

#pragma inline_depth(0)

    /* Now we can release mutex and... */
    if ((result = gen_mutex_unlock(mutex)) == 0) 
    {
        /* convert difference in times to milliseconds */
        DWORD ms = INFINITE;
        if (abstime)
        {
            nano_ms = abstime->tv_nsec / 1000000L;
            _ftime_s(&curtime);
            ms = (abstime->tv_sec - curtime.time) > 0 ? (abstime->tv_sec - curtime.time) * 1000 : 0;            
            if (ms > 0) 
            {
                if (nano_ms >= curtime.millitm) 
                {
                    ms_diff = nano_ms - curtime.millitm; 
                }
                else 
                {
                    ms_diff = nano_ms + 1000 - curtime.millitm;
                    ms -= 1000;
                }
            }
            else 
            {
                ms_diff = (nano_ms >= curtime.millitm) ? nano_ms - curtime.millitm : 0;
            }
            ms += ms_diff;
        }
        /* always wait at least 1ms so we get WAIT_TIMEOUT result */
        if (ms == 0) ms = 1;
        
        result = WaitForSingleObject(cv->semBlockQueue, ms);
        SET_ERROR(result)
        result = errno;
    }
    else 
    {
        result = errno;
    }

    cond_wait_cleanup(&cleanup_args);

#pragma inline_depth()

    return result;
}

int gen_win_cond_wait(gen_cond_t *cond, HANDLE *mut)
{    
    return cond_timedwait(cond, mut, NULL);
}

int gen_win_cond_timedwait(gen_cond_t *cond, HANDLE *mut,
                             const struct timespec *abstime)
{    
    return cond_timedwait(cond, mut, abstime);
}

static _inline int cond_unblock(gen_cond_t *cond, int unblockAll)
{
    int result;
    gen_cond_t cv;
    int nSignalsToIssue;

    if (cond == NULL || *cond == NULL)
    {
        return EINVAL;
    }

    errno = 0;

    cv = *cond;

    /* uninitialized static cv */
    if (cv == GEN_COND_INITIALIZER)
    {
        return 0;
    }

    if ((result = gen_mutex_lock(&(cv->mtxUnblockLock))) != 0)
    {
        return errno;
    }

    if (cv->nWaitersToUnblock != 0)
    {
        if (cv->nWaitersBlocked == 0)
        {
            result = gen_mutex_unlock(&(cv->mtxUnblockLock));
            return (result == 0) ? 0 : errno;
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
            SET_ERROR(result)
            gen_mutex_unlock(&(cv->mtxUnblockLock));
            return errno;
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
        result = gen_mutex_unlock(&(cv->mtxUnblockLock));
        return (result == 0) ? 0 : errno;
    }

    if ((result = gen_mutex_unlock(&(cv->mtxUnblockLock))) == 0)
    {
        if (!ReleaseSemaphore(cv->semBlockQueue, nSignalsToIssue, NULL))
        {
            result = GetLastError();
            SET_ERROR(result)
        }
    }


    return errno;
}

int gen_win_cond_signal(gen_cond_t *cond)
{   
    return cond_unblock(cond, FALSE);
}

int gen_win_cond_broadcast(gen_cond_t *cond)
{    
    return cond_unblock(cond, TRUE);
}

int gen_win_cond_init(gen_cond_t *cond)
{
    DWORD err;
    gen_cond_t cv = NULL;

    if (!cond)
    {
        return EINVAL;
    }

    /* Allocate condition variable */
    cv = (gen_cond_t) calloc(1, sizeof(*cv));
    if (cv == NULL)
    {
        err = ENOMEM;
        goto DONE;
    }

    /* Create locking semaphore */
    cv->semBlockLock = CreateSemaphore(NULL, 1, LONG_MAX, NULL);
    if (cv->semBlockLock == NULL)
    {
        err = GetLastError();
        SET_ERROR(err)
        goto FAIL0;
    }

    /* Create queue semaphore */
    cv->semBlockQueue = CreateSemaphore(NULL, 0, LONG_MAX, NULL);
    if (cv->semBlockQueue == NULL) 
    {
        err = GetLastError();
        SET_ERROR(err)
        goto FAIL1;
    }

    /* Create unblock/lock mutex */
    if ((err = gen_mutex_init(&(cv->mtxUnblockLock))) != 0)
    {
        SET_ERROR(err)
        goto FAIL2;
    }

    err = 0;

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
    if (err == 0)
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

    return errno;
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
