/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */


/* 
 * These are the exported functions from the sockio library.  They
 * provide a simple intuitive interface to the TCP/IP sockets API.
 */

/*
 * Defines which may be set at compile time to determine functionality:
 *
 * __USE_SENDFILE__ turns on the use of sendfile() in the library and
 * makes the nbsendfile function available to the application.
 * Older glibc systems do not have this functionality so we leave it to
 * be turned on manually.
 *
 * BRAINDEADSOCKS can be defined to prevent the macros that set socket
 * buffer sizes from having an effect.  On some kernels (such as Linux
 * 1.3) setting socket buffer sizes can cause problems.
 *
 * BSEND_NO_WRITEV determines whether the bsendv (blocking vector send)
 * function will use the writev system call or not.
 */

#ifndef SOCKIO_H
#define SOCKIO_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>

int new_sock(void);
int bind_sock(int,
	      int);
int connect_sock(int,
		 char *,
		 int);
int init_sock(struct sockaddr *,
	      char *,
	      int);
int brecv(int s,
	  void *buf,
	  int len);
int nbrecv(int s,
	   void *buf,
	   int len);
int bsend(int s,
	  void *buf,
	  int len);
int bsendv(int s,
	   const struct iovec *vec,
	   int cnt);
int nbsend(int s,
	   void *buf,
	   int len);
int get_sockopt(int s,
		int optname);
int set_tcpopt(int s,
	       int optname,
	       int val);
int set_sockopt(int s,
		int optname,
		int size);
int set_socktime(int s,
		 int optname,
		 int size);
int sockio_dump_sockaddr(struct sockaddr_in *ptr,
			 FILE * fp);
int brecv_timeout(int s,
		  void *buf,
		  int len,
		  int timeout);
int connect_timeout(int s,
		    struct sockaddr *saddrp,
		    int len,
		    int time_secs);
int nbpeek(int s,
	   void* buf,
	   int len);
#ifdef __USE_SENDFILE__
int nbsendfile(int s,
	       int f,
	       int off,
	       int len);
#endif

#define GET_RECVBUFSIZE(s) get_sockopt(s, SO_RCVBUF)
#define GET_SENDBUFSIZE(s) get_sockopt(s, SO_SNDBUF)

/* some OS's (ie. Linux 1.3.xx) can't handle buffer sizes of certain
 * sizes, and will hang up
 */
#ifdef BRAINDEADSOCKS
/* setting socket buffer sizes can do bad things */
#define SET_RECVBUFSIZE(s, size)
#define SET_SENDBUFSIZE(s, size)
#else
#define SET_RECVBUFSIZE(s, size) set_sockopt(s, SO_RCVBUF, size)
#define SET_SENDBUFSIZE(s, size) set_sockopt(s, SO_SNDBUF, size)
#endif

#define GET_MINSENDSIZE(s) get_sockopt(s, SO_SNDLOWAT)
#define GET_MINRECVSIZE(s) get_sockopt(s, SO_RCVLOWAT)
#define SET_MINSENDSIZE(s, size) set_sockopt(s, SO_SNDLOWAT, size)
#define SET_MINRECVSIZE(s, size) set_sockopt(s, SO_RCVLOWAT, size)

/* BLOCKING / NONBLOCKING MACROS */

#define SET_NONBLOCK(x_fd) fcntl((x_fd), F_SETFL, O_NONBLOCK | \
   fcntl((x_fd), F_GETFL, 0))

#define SET_NONBLOCK_AND_SIGIO(x_fd) \
do { \
	fcntl((x_fd), F_SETOWN, getpid()); \
	fcntl((x_fd), F_SETFL, FASYNC | O_NONBLOCK | fcntl((x_fd), F_GETFL, 0)); \
} while (0)

#define CLR_NONBLOCK(x_fd) fcntl((x_fd), F_SETFL, fcntl((x_fd), F_GETFL, 0) & \
   (~O_NONBLOCK))


#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
