/*
 * (C) 2006 The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <rpc/rpc.h>
#include <rpc/svc.h>
#include <rpc/clnt.h>
#include <sys/types.h>
#include <errno.h>
#include <rpc/pmap_clnt.h>
#include <unistd.h>
#include <rpc/pmap_prot.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <semaphore.h>
#include "rpcutils.h"
#include "gossip.h"
#include "pvfs2-debug.h"

static int set_tcpopt(int s, int optname, int val)
{
#ifdef SOL_TCP
	if (setsockopt(s, SOL_TCP, optname, &val, sizeof(val)) == -1) return(-1);
	else return val;
#else
	return val;
#endif
}

static int set_sockopt(int s, int optname, int val)
{
	if (setsockopt(s, SOL_SOCKET, optname, &val, sizeof(val)) == -1)
		return(-1);
	else return(val);
}

/*
 * For a given 
 * <port, program number, version number, socket fd>
 * we can do one of the following things,
 * if port was ephemeral and program number is first available one,
 * else if port was ephemeral and program number is fixed,
 * else if port was fixed and program number is first available one,
 * else if port was fixed and program number was fixed as well.
 * sockp, port and prog_num are IN/OUT parameters.
 * They could be modified if they are set to -1.
 */
int register_svc(int *sockp, int proto, int *port, int *prog_num, int version)
{
	rpcprog_t available_range = 0x40000000, end_range = 0x60000000;
	int    s;
	struct sockaddr_in addr;
	socklen_t len;

	if (proto != IPPROTO_TCP && proto != IPPROTO_UDP) {
		gossip_err( "Invalid protocol type specified\n");
		errno = EINVAL;
		return -1;
	}
	/* create a new socket based on specified protocol */
	if (*sockp == RPC_ANYSOCK) {
		s = -1;
		if (proto == IPPROTO_TCP) {
			s = socket(AF_INET, SOCK_STREAM, 0);
		}
		else if (proto == IPPROTO_UDP) {
			s = socket(AF_INET, SOCK_DGRAM, 0);
		}
		if (s < 0) {
			gossip_err( "Socket create failure\n");
			*sockp = -1;
			return -1;
		}
	}
	else {
		s = *sockp;
	}
	/* For faster restart times, we allow reuse */
	set_sockopt(s, SO_REUSEADDR, 1);
	if (proto == IPPROTO_TCP)
	{
		if (set_tcpopt(s, TCP_NODELAY, 1) < 0)
		{
			gossip_err( "set_tcpopt on socket %d to kill Nagle failed %s\n",
					s, strerror(errno));
		}
	}
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	len = sizeof(addr);
	/* Bind it to a fixed port */
	if (*port >  0) {
		short p = (short) *port;
		addr.sin_port = htons(p);
	}
	/* bind failed. not good */
	if (bind(s, (struct sockaddr *) &addr, len) != 0) {
		gossip_err( "Bind to the specified port and/or ephemeral ports failed! %s\n", strerror(errno));
		if (*sockp == RPC_ANYSOCK) {
			close(s);
			*sockp = -1;
		}
		return -1;
	}
	/* If it was an ephemeral port we would need to return the port assigned */
	if (*port <= 0) {
		getsockname(s, (struct sockaddr *) &addr, &len);
		*port = ntohs(addr.sin_port);
	}
	/* Get any available program number */
	if (*prog_num < 0) {
		short p = (short) *port;

		do {
			if (pmap_set(available_range, version, proto, p))
				break;
			available_range++;
		} while (available_range <= end_range);

		/* failed attempt. no program number available! */
		if (available_range > end_range) {
			if (*sockp == RPC_ANYSOCK) {
				close(s);
				*sockp = -1;
			}
			/* can't close(s) because s was passed from upper layer */
			return -1;
		}
		else {
			*prog_num = available_range;
		}
	}
	/* use the specified one for registration with the portmapper */
	else {
		short p = (short) *port;

		/* Failed attempt */
		if (pmap_set(*prog_num, version, proto, p) == 0) {
			if (*sockp == RPC_ANYSOCK) {
				close(s);
				*sockp = -1;
			}
			/* can't close(s) because s was passed from upper layer */
			return -1;
		}
	}
	*sockp = s;
	return 0;
}

CLIENT *get_svc_handle(char *hostname, int prog_number, int version, int protocol, int timeout,
		struct sockaddr *my_address)
{
	CLIENT *handle;
	int s = RPC_ANYSOCK;
	struct sockaddr_in addr;
	struct hostent *hent;
	struct timeval tv;
	socklen_t len = sizeof(struct sockaddr);

	tv.tv_sec = timeout;
	memset(&addr, 0, sizeof(addr));
	hent = gethostbyname(hostname);
	if (hent == NULL) {
		herror("Could not lookup hostname\n");
		return NULL;
	}
	memcpy(&addr.sin_addr.s_addr, hent->h_addr_list[0], hent->h_length);
	addr.sin_family = AF_INET;
	addr.sin_port = 0;
	if (protocol == IPPROTO_UDP) {
		handle = clntudp_create(&addr, prog_number, version, tv, &s);
	}
	else {
		handle = clnttcp_create(&addr, prog_number, version, &s, 131071, 131071);
		if (handle && set_tcpopt(s, TCP_NODELAY, 1) < 0)
		{
			gossip_err( "set_tcpopt on socket %d to kill Nagle's algorithm failed %s\n",
					s, strerror(errno));
		}
	}
	/* get our own socket address */
	if (my_address)
	{
		getsockname(s, my_address, &len);
	}
	return handle;
}

int get_program_info(char *hostname, int protocol, int _port, int *version)
{
	struct sockaddr_in addr;
	struct hostent *hent = NULL;
	struct pmaplist *list = NULL;
	int prog_num = -1, flag = 0;
	short port = (short) _port;

	*version = -1;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	hent = gethostbyname(hostname);
	if (hent == NULL) {
		herror("Could not lookup hostname");
		errno = h_errno;
		return -1;
	}
	memcpy(&addr.sin_addr.s_addr, hent->h_addr_list[0], hent->h_length);
	addr.sin_port = htons(port);
	if ((list = pmap_getmaps(&addr)) == NULL) {
		gossip_err( "Could not get program number for the specified host\n");
		return -1;
	}
	do {
		struct pmap *map = &list->pml_map;

		if (map->pm_prot == protocol && map->pm_port == port) {
			if (flag == 0) {
				prog_num = map->pm_prog;
				*version = map->pm_vers;
				flag = 1;
			}
			else {
				/* We have a problem here */
				gossip_err( "Can we have different program numbers/version for a specific host/port/proto combination\n");
				return -1;
			}
		}
	} while ((list = list->pml_next) != NULL);
	return prog_num;
}

struct t_args {
	/* semaphore for synch. start/stop */
	sem_t ta_sem;
	/* thread identifier */
	int   ta_tid;
	/* exit status of the thread */
	int   ta_status;
	/* ta_prog == -1, means get the next available program # */
	int 	ta_prog;
	/* ta_vers cannot be -ve! It must be the version of the local rpc service */
	int   ta_vers;
	/* ta_proto == -1 means both IPPROTO_TCP & UDP, else it is one of the 2 */
	int   ta_proto;
	/* ta_port == -1 means any ephemeral port, else it is the specified one */
	int   ta_port;
	/* TCP program number */
	int   ta_tcp_prog;
	/* UDP program number */
	int   ta_udp_prog;
	/* TCP socket fd */
	int   ta_tcp_sock;
	/* UDP socket fd */
	int   ta_udp_sock;
	/* RPC service dispatch function */
	void (*ta_dispatch_fn)(struct svc_req *, SVCXPRT *);
	/* Information about the service */
	struct svc_info *ta_info;
};

static pthread_key_t key;
static pthread_once_t once = PTHREAD_ONCE_INIT;

/*
 * signal handler for the created service threads
 */
static void handle_signal(int sig_nr, siginfo_t *info, void *unused)
{
	struct t_args *args = (struct t_args *) pthread_getspecific(key);

	if (args) {
		/* De-register yourself from the portmapper if need be */
		if (args->ta_proto == -1 || args->ta_proto == IPPROTO_TCP) {
			svc_unregister(args->ta_tcp_prog, args->ta_vers);
			pmap_unset(args->ta_tcp_prog, args->ta_vers);
			close(args->ta_tcp_sock);
		}
		if (args->ta_tcp_prog != args->ta_udp_prog
				&& (args->ta_proto == -1 || args->ta_proto == IPPROTO_UDP)) {
			svc_unregister(args->ta_udp_prog, args->ta_vers);
			pmap_unset(args->ta_udp_prog, args->ta_vers);
			close(args->ta_udp_sock);
		}
		/* clear your tid and prepare for exit */
		args->ta_tid = 0;
		args->ta_status = -EINTR;
		sem_post(&args->ta_sem);
		free(args);
		pthread_setspecific(key, NULL);
		/* exit */
		pthread_exit(NULL);
	}
	return;
}

static void key_create(void)
{
	struct sigaction handler;
	/* This function can fail if more than 
	*  a certain number of keys are allocated
	*  by this proces, but there is no way to
	*  check this if this function gets called
	*  through pthread_once
	*/
	pthread_key_create(&key, NULL);

	memset(&handler, 0, sizeof(struct sigaction));
	handler.sa_sigaction = handle_signal;
	handler.sa_flags = SA_SIGINFO;
	/*
	 * sigaction can also fail! damn
	 */
	if (sigaction(SIGUSR1, &handler, NULL) != 0) {
		gossip_err( "Could not setup signal handler for SIGUSR1: %s\n", strerror(errno));
		exit(1);
	}
	return;
}

static int do_register(int proto, struct t_args *targs)
{
	int prog_number = 0, version_number, s;
	int port;
	register SVCXPRT *xprt = NULL;
	void (*dispatch_fn)(struct svc_req *, SVCXPRT *);

	s = RPC_ANYSOCK;
	prog_number = targs->ta_prog;
	version_number = targs->ta_vers;
	port = targs->ta_port;
	dispatch_fn = targs->ta_dispatch_fn;

	/* whoops! could not register with portmapper */
	if (register_svc(&s, proto, &port, &prog_number, version_number) < 0) {
		gossip_err( "Could not register with portmapper daemon!\n");
		targs->ta_tid = 0;
		targs->ta_status = -errno;
		return -1;
	}
	gossip_debug(GOSSIP_RPC_DEBUG, "Registered %s transport program number 0x%x "
			"version number %d on port %d using socket %d\n",
			(proto == IPPROTO_TCP) ? "TCP": "UDP",
			prog_number, version_number, port, s);
	/* Create a protocol specific transport handle */
	if (proto == IPPROTO_TCP) {
		/* 2nd and 3rd parameters are the socket buffer sizes. 0 means system defaults */
		if ((xprt = svctcp_create(s, 0, 0)) == NULL) {
			gossip_err( "Could not setup TCP xport handle: %s\n",
					strerror(errno));
			targs->ta_tid = 0;
			targs->ta_status = -errno;
			/* Deregister from the portmapper */
			pmap_unset(prog_number, version_number);
			/* Close the socket */
			close(s);
			return -1;
		}
	}
	else {
		if ((xprt = svcudp_create(s)) == NULL) {
			gossip_err( "Could not setup UDP xprt handle: %s\n", 
					strerror(errno));
			targs->ta_tid = 0;
			targs->ta_status = -errno;
			/* Deregister from the portmapper */
			pmap_unset(prog_number, version_number);
			/* Close the socket */
			close(s);
			return -1;
		}
	}
	/* Register the dispatch function, last parameter == 0 disables registration with the portmapper */
	if (svc_register(xprt, prog_number, version_number, dispatch_fn, 0) == 0) {
		gossip_err( "Could not register protocol %s service's dispatch function: %s\n", 
				(proto == IPPROTO_TCP) ? "tcp" : "udp", strerror(errno));
		svc_destroy(xprt);
		targs->ta_tid = 0;
		targs->ta_status = -errno;
		/* Deregister from the portmapper */
		pmap_unset(prog_number, version_number);
		close(s);
		return -1;
	}
	if (proto == IPPROTO_TCP) {
		targs->ta_info->svc_tcp_prog = targs->ta_tcp_prog = prog_number;
		targs->ta_info->svc_tcp_sock = targs->ta_tcp_sock = s;
	}
	else {
		targs->ta_info->svc_udp_prog = targs->ta_udp_prog = prog_number;
		targs->ta_info->svc_udp_sock = targs->ta_udp_sock = s;
	}
	return 0;
}

static void *
svc_func(void *_args)
{
	struct t_args *targs = (struct t_args *) _args;
	sigset_t set;
	int proto;

	targs->ta_tid = pthread_self();
	/* create a thread-specific key, only one person will be successful */
	pthread_once(&once, key_create);
	/* set the passed parameter as the tsd data */
	if (pthread_setspecific(key, targs) != 0) {
		gossip_err( "Could not set thread specific data\n");
		targs->ta_tid = 0;
		targs->ta_status = -errno;
		/* wake up the waiting thread */
		sem_post(&targs->ta_sem);
		pthread_exit(NULL);
	}
	/* ignore all signals but SIGUSR1 */
	sigfillset(&set);
	sigdelset(&set, SIGUSR1);
	if(pthread_sigmask(SIG_SETMASK, &set, NULL) != 0) {
		gossip_err( "Could not set thread's signal mask\n");
		targs->ta_tid = 0;
		targs->ta_status = -errno;
		/* wake up your parent */
		sem_post(&targs->ta_sem);
		pthread_exit(NULL);
	}
	proto = targs->ta_proto;
	/* Register the TCP mappings */
	if (proto == -1 || proto == IPPROTO_TCP) {
		if (do_register(IPPROTO_TCP, targs) < 0) {
			sem_post(&targs->ta_sem);
			pthread_exit(NULL);
		}
	}
	/* Repeat for UDP mappings as well if need be */
	if (proto == -1 || proto == IPPROTO_UDP) {
		if (do_register(IPPROTO_UDP, targs) < 0) {
			sem_post(&targs->ta_sem);
			pthread_exit(NULL);
		}
	}
	targs->ta_status = 0;
	/* wake your parent up */
	sem_post(&targs->ta_sem);
	/* Wait for RPC calls to be dispatched */
	svc_run();
	gossip_err( "svc_run() should not return!\n");
	return NULL;
}

/*
 * Create a thread that sets up a local RPC service with 
 * the dispatch function set to handle_fn, and wait for it
 * to set itself up before returning.
 */
int setup_service(int prog, int vers, int proto, int port,
		void (*handle_fn)(struct svc_req *, SVCXPRT *), struct svc_info *info)
{
	struct t_args *args;
	int error;

	if (handle_fn == NULL || info == NULL) {
		gossip_err( "Invalid address specified for tid"
				"and/or dispatch function and/or svc_info pointer\n");
		return -EFAULT;
	}
	if (vers < 0) {
		gossip_err( "-ve version number is not allowed!\n");
		return -EINVAL;
	}
	if (proto > 0 && (proto != IPPROTO_TCP && proto != IPPROTO_UDP)) {
		gossip_err( "Invalid/unhandled value of protocol %d\n", proto);
		return -EINVAL;
	}
	if (info->use_thread != 0 && info->use_thread != 1) {
		gossip_err( "Invalid value of use_thread = %d\n", info->use_thread);
		return -EINVAL;
	}
	args = (struct t_args *) calloc(1, sizeof(struct t_args));
	sem_init(&args->ta_sem, 0, 0);
	args->ta_status = 0;
	args->ta_prog = prog;
	args->ta_vers = vers;
	args->ta_proto = proto;
	args->ta_port = port;
	args->ta_dispatch_fn = handle_fn;
	args->ta_info = info;
	info->svc_vers = vers;
	/* spawn a thread if requested that will set itself up as the RPC service */
	if (info->use_thread == 1) {

		if ((error = pthread_create(&info->tid, NULL, svc_func, args)) != 0) {
			gossip_err( "Could not spawn service thread!\n");
			free(args);
			return -error;
		}
		/* wait for the service thread to register its RPC interface */
		sem_wait(&args->ta_sem);
		if (args->ta_status != 0) {
			error = args->ta_status;
			free(args);
			gossip_err( "Could not setup RPC service thread\n");
			return error;
		}
		/* args is freed by the thread on signal delivery during cleanup */
	}
	/* do not spawn a thread. Instead setup the RPC service and start waiting for them */
	else {
		if (proto == -1 || proto == IPPROTO_TCP) {
			if (do_register(IPPROTO_TCP, args) < 0) {
				error = args->ta_status;
				gossip_err( "Could not setup RPC service\n");
				free(args);
				return error;
			}
		}
		if (proto == -1 || proto == IPPROTO_UDP) {
			/* Repeat for the UDP mappings as well if need be */
			if (do_register(IPPROTO_UDP, args) < 0) {
				error = args->ta_status;
				gossip_err( "Could not setup RPC service\n");
				free(args);
				return error;
			}
		}
		free(args);
		/* start servicing RPC requests */
		svc_run();
		gossip_err( "Critical! svc_run() should never return\n");
	}
	return 0;
}

int cleanup_service(struct svc_info *info)
{
	int tid;

	if (info == NULL) {
		return -EFAULT;
	}
	/* cleanup only if we spawned a thread in the first place */
	if (info->use_thread == 1) {
		tid = info->tid;
		/* try to terminate the RPC service thread */
		if (pthread_kill(tid, SIGUSR1) == ESRCH) {
			return 0;
		}
		/* wait for it to exit */
		pthread_join(tid, NULL);
	}
	/* De-register yourself from the portmapper if need be */
	svc_unregister(info->svc_tcp_prog, info->svc_vers);
	pmap_unset(info->svc_tcp_prog, info->svc_vers);
	close(info->svc_tcp_sock);
	svc_unregister(info->svc_udp_prog, info->svc_vers);
	pmap_unset(info->svc_udp_prog, info->svc_vers);
	close(info->svc_udp_sock);
	return 0;
}

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
