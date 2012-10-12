/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


#ifndef SOCKIO_H
#define SOCKIO_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

int new_sock(void);
int bind_sock(int, int);
int connect_sock(int, char *, int);
int init_sock(struct sockaddr *, char *, int);
int brecv(int s, void *buf, int len);
int bvrecv(int s, const struct iovec *vec, int cnt);
int nbrecv(int s, void *buf, int len);
int brecv_timeout(int s, void *buf, int len, int timeout);
int bsend(int s, void *buf, int len);
int bsendv(int s, const struct iovec *vec, int cnt);
int nbsend(int s, void *buf, int len);
int get_sockopt(int s, int optname);
int set_tcpopt(int s, int optname, int val);
int set_sockopt(int s, int optname, int size);
int set_socktime(int s, int optname, int size);
int sockio_dump_sockaddr(struct sockaddr_in *ptr, FILE *fp);

int nbsendfile(int s, int f, off_t off, int len);

int connect_timeout(int s, struct sockaddr *saddrp, int len, int time_secs);
int nbpeek(int s, void* buf, int len);


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
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 
