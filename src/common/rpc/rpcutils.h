/*
 * (C) 2006 The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef _RPCUTILS_H
#define _RPCUTILS_H

#include <stdio.h>
#include <rpc/rpc.h>
#include <rpc/svc.h>
#include <rpc/clnt.h>
#include <sys/types.h>

struct svc_info {
	/* passed by the caller */
	int       use_thread;
	/* filled in by the RPC utils layer */
	pthread_t tid;
	int       svc_tcp_prog;
	int		 svc_udp_prog;
	int       svc_vers;
	int       svc_tcp_sock;
	int       svc_udp_sock;
};

extern int register_svc(int *sockp, int proto, int *port, int *prog_num, int version);
extern CLIENT *get_svc_handle(char *hostname, int prog_number, int version, int protocol, int timeout, struct sockaddr *);
extern int get_program_info(char *hostname, int protocol, int port, int *version);
extern int setup_service(int prog, int vers, int proto, int port,
		void (*handle_fn)(struct svc_req *, SVCXPRT *), struct svc_info *info);
extern int cleanup_service(struct svc_info *info);

#endif
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
