/* Copyright (C) 2011 Omnibond, LLC
   Client test -- timer declarations */

#ifdef WIN32
/* get the start time for the timer */
unsigned __int64 timer_start();

/* get the elapsed time for the timer (seconds) */
double timer_elapsed(unsigned __int64 start);
#else
#include <sys/time.h>

void timer_start(struct timeval *start);
double timer_elapsed(struct timeval *start);
#endif
