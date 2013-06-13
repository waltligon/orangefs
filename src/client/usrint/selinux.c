/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup usrint
 *
 *  PVFS2 user interface routines - implementation of stdio for pvfs
 */
/* this prevents headers from using inlines for 64 bit calls */

#define USRINT_SOURCE 1
#include "usrint.h"

/* if no header file we assume no selinux */
#ifdef HAVE_SELINUX_H

#ifdef getfscreatecon
#undef getfscreatecon
#endif

#ifdef getfilecon
#undef getfilecon
#endif

#ifdef lgetfilecon
#undef lgetfilecon
#endif

#ifdef fgetfilecon
#undef fgetfilecon
#endif

#ifdef setfscreatecon
#undef setfscreatecon
#endif

#ifdef setfilecon
#undef setfilecon
#endif

#ifdef lsetfilecon
#undef lsetfilecon
#endif

#ifdef fsetfilecon
#undef fsetfilecon
#endif

/* for the moment these are not supported but eventually we need to
 * decide if this is PVFS or not and either return not supported or call
 * the real version of this for regular files
 */

int getfscreatecon(security_context_t *con)
{
    errno = ENOTSUP;
    return -1;
}

int getfilecon(const char *path, security_context_t *con)
{
    errno = ENOTSUP;
    return -1;
}

int lgetfilecon(const char *path, security_context_t *con)
{
    errno = ENOTSUP;
    return -1;
}

int fgetfilecon(int fd, security_context_t *con)
{
    errno = ENOTSUP;
    return -1;
}

int setfscreatecon(security_context_t con)
{
    errno = ENOTSUP;
    return -1;
}

int setfilecon(const char *path, security_context_t con)
{
    errno = ENOTSUP;
    return -1;
}

int lsetfilecon(const char *path, security_context_t con)
{
    errno = ENOTSUP;
    return -1;
}

int fsetfilecon(int fd, security_context_t con)
{
    errno = ENOTSUP;
    return -1;
}

#endif /* HAVE_SELINUX_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
