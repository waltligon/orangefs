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
#include <time.h>
#include <sys/time.h>
#include <stdio.h>

#include "bmi.h"
#include "bmi-method-support.h"
#include "bmi-method-callback.h"
#include "gossip.h"
#include "reference-list.h"
#include "op-list.h"
#include "gen-locks.h"
#include "str-utils.h"
#include "id-generator.h"
#include "pvfs2-internal.h"
#include "pvfs2-debug.h"

static int bmi_initialized_count = 0;
static gen_mutex_t bmi_initialize_mutex = GEN_MUTEX_INITIALIZER;

/*
 * List of BMI addrs currently managed.
 */
static ref_list_p cur_ref_list = NULL;

/* array to keep up with active contexts */
static int context_array[BMI_MAX_CONTEXTS] = { 0 };
static gen_mutex_t context_mutex = GEN_MUTEX_INITIALIZER;
static gen_mutex_t ref_mutex = GEN_MUTEX_INITIALIZER;

static QLIST_HEAD(forget_list);
static gen_mutex_t forget_list_mutex = GEN_MUTEX_INITIALIZER;

struct forget_item
{
    struct qlist_head link;
    BMI_addr_t addr;
};

/*
 * BMI trigger to reap all method resources for inactive addresses.
 */
static QLIST_HEAD(bmi_addr_force_drop_list);
static gen_mutex_t bmi_addr_force_drop_list_mutex = GEN_MUTEX_INITIALIZER;
struct drop_item
{
    struct qlist_head link;
    char  *method_name;
};

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
#ifdef __STATIC_METHOD_BMI_MX__
extern struct bmi_method_ops bmi_mx_ops;
#endif
#ifdef __STATIC_METHOD_BMI_IB__
extern struct bmi_method_ops bmi_ib_ops;
#endif
#ifdef __STATIC_METHOD_BMI_PORTALS__
extern struct bmi_method_ops bmi_portals_ops;
#endif
#ifdef __STATIC_METHOD_BMI_ZOID__
extern struct bmi_method_ops bmi_zoid_ops;
#endif

static struct bmi_method_ops *const static_methods[] = {
#ifdef __STATIC_METHOD_BMI_TCP__
    &bmi_tcp_ops,
#endif
#ifdef __STATIC_METHOD_BMI_GM__
    &bmi_gm_ops,
#endif
#ifdef __STATIC_METHOD_BMI_MX__
    &bmi_mx_ops,
#endif
#ifdef __STATIC_METHOD_BMI_IB__
    &bmi_ib_ops,
#endif
#ifdef __STATIC_METHOD_BMI_PORTALS__
    &bmi_portals_ops,
#endif
#ifdef __STATIC_METHOD_BMI_ZOID__
    &bmi_zoid_ops,
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

struct method_usage_t {
    int iters_polled;  /* how many iterations since this method was polled */
    int iters_active;  /* how many iterations since this method had action */
    int plan;
    int flags;
};

static struct method_usage_t * expected_method_usage = NULL;
static struct method_usage_t * unexpected_method_usage = NULL;

static const int usage_iters_starvation = 100000;
static const int usage_iters_active = 10000;
static int global_flags;

static int activate_method(const char *name, const char *listen_addr,
    int flags);
static void bmi_addr_drop(ref_st_p tmp_ref);
static void bmi_addr_force_drop(ref_st_p ref, ref_list_p ref_list);
static void bmi_check_forget_list(void);
static void bmi_check_addr_force_drop (void);

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
    int i = 0, j = 0;
    char **requested_methods = NULL;
    char **listen_addrs = NULL;
    char *this_addr = NULL;
    char *proto = NULL;
    int addr_count = 0;

    gen_mutex_lock(&bmi_initialize_mutex);
    if(bmi_initialized_count > 0)
    {
        /* Already initialized! Just increment ref count and return. */
	++bmi_initialized_count;
	gen_mutex_unlock(&bmi_initialize_mutex);
        return 0;
    }
    ++bmi_initialized_count;
    gen_mutex_unlock(&bmi_initialize_mutex);

    global_flags = flags;

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

    /* make sure that id generator is initialized if not already */
    ret = id_gen_safe_initialize();
    if(ret < 0)
    {
        return(ret);
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

	/* Today is that day! */
	addr_count = PINT_split_string_list(&listen_addrs, listen_addr);
	
	for (i=0; i<numreq; i++) {

	    /* assume the method name is bmi_<proto>, and find the <proto>
	     * part
	     */
	    proto = strstr(requested_methods[i], "bmi_");
	    if(!proto)
	    {
	        gossip_err("%s: Invalid method name: %s.  Method names "
			   "must start with 'bmi_'\n",
			   __func__, requested_methods[i]);
		ret = -EINVAL;
		gen_mutex_unlock(&active_method_count_mutex);
		goto bmi_initialize_failure;
	    }
	    proto += 4;

	    /* match the proper listen addr to the method */
	    for(j=0; j<addr_count; ++j)
	    {
		/* we don't want a strstr here in case the addr has
		 * the proto as part of the hostname
		 */
		if(!strncmp(listen_addrs[j], proto, strlen(proto)))
		{
		    /* found the right addr */
		    this_addr = listen_addrs[j];
		    break;
		}
	    }
		
	    if(!this_addr)
	    {
		/* couldn't find the right listen addr */
		gossip_err("%s: Failed to find an appropriate listening "
			   "address for the bmi method: %s\n",
			   __func__, requested_methods[i]);
		ret = -EINVAL;
		gen_mutex_unlock(&active_method_count_mutex);
		goto bmi_initialize_failure;
	    }

	    ret = activate_method(requested_methods[i], this_addr, flags);
	    if (ret < 0) {
		ret = bmi_errno_to_pvfs(ret);
		gen_mutex_unlock(&active_method_count_mutex);
		goto bmi_initialize_failure;
	    }
	    free(requested_methods[i]);
	}
	free(requested_methods);
	if(listen_addrs)
	{
	    PINT_free_string_list(listen_addrs, addr_count);
	    listen_addrs = NULL;
	}
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
		active_method_table[i]->finalize();
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

    if(listen_addrs)
    {
	PINT_free_string_list(listen_addrs, addr_count);
    }

    active_method_count = 0;
    gen_mutex_unlock(&active_method_count_mutex);

    /* shut down id generator */
    id_gen_safe_finalize();

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

    gen_mutex_lock(&bmi_initialize_mutex);
    --bmi_initialized_count;
    if(bmi_initialized_count > 0)
    {
        gen_mutex_unlock(&bmi_initialize_mutex);
        return 0;
    }
    gen_mutex_unlock(&bmi_initialize_mutex);

    gen_mutex_lock(&active_method_count_mutex);
    /* attempt to shut down active methods */
    for (i = 0; i < active_method_count; i++)
    {
	active_method_table[i]->finalize();
    }
    active_method_count = 0;
    free(active_method_table);
    gen_mutex_unlock(&active_method_count_mutex);

    free(known_method_table);
    known_method_count = 0;

    if (expected_method_usage)
        free(expected_method_usage);

    if (unexpected_method_usage)
       free(unexpected_method_usage);

    /* destroy the reference list */
    /* (side effect: destroys all method addresses as well) */
    ref_list_cleanup(cur_ref_list);

    /* shut down id generator */
    id_gen_safe_finalize();

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
    int i;
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
	ret = active_method_table[i]->open_context(
            context_index);
	if(ret < 0)
	{
	    /*
              one of them failed; kill this context in the previous
              modules
            */
            --i;
            while (i >= 0)
	    {
		active_method_table[i]->close_context(
                    context_index);
                --i;
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
	active_method_table[i]->close_context(context_id);
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
		  BMI_addr_t src,
		  void *buffer,
		  bmi_size_t expected_size,
		  bmi_size_t * actual_size,
		  enum bmi_buffer_type buffer_type,
		  bmi_msg_tag_t tag,
		  void *user_ptr,
		  bmi_context_id context_id,
                  bmi_hint hints)
{
    return(BMI_post_recv_list(id, src, &buffer, &expected_size, 1,
        expected_size, actual_size, buffer_type, tag, user_ptr, context_id,
        hints));
}


/** Submits send operations for subsequent service.
 *
 *  \return 0 on success, -errno on failure.
 */
int BMI_post_send(bmi_op_id_t * id,
		  BMI_addr_t dest,
		  const void *buffer,
		  bmi_size_t size,
		  enum bmi_buffer_type buffer_type,
		  bmi_msg_tag_t tag,
		  void *user_ptr,
		  bmi_context_id context_id,
                  bmi_hint hints)
{
    return(BMI_post_send_list(id, dest, &buffer, &size, 1, size,
        buffer_type, tag, user_ptr, context_id, hints));
}


/** Submits unexpected send operations for subsequent service.
 *
 *  \return 0 on success, -errno on failure.
 */
int BMI_post_sendunexpected_class(bmi_op_id_t * id,
			    BMI_addr_t dest,
			    const void *buffer,
			    bmi_size_t size,
			    enum bmi_buffer_type buffer_type,
			    bmi_msg_tag_t tag,
                            uint8_t class,
			    void *user_ptr,
			    bmi_context_id context_id,
                            bmi_hint hints)
{
    return(BMI_post_sendunexpected_list_class(id, dest, &buffer, &size, 1, size,
        buffer_type, tag, class, user_ptr, context_id, hints));
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

    target_op = id_gen_fast_lookup(id);
    assert(target_op->op_id == id);

    ret = active_method_table[
        target_op->addr->method_type]->test(
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
	ret = active_method_table[0]->testsome(
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
                    id_gen_fast_lookup(id_array[j]);
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
	    ret = active_method_table[i]->testsome(
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
construct_poll_plan(struct method_usage_t * method_usage,
      int nmeth, int *idle_time_ms)
{
    int i, numplan;

    numplan = 0;
    for (i=0; i<nmeth; i++) {
        ++method_usage[i].iters_polled;
        ++method_usage[i].iters_active;
        method_usage[i].plan = 0;
        if ((method_usage[i].iters_active <= usage_iters_active) &&
            (!(method_usage[i].flags & BMI_METHOD_FLAG_NO_POLLING))){
            /* recently busy, poll */
	    if (0) gossip_debug(GOSSIP_BMI_DEBUG_CONTROL,
                         "%s: polling active meth %d: %d / %d\n", __func__, i,
                         method_usage[i].iters_active, usage_iters_active);
            method_usage[i].plan = 1;
            ++numplan;
            *idle_time_ms = 0;  /* busy polling */
        } else if (method_usage[i].iters_polled >= usage_iters_starvation) {
            /* starving, time to poke this one */
	    if (0) gossip_debug(GOSSIP_BMI_DEBUG_CONTROL,
                         "%s: polling starving meth %d: %d / %d\n", __func__, i,
                         method_usage[i].iters_polled, usage_iters_starvation);
            method_usage[i].plan = 1;
            ++numplan;
        } 
    }

    /* if nothing is starving or busy, poll everybody */
    if (numplan == 0) {
        for (i=0; i<nmeth; i++)
            method_usage[i].plan = 1;
        numplan = nmeth;

        /* spread idle time evenly */
        if (*idle_time_ms) {
            *idle_time_ms /= numplan;
            if (*idle_time_ms == 0)
                *idle_time_ms = 1;
        }
        /* note that BMI_testunexpected is always called with idle_time 0 */
        if (0) gossip_debug(GOSSIP_BMI_DEBUG_CONTROL,
                     "%s: polling all %d methods, idle %d ms\n", __func__,
                     numplan, *idle_time_ms);
    }
}


/** Checks to see if any unexpected messages have completed.
 *
 *  \return 0 on success, -errno on failure.
 */
int BMI_testunexpected_class(int incount,
		       int *outcount,
		       struct BMI_unexpected_info *info_array,
                       uint8_t class, 
		       int max_idle_time_ms)
{
    int i = 0;
    int ret = -1;
    int position = 0;
    int tmp_outcount = 0;
    struct bmi_method_unexpected_info sub_info[incount];
    ref_st_p tmp_ref = NULL;
    int tmp_active_method_count = 0;

    /* figure out if we need to drop any stale addresses */
    bmi_check_forget_list();
    bmi_check_addr_force_drop();

    gen_mutex_lock(&active_method_count_mutex);
    tmp_active_method_count = active_method_count;
    gen_mutex_unlock(&active_method_count_mutex);

    if (max_idle_time_ms < 0)
	return (bmi_errno_to_pvfs(-EINVAL));

    *outcount = 0;

    construct_poll_plan(unexpected_method_usage,
          tmp_active_method_count, &max_idle_time_ms);

    while (position < incount && i < tmp_active_method_count)
    {
        if (unexpected_method_usage[i].plan) {
            ret = active_method_table[i]->testunexpected(
                (incount - position), &tmp_outcount,
                (&(sub_info[position])), class, max_idle_time_ms);
            if (ret < 0)
            {
                /* can't recover from this */
                gossip_lerr("Error: critical BMI_testunexpected failure.\n");
                return (ret);
            }
            position += tmp_outcount;
            (*outcount) += tmp_outcount;
            unexpected_method_usage[i].iters_polled = 0;
            if (ret)
                unexpected_method_usage[i].iters_active = 0;
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
        if(global_flags & BMI_AUTO_REF_COUNT)
        {
            tmp_ref->ref_count++;
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

    construct_poll_plan(expected_method_usage,
          tmp_active_method_count, &max_idle_time_ms);

    while (position < incount && i < tmp_active_method_count)
    {
        if (expected_method_usage[i].plan) {
            ret = active_method_table[i]->testcontext(
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
            expected_method_usage[i].iters_polled = 0;
            if (ret)
                expected_method_usage[i].iters_active = 0;
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
const char* BMI_addr_rev_lookup(BMI_addr_t addr)
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
const char* BMI_addr_rev_lookup_unexpected(BMI_addr_t addr)
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
    
    if(!tmp_ref->interface->rev_lookup_unexpected)
    {
        return("UNKNOWN");
    }

    return(tmp_ref->interface->rev_lookup_unexpected(
        tmp_ref->method_addr));
}


/** Allocates memory that can be used in native mode by the BMI layer.
 *
 *  \return Pointer to buffer on success, NULL on failure.
 */
void *BMI_memalloc(BMI_addr_t addr,
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
    new_buffer = tmp_ref->interface->memalloc(size, send_recv);

    return (new_buffer);
}

/** Frees memory that was allocated with BMI_memalloc().
 *
 *  \return 0 on success, -errno on failure.
 */
int BMI_memfree(BMI_addr_t addr,
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
    ret = tmp_ref->interface->memfree(buffer, size, send_recv);

    return (ret);
}

/** Acknowledge that an unexpected message has been
 * serviced that was returned from BMI_test_unexpected().
 *
 *  \return 0 on success, -errno on failure.
 */
int BMI_unexpected_free(BMI_addr_t addr,
		void *buffer)
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

    if (!tmp_ref->interface->unexpected_free)
    {
        gossip_err("unimplemented unexpected_free callback\n");
        return bmi_errno_to_pvfs(-EOPNOTSUPP);
    }
    /* free the memory */
    ret = tmp_ref->interface->unexpected_free(buffer);

    return (ret);
}

/** Pass in optional parameters.
 *
 *  \return 0 on success, -errno on failure.
 */
int BMI_set_info(BMI_addr_t addr,
		 int option,
		 void *inout_parameter)
{
    int ret = -1;
    int i = 0;
    ref_st_p tmp_ref = NULL;

    gossip_debug(GOSSIP_BMI_DEBUG_CONTROL,
                 "[BMI CONTROL]: %s: set_info: %llu option: %d\n",
                 __func__, llu(addr), option);
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
	    ret = active_method_table[i]->set_info(
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

    gossip_debug(GOSSIP_BMI_DEBUG_CONTROL,
                 "[BMI CONTROL]: %s: searching for ref %llu\n",
                 __func__, llu(addr));
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
        gossip_debug(GOSSIP_BMI_DEBUG_CONTROL,
                     "[BMI CONTROL]: %s: incremented ref %llu to: %d\n",
                     __func__, llu(addr), tmp_ref->ref_count);
	gen_mutex_unlock(&ref_mutex);
	return(0);
    }
    if(option == BMI_DEC_ADDR_REF)
    {
	tmp_ref->ref_count--;
        gossip_debug(GOSSIP_BMI_DEBUG_CONTROL,
                     "[BMI CONTROL]: %s: decremented ref %llu to: %d\n",
                     __func__, llu(addr), tmp_ref->ref_count);
	assert(tmp_ref->ref_count >= 0);

	if(tmp_ref->ref_count == 0)
	{
            bmi_addr_drop(tmp_ref);
	}
	gen_mutex_unlock(&ref_mutex);
	return(0);
    }

    /* if the caller requests a TCP specific close socket action */
    if (option == BMI_TCP_CLOSE_SOCKET) 
    {
        /* check to see if the address is in fact a tcp address */
        if(strcmp(tmp_ref->interface->method_name, "bmi_tcp") == 0)
        {
            /* take the same action as in the BMI_DEC_ADDR_REF case to clean
             * out the entire address structure and anything linked to it so 
             * that the next addr_lookup starts from scratch
             */
	    gossip_debug(GOSSIP_BMI_DEBUG_CONTROL,
                         "[BMI CONTROL]: %s: Closing bmi_tcp "
                         "connection at caller's request.\n",
                         __func__); 
            ref_list_rem(cur_ref_list, addr);
            dealloc_ref_st(tmp_ref);
        }
        gen_mutex_unlock(&ref_mutex);
        return 0;
    }

    gen_mutex_unlock(&ref_mutex);

    ret = tmp_ref->interface->set_info(option, inout_parameter);

    return (ret);
}

/** Query for optional parameters.
 *
 *  \return 0 on success, -errno on failure.
 */
int BMI_get_info(BMI_addr_t addr,
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
    case BMI_CHECK_MAXSIZE:
	gen_mutex_lock(&active_method_count_mutex);
	for (i = 0; i < active_method_count; i++)
	{
	    ret = active_method_table[i]->get_info(
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
    case BMI_GET_UNEXP_SIZE:
        gen_mutex_lock(&ref_mutex);
        tmp_ref = ref_list_search_addr(cur_ref_list, addr);
        if(!tmp_ref)
        {
            gen_mutex_unlock(&ref_mutex);
            return (bmi_errno_to_pvfs(-EINVAL));
        }
        gen_mutex_unlock(&ref_mutex);
        ret = tmp_ref->interface->get_info(
            option, inout_parameter);
        if(ret < 0)
        {
            return ret;
        }
        break;

    default:
	return (bmi_errno_to_pvfs(-ENOSYS));
    }
    return (0);
}

/** Given a string representation of a host/network address and a BMI
 * address handle, return whether the BMI address handle is part of the wildcard
 * address range specified by the string.
 * \return 1 on success, -errno on failure and 0 if it is not part of
 * the specified range
 */
int BMI_query_addr_range (BMI_addr_t addr, const char *id_string, int netmask)
{
    int ret = -1;
    int i = 0, failed = 1;
    int provided_method_length = 0;
    char *ptr, *provided_method_name = NULL;
    ref_st_p tmp_ref = NULL;

    if((strlen(id_string)+1) > BMI_MAX_ADDR_LEN)
    {
	return(bmi_errno_to_pvfs(-ENAMETOOLONG));
    }
    /* lookup the provided address */
    gen_mutex_lock(&ref_mutex);
    tmp_ref = ref_list_search_addr(cur_ref_list, addr);
    if (!tmp_ref)
    {
	gen_mutex_unlock(&ref_mutex);
	return (bmi_errno_to_pvfs(-EPROTO));
    }
    gen_mutex_unlock(&ref_mutex);

    ptr = strchr(id_string, ':');
    if (ptr == NULL)
    {
        return (bmi_errno_to_pvfs(-EINVAL));
    }
    ret = -EPROTO;
    provided_method_length = (unsigned long) ptr - (unsigned long) id_string;
    provided_method_name = (char *) calloc(provided_method_length + 1, sizeof(char));
    if (provided_method_name == NULL)
    {
        return bmi_errno_to_pvfs(-ENOMEM);
    }
    strncpy(provided_method_name, id_string, provided_method_length);

    /* Now we will run through each method looking for one that
     * matches the specified wildcard address. 
     */
    i = 0;
    gen_mutex_lock(&active_method_count_mutex);
    while (i < active_method_count)
    {
        const char *active_method_name = active_method_table[i]->method_name + 4;
        /* provided name matches this interface */
        if (!strncmp(active_method_name, provided_method_name, provided_method_length))
        {
            int (*meth_fnptr)(bmi_method_addr_p, const char *, int);
            failed = 0;
            if ((meth_fnptr = active_method_table[i]->query_addr_range) == NULL)
            {
                ret = -ENOSYS;
                gossip_lerr("Error: method doesn't implement querying address range/wildcards! Cannot implement FS export options!\n");
                failed = 1;
                break;
            }
            /* pass it into the specific bmi layer */
            ret = meth_fnptr(tmp_ref->method_addr, id_string, netmask);
            if (ret < 0)
                failed = 1;
            break;
        }
	i++;
    }
    gen_mutex_unlock(&active_method_count_mutex);
    free(provided_method_name);
    if (failed)
        return bmi_errno_to_pvfs(ret);
    return ret;
}

/** Resolves the string representation of a host address into a BMI
 *  address handle.
 *
 *  \return 0 on success, -errno on failure.
 */
int BMI_addr_lookup(BMI_addr_t * new_addr,
                    const char *id_string)
{

    ref_st_p new_ref = NULL;
    bmi_method_addr_p meth_addr = NULL;
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
           !(meth_addr = active_method_table[i]->method_addr_lookup(id_string)))
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
		    method_addr_lookup(id_string);
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
        return bmi_errno_to_pvfs(-ENOPROTOOPT);
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
    meth_addr->parent = new_ref;
    new_ref->id_string = (char *) malloc(strlen(id_string) + 1);
    if (!new_ref->id_string)
    {
	ret = bmi_errno_to_pvfs(errno);
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
	active_method_table[i]->set_info(
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
		       BMI_addr_t dest,
		       const void *const *buffer_list,
		       const bmi_size_t *size_list,
		       int list_count,
		       /* "total_size" is the sum of the size list */
		       bmi_size_t total_size,
		       enum bmi_buffer_type buffer_type,
		       bmi_msg_tag_t tag,
		       void *user_ptr,
		       bmi_context_id context_id,
                       bmi_hint hints)
{
    ref_st_p tmp_ref = NULL;
    int ret = -1;

#ifndef GOSSIP_DISABLE_DEBUG
    int i;

    gossip_debug(GOSSIP_BMI_DEBUG_OFFSETS,
	"BMI_post_send_list: addr: %ld, count: %d, total_size: %ld, tag: %d\n", 
	(long)dest, list_count, (long)total_size, (int)tag);

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

    if (tmp_ref->interface->post_send_list)
    {
	ret = tmp_ref->interface->post_send_list(
            id, tmp_ref->method_addr, buffer_list, size_list,
            list_count, total_size, buffer_type, tag, user_ptr,
            context_id, (PVFS_hint)hints);

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
		       BMI_addr_t src,
		       void *const *buffer_list,
		       const bmi_size_t *size_list,
		       int list_count,
		       bmi_size_t total_expected_size,
		       bmi_size_t * total_actual_size,
		       enum bmi_buffer_type buffer_type,
		       bmi_msg_tag_t tag,
		       void *user_ptr,
		       bmi_context_id context_id,
                       bmi_hint hints)
{
    ref_st_p tmp_ref = NULL;
    int ret = -1;

#ifndef GOSSIP_DISABLE_DEBUG
    int i;

    gossip_debug(GOSSIP_BMI_DEBUG_OFFSETS,
	"BMI_post_recv_list: addr: %ld, count: %d, total_size: %ld, tag: %d\n", 
	(long)src, list_count, (long)total_expected_size, (int)tag);

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

    if (tmp_ref->interface->post_recv_list)
    {
	ret = tmp_ref->interface->post_recv_list(
            id, tmp_ref->method_addr, buffer_list, size_list,
            list_count, total_expected_size, total_actual_size,
            buffer_type, tag, user_ptr, context_id, (PVFS_hint)hints);

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
int BMI_post_sendunexpected_list_class(bmi_op_id_t * id,
				 BMI_addr_t dest,
				 const void *const *buffer_list,
				 const bmi_size_t *size_list,
				 int list_count,
				 bmi_size_t total_size,
				 enum bmi_buffer_type buffer_type,
				 bmi_msg_tag_t tag,
                                 uint8_t class,
				 void *user_ptr,
				 bmi_context_id context_id,
                                 bmi_hint hints)
{
    ref_st_p tmp_ref = NULL;
    int ret = -1;

#ifndef GOSSIP_DISABLE_DEBUG
    int i;

    gossip_debug(GOSSIP_BMI_DEBUG_OFFSETS,
	"BMI_post_sendunexpected_list: addr: %ld, count: %d, "
                 "total_size: %ld, tag: %d\n",  (long)dest, list_count,
                 (long)total_size, (int)tag);

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

    if (tmp_ref->interface->post_send_list)
    {
	ret = tmp_ref->interface->post_sendunexpected_list(
            id, tmp_ref->method_addr, buffer_list, size_list,
            list_count, total_size, buffer_type, tag, class, user_ptr,
            context_id, (PVFS_hint)hints);

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

    target_op = id_gen_fast_lookup(id);
    if(target_op == NULL)
    {
        /* if we can't find the operation, then assume it has already
         * completed naturally.
         */
        return(0);
    }

    assert(target_op->op_id == id);

    if(active_method_table[target_op->addr->method_type]->cancel)
    {
	ret = active_method_table[
            target_op->addr->method_type]->cancel(
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
 * discovered.  Only call this method when the device gets an
 * unexpected receive from a new peer, i.e., if you do the equivalent
 * of a socket accept() and get a new connection.
 *
 * Do not call this function for active lookups, that is from your
 * method_addr_lookup.  BMI already knows about the address in
 * this case, since the user provided it.
 *
 * returns 0 on success, -errno on failure
 */
BMI_addr_t bmi_method_addr_reg_callback(bmi_method_addr_p map)
{
    ref_st_p new_ref = NULL;

    /* NOTE: we are trusting the method to make sure that we really
     * don't know about the address yet.  No verification done here.
     */

    /* create a new reference structure */
    new_ref = alloc_ref_st();
    if (!new_ref)
    {
	return 0;
    }

    /*
      fill in the details; we don't have an id string for this one.
    */
    new_ref->method_addr = map;
    new_ref->id_string = NULL;
    map->parent = new_ref;

    /* check the method_type from the method_addr pointer to know
     * which interface to use */
    new_ref->interface = active_method_table[map->method_type];

    /* add the reference structure to the list */
    ref_list_add(cur_ref_list, new_ref);

    return new_ref->bmi_addr;
}

int bmi_method_addr_forget_callback(BMI_addr_t addr)
{
    struct forget_item* tmp_item = NULL;

    tmp_item = (struct forget_item*)malloc(sizeof(struct forget_item));
    if(!tmp_item)
    {
        return(bmi_errno_to_pvfs(-ENOMEM));
    }

    tmp_item->addr = addr;

    /* add to queue of items that we want the BMI control layer to consider
     * deallocating
     */
    gen_mutex_lock(&forget_list_mutex);
    qlist_add(&tmp_item->link, &forget_list);
    gen_mutex_unlock(&forget_list_mutex);

    return (0);
}

/*
 * Signal BMI to drop inactive connections for this method.
 */
void bmi_method_addr_drop_callback (char* method_name)
{
    struct drop_item *item =
        (struct drop_item *) malloc(sizeof(struct drop_item));

    /*
     * If we can't allocate, just return.
     * Maybe this will succeed next time.
     */
    if (!item) return;

    item->method_name = method_name;
    
    gen_mutex_lock(&bmi_addr_force_drop_list_mutex);
    qlist_add(&item->link, &bmi_addr_force_drop_list);
    gen_mutex_unlock(&bmi_addr_force_drop_list_mutex);

    return;
}


/**
 * Try to increase method_usage_t struct to include room for a new method.
 */
static int grow_method_usage (struct method_usage_t ** p, int newflags)
{
    struct method_usage_t * x = *p;
    *p = malloc((active_method_count + 1) * sizeof(**p));
    if (!*p) {
        *p = x;
        return 0;
    }
    if (active_method_count) {
        memcpy(*p, x, active_method_count * sizeof(**p));
        free(x);
    }
    memset(&((*p)[active_method_count]), 0, sizeof(**p));
    (*p)[active_method_count].flags = newflags;

    return 1;
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
    bmi_method_addr_p new_addr;

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

    if (!grow_method_usage (&unexpected_method_usage, meth->flags))
       return -ENOMEM;

    /**
     * If we run out of memory here, the unexpected_method_usage will be
     * larger than strictly required but there is no memory leak.
     */

    if (!grow_method_usage (&expected_method_usage, meth->flags))
       return -ENOMEM;

    ++active_method_count;

    /* initialize it */
    new_addr = 0;
    if (listen_addr) {
	new_addr = meth->method_addr_lookup(listen_addr);
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
    ret = meth->initialize(new_addr, active_method_count - 1, flags);
    if (ret < 0) {
	gossip_debug(GOSSIP_BMI_DEBUG_CONTROL,
          "failed to initialize method %s.\n", name);
	--active_method_count;
	return ret;
    }

    /* tell it about any open contexts */
    for (i=0; i<BMI_MAX_CONTEXTS; i++)
	if (context_array[i]) {
	    ret = meth->open_context(i);
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
        __CASE(ECONNRESET);
#undef __CASE
    }
    return bmi_errno;
}

/* bmi_check_forget_list()
 * 
 * Scans queue of items that methods have suggested that we forget about 
 *
 * no return value
 */
static void bmi_check_forget_list(void)
{
    BMI_addr_t tmp_addr;
    struct forget_item* tmp_item;
    ref_st_p tmp_ref = NULL;
    
    gen_mutex_lock(&forget_list_mutex);
    while(!qlist_empty(&forget_list))
    {
        tmp_item = qlist_entry(forget_list.next, struct forget_item,
            link);     
        qlist_del(&tmp_item->link);
        /* item is off of the list; unlock for a moment while we work on
         * this addr 
         */
        gen_mutex_unlock(&forget_list_mutex);
        tmp_addr = tmp_item->addr;
        free(tmp_item);

        gen_mutex_lock(&ref_mutex);
        tmp_ref = ref_list_search_addr(cur_ref_list, tmp_addr);
        if(tmp_ref && tmp_ref->ref_count == 0)
        {
            bmi_addr_drop(tmp_ref);
        }   
        gen_mutex_unlock(&ref_mutex);

        gen_mutex_lock(&forget_list_mutex);
    }
    gen_mutex_unlock(&forget_list_mutex);

    return;
}

/* bmi_addr_drop
 *
 * Destroys a complete BMI address, including asking the method to clean up 
 * its portion.  Will query the method for permission before proceeding
 *
 * NOTE: must be called with ref list mutex held 
 */
static void bmi_addr_drop(ref_st_p tmp_ref)
{
    struct method_drop_addr_query query;
    query.response = 0;
    query.addr = tmp_ref->method_addr;
    int ret = 0;

    /* reference count is zero; ask module if it wants us to discard
     * the address; TCP will tell us to drop addresses for which the
     * socket has died with no possibility of reconnect 
     */
    ret = tmp_ref->interface->get_info(BMI_DROP_ADDR_QUERY,
        &query);
    if(ret == 0 && query.response == 1)
    {
        /* kill the address */
        gossip_debug(GOSSIP_BMI_DEBUG_CONTROL,
            "[BMI CONTROL]: %s: bmi discarding address: %llu\n",
            __func__, llu(tmp_ref->bmi_addr));
        ref_list_rem(cur_ref_list, tmp_ref->bmi_addr);
        /* NOTE: this triggers request to module to free underlying
         * resources if it wants to
         */
        dealloc_ref_st(tmp_ref);
    }
    return;
}


/* bmi_addr_force_drop
 *
 * Destroys a complete BMI address, including forcing the method to clean up 
 * its portion.
 *
 * NOTE: must be called with ref list mutex held 
 */
static void bmi_addr_force_drop(ref_st_p ref, ref_list_p ref_list)
{
    gossip_debug(GOSSIP_BMI_DEBUG_CONTROL,
                 "[BMI CONTROL]: %s: bmi discarding address: %llu\n",
                 __func__, llu(ref->bmi_addr));

    ref_list_rem(ref_list, ref->bmi_addr);
    dealloc_ref_st(ref);

    return;
}

/*
 * bmi_check_addr_force_drop
 *
 * Checks to see if any method has requested freeing resources.
 */
static void bmi_check_addr_force_drop (void)
{
    struct drop_item *drop_item = NULL;
    ref_st_p          ref_item = NULL;

    gen_mutex_lock(&bmi_addr_force_drop_list_mutex);
    while (!qlist_empty(&bmi_addr_force_drop_list))
    {
        drop_item = qlist_entry(qlist_pop(&bmi_addr_force_drop_list),
                                struct drop_item,
                                link);
        gen_mutex_unlock(&bmi_addr_force_drop_list_mutex);
        gen_mutex_lock(&ref_mutex);
        qlist_for_each_entry(ref_item, cur_ref_list, list_link)
        {
             if ((ref_item->ref_count == 0) &&
                 (ref_item->interface->method_name == drop_item->method_name))
             {
                 bmi_addr_force_drop(ref_item, cur_ref_list);
             }
        }
        gen_mutex_unlock(&ref_mutex);
        gen_mutex_lock(&bmi_addr_force_drop_list_mutex);
    }
    gen_mutex_unlock(&bmi_addr_force_drop_list_mutex);

    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
