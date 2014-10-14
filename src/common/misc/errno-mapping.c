/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* for POSIX strerror_r */
/* Save _XOPEN_SOURCE */
#ifdef _XOPEN_SOURCE
#if _XOPEN_SOURCE < 600
#define _SAVE_XOPEN_SOURCE _XOPEN_SOURCE
#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#else
#define _XOPEN_SOURCE 600
#endif

#ifdef _GNU_SOURCE
#define _SAVE_GNU_SOURCE _GNU_SOURCE
#undef _GNU_SOURCE
#endif

#ifdef __USE_GNU
#define _SAVE__USE_GNU __USE_GNU
#undef __USE_GNU
#endif

#include <string.h>

/* Restore macros */
#ifdef _SAVE__USE_GNU
#undef __USE_GNU
#define __USE_GNU _SAVE__USE_GNU
#endif

#ifdef _SAVE_GNU_SOURCE
#undef _GNU_SOURCE
#define _GNU_SOURCE _SAVE_GNU_SOURCE
#endif

#ifdef _SAVE_XOPEN_SOURCE
#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE _SAVE_XOPEN_SOURCE
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "pvfs2-internal.h"
#include "pvfs2-util.h"
#include "gossip.h"

#ifdef WIN32
#include "wincommon.h"

/* error codes not defined on Windows */
#define EREMOTE    66
#define EHOSTDOWN  112
#endif

#define MAX_PVFS_STRERROR_LEN 256

/* static global controls whether pvfs_sys calls print user errors */
static int pvfs_perror_gossip_silent = 0;

/* macro defined in include/pvfs2-types.h */
DECLARE_ERRNO_MAPPING_AND_FN();

/**
  the pvfs analog to strerror_r that handles PVFS_error codes as well
  as errno error codes
*/
int PVFS_strerror_r(int errnum, char *buf, int n)
{
    int ret = 0, limit, map_err;

    limit = PVFS_util_min(n, MAX_PVFS_STRERROR_LEN);    

    map_err = PVFS_get_errno_mapping(errnum);

    if (IS_PVFS_NON_ERRNO_ERROR(abs(errnum)))
    {
        snprintf(buf, limit, "%s", PINT_non_errno_strerror_mapping[map_err]);
    }
    else
    {

#if defined(WIN32)
        ret = (int) strerror_s(buf, (size_t) limit, map_err);
#else 
        ret = (int) strerror_r(map_err, buf, (size_t)limit);
#endif
    }

    return ret;
}

/** PVFS_perror()
 *
 * prints a message on stderr, consisting of text argument followed by
 * a colon, space, and error string for the given retcode.  NOTE: also
 * prints a warning if the error code is not in a pvfs2 format and
 * assumes errno
 */
void PVFS_perror(const char *text, int retcode)
{
    int abscode = abs(retcode);
    char buf[MAX_PVFS_STRERROR_LEN] = {0};

    if (IS_PVFS_NON_ERRNO_ERROR(abscode))
    {
        int index = PVFS_get_errno_mapping(abscode);

        snprintf(buf, MAX_PVFS_STRERROR_LEN, "%s: %s (error class: %d)\n", 
                 text, PINT_non_errno_strerror_mapping[index], 
                 PVFS_ERROR_CLASS(abscode));
        fprintf(stderr, "%s", buf);
    }
    else if (IS_PVFS_ERROR(abscode))
    {
        
        PVFS_strerror_r(PVFS_ERROR_TO_ERRNO(abscode), buf, sizeof(buf));

        fprintf(stderr, "%s: %s (error class: %d)\n", text, buf, 
                PVFS_ERROR_CLASS(abscode));
    }
    else
    {
        fprintf(stderr, "Warning: non PVFS2 error code (%d):\n", retcode);

        PVFS_strerror_r(abscode, buf, sizeof(buf));

        fprintf(stderr, "%s: %s\n", text, buf);
    }
    return;
}

/** silences user error messages from system interface calls
 */
void PVFS_perror_gossip_silent(void)
{
    pvfs_perror_gossip_silent = 1;
    return;
}

/** turns on user error messages from system interface calls
 */
void PVFS_perror_gossip_verbose(void)
{
    pvfs_perror_gossip_silent = 0;
    return;
}

/** PVFS_perror_gossip()
 *
 * same as PVFS_perror, except that the output is routed through
 * gossip rather than stderr
 */
void PVFS_perror_gossip(const char *text, int retcode)
{
    int abscode = abs(retcode);
    char buf[MAX_PVFS_STRERROR_LEN] = {0};

    if (pvfs_perror_gossip_silent)
    {
        return;
    }

    if (IS_PVFS_NON_ERRNO_ERROR(abscode))
    {
        int index = PVFS_get_errno_mapping(abscode);

        snprintf(buf, MAX_PVFS_STRERROR_LEN, "%s: %s (error class: %d)\n", 
                 text, PINT_non_errno_strerror_mapping[index], 
                 PVFS_ERROR_CLASS(abscode));
        gossip_err("%s", buf);
    }
    else if (IS_PVFS_ERROR(abscode))
    {
        PVFS_strerror_r(PVFS_ERROR_TO_ERRNO(abscode), buf, sizeof(buf));

        gossip_err("%s: %s (error class: %d)\n", text, buf, 
                   PVFS_ERROR_CLASS(abscode));
    }
    else
    {
        gossip_err("Warning: non PVFS2 error code (%d):\n", retcode);

        PVFS_strerror_r(abscode, buf, sizeof(buf));

        gossip_err("%s: %s\n", text, buf);
    }
    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
