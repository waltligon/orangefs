/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <sys/poll.h>
#include <sys/uio.h>

#include "sockio.h"

int new_sock()
{
    static int p_num = -1;	/* set to tcp protocol # on first call */
    struct protoent *pep;

    if (p_num == -1)
    {
	if ((pep = getprotobyname("tcp")) == NULL)
	{
	    perror("Kernel does not support tcp");
	    return (-1);
	}
	p_num = pep->p_proto;
    }
    return (socket(AF_INET, SOCK_STREAM, p_num));
}

int bind_sock(int sockd,
	      int service)
{
    struct sockaddr_in saddr;

    bzero((char *) &saddr, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons((u_short) service);
    saddr.sin_addr.s_addr = INADDR_ANY;
  bind_sock_restart:
    if (bind(sockd, (struct sockaddr *) &saddr, sizeof(saddr)) < 0)
    {
	if (errno == EINTR)
	    goto bind_sock_restart;
	return (-1);
    }
    return (sockd);
}

int connect_sock(int sockd,
		 char *name,
		 int service)
{
    struct sockaddr saddr;

    if (init_sock(&saddr, name, service) != 0)
	return (-1);
  connect_sock_restart:
    if (connect(sockd, (struct sockaddr *) &saddr, sizeof(saddr)) < 0)
    {
	if (errno == EINTR)
	    goto connect_sock_restart;
	return (-1);
    }
    return (sockd);
}

int init_sock(struct sockaddr *saddrp,
	      char *name,
	      int service)
{
    struct hostent *hep;

    bzero((char *) saddrp, sizeof(struct sockaddr_in));
    if (name == NULL)
    {
	if ((hep = gethostbyname("localhost")) == NULL)
	{
	    return (-1);
	}
    }
    else if ((hep = gethostbyname(name)) == NULL)
    {
	return (-1);
    }
    ((struct sockaddr_in *) saddrp)->sin_family = AF_INET;
    ((struct sockaddr_in *) saddrp)->sin_port = htons((u_short) service);
    bcopy(hep->h_addr, (char *) &(((struct sockaddr_in *) saddrp)->sin_addr),
	  hep->h_length);
    return (0);
}


/* blocking receive */
/* Returns -1 if it cannot get all len bytes
 * and the # of bytes received otherwise
 */
int brecv(int s,
	  void *buf,
	  int len)
{
    int oldfl, ret, comp = len;
    oldfl = fcntl(s, F_GETFL, 0);
    if (oldfl & O_NONBLOCK)
	fcntl(s, F_SETFL, oldfl & (~O_NONBLOCK));

    while (comp)
    {
      brecv_restart:
	if ((ret = recv(s, (char *) buf, comp, 0)) < 0)
	{
	    if (errno == EINTR)
		goto brecv_restart;
	    return (-1);
	}
	if (!ret)
	{
	    /* Note: this indicates a closed socket.  However, I don't
	     * like this behavior, so we're going to return -1 w/an EPIPE
	     * instead.
	     */
	    errno = EPIPE;
	    return (-1);
	}
	comp -= ret;
	buf += ret;
    }
    return (len - comp);
}

/* nonblocking receive */
int nbrecv(int s,
	   void *buf,
	   int len)
{
    int oldfl, ret, comp = len;

    oldfl = fcntl(s, F_GETFL, 0);
    if (!(oldfl & O_NONBLOCK))
	fcntl(s, F_SETFL, oldfl | O_NONBLOCK);

    while (comp)
    {
      nbrecv_restart:
	ret = recv(s, buf, comp, 0);
	if (!ret)	/* socket closed */
	{
	    errno = EPIPE;
	    return (-1);
	}
	if (ret == -1 && errno == EWOULDBLOCK)
	{
	    return (len - comp);	/* return amount completed */
	}
	if (ret == -1 && errno == EINTR)
	{
	    goto nbrecv_restart;
	}
	else if (ret == -1)
	{
	    return (-1);
	}
	comp -= ret;
	buf += ret;
    }
    return (len - comp);
}

/* nbpeek()
 *
 * performs a nonblocking check to see if the amount of data requested
 * is actually available in a socket.  Does not actually read the data
 * out.
 *
 * returns number of bytes available on succes, -1 on failure.
 */
int nbpeek(int s, void* buf, int len)
{
    int oldfl, ret, comp = len;

    oldfl = fcntl(s, F_GETFL, 0);
    if (!(oldfl & O_NONBLOCK))
	fcntl(s, F_SETFL, oldfl | O_NONBLOCK);

    while (comp)
    {
      nbpeek_restart:
	ret = recv(s, buf, comp, MSG_PEEK);
	if (!ret)	/* socket closed */
	{
	    errno = EPIPE;
	    return (-1);
	}
	if (ret == -1 && errno == EWOULDBLOCK)
	{
	    return (len - comp);	/* return amount completed */
	}
	if (ret == -1 && errno == EINTR)
	{
	    goto nbpeek_restart;
	}
	else if (ret == -1)
	{
	    return (-1);
	}
	comp -= ret;
    }
    return (len - comp);
}


/* blocking send */
int bsend(int s,
	  void *buf,
	  int len)
{
    int oldfl, ret, comp = len;
    oldfl = fcntl(s, F_GETFL, 0);
    if (oldfl & O_NONBLOCK)
	fcntl(s, F_SETFL, oldfl & (~O_NONBLOCK));

    while (comp)
    {
      bsend_restart:
	if ((ret = send(s, (char *) buf, comp, 0)) < 0)
	{
	    if (errno == EINTR)
		goto bsend_restart;
	    return (-1);
	}
	comp -= ret;
	buf += ret;
    }
    return (len - comp);
}


/* nonblocking send */
/* should always return 0 when nothing gets done! */
int nbsend(int s,
	   void *buf,
	   int len)
{
    int oldfl, ret, comp = len;
    oldfl = fcntl(s, F_GETFL, 0);
    if (!(oldfl & O_NONBLOCK))
	fcntl(s, F_SETFL, oldfl | O_NONBLOCK);

    while (comp)
    {
      nbsend_restart:
	ret = send(s, (char *) buf, comp, 0);
	if (ret == 0 || (ret == -1 && errno == EWOULDBLOCK))
	    return (len - comp);	/* return amount completed */
	if (ret == -1 && errno == EINTR)
	{
	    goto nbsend_restart;
	}
	else if (ret == -1)
	    return (-1);
	comp -= ret;
	buf += ret;
    }
    return (len - comp);
}

/* nonblocking vector send */
int nbvector(int s,
	    struct iovec* vector,
	    int count, 
	    int recv_flag)
{
    int oldfl, ret;
    oldfl = fcntl(s, F_GETFL, 0);
    if (!(oldfl & O_NONBLOCK))
	fcntl(s, F_SETFL, oldfl | O_NONBLOCK);

    /* NOTE: this function is different from the others that will
     * keep making the I/O system call until EWOULDBLOCK is encountered; we 
     * give up after one call
     */

    /* loop over if interrupted */
    do
    {
	if(recv_flag)
	{
	    ret = readv(s, vector, count);
	}
	else
	{
	    ret = writev(s, vector, count);
	}
    }while(ret == -1 && errno == EINTR);

    /* return zero if can't do any work at all */
    if(ret == -1 && errno == EWOULDBLOCK)
	return(0);

    /* if data transferred or an error */
    return(ret);
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
int nbsendfile(int s,
	       int f,
	       int off,
	       int len)
{
    int oldfl, ret, comp = len, myoff;

    oldfl = fcntl(s, F_GETFL, 0);
    if (!(oldfl & O_NONBLOCK))
	fcntl(s, F_SETFL, oldfl | O_NONBLOCK);

    while (comp)
    {
      nbsendfile_restart:
	myoff = off;
	ret = sendfile(s, f, &myoff, comp);
	if (ret == 0 || (ret == -1 && errno == EWOULDBLOCK))
	    return (len - comp);	/* return amount completed */
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
int get_sockopt(int s,
		int optname)
{
    int val, len = sizeof(val);
    if (getsockopt(s, SOL_SOCKET, optname, &val, &len) == -1)
	return (-1);
    else
	return (val);
}

int set_tcpopt(int s,
	       int optname,
	       int val)
{
    struct protoent* p = getprotobyname("tcp");
    if(!p)
	return(-1);
    if (setsockopt(s, p->p_proto, optname, &val, sizeof(val)) == -1)
	return (-1);
    else
	return (val);
}

int set_sockopt(int s,
		int optname,
		int val)
{
    if (setsockopt(s, SOL_SOCKET, optname, &val, sizeof(val)) == -1)
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
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
