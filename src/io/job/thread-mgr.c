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

#define THREAD_MGR_TEST_COUNT 5
#define THREAD_MGR_TEST_TIMEOUT 10

/* TODO: organize this stuff better */
static void *bmi_thread_function(void *ptr);
static pthread_t bmi_thread_id;
static struct BMI_unexpected_info stat_bmi_unexp_array[THREAD_MGR_TEST_COUNT];
static bmi_op_id_t stat_bmi_id_array[THREAD_MGR_TEST_COUNT];
static bmi_error_code_t stat_bmi_error_code_array[THREAD_MGR_TEST_COUNT];
static bmi_size_t stat_bmi_actual_size_array[THREAD_MGR_TEST_COUNT];
static void *stat_bmi_user_ptr_array[THREAD_MGR_TEST_COUNT];
static gen_mutex_t bmi_mutex = GEN_MUTEX_INITIALIZER;
static int bmi_unexp_count = 0;
static void (*bmi_unexp_fn)(struct BMI_unexpected_info* unexp);
static bmi_context_id global_bmi_context = -1;
static int bmi_thread_ref_count = 0;

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
    int test_timeout = THREAD_MGR_TEST_TIMEOUT;
    struct PINT_thread_mgr_bmi_callback *tmp_callback;

    /* TODO: fix the locking */

    while (1)
    {
	gen_mutex_lock(&bmi_mutex);
	if(bmi_unexp_count)
	{
	    incount = bmi_unexp_count;
	    if(incount > THREAD_MGR_TEST_COUNT)
		incount = THREAD_MGR_TEST_COUNT;

	    ret = BMI_testunexpected(incount, &outcount, stat_bmi_unexp_array, 0);
	    if(ret < 0)
	    {
		/* critical failure */
		/* TODO: how to handle this? */
		assert(0);
		gossip_lerr("Error: critical BMI failure.\n");
	    }

	    /* execute callback function for each completed unexpected message */
	    for(i=0; i<outcount; i++)
	    {
		bmi_unexp_fn(&stat_bmi_unexp_array[i]);
		bmi_unexp_count--;
	    }

	    /* set a flag if we are getting as many incoming BMI unexpected
	     * operations as we can handle to indicate that we should cycle
	     * quickly 
	     */
	    if(outcount == THREAD_MGR_TEST_COUNT)
		quick_flag = 1;
	}

	/* decide how long we are willing to wait on the main test call */
	if(quick_flag)
	{
	    quick_flag = 0;
	    test_timeout = 0;
	}
	else
	{
	    test_timeout = THREAD_MGR_TEST_TIMEOUT;
	}

	incount = THREAD_MGR_TEST_COUNT;
	ret = BMI_testcontext(incount, stat_bmi_id_array, &outcount,
	    stat_bmi_error_code_array, stat_bmi_actual_size_array,
	    stat_bmi_user_ptr_array, test_timeout, global_bmi_context);
	if(ret < 0)
	{
	    /* critical error */
	    /* TODO: how to handle this */
	    assert(0);
	    gossip_lerr("Error: critical BMI failure.\n");
	}

	for(i=0; i<outcount; i++)
	{
	    /* execute a callback function for each completed BMI operation */
	    tmp_callback = 
		(struct PINT_thread_mgr_bmi_callback*)stat_bmi_user_ptr_array[i];
	    /* sanity check */
	    assert(tmp_callback->fn != 0);
	    tmp_callback->fn(tmp_callback->data, stat_bmi_actual_size_array[i],
		stat_bmi_error_code_array[i]);
	}

	gen_mutex_unlock(&bmi_mutex);
    }

    return (NULL);
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

    ret = pthread_create(&bmi_thread_id, NULL, bmi_thread_function, NULL);
    if(ret != 0)
    {
	BMI_close_context(global_bmi_context);
	gen_mutex_unlock(&bmi_mutex);
	/* TODO: convert error code */
	return(-ret);
    }
    bmi_thread_ref_count++;

    gen_mutex_unlock(&bmi_mutex);
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
	pthread_cancel(bmi_thread_id);
	BMI_close_context(global_bmi_context);
    }
    gen_mutex_unlock(&bmi_mutex);

    return(0);
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

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
