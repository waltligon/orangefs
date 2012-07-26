/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup usrint
 *
 *  PVFS2 user interface routines - wrappers for posix socket system calls
 */
#define USRINT_SOURCE 1
#include "usrint.h"
#include <sys/syscall.h>
#include "posix-ops.h"
#include "posix-pvfs.h"
#include "openfile-util.h"

/*
 * SOCKET SYSTEM CALLS
 */

int socket (int domain, int type, int protocol)
{   
    int sockfd;
    pvfs_descriptor *pd;

    /* sockfd = glibc_ops.socket(domain, type, protocol); */
    sockfd = syscall(SYS_socketcall, domain, type, protocol);
    if (sockfd < 0)
    {
        return sockfd;
    }
    pd = pvfs_alloc_descriptor(&glibc_ops, sockfd, NULL, 0);
    pd->mode |= S_IFSOCK;
    return pd->fd;
}

int accept (int sockfd, struct sockaddr *addr, socklen_t *alen)
{
    int rc = 0, fd;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (!pd)
    {
        errno = EBADF;
        rc = -1;
        goto errorout;
    }
    if (!S_ISSOCK(pd->mode))
    {
        errno = ENOTSOCK;
        rc = -1;
        goto errorout;
    }
    fd = pd->fsops->accept(pd->true_fd, addr, alen);
    if (fd < 0)
    {
        rc = -1;
        goto errorout;
    }
    pd = pvfs_alloc_descriptor(&glibc_ops, fd , NULL, 0);
    pd->mode |= S_IFSOCK;
    rc = fd;   
errorout:
    return rc;
}

int bind (int sockfd, const struct sockaddr *addr, socklen_t alen)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (!pd)
    {
        errno = EBADF;
        rc = -1;
        goto errorout;
    }
    if (!S_ISSOCK(pd->mode))
    {
        errno = ENOTSOCK;
        rc = -1;
        goto errorout;
    }
    rc = pd->fsops->bind(pd->true_fd, addr, alen);
errorout:
    return rc;
}

int connect (int sockfd, const struct sockaddr *addr, socklen_t alen)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (!pd)
    {
        errno = EBADF;
        rc = -1;
        goto errorout;
    }
    if (!S_ISSOCK(pd->mode))
    {
        errno = ENOTSOCK;
        rc = -1;
        goto errorout;
    }
    rc = pd->fsops->connect(pd->true_fd, addr, alen);
errorout:
    return rc;
}

int getpeername (int sockfd, struct sockaddr *addr, socklen_t *alen)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (!pd)
    {
        errno = EBADF;
        rc = -1;
        goto errorout;
    }
    if (!S_ISSOCK(pd->mode))
    {
        errno = ENOTSOCK;
        rc = -1;
        goto errorout;
    }
    rc = pd->fsops->getpeername(pd->true_fd, addr, alen);
errorout:
    return rc;
}

int getsockname (int sockfd, struct sockaddr *addr, socklen_t *alen)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (!pd)
    {
        errno = EBADF;
        rc = -1;
        goto errorout;
    }
    if (!S_ISSOCK(pd->mode))
    {
        errno = ENOTSOCK;
        rc = -1;
        goto errorout;
    }
    rc = pd->fsops->getsockname(pd->true_fd, addr, alen);
errorout:
    return rc;
}

int getsockopt (int sockfd, int lvl, int oname,
                  void *oval, socklen_t *olen)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (!pd)
    {
        errno = EBADF;
        rc = -1;
        goto errorout;
    }
    if (!S_ISSOCK(pd->mode))
    {
        errno = ENOTSOCK;
        rc = -1;
        goto errorout;
    }
    rc = pd->fsops->getsockopt(pd->true_fd, lvl, oname, oval, olen);
errorout:
    return rc;
}

int setsockopt (int sockfd, int lvl, int oname,
                  const void *oval, socklen_t olen)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (!pd)
    {
        errno = EBADF;
        rc = -1;
        goto errorout;
    }
    if (!S_ISSOCK(pd->mode))
    {
        errno = ENOTSOCK;
        rc = -1;
        goto errorout;
    }
    rc = pd->fsops->setsockopt(pd->true_fd, lvl, oname, oval, olen);
errorout:
    return rc;
}

int ioctl (int fd, int request, ...)
{
    int rc;
    pvfs_descriptor *pd;
    va_list ap;

    va_start(ap, request);
    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        errno = EBADF;
        rc = -1;
        goto errorout;
    }
    if (!S_ISSOCK(pd->mode))
    {
        errno = ENOTSOCK;
        rc = -1;
        goto errorout;
    }
    rc = pd->fsops->ioctl(pd->true_fd, request, ap);
    va_end(ap);
errorout:
    return rc;
}

int listen (int sockfd, int backlog)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (!pd)
    {
        errno = EBADF;
        rc = -1;
        goto errorout;
    }
    if (!S_ISSOCK(pd->mode))
    {
        errno = ENOTSOCK;
        rc = -1;
        goto errorout;
    }
    rc = pd->fsops->listen(pd->true_fd, backlog);
errorout:
    return rc;
}

int recv (int sockfd, void *buf, size_t len, int flags)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (!pd)
    {
        errno = EBADF;
        rc = -1;
        goto errorout;
    }
    if (!S_ISSOCK(pd->mode))
    {
        errno = ENOTSOCK;
        rc = -1;
        goto errorout;
    }
    rc = pd->fsops->recv(pd->true_fd, buf, len, flags);
errorout:
    return rc;
}

int recvfrom (int sockfd, void *buf, size_t len, int flags,
                struct sockaddr *addr, socklen_t *alen)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (!pd)
    {
        errno = EBADF;
        rc = -1;
        goto errorout;
    }
    if (!S_ISSOCK(pd->mode))
    {
        errno = ENOTSOCK;
        rc = -1;
        goto errorout;
    }
    rc = pd->fsops->recvfrom(pd->true_fd, buf, len, flags, addr, alen);
errorout:
    return rc;
}

int recvmsg (int sockfd, struct msghdr *msg, int flags)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (!pd)
    {
        errno = EBADF;
        rc = -1;
        goto errorout;
    }
    if (!S_ISSOCK(pd->mode))
    {
        errno = ENOTSOCK;
        rc = -1;
        goto errorout;
    }
    rc = pd->fsops->recvmsg(pd->true_fd, msg, flags);
errorout:
    return rc;
}

/* int select (int nfds, fd_set *rfds, fd_set *wfds, fd_set *efds,
              struct timeval *timeout); */
/* void FD_CLR (int fd, fd_set *set) */
/* void FD_ISSET (int fd, fd_set *set) */
/* void FD_SET (int fd, fd_set *set) */
/* void FD_ZERO (fd_set *set); */
/* int pselect (int nfds, fd_set *rfds, fd_set *wfds, fd_set *efds,
               const struct timeval *timeout, const sigset_t *sigmask); */

int send (int sockfd, const void *buf, size_t len, int flags)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (!pd)
    {
        errno = EBADF;
        rc = -1;
        goto errorout;
    }
    if (!S_ISSOCK(pd->mode))
    {
        errno = ENOTSOCK;
        rc = -1;
        goto errorout;
    }
    rc = pd->fsops->send(pd->true_fd, buf, len, flags);
errorout:
    return rc;
}

int sendto (int sockfd, const void *buf, size_t len, int flags,
            const struct sockaddr *addr, socklen_t alen)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (!pd)
    {
        errno = EBADF;
        rc = -1;
        goto errorout;
    }
    if (!S_ISSOCK(pd->mode))
    {
        errno = ENOTSOCK;
        rc = -1;
        goto errorout;
    }
    rc = pd->fsops->sendto(pd->true_fd, buf, len, flags, addr, alen);
errorout:
    return rc;
}

int sendmsg (int sockfd, const struct msghdr *msg, int flags)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (!pd)
    {
        errno = EBADF;
        rc = -1;
        goto errorout;
    }
    if (!S_ISSOCK(pd->mode))
    {
        errno = ENOTSOCK;
        rc = -1;
        goto errorout;
    }
    rc = pd->fsops->sendmsg(pd->true_fd, msg, flags);
errorout:
    return rc;
}

int shutdown (int sockfd, int how)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (!pd)
    {
        errno = EBADF;
        rc = -1;
        goto errorout;
    }
    if (!S_ISSOCK(pd->mode))
    {
        errno = ENOTSOCK;
        rc = -1;
        goto errorout;
    }
    rc = pd->fsops->shutdown(pd->true_fd, how);
errorout:
    return rc;
}

int socketpair (int d, int type, int protocol, int sv[2])
{
    int rc = 0;
    pvfs_descriptor *pd0, *pd1;
    rc = glibc_ops.socketpair(d, type, protocol, sv);
    if (rc < 0)
    {
        goto errorout;
    }
    pd0 = pvfs_alloc_descriptor(&glibc_ops, sv[0], NULL, 0);
    if (!pd0)
    {
        goto errorout;
    }
    pd1 = pvfs_alloc_descriptor(&glibc_ops, sv[1], NULL, 0);
    if (!pd1)
    {
        pvfs_free_descriptor(pd0->fd);
        errno = EMFILE;
        rc = -1;
        goto errorout;
    }
    pd0->mode |= S_IFSOCK;
    pd1->mode |= S_IFSOCK;
    sv[0] = pd0->true_fd;
    sv[1] = pd1->true_fd;
errorout:
    return rc;
}

int pipe(int filedes[2])
{
    int rc = 0;
    pvfs_descriptor *f0, *f1;
    int fa[2];
    if(!filedes)
    {
        errno = EFAULT;
        rc = -1;
        goto errorout;
    }   
    rc = glibc_ops.pipe(fa);
    if (rc < 0)
    {
        goto errorout;
    }
    f0 = pvfs_alloc_descriptor(&glibc_ops, fa[0], NULL, 0);
    if (!f0)
    {
        goto errorout;
    }
    f1 = pvfs_alloc_descriptor(&glibc_ops, fa[1], NULL, 0);
    if (!f1)
    {
        pvfs_free_descriptor(f0->fd);
        errno = EMFILE;
        rc = -1;
        goto errorout;
    }
    f0->mode |= S_IFSOCK;
    f1->mode |= S_IFSOCK;
    filedes[0] = f0->true_fd;
    filedes[1] = f1->true_fd;
errorout:
    return rc;
}

/*  
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End: 
 *      
 * vim: ts=8 sts=4 sw=4 expandtab
 */ 

