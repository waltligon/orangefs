/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <pthread.h>
#include <stdlib.h>
#include <assert.h>

#include "pvfs2-types.h"
#include "thread-mgr.h"
#include "gen-locks.h"
#include "gossip.h"
#include "bmi.h"
#include "trove.h"

#define THREAD_MGR_TEST_COUNT 5
#define THREAD_MGR_TEST_TIMEOUT 10
static int thread_mgr_test_timeout = THREAD_MGR_TEST_TIMEOUT;

/* TODO: organize this stuff better */
static void *bmi_thread_function(void *ptr);
static void *trove_thread_function(void *ptr);
static void *dev_thread_function(void *ptr);
static struct BMI_unexpected_info stat_bmi_unexp_array[THREAD_MGR_TEST_COUNT];
static bmi_op_id_t stat_bmi_id_array[THREAD_MGR_TEST_COUNT];
static bmi_error_code_t stat_bmi_error_code_array[THREAD_MGR_TEST_COUNT];
static bmi_size_t stat_bmi_actual_size_array[THREAD_MGR_TEST_COUNT];
static void *stat_bmi_user_ptr_array[THREAD_MGR_TEST_COUNT];
static TROVE_op_id stat_trove_id_array[THREAD_MGR_TEST_COUNT];
static void *stat_trove_user_ptr_array[THREAD_MGR_TEST_COUNT];
static TROVE_ds_state stat_trove_error_code_array[THREAD_MGR_TEST_COUNT];
static gen_mutex_t bmi_mutex = GEN_MUTEX_INITIALIZER;
static gen_mutex_t trove_mutex = GEN_MUTEX_INITIALIZER;
static gen_mutex_t dev_mutex = GEN_MUTEX_INITIALIZER;
static int bmi_unexp_count = 0;
static int dev_unexp_count = 0;
static void (*bmi_unexp_fn)(struct BMI_unexpected_info* unexp);
static void (*dev_unexp_fn)(struct PINT_dev_unexp_info* unexp);
static bmi_context_id global_bmi_context = -1;
static TROVE_context_id global_trove_context = -1;
static int bmi_thread_ref_count = 0;
static int trove_thread_ref_count = 0;
static int dev_thread_ref_count = 0;
static PVFS_fs_id HACK_fs_id = 9; /* TODO: fix later */
static struct PINT_dev_unexp_info stat_dev_unexp_array[THREAD_MGR_TEST_COUNT];
#ifdef __PVFS2_JOB_THREADED__
static pthread_t bmi_thread_id;
static pthread_t trove_thread_id;
static pthread_t dev_thread_id;

static pthread_cond_t bmi_test_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t trove_test_cond = PTHREAD_COND_INITIALIZER;
#endif /* __PVFS2_JOB_THREADED__ */

/* used to indicate that a bmi testcontext is in progress; we can't simply
 * hold a lock while calling bmi testcontext for performance reasons
 * (particularly under NPTL)
 */
static gen_mutex_t bmi_test_mutex = GEN_MUTEX_INITIALIZER;
static int bmi_test_flag = 0;
static int bmi_test_count = 0;
static gen_mutex_t trove_test_mutex = GEN_MUTEX_INITIALIZER;
static int trove_test_flag = 0;
static int trove_test_count = 0;

static int bmi_thread_running = 0;
static int trove_thread_running = 0;
static int dev_thread_running = 0;

/* trove_thread_function()
 *
 * function executed by the thread in charge of trove
 */
static void *trove_thread_function(void *ptr)
{
    int ret = -1;
    int i=0;
    struct PINT_thread_mgr_trove_callback *tmp_callback;
    int timeout = thread_mgr_test_timeout;

#ifdef __PVFS2_JOB_THREADED__
    while (trove_thread_running)
#endif
    {
	/* indicate that a test is in progress */
	gen_mutex_lock(&trove_test_mutex);
	trove_test_flag = 1;
	gen_mutex_unlock(&trove_test_mutex);
	
	trove_test_count = THREAD_MGR_TEST_COUNT;
#ifdef __PVFS2_TROVE_SUPPORT__
	ret = trove_dspace_testcontext(HACK_fs_id,
	    stat_trove_id_array,
	    &trove_test_count,
	    stat_trove_error_code_array,
	    stat_trove_user_ptr_array,
	    timeout,
	    global_trove_context);
#else
	timeout = 0;
	stat_trove_id_array[0] = 0;
	HACK_fs_id = 0;
	assert(0);
#endif
	gen_mutex_lock(&trove_test_mutex);
	trove_test_flag = 0;
#ifdef __PVFS2_JOB_THREADED__
	pthread_cond_signal(&trove_test_cond);
#endif
	gen_mutex_unlock(&trove_test_mutex);

	if(ret < 0)
	{
	    /* critical error */
	    /* TODO: how to handle this */
	    assert(0);
	    gossip_lerr("Error: critical Trove failure.\n");
	}

	for(i=0; i<trove_test_count; i++)
	{
	    /* execute a callback function for each completed BMI operation */
	    tmp_callback = 
		(struct PINT_thread_mgr_trove_callback*)stat_trove_user_ptr_array[i];
	    /* sanity check */
	    assert(tmp_callback != NULL);
	    assert(tmp_callback->fn != NULL);
	   
	    tmp_callback->fn(tmp_callback->data, stat_trove_error_code_array[i]);
	}
    }
    return (NULL);
}


/* bmi_thread_function()
 *
 * function executed by the thread in charge of BMI
 */
static void *bmi_thread_function(void *ptr)
{
    int ret = -1;
    int quick_flag = 0;
    int incount, outcount;
    int i=0;
    int test_timeout = thread_mgr_test_timeout;
    struct PINT_thread_mgr_bmi_callback *tmp_callback;

#ifdef __PVFS2_JOB_THREADED__
    while (bmi_thread_running)
#endif
    {
	gen_mutex_lock(&bmi_mutex);
	if(bmi_unexp_count)
	{
	    incount = bmi_unexp_count;
	    if(incount > THREAD_MGR_TEST_COUNT)
		incount = THREAD_MGR_TEST_COUNT;
	    gen_mutex_unlock(&bmi_mutex);

	    ret = BMI_testunexpected(incount, &outcount, stat_bmi_unexp_array, 0);
	    if(ret < 0)
	    {
		/* critical failure */
		/* TODO: how to handle this? */
		assert(0);
		gossip_lerr("Error: critical BMI failure.\n");
	    }

	    /* execute callback function for each completed unexpected message */
	    gen_mutex_lock(&bmi_mutex);
	    for(i=0; i<outcount; i++)
	    {
		bmi_unexp_fn(&stat_bmi_unexp_array[i]);
		bmi_unexp_count--;
	    }
	    gen_mutex_unlock(&bmi_mutex);

	    /* set a flag if we are getting as many incoming BMI unexpected
	     * operations as we can handle to indicate that we should cycle
	     * quickly 
	     */
	    if(outcount == THREAD_MGR_TEST_COUNT)
		quick_flag = 1;
	}
	else
	{
	    gen_mutex_unlock(&bmi_mutex);
	}

	/* decide how long we are willing to wait on the main test call */
	if(quick_flag)
	{
	    quick_flag = 0;
	    test_timeout = 0;
	}
	else
	{
	    test_timeout = thread_mgr_test_timeout;
	}

	/* indicate that a test is in progress */
	gen_mutex_lock(&bmi_test_mutex);
	bmi_test_flag = 1;
	gen_mutex_unlock(&bmi_test_mutex);
	
	incount = THREAD_MGR_TEST_COUNT;
	bmi_test_count = 0;

	ret = BMI_testcontext(incount, stat_bmi_id_array, &bmi_test_count,
	    stat_bmi_error_code_array, stat_bmi_actual_size_array,
	    stat_bmi_user_ptr_array, test_timeout, global_bmi_context);

	gen_mutex_lock(&bmi_test_mutex);
	bmi_test_flag = 0;
#ifdef __PVFS2_JOB_THREADED__
	pthread_cond_signal(&bmi_test_cond);
#endif
	gen_mutex_unlock(&bmi_test_mutex);

	if(ret < 0)
	{
	    /* critical error */
	    /* TODO: how to handle this */
	    assert(0);
	    gossip_lerr("Error: critical BMI failure.\n");
	}

	for(i=0; i<bmi_test_count; i++)
	{
	    /* execute a callback function for each completed BMI operation */
	    tmp_callback = 
		(struct PINT_thread_mgr_bmi_callback*)stat_bmi_user_ptr_array[i];
	    /* sanity check */
	    assert(tmp_callback != NULL);
	    assert(tmp_callback->fn != NULL);
	
	    tmp_callback->fn(tmp_callback->data, stat_bmi_actual_size_array[i],
		stat_bmi_error_code_array[i]);
	}
    }

    return (NULL);
}

/* dev_thread_function()
 *
 * function executed by the thread in charge of the device interface
 */
static void *dev_thread_function(void *ptr)
{
    int ret = -1;
    int incount, outcount;
    int i=0;
    int timeout = thread_mgr_test_timeout;

#ifdef __PVFS2_JOB_THREADED__
    while (dev_thread_running)
#endif
    {
	gen_mutex_lock(&dev_mutex);
	incount = dev_unexp_count;
	if(incount > THREAD_MGR_TEST_COUNT)
	    incount = THREAD_MGR_TEST_COUNT;
	gen_mutex_unlock(&dev_mutex);

	ret = PINT_dev_test_unexpected(incount, &outcount,
	    stat_dev_unexp_array, timeout);
	if(ret < 0)
	{
	    /* critical failure */
	    /* TODO: how to handle this? */
	    gossip_lerr("Error: critical device failure.\n");
	    assert(0);
	}

	/* execute callback function for each completed unexpected message */
	gen_mutex_lock(&dev_mutex);
	for(i=0; i<outcount; i++)
	{
	    dev_unexp_fn(&stat_dev_unexp_array[i]);
	    dev_unexp_count--;
	}
	gen_mutex_unlock(&dev_mutex);
    }

    return (NULL);
}


/* PINT_thread_mgr_dev_start()
 *
 * starts a dev mgmt thread, if not already running
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_thread_mgr_dev_start(void)
{
    int ret;

    ret = -1;

    gen_mutex_lock(&dev_mutex);
    if(dev_thread_ref_count > 0)
    {
	/* nothing to do, thread is already started.  Just increment 
	 * reference count and return
	 */
	dev_thread_ref_count++;
	gen_mutex_unlock(&dev_mutex);
	return(0);
    }

    dev_thread_running = 1;
#ifdef __PVFS2_JOB_THREADED__
    ret = pthread_create(&dev_thread_id, NULL, dev_thread_function, NULL);
    if(ret != 0)
    {
	gen_mutex_unlock(&dev_mutex);
	dev_thread_running = 0;
	/* TODO: convert error code */
	return(-ret);
    }
#endif
    dev_thread_ref_count++;

    gen_mutex_unlock(&dev_mutex);
    return(0);
}



/* PINT_thread_mgr_trove_start()
 *
 * starts a trove mgmt thread, if not already running
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_thread_mgr_trove_start(void)
{
    int ret = -1;

    gen_mutex_lock(&trove_mutex);
    if(trove_thread_ref_count > 0)
    {
	/* nothing to do, thread is already started.  Just increment 
	 * reference count and return
	 */
	trove_thread_ref_count++;
	gen_mutex_unlock(&trove_mutex);
	return(0);
    }

#ifdef __PVFS2_TROVE_SUPPORT__
    /* if we reach this point, then we have to start the thread ourselves */
    ret = trove_open_context(HACK_fs_id, &global_trove_context);
    if(ret < 0)
    {
	gen_mutex_unlock(&trove_mutex);
	return(ret);
    }
#else
    ret = 0;
    assert(0);
#endif

    trove_thread_running = 1;
#ifdef __PVFS2_JOB_THREADED__
    ret = pthread_create(&trove_thread_id, NULL, trove_thread_function, NULL);
    if(ret != 0)
    {
	trove_close_context(HACK_fs_id, global_trove_context);
	gen_mutex_unlock(&trove_mutex);
	trove_thread_running = 0;
	/* TODO: convert error code */
	return(-ret);
    }
#endif
    trove_thread_ref_count++;

    gen_mutex_unlock(&trove_mutex);
    return(0);
}


/* PINT_thread_mgr_bmi_start()
 *
 * starts a BMI mgmt thread, if not already running
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_thread_mgr_bmi_start(void)
{
    int ret = -1;

    gen_mutex_lock(&bmi_mutex);
    if(bmi_thread_ref_count > 0)
    {
	/* nothing to do, thread is already started.  Just increment 
	 * reference count and return
	 */
	bmi_thread_ref_count++;
	gen_mutex_unlock(&bmi_mutex);
	return(0);
    }

    /* if we reach this point, then we have to start the thread ourselves */
    ret = BMI_open_context(&global_bmi_context);
    if(ret < 0)
    {
	gen_mutex_unlock(&bmi_mutex);
	return(ret);
    }

    bmi_thread_running = 1;
#ifdef __PVFS2_JOB_THREADED__
    ret = pthread_create(&bmi_thread_id, NULL, bmi_thread_function, NULL);
    if(ret != 0)
    {
	BMI_close_context(global_bmi_context);
	gen_mutex_unlock(&bmi_mutex);
	bmi_thread_running = 0;
	/* TODO: convert error code */
	return(-ret);
    }
#endif
    bmi_thread_ref_count++;

    gen_mutex_unlock(&bmi_mutex);
    return(0);
}

/* PINT_thread_mgr_dev_stop()
 *
 * stops a Trove mgmt thread
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_thread_mgr_dev_stop(void)
{
    gen_mutex_lock(&dev_mutex);
    dev_thread_ref_count--;
    if(dev_thread_ref_count <= 0)
    {
	assert(dev_thread_ref_count == 0); /* sanity check */
	dev_thread_running = 0;
#ifdef __PVFS2_JOB_THREADED__
        gen_mutex_unlock(&dev_mutex);
	pthread_join(dev_thread_id, NULL);
        gen_mutex_lock(&dev_mutex);
#endif
    }
    gen_mutex_unlock(&dev_mutex);

    return(0);
}

/* PINT_thread_mgr_bmi_cancel()
 *
 * cancels a pending BMI operation for which the callback function has not
 * yet been executed
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_thread_mgr_bmi_cancel(PVFS_id_gen_t id, void* user_ptr)
{
    int i;
    int ret;

    /* wait until we can guarantee that a BMI_testcontext() is not in
     * progress
     */
    gen_mutex_lock(&bmi_test_mutex);
    while(bmi_test_flag == 1)
    {
#ifdef __PVFS2_JOB_THREADED__
	pthread_cond_wait(&bmi_test_cond, &bmi_test_mutex);
#else
	/* this condition shouldn't be possible without threads */
	assert(0);
#endif
    }

    /* iterate down list of pending completions, to see if the caller is
     * trying to cancel one of them
     */
#if 0
    gossip_err("THREAD MGR trying to cancel op: %Lu, ptr: %p.\n",
	Lu(id), user_ptr);
#endif
    for(i=0; i<bmi_test_count; i++)
    {
#if 0
	gossip_err("THREAD MGR bmi cancel scanning op: %Lu.\n", 
	    Lu(stat_bmi_id_array[i]));
#endif
	if(stat_bmi_id_array[i] == id && stat_bmi_user_ptr_array[i] ==
	    user_ptr)
	{
#if 0
	    gossip_err("THREAD MGR bmi cancel SKIPPING op: %Lu.\n", 
		Lu(stat_bmi_id_array[i]));
#endif
	    /* match; no steps needed to cancel, the op is already done */
	    gen_mutex_unlock(&bmi_test_mutex);
	    return(0);
	}
    }

    /* tell BMI to cancel the operation */
    ret = BMI_cancel(id, global_bmi_context);
    if(ret < 0)
	gossip_err("WARNING: BMI cancel failed, proceeding anyway.\n");
    gen_mutex_unlock(&bmi_test_mutex);
    return(ret);
}

/* PINT_thread_mgr_trove_stop()
 *
 * stops a Trove mgmt thread
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_thread_mgr_trove_stop(void)
{
    gen_mutex_lock(&trove_mutex);
    trove_thread_ref_count--;
    if(trove_thread_ref_count <= 0)
    {
	assert(trove_thread_ref_count == 0); /* sanity check */
	trove_thread_running = 0;
#ifdef __PVFS2_JOB_THREADED__
	pthread_join(trove_thread_id, NULL);
#endif
#ifdef __PVFS2_TROVE_SUPPORT__
	trove_close_context(HACK_fs_id, global_trove_context);
#else
	assert(0);
#endif
    }
    gen_mutex_unlock(&trove_mutex);

    return(0);
}


/* PINT_thread_mgr_bmi_stop()
 *
 * stops a BMI mgmt thread, if not already running
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_thread_mgr_bmi_stop(void)
{
    gen_mutex_lock(&bmi_mutex);
    bmi_thread_ref_count--;
    if(bmi_thread_ref_count <= 0)
    {
	assert(bmi_thread_ref_count == 0); /* sanity check */
	bmi_thread_running = 0;
#ifdef __PVFS2_JOB_THREADED__
	pthread_join(bmi_thread_id, NULL);
#endif
	BMI_close_context(global_bmi_context);
    }
    gen_mutex_unlock(&bmi_mutex);

    return(0);
}

/* PINT_thread_mgr_trove_getcontext()
 *
 * retrieves the context that the current trove thread is using
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_thread_mgr_trove_getcontext(PVFS_context_id *context)
{
    gen_mutex_lock(&trove_mutex);
    if(trove_thread_ref_count > 0)
    {
	*context = global_trove_context;
	gen_mutex_unlock(&trove_mutex);
	return(0);
    }
    gen_mutex_unlock(&trove_mutex);

    return(-PVFS_EINVAL);
}

/* PINT_thread_mgr_trove_cancel()
 *
 * cancels a pending Trove operation for which the callback function has not
 * yet been executed
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_thread_mgr_trove_cancel(PVFS_id_gen_t id, PVFS_fs_id fs_id,
    void* user_ptr)
{
    int i;
    int ret;

    /* wait until we can guarantee that a trove_testcontext() is not in
     * progress
     */
    gen_mutex_lock(&trove_test_mutex);
    while(trove_test_flag == 1)
    {
#ifdef __PVFS2_JOB_THREADED__
	pthread_cond_wait(&trove_test_cond, &trove_test_mutex);
#else
	/* this condition shouldn't be possible without threads */
	assert(0);
#endif
    }

    /* iterate down list of pending completions, to see if the caller is
     * trying to cancel one of them
     */
    for(i=0; i<trove_test_count; i++)
    {
#if 0
	gossip_err("THREAD MGR trove cancel scanning op: %Lu.\n", 
	    Lu(stat_trove_id_array[i]));
#endif
	if(stat_trove_id_array[i] == id && stat_trove_user_ptr_array[i] ==
	    user_ptr)
	{
#if 0
	    gossip_err("THREAD MGR trove cancel SKIPPING op: %Lu.\n", 
		Lu(stat_trove_id_array[i]));
#endif
	    /* match; no steps needed to cancel, the op is already done */
	    gen_mutex_unlock(&trove_test_mutex);
	    return(0);
	}
    }

    /* tell Trove to cancel the operation */
#ifdef __PVFS2_TROVE_SUPPORT__
    ret = trove_dspace_cancel(fs_id, id, global_trove_context);
#else
    ret = 0;
    assert(0);
#endif
    gen_mutex_unlock(&trove_test_mutex);
    return(ret);
}

/* PINT_thread_mgr_bmi_getcontext()
 *
 * retrieves the context that the current BMI thread is using
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_thread_mgr_bmi_getcontext(PVFS_context_id *context)
{
    gen_mutex_lock(&bmi_mutex);
    if(bmi_thread_ref_count > 0)
    {
	*context = global_bmi_context;
	gen_mutex_unlock(&bmi_mutex);
	return(0);
    }
    gen_mutex_unlock(&bmi_mutex);

    return(-PVFS_EINVAL);
}

/* PINT_thread_mgr_dev_unexp_handler()
 *
 * registers a handler for unexpected device messages
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_thread_mgr_dev_unexp_handler(
    void (*fn)(struct PINT_dev_unexp_info* unexp))
{
    /* sanity check */
    assert(fn != 0);

    gen_mutex_lock(&dev_mutex);
    if(dev_unexp_count > 0 && fn != dev_unexp_fn)
    {
	gossip_lerr("Error: dev_unexp_handler already set.\n");
	gen_mutex_unlock(&dev_mutex);
	return(-PVFS_EALREADY);
    }
    dev_unexp_fn = fn;
    dev_unexp_count++;
    gen_mutex_unlock(&dev_mutex);
    return(0);
}


/* PINT_thread_mgr_bmi_unexp_handler()
 *
 * registers a handler for unexpected BMI messages
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_thread_mgr_bmi_unexp_handler(
    void (*fn)(struct BMI_unexpected_info* unexp))
{
    /* sanity check */
    assert(fn != 0);

    gen_mutex_lock(&bmi_mutex);
    if(bmi_unexp_count > 0 && fn != bmi_unexp_fn)
    {
	gossip_lerr("Error: bmi_unexp_handler already set.\n");
	gen_mutex_unlock(&bmi_mutex);
	return(-PVFS_EALREADY);
    }
    bmi_unexp_fn = fn;
    bmi_unexp_count++;
    gen_mutex_unlock(&bmi_mutex);
    return(0);
}

/* PINT_thread_mgr_dev_push()
 *
 * pushes on test progress manually, without using threads 
 *
 * no return value 
 */
void PINT_thread_mgr_dev_push(int max_idle_time)
{
    thread_mgr_test_timeout = max_idle_time;
    dev_thread_function(NULL);
}


/* PINT_thread_mgr_trove_push()
 *
 * pushes on test progress manually, without using threads 
 *
 * no return value 
 */
void PINT_thread_mgr_trove_push(int max_idle_time)
{
    thread_mgr_test_timeout = max_idle_time;
    trove_thread_function(NULL);
}

/* PINT_thread_mgr_bmi_push()
 *
 * pushes on test progress manually, without using threads 
 *
 * no return value 
 */
void PINT_thread_mgr_bmi_push(int max_idle_time)
{
    thread_mgr_test_timeout = max_idle_time;
    bmi_thread_function(NULL);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
