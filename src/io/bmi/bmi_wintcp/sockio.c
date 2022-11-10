/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-config.h"

#include <WinSock2.h>
/* #include <unistd.h> */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
/* #include <sys/socket.h> */
/* #include <sys/time.h> */
#include <fcntl.h>
/* #include <netinet/in.h>
#include <netinet/tcp.h>
*/
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
/* #include <sys/poll.h>
#include <sys/uio.h> */
#include <assert.h>

#include "sockio.h"
#include "gossip.h"

typedef unsigned int socklen_t;

/* if the platform provides a MSG_NOSIGNAL option (which disables the
 * generation of signals on broken pipe), then use it
 */
#ifdef MSG_NOSIGNAL
#define DEFAULT_MSG_FLAGS MSG_NOSIGNAL
#else
#define DEFAULT_MSG_FLAGS 0
#endif

SOCKET BMI_sockio_new_sock()
{
    return(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
}

int BMI_sockio_bind_sock(int sockd,
              int service)
{
    struct sockaddr_in saddr;

    memset((char *) &saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons((u_short) service);
    saddr.sin_addr.s_addr = INADDR_ANY;
  bind_sock_restart:
    if (bind(sockd, (struct sockaddr *) &saddr, sizeof(saddr)) < 0)
    {
        if (WSAGetLastError() == WSAEINTR)
            goto bind_sock_restart;
        return (-1);
    }
    return (sockd);
}

/* NOTE: this function returns BMI error codes */
int BMI_sockio_bind_sock_specific(int sockd,
              const char *name,
              int service)
{
    struct sockaddr saddr;
    int ret;

    if ((ret = BMI_sockio_init_sock(&saddr, name, service)) != 0)
        return (ret); /* converted to PVFS error code below */

  bind_sock_restart:
    if (bind(sockd, &saddr, sizeof(saddr)) < 0)
    {
        if (WSAGetLastError() == WSAEINTR)
            goto bind_sock_restart;
        return(bmi_errno_to_pvfs(-WSAGetLastError()));
    }
    return (sockd);
}


/* NOTE: this function returns BMI error codes */
int BMI_sockio_connect_sock(int sockd,
                 const char *name,
                 int service)
{
    struct sockaddr saddr;
    int ret;

    if ((ret = BMI_sockio_init_sock(&saddr, name, service)) != 0)
        return (ret);
    connect_sock_restart:
    if (connect(sockd, (struct sockaddr *) &saddr, sizeof(saddr)) < 0)
    {
        if (WSAGetLastError() == WSAEINTR)
            goto connect_sock_restart;
        return(bmi_errno_to_pvfs(-WSAGetLastError()));
    }
    return (sockd);
}

#ifdef HAVE_GETHOSTBYNAME
static int conv_h_errno(int herr)
{   
    switch (herr)
    {
    case WSAHOST_NOT_FOUND :
        return BMI_EHOSTNTFD;
    case WSANO_ADDRESS :
        return BMI_EADDRNTFD;
    case WSANO_RECOVERY :
        return BMI_ENORECVR;
    case WSATRY_AGAIN :
        return BMI_ETRYAGAIN;
    default :
        return herr;
    }
}

/* gethostbyname version */
int BMI_sockio_init_sock(struct sockaddr *saddrp,
                         const char *name,
                         int service)
{
    struct hostent *hep;

    memset((char *) saddrp, 0, sizeof(struct sockaddr_in));
    if (name == NULL)
    {
        if ((hep = gethostbyname("localhost")) == NULL)
        {
            return (-conv_h_errno(WSAGetLastError()));
        }
    }
    else if ((hep = gethostbyname(name)) == NULL)
    {
        return (-conv_h_errno(WSAGetLastError()));
    }
    ((struct sockaddr_in *) saddrp)->sin_family = AF_INET;
    ((struct sockaddr_in *) saddrp)->sin_port = htons((u_short) service);
    memcpy((char *) &(((struct sockaddr_in *) saddrp)->sin_addr), hep->h_addr, 
          hep->h_length);
    return (0);
}
#else
/* inet_aton version */
int BMI_sockio_init_sock(struct sockaddr *saddrp,
                         const char *name,
                         int service)
{
    int ret;
    struct in_addr addr;

    bzero((char *) saddrp, sizeof(struct sockaddr_in));
    if (name == NULL)
    {
        ret = inet_aton("127.0.0.1", &addr);
    }
    else
    {
        ret = inet_aton(name, &addr);
    }

    if (ret == 0) return -1;

    ((struct sockaddr_in *) saddrp)->sin_family = AF_INET;
    ((struct sockaddr_in *) saddrp)->sin_port = htons((u_short) service);
    memcpy((char *) &(((struct sockaddr_in *) saddrp)->sin_addr), &addr, 
          sizeof(addr));

    return 0;
}
#endif


/* nonblocking receive */
int BMI_sockio_nbrecv(int s,
           void *buf,
           int len)
{
    int ret, comp = len, err;

    /* We can't read the blocking state on Windows */
    /* assert(fcntl(s, F_GETFL, 0) & O_NONBLOCK); */

    while (comp)
    {
      nbrecv_restart:
        ret = recv(s, (char *) buf, comp, DEFAULT_MSG_FLAGS);
        err = WSAGetLastError();
        if (ret == 0) /* socket closed */
        {
            errno = EPIPE;
            return (-1);
        }
        if (ret == -1 && err == WSAEINTR) 
        {
            goto nbrecv_restart;
        }
        else if (ret == -1 && err == WSAEWOULDBLOCK)
        {
            /* return what we got so far, this is a nonblocking call */
            return(len-comp);
        }
        else if (ret == -1)
        {
            return (-1);
        }
        comp -= ret;
        buf = (char *)buf + ret;
    }
    return (len - comp);
}

/* BMI_sockio_nbpeek()
 *
 * performs a nonblocking check to see if the amount of data requested
 * is actually available in a socket.  Does not actually read the data
 * out.
 *
 * returns number of bytes available on succes, -1 on failure.
 */
int BMI_sockio_nbpeek(int s, void* buf, int len)
{
    int ret, err;
    
    /* We can't read the blocking state on Windows */
    /* assert(fcntl(s, F_GETFL, 0) & O_NONBLOCK); */

  nbpeek_restart:
    ret = recv(s, (char *) buf, len, (MSG_PEEK|DEFAULT_MSG_FLAGS));
    err = WSAGetLastError();
    if(ret == 0)
    {
        /* errno = EPIPE; */
        return (-1);
    }
    else if (ret == -1 && err == WSAEWOULDBLOCK)
    {
        return(0);
    }
    else if (ret == -1 && err == WSAEINTR)
    {
        goto nbpeek_restart;
    }
    else if (ret == -1)
    {
        return (-1);
    }
    
    return(ret);
}


/* nonblocking send */
/* should always return 0 when nothing gets done! */
int BMI_sockio_nbsend(int s,
           void *buf,
           int len)
{
    int ret, comp = len, err;

    while (comp)
    {
      nbsend_restart:
        ret = send(s, (char *) buf, comp, DEFAULT_MSG_FLAGS);
        err = WSAGetLastError();
        if (ret == 0 || (ret == -1 && err == WSAEWOULDBLOCK))
            return (len - comp);        /* return amount completed */
        if (ret == -1 && err == WSAEINTR)
        {
            goto nbsend_restart;
        }
        else if (ret == -1)
            return (-1);
        comp -= ret;
        buf = (char *)buf + ret;
    }
    return (len - comp);
}

/* nonblocking vector send */
int BMI_sockio_nbvector(int s,
            LPWSABUF vector,
            int count, 
            int recv_flag)
{
    int ret, err;
    DWORD bytes, flags;

    /* NOTE: this function is different from the others that will
     * keep making the I/O system call until EWOULDBLOCK is encountered; we 
     * give up after one call
     */

    /* loop over if interrupted */
    do
    {
        if (recv_flag)
        {
            /* ret = readv(s, vector, count); */
            flags = MSG_PARTIAL;
            ret = WSARecv(s, vector, count, &bytes, &flags, NULL, NULL);
            err = WSAGetLastError();
        }
        else
        {
            /* ret = writev(s, vector, count); */
            flags = 0;
            ret = WSASend(s, vector, count, &bytes, flags, NULL, NULL);
            err = WSAGetLastError();
        }
    } while ((ret == 0 && flags & MSG_PARTIAL) || (ret == -1 && err == WSAEINTR));

    /* return zero if can't do any work at all */
    if (ret == -1 && err == WSAEWOULDBLOCK)
        return(0);

    /* if data transferred or an error */
    return ret == -1 ? -1 : bytes;
}

#ifdef __USE_SENDFILE__
/* NBSENDFILE() - nonblocking (on the socket) send from file
 *
 * Here we are going to take advantage of the sendfile() call provided
 * in the linux 2.2 kernel to send from an open file directly (ie. w/out
 * explicitly reading into user space memory or memory mapping).
 *
 * We are going to set the non-block flag on the socket, but leave the
 * file as is.
 *
 * Boy, that type on the offset for sockfile() sure is lame, isn't it?
 * That's going to cause us some headaches when we want to do 64-bit
 * I/O...
 *
 * Returns -1 on error, amount of data written to socket on success.
 */
int BMI_sockio_nbsendfile(int s,
               int f,
               int off,
               int len)
{
    int ret, comp = len, myoff;

    while (comp)
    {
      nbsendfile_restart:
        myoff = off;
        ret = sendfile(s, f, &myoff, comp);
        if (ret == 0 || (ret == -1 && errno == EWOULDBLOCK))
            return (len - comp);        /* return amount completed */
        if (ret == -1 && errno == EINTR)
        {
            goto nbsendfile_restart;
        }
        else if (ret == -1)
            return (-1);
        comp -= ret;
        off += ret;
    }
    return (len - comp);
}
#endif

/* routines to get and set socket options */
int BMI_sockio_get_sockopt(int s,
                int optname)
{
    int val;
    socklen_t len = sizeof(val);

    if (getsockopt(s, SOL_SOCKET, optname, (char *) &val, (int *) &len) == -1)
        return (-1);
    else
        return (val);
}

int BMI_sockio_set_tcpopt(int s,
               int optname,
               int val)
{
    if (setsockopt(s, IPPROTO_TCP, optname, (char *) &val, sizeof(val)) == -1)
        return (-1);
    else
        return (val);
}

int BMI_sockio_set_sockopt(int s,
                int optname,
                int val)
{
    if (setsockopt(s, SOL_SOCKET, optname, (char *) &val, sizeof(val)) == -1)
        return (-1);
    else
        return (val);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
