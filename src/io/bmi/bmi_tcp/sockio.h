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
 * makes the BMI_sockio_nbsendfile function available to the application.
 * Older glibc systems do not have this functionality so we leave it to
 * be turned on manually.
 */

#ifndef SOCKIO_H
#define SOCKIO_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>

#include "bmi-types.h"

int BMI_sockio_new_sock(void);
int BMI_sockio_bind_sock(int,
			 int);
int BMI_sockio_bind_sock_specific(int sockd,
              const char *name,
	      int service);
int BMI_sockio_connect_sock(int,
			    const char *,
			    int);
int BMI_sockio_init_sock(struct sockaddr *,
			 const char *,
			 int);
int BMI_sockio_nbrecv(int s,
		      void *buf,
		      int len);
int BMI_sockio_nbsend(int s,
		      void *buf,
		      int len);
int BMI_sockio_nbvector(int s,
			struct iovec* vector,
			int count,
			int recv_flag);
int BMI_sockio_get_sockopt(int s,
			   int optname);
int BMI_sockio_set_tcpopt(int s,
			  int optname,
			  int val);
int BMI_sockio_set_sockopt(int s,
			   int optname,
			   int size);
int BMI_sockio_nbpeek(int s,
		      void* buf,
		      int len);
#ifdef __USE_SENDFILE__
int BMI_sockio_nbsendfile(int s,
			  int f,
			  int off,
			  int len);
#endif

#define GET_RECVBUFSIZE(s) BMI_sockio_get_sockopt(s, SO_RCVBUF)
#define GET_SENDBUFSIZE(s) BMI_sockio_get_sockopt(s, SO_SNDBUF)

/* some OS's (ie. Linux 1.3.xx) can't handle buffer sizes of certain
 * sizes, and will hang up
 */
#ifdef BRAINDEADSOCKS
/* setting socket buffer sizes can do bad things */
#define SET_RECVBUFSIZE(s, size)
#define SET_SENDBUFSIZE(s, size)
#else
#define SET_RECVBUFSIZE(s, size) BMI_sockio_set_sockopt(s, SO_RCVBUF, size)
#define SET_SENDBUFSIZE(s, size) BMI_sockio_set_sockopt(s, SO_SNDBUF, size)
#endif

#define GET_MINSENDSIZE(s) BMI_sockio_get_sockopt(s, SO_SNDLOWAT)
#define GET_MINRECVSIZE(s) BMI_sockio_get_sockopt(s, SO_RCVLOWAT)
#define SET_MINSENDSIZE(s, size) BMI_sockio_set_sockopt(s, SO_SNDLOWAT, size)
#define SET_MINRECVSIZE(s, size) BMI_sockio_set_sockopt(s, SO_RCVLOWAT, size)

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
 * vim: ts=8 sts=4 sw=4 expandtab
 */

