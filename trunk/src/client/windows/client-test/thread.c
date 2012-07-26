/* Copyright (C) 2011 Omnibond, LLC
   Windows client tests -- thread functions */

#ifdef WIN32

#include <Windows.h>
#include <process.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "thread.h"


/*
#ifdef WIN32
int thread_create(void *handle, void *(*start_routine)(void *), void *arg)
{
    uintptr_t uhandle;
    uhandle = _beginthreadex(NULL, 0, start_routine, arg, 0, NULL);

    if (uhandle == 0)
        return -1;

    memcpy(handle, &uhandle, sizeof(uintptr_t));

    return 0;
}
#else
int thread_create(void *handle, void *(*start_routine)(void *), void *arg)
{
    return pthread_create((pthread_t *) handle,                          
                          NULL,
                          start_routine,
                          arg);
}
#endif
*/

int thread_wait(uintptr_t handle, unsigned int timeout)
{
    DWORD ret;

    ret = WaitForSingleObject((HANDLE) handle, timeout);

    if (ret == WAIT_FAILED)
        ret = GetLastError() * -1;

    return ret;
}

int thread_wait_multiple(unsigned int count, uintptr_t *handles, 
                                  int wait_all, unsigned int timeout)
{
    DWORD ret;

    ret = WaitForMultipleObjects(count, (HANDLE *) handles, wait_all, timeout);

    if (ret == WAIT_FAILED)
        ret = GetLastError() * -1;

    return ret;
}

int get_thread_exit_code(uintptr_t handle, unsigned int *code)
{
    return GetExitCodeThread((HANDLE) handle, (LPDWORD) code);
}

#endif