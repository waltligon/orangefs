/*
 * (C) 2006 The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "tp_common.h"
#include "tp_pool.h"
#include "tp_impl.h"

const int THROTTLE_NUMBER = 19;
/* FUNCTION PROTOTYPES */
static pool_info *pi_ctor(int pi_number);
static void pi_dtor(pool_info *info);
static per_pool_info *ppi_ctor(tp_id ppi_id, tp_info *ppi_info);
static void ppi_dtor(per_pool_info *ppi);
static tp_info *tpi_ctor(tp_info *tpi);
static void tpi_dtor(tp_info *tpi);
static per_thread_info* pti_ctor(int thread_count);
static void pti_dtor(per_thread_info *ptr);


static pool_info* pi = NULL;

char *tp_err_list[]={
	"Success",
	"Invalid pool-id",
	"No such pool-name",
	"Too many resources requested",
	"Invalid parameter",
	"No memory available",
	"Fatal library error",
	"Non-existent pool",
	"System dependant error",
	"Un-initialized thread pool library",
};

/* Constructors and Destructors for various internal data structures */

static pool_info *pi_ctor(int pi_number)
{
	pool_info *temp = NULL;
	temp = (pool_info *)calloc(sizeof(pool_info),1);
	if(!temp) {
		PERROR("Could not allocate memory!\n");
		return NULL;
	}
	temp->pi_number = pi_number;
	temp->pi_count  = 0;
	temp->pi_pool = (per_pool_info **)calloc(sizeof(per_pool_info *),temp->pi_number);
	if(!temp->pi_pool) {
		PERROR("Could not allocate memory!\n");
		free(temp);
		return NULL;
	}
	temp->pi_status = (bool *)calloc(sizeof(bool),temp->pi_number);
	if(!temp->pi_status) {
		PERROR("Could not allocate memory!\n");
		free(temp->pi_pool);
		free(temp);
		return NULL;
	}
	return temp;
}

static void pi_dtor(pool_info *info)
{
	if(info) {
		int i = 0, count = 0;
		if(info->pi_status) {
			free(info->pi_status);
			info->pi_status = NULL;
		}
		if(info->pi_pool) {
			for(i = 0; i < info->pi_number; i++) {
				if(count >= info->pi_count)
					break;
				if(info->pi_pool[i]) {
					/* call the destructor for the per_pool_info */
					ppi_dtor(info->pi_pool[i]);
					info->pi_pool[i] = NULL;
					count++;
				}
			}
			free(info->pi_pool);
			info->pi_pool = NULL;
		}
		free(info);
	}
	return;
}

static per_pool_info *ppi_ctor(tp_id ppi_id, tp_info *ppi_info)
{
	per_pool_info *ppi = NULL;
	/* Allocate memory for the fields and then fire up the threads */
	ppi = (per_pool_info *)calloc(sizeof(per_pool_info),1);
	if(!ppi) {
		PERROR("Could not allocate memory!\n");
		return NULL;
	}
	ppi->ppi_id = ppi_id;
	ppi->ppi_info = tpi_ctor(ppi_info);
	if(!ppi->ppi_info) {
		PERROR("Could not allocate memory!\n");
		free(ppi);
		return NULL;
	}
	ppi->ppi_threads = pti_ctor(ppi_info->tpi_count);
	if(!ppi->ppi_threads) {
		PERROR("Could not allocate memory!\n");
		tpi_dtor(ppi->ppi_info);
		free(ppi);
	}
	/* initialize the semaphores */
	sem_init(&ppi->ppi_active_count, 0, 0);
	/* initialize it to a reasonable number to do rate throttling */
	sem_init(&ppi->ppi_empty,0,THROTTLE_NUMBER);
	sem_init(&ppi->ppi_full,0,0);
	sem_init(&ppi->ppi_lock,0,1);
	/* now fire up the threads */
	if(ppi_thread_start(ppi) < 0) {
		PERROR("Could not fire up threads!\n");
		ppi_dtor(ppi);
		ppi = NULL;
	}
	return ppi;
}

static void ppi_dtor(per_pool_info *ppi)
{
	if(ppi) {
		/* now stop the threads, Ignore any errors from this */
		ppi_thread_stop(ppi);
		if(ppi->ppi_info) {
			tpi_dtor(ppi->ppi_info);
			ppi->ppi_info = NULL;
		}
		if(ppi->ppi_threads) {
			pti_dtor(ppi->ppi_threads);
			ppi->ppi_threads = NULL;
		}
		free(ppi);
	}
	return;
}

static tp_info *tpi_ctor(tp_info *tpi)
{
	tp_info *info = NULL;
	if(!tpi) {
		PERROR("Could not allocate memory!\n");
		return NULL;
	}
	info = (tp_info *)calloc(1,sizeof(tp_info));
	if(!info) {
		PERROR("Could not allocate memory!\n");
		return NULL;
	}
	if(tpi->tpi_name) {
		info->tpi_name = (char *)calloc(sizeof(char), strlen(tpi->tpi_name) + 1);
		if(!info->tpi_name) {
			PERROR("Could not allocate memory!\n");
			free(info);
			return NULL;
		}
		strcpy(info->tpi_name, tpi->tpi_name);
	}
	info->tpi_count = tpi->tpi_count;
	info->tpi_stack_size = tpi->tpi_stack_size;
	PDEBUG(D_IMPL,"Requested to start %d thread\n",info->tpi_count);
	return info;
}

static void tpi_dtor(tp_info *tpi)
{
	if(tpi) {
		if(tpi->tpi_name) {
			free(tpi->tpi_name);
			tpi->tpi_name = NULL;
		}
		free(tpi);
	}
	return;
}

static per_thread_info* pti_ctor(int thread_count)
{
	int i;
	per_thread_info *pti = NULL;
	pti = (per_thread_info *)calloc(sizeof(per_thread_info), thread_count);
	if(!pti) {
		PERROR("Could not allocate memory!\n");
		return NULL;
	}
	for(i=0; i < thread_count; i++) {
		pti[i].pti_id = 0;
	}
	return pti;
}

static void pti_dtor(per_thread_info *pti)
{
	if(pti) {
		free(pti);
	}
	return;
}


/* should be called only once */
int tp_setup(void)
{
	struct sigaction act;
	pi = pi_ctor(TP_PREALLOC);
	if(!pi) {
		PERROR("Could not allocate memory!\n");
		return TP_NOMEMORY;
	}
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_flags = SA_SIGINFO;
	act.sa_sigaction = exit_handler;
	/* setup signal handler for SIGUSR2 */
	if(sigaction(SIGUSR2,&act, NULL) < 0) {
		PERROR("Could not setup signal handler %s\n",strerror(errno));
		pi_dtor(pi);
		pi = NULL;
		return TP_FATAL;
	}
	return 0;
}

/* should be called only once */
/* tear down all the created thread-pools */
#if 0
void tp_cleanup(void) __attribute__ ((destructor));
#endif
void tp_cleanup(void)
{
	if(!pi) {
		return ;
	}
	pi_dtor(pi);
	return ;
}

/* should be called with a lock held */
/* Register a thread-pool with the system.
 *  Returns the thread-pool id of the newly created pool 
 */
tp_id tp_register(tp_info *info)
{
	int i = 0;
	if(pi->pi_count >= pi->pi_number) {
		PERROR("Too many thread pools!\n");
		return TP_TOO_MANY_RESOURCES;
	}
	for(i = 0; i < pi->pi_number; i++) {
		if(pi->pi_status[i] == false)
			break;
	}
	if(i == pi->pi_number) {
		PERROR("BUG: Could not find spare free slot?\n");
		return TP_FATAL;
	}
	pi->pi_pool[i] = ppi_ctor(i,info);
	if(!pi->pi_pool[i]) {
		PERROR("Could not allocate memory!\n");
		return TP_NOMEMORY;
	}
	pi->pi_count++;
	pi->pi_status[i] = true;
	return (tp_id)i;
}

/* Should be called with a lock held */
/* De-register a thread-pool from the system */
int tp_unregister_by_id(tp_id id)
{
	if(id < 0 || id >= pi->pi_number) {
		PERROR("Invalid thread-pool id!\n");
		return TP_INVALID_POOL_ID;
	}
	pi->pi_count--;
	ppi_dtor(pi->pi_pool[id]);
	pi->pi_pool[id] = NULL;
	pi->pi_status[id] = false;
	return 0;
}

static tp_id name2tpid(const char *name)
{
	int i = 0;
	if(!name) {
		PERROR("Invalid parameter!\n");
		return TP_INVALID_PARAMETER;
	}
	for(i=0;i < pi->pi_number; i++) {
		if(pi->pi_pool[i]) {
			if(pi->pi_pool[i]->ppi_info) {
				if(pi->pi_pool[i]->ppi_info->tpi_name) {
					if(!strcmp(pi->pi_pool[i]->ppi_info->tpi_name, name)) {
						return i;
					}
				}
			}
			else {
				PERROR("BUG? No thread information for thread pool %d\n",i);
			}
		}
	}
	PERROR("Could not locate thread-pool specified!\n");
	return TP_NOPOOL;
}

/* Should be called with a lock held */
int tp_unregister_by_name(const char *name)
{
	int tp_id=-1;
	tp_id = name2tpid(name);
	if(tp_id >= 0) {
		return tp_unregister_by_id(tp_id);
	}
	return tp_id;
}

int do_assign_work_by_id(tp_id id, pthread_work_function func, PVOID args)
{
	int ret = 0;
	if(id < 0 || id >= pi->pi_number) {
		PERROR("Invalid thread-pool id!\n");
		return TP_INVALID_POOL_ID;
	}
	if(!pi->pi_pool[id]) {
		PERROR("Could not locate thread-pool specified!\n");
		return TP_NOPOOL;
	}
	ret = ppi_thread_assign(pi->pi_pool[id],func,args);
	return ret;
}

int do_assign_work_by_name(const char *name, pthread_work_function func, PVOID args)
{
	int tp_id=-1;
	tp_id = name2tpid(name);
	if(tp_id >= 0) {
		return do_assign_work_by_id(tp_id, func, args);
	}
	return tp_id;
}

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
