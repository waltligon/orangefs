/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <string.h>
#include <dlfcn.h>

#include "bmi.h"
#include "bmi-method-support.h"
#include "gossip.h"
#include "reference-list.h"
#include "op-list.h"
#include "gen-locks.h"

static int active_method_count = 0;
static struct bmi_method_ops **active_method_table = NULL;
static ref_list_p cur_ref_list = NULL;
static gen_mutex_t interface_mutex = GEN_MUTEX_INITIALIZER;

static int split_string_list(char ***tokens,
			     const char *comma_list);

/* array to keep up with active contexts */
static int context_array[BMI_MAX_CONTEXTS] = { 0 };

#if !defined(__STATIC_METHOD_BMI_GM__) ||\
 !defined(__STATIC_METHOD_BMI_TCP__)
/* define if there is only one active method */
#define __BMI_SINGLE_METHOD__
#endif

/* BMI_initialize()
 * 
 * Initializes the BMI layer.  Must be called before any other BMI
 * functions.  method_list is a comma separated list of BMI methods to
 * use, listen_addr is a comma separated list of addresses to listen on
 * for each method (if needed), and flags are initialization flags.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_initialize(const char *method_list,
		   const char *listen_addr,
		   int flags)
{
    int ret = -1;
    int i = 0;
    char **requested_methods = NULL;
    method_addr_p new_addr = NULL;
    struct bmi_method_ops **tmp_method_ops = NULL;

    /* bring in the BMI methods that we need */
#ifdef __STATIC_METHOD_BMI_GM__
    extern struct bmi_method_ops bmi_gm_ops;
#endif /* __STATIC_METHOD_BMI_GM__ */
#ifdef __STATIC_METHOD_BMI_TCP__
    extern struct bmi_method_ops bmi_tcp_ops;
#endif /* __STATIC_METHOD_BMI_TCP__ */

    static struct bmi_method_ops *static_methods[] = {
#ifdef __STATIC_METHOD_BMI_GM__
	&bmi_gm_ops,
#endif				/* __STATIC_METHOD_BMI_GM__ */
#ifdef __STATIC_METHOD_BMI_TCP__
	&bmi_tcp_ops,
#endif				/* __STATIC_METHOD_BMI_TCP__ */
	NULL
    };

    if (((flags & BMI_INIT_SERVER) && (!listen_addr)) || !method_list)
    {
	return (-EINVAL);
    }

    gen_mutex_lock(&interface_mutex);

    /* separate out the method list */
    active_method_count = split_string_list(&requested_methods, method_list);
    if (active_method_count < 1)
    {
	gossip_lerr("Error: bad method list.\n");
	ret = -EINVAL;
	goto bmi_initialize_failure;
    }

    /* create a table to keep up with the active methods */
    active_method_table =
	(struct bmi_method_ops **) malloc(active_method_count *
					  sizeof(struct bmi_method_ops *));
    if (!active_method_table)
    {
	ret = -ENOMEM;
	goto bmi_initialize_failure;
    }
    memset(active_method_table, 0, active_method_count * sizeof(struct
								bmi_method_ops
								*));

    /* find the interface for each requested method and load it into the
     * active table
     */
    for (i = 0; i < active_method_count; i++)
    {
	tmp_method_ops = static_methods;
	while ((*tmp_method_ops) != NULL &&
	       strcmp((*tmp_method_ops)->method_name,
		      requested_methods[i]) != 0)
	{
	    tmp_method_ops++;
	}
	if ((*tmp_method_ops) == NULL)
	{
	    gossip_lerr("Error: no method available for %s.\n",
			requested_methods[i]);
	    ret = -ENOPROTOOPT;
	    goto bmi_initialize_failure;
	}
	active_method_table[i] = (*tmp_method_ops);
    }

    /* make a new reference list */
    cur_ref_list = ref_list_new();
    if (!cur_ref_list)
    {
	ret = -ENOMEM;
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
		ret =
		    active_method_table[i]->BMI_meth_initialize(new_addr, i,
								flags);
	    }
	    else
	    {
		ret = -1;
	    }
	}
	else
	{
	    ret = active_method_table[i]->BMI_meth_initialize(NULL, i, flags);
	}
	if (ret < 0)
	{
	    gossip_lerr("Error: initializing method: %s\n",
			requested_methods[i]);
	    active_method_table[i] = NULL;
	    goto bmi_initialize_failure;
	}
    }
    /* done with method string list now */
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

    gen_mutex_unlock(&interface_mutex);
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

    gen_mutex_unlock(&interface_mutex);
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
	return (-EINVAL);
    }

    /* separate out the module list */
    active_method_count = split_string_list(&modules, module_string);
    if (active_method_count < 1)
    {
	gossip_lerr("Error: bad module list.\n");
	ret = -EINVAL;
	goto bmi_initialize_failure;
    }

    /* create a table to keep up with the method modules */
    active_method_table =
	(struct bmi_method_ops **) malloc(active_method_count *
					  sizeof(struct bmi_method_ops *));
    if (!active_method_table)
    {
	ret = -ENOMEM;
	goto bmi_initialize_failure;
    }

    /* iterate through each method in the list and load its module */
    for (i = 0; i < active_method_count; i++)
    {
	meth_mod = dlopen(modules[i], RTLD_NOW);
	if (!meth_mod)
	{
	    gossip_lerr("Error: could not open module: %s\n", dlerror());
	    ret = -EINVAL;
	    goto bmi_initialize_failure;
	}
	dlerror();

	active_method_table[i] = (struct bmi_method_ops *) dlsym(meth_mod,
								 "method_interface");
	mod_error = dlerror();
	if (mod_error)
	{
	    gossip_lerr("Error: module load: %s\n", mod_error);
	    ret = -EINVAL;
	    goto bmi_initialize_failure;
	}
    }

    /* make a new reference list */
    cur_ref_list = ref_list_new();
    if (!cur_ref_list)
    {
	ret = -ENOMEM;
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
		ret =
		    active_method_table[i]->BMI_meth_initialize(new_addr, i,
								flags);
	    }
	    else
	    {
		ret = -1;
	    }
	}
	else
	{
	    ret = active_method_table[i]->BMI_meth_initialize(NULL, i, flags);
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

/* BMI_finalize()
 * 
 * Shuts down the BMI layer.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_finalize(void)
{
    int i = -1;

    gen_mutex_lock(&interface_mutex);

    /* attempt to shut down active methods */
    for (i = 0; i < active_method_count; i++)
    {
	active_method_table[i]->BMI_meth_finalize();
    }
    active_method_count = 0;
    free(active_method_table);

    /* destroy the reference list */
    /* (side effect: destroys all method addresses as well) */
    ref_list_cleanup(cur_ref_list);

    gen_mutex_unlock(&interface_mutex);
    return (0);
}

/* BMI_open_context()
 *
 * creates a new context to be used for communication; this can be used,
 * for example, to distinguish between operations posted by different
 * threads 
 *
 * returns 0 on success, -errno on failure
 */
int BMI_open_context(bmi_context_id* context_id)
{
    int context_index;
    int i,j;
    int ret = -1;

    gen_mutex_lock(&interface_mutex);

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
	gen_mutex_unlock(&interface_mutex);
	return(-EBUSY);
    }

    /* tell all of the modules about the new context */
    for (i = 0; i < active_method_count; i++)
    {
	ret = active_method_table[i]->BMI_meth_open_context(context_index);
	if(ret < 0)
	{
	    /* one of them failed; kill this context in the previous modules */
	    for(j=(i-1); i>-1; i++)
	    {
		active_method_table[i]->BMI_meth_close_context(context_index);
	    }
	    goto out;
	}
    }

    context_array[context_index] = 1;
    *context_id = context_index;

out:

    gen_mutex_unlock(&interface_mutex);
    return(ret);
}


/* BMI_close_context()
 *
 * destroys a context previous generated with BMI_open_context()
 *
 * no return value
 */
void BMI_close_context(bmi_context_id context_id)
{
    int i;

    gen_mutex_lock(&interface_mutex);

    if(!context_array[context_id])
    {
	gen_mutex_unlock(&interface_mutex);
	return;
    }

    /* tell all of the modules to get rid of this context */
    for (i = 0; i < active_method_count; i++)
    {
	active_method_table[i]->BMI_meth_close_context(context_id);
    }
    context_array[context_id] = 0;

    gen_mutex_unlock(&interface_mutex);
    return;
}


/* BMI_post_recv()
 * 
 * Submits receive operations.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_post_recv(bmi_op_id_t * id,
		  bmi_addr_t src,
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

    gossip_debug(BMI_DEBUG_OFFSETS,
	"BMI_post_recv: addr: %ld, offset: 0x%lx, size: %ld\n", (long)src,
	(long)buffer, (long)expected_size);

    gen_mutex_lock(&interface_mutex);

    *id = 0;

    tmp_ref = ref_list_search_addr(cur_ref_list, src);
    if (!tmp_ref)
    {
	gen_mutex_unlock(&interface_mutex);
	return (-EPROTO);
    }

    ret = tmp_ref->interface->BMI_meth_post_recv(id, tmp_ref->method_addr,
						 buffer, expected_size,
						 actual_size,
						 buffer_type, tag,
						 user_ptr, context_id);
    gen_mutex_unlock(&interface_mutex);
    return (ret);
}


/* BMI_post_send()
 * 
 * Submits send operations.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_post_send(bmi_op_id_t * id,
		  bmi_addr_t dest,
		  void *buffer,
		  bmi_size_t size,
		  enum bmi_buffer_type buffer_type,
		  bmi_msg_tag_t tag,
		  void *user_ptr,
		  bmi_context_id context_id)
{
    ref_st_p tmp_ref = NULL;
    int ret = -1;

    gossip_debug(BMI_DEBUG_OFFSETS,
	"BMI_post_send: addr: %ld, offset: 0x%lx, size: %ld\n", (long)dest,
	(long)buffer, (long)size);

    gen_mutex_lock(&interface_mutex);

    *id = 0;

    tmp_ref = ref_list_search_addr(cur_ref_list, dest);
    if (!tmp_ref)
    {
	gen_mutex_unlock(&interface_mutex);
	return (-EPROTO);
    }

    ret = tmp_ref->interface->BMI_meth_post_send(id, tmp_ref->method_addr,
						 buffer, size,
						 buffer_type, tag,
						 user_ptr, context_id);
    gen_mutex_unlock(&interface_mutex);
    return (ret);
}


/* BMI_post_sendunexpected()
 * 
 * Submits send operations.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_post_sendunexpected(bmi_op_id_t * id,
			    bmi_addr_t dest,
			    void *buffer,
			    bmi_size_t size,
			    enum bmi_buffer_type buffer_type,
			    bmi_msg_tag_t tag,
			    void *user_ptr,
			    bmi_context_id context_id)
{
    ref_st_p tmp_ref = NULL;
    int ret = -1;

    gossip_debug(BMI_DEBUG_OFFSETS,
	"BMI_post_sendunexpected: addr: %ld, offset: 0x%lx, size: %ld\n", 
	(long)dest, (long)buffer, (long)size);

    gen_mutex_lock(&interface_mutex);

    *id = 0;

    tmp_ref = ref_list_search_addr(cur_ref_list, dest);
    if (!tmp_ref)
    {
	gen_mutex_unlock(&interface_mutex);
	return (-EPROTO);
    }

    ret = tmp_ref->interface->BMI_meth_post_sendunexpected(id,
							   tmp_ref->method_addr,
							   buffer, size,
							   buffer_type, tag,
							   user_ptr,
							   context_id);
    gen_mutex_unlock(&interface_mutex);
    return (ret);
}


/* BMI_test()
 * 
 * Checks to see if a particular message has completed.
 *
 * returns 0 on success, -errno on failure
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
	return (-EINVAL);

    gen_mutex_lock(&interface_mutex);

    *outcount = 0;

    target_op = id_gen_fast_lookup(id);
    if (target_op->op_id != id)
    {
	gen_mutex_unlock(&interface_mutex);
	return (-EINVAL);
    }

    ret = active_method_table[target_op->addr->method_type]->BMI_meth_test(id,
									   outcount,
									   error_code,
									   actual_size,
									   user_ptr,
									   max_idle_time_ms,
									   context_id);
    gen_mutex_unlock(&interface_mutex);
    /* return 1 if anything completed */
    if (ret == 0 && *outcount == 1)
    {
	return (1);
    }
    return (ret);
}


/* BMI_testsome()
 * 
 * Checks to see if any messages from the specified list have completed.
 *
 * returns 0 on success, -errno on failure
 */
#ifdef __BMI_SINGLE_METHOD__
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
    int ret = -1;

    if (max_idle_time_ms < 0)
	return (-EINVAL);

    gen_mutex_lock(&interface_mutex);

    *outcount = 0;

    ret = active_method_table[0]->BMI_meth_testsome(incount,
						    id_array, outcount,
						    index_array,
						    error_code_array,
						    actual_size_array,
						    user_ptr_array,
						    max_idle_time_ms,
						    context_id);
    if (ret < 0)
    {
	gen_mutex_unlock(&interface_mutex);
	return (ret);
    }

    gen_mutex_unlock(&interface_mutex);
    /* return 1 if anything completed */
    if (ret == 0 && *outcount > 0)
    {
	return (1);
    }
    return (0);
}

#else /* not __BMI_SINGLE_METHOD__ */
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
    int ret = -1;
    int idle_per_method = 0;
    bmi_op_id_t* tmp_id_array;
    int i,j;
    struct method_op *query_op;
    int need_to_test;
    int tmp_outcount;

    if (max_idle_time_ms < 0)
	return (-EINVAL);

    *outcount = 0;

    /* TODO: do something more clever here */
    if (max_idle_time_ms)
    {
	idle_per_method = max_idle_time_ms / active_method_count;
	if (!idle_per_method)
	    idle_per_method = 1;
    }

    tmp_id_array = (bmi_op_id_t*)malloc(incount*sizeof(bmi_op_id_t));
    if(!tmp_id_array)
	return(-ENOMEM);

    gen_mutex_lock(&interface_mutex);

    /* iterate over each active method */
    for(i=0; i<active_method_count; i++)
    {
	/* setup the tmp id array with only operations that match
	 * that method
	 */
	memset(tmp_id_array, 0, incount*sizeof(bmi_op_id_t));
	need_to_test = 0;
	for(j=0; j<incount; j++)
	{
	    if(id_array[j])
	    {
		query_op = (struct method_op*)id_gen_fast_lookup(id_array[j]);
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
	    if(user_ptr_array)
		ret = active_method_table[i]->BMI_meth_testsome(
		    incount, tmp_id_array, &tmp_outcount, 
		    &(index_array[*outcount]),
		    &(error_code_array[*outcount]),
		    &(actual_size_array[*outcount]),
		    &(user_ptr_array[*outcount]),
		    idle_per_method,
		    context_id);
	    else
		ret = active_method_table[i]->BMI_meth_testsome(
		    incount, tmp_id_array, &tmp_outcount, 
		    &(index_array[*outcount]),
		    &(error_code_array[*outcount]),
		    &(actual_size_array[*outcount]),
		    &(user_ptr_array[*outcount]),
		    idle_per_method,
		    context_id);
	    if(ret < 0)
	    {
		/* can't recover from this... */
		gossip_lerr("Error: critical BMI_testsome failure.\n");
		gen_mutex_unlock(&interface_mutex);
		free(tmp_id_array);
		return(ret);
	    }
	    *outcount += tmp_outcount;
	}
    }


    gen_mutex_unlock(&interface_mutex);

    free(tmp_id_array);

    if(ret == 0 && *outcount > 0)
	return(1);
    else
	return(0);
}
#endif /* __BMI_SINGLE_METHOD__ */


/* BMI_testunexpected()
 * 
 * Checks to see if any unexpected messages have completed.
 *
 * returns 0 on success, -errno on failure
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
    int idle_per_method = 0;

    if (max_idle_time_ms < 0)
	return (-EINVAL);

    gen_mutex_lock(&interface_mutex);

    *outcount = 0;

    /* TODO: do something more clever here */
    if (max_idle_time_ms)
    {
	idle_per_method = max_idle_time_ms / active_method_count;
	if (!idle_per_method)
	    idle_per_method = 1;
    }

    while (position < incount && i < active_method_count)
    {
	ret =
	    active_method_table[i]->
	    BMI_meth_testunexpected((incount - position), &tmp_outcount,
				    (&(sub_info[position])), idle_per_method);
	if (ret < 0)
	{
	    /* can't recover from this */
	    gossip_lerr("Error: critical BMI_testunexpected failure.\n");
	    gen_mutex_unlock(&interface_mutex);
	    return (ret);
	}
	position += tmp_outcount;
	(*outcount) += tmp_outcount;
	i++;
    }

    for (i = 0; i < (*outcount); i++)
    {
	info_array[i].error_code = sub_info[i].error_code;
	info_array[i].buffer = sub_info[i].buffer;
	info_array[i].size = sub_info[i].size;
	info_array[i].tag = sub_info[i].tag;
	tmp_ref = ref_list_search_method_addr(cur_ref_list, sub_info[i].addr);
	if (!tmp_ref)
	{
	    /* yeah, right */
	    gossip_lerr("Error: critical BMI_testunexpected failure.\n");
	    gen_mutex_unlock(&interface_mutex);
	    return (-EPROTO);
	}
	info_array[i].addr = tmp_ref->bmi_addr;
    }
    gen_mutex_unlock(&interface_mutex);
    /* return 1 if anything completed */
    if (ret == 0 && *outcount > 0)
    {
	return (1);
    }
    return (0);
}


/* BMI_testcontext()
 * 
 * Checks to see if any messages from the specified context have completed.
 *
 * returns 0 on success, -errno on failure
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
    int idle_per_method = 0;

    if (max_idle_time_ms < 0)
	return (-EINVAL);

    gen_mutex_lock(&interface_mutex);

    *outcount = 0;

    /* TODO: do something more clever here */
    if (max_idle_time_ms)
    {
	idle_per_method = max_idle_time_ms / active_method_count;
	if (!idle_per_method)
	    idle_per_method = 1;
    }

    while (position < incount && i < active_method_count)
    {
	if(user_ptr_array)
	{
	    ret =
		active_method_table[i]->
		BMI_meth_testcontext((incount - position), 
					(&(out_id_array[position])),
					&tmp_outcount,
					(&(error_code_array[position])), 
					(&(actual_size_array[position])),
					(&(user_ptr_array[position])),
					idle_per_method,
					context_id);
	}
	else
	{
	    ret =
		active_method_table[i]->
		BMI_meth_testcontext((incount - position), 
					(&(out_id_array[position])),
					&tmp_outcount,
					(&(error_code_array[position])), 
					(&(actual_size_array[position])),
					NULL,
					idle_per_method,
					context_id);
	}
	if (ret < 0)
	{
	    /* can't recover from this */
	    gossip_lerr("Error: critical BMI_testcontext failure.\n");
	    gen_mutex_unlock(&interface_mutex);
	    return (ret);
	}
	position += tmp_outcount;
	(*outcount) += tmp_outcount;
	i++;
    }

    gen_mutex_unlock(&interface_mutex);
    /* return 1 if anything completed */
    if (ret == 0 && *outcount > 0)
    {
	return (1);
    }
    return (0);

}


/* BMI_memalloc()
 * 
 * Allocates memory that can be used in native mode by the BMI layer.
 *
 * returns pointer to buffer on success, NULL on failure.
 */
void *BMI_memalloc(bmi_addr_t addr,
		   bmi_size_t size,
		   enum bmi_op_type send_recv)
{
    void *new_buffer = NULL;
    ref_st_p tmp_ref = NULL;

    gen_mutex_lock(&interface_mutex);

    /* find a reference that matches this address */
    tmp_ref = ref_list_search_addr(cur_ref_list, addr);
    if (!tmp_ref)
    {
	gen_mutex_unlock(&interface_mutex);
	return (NULL);
    }

    /* allocate the buffer using the method's mechanism */
    new_buffer = tmp_ref->interface->BMI_meth_memalloc(size, send_recv);

    gen_mutex_unlock(&interface_mutex);
    return (new_buffer);
}

/* BMI_memfree()
 * 
 * Frees memory that was allocated with BMI_memalloc()
 *
 * returns 0 on success, -errno on failure
 */
int BMI_memfree(bmi_addr_t addr,
		void *buffer,
		bmi_size_t size,
		enum bmi_op_type send_recv)
{
    ref_st_p tmp_ref = NULL;
    int ret = -1;

    gen_mutex_lock(&interface_mutex);

    /* find a reference that matches this address */
    tmp_ref = ref_list_search_addr(cur_ref_list, addr);
    if (!tmp_ref)
    {
	gen_mutex_unlock(&interface_mutex);
	return (-EINVAL);
    }

    /* free the memory */
    ret = tmp_ref->interface->BMI_meth_memfree(buffer, size, send_recv);

    gen_mutex_unlock(&interface_mutex);
    return (ret);
}

/* BMI_set_info()
 * 
 * Pass in optional parameters.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_set_info(bmi_addr_t addr,
		 int option,
		 void *inout_parameter)
{
    int ret = -1;
    int i = 0;
    ref_st_p tmp_ref = NULL;

    gen_mutex_lock(&interface_mutex);

    /* if the addr is NULL, then the set_info should apply to all
     * available methods.
     */
    if (!addr)
    {
	if (!active_method_table)
	{
	    gen_mutex_unlock(&interface_mutex);
	    return (-EINVAL);
	}
	for (i = 0; i < active_method_count; i++)
	{
	    ret =
		active_method_table[i]->BMI_meth_set_info(option,
							  inout_parameter);
	    /* we bail out if even a single set_info fails */
	    if (ret < 0)
	    {
		gossip_lerr("Error: failure on set_info to method: %d\n", i);
		gen_mutex_unlock(&interface_mutex);
		return (ret);
	    }
	}
	gen_mutex_unlock(&interface_mutex);
	return (0);
    }

    /* find a reference that matches this address */
    tmp_ref = ref_list_search_addr(cur_ref_list, addr);
    if (!tmp_ref)
    {
	gen_mutex_unlock(&interface_mutex);
	return (-EINVAL);
    }

    /* pass along the set_info to the method */
    ret = tmp_ref->interface->BMI_meth_set_info(option, inout_parameter);
    gen_mutex_unlock(&interface_mutex);
    return (ret);
}

/* BMI_get_info()
 * 
 * Query for optional parameters.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_get_info(bmi_addr_t addr,
		 int option,
		 void *inout_parameter)
{
    int i = 0;
    int maxsize = 0;
    int tmp_maxsize;
    int ret = 0;
    ref_st_p tmp_ref = NULL;

    gen_mutex_lock(&interface_mutex);

    switch (option)
    {
	/* check to see if the interface is initialized */
    case BMI_CHECK_INIT:
	if (active_method_count > 0)
	{
	    gen_mutex_unlock(&interface_mutex);
	    return (0);
	}
	else
	{
	    gen_mutex_unlock(&interface_mutex);
	    return (-ENETDOWN);
	}
	break;
    case BMI_CHECK_MAXSIZE:
	for (i = 0; i < active_method_count; i++)
	{
	    ret =
		active_method_table[i]->BMI_meth_get_info(option, &tmp_maxsize);
	    if (ret < 0)
	    {
		gen_mutex_unlock(&interface_mutex);
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
	break;
    case BMI_GET_METH_ADDR:
	tmp_ref = ref_list_search_addr(cur_ref_list, addr);
	if(!tmp_ref)
	{
	    gen_mutex_unlock(&interface_mutex);
	    return (-EINVAL);
	}
	*((void**) inout_parameter) = tmp_ref->method_addr;
	break;
    default:
	gen_mutex_unlock(&interface_mutex);
	return (-ENOSYS);
	break;
    }
    gen_mutex_unlock(&interface_mutex);
    return (0);
}


/*
 * BMI_addr_lookup()
 *
 * Resolves the string representation of a host address into a BMI
 * address handle.
 *
 * returns 0 on success, -errno on failure.
 */
int BMI_addr_lookup(bmi_addr_t * new_addr,
		    const char *id_string)
{

    ref_st_p new_ref = NULL;
    method_addr_p meth_addr = NULL;
    int ret = -1;
    int i = 0;

    gen_mutex_lock(&interface_mutex);

    /* set the addr to zero in case we fail */
    *new_addr = 0;

    /* First we want to check to see if this host has already been
     * discovered! */
    new_ref = ref_list_search_str(cur_ref_list, id_string);

    if (new_ref)
    {
	/* we found it. */
	*new_addr = new_ref->bmi_addr;
	gen_mutex_unlock(&interface_mutex);
	return (0);
    }

    /* Now we will run through each method looking for one that responds
     * successfully.  It is assumed that they are already listed in order
     * of preference
     */
    i = 0;
    while (i < active_method_count && !(meth_addr =
					active_method_table[i]->
					BMI_meth_method_addr_lookup(id_string)))
    {
	i++;
    }

    /* make sure one was successful */
    if (!meth_addr)
    {
	gossip_lerr("Error: could not resolve id_string!\n");
	gen_mutex_unlock(&interface_mutex);
	return (-ENOPROTOOPT);
    }

    /* create a new reference for the addr */
    new_ref = alloc_ref_st();
    if (!new_ref)
    {
	ret = -ENOMEM;
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
    ref_list_add(cur_ref_list, new_ref);

    *new_addr = new_ref->bmi_addr;
    gen_mutex_unlock(&interface_mutex);
    return (0);

  bmi_addr_lookup_failure:

    if (meth_addr)
    {
	active_method_table[i]->BMI_meth_set_info(BMI_DROP_ADDR, meth_addr);
    }

    if (new_ref)
    {
	if (new_ref->bmi_addr)
	{
	    /* attempt a remove; we don't care if it fails */
	    ref_list_rem(cur_ref_list, new_ref->bmi_addr);
	}
	dealloc_ref_st(new_ref);
    }

    gen_mutex_unlock(&interface_mutex);
    return (ret);
}


/* BMI_post_send_list()
 *
 * like BMI_post_send(), except that the source buffer is 
 * replaced by a list of (possibly non contiguous) buffers
 *
 * returns 0 on success, 1 on immediate successful completion,
 * -errno on failure
 */
int BMI_post_send_list(bmi_op_id_t * id,
		       bmi_addr_t dest,
		       void **buffer_list,
		       bmi_size_t * size_list,
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

    gossip_debug(BMI_DEBUG_OFFSETS,
	"BMI_post_send_list: addr: %ld, count: %d, total_size: %ld\n", 
	(long)dest, list_count, (long)total_size);

    for(i=0; i<list_count; i++)
    {
	gossip_debug(BMI_DEBUG_OFFSETS,
	    "   element %d: offset: 0x%lx, size: %ld\n",
	    i, (long)buffer_list[i], (long)size_list[i]);
    }
#endif

    gen_mutex_lock(&interface_mutex);

    *id = 0;

    tmp_ref = ref_list_search_addr(cur_ref_list, dest);
    if (!tmp_ref)
    {
	gen_mutex_unlock(&interface_mutex);
	return (-EPROTO);
    }

    if (tmp_ref->interface->BMI_meth_post_send_list)
    {
	ret = tmp_ref->interface->BMI_meth_post_send_list(id,
							  tmp_ref->method_addr,
							  buffer_list,
							  size_list, list_count,
							  total_size,
							  buffer_type, tag,
							  user_ptr,
							  context_id);
	gen_mutex_unlock(&interface_mutex);
	return (ret);
    }

    gossip_lerr("Error: method doesn't implement send_list.\n");
    gossip_lerr("Error: send_list emulation not yet available.\n");

    gen_mutex_unlock(&interface_mutex);
    return (-ENOSYS);
}


/* BMI_post_recv_list()
 *
 * like BMI_post_recv(), except that the dest buffer is 
 * replaced by a list of (possibly non contiguous) buffers
 *
 * returns 0 on success, 1 on immediate successful completion,
 * -errno on failure
 */
int BMI_post_recv_list(bmi_op_id_t * id,
		       bmi_addr_t src,
		       void **buffer_list,
		       bmi_size_t * size_list,
		       int list_count,
		       /* "total_expected_size" is the sum of the size list */
		       bmi_size_t total_expected_size,
		       /* "total_actual_size" is the aggregate amt that was received */
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

    gossip_debug(BMI_DEBUG_OFFSETS,
	"BMI_post_recv_list: addr: %ld, count: %d, total_size: %ld\n", 
	(long)src, list_count, (long)total_expected_size);

    for(i=0; i<list_count; i++)
    {
	gossip_debug(BMI_DEBUG_OFFSETS,
	    "   element %d: offset: 0x%lx, size: %ld\n",
	    i, (long)buffer_list[i], (long)size_list[i]);
    }
#endif


    gen_mutex_lock(&interface_mutex);

    *id = 0;

    tmp_ref = ref_list_search_addr(cur_ref_list, src);
    if (!tmp_ref)
    {
	gen_mutex_unlock(&interface_mutex);
	return (-EPROTO);
    }

    if (tmp_ref->interface->BMI_meth_post_recv_list)
    {
	ret = tmp_ref->interface->BMI_meth_post_recv_list(id,
							  tmp_ref->method_addr,
							  buffer_list,
							  size_list, list_count,
							  total_expected_size,
							  total_actual_size,
							  buffer_type, tag,
							  user_ptr,
							  context_id);
	gen_mutex_unlock(&interface_mutex);
	return (ret);
    }

    gossip_lerr("Error: method doesn't implement recv_list.\n");
    gossip_lerr("Error: recv_list emulation not yet available.\n");

    gen_mutex_unlock(&interface_mutex);
    return (-ENOSYS);
}


/* BMI_post_sendunexpected_list()
 *
 * like BMI_post_sendunexpected(), except that the source buffer is 
 * replaced by a list of (possibly non contiguous) buffers
 *
 * returns 0 on success, 1 on immediate successful completion,
 * -errno on failure
 */
int BMI_post_sendunexpected_list(bmi_op_id_t * id,
				 bmi_addr_t dest,
				 void **buffer_list,
				 bmi_size_t * size_list,
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

    gossip_debug(BMI_DEBUG_OFFSETS,
	"BMI_post_sendunexpected_list: addr: %ld, count: %d, total_size: %ld\n", 
	(long)dest, list_count, (long)total_size);

    for(i=0; i<list_count; i++)
    {
	gossip_debug(BMI_DEBUG_OFFSETS,
	    "   element %d: offset: 0x%lx, size: %ld\n",
	    i, (long)buffer_list[i], (long)size_list[i]);
    }
#endif

    gen_mutex_lock(&interface_mutex);

    *id = 0;

    tmp_ref = ref_list_search_addr(cur_ref_list, dest);
    if (!tmp_ref)
    {
	gen_mutex_unlock(&interface_mutex);
	return (-EPROTO);
    }

    if (tmp_ref->interface->BMI_meth_post_send_list)
    {
	ret = tmp_ref->interface->BMI_meth_post_sendunexpected_list(id,
								    tmp_ref->
								    method_addr,
								    buffer_list,
								    size_list,
								    list_count,
								    total_size,
								    buffer_type,
								    tag,
								    user_ptr,
								    context_id);
	gen_mutex_unlock(&interface_mutex);
	return (ret);
    }

    gossip_lerr("Error: method doesn't implement sendunexpected_list.\n");
    gossip_lerr("Error: send_list emulation not yet available.\n");

    gen_mutex_unlock(&interface_mutex);
    return (-ENOSYS);
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

    /* NOTE: we are trusting the method to make sure that we really don't
     * know about the address yet.  No verification done here.
     */

    /* create a new reference structure */
    new_ref = alloc_ref_st();
    if (!new_ref)
    {
	return (-ENOMEM);
    }

    /* fill in the details */
    new_ref->method_addr = map;
    new_ref->id_string = NULL;	/* we don't have an id string for this one. */

    /* check the method_type from the method_addr pointer to know which
     * interface to use */
    new_ref->interface = active_method_table[map->method_type];

    /* add the reference structure to the list */
    ref_list_add(cur_ref_list, new_ref);

    return (0);
}


/************************************************************
 * Internal utility functions
 */

/*
 * split_string_list()
 *
 * separates a comma delimited list of items into an array of strings
 *
 * returns the number of strings successfully parsed
 */
static int split_string_list(char ***tokens,
			     const char *comma_list)
{

    const char *holder = NULL;
    const char *holder2 = NULL;
    const char *end = NULL;
    int tokencount = 1;
    int i = -1;

    if (!comma_list || !tokens)
    {
	return (0);
    }

    /* count how many commas we have first */
    holder = comma_list;
    while ((holder = index(holder, ',')))
    {
	tokencount++;
    }

    /* allocate pointers for each */
    *tokens = (char **) malloc(sizeof(char **));
    if (!(*tokens))
    {
	return 0;
    }

    /* copy out all of the tokenized strings */
    holder = comma_list;
    end = comma_list + strlen(comma_list) + 1;
    for (i = 0; i < tokencount; i++)
    {
	holder2 = index(holder, ',');
	if (!holder2)
	{
	    holder2 = end;
	}
	(*tokens)[i] = (char *) malloc((holder2 - holder) + 1);
	if (!(*tokens)[i])
	{
	    goto failure;
	}
	strncpy((*tokens)[i], holder, (holder2 - holder));
	(*tokens)[i][(holder2 - holder)] = '\0';
	holder = holder2 + 1;

    }

    return (tokencount);

  failure:

    /* free up any memory we allocated if we failed */
    if (*tokens)
    {
	for (i = 0; i < tokencount; i++)
	{
	    if ((*tokens)[i])
	    {
		free((*tokens)[i]);
	    }
	}
	free(*tokens);
    }
    return (0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
