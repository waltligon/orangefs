/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */


/* UNIX INCLUDE FILES */
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>	/* bzero and bcopy prototypes */
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

/* FUNCTIONS */
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

/* blocking vector send */
int bsendv(int s,
	   const struct iovec *vec,
	   int cnt)
{
    int tot, comp, ret, oldfl;
#ifdef BSENDV_NO_WRITEV
    struct iovec *cur = (struct iovec *) vec;
    char *buf;
#else
#endif

    oldfl = fcntl(s, F_GETFL, 0);
    if (oldfl & O_NONBLOCK)
	fcntl(s, F_SETFL, oldfl & (~O_NONBLOCK));

#ifdef BSENDV_NO_WRITEV
    for (tot = 0; cnt--; cur++)
    {
	buf = (char *) cur->iov_base;
	comp = cur->iov_len;
	while (comp)
	{
	  bsendv_restart:
	    if ((ret = send(s, buf, comp, 0)) < 0)
	    {
		if (errno == EINTR)
		    goto bsendv_restart;
		return (-1);
	    }
	    comp -= ret;
	    buf += ret;
	}
	tot += cur->iov_len;
    }
    return (tot);
#else

    for (comp = 0, tot = 0; comp < cnt; comp++)
	tot += vec[comp].iov_len;

    if ((ret = writev(s, vec, cnt)) < 0)
    {
      bsendv_restart:
	if (errno == EINTR)
	    goto bsendv_restart;
	return (-1);
    }
    return (ret);
#endif
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

int get_socktime(int s,
		 int optname)
{
    struct timeval val;
    int len = sizeof(val);
    if (getsockopt(s, SOL_SOCKET, optname, &val, &len) == -1)
	return (-1);
    else
	return (val.tv_sec * 1000000 + val.tv_usec);
}

int set_socktime(int s,
		 int optname,
		 int size)
{
    struct timeval val;
    val.tv_sec = size / 1000000;
    val.tv_usec = size % 1000000;
    if (setsockopt(s, SOL_SOCKET, optname, &val, sizeof(val)) == -1)
	return (-1);
    else
	return (size);
}

/* SOCKIO_DUMP_SOCKADDR() - dump info in a sockaddr structure
 *
 * Might or might not work for any given platform!
 */
int sockio_dump_sockaddr(struct sockaddr_in *ptr,
			 FILE * fp)
{
    int i;
    unsigned long int tmp;
    struct hostent *hp;
    char abuf[] = "xxx.xxx.xxx.xxx\0";

    fprintf(fp, "sin_family = %d\n", ptr->sin_family);
    fprintf(fp, "sin_port = %d (%d)\n", ptr->sin_port, ntohs(ptr->sin_port));
    /* print in_addr info */
    tmp = ptr->sin_addr.s_addr;
    sprintf(&abuf[0], "%d.%d.%d.%d", (int) (tmp & 0xff),
	    (int) ((tmp >> 8) & 0xff), (int) ((tmp >> 16) & 0xff),
	    (int) ((tmp >> 24) & 0xff));
    hp = gethostbyaddr((char *) &ptr->sin_addr.s_addr, sizeof(ptr->sin_addr),
		       ptr->sin_family);
    fprintf(fp, "sin_addr = %lx (%s = %s)\n",
	    (unsigned long) (ptr->sin_addr.s_addr), abuf, hp->h_name);
    for (i = 0;
	 i <
	 sizeof(struct sockaddr) - sizeof(short int) -
	 sizeof(unsigned short int) - sizeof(struct in_addr); i++)
    {
	fprintf(fp, "%x", ptr->sin_zero[i]);
    }
    fprintf(fp, "\n");
    return (0);
}	/* end of SOCKIO_DUMP_SOCKADDR() */


/* connect_timeout()
 *
 * Attempts to do nonblocking connect() until a timeout occurs.  Useful for
 * recovering from deadlocks...
 *
 * I apologize for all the goto's, but I hate to every syscall in a while()
 * just for the EINTR case!  
 */
int connect_timeout(int s,
		    struct sockaddr *saddrp,
		    int len,
		    int time_secs)
{
    struct pollfd pfds;
    int ret, oldfl, err, val, val_len;

    /* set our socket to nonblocking */
    oldfl = fcntl(s, F_GETFL, 0);
    if (!(oldfl & O_NONBLOCK))
	fcntl(s, F_SETFL, oldfl | O_NONBLOCK);

  connect_timeout_connect_restart:
    ret = connect(s, saddrp, len);
    if (ret < 0)
    {
	if (errno == EINTR)
	    goto connect_timeout_connect_restart;
	if (errno != EINPROGRESS)
	    goto connect_timeout_err;

	pfds.fd = s;
	pfds.events = POLLOUT;
      connect_timeout_poll_restart:
	ret = poll(&pfds, 1, time_secs * 1000);
	if (ret < 0)
	{
	    if (errno == EINTR)
		goto connect_timeout_poll_restart;
	    else
		goto connect_timeout_err;
	}
	if (ret == 0)
	{
	    /* timed out */
	    err = errno;
	    close(s);	/* don't want it to keep trying */
	    errno = err;
	    goto connect_timeout_err;
	}
    }

    /* _apparent_ success -- check if connect really completed */
    val_len = sizeof(val);
    ret = getsockopt(s, SOL_SOCKET, SO_ERROR, &val, &len);
    if (val != 0)
    {
	errno = val;
	goto connect_timeout_err;
    }

    /* success -- make socket blocking again and return */
    fcntl(s, F_SETFL, oldfl & (~O_NONBLOCK));
    return 0;

  connect_timeout_err:
    /* save errno, set flags on socket back to what they were, return -1 */
    err = errno;
    fcntl(s, F_SETFL, oldfl);
    errno = err;
    return -1;
}


/* brecv_timeout()
 *
 * Attempts to do nonblocking recv's until a timeout occurs.  Useful for
 * recovering from deadlocks in situations where you want to give up if a
 * message does not finish arriving within a certain time frame.
 *
 * Same args as brecv, but with an additional timeout argument that is an
 * integer number of seconds.   
 * 
 * returns number of bytes received, even if it did not recv all that was
 * asked.  If any error other than timeout occurs, it returns -1 and sets
 * errno accordingly.  */
int brecv_timeout(int s,
		  void *buf,
		  int len,
		  int timeout)
{

    int recv_size = len;	/* amt we hope for each iteration */
    int recv_total = 0;		/* total amt recv'd thus far */
    void *recv_ptr = buf;	/* where to put the data */
    int initial_tries = 4;	/* number of initial attempts to make */
    int i = 0;
    int ret = -1;
    struct pollfd poll_conn;

    /* This determines the backoff times.  It will do a few attempts with the
     * initial backoff first, then wait and do a final one after the
     * remainder of the time is up.  Note that the initial backoff will be
     * zero if a small overall timeout is specified by the caller.
     */
    int initial_backoff = timeout / initial_tries;
    int final_backoff = timeout - (timeout / initial_tries);

    /* we don't accept -1 for infinite timeout.  That is the job of brecv. */
    if (timeout < 0)
    {
	errno = EINVAL;
	return (-1);
    }

    /* initial attempts */
    do
    {
	/* nbrecv() handles EINTR on its own */
	if ((ret = nbrecv(s, recv_ptr, recv_size)) < 0)
	{
	    /* bail out at any error */
	    return (ret);
	}

	/* update our progress */
	recv_size -= ret;
	recv_total += ret;
	recv_ptr += ret;

	/* if we didn't finish, then poll for a while. */
	if (recv_total < len)
	{
	    poll_conn.fd = s;
	    poll_conn.events = POLLIN;
	  brecv_timeout_poll1_restart:
	    ret = poll(&poll_conn, 1, (initial_backoff * 1000));
	    if (ret < 0)
	    {
		if (errno == EINTR)
		    goto brecv_timeout_poll1_restart;
		/* bail out on any "real" error */
		return (ret);
	    }
	}
	i++;
    }
    while ((i < initial_tries) && (recv_total < len));

    /* see if we are done */
    if (recv_total == len)
    {
	return (len);
    }

    /* not done yet, give it one last chance: */
    poll_conn.fd = s;
    poll_conn.events = POLLIN;
  brecv_timeout_poll2_restart:
    ret = poll(&poll_conn, 1, (final_backoff * 1000));
    if (ret < 0)
    {
	if (errno == EINTR)
	    goto brecv_timeout_poll2_restart;
	/* bail out on any "real" error */
	return (ret);
    }

    if ((ret = nbrecv(s, recv_ptr, recv_size)) < 0)
    {
	return (ret);
    }

    recv_size -= ret;
    recv_total += ret;

    return (recv_total);
}

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * End:
 */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
