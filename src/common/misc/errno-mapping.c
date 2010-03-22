/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pvfs2-util.h"
#include "gossip.h"

#define MAX_PVFS_STRERROR_LEN 256

/* macro defined in include/pvfs2-types.h */
DECLARE_ERRNO_MAPPING_AND_FN();

/*
  the pvfs analog to strerror_r that handles PVFS_error codes as well
  as errno error codes
*/
int PVFS_strerror_r(int errnum, char *buf, int n)
{
    int ret = 0;
    int limit = PVFS_util_min(n, MAX_PVFS_STRERROR_LEN);
    int tmp = PVFS_get_errno_mapping(-errnum);

    if (IS_PVFS_NON_ERRNO_ERROR(-errnum))
    {
        snprintf(buf, limit, "%s", PINT_non_errno_strerror_mapping[tmp]);
    }
    else
    {
#ifdef HAVE_GNU_STRERROR_R
        char *tmpbuf = strerror_r(tmp, buf, limit);
        if (tmpbuf && (strcmp(tmpbuf, buf)))
        {
            limit = PVFS_util_min(limit, strlen(tmpbuf)+1);
            strncpy(buf, tmpbuf, (size_t)limit);
        }
        ret = (tmpbuf ? 0 : -1);
#else
	ret = strerror_r(tmp, buf, (size_t)limit);
#endif
    }
    return ret;
}

/* PVFS_perror()
 *
 * prints a message on stderr, consisting of text argument followed by
 * a colon, space, and error string for the given retcode.  NOTE: also
 * prints a warning if the error code is not in a pvfs2 format and
 * assumes errno
 */
void PVFS_perror(const char *text, int retcode)
{
    if (IS_PVFS_NON_ERRNO_ERROR(-retcode))
    {
        char buf[MAX_PVFS_STRERROR_LEN] = {0};
        int index = PVFS_get_errno_mapping(-retcode);

        snprintf(buf,MAX_PVFS_STRERROR_LEN,"%s: %s (error class: %d)\n",text,
                 PINT_non_errno_strerror_mapping[index], PVFS_ERROR_CLASS(-retcode));
        fprintf(stderr, "%s", buf);
    }
    else if (IS_PVFS_ERROR(-retcode))
    {
	fprintf(stderr, "%s: %s (error class: %d)\n", text,
	strerror(PVFS_ERROR_TO_ERRNO(-retcode)),
        PVFS_ERROR_CLASS(-retcode));
    }
    else
    {
	fprintf(stderr, "Warning: non PVFS2 error code (%d):\n",
                -retcode);
	fprintf(stderr, "%s: %s\n", text, strerror(-retcode));
    }
    return;
}

/* PVFS_perror_gossip()
 *
 * same as PVFS_perror, except that the output is routed through
 * gossip rather than stderr
 */
void PVFS_perror_gossip(const char *text, int retcode)
{
    if (IS_PVFS_NON_ERRNO_ERROR(-retcode))
    {
        char buf[MAX_PVFS_STRERROR_LEN] = {0};
        int index = PVFS_get_errno_mapping(-retcode);

        snprintf(buf,MAX_PVFS_STRERROR_LEN,"%s: %s\n",text,
                 PINT_non_errno_strerror_mapping[index]);
	gossip_err("%s", buf);
    }
    else if (IS_PVFS_ERROR(-retcode))
    {
	gossip_err("%s: %s\n", text,
                   strerror(PVFS_ERROR_TO_ERRNO(-retcode)));
    }
    else
    {
	gossip_err("Warning: non PVFS2 error code (%d):\n", -retcode);
	gossip_err("%s: %s\n", text, strerror(-retcode));
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
