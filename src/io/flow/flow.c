/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This the top level implementation of the flow interface */
/* (see flow.h) */

#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "quicklist.h"
#include "gossip.h"
#include "gen-locks.h"
#include "flow.h"
#include "flowproto-support.h"
#include "flow-ref.h"

/* mutex lock used to prevent more than one process from entering the
 * interface at a time
 */
static gen_mutex_t interface_mutex = GEN_MUTEX_INITIALIZER;

/* number of active flow protocols */
static int active_flowproto_count = 0;
/* table of active flow protocols */
static struct flowproto_ops **active_flowproto_table = NULL;
/* mappings of flow endpoints to the correct protocol */
static flow_ref_p flow_mapping = NULL;

static void flow_release(flow_descriptor * flow_d);
static int split_string_list(char ***tokens,
			     const char *comma_list);

/* PINT_flow_initialize()
 *
 * initializes the flow interface.  Should be called exactly once before
 * any other operations are performed.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_flow_initialize(const char *flowproto_list,
			 int flags)
{
    int ret = -1;
    int i = 0;
    char **requested_flowprotos = NULL;
    struct flowproto_ops **tmp_flowproto_ops = NULL;

    /* bring in the flowproto interfaces that we need */
#ifdef __STATIC_FLOWPROTO_TEMPLATE__
    extern struct flowproto_ops flowproto_template_ops;
#endif /* __STATIC_FLOWPROTO_TEMPLATE__ */
#ifdef __STATIC_FLOWPROTO_BMI_TROVE__
    extern struct flowproto_ops flowproto_bmi_trove_ops;
#endif /* __STATIC_FLOWPROTO_BMI_TROVE__ */
#ifdef __STATIC_FLOWPROTO_DUMP_OFFSETS__
    extern struct flowproto_ops flowproto_dump_offsets_ops;
#endif /* __STATIC_FLOWPROTO_DUMP_OFFSETS__ */
#ifdef __STATIC_FLOWPROTO_BMI_CACHE__
    extern struct flowproto_ops fp_bmi_cache_ops;
#endif /* __STATIC_FLOWPROTO_BMI_CACHE__ */
#ifdef __STATIC_FLOWPROTO_MULTIQUEUE__
    extern struct flowproto_ops fp_multiqueue_ops;
#endif /* __STATIC_FLOWPROTO_MULTIQUEUE__ */

    static struct flowproto_ops *static_flowprotos[] = {
#ifdef __STATIC_FLOWPROTO_TEMPLATE__
	&flowproto_template_ops,
#endif				/* __STATIC_FLOWPROTO_TEMPLATE__ */
#ifdef __STATIC_FLOWPROTO_BMI_TROVE__
	&flowproto_bmi_trove_ops,
#endif				/* __STATIC_FLOWPROTO_BMI_TROVE__ */
#ifdef __STATIC_FLOWPROTO_DUMP_OFFSETS__
	&flowproto_dump_offsets_ops,
#endif				/* __STATIC_FLOWPROTO_DUMP_OFFSETS__ */
#ifdef __STATIC_FLOWPROTO_BMI_CACHE__
	&fp_bmi_cache_ops,
#endif				/* __STATIC_FLOWPROTO_BMI_CACHE__ */
#ifdef __STATIC_FLOWPROTO_MULTIQUEUE__
	&fp_multiqueue_ops,
#endif				/* __STATIC_FLOWPROTO_MULTIQUEUE__ */
	NULL
    };

    gen_mutex_lock(&interface_mutex);

    /* seperate out the list of flowprotos to activate */
    active_flowproto_count = split_string_list(&requested_flowprotos,
					       flowproto_list);
    if (active_flowproto_count < 1)
    {
	gossip_lerr("Error: bad flow protocol list.\n");
	ret = -EINVAL;
	goto PINT_flow_initialize_failure;
    }

    /* create table to keep up with active flow protocols */
    active_flowproto_table =
	(struct flowproto_ops **) malloc(active_flowproto_count *
					 sizeof(struct flowproto_ops *));
    if (!active_flowproto_table)
    {
	ret = -ENOMEM;
	goto PINT_flow_initialize_failure;
    }
    memset(active_flowproto_table, 0, active_flowproto_count * sizeof(struct
								      flowproto_ops
								      *));

    /* find the interface for each requested method and load it into the
     * active table.
     */
    for (i = 0; i < active_flowproto_count; i++)
    {
	tmp_flowproto_ops = static_flowprotos;
	while ((*tmp_flowproto_ops) != NULL &&
	       strcmp((*tmp_flowproto_ops)->flowproto_name,
		      requested_flowprotos[i]) != 0)
	{
	    tmp_flowproto_ops++;
	}
	if ((*tmp_flowproto_ops) == NULL)
	{
	    gossip_lerr("Error: no flowproto available for: %s\n",
			requested_flowprotos[i]);
	    ret = -ENOPROTOOPT;
	    goto PINT_flow_initialize_failure;
	}
	active_flowproto_table[i] = (*tmp_flowproto_ops);
    }

    /* create a cache of mappings to flow protocols */
    flow_mapping = flow_ref_new();
    if (!flow_mapping)
    {
	ret = -ENOMEM;
	goto PINT_flow_initialize_failure;
    }

    /* initialize all of the flow protocols */
    for (i = 0; i < active_flowproto_count; i++)
    {
	ret = active_flowproto_table[i]->flowproto_initialize(i);
	if (ret < 0)
	{
	    gossip_lerr("Error: could not initialize protocol: %s.\n",
			requested_flowprotos[i]);
	    goto PINT_flow_initialize_failure;
	}
    }

    /* get rid of method string list */
    if (requested_flowprotos)
    {
	for (i = 0; i < active_flowproto_count; i++)
	{
	    if (requested_flowprotos[i])
	    {
		free(requested_flowprotos[i]);
	    }
	}
	free(requested_flowprotos);
    }

    gen_mutex_unlock(&interface_mutex);
    return (0);

  PINT_flow_initialize_failure:

    /* shut down any protocols which may have started */
    if (active_flowproto_table)
    {
	for (i = 0; i < active_flowproto_count; i++)
	{
	    if (active_flowproto_table[i])
	    {
		active_flowproto_table[i]->flowproto_finalize();
	    }
	}
	free(active_flowproto_table);
    }

    /* get rid of method string list */
    if (requested_flowprotos)
    {
	for (i = 0; i < active_flowproto_count; i++)
	{
	    if (requested_flowprotos[i])
	    {
		free(requested_flowprotos[i]);
	    }
	}
	free(requested_flowprotos);
    }

    active_flowproto_count = 0;

    if (flow_mapping)
    {
	flow_ref_cleanup(flow_mapping);
    }
    gen_mutex_unlock(&interface_mutex);
    return (ret);
}

/* PINT_flow_finalize()
 *
 * shuts down the flow interface.  
 *
 * returns 0 on success, -errno on failure
 */
int PINT_flow_finalize(void)
{
    int i = 0;
    int ret = -1;

    gen_mutex_lock(&interface_mutex);

    /* shut down each active protocol */
    for (i = 0; i < active_flowproto_count; i++)
    {
	ret = active_flowproto_table[i]->flowproto_finalize();
    }

    free(active_flowproto_table);

    active_flowproto_count = 0;

    flow_ref_cleanup(flow_mapping);

    gen_mutex_unlock(&interface_mutex);
    return (0);
}


/* PINT_flow_alloc()
 * 
 * Allocates a new flow descriptor and sets the source and destination
 * endpoints.
 *
 * returns pointer to descriptor on success, NULL on failure
 */
flow_descriptor *PINT_flow_alloc(void)
{
    flow_descriptor *tmp_desc = NULL;

    tmp_desc = (flow_descriptor *) malloc(sizeof(struct flow_descriptor));
    if (!tmp_desc)
    {
	return (NULL);
    }

    PINT_flow_reset(tmp_desc);

    return (tmp_desc);
}


/* PINT_flow_reset()
 * 
 * resets an existing flow descriptor to its initial state 
 *
 * returns pointer to descriptor on success, NULL on failure
 */
void PINT_flow_reset(flow_descriptor * flow_d)
{

    memset(flow_d, 0, sizeof(struct flow_descriptor));

    flow_d->flowproto_id = -1;
    flow_d->aggregate_size = -1;
    flow_d->state = FLOW_INITIAL;
    flow_d->type = FLOWPROTO_DEFAULT;

    return;
}

/* PINT_flow_free()
 * 
 * destroys a flow descriptor
 *
 * no return value
 */
void PINT_flow_free(flow_descriptor * flow_d)
{
    free(flow_d);
    return;
}


/* PINT_flow_post()
 * 
 * Posts a flow descriptor to the flow interface so that it may be
 * processed
 *
 * returns 0 on success, -errno on failure
 */
int PINT_flow_post(flow_descriptor * flow_d)
{
    int flowproto_id = -1;
    int ret = -1;
    int i;
    int type = flow_d->type;

    assert(flow_d->callback);
    /* sanity check; if the caller doesn't provide a memory datatype,
     * then the must at least indicate the aggregate size to transfer
     */
    assert(flow_d->aggregate_size > -1 || flow_d->mem_req != 0);

    gen_mutex_lock(&interface_mutex);

    /* NOTE: if an error occurs here, then we will normally just return
     * -errno and _not_ set any error codes in the flow descriptor.
     */

    /* search for match to specified flow protocol type */
    for(i=0; i<active_flowproto_count; i++)
    {
	ret =
	    active_flowproto_table[i]->flowproto_getinfo(NULL,
	    FLOWPROTO_TYPE_QUERY,
	    &type);
	if(ret >= 0)
	{
	    flowproto_id = i;
	    break;
	}
    }

    if (flowproto_id < 0)
    {
	gen_mutex_unlock(&interface_mutex);
	gossip_err("Error: requested flow protocol %d, which doesn't appear to be loaded.\n", (int)type);
	return (-ENOPROTOOPT);
    }

    /* setup the request processing states */
    flow_d->file_req_state = PINT_New_request_state(flow_d->file_req);
    if (!flow_d->file_req_state)
    {
	gen_mutex_unlock(&interface_mutex);
	return (-EINVAL);
    }

    /* only setup a memory datatype state if caller provided a memory datatype */
    if(flow_d->mem_req)
    {
	flow_d->mem_req_state = PINT_New_request_state(flow_d->mem_req);
	if (!flow_d->mem_req_state)
	{
	    gen_mutex_unlock(&interface_mutex);
	    return (-EINVAL);
	}
    }

    flow_d->release = flow_release;

    /* post the flow to the flow protocol level */
    ret = active_flowproto_table[flowproto_id]->flowproto_post(flow_d);
    gen_mutex_unlock(&interface_mutex);
    return (ret);
}


/* PINT_flow_unpost()
 * 
 * Aborts a previously posted flow.  
 *
 * returns 0 on success, -errno on failure
 */
int PINT_flow_unpost(flow_descriptor * flow_d)
{
    gen_mutex_lock(&interface_mutex);
    gossip_lerr("function not implemented.\n");
    gen_mutex_unlock(&interface_mutex);
    return (-ENOSYS);
}


/* PINT_flow_setinfo()
 * 
 * Used to pass along hints or configuration info to the flow interface
 *
 * returns 0 on success, -errno on failure
 */
int PINT_flow_setinfo(flow_descriptor * flow_d,
		      int option,
		      void *parameter)
{
    gen_mutex_lock(&interface_mutex);
    gossip_lerr("function not implemented.\n");
    gen_mutex_unlock(&interface_mutex);
    return (-ENOSYS);
}


/* PINT_flow_getinfo()
 * 
 * Used to query for parameters or information from the flow interface
 *
 * returns 0 on success, -errno on failure
 */
int PINT_flow_getinfo(flow_descriptor * flow_d,
		      enum flow_getinfo_option opt,
		      void *parameter)
{
    gen_mutex_lock(&interface_mutex);
    gossip_lerr("function not implemented.\n");
    gen_mutex_unlock(&interface_mutex);
    return (-ENOSYS);
}

/*****************************************************************
 * Internal helper functions
 */

/* TODO: we need to do this from the flow protocol level now */
/* TODO: or else incorporate it into callback */
/* flow_release()
 *
 * releases any resources associated with a flow before returning it to
 * the user
 *
 * no return value
 */
static void flow_release(flow_descriptor * flow_d)
{
    /* let go of the request processing states */
    if (flow_d->file_req_state)
	PINT_Free_request_state(flow_d->file_req_state);
    if (flow_d->mem_req_state)
	PINT_Free_request_state(flow_d->mem_req_state);

    return;
}


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
	holder++;
	tokencount++;
    }

    /* allocate pointers for each */
    *tokens = (char **) malloc(tokencount * sizeof(char **));
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
