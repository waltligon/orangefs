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
#include <usrint.h>
#include <linux/dirent.h>
#include <posix-ops.h>
#include <posix-pvfs.h>
#include <openfile-util.h>

/*
 * SOCKET SYSTEM CALLS
 */

int socket (int domain, int type, int protocol)
{   
    int sockfd;
    pvfs_descriptor *pd;

    sockfd = glibc_ops.socket(domain, type, protocol);
    if (sockfd < 0)
    {
        return sockfd;
    }
    pd = pvfs_alloc_descriptor(&glibc_ops);
    pd->is_in_use = PVFS_FS;
    pd->true_fd = sockfd;
    return pd->fd;
}

int accept (int sockfd, struct sockaddr *addr, socklen_t *alen)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (pd)
    {
        rc = pd->fsops->accept(pd->true_fd, addr, alen);
    }
    else
    {
        rc = -1;
        errno = EBADF;
    }
    return rc;
}

int bind (int sockfd, const struct sockaddr *addr, socklen_t alen)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (pd)
    {
        rc = pd->fsops->bind(pd->true_fd, addr, alen);
    }
    else
    {
        rc = -1;
        errno = EBADF;
    }
    return rc;
}

int connect (int sockfd, const struct sockaddr *addr, socklen_t alen)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (pd)
    {
        rc = pd->fsops->connect(pd->true_fd, addr, alen);
    }
    else
    {
        rc = -1;
        errno = EBADF;
    }
    return rc;
}

int getpeername (int sockfd, struct sockaddr *addr, socklen_t *alen)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (pd)
    {
        rc = pd->fsops->getpeername(pd->true_fd, addr, alen);
    }
    else
    {
        rc = -1;
        errno = EBADF;
    }
    return rc;
}

int getsockname (int sockfd, struct sockaddr *addr, socklen_t *alen)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (pd)
    {
        rc = pd->fsops->getsockname(pd->true_fd, addr, alen);
    }
    else
    {
        rc = -1;
        errno = EBADF;
    }
    return rc;
}

int getsockopt (int sockfd, int lvl, int oname,
                  void *oval, socklen_t *olen)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (pd)
    {
        rc = pd->fsops->getsockopt(pd->true_fd, lvl, oname, oval, olen);
    }
    else
    {
        rc = -1;
        errno = EBADF;
    }
    return rc;
}

int setsockopt (int sockfd, int lvl, int oname,
                  const void *oval, socklen_t olen)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (pd)
    {
        rc = pd->fsops->setsockopt(pd->true_fd, lvl, oname, oval, olen);
    }
    else
    {
        rc = -1;
        errno = EBADF;
    }
    return rc;
}

int ioctl (int fd, int request, ...)
{
    int rc;
    pvfs_descriptor *pd;
    va_list ap;

    va_start(ap, request);
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->ioctl(pd->true_fd, request, ap);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    va_end(ap);
    return rc;
}

int listen (int sockfd, int backlog)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (pd)
    {
        rc = pd->fsops->listen(pd->true_fd, backlog);
    }
    else
    {
        rc = -1;
        errno = EBADF;
    }
    return rc;
}

int recv (int sockfd, void *buf, size_t len, int flags)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (pd)
    {
        rc = pd->fsops->recv(pd->true_fd, buf, len, flags);
    }
    else
    {
        rc = -1;
        errno = EBADF;
    }
    return rc;
}

int recvfrom (int sockfd, void *buf, size_t len, int flags,
                struct sockaddr *addr, socklen_t *alen)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (pd)
    {
        rc = pd->fsops->recvfrom(pd->true_fd, buf, len, flags, addr, alen);
    }
    else
    {
        rc = -1;
        errno = EBADF;
    }
    return rc;
}

int recvmsg (int sockfd, struct msghdr *msg, int flags)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (pd)
    {
        rc = pd->fsops->recvmsg(pd->true_fd, msg, flags);
    }
    else
    {
        rc = -1;
        errno = EBADF;
    }
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
    if (pd)
    {
        rc = pd->fsops->send(pd->true_fd, buf, len, flags);
    }
    else
    {
        rc = -1;
        errno = EBADF;
    }
    return rc;
}

int sendto (int sockfd, const void *buf, size_t len, int flags,
            const struct sockaddr *addr, socklen_t alen)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (pd)
    {
        rc = pd->fsops->sendto(pd->true_fd, buf, len, flags, addr, alen);
    }
    else
    {
        rc = -1;
        errno = EBADF;
    }
    return rc;
}

int sendmsg (int sockfd, const struct msghdr *msg, int flags)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (pd)
    {
        rc = pd->fsops->sendmsg(pd->true_fd, msg, flags);
    }
    else
    {
        rc = -1;
        errno = EBADF;
    }
    return rc;
}

int shutdown (int sockfd, int how)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(sockfd);
    if (pd)
    {
        rc = pd->fsops->shutdown(pd->true_fd, how);
    }
    else
    {
        rc = -1;
        errno = EBADF;
    }
    return rc;
}

int socketpair (int d, int type, int protocol, int sv[2])
{
    int rc;
    pvfs_descriptor *pd;
    rc = glibc_ops.socketpair(d, type, protocol, sv);
    if (rc < 0)
    {
        return rc;
    }
    pd = pvfs_alloc_descriptor(&glibc_ops);
    pd->is_in_use = PVFS_FS;
    pd->true_fd = sv[0];
    sv[0] = pd->fd;
    pd = pvfs_alloc_descriptor(&glibc_ops);
    pd->is_in_use = PVFS_FS;
    pd->true_fd = sv[1];
    sv[1] = pd->fd;
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
    f0 = pvfs_alloc_descriptor(&glibc_ops);
    if (!f0)
    {
        goto errorout;
    }
    f1 = pvfs_alloc_descriptor(&glibc_ops);
    if (!f1)
    {
        pvfs_free_descriptor(f0);
        errno = EMFILE;
        rc = -1;
        goto errorout;
    }
    f0->true_fd = fa[0];
    filedes[0] = f0->fd;
    f1->true_fd = fa[1];
    filedes[1] = f1->fd;
    /* need to set mode and stuff appropriately */
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

