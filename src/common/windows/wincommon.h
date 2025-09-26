/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 * declarations for Windows
 */

#ifndef __WINCOMMON_H
#define __WINCOMMON_H

#include <Windows.h>
#include <sys/timeb.h>

#define __inline__     _inline
#define inline         _inline
#define __func__       __FUNCTION__

/* ignore the __attribute__ keyword */
#define __attribute__(x)  

#define index(s, c)    strchr(s, c)
/* #define snprintf(s, n, f, ...)    _snprintf(s, n, f, __VA_ARGS__) */
#define strdup(s)      _strdup(s)
#define strcasecmp     stricmp
#define strncasecmp    strnicmp
#define strtoll(str, end, base)    _atoi64(str)
#define strtok_r(str, delim, saveptr)    strtok_s(str, delim, saveptr)

/*
 * gettimeofday
 */
static int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    struct _timeb timebuffer;
    errno_t ret;

    memset(&timebuffer, 0, sizeof(struct _timeb));
    ret = _ftime_s(&timebuffer);
    if (ret == 0)
    {
        tv->tv_sec = (long) timebuffer.time;
        tv->tv_usec = timebuffer.millitm * 1000;
    }

    return ret;
}

#endif
