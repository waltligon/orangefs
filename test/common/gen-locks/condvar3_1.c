/*
 * File: condvar3_1.c
 *
 *
 * --------------------------------------------------------------------------
 *
 *      Pthreads-win32 - POSIX Threads Library for Win32
 *      Copyright(C) 1998 John E. Bossom
 *      Copyright(C) 1999,2005 Pthreads-win32 contributors
 * 
 *      Contact Email: rpj@callisto.canberra.edu.au
 * 
 *      The current list of contributors is contained
 *      in the file CONTRIBUTORS included with the source
 *      code distribution. The list can also be seen at the
 *      following World Wide Web location:
 *      http://sources.redhat.com/pthreads-win32/contributors.html
 * 
 *      This library is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU Lesser General Public
 *      License as published by the Free Software Foundation; either
 *      version 2 of the License, or (at your option) any later version.
 * 
 *      This library is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *      Lesser General Public License for more details.
 * 
 *      You should have received a copy of the GNU Lesser General Public
 *      License along with this library in the file COPYING.LIB;
 *      if not, write to the Free Software Foundation, Inc.,
 *      59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 *
 * --------------------------------------------------------------------------
 *
 * Test Synopsis:
 * - Test timeout of multiple waits on a CV with some signaled.
 *
 * Test Method (Validation or Falsification):
 * - Validation
 *
 * Requirements Tested:
 * - 
 *
 * Features Tested:
 * - 
 *
 * Cases Tested:
 * - 
 *
 * Description:
 * - Because some CVs are never signaled, we expect their waits to time out.
 *   Some are signaled, the rest time out. Pthread_cond_destroy() will fail
 *   unless all are accounted for, either signaled or timedout.
 *
 * Environment:
 * -
 *
 * Input:
 * - None.
 *
 * Output:
 * - File name, Line number, and failed expression on failure.
 * - No output on success.
 *
 * Assumptions:
 * - 
 *
 * Pass Criteria:
 * - pthread_cond_timedwait returns ETIMEDOUT.
 * - Process returns zero exit status.
 *
 * Fail Criteria:
 * - pthread_cond_timedwait does not return ETIMEDOUT.
 * - Process returns non-zero exit status.
 */

//#define _WIN32_WINNT 0x400
#define _USE_32BIT_TIME_T

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <sys/timeb.h>

#include "gen-locks.h"

static gen_cond_t cv;
static gen_cond_t cv1;
static gen_mutex_t mutex;
static gen_mutex_t mutex1;
static struct timespec abstime = { 0, 0 };
static int timedout = 0;
static int signaled = 0;
static int awoken = 0;
static int waiting = 0;

enum {
  NUMTHREADS = 30
};

DWORD WINAPI
mythread(void * arg)
{
  int result;

  assert(gen_mutex_lock(&mutex1) == 0);
  ++waiting;
  assert(gen_mutex_unlock(&mutex1) == 0);
  assert(gen_cond_signal(&cv1) == 0);

  assert(gen_mutex_lock(&mutex) == 0);
  result = gen_cond_timedwait(&cv, &mutex, &abstime);
  if (result == ETIMEDOUT)
    {
      timedout++;
    }
  else
    {
      awoken++;
    }
  assert(gen_mutex_unlock(&mutex) == 0);

  return (DWORD) arg;
}

int thread_join(gen_thread_t thread, LPDWORD retval)
{
    BOOL rc;
    DWORD iretval;
    LPDWORD pretval = (retval) ? retval : &iretval;

    do
    {
        rc = GetExitCodeThread(thread, pretval);
        if (rc && *pretval == STILL_ACTIVE)
        {
            Sleep(500);
        }
    } while (rc && *pretval == STILL_ACTIVE);

    return 0;
}

int
main()
{
  int i;
  gen_thread_t t[NUMTHREADS + 1];
  int result = 0;
  struct _timeb currSysTime;
  const DWORD NANOSEC_PER_MILLISEC = 1000000;

  assert(gen_cond_init(&cv) == 0);
  assert(gen_cond_init(&cv1) == 0);

  assert(gen_mutex_init(&mutex) == 0);
  assert(gen_mutex_init(&mutex1) == 0);

  /* get current system time */
  _ftime_s(&currSysTime);

  abstime.tv_sec = currSysTime.time;
  abstime.tv_nsec = NANOSEC_PER_MILLISEC * currSysTime.millitm;

  abstime.tv_sec += 5;

  assert(gen_mutex_lock(&mutex1) == 0);

  for (i = 1; i <= NUMTHREADS; i++)
    {
      /* assert(pthread_create(&t[i], NULL, mythread, (void *) i) == 0); */
      assert((t[i] = CreateThread(NULL, 0, mythread, (void *) i, 0, NULL)) != NULL);
    }

  do {
    assert(gen_cond_wait(&cv1, &mutex1) == 0);
  } while ( NUMTHREADS > waiting );

  assert(gen_mutex_unlock(&mutex1) == 0);

  for (i = NUMTHREADS/3; i <= 2*NUMTHREADS/3; i++)
    {
      assert(gen_cond_signal(&cv) == 0);

      signaled++;
    }

  for (i = 1; i <= NUMTHREADS; i++)
    {
        assert(thread_join(t[i], (LPDWORD) &result) == 0);
        assert(result == i);
    }

      fprintf(stderr, "awk = %d\n", awoken);
      fprintf(stderr, "sig = %d\n", signaled);
      fprintf(stderr, "tot = %d\n", timedout);

  assert(signaled == awoken);
  assert(timedout == NUMTHREADS - signaled);

  assert(gen_cond_destroy(&cv1) == 0);

  {
  int result = gen_cond_destroy(&cv);
  if (result != 0)
    {
      fprintf(stderr, "Result = %d\n", result);
        fprintf(stderr, "\tWaitersBlocked = %ld\n", cv->nWaitersBlocked);
        fprintf(stderr, "\tWaitersGone = %ld\n", cv->nWaitersGone);
        fprintf(stderr, "\tWaitersToUnblock = %ld\n", cv->nWaitersToUnblock);
        fflush(stderr);
    }
  assert(result == 0);
  }

  assert(gen_mutex_destroy(&mutex1) == 0);
  assert(gen_mutex_destroy(&mutex) == 0);

  return 0;
}
