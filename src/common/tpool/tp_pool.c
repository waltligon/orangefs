/*
 * (C) 2006 The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "tp_common.h"
#include <assert.h>
#include "tp_pool.h"


/* TSD-stuff */
static pthread_once_t slot_key_once = PTHREAD_ONCE_INIT;
/* We need multiple thread keys, one per thread pool */
static pthread_key_t  slot_key[TP_PREALLOC];
static void tsd_key_alloc(void);
static int tsd_set(int poolid, void *args);
static void *tsd_get(int poolid);

/* QUEUE FUNCTION PROTOTYPES */
static qelem *qe_ctor(pthread_work_function qe_func, PVOID qe_args);
static void qe_dtor(qelem *qe);
static int insert_end(per_pool_info *ppi,qelem *new_work);
static qelem* delete_front(per_pool_info *ppi);
static struct thread_args* ta_ctor(per_pool_info *ta_ppi, int ta_slot);
static void ta_dtor(struct thread_args *args);
static void change_active_counter(per_pool_info *ppi);
static void *thread_cradle_func(PVOID arg);

struct thread_args {
	per_pool_info *ta_ppi;
	int            ta_slot;
};

static qelem *qe_ctor(pthread_work_function qe_func, PVOID qe_args)
{
	qelem *temp = NULL;
	temp = (qelem *)calloc(1,sizeof(qelem));
	if(!temp) {
		return NULL;
	}
	temp->qe_func = qe_func;
	temp->qe_args = qe_args;
	temp->next    = NULL;
	return temp;
}

static void qe_dtor(qelem *qe)
{
	if(qe) {
		free(qe);
	}
	return;
}

static struct thread_args* ta_ctor(per_pool_info *ta_ppi, int ta_slot)
{
	struct thread_args *args = NULL;
	args = (struct thread_args *)calloc(1,sizeof(struct thread_args));
	if(!args) {
		return NULL;
	}
	args->ta_ppi = ta_ppi;
	args->ta_slot = ta_slot;
	return args;
}

static void ta_dtor(struct thread_args *args)
{
	free(args);
	return;
}

static void change_active_counter(per_pool_info *ppi)
{
	sem_post(&ppi->ppi_active_count);
	return;
}

static void *thread_cradle_func(PVOID arg)
{ 
	struct thread_args *targs = (struct thread_args *)arg;
	per_pool_info *ppi = targs->ta_ppi;
	//per_thread_info *pti = (per_thread_info *)&ppi->ppi_threads[targs->ta_slot];
	sigset_t signal_set;
	sigfillset(&signal_set);
	/* ignore all signals but SIGUSR2 */
	sigdelset(&signal_set, SIGUSR2);
	pthread_sigmask(SIG_SETMASK,&signal_set, NULL);
	/* set thread arguments as thread-specific data */
	tsd_set(ppi->ppi_id, targs);
	PDEBUG(D_POOL,"Spawned successfully %d with args %p\n",targs->ta_slot,targs);
	change_active_counter(ppi);
	/* Main loop of all threads
	 * - get work.
	 * - execute it.
	 * - return back to sleep.
	 */
	for(;;) {
		pthread_work_function func = NULL;
		PVOID args = NULL;
		qelem *work = NULL;
		sem_wait(&ppi->ppi_full);
		work = delete_front(ppi);
		sem_post(&ppi->ppi_empty);
		if (work) {
			func = work->qe_func;
			args = work->qe_args;
			qe_dtor(work);
			PDEBUG(D_POOL,"%ld got work\n",pthread_self());
			if(func) {
				func(args);
			}
			PDEBUG(D_POOL,"%ld done work\n",pthread_self());
		}
	}
	return NULL;
}

void exit_handler(int sig_nr, siginfo_t *info, void *unused)
{	
	int i;
	struct thread_args *args = NULL;
	/* When a thread exits, we need to be able to determine, which 
	 * pool id it belongs to, since there is no easy way, we iterate
	 * through all the keys and obviously only 1 pool will match 
	 */
	PDEBUG(D_POOL, "Thread %lu got signal %d\n", pthread_self(), sig_nr);
	for (i = 0; i < TP_PREALLOC; i++) {
		args = (struct thread_args *)tsd_get(i);
		PDEBUG(D_POOL, "Thread %lu tried to get args as %p\n", pthread_self(), args);
		if(args) {
			per_pool_info *ppi = args->ta_ppi;
			assert(ppi->ppi_id == i);
			//per_thread_info *pti = &ppi->ppi_threads[args->ta_slot];
			change_active_counter(ppi);
			break;
		}
		else {
			PDEBUG(D_POOL, "Could not get tsd pointer! skipping...\n");
		}
	}
	if (i == TP_PREALLOC) {
		PDEBUG(D_POOL, "Thread %lu could not get tsd!\n", pthread_self());
 	}
	else {
		PDEBUG(D_POOL,"Exiting %d\n",args->ta_slot);
	}
	pthread_exit(NULL);
}

static pthread_attr_t attr;
/* DONT NEED TO CLEAN UP ON AN ERROR. UPTO HIGHER LEVEL LAYERS TO CLEANUP */
int ppi_thread_start(per_pool_info* ppi)
{
	int i = 0, count = 0, err = 0;

	if(!ppi || !ppi->ppi_info || !ppi->ppi_threads) {
		PERROR("Invalid parameter!\n");
		return TP_INVALID_PARAMETER;
	}
	sem_init(&ppi->ppi_active_count, 0, 0);
	count = ppi->ppi_info->tpi_count;
	pthread_attr_init(&attr);
	if (ppi->ppi_info->tpi_stack_size > 0) {
		if ((err = pthread_attr_setstacksize(&attr, ppi->ppi_info->tpi_stack_size)) != 0) {
			errno = err;
			PERROR("Warning! Could not set stack size of thread to %ld!\n", ppi->ppi_info->tpi_stack_size);
		}
	}
	for(i = 0; i < count; i++) {
		struct thread_args *args = NULL;
		/* freed by thread when it exits */
		args = ta_ctor(ppi,i);
		if(!args) {
			PERROR("No memory\n");
			err = TP_NOMEMORY;
			break;
		}
		/* fire up all the threads */
		if(pthread_create(&ppi->ppi_threads[i].pti_id, NULL, thread_cradle_func, (PVOID)args) != 0) {
			PERROR("Could not create thread\n");
			err = TP_SYSTEM_ERROR;
			break;
		}
	}
	/* no problems!! */
	if(i == count) {
		/* wait for all threads to set themselves up */
		for(i=0; i < count; i++) {
			sem_wait(&ppi->ppi_active_count);
		}
		PDEBUG(D_POOL,"Started %d threads successfully\n",count);
		return 0;
	}
	/* ohoh error */
	return err;
}

int ppi_thread_stop(per_pool_info* ppi)
{
	int count = 0,i,kill_count=0;
	if(!ppi) {
		return TP_INVALID_PARAMETER;
	}
	sem_init(&ppi->ppi_active_count, 0, 0);
	count = ppi->ppi_info->tpi_count;
	PDEBUG(D_POOL,"Asked to kill %d threads to exit\n",count);
	for(i = 0; i < count; i++) {
		/* send a terminate signal to all threads */
		if(pthread_kill(ppi->ppi_threads[i].pti_id, SIGUSR2) != 0) {
			PERROR("Could not send a signal to %d: %s\n",i, strerror(errno));
		}
		else {
			kill_count++;
		}
	}
	for(i=0; i < kill_count; i++) {
		sem_wait(&ppi->ppi_active_count);
	}
	pthread_key_delete(slot_key[ppi->ppi_id]);
	PDEBUG(D_POOL,"Killed %d threads successfully\n",kill_count);
	return 0;
}

int ppi_thread_assign(per_pool_info *ppi, pthread_work_function func, PVOID args)
{
	//int i = 0, count = ppi->ppi_info->tpi_count;
	/* just add to queue atomically and return */
	qelem *new_work = qe_ctor(func, args);
	if(!new_work) {
		return TP_NOMEMORY;
	}
	sem_wait(&ppi->ppi_empty);
	insert_end(ppi,new_work);
	sem_post(&ppi->ppi_full);
	return 0;
}

/* THREAD-SPECIFIC DATA */

static int tsd_set(int poolid, void *args)
{
	/* make sure that tsd_key_alloc executes only once */
	pthread_once(&slot_key_once, tsd_key_alloc);
	PDEBUG(D_POOL, "Thread %lu setting args to %p\n", pthread_self(), args);
	return pthread_setspecific(slot_key[poolid], args);
}

static void tsd_key_alloc(void)
{
	int i;
	
	for (i = 0; i < TP_PREALLOC; i++) {
		pthread_key_create(&slot_key[i], (void (*)(void *))ta_dtor);
	}
	return;
}

static void *tsd_get(int poolid)
{
	return pthread_getspecific(slot_key[poolid]);
}

static int insert_end(per_pool_info *ppi,qelem *new_work)
{
	sem_wait(&ppi->ppi_lock);
	if(ppi->ppi_head.head == NULL) {
		ppi->ppi_head.head = ppi->ppi_head.tail = new_work;
		sem_post(&ppi->ppi_lock);
		return 0;
	}
	ppi->ppi_head.tail->next = new_work;
	ppi->ppi_head.tail = new_work;
	sem_post(&ppi->ppi_lock);
	return 0;
}

static qelem* delete_front(per_pool_info *ppi)
{
	qelem *front = NULL;
	sem_wait(&ppi->ppi_lock);
	if(ppi->ppi_head.head == NULL) {
		sem_post(&ppi->ppi_lock);
		return NULL;
	}
	front = ppi->ppi_head.head;
	ppi->ppi_head.head = ppi->ppi_head.head->next;
	if(ppi->ppi_head.head == NULL) {
		ppi->ppi_head.tail = NULL;
	}
	sem_post(&ppi->ppi_lock);
	return front;
}

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
