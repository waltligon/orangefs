/* Copyright (C) 2011 Omnibond, LLC
   Client test -- timer functions */

#include <Windows.h>

#include "timer.h"

/* get the start time for the timer */
unsigned long long timer_start()
{
    LARGE_INTEGER counter;

    if (!QueryPerformanceCounter(&counter))
        return 0;

    return (unsigned) counter.QuadPart;
}

/* get the elapsed time for the timer (seconds) */
double timer_elapsed(unsigned long long start)
{
    LARGE_INTEGER counter, freq;
    unsigned long long elapsed;

    if (!QueryPerformanceCounter(&counter))
        return 0;

    if (!QueryPerformanceFrequency(&freq))
        return 0;

    return ((double) counter.QuadPart - (double) start) / (double) freq.QuadPart;
}

