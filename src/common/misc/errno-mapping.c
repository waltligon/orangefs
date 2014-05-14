/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */


#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pvfs2-internal.h"
#include "pvfs2-util.h"
#include "gossip.h"

#ifdef WIN32
#include "wincommon.h"

/* error codes not defined on Windows */
#define EREMOTE    66
#define EHOSTDOWN  112
#endif

/* static global controls whether pvfs_sys calls print user errors */
static int pvfs_perror_gossip_silent = 0;

/* macro defined in include/pvfs2-types.h */
DECLARE_ERRNO_MAPPING_AND_FN();

/**
 * The PVFS analog to strerror_r that handles PVFS_error codes as well as errno
 * error codes. The fact that this does not ever fail is used in functions
 * below but should not be relied on in other modules.
 */
int PVFS_strerror_r(int errnum, char *buf, int n)
{
    size_t i = 0, r;
    int map_err;
    int class;
    map_err = PVFS_get_errno_mapping(-errnum);
    if (errnum >= 0)
    {
        gossip_err("PVFS_strerror_r: called with positive errnum. "
                "please fix in caller.\n");
        abort();
    }

    if (IS_PVFS_NON_ERRNO_ERROR(-errnum))
    {
        r = snprintf(buf+i, n-i,
                "%s", PINT_non_errno_strerror_mapping[map_err]);
        if (i+r <= n) {
            i += r;
        }
    }
    else
    {
#if defined(HAVE_GNU_STRERROR_R) || defined(_GNU_SOURCE)
        char *tmp;
        size_t tmplen;
        tmp = strerror_r(map_err, buf+i, n-i);
        tmplen = strlen(tmp);
        if (strcmp(tmp, buf+i) == 0)
        {
            /* If there wasn't a complete copy due to lack of space,
             * this will not run. */
            i += tmplen;
        }
        else
        {
            strncpy(buf+i, tmp, n-i);
            if (tmplen >= n-i)
            {
                i += n-i;
            }
            else
            {
                i += tmplen;
            }
        }
#elif defined(WIN32)
        if (strerror_s(buf, (size_t)limit, map_err) != 0)
        {
            r = snprintf(buf+i, n-i, "could not lookup system error message");
            if (i+r <= n) {
                i += r;
            }
        }
#else
        if (strerror_r(map_err, buf+i, n-i) != 0)
        {
            r = snprintf(buf+i, n-i, "could not lookup system error message");
            if (i+r <= n) {
                i += r;
            }
        }
#endif
    }

    class = PVFS_ERROR_CLASS(-errnum);
    switch (class)
    {
    case PVFS_ERROR_BMI:
        r = snprintf(buf+i, n-i, " (BMI)");
        break;
    case PVFS_ERROR_TROVE:
        r = snprintf(buf+i, n-i, " (TROVE)");
        break;
    case PVFS_ERROR_FLOW:
        r = snprintf(buf+i, n-i, " (FLOW)");
        break;
    case PVFS_ERROR_SM:
        r = snprintf(buf+i, n-i, " (SM)");
        break;
    case PVFS_ERROR_SCHED:
        r = snprintf(buf+i, n-i, " (SCHED)");
        break;
    case PVFS_ERROR_CLIENT:
        r = snprintf(buf+i, n-i, " (CLIENT)");
        break;
    case PVFS_ERROR_DEV:
        r = snprintf(buf+i, n-i, " (DEV)");
        break;
    default:
        r = snprintf(buf+i, n-i, " (no class)");
        break;
    }
    if (i+r <= n) {
        i += r;
    }

    return 0;
}

#define MAX_PVFS_STRERROR_LEN 256

/** PVFS_perror()
 *
 * prints a message on stderr, consisting of text argument followed by
 * a colon, space, and error string for the given retcode.
 */
void PVFS_perror(const char *text, int retcode)
{
    char buf[MAX_PVFS_STRERROR_LEN];
    /* PVFS_strerror_r does not fail. */
    PVFS_strerror_r(retcode, buf, MAX_PVFS_STRERROR_LEN);
    fprintf(stderr, "%s: %s\n", text, buf);
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
    char buf[MAX_PVFS_STRERROR_LEN];
    if (!pvfs_perror_gossip_silent)
    {
        /* PVFS_strerror_r does not fail. */
        PVFS_strerror_r(retcode, buf, MAX_PVFS_STRERROR_LEN);
        gossip_err("%s: %s\n", text, buf);
    }
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
