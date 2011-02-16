/* Copyright (C) 2011 Omnibond, LLC
   Windows client tests -- thread functions */

#include <Windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "thread.h"

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
