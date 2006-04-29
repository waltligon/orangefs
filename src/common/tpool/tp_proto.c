/*
 * (C) 2006 The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "tp_common.h"
#include "tp_pool.h" /* includes tp_proto.h */
#include "tp_impl.h"


#if 0
static int testandset(tp_spinlock_t *spinlock)
{
  int ret;
  /* atomically exchange 1 with the memory location pointed to by spinlock */
  __asm__ __volatile__("xchgl %0, %1"
	: "=r"(ret), "=m"(*spinlock)
	: "0"(1), "m"(*spinlock));
  /* return the contents of the location pointed to by spinlock */
  return ret;
}

static void unset(tp_spinlock_t *spinlock)
{
	/* release the spinlock by setting it to 0 */
	*spinlock = 0;
}
#endif


/* simple spin-locks */
void tp_spinlock(tp_spinlock_t *spinlock)
{
	//pthread_spin_lock(spinlock);
	return;
}

void tp_spinunlock(tp_spinlock_t *spinlock)
{
	//pthread_spin_unlock(spinlock);
	return;
}

/* tp_lock: the job of this spin lock is to
*  prevent races between someone yanking 
*  out a thread pool id while assigning work 
*  to the same thread pool, which is
*  theoretically possible, though admittedly
*  contrived and stupid!
*  FIXME: Make this more fine grained!
*/

int tp_debug = 0;
static tp_spinlock_t tp_lock;
static bool tp_initialized = false;

/* called with the spinlock held */
static int tp_once_setup(void)
{
	int ret = 0;
	if(tp_initialized == false) {
		/* setup the tp data structures */
		if((ret=tp_setup()) < 0) {
			PERROR("Could not setup %s!\n",tp_strerror(ret));
			return ret;
		}
#if 1
		if(atexit(tp_cleanup) < 0) {
			PERROR("Could not register atexit method!\n");
			tp_cleanup();
			return TP_SYSTEM_ERROR;
		}
#endif
		tp_initialized = true;
	}
	return 0;
}

/* 
*  create a thread pool based on the given parameters. 
*  If necessary, initialize the data structures as well. 
*/
tp_id tp_init(tp_info *info)
{
	tp_id ret = 0;

	//pthread_spin_init(&tp_lock, 0);
	if(!info) {
		PERROR("Invalid pointer to tp_info\n");
		return (tp_id)TP_INVALID_PARAMETER;
	}
	if(info->tpi_count <= 0 || info->tpi_count >= TP_MAX_THREADS) {
		PERROR("Invalid parameter tpi_count %d\n",info->tpi_count);
		return (tp_id)TP_INVALID_PARAMETER;
	}
	tp_spinlock(&tp_lock);
	if((ret = tp_once_setup()) < 0) {
		tp_spinunlock(&tp_lock);
		return ret;
	}
	/* Register this thread pool with the system */
	ret = tp_register(info);
	if(ret < 0) {
		PERROR("Could not register thread pool with system %s\n",tp_strerror(ret));
	}
	tp_spinunlock(&tp_lock);
	return ret;
}

int tp_cleanup_by_id(tp_id id)
{
	int ret;
	if(id < 0) {
		PERROR("Invalid thread-pool id!\n");
		return TP_INVALID_POOL_ID;
	}
	tp_spinlock(&tp_lock);
	if(tp_initialized == false) {
		PERROR("Thread pool not initialized\n");
		tp_spinunlock(&tp_lock);
		return TP_UNINITIALIZED;
	}
	ret = tp_unregister_by_id(id);
	tp_spinunlock(&tp_lock);
	if(ret < 0) {
		PERROR("Could not unregister thread pool %s\n",tp_strerror(ret));
	}
	return ret;
}

int tp_cleanup_by_name(const char* name)
{
	int ret = 0;
	if(!name) {
		PERROR("Invalid pool name!\n");
		return TP_INVALID_POOL_NAME;
	}
	tp_spinlock(&tp_lock);
	if(tp_initialized == false) {
		PERROR("Thread pool not initialized\n");
		tp_spinunlock(&tp_lock);
		return TP_UNINITIALIZED;
	}
	ret = tp_unregister_by_name(name);
	tp_spinunlock(&tp_lock);
	if(ret < 0) {
		PERROR("Could not unregister thread pool %s\n",tp_strerror(ret));
	}
	return ret;
}

int tp_assign_work_by_id(tp_id id, pthread_work_function func, PVOID args)
{
	int ret = 0;
	if(!func) {
		PERROR("Invalid parameter!\n");
		return TP_INVALID_PARAMETER;
	}
	tp_spinlock(&tp_lock);
	if(tp_initialized == false) {
		PERROR("Thread pool not initialized\n");
		tp_spinunlock(&tp_lock);
		return TP_UNINITIALIZED;
	}
	ret = do_assign_work_by_id(id,func, args); 
	tp_spinunlock(&tp_lock);
	if(ret <  0) {
		PERROR("Could not assign work %s\n",tp_strerror(ret));
	}
	return ret;
}

int tp_assign_work_by_name(const char* name, pthread_work_function func, PVOID args)
{
	int ret = 0;
	if(!func ) {
		PERROR("Invalid parameter!\n");
		return TP_INVALID_PARAMETER;
	}
	tp_spinlock(&tp_lock);
	if(tp_initialized == false) {
		PERROR("Thread pool not initialized\n");
		tp_spinunlock(&tp_lock);
		return TP_UNINITIALIZED;
	}
	ret = do_assign_work_by_name(name,func, args); 
	tp_spinunlock(&tp_lock);
	if(ret <  0) {
		PERROR("Could not assign work %s\n",tp_strerror(ret));
	}
	return ret;
}

char *tp_strerror(int tp_error)
{
	static char str[40];
	if(tp_error > 0 || tp_error <= TP_MAX_ERROR) 
		return "Unknown Error\n";
	if(tp_error == TP_SYSTEM_ERROR) {
		sprintf(str,"%s: %s\n",(char *)tp_err_list[-tp_error],(char *)strerror(errno));
		return str;
	}
	else {
		return tp_err_list[-tp_error];
	}
}

void tp_perror(int tp_error)
{
	if(tp_error > 0 || tp_error <= TP_MAX_ERROR) {
		fprintf(stderr,"Unknown Error\n");
	}
	else {
		if(tp_error == TP_SYSTEM_ERROR) {
			fprintf(stderr,"%s : %s\n",(char *)tp_err_list[-tp_error],(char *)strerror(errno));
		}
		else {
			fprintf(stderr,"%s\n",tp_err_list[-tp_error]);
		}
	}
	return;
}

int tp2errno(int tp_error)
{
	switch(tp_error) {
		case TP_INVALID_POOL_ID:
		case TP_INVALID_POOL_NAME:
		case TP_INVALID_PARAMETER:
		case TP_UNINITIALIZED:
			return EINVAL;
		case TP_TOO_MANY_RESOURCES:
		case TP_NOMEMORY:
			return ENOMEM;
		case TP_FATAL:
		case TP_SYSTEM_ERROR:
			/* nothing equivalent at all */
			return EFAULT;
		case TP_NOPOOL:
			/* nothing equivalent at all */
			return ENOENT;
		default:
			return 0;
	}
}

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
