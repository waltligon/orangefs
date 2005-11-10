/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup bmiint
 *
 *  Top-level BMI network interface routines.
 */

#include <errno.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>

#include "bmi.h"
#include "bmi-method-support.h"
#include "bmi-method-callback.h"
#include "gossip.h"
#include "reference-list.h"
#include "op-list.h"
#include "gen-locks.h"
#include "str-utils.h"
#include "id-generator.h"

/*
 * List of BMI addrs currently managed.
 */
static ref_list_p cur_ref_list = NULL;

/* array to keep up with active contexts */
static int context_array[BMI_MAX_CONTEXTS] = { 0 };
static gen_mutex_t context_mutex = GEN_MUTEX_INITIALIZER;
static gen_mutex_t ref_mutex = GEN_MUTEX_INITIALIZER;

/*
 * Static list of defined BMI methods.  These are pre-compiled into
 * the client libraries and into the server.
 */
#ifdef __STATIC_METHOD_BMI_TCP__
extern struct bmi_method_ops bmi_tcp_ops;
#endif
#ifdef __STATIC_METHOD_BMI_GM__
extern struct bmi_method_ops bmi_gm_ops;
#endif
#ifdef __STATIC_METHOD_BMI_IB__
extern struct bmi_method_ops bmi_ib_ops;
#endif

static struct bmi_method_ops *const static_methods[] = {
#ifdef __STATIC_METHOD_BMI_TCP__
    &bmi_tcp_ops,
#endif
#ifdef __STATIC_METHOD_BMI_GM__
    &bmi_gm_ops,
#endif
#ifdef __STATIC_METHOD_BMI_IB__
    &bmi_ib_ops,
#endif
    NULL
};

/*
 * List of "known" BMI methods.  This is dynamic, starting with
 * just the static ones above, and perhaps adding more if we turn
 * back on dynamic module loading.
 */
static int known_method_count = 0;
static struct bmi_method_ops **known_method_table = 0;

/*
 * List of active BMI methods.  These are the ones that will be
 * dealt with for a test call, for example.  On a client, known methods
 * become active only when someone calls BMI_addr_lookup().  On
 * a server, all possibly active methods are known at startup time
 * because we listen on them for the duration.
 */
static int active_method_count = 0;
static gen_mutex_t active_method_count_mutex = GEN_MUTEX_INITIALIZER;
static struct bmi_method_ops **active_method_table = NULL;
static struct {
    struct timeval active;
    struct timeval polled;
    int plan;
} *method_usage = NULL;

#ifndef timersub
# define timersub(a, b, result) \
  do { \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec; \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
    if ((result)->tv_usec < 0) { \
      --(result)->tv_sec; \
      (result)->tv_usec += 1000000; \
    } \
  } while (0)
#endif

static int activate_method(const char *name, const char *listen_addr,
    int flags);

/** Initializes the BMI layer.  Must be called before any other BMI
 *  functions.
 *
 *  \param method_list a comma separated list of BMI methods to
 *         use
 *  \param listen_addr a comma separated list of addresses to listen on
 *         for each method (if needed)
 *  \param flags initialization flags
 *
 *  \return 0 on success, -errno on failure
 */
int BMI_initialize(const char *method_list,
		   const char *listen_addr,
		   int flags)
{
    int ret = -1;
    int i = 0;
    char **requested_methods = NULL;

    /* server must specify method list at startup, optional for client */
    if (flags & BMI_INIT_SERVER) {
	if (!listen_addr || !method_list)
	    return bmi_errno_to_pvfs(-EINVAL);
    } else {
	if (listen_addr)
	    return bmi_errno_to_pvfs(-EINVAL);
	if (flags) {
	    gossip_lerr("Warning: flags ignored on client.\n");
	}
    }

    /* make a new reference list */
    cur_ref_list = ref_list_new();
    if (!cur_ref_list)
    {
	ret = bmi_errno_to_pvfs(-ENOMEM);
	goto bmi_initialize_failure;
    }

    /* initialize the known method list from the null-terminated static list */
    known_method_count = sizeof(static_methods) / sizeof(static_methods[0]) - 1;
    known_method_table = malloc(
	known_method_count * sizeof(*known_method_table));
    if (!known_method_table)
	return bmi_errno_to_pvfs(-ENOMEM);
    memcpy(known_method_table, static_methods,
	known_method_count * sizeof(*known_method_table));

    gen_mutex_lock(&active_method_count_mutex);
    if (!method_list) {
	/* nothing active until lookup */
	active_method_count = 0;
    } else {
	/* split and initialize the requested method list */
	int numreq = PINT_split_string_list(&requested_methods, method_list);
	if (numreq < 1)
	{
	    gossip_lerr("Error: bad method list.\n");
	    ret = bmi_errno_to_pvfs(-EINVAL);
	    gen_mutex_unlock(&active_method_count_mutex);
	    goto bmi_initialize_failure;
	}

	/*
	 * XXX: the same listen_addr is obviously not going to work on
	 * all the requested methods, though.  Figure out how to deal with
	 * this someday.
	 */
	for (i=0; i<numreq; i++) {
	    ret = activate_method(requested_methods[i], listen_addr, flags);
	    if (ret < 0) {
		ret = bmi_errno_to_pvfs(ret);
		gen_mutex_unlock(&active_method_count_mutex);
		goto bmi_initialize_failure;
	    }
	    free(requested_methods[i]);
	}
	free(requested_methods);
    }
    gen_mutex_unlock(&active_method_count_mutex);

    return (0);

  bmi_initialize_failure:

    /* kill reference list */
    if (cur_ref_list)
    {
	ref_list_cleanup(cur_ref_list);
    }

    gen_mutex_lock(&active_method_count_mutex);
    /* look for loaded methods and shut down */
    if (active_method_table)
    {
	for (i = 0; i < active_method_count; i++)
	{
	    if (active_method_table[i])
	    {
		active_method_table[i]->BMI_meth_finalize();
	    }
	}
	free(active_method_table);
    }

    if (known_method_table) {
	free(known_method_table);
	known_method_count = 0;
    }

    /* get rid of method string list */
    if (requested_methods)
    {
	for (i = 0; i < active_method_count; i++)
	{
	    if (requested_methods[i])
	    {
		free(requested_methods[i]);
	    }
	}
	free(requested_methods);
    }
    active_method_count = 0;
    gen_mutex_unlock(&active_method_count_mutex);

    return (ret);
}

/* the following is the old BMI_initialize() function that used dl to
 * pull in method modules dynamically.  Just hanging around as an
 * example...
 */
#if 0
/* BMI_initialize()
 * 
 * Initializes the BMI layer.  Must be called before any other BMI
 * functions.  module_string is a comma separated list of BMI modules to
 * use, listen_addr is a comma separated list of addresses to listen on
 * for each module (if needed), and flags are initialization flags.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_initialize(const char *module_string,
		   const char *listen_addr,
		   int flags)
{

    int ret = -1;
    int i = 0;
    char **modules = NULL;
    void *meth_mod = NULL;
    char *mod_error = NULL;
    method_addr_p new_addr = NULL;
    op_list_p olp = NULL;

    /* TODO: this is a hack to make sure we get all of the symbols loaded
     * into the library... is there a better way?
     */
    olp = op_list_new();
    op_list_cleanup(olp);

    if (((flags & BMI_INIT_SERVER) && (!listen_addr)) || !module_string)
    {
	return (bmi_errno_to_pvfs(-EINVAL));
    }

    /* separate out the module list */
    active_method_count = PINT_split_string_list(
        &modules, module_string);
    if (active_method_count < 1)
    {
	gossip_lerr("Error: bad module list.\n");
	ret = bmi_errno_to_pvfs(-EINVAL);
	goto bmi_initialize_failure;
    }

    /* create a table to keep up with the method modules */
    active_method_table = (struct bmi_method_ops **)malloc(
        active_method_count * sizeof(struct bmi_method_ops *));
    if (!active_method_table)
    {
	ret = bmi_errno_to_pvfs(-ENOMEM);
	goto bmi_initialize_failure;
    }

    /* iterate through each method in the list and load its module */
    for (i = 0; i < active_method_count; i++)
    {
	meth_mod = dlopen(modules[i], RTLD_NOW);
	if (!meth_mod)
	{
	    gossip_lerr("Error: could not open module: %s\n", dlerror());
	    ret = bmi_errno_to_pvfs(-EINVAL);
	    goto bmi_initialize_failure;
	}
	dlerror();

	active_method_table[i] = (struct bmi_method_ops *)
            dlsym(meth_mod, "method_interface");
	mod_error = dlerror();
	if (mod_error)
	{
	    gossip_lerr("Error: module load: %s\n", mod_error);
	    ret = bmi_errno_to_pvfs(-EINVAL);
	    goto bmi_initialize_failure;
	}
    }

    /* make a new reference list */
    cur_ref_list = ref_list_new();
    if (!cur_ref_list)
    {
	ret = bmi_errno_to_pvfs(-ENOMEM);
	goto bmi_initialize_failure;
    }

    /* initialize methods */
    for (i = 0; i < active_method_count; i++)
    {
	if (flags & BMI_INIT_SERVER)
	{
	    if ((new_addr =
		 active_method_table[i]->
		 BMI_meth_method_addr_lookup(listen_addr)) != NULL)
	    {
		/* this is a bit of a hack */
		new_addr->method_type = i;
		ret = active_method_table[i]->BMI_meth_initialize(
                    new_addr, i, flags);
	    }
	    else
	    {
		ret = -1;
	    }
	}
	else
	{
	    ret = active_method_table[i]->BMI_meth_initialize(
                NULL, i, flags);
	}
	if (ret < 0)
	{
	    gossip_lerr("Error: initializing module: %s\n", modules[i]);
	    goto bmi_initialize_failure;
	}
    }

    return (0);

  bmi_initialize_failure:

    /* kill reference list */
    if (cur_ref_list)
    {
	ref_list_cleanup(cur_ref_list);
    }

    /* look for loaded methods and shut down */
    if (active_method_table)
    {
	for (i = 0; i < active_method_count; i++)
	{
	    if (active_method_table[i])
	    {
		active_method_table[i]->BMI_meth_finalize();
	    }
	}
	free(active_method_table);
    }

    /* get rid of method string list */
    if (modules)
    {
	for (i = 0; i < active_method_count; i++)
	{
	    if (modules[i])
	    {
		free(modules[i]);
	    }
	}
	free(modules);
    }

    return (ret);
}
#endif /* 0 */

/** Shuts down the BMI layer.
 *
 * \return 0.
 */
int BMI_finalize(void)
{
    int i = -1;

    gen_mutex_lock(&active_method_count_mutex);
    /* attempt to shut down active methods */
    for (i = 0; i < active_method_count; i++)
    {
	active_method_table[i]->BMI_meth_finalize();
    }
    active_method_count = 0;
    free(active_method_table);
    gen_mutex_unlock(&active_method_count_mutex);

    free(known_method_table);
    known_method_count = 0;

    /* destroy the reference list */
    /* (side effect: destroys all method addresses as well) */
    ref_list_cleanup(cur_ref_list);

    return (0);
}

/** Creates a new context to be used for communication.  This can be
 *  used, for example, to distinguish between operations posted by
 *  different threads.
 *
 *  \return 0 on success, -errno on failure.
 */
int BMI_open_context(bmi_context_id* context_id)
{
    int context_index;
    int i,j;
    int ret = 0;

    gen_mutex_lock(&context_mutex);

    /* find an unused context id */
    for(context_index=0; context_index<BMI_MAX_CONTEXTS; context_index++)
    {
	if(context_array[context_index] == 0)
	{
	    break;
	}
    }

    if(context_index >= BMI_MAX_CONTEXTS)
    {
	/* we don't have any more available! */
	gen_mutex_unlock(&context_mutex);
	return(bmi_errno_to_pvfs(-EBUSY));
    }

    gen_mutex_lock(&active_method_count_mutex);
    /* tell all of the modules about the new context */
    for (i = 0; i < active_method_count; i++)
    {
	ret = active_method_table[i]->BMI_meth_open_context(
            context_index);
	if(ret < 0)
	{
	    /*
              one of them failed; kill this context in the previous
              modules
            */
	    for(j=(i-1); i>-1; i++)
	    {
		active_method_table[i]->BMI_meth_close_context(
                    context_index);
	    }
	    goto out;
	}
    }
    gen_mutex_unlock(&active_method_count_mutex);

    context_array[context_index] = 1;
    *context_id = context_index;

out:

    gen_mutex_unlock(&context_mutex);
    return(ret);
}


/** Destroys a context previous generated with BMI_open_context().
 */
void BMI_close_context(bmi_context_id context_id)
{
    int i;

    gen_mutex_lock(&context_mutex);

    if(!context_array[context_id])
    {
	gen_mutex_unlock(&context_mutex);
	return;
    }

    /* tell all of the modules to get rid of this context */
    gen_mutex_lock(&active_method_count_mutex);
    for (i = 0; i < active_method_count; i++)
    {
	active_method_table[i]->BMI_meth_close_context(context_id);
    }
    context_array[context_id] = 0;
    gen_mutex_unlock(&active_method_count_mutex);

    gen_mutex_unlock(&context_mutex);
    return;
}


/** Submits receive operations for subsequent service.
 *
 *  \return 0 on success, -errno on failure.
 */
int BMI_post_recv(bmi_op_id_t * id,
		  PVFS_BMI_addr_t src,
		  void *buffer,
		  bmi_size_t expected_size,
		  bmi_size_t * actual_size,
		  enum bmi_buffer_type buffer_type,
		  bmi_msg_tag_t tag,
		  void *user_ptr,
		  bmi_context_id context_id)
{
    ref_st_p tmp_ref = NULL;
    int ret = -1;

    gossip_debug(GOSSIP_BMI_DEBUG_OFFSETS,
                 "BMI_post_recv: addr: %ld, offset: 0x%lx, size: %ld\n",
                 (long)src, (long)buffer, (long)expected_size);

    *id = 0;

    gen_mutex_lock(&ref_mutex);
    tmp_ref = ref_list_search_addr(cur_ref_list, src);
    if (!tmp_ref)
    {
	gen_mutex_unlock(&ref_mutex);
	return (bmi_errno_to_pvfs(-EPROTO));
    }
    gen_mutex_unlock(&ref_mutex);

    ret = tmp_ref->interface->BMI_meth_post_recv(
        id, tmp_ref->method_addr, buffer, expected_size, actual_size,
        buffer_type, tag, user_ptr, context_id);
    return (ret);
}


/** Submits send operations for subsequent service.
 *
 *  \return 0 on success, -errno on failure.
 */
int BMI_post_send(bmi_op_id_t * id,
		  PVFS_BMI_addr_t dest,
		  const void *buffer,
		  bmi_size_t size,
		  enum bmi_buffer_type buffer_type,
		  bmi_msg_tag_t tag,
		  void *user_ptr,
		  bmi_context_id context_id)
{
    ref_st_p tmp_ref = NULL;
    int ret = -1;

    gossip_debug(GOSSIP_BMI_DEBUG_OFFSETS,
                 "BMI_post_send: addr: %ld, offset: 0x%lx, size: %ld\n",
                 (long)dest, (long)buffer, (long)size);

    *id = 0;

    gen_mutex_lock(&ref_mutex);
    tmp_ref = ref_list_search_addr(cur_ref_list, dest);
    if (!tmp_ref)
    {
	gen_mutex_unlock(&ref_mutex);
	return (bmi_errno_to_pvfs(-EPROTO));
    }
    gen_mutex_unlock(&ref_mutex);

    ret = tmp_ref->interface->BMI_meth_post_send(
        id, tmp_ref->method_addr, buffer, size, buffer_type, tag,
        user_ptr, context_id);
    return (ret);
}


/** Submits unexpected send operations for subsequent service.
 *
 *  \return 0 on success, -errno on failure.
 */
int BMI_post_sendunexpected(bmi_op_id_t * id,
			    PVFS_BMI_addr_t dest,
			    const void *buffer,
			    bmi_size_t size,
			    enum bmi_buffer_type buffer_type,
			    bmi_msg_tag_t tag,
			    void *user_ptr,
			    bmi_context_id context_id)
{
    ref_st_p tmp_ref = NULL;
    int ret = -1;

    gossip_debug(GOSSIP_BMI_DEBUG_OFFSETS,
	"BMI_post_sendunexpected: addr: %ld, offset: 0x%lx, size: %ld\n", 
	(long)dest, (long)buffer, (long)size);

    *id = 0;

    gen_mutex_lock(&ref_mutex);
    tmp_ref = ref_list_search_addr(cur_ref_list, dest);
    if (!tmp_ref)
    {
	gen_mutex_unlock(&ref_mutex);
	return (bmi_errno_to_pvfs(-EPROTO));
    }
    gen_mutex_unlock(&ref_mutex);

    ret = tmp_ref->interface->BMI_meth_post_sendunexpected(
        id, tmp_ref->method_addr, buffer, size, buffer_type, tag,
        user_ptr, context_id);
    return (ret);
}


/** Checks to see if a particular message has completed.
 *
 *  \return 0 on success, -errno on failure.
 */
int BMI_test(bmi_op_id_t id,
	     int *outcount,
	     bmi_error_code_t * error_code,
	     bmi_size_t * actual_size,
	     void **user_ptr,
	     int max_idle_time_ms,
	     bmi_context_id context_id)
{
    struct method_op *target_op = NULL;
    int ret = -1;

    if (max_idle_time_ms < 0)
	return (bmi_errno_to_pvfs(-EINVAL));

    *outcount = 0;

    target_op = id_gen_safe_lookup(id);
    assert(target_op->op_id == id);

    ret = active_method_table[
        target_op->addr->method_type]->BMI_meth_test(
            id, outcount, error_code, actual_size, user_ptr,
            max_idle_time_ms, context_id);

    /* return 1 if anything completed */
    if (ret == 0 && *outcount == 1)
    {
	gossip_debug(GOSSIP_BMI_DEBUG_CONTROL,
                     "BMI_test completing: %llu\n", llu(id));
	return (1);
    }
    return (ret);
}


/** Checks to see if any messages from the specified list have completed.
 *
 * \return 0 on success, -errno on failure.
 *
 * XXX: never used.  May want to add adaptive polling strategy of testcontext
 * if it becomes used again.
 */
int BMI_testsome(int incount,
		 bmi_op_id_t * id_array,
		 int *outcount,
		 int *index_array,
		 bmi_error_code_t * error_code_array,
		 bmi_size_t * actual_size_array,
		 void **user_ptr_array,
		 int max_idle_time_ms,
		 bmi_context_id context_id)
{
    int ret = 0;
    int idle_per_method = 0;
    bmi_op_id_t* tmp_id_array;
    int i,j;
    struct method_op *query_op;
    int need_to_test;
    int tmp_outcount;
    int tmp_active_method_count;

    gen_mutex_lock(&active_method_count_mutex);
    tmp_active_method_count = active_method_count;
    gen_mutex_unlock(&active_method_count_mutex);

    if (max_idle_time_ms < 0)
	return (bmi_errno_to_pvfs(-EINVAL));

    *outcount = 0;

    if (tmp_active_method_count == 1) {
	/* shortcircuit for perhaps common case of only one method */
	ret = active_method_table[0]->BMI_meth_testsome(
	    incount, id_array, outcount, index_array,
	    error_code_array, actual_size_array, user_ptr_array,
	    max_idle_time_ms, context_id);

	/* return 1 if anything completed */
	if (ret == 0 && *outcount > 0)
	    return (1);
	else
	    return ret;
    }

    /* TODO: do something more clever here */
    if (max_idle_time_ms)
    {
	idle_per_method = max_idle_time_ms / tmp_active_method_count;
	if (!idle_per_method)
	    idle_per_method = 1;
    }

    tmp_id_array = (bmi_op_id_t*)malloc(incount*sizeof(bmi_op_id_t));
    if(!tmp_id_array)
	return(bmi_errno_to_pvfs(-ENOMEM));

    /* iterate over each active method */
    for(i=0; i<tmp_active_method_count; i++)
    {
	/* setup the tmp id array with only operations that match
	 * that method
	 */
	need_to_test = 0;
	for(j=0; j<incount; j++)
	{
	    if(id_array[j])
	    {
		query_op = (struct method_op*)
                    id_gen_safe_lookup(id_array[j]);
		assert(query_op->op_id == id_array[j]);
		if(query_op->addr->method_type == i)
		{
		    tmp_id_array[j] = id_array[j];
		    need_to_test++;
		}
	    }
	}

	/* call testsome if we found any ops for this method */
	if(need_to_test)
	{
	    tmp_outcount = 0;
	    ret = active_method_table[i]->BMI_meth_testsome(
		need_to_test, tmp_id_array, &tmp_outcount, 
		&(index_array[*outcount]),
		&(error_code_array[*outcount]),
		&(actual_size_array[*outcount]),
		user_ptr_array ? &(user_ptr_array[*outcount]) : 0,
		idle_per_method,
		context_id);
	    if(ret < 0)
	    {
		/* can't recover from this... */
		gossip_lerr("Error: critical BMI_testsome failure.\n");
		goto out;
	    }
	    *outcount += tmp_outcount;
	}
    }

  out:
    free(tmp_id_array);

    if(ret == 0 && *outcount > 0)
	return(1);
    else
	return(0);
}


/*
 * If some method was recently active, poll it again for speed,
 * but be sure not to starve any method.  If multiple active,
 * poll them all.  Return idle_time per method too.
 */
static void
construct_poll_plan(int nmeth, int *idle_time_ms)
{
    struct timeval now, delta;
    int i, numplan;

    gettimeofday(&now, 0);
    numplan = 0;
    for (i=0; i<nmeth; i++) {
        method_usage[i].plan = 0;
        timersub(&now, &method_usage[i].polled, &delta);
        if (delta.tv_sec >= 1) {
            method_usage[i].plan = 1;  /* >= 1s starving */
            method_usage[i].polled = now;
            ++numplan;
        } else {
            timersub(&now, &method_usage[i].active, &delta);
            if (delta.tv_sec == 0) {
                method_usage[i].plan = 1;  /* < 1s busy, prefer poll */
                method_usage[i].polled = now;
                ++numplan;
            }
        } 
    }

    /* if nothing is starving or busy, poll everybody */
    if (numplan == 0) {
        for (i=0; i<nmeth; i++) {
            method_usage[i].plan = 1;
            method_usage[i].polled = now;
        }
        numplan = nmeth;
    }

    /* spread idle time evenly */
    if (*idle_time_ms)
    {
	*idle_time_ms /= numplan;
	if (!*idle_time_ms)
	    *idle_time_ms = 1;
    }
}


/** Checks to see if any unexpected messages have completed.
 *
 *  \return 0 on success, -errno on failure.
 */
int BMI_testunexpected(int incount,
		       int *outcount,
		       struct BMI_unexpected_info *info_array,
		       int max_idle_time_ms)
{
    int i = 0;
    int ret = -1;
    int position = 0;
    int tmp_outcount = 0;
    struct method_unexpected_info sub_info[incount];
    ref_st_p tmp_ref = NULL;
    int tmp_active_method_count = 0;

    gen_mutex_lock(&active_method_count_mutex);
    tmp_active_method_count = active_method_count;
    gen_mutex_unlock(&active_method_count_mutex);

    if (max_idle_time_ms < 0)
	return (bmi_errno_to_pvfs(-EINVAL));

    *outcount = 0;

    construct_poll_plan(tmp_active_method_count, &max_idle_time_ms);

    while (position < incount && i < tmp_active_method_count)
    {
        if (method_usage[i].plan) {
            ret = active_method_table[i]->BMI_meth_testunexpected(
                (incount - position), &tmp_outcount,
                (&(sub_info[position])), max_idle_time_ms);
            if (ret < 0)
            {
                /* can't recover from this */
                gossip_lerr("Error: critical BMI_testunexpected failure.\n");
                return (ret);
            }
            position += tmp_outcount;
            (*outcount) += tmp_outcount;
            if (tmp_outcount)
                gettimeofday(&method_usage[i].active, 0);
        }
	i++;
    }

    for (i = 0; i < (*outcount); i++)
    {
	info_array[i].error_code = sub_info[i].error_code;
	info_array[i].buffer = sub_info[i].buffer;
	info_array[i].size = sub_info[i].size;
	info_array[i].tag = sub_info[i].tag;
	gen_mutex_lock(&ref_mutex);
	tmp_ref = ref_list_search_method_addr(
            cur_ref_list, sub_info[i].addr);
	if (!tmp_ref)
	{
	    /* yeah, right */
	    gossip_lerr("Error: critical BMI_testunexpected failure.\n");
	    gen_mutex_unlock(&ref_mutex);
	    return (bmi_errno_to_pvfs(-EPROTO));
	}
	gen_mutex_unlock(&ref_mutex);
	info_array[i].addr = tmp_ref->bmi_addr;
    }
    /* return 1 if anything completed */
    if (ret == 0 && *outcount > 0)
    {
	return (1);
    }
    return (0);
}


/** Checks to see if any messages from the specified context have
 *  completed.
 *
 *  \returns 0 on success, -errno on failure.
 */
int BMI_testcontext(int incount,
		    bmi_op_id_t* out_id_array,
		    int *outcount,
		    bmi_error_code_t * error_code_array,
		    bmi_size_t * actual_size_array,
		    void **user_ptr_array,
		    int max_idle_time_ms,
		    bmi_context_id context_id)
{
    int i = 0;
    int ret = -1;
    int position = 0;
    int tmp_outcount = 0;
    int tmp_active_method_count = 0;
    struct timespec ts;

    gen_mutex_lock(&active_method_count_mutex);
    tmp_active_method_count = active_method_count;
    gen_mutex_unlock(&active_method_count_mutex);

    if (max_idle_time_ms < 0)
	return (bmi_errno_to_pvfs(-EINVAL));

    *outcount = 0;

    if(tmp_active_method_count < 1)
    {
	/* nothing active yet, just snooze and return */
	if(max_idle_time_ms > 0)
	{
	    ts.tv_sec = 0;
	    ts.tv_nsec = 2000;
	    nanosleep(&ts, NULL);
	}
	return(0);
    }

    construct_poll_plan(tmp_active_method_count, &max_idle_time_ms);

    while (position < incount && i < tmp_active_method_count)
    {
        if (method_usage[i].plan) {
            ret = active_method_table[i]->BMI_meth_testcontext(
                incount - position, 
                &out_id_array[position],
                &tmp_outcount,
                &error_code_array[position], 
                &actual_size_array[position],
                user_ptr_array ?  &user_ptr_array[position] : NULL,
                max_idle_time_ms,
                context_id);
            if (ret < 0)
            {
                /* can't recover from this */
                gossip_lerr("Error: critical BMI_testcontext failure.\n");
                return (ret);
            }
            position += tmp_outcount;
            (*outcount) += tmp_outcount;
            if (tmp_outcount)
                gettimeofday(&method_usage[i].active, 0);
        }
	i++;
    }

    /* return 1 if anything completed */
    if (ret == 0 && *outcount > 0)
    {
	for(i=0; i<*outcount; i++)
	{
	    gossip_debug(GOSSIP_BMI_DEBUG_CONTROL, 
		"BMI_testcontext completing: %llu\n", llu(out_id_array[i]));
	}
	return (1);
    }
    return (0);

}


/** Performs a reverse lookup, returning the string (URL style)
 *  address for a given opaque address.
 *
 *  NOTE: caller must not free or modify returned string
 *
 *  \return Pointer to string on success, NULL on failure.
 */
const char* BMI_addr_rev_lookup(PVFS_BMI_addr_t addr)
{
    ref_st_p tmp_ref = NULL;
    char* tmp_str = NULL;

    /* find a reference that matches this address */
    gen_mutex_lock(&ref_mutex);
    tmp_ref = ref_list_search_addr(cur_ref_list, addr);
    if (!tmp_ref)
    {
	gen_mutex_unlock(&ref_mutex);
	return (NULL);
    }
    gen_mutex_unlock(&ref_mutex);
    
    tmp_str = tmp_ref->id_string;

    return(tmp_str);
}

/** Performs a reverse lookup, returning a string
 *  address for a given opaque address.  Works on any address, even those
 *  generated unexpectedly, but only gives hostname instead of full
 *  BMI URL style address
 *
 *  NOTE: caller must not free or modify returned string
 *
 *  \return Pointer to string on success, NULL on failure.
 */
const char* BMI_addr_rev_lookup_unexpected(PVFS_BMI_addr_t addr)
{
    ref_st_p tmp_ref = NULL;

    /* find a reference that matches this address */
    gen_mutex_lock(&ref_mutex);
    tmp_ref = ref_list_search_addr(cur_ref_list, addr);
    if (!tmp_ref)
    {
	gen_mutex_unlock(&ref_mutex);
	return ("UNKNOWN");
    }
    gen_mutex_unlock(&ref_mutex);
    
    if(!tmp_ref->interface->BMI_meth_rev_lookup_unexpected)
    {
        return("UNKNOWN");
    }

    return(tmp_ref->interface->BMI_meth_rev_lookup_unexpected(
        tmp_ref->method_addr));
}


/** Allocates memory that can be used in native mode by the BMI layer.
 *
 *  \return Pointer to buffer on success, NULL on failure.
 */
void *BMI_memalloc(PVFS_BMI_addr_t addr,
		   bmi_size_t size,
		   enum bmi_op_type send_recv)
{
    void *new_buffer = NULL;
    ref_st_p tmp_ref = NULL;

    /* find a reference that matches this address */
    gen_mutex_lock(&ref_mutex);
    tmp_ref = ref_list_search_addr(cur_ref_list, addr);
    if (!tmp_ref)
    {
	gen_mutex_unlock(&ref_mutex);
	return (NULL);
    }
    gen_mutex_unlock(&ref_mutex);

    /* allocate the buffer using the method's mechanism */
    new_buffer = tmp_ref->interface->BMI_meth_memalloc(size, send_recv);

    return (new_buffer);
}

/** Frees memory that was allocated with BMI_memalloc().
 *
 *  \return 0 on success, -errno on failure.
 */
int BMI_memfree(PVFS_BMI_addr_t addr,
		void *buffer,
		bmi_size_t size,
		enum bmi_op_type send_recv)
{
    ref_st_p tmp_ref = NULL;
    int ret = -1;

    /* find a reference that matches this address */
    gen_mutex_lock(&ref_mutex);
    tmp_ref = ref_list_search_addr(cur_ref_list, addr);
    if (!tmp_ref)
    {
	gen_mutex_unlock(&ref_mutex);
	return (bmi_errno_to_pvfs(-EINVAL));
    }
    gen_mutex_unlock(&ref_mutex);

    /* free the memory */
    ret = tmp_ref->interface->BMI_meth_memfree(buffer, size, send_recv);

    return (ret);
}

/** Pass in optional parameters.
 *
 *  \return 0 on success, -errno on failure.
 */
int BMI_set_info(PVFS_BMI_addr_t addr,
		 int option,
		 void *inout_parameter)
{
    int ret = -1;
    int i = 0;
    ref_st_p tmp_ref = NULL;

    /* if the addr is NULL, then the set_info should apply to all
     * available methods.
     */
    if (!addr)
    {
	if (!active_method_table)
	{
	    return (bmi_errno_to_pvfs(-EINVAL));
	}
	gen_mutex_lock(&active_method_count_mutex);
	for (i = 0; i < active_method_count; i++)
	{
	    ret = active_method_table[i]->BMI_meth_set_info(
                option, inout_parameter);
	    /* we bail out if even a single set_info fails */
	    if (ret < 0)
	    {
		gossip_lerr(
                    "Error: failure on set_info to method: %d\n", i);
		gen_mutex_unlock(&active_method_count_mutex);
		return (ret);
	    }
	}
	gen_mutex_unlock(&active_method_count_mutex);
	return (0);
    }

    /* find a reference that matches this address */
    gen_mutex_lock(&ref_mutex);
    tmp_ref = ref_list_search_addr(cur_ref_list, addr);
    if (!tmp_ref)
    {
	gen_mutex_unlock(&ref_mutex);
	return (bmi_errno_to_pvfs(-EINVAL));
    }

    /* shortcut address reference counting */
    if(option == BMI_INC_ADDR_REF)
    {
	tmp_ref->ref_count++;
	gen_mutex_unlock(&ref_mutex);
	return(0);
    }
    if(option == BMI_DEC_ADDR_REF)
    {
	tmp_ref->ref_count--;
	assert(tmp_ref->ref_count >= 0);

	if(tmp_ref->ref_count == 0)
	{
	    struct method_drop_addr_query query;
	    query.response = 0;
	    query.addr = tmp_ref->method_addr;
	    /* reference count is zero; ask module if it wants us to discard
	     * the address; TCP will tell us to drop addresses for which the
	     * socket has died with no possibility of reconnect 
	     */
	    ret = tmp_ref->interface->BMI_meth_get_info(BMI_DROP_ADDR_QUERY,
		&query);
	    if(ret == 0 && query.response == 1)
	    {
		/* kill the address */
		gossip_debug(GOSSIP_BMI_DEBUG_CONTROL,
		    "bmi discarding address: %llu\n", llu(addr));
		ref_list_rem(cur_ref_list, addr);
		/* NOTE: this triggers request to module to free underlying
		 * resources if it wants to
		 */
		dealloc_ref_st(tmp_ref);
	    }
	}
	gen_mutex_unlock(&ref_mutex);
	return(0);
    }

    gen_mutex_unlock(&ref_mutex);

    ret = tmp_ref->interface->BMI_meth_set_info(option, inout_parameter);

    return (ret);
}

/** Query for optional parameters.
 *
 *  \return 0 on success, -errno on failure.
 */
int BMI_get_info(PVFS_BMI_addr_t addr,
		 int option,
		 void *inout_parameter)
{
    int i = 0;
    int maxsize = 0;
    int tmp_maxsize;
    int ret = 0;
    ref_st_p tmp_ref = NULL;

    switch (option)
    {
	/* check to see if the interface is initialized */
    case BMI_CHECK_INIT:
	gen_mutex_lock(&active_method_count_mutex);
	if (active_method_count > 0)
	{
	    gen_mutex_unlock(&active_method_count_mutex);
	    return (0);
	}
	else
	{
	    gen_mutex_unlock(&active_method_count_mutex);
	    return (bmi_errno_to_pvfs(-ENETDOWN));
	}
	break;
    case BMI_CHECK_MAXSIZE:
	gen_mutex_lock(&active_method_count_mutex);
	for (i = 0; i < active_method_count; i++)
	{
	    ret = active_method_table[i]->BMI_meth_get_info(
                option, &tmp_maxsize);
	    if (ret < 0)
	    {
		return (ret);
	    }
	    if (i == 0)
	    {
		maxsize = tmp_maxsize;
	    }
	    else
	    {
		if (tmp_maxsize < maxsize)
		    maxsize = tmp_maxsize;
	    }
	    *((int *) inout_parameter) = maxsize;
	}
	gen_mutex_unlock(&active_method_count_mutex);
	break;
    case BMI_GET_METH_ADDR:
	gen_mutex_lock(&ref_mutex);
	tmp_ref = ref_list_search_addr(cur_ref_list, addr);
	if(!tmp_ref)
	{
	    gen_mutex_unlock(&ref_mutex);
	    return (bmi_errno_to_pvfs(-EINVAL));
	}
	gen_mutex_unlock(&ref_mutex);
	*((void**) inout_parameter) = tmp_ref->method_addr;
	break;
    default:
	return (bmi_errno_to_pvfs(-ENOSYS));
	break;
    }
    return (0);
}

/** Resolves the string representation of a host address into a BMI
 *  address handle.
 *
 *  \return 0 on success, -errno on failure.
 */
int BMI_addr_lookup(PVFS_BMI_addr_t * new_addr,
		    const char *id_string)
{

    ref_st_p new_ref = NULL;
    method_addr_p meth_addr = NULL;
    int ret = -1;
    int i = 0;
    int failed;

    if((strlen(id_string)+1) > BMI_MAX_ADDR_LEN)
    {
	return(bmi_errno_to_pvfs(-ENAMETOOLONG));
    }

    /* set the addr to zero in case we fail */
    *new_addr = 0;

    /* First we want to check to see if this host has already been
     * discovered! */
    gen_mutex_lock(&ref_mutex);
    new_ref = ref_list_search_str(cur_ref_list, id_string);
    gen_mutex_unlock(&ref_mutex);

    if (new_ref)
    {
	/* we found it. */
	*new_addr = new_ref->bmi_addr;
	return (0);
    }

    /* Now we will run through each method looking for one that
     * responds successfully.  It is assumed that they are already
     * listed in order of preference
     */
    i = 0;
    gen_mutex_lock(&active_method_count_mutex);
    while ((i < active_method_count) &&
           !(meth_addr = active_method_table[i]->
             BMI_meth_method_addr_lookup(id_string)))
    {
	i++;
    }

    /* if not found, try to bring it up now */
    failed = 0;
    if (!meth_addr) {
	for (i=0; i<known_method_count; i++) {
	    const char *name;
	    /* only bother with those not active */
	    int j;
	    for (j=0; j<active_method_count; j++)
		if (known_method_table[i] == active_method_table[j])
		    break;
	    if (j < active_method_count)
		continue;

	    /* well-known that mapping is "x" -> "bmi_x" */
	    name = known_method_table[i]->method_name + 4;
	    if (!strncmp(id_string, name, strlen(name))) {
	        ret = activate_method(known_method_table[i]->method_name, 0, 0);
	        if (ret < 0) {
                    failed = 1;
                    break;
                }
		meth_addr = known_method_table[i]->
		    BMI_meth_method_addr_lookup(id_string);
		i = active_method_count - 1;  /* point at the new one */
		break;
	    }
	}
    }
    gen_mutex_unlock(&active_method_count_mutex);
    if (failed)
        return bmi_errno_to_pvfs(ret);

    /* make sure one was successful */
    if (!meth_addr)
    {
	return (bmi_errno_to_pvfs(-ENOPROTOOPT));
    }

    /* create a new reference for the addr */
    new_ref = alloc_ref_st();
    if (!new_ref)
    {
	ret = bmi_errno_to_pvfs(-ENOMEM);
	goto bmi_addr_lookup_failure;
    }

    /* fill in the details */
    new_ref->method_addr = meth_addr;
    new_ref->id_string = (char *) malloc(strlen(id_string) + 1);
    if (!new_ref->id_string)
    {
	ret = -errno;
	goto bmi_addr_lookup_failure;
    }
    strcpy(new_ref->id_string, id_string);
    new_ref->interface = active_method_table[i];

    /* keep up with the reference and we are done */
    gen_mutex_lock(&ref_mutex);
    ref_list_add(cur_ref_list, new_ref);
    gen_mutex_unlock(&ref_mutex);

    *new_addr = new_ref->bmi_addr;
    return (0);

  bmi_addr_lookup_failure:

    if (meth_addr)
    {
	active_method_table[i]->BMI_meth_set_info(
            BMI_DROP_ADDR, meth_addr);
    }

    if (new_ref)
    {
	dealloc_ref_st(new_ref);
    }

    return (ret);
}


/** Similar to BMI_post_send(), except that the source buffer is 
 *  replaced by a list of (possibly non contiguous) buffers.
 *
 *  \return 0 on success, 1 on immediate successful completion,
 *  -errno on failure.
 */
int BMI_post_send_list(bmi_op_id_t * id,
		       PVFS_BMI_addr_t dest,
		       const void *const *buffer_list,
		       const bmi_size_t *size_list,
		       int list_count,
		       /* "total_size" is the sum of the size list */
		       bmi_size_t total_size,
		       enum bmi_buffer_type buffer_type,
		       bmi_msg_tag_t tag,
		       void *user_ptr,
		       bmi_context_id context_id)
{
    ref_st_p tmp_ref = NULL;
    int ret = -1;

#ifndef GOSSIP_DISABLE_DEBUG
    int i;

    gossip_debug(GOSSIP_BMI_DEBUG_OFFSETS,
	"BMI_post_send_list: addr: %ld, count: %d, total_size: %ld\n", 
	(long)dest, list_count, (long)total_size);

    for(i=0; i<list_count; i++)
    {
	gossip_debug(GOSSIP_BMI_DEBUG_OFFSETS,
	    "   element %d: offset: 0x%lx, size: %ld\n",
	    i, (long)buffer_list[i], (long)size_list[i]);
    }
#endif

    *id = 0;

    gen_mutex_lock(&ref_mutex);
    tmp_ref = ref_list_search_addr(cur_ref_list, dest);
    if (!tmp_ref)
    {
	gen_mutex_unlock(&ref_mutex);
	return (bmi_errno_to_pvfs(-EPROTO));
    }
    gen_mutex_unlock(&ref_mutex);

    if (tmp_ref->interface->BMI_meth_post_send_list)
    {
	ret = tmp_ref->interface->BMI_meth_post_send_list(
            id, tmp_ref->method_addr, buffer_list, size_list,
            list_count, total_size, buffer_type, tag, user_ptr,
            context_id);

	return (ret);
    }

    gossip_lerr("Error: method doesn't implement send_list.\n");
    gossip_lerr("Error: send_list emulation not yet available.\n");

    return (bmi_errno_to_pvfs(-ENOSYS));
}


/** Similar to BMI_post_recv(), except that the dest buffer is 
 *  replaced by a list of (possibly non contiguous) buffers
 *
 *  \param total_expected_size the sum of the size list.
 *  \param total_actual_size the aggregate amt that was received.
 *
 *  \return 0 on success, 1 on immediate successful completion,
 *  -errno on failure.
 */
int BMI_post_recv_list(bmi_op_id_t * id,
		       PVFS_BMI_addr_t src,
		       void *const *buffer_list,
		       const bmi_size_t *size_list,
		       int list_count,
		       bmi_size_t total_expected_size,
		       bmi_size_t * total_actual_size,
		       enum bmi_buffer_type buffer_type,
		       bmi_msg_tag_t tag,
		       void *user_ptr,
		       bmi_context_id context_id)
{
    ref_st_p tmp_ref = NULL;
    int ret = -1;

#ifndef GOSSIP_DISABLE_DEBUG
    int i;

    gossip_debug(GOSSIP_BMI_DEBUG_OFFSETS,
	"BMI_post_recv_list: addr: %ld, count: %d, total_size: %ld\n", 
	(long)src, list_count, (long)total_expected_size);

    for(i=0; i<list_count; i++)
    {
	gossip_debug(GOSSIP_BMI_DEBUG_OFFSETS,
	    "   element %d: offset: 0x%lx, size: %ld\n",
	    i, (long)buffer_list[i], (long)size_list[i]);
    }
#endif

    *id = 0;

    gen_mutex_lock(&ref_mutex);
    tmp_ref = ref_list_search_addr(cur_ref_list, src);
    if (!tmp_ref)
    {
	gen_mutex_unlock(&ref_mutex);
	return (bmi_errno_to_pvfs(-EPROTO));
    }
    gen_mutex_unlock(&ref_mutex);

    if (tmp_ref->interface->BMI_meth_post_recv_list)
    {
	ret = tmp_ref->interface->BMI_meth_post_recv_list(
            id, tmp_ref->method_addr, buffer_list, size_list,
            list_count, total_expected_size, total_actual_size,
            buffer_type, tag, user_ptr, context_id);

	return (ret);
    }

    gossip_lerr("Error: method doesn't implement recv_list.\n");
    gossip_lerr("Error: recv_list emulation not yet available.\n");

    return (bmi_errno_to_pvfs(-ENOSYS));
}


/** Similar to BMI_post_sendunexpected(), except that the source buffer is 
 *  replaced by a list of (possibly non contiguous) buffers.
 *
 *  \param total_size the sum of the size list.
 *
 *  \return 0 on success, 1 on immediate successful completion,
 *  -errno on failure.
 */
int BMI_post_sendunexpected_list(bmi_op_id_t * id,
				 PVFS_BMI_addr_t dest,
				 const void *const *buffer_list,
				 const bmi_size_t *size_list,
				 int list_count,
				 bmi_size_t total_size,
				 enum bmi_buffer_type buffer_type,
				 bmi_msg_tag_t tag,
				 void *user_ptr,
				 bmi_context_id context_id)
{
    ref_st_p tmp_ref = NULL;
    int ret = -1;

#ifndef GOSSIP_DISABLE_DEBUG
    int i;

    gossip_debug(GOSSIP_BMI_DEBUG_OFFSETS,
	"BMI_post_sendunexpected_list: addr: %ld, count: %d, "
                 "total_size: %ld\n",  (long)dest, list_count,
                 (long)total_size);

    for(i=0; i<list_count; i++)
    {
	gossip_debug(GOSSIP_BMI_DEBUG_OFFSETS,
	    "   element %d: offset: 0x%lx, size: %ld\n",
	    i, (long)buffer_list[i], (long)size_list[i]);
    }
#endif

    *id = 0;

    gen_mutex_lock(&ref_mutex);
    tmp_ref = ref_list_search_addr(cur_ref_list, dest);
    if (!tmp_ref)
    {
	gen_mutex_unlock(&ref_mutex);
	return (bmi_errno_to_pvfs(-EPROTO));
    }
    gen_mutex_unlock(&ref_mutex);

    if (tmp_ref->interface->BMI_meth_post_send_list)
    {
	ret = tmp_ref->interface->BMI_meth_post_sendunexpected_list(
            id, tmp_ref->method_addr, buffer_list, size_list,
            list_count, total_size, buffer_type, tag, user_ptr,
            context_id);

	return (ret);
    }

    gossip_lerr("Error: method doesn't implement sendunexpected_list.\n");
    gossip_lerr("Error: send_list emulation not yet available.\n");

    return (bmi_errno_to_pvfs(-ENOSYS));
}


/** Attempts to cancel a pending operation that has not yet completed.
 *  Caller must still test to gather error code after calling this
 *  function even if it returns 0.
 *
 *  \return 0 on success, -errno on failure.
 */
int BMI_cancel(bmi_op_id_t id, 
	       bmi_context_id context_id)
{
    struct method_op *target_op = NULL;
    int ret = -1;

    gossip_debug(GOSSIP_BMI_DEBUG_CONTROL,
                 "%s: cancel id %llu\n", __func__, llu(id));

    target_op = id_gen_safe_lookup(id);
    assert(target_op->op_id == id);

    if(active_method_table[target_op->addr->method_type]->BMI_meth_cancel)
    {
	ret = active_method_table[
            target_op->addr->method_type]->BMI_meth_cancel(
                id, context_id);
    }
    else
    {
	gossip_err("Error: BMI_cancel() unimplemented "
                   "for this module.\n");
	ret = bmi_errno_to_pvfs(-ENOSYS);
    }

    return (ret);
}

/**************************************************************
 * method callback functions
 */

/* bmi_method_addr_reg_callback()
 * 
 * Used by the methods to register new addresses when they are
 * discovered.
 *
 * returns 0 on success, -errno on failure
 */
int bmi_method_addr_reg_callback(method_addr_p map)
{
    ref_st_p new_ref = NULL;

    /* NOTE: we are trusting the method to make sure that we really
     * don't know about the address yet.  No verification done here.
     */

    /* create a new reference structure */
    new_ref = alloc_ref_st();
    if (!new_ref)
    {
	return (bmi_errno_to_pvfs(-ENOMEM));
    }

    /*
      fill in the details; we don't have an id string for this one.
    */
    new_ref->method_addr = map;
    new_ref->id_string = NULL;

    /* check the method_type from the method_addr pointer to know
     * which interface to use */
    new_ref->interface = active_method_table[map->method_type];

    /* add the reference structure to the list */
    ref_list_add(cur_ref_list, new_ref);

    return (0);
}

/*
 * Attempt to insert this name into the list of active methods,
 * and bring it up.
 * NOTE: assumes caller has protected active_method_count with a mutex lock
 */
static int
activate_method(const char *name, const char *listen_addr, int flags)
{
    int i, ret;
    void *x;
    struct bmi_method_ops *meth;
    method_addr_p new_addr;

    /* already active? */
    for (i=0; i<active_method_count; i++)
	if (!strcmp(active_method_table[i]->method_name, name)) break;
    if (i < active_method_count)
    {
	return 0;
    }

    /* is the method known? */
    for (i=0; i<known_method_count; i++)
	if (!strcmp(known_method_table[i]->method_name, name)) break;
    if (i == known_method_count) {
	gossip_lerr("Error: no method available for %s.\n", name);
	return -ENOPROTOOPT;
    }
    meth = known_method_table[i];

    /*
     * Later: try to load a dynamic module, growing the known method
     * table and search it again.
     */

    /* toss it into the active table */
    x = active_method_table;
    active_method_table = malloc(
	(active_method_count + 1) * sizeof(*active_method_table));
    if (!active_method_table) {
	active_method_table = x;
	return -ENOMEM;
    }
    if (active_method_count) {
	memcpy(active_method_table, x,
	    active_method_count * sizeof(*active_method_table));
	free(x);
    }
    active_method_table[active_method_count] = meth;

    x = method_usage;
    method_usage = malloc((active_method_count + 1) * sizeof(*method_usage));
    if (!method_usage) {
        method_usage = x;
        return -ENOMEM;
    }
    if (active_method_count) {
        memcpy(method_usage, x, active_method_count * sizeof(*method_usage));
        free(x);
    }
    memset(&method_usage[active_method_count], 0, sizeof(*method_usage));

    ++active_method_count;

    /* initialize it */
    new_addr = 0;
    if (listen_addr) {
	new_addr = meth->BMI_meth_method_addr_lookup(listen_addr);
	if (!new_addr) {
	    gossip_err(
		"Error: failed to lookup listen address %s for method %s.\n",
		listen_addr, name);
	    --active_method_count;
	    return -EINVAL;
	}
	/* this is a bit of a hack */
	new_addr->method_type = active_method_count - 1;
    }
    ret = meth->BMI_meth_initialize(new_addr, active_method_count - 1, flags);
    if (ret < 0) {
	gossip_debug(GOSSIP_BMI_DEBUG_CONTROL,
          "failed to initialize method %s.\n", name);
	--active_method_count;
	return ret;
    }

    /* tell it about any open contexts */
    for (i=0; i<BMI_MAX_CONTEXTS; i++)
	if (context_array[i]) {
	    ret = meth->BMI_meth_open_context(i);
	    if (ret < 0)
		break;
	}

    return ret;
}

 
int bmi_errno_to_pvfs(int error)
{
    int bmi_errno = error;

#define __CASE(err)                      \
case -err: bmi_errno = -BMI_##err; break;\
case err: bmi_errno = BMI_##err; break

    switch(error)
    {
        __CASE(EPERM);
        __CASE(ENOENT);
        __CASE(EINTR);
        __CASE(EIO);
        __CASE(ENXIO);
        __CASE(EBADF);
        __CASE(EAGAIN);
        __CASE(ENOMEM);
        __CASE(EFAULT);
        __CASE(EBUSY);
        __CASE(EEXIST);
        __CASE(ENODEV);
        __CASE(ENOTDIR);
        __CASE(EISDIR);
        __CASE(EINVAL);
        __CASE(EMFILE);
        __CASE(EFBIG);
        __CASE(ENOSPC);
        __CASE(EROFS);
        __CASE(EMLINK);
        __CASE(EPIPE);
        __CASE(EDEADLK);
        __CASE(ENAMETOOLONG);
        __CASE(ENOLCK);
        __CASE(ENOSYS);
        __CASE(ENOTEMPTY);
        __CASE(ELOOP);
        __CASE(ENOMSG);
        __CASE(ENODATA);
        __CASE(ETIME);
        __CASE(EREMOTE);
        __CASE(EPROTO);
        __CASE(EBADMSG);
        __CASE(EOVERFLOW);
        __CASE(EMSGSIZE);
        __CASE(EPROTOTYPE);
        __CASE(ENOPROTOOPT);
        __CASE(EPROTONOSUPPORT);
        __CASE(EOPNOTSUPP);
        __CASE(EADDRINUSE);
        __CASE(EADDRNOTAVAIL);
        __CASE(ENETDOWN);
        __CASE(ENETUNREACH);
        __CASE(ENETRESET);
        __CASE(ENOBUFS);
        __CASE(ETIMEDOUT);
        __CASE(ECONNREFUSED);
        __CASE(EHOSTDOWN);
        __CASE(EHOSTUNREACH);
        __CASE(EALREADY);
        __CASE(EACCES);
#undef __CASE
    }
    return bmi_errno;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
