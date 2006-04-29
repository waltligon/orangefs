/*
 * (C) 2006 The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "vec_config.h"
#include "vec_prot.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <syslog.h>
#include <memory.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <rpc/pmap_clnt.h>

#include "gossip.h"
#include "pvfs2-debug.h"
#include "rpcutils.h"
#include "tp_proto.h"
#include "vec_grant_version.h"

#define _VEC_DISPATCH_FN(x) vec_mgr_ ## x
#define VEC_DISPATCH_FN(x)  _VEC_DISPATCH_FN(x)

extern void VEC_DISPATCH_FN(vec_v1)(struct svc_req *, register SVCXPRT *);

static int is_daemon   = 1;
/* Threads does not seem to work well with TCP-based RPC for some reason. Using UDP for now. */
static int use_tpool   = 1;
static int vec_port    = VEC_REQ_PORT; /* Although we use RPCs association with port is crucial */
static int num_threads = VEC_NUM_THREADS;

static pthread_attr_t attr;
static pthread_t  tid;

/* Structure below is defined in rpcutils.h */
static struct svc_info info = {
	use_thread: 0, /* it is important not to change this variable!!!! */
};
/* Thread pool identifier */
static tp_id id;
static void vec_cleanup(void);

static void usage(char *str)
{
	gossip_err( "Usage: %s -c (don't use a thread pool) -n <number of threads>"
			" -t <timeout> -d {daemonize or not} -p <port>\n", str);
	return;
}

/* Fatal signal handlers */
static void do_fatal_signal(int sig_nr, siginfo_t *info, void *unused)
{
   char buffer[1024];
   
   gossip_err("Received signal=[%d]\n", sig_nr);
   
   if (sig_nr == SIGSEGV)
   {
      struct rlimit rlim;

      gossip_err("Current working directory: [%s]\n", 
          getcwd(buffer, sizeof(buffer)));
      gossip_err("pid: [%d]\n", getpid());
      if (getrlimit(RLIMIT_CORE, &rlim))
      {
         gossip_err("getrlimit() failed\n");
      }
      else
      {
         gossip_err("rlim_cur (RLIMIT_CORE): [%Ld]\n", rlim.rlim_cur);
         gossip_err("rlim_max (RLIMIT_CORE): [%Ld]\n", rlim.rlim_max);
      }
		exit(1);
   }
	else if (sig_nr != SIGPIPE)
	{
		/* TODO: Any other cleanups should be done here */
		exit(1);
	}
}

static void do_restart(int sig_nr, siginfo_t *info, void *unused)
{
	gossip_err("Got a SIGHUP! Reinitializing!\n");
	/* TODO: Any reinitialization stuff must go here */
	return;
}

static void do_graceful_shutdown(int sig_nr, siginfo_t *info, void *unused)
{
	gossip_err("Got signal %d! Terminating gracefully!\n", sig_nr);
	/* TODO: Do any graceful shutdown here */
	vec_cleanup();
	exit(0);
}

/* Start the server, setup signal handlers and daemonize before returning */
static int startup(int argc, char **argv)
{
	struct sigaction handler;
	struct rlimit    limit;

	if (getuid() != 0 && geteuid() != 0) {
		gossip_err(  "WARNING: %s should be run as root\n", argv[0]);
		exit(1);
	}

	if (is_daemon) 
	{
		int logfd, nullfd;
		char logname[] = "/tmp/veclog.XXXXXX";

		if ((logfd = mkstemp(logname)) == -1) 
		{
			if ((nullfd = open("/dev/null", O_RDWR)) == -1) {
				gossip_err("vec: error opening log");
				return -1;
			}
			gossip_err("couldn't create logfile [%s].\n", logname);

			/* ensure that 0-2 don't get used elsewhere */
			dup2(nullfd, 0);
			dup2(nullfd, 1);
			dup2(nullfd, 2);
			if (nullfd > 2) close(nullfd);
		}
		else {
			fchmod(logfd, 0755);
			dup2(logfd, 2);
			dup2(logfd, 1);
			close(0);
		}
		if (fork()) exit(0); /* fork() and kill parent */
		setsid();
	}
	memset(&handler, 0, sizeof(struct sigaction));
	handler.sa_sigaction = (void *) do_graceful_shutdown;
	handler.sa_flags = SA_SIGINFO;
	/* set up SIGTERM handler to shut things down */
	if (sigaction(SIGTERM, &handler, NULL) != 0) {
		gossip_err( "Could not setup signal handler for SIGTERM: %s\n", strerror(errno));
		return -1;
	}
	/* set up SIGINT handler to shut things down */
	if (sigaction(SIGINT, &handler, NULL) != 0) {
		gossip_err( "Could not setup signal handler for SIGINT: %s\n", strerror(errno));
		return -1;
	}
	/* set up SIGHUP handler to restart the daemon */
	handler.sa_sigaction = (void *) do_restart;
	if (sigaction(SIGHUP, &handler, NULL) != 0) {
		gossip_err( "Could not setup signal handler for SIGHUP: %s\n", strerror(errno));
		return -1;
	}
	/* catch SIGPIPE and SIGSEGV signals and log them, on SEGV we die */
	handler.sa_sigaction = (void *) do_fatal_signal;
	if (sigaction(SIGPIPE, &handler, NULL) != 0) {
		gossip_err( "Could not setup signal handler for SIGPIPE: %s\n", strerror(errno));
		return -1;
	}
	if (sigaction(SIGSEGV, &handler, NULL) != 0) {
		gossip_err( "Could not setup signal handler for SIGSEGV: %s\n", strerror(errno));
		return -1;
	}

	umask(0);
	chdir("/"); /* to avoid unnecessary busy filesystems */

	/* Increase the stack size, make it unlimited */
	limit.rlim_cur = RLIM_INFINITY;
	limit.rlim_max = RLIM_INFINITY;
	if (setrlimit(RLIMIT_STACK, &limit) != 0) {
		gossip_err( "Warning! Could not setup ulimited stack size: %s\n", strerror(errno));
	}
    /* Set up the hash tables for the version structures */
    PINT_handle_vec_cache_init();
	return(0);
}

struct slave_args {
	struct svc_req *rqstp;
	SVCXPRT *transp;
};

static void *vec_slave_func(void *args)
{
	struct slave_args *sl_args = (struct slave_args *) args;
	struct svc_req *rqstp;
	register SVCXPRT *xprt;

	if (sl_args == NULL) {
		gossip_err(  "slave thread got invalid arguments!\n");
		vec_cleanup();
		exit(1);
	}
	rqstp = sl_args->rqstp;
	xprt  = sl_args->transp;
	VEC_DISPATCH_FN(vec_v1)(rqstp, xprt);

	free(sl_args);
	free(rqstp);
	return NULL;
}

/*
 * Determines if "sock" is a TCP/UDP based socket.
 * This is a pretty nasty way to do it, but I don't
 * know if there is any other way to do this.
 */
static int is_tcp(int sock)
{
	static int level = -1;
	struct tcp_info info;
	socklen_t len = sizeof(info);

	if (level < 0) {
		level = (getprotobyname("tcp"))->p_proto;
	}
	if (getsockopt(sock, level, TCP_INFO, &info, &len) != 0) {
		if (errno == ENOPROTOOPT || errno == EOPNOTSUPP) {
			return 0;
		}
		return -1;
	}
	return 1;
}

/* called by the RPC layer when a request arrives */
static void vec_dispatch(struct svc_req *rqstp, SVCXPRT *xprt)
{
	/*
	 * what we do here is assign this request to the slave thread
	 * from the pool, and continue back to the RPC layer.
	 */
	struct slave_args *sl_args = NULL;
	int is_it_tcp;

	sl_args = (struct slave_args *) calloc(1, sizeof(struct slave_args));
	if (sl_args == NULL) {
		gossip_err(  "calloc failed. exiting!\n");
		vec_cleanup();
		exit(1);
	}
	sl_args->rqstp = (struct svc_req *)  calloc(1, sizeof(*rqstp));
	if (sl_args->rqstp == NULL) {
		free(sl_args);
		gossip_err(  "calloc failed. exiting!\n");
		vec_cleanup();
		exit(1);
	}
	memcpy(sl_args->rqstp, rqstp, sizeof(*rqstp));
	sl_args->transp = xprt;
	/*
	 * At this point determine if the connection is on the UDP/TCP transport.
	 * We have had problems with multi-threading when xport was TCP,
	 * so we will dynamically determine that and either dispatch it
	 * to the thread pool or we will service it directly here.
	 */
	is_it_tcp = is_tcp(rqstp->rq_xprt->xp_sock);
	/*
	 * sl_args is freed by the slave thread upon its
	 * exit. Please do not free it up here.
	 */
	/* only if it is a udp-based xport we will use a thread pool */
	if (is_it_tcp == 0) {
		/* submit it to the thread pool */
		if (use_tpool) {
			int ret;
			typedef void *(*pthread_fn)(void *);

			if ((ret = tp_assign_work_by_id(id, (pthread_fn) vec_slave_func, (void *)sl_args)) != 0) {
				gossip_err("Could not assign work to thread pool! %s\n", tp_strerror(ret));
				vec_cleanup();
				exit(1);
			}
			return;
		}
		else { /* create a thread each time */
			typedef void *(*pthread_fn)(void *);

			pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
			if (pthread_create(&tid, &attr, (pthread_fn) vec_slave_func, (void *)sl_args) != 0) {
				gossip_err("Could not create thread! %s\n", strerror(errno));
				vec_cleanup();
				exit(1);
			}
			return;
		}
	}
	else {
		/* either tcp or something else, we service it ourselves */
		vec_slave_func(sl_args);
	}
	return;
}

/* Sets the ball rolling after parsing the command line options */
static int vec_init(int argc, char **argv)
{
	int opt, timeout;
	static tp_info tinfo = {
tpi_count:VEC_NUM_THREADS,
tpi_name :NULL,
tpi_stack_size:-1, /* default stack size is good enough */
	};

	while ((opt = getopt(argc, argv, "cn:t:dp:")) != EOF) 
	{
		switch (opt) {
			case 'c':
				/* create a thread each time. dont start up a tpool */
				use_tpool = 0;
				break;
			case 'n':
				num_threads = atoi(optarg);
				if (num_threads > 0) {
					tinfo.tpi_count = num_threads;
					/* force the use of a thread pool if possible */
					use_tpool = 1;
				}
				break;
			case 't':
				timeout = atoi(optarg);
				break;
			case 'd':
				is_daemon = 0;
				break;
			case 'p':
				vec_port = atoi(optarg);
				break;
			default:
				usage(argv[0]);
				return -1;
		}
	}
	if (startup(argc, argv) < 0) {
		return -1;
	}
	if (use_tpool) {
		gossip_debug(GOSSIP_VEC_DEBUG, 
                "---------- Starting version-vector server servicing RPCs using a thread pool "
                " [%d threads] ---------- \n", tinfo.tpi_count);
	}
	else {
		gossip_debug(GOSSIP_VEC_DEBUG, 
                "---------- Starting version-vector server servicing RPCs by creating "
                " a thread each time  ---------- \n");
	}
	if (use_tpool) {
		/* Fire up a thread pool for servicing future requests */
		id = tp_init(&tinfo);
		if (id < 0) {
			gossip_err(  "Could not fire up thread pool!\n");
			return -1;
		}
	}
	/* Start up the local RPC service on both TCP and UDP */
	if (setup_service(-1 /* first available prog# */,
				vec_v1 /* version */,
				-1 /* both tcp & udp */,
				vec_port /* port */,
				vec_dispatch, /* dispatch function */
				&info) < 0) 
	{
		if (use_tpool) {
			tp_cleanup_by_id(id);
		}
		gossip_err(  "Could not fire up RPC service!\n");
		return -1;
	}
	/* Should not return */
	gossip_err("Panic! setup_service returned!\n");
	if (use_tpool) {
		tp_cleanup_by_id(id);
	}
	return 0;
}

static void vec_cleanup(void)
{
	gossip_err("About to turn off RPC service\n");
	/* cleanup the RPC service */
	cleanup_service(&info);
	if (use_tpool) {
		/* Clean up the thread pool */
		tp_cleanup_by_id(id);
	}
    PINT_handle_vec_cache_finalize();
}

/* Main function where the vec server is fired up from */
int main (int argc, char **argv)
{
	srand(time(NULL));
	gossip_enable_stderr();
	gossip_set_debug_mask(1, GOSSIP_VEC_DEBUG);
	/* Unset the program number and version from the portmapper */
	pmap_unset (VEC_MGR, vec_v1);
	/* Start up the thread pool and register a service with the portmapper */
	if (vec_init(argc, argv) < 0) {
		exit(1);
	}
	/* NOTREACHED */
	exit (0);
}
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
