/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This the top level implementation of the flow interface */
/* (see flow.h) */

#include <errno.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "quicklist.h"
#include "gossip.h"
#include "gen-locks.h"
#include "str-utils.h"
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

/* PINT_flow_initialize()
 *
 * initializes the flow interface.  Should be called exactly once before
 * any other operations are performed.
 *
 * flowproto_list specifies which flow protocols to initialize; if NULL,
 * all compiled in flowprotocols will be started
 * TODO: change this so that we can add flowprotocols on the fly as needed
 * rather than having to make the decision on what to init right now
 *
 * returns 0 on success, -errno on failure
 */
int PINT_flow_initialize(
    const char *flowproto_list,
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

    if(flowproto_list)
    {
	/* seperate out the list of flowprotos to activate */
	active_flowproto_count = PINT_split_string_list(
            &requested_flowprotos, flowproto_list);
	if (active_flowproto_count < 1)
	{
	    gossip_lerr("Error: bad flow protocol list.\n");
	    ret = -EINVAL;
	    goto PINT_flow_initialize_failure;
	}
    }
    else
    {
	/* count compiled in flow protocols, we will activate all of them */
	tmp_flowproto_ops = static_flowprotos;
	active_flowproto_count = 0;
	while ((*tmp_flowproto_ops) != NULL)
	{
	    tmp_flowproto_ops++;
	    active_flowproto_count++;
	}
    }

    /* create table to keep up with active flow protocols */
    active_flowproto_table = (struct flowproto_ops **)
        malloc((active_flowproto_count * sizeof(struct flowproto_ops *)));
    if (!active_flowproto_table)
    {
	ret = -ENOMEM;
	goto PINT_flow_initialize_failure;
    }
    memset(active_flowproto_table, 0,
           (active_flowproto_count * sizeof(struct flowproto_ops *)));

    /* find the interface for each requested method and load it into the
     * active table.
     */
    if(flowproto_list)
    {
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
    }
    else
    {
	tmp_flowproto_ops = static_flowprotos;
	for(i=0; i<active_flowproto_count; i++)
	{
	    active_flowproto_table[i] = (*tmp_flowproto_ops);
	    tmp_flowproto_ops++;
	}
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
			active_flowproto_table[i]->flowproto_name);
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

    tmp_desc = (flow_descriptor *)malloc(sizeof(struct flow_descriptor));
    if (tmp_desc)
    {
        tmp_desc->flow_mutex = NULL;
        PINT_flow_reset(tmp_desc);
    }
    return tmp_desc;
}


/* PINT_flow_reset()
 * 
 * resets an existing flow descriptor to its initial state 
 *
 * returns pointer to descriptor on success, NULL on failure
 */
void PINT_flow_reset(flow_descriptor *flow_d)
{
    gen_mutex_t *tmp_mutex = NULL;

    assert(flow_d);

    if (flow_d->flow_mutex)
    {
        tmp_mutex = flow_d->flow_mutex;
    }
    memset(flow_d, 0, sizeof(struct flow_descriptor));

    flow_d->flowproto_id = -1;
    flow_d->aggregate_size = -1;
    flow_d->state = FLOW_INITIAL;
    flow_d->type = FLOWPROTO_DEFAULT;

    flow_d->flow_mutex = (tmp_mutex ? tmp_mutex : gen_mutex_build());
    assert(flow_d->flow_mutex);
}

/* PINT_flow_free()
 * 
 * destroys a flow descriptor
 *
 * no return value
 */
void PINT_flow_free(flow_descriptor *flow_d)
{
    assert(flow_d);
    assert(flow_d->flow_mutex);

    gen_mutex_destroy(flow_d->flow_mutex);
    flow_d->flow_mutex = NULL;

    free(flow_d);
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
    flow_d->flowproto_id = flowproto_id;
    ret = active_flowproto_table[flowproto_id]->flowproto_post(flow_d);
    gen_mutex_unlock(&interface_mutex);
    return (ret);
}


/* PINT_flow_cancel()
 * 
 * attempts to cancel a previously posted (but not yet completed) flow
 *
 * returns 0 on successful attempt, -errno on failure
 */
int PINT_flow_cancel(flow_descriptor * flow_d)
{
    int ret;

    gen_mutex_lock(&interface_mutex);
    assert(flow_d);
    assert(flow_d->flowproto_id >= 0);

    if(active_flowproto_table[flow_d->flowproto_id]->flowproto_cancel)
    {
	ret =
	active_flowproto_table[flow_d->flowproto_id]->flowproto_cancel(flow_d);
    }
    else
    {
	ret = -ENOSYS;
    }
    
    gen_mutex_unlock(&interface_mutex);
    return (ret);
}

/* PINT_flow_setinfo()
 * 
 * Used to pass along hints or configuration info to the flow
 * interface
 *
 * returns 0 on success, -errno on failure
 */
int PINT_flow_setinfo(flow_descriptor *flow_d,
		      int option,
		      void *parameter)
{
    int ret = -ENOSYS, i = 0;

    gen_mutex_lock(&interface_mutex);
    if (flow_d)
    {
        ret = active_flowproto_table[
            flow_d->flowproto_id]->flowproto_setinfo(
                flow_d, option, parameter);
    }
    else
    {
        for(i = 0; i < active_flowproto_count; i++)
        {
            ret = active_flowproto_table[i]->flowproto_setinfo(
                flow_d, option, parameter);
        }
    }
    gen_mutex_unlock(&interface_mutex);

    return -ENOSYS;
}


/* PINT_flow_getinfo()
 * 
 * Used to query for parameters or information from the flow interface
 *
 * returns 0 on success, -errno on failure
 */
int PINT_flow_getinfo(flow_descriptor *flow_d,
		      enum flow_getinfo_option opt,
		      void *parameter)
{
    PVFS_size* tmp_size;

    gen_mutex_lock(&interface_mutex);
    
    switch(opt)
    {
    case FLOW_AMT_COMPLETE_QUERY:
	tmp_size = (PVFS_size*)parameter;
	*tmp_size = flow_d->total_transfered;
	break;
    default:
	break;
    }

    gen_mutex_unlock(&interface_mutex);
    return (0);
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
static void flow_release(flow_descriptor *flow_d)
{
    /* let go of the request processing states */
    if (flow_d->file_req_state)
    {
	PINT_Free_request_state(flow_d->file_req_state);
    }

    if (flow_d->mem_req_state)
    {
	PINT_Free_request_state(flow_d->mem_req_state);
    }
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
