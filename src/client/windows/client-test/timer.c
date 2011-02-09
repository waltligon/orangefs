/* Copyright (C) 2011 Omnibond, LLC
   Client test -- timer functions */

#include <Windows.h>

#include "timer.h"

/* get the start time for the timer */
unsigned __int64 timer_start()
{
    LARGE_INTEGER counter;

    if (!QueryPerformanceCounter(&counter))
        return 0;

    return counter.QuadPart;
}

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

