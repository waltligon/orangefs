/* Copyright (C) 2011 Omnibond, LLC
   Client test -- timer functions */

#ifdef WIN32
#include <Windows.h>
#else
#include <sys/time.h>
#endif
#include <stdlib.h>

#include "timer.h"

/* get the start time for the timer */
#ifdef WIN32
unsigned __int64 timer_start()
{
    LARGE_INTEGER counter;

    if (!QueryPerformanceCounter(&counter))
        return 0;

    return counter.QuadPart;
}
#else
void timer_start(struct timeval *start)
{
    gettimeofday(start, NULL);
}
#endif

#ifdef WIN32
/* get the elapsed time for the timer (seconds) */
double timer_elapsed(unsigned __int64 start)
{
    LARGE_INTEGER counter, freq;

    if (!QueryPerformanceCounter(&counter))
        return 0;

    if (!QueryPerformanceFrequency(&freq))
        return 0;

    return ((double) counter.QuadPart - (double) start) / (double) freq.QuadPart;
}
#else
double timer_elapsed(struct timeval *start)
{
    struct timeval end;
    double elapsed;

    gettimeofday(&end, NULL);

    elapsed = (double) end.tv_sec + (double) end.tv_usec * 0.000001;
    elapsed -= (double) start->tv_sec + (double) start->tv_usec * 0.000001;

    return elapsed;
}
#endif
