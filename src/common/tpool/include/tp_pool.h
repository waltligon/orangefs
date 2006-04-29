#ifndef _THREAD_POOL_H
#define _THREAD_POOL_H
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include "tp_proto.h"

#ifndef __cplusplus
typedef enum {false=0,true=1} bool;
#endif


typedef struct qelem qelem;
struct qelem{
	pthread_work_function qe_func;
	PVOID  qe_args;
	qelem *next;
};

typedef struct {
	qelem *head,*tail;
}list_head;

typedef struct {
	/* the thread-pool id to which this thread belongs to */
	tp_id tpa_id;
	/* the index into the array maintained by each thread pool */
	int tpa_index;
}tp_args;

typedef struct {
	pthread_t pti_id;
}per_thread_info;

/*
 * In order to make the thread-pool implementation Async-signal safe,
 * we only use sem_t and a custom implementation of a spin lock
 * which are the only POSIX synchronization primitives
 * which are guaranteed async signal safe and can be used safely
 * inside a signal handler function.
 * This thread library makes use of SIGUSR2 to deliver signals to the
 * individual threads, since we need control over the signal handler
 * code. Choice of this signal is quite arbitrary , however the 
 * overriding concern here is that upper level layers should not
 * define signal handlers for this signal
 */


/* Information for a particular thread pool */
typedef struct {
	/* pool identifier */
	tp_id   ppi_id;
	/* number of threads created originally, name of the pool, stack size etc */
	tp_info *ppi_info;
	/* semaphore for counting the number of active threads */
	sem_t   ppi_active_count;
	/* counting and binary semaphores for normal operations */
	sem_t  ppi_empty,ppi_full,ppi_lock;
	list_head ppi_head;
	/* Information on each thread belonging to this pool */
	per_thread_info *ppi_threads;
}per_pool_info;

/* Information about all registered thread-pools */
typedef struct {
	/* Number of pre-allocated thread-pool info. We cannot register more than this number
		of thread-pools
	*/
	int pi_number;
	/* actual number of registered thread pools */
	int pi_count;
	/* pointer(s) to per-pool information */
	per_pool_info **pi_pool;
	/* status of each thread-pool */
	bool *pi_status;
}pool_info;


/* fire up threads for a specified pool */
extern int ppi_thread_start(per_pool_info*);
extern int ppi_thread_stop(per_pool_info*);
extern int ppi_thread_assign(per_pool_info *ppi, pthread_work_function func, PVOID args);
extern void exit_handler(int sig_nr, siginfo_t *,void *);
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
