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
#include "flow-queue.h"

/* internal queues */
/* note: a flow can exist in at most one of these queues at a time */
static flow_queue_p completion_queue;
static flow_queue_p transmitting_queue;
static flow_queue_p need_svc_queue;
static flow_queue_p scheduled_queue;

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

static int do_one_work_cycle(int *num_completed,
			     int max_idle_time_ms);
static void flow_release(flow_descriptor * flow_d);
static int map_endpoints_to_flowproto(int src_endpoint_id,
				      int dest_endpoint_id);
static void default_scheduler(void);
static int split_string_list(char ***tokens,
			     const char *comma_list);

static int setup_flow_queues(void);
static int teardown_flow_queues(void);

/* tunable parameters */
enum
{
    /* number of flows we are willing to check at once (per protocol) */
    CHECKGLOBAL_COUNT = 5
};

/* PINT_flow_initialize()
 *
 * initializes the flow interface.  Should be called exactly once before
 * any other operations are performed.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_flow_initialize(const char *flowproto_list,
			 PVFS_bitfield flags)
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

    static struct flowproto_ops *static_flowprotos[] = {
#ifdef __STATIC_FLOWPROTO_TEMPLATE__
	&flowproto_template_ops,
#endif				/* __STATIC_FLOWPROTO_TEMPLATE__ */
#ifdef __STATIC_FLOWPROTO_BMI_TROVE__
	&flowproto_bmi_trove_ops,
#endif				/* __STATIC_FLOWPROTO_BMI_TROVE__ */
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

    /* setup queues */
    ret = setup_flow_queues();
    if (ret < 0)
    {
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

    teardown_flow_queues();

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
    flow_d->state = FLOW_INITIAL;
    INIT_QLIST_HEAD(&(flow_d->sched_queue_link));

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

    gen_mutex_lock(&interface_mutex);

    /* NOTE: if an error occurs here, then we will normally just return
     * -errno and _not_ set any error codes in the flow descriptor.
     * It doesn't seem safe to modify it because  we don't really gain 
     * control of the flow until this function completes successfully.
     */

    /* figure out who should handle this flow */
    flowproto_id = map_endpoints_to_flowproto(flow_d->src.endpoint_id,
					      flow_d->dest.endpoint_id);
    if (flowproto_id < 0)
    {
	flow_release(flow_d);
	gen_mutex_unlock(&interface_mutex);
	return (-ENOPROTOOPT);
    }

    /* setup the request processing state */
    flow_d->request_state = PINT_New_request_state(flow_d->request);
    if (!flow_d->request_state)
    {
	flow_release(flow_d);
	gen_mutex_unlock(&interface_mutex);
	return (-EINVAL);
    }

    /* announce the flow */
    ret = active_flowproto_table[flowproto_id]->flowproto_announce_flow(flow_d);
    if (ret < 0)
    {
	flow_release(flow_d);
	gen_mutex_unlock(&interface_mutex);
	return (ret);
    }

    /* put the flow in the correct queue based on the results of the
     * announce_flow() function
     */
    if (flow_d->state & FLOW_FINISH_MASK)
    {
	flow_queue_add(completion_queue, flow_d);
    }
    else if (flow_d->state == FLOW_TRANSMITTING)
    {
	flow_queue_add(transmitting_queue, flow_d);
    }
    else if (flow_d->state == FLOW_SVC_READY)
    {
	flow_queue_add(need_svc_queue, flow_d);
    }
    else
    {
	gossip_lerr("Error: Inconsistent state.\n");
	flow_release(flow_d);
	gen_mutex_unlock(&interface_mutex);
	return (-EPROTO);
    }

    gen_mutex_unlock(&interface_mutex);
    return (0);
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


/* PINT_flow_setpriority()
 *
 * sets the priority level of a flow.  May be safely called before or
 * after the flow is posted.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_flow_setpriority(flow_descriptor * flow_d,
			  PVFS_bitfield priority)
{
    gen_mutex_lock(&interface_mutex);
    gossip_lerr("function not implemented.\n");
    gen_mutex_unlock(&interface_mutex);
    return (-ENOSYS);
}

/* PINT_flow_getpriority()
 *
 * Checks the priority level of a particular flow
 *
 * returns 0 on success, -errno on failure
 */
int PINT_flow_getpriority(flow_descriptor * flow_d,
			  PVFS_bitfield * priority)
{
    gen_mutex_lock(&interface_mutex);
    gossip_lerr("function not implemented.\n");
    gen_mutex_unlock(&interface_mutex);
    return (-ENOSYS);
}


/* PINT_flow_memalloc()
 * 
 * Allocates a region of memory optimized for use in conjunction with a
 * particular endpoint
 *
 * returns pointer to memory region on success, NULL on failure
 */
void *PINT_flow_memalloc(flow_descriptor * flow_d,
			 PVFS_size size,
			 PVFS_bitfield send_recv_flag)
{
    gossip_lerr("function not implemented.\n");
    return (NULL);
}


/* PINT_flow_memfree()
 *
 * Frees a region of memory allocated with PINT_flow_memalloc
 *
 * returns 0 on success, -errno on failure
 */
int PINT_flow_memfree(flow_descriptor * flow_d,
		      void *buffer,
		      PVFS_bitfield send_recv_flag)
{
    gossip_lerr("function not implemented.\n");
    return (-ENOSYS);
}

/* PINT_flow_test()
 *
 * Check for completion of a particular flow; is allowed to do work or
 * briefly block within function
 *
 * returns 0 on success, -errno on failure
 */
int PINT_flow_test(flow_descriptor * flow_d,
		   int *outcount,
		   int max_idle_time_ms)
{
    int ret = -1;
    int num_completed;

    assert(flow_d != 0);

    *outcount = 0;

    gen_mutex_lock(&interface_mutex);

    if(flow_d->state & FLOW_FINISH_MASK)
    {
	flow_queue_remove(flow_d);
	flow_release(flow_d);
	*outcount = 1;
	gen_mutex_unlock(&interface_mutex);
	return(1);
    }

    /* push on work for one round */
    ret = do_one_work_cycle(&num_completed, max_idle_time_ms);

    if (ret < 0)
    {
	gen_mutex_unlock(&interface_mutex);
	return (ret);
    }
    if (num_completed == 0)
    {
	/* don't bother scanning the completion queue again */
	gen_mutex_unlock(&interface_mutex);
	return (0);
    }

    if(flow_d->state & FLOW_FINISH_MASK)
    {
	flow_queue_remove(flow_d);
	flow_release(flow_d);
	*outcount = 1;
	gen_mutex_unlock(&interface_mutex);
	return(1);
    }

    gen_mutex_unlock(&interface_mutex);
    return(0);
}


/* PINT_flow_testsome()
 *
 * Check for completion of any of a specified set of
 * flows; is allowed to do work or briefly block within function
 *
 * returns 0 on success, -errno on failure
 */
int PINT_flow_testsome(int incount,
		       flow_descriptor ** flow_array,
		       int *outcount,
		       int *index_array,
		       int max_idle_time_ms)
{
    int ret = -1;
    int num_completed;
    int i;

    gen_mutex_lock(&interface_mutex);

    *outcount = 0;

    for(i=0; i<incount; i++)
    {
	if(flow_array[i] && (flow_array[i]->state & FLOW_FINISH_MASK))
	{
	    index_array[*outcount] = i;
	    (*outcount)++;
	    flow_queue_remove(flow_array[i]);
	    flow_release(flow_array[i]);
	}
    }

    /* go ahead and return if we found anything the caller wanted */
    if((*outcount) > 0)
    {
	gen_mutex_unlock(&interface_mutex);
	return(1);
    }

    /* push on work for one round */
    ret = do_one_work_cycle(&num_completed, max_idle_time_ms);

    if (ret < 0)
    {
	gen_mutex_unlock(&interface_mutex);
	return (ret);
    }
    if (num_completed == 0)
    {
	/* don't bother checking completion queue again */
	gen_mutex_unlock(&interface_mutex);
	return (0);
    }

    *outcount = 0;

    for(i=0; i<incount; i++)
    {
	if(flow_array[i] && (flow_array[i]->state & FLOW_FINISH_MASK))
	{
	    index_array[*outcount] = i;
	    (*outcount)++;
	    flow_queue_remove(flow_array[i]);
	    flow_release(flow_array[i]);
	}
    }

    gen_mutex_unlock(&interface_mutex);
    if((*outcount) > 0)
	return(1);
    else
	return(0);
}


/* PINT_flow_testworld()
 * 
 * Check for completion of any flows in progress for the
 * flow interface; is allowed to do work or briefly block within
 * function.  This may return unexpected flows as well.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_flow_testworld(int incount,
			flow_descriptor ** flow_array,
			int *outcount,
			int max_idle_time_ms)
{
    flow_descriptor *flow_d = NULL;
    int num_completed = 0;
    int ret = -1;

    gen_mutex_lock(&interface_mutex);

    *outcount = 0;

    /* do some work if the completion queue is empty */
    if (flow_queue_empty(completion_queue))
    {
	ret = do_one_work_cycle(&num_completed, max_idle_time_ms);
	if (ret < 0)
	{
	    return (ret);
	}
    }

    while (*outcount < incount && (flow_d =
				   flow_queue_shownext(completion_queue)))
    {
	flow_array[*outcount] = flow_d;
	flow_queue_remove(flow_d);
	flow_release(flow_d);
	(*outcount)++;
    }

    gen_mutex_unlock(&interface_mutex);
    if (*outcount > 0)
	return (1);
    else
	return (0);
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
		      int option,
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

/* do_one_work_cycle() 
 *
 * performs one flow work cycle (iterating through flow queue,
 * scheduling, etc.)
 *
 */
static int do_one_work_cycle(int *num_completed,
			     int max_idle_time_ms)
{
    flow_descriptor *flow_array[CHECKGLOBAL_COUNT];
    int tmp_count = 0;
    int i = 0;
    int j = 0;
    int ret = -1;
    flow_descriptor *tmp_flow = NULL;

    *num_completed = 0;

    /* divide up the idle time if necessary */
    /* TODO: do something more clever here later */
    if (max_idle_time_ms)
    {
	max_idle_time_ms = max_idle_time_ms / active_flowproto_count;
	if (!max_idle_time_ms)
	    max_idle_time_ms = 1;
    }

    /* what should happen here? */
    /* 1) service flows that are currently transmitting, and move any to
     * need_svc or completion queues as needed
     * 2) apply scheduling filter to need_svc queue to generate scheduled
     * queue
     * 3) service each of the flows in the scheduled queue in order
     */

    gossip_ldebug(FLOW_DEBUG, "do_one_work_cycle checking.\n");
    /* perform a checkworld for each protocol */
    for (i = 0; i < active_flowproto_count; i++)
    {
	tmp_count = CHECKGLOBAL_COUNT;
	ret = active_flowproto_table[i]->flowproto_checkworld(flow_array,
							      &tmp_count,
							      max_idle_time_ms);
	if (ret < 0)
	{
	    /* no good way to clean this up */
	    gossip_lerr("Error: Critical failure.\n");
	    return (ret);
	}

	/* handle anything returned by checkworld */
	for (j = 0; j < tmp_count; j++)
	{
	    if (flow_array[j]->state & FLOW_FINISH_MASK)
	    {
		/* move the flow to the completion queue */
		flow_queue_remove(flow_array[j]);

		flow_queue_add(completion_queue, flow_array[j]);
	    }
	    else if (flow_array[j]->state == FLOW_SVC_READY)
	    {
		/* move the flow to the need service queue */
		flow_queue_remove(flow_array[j]);

		flow_queue_add(need_svc_queue, flow_array[j]);
	    }
	    else
	    {
		/* we should not get here */
		gossip_lerr("Error: inconsistent state!\n");
		return (ret);
	    }
	}
    }

    gossip_ldebug(FLOW_DEBUG, "do_one_work_cycle scheduling.\n");
    /* schedule the next round of service */
    default_scheduler();

    gossip_ldebug(FLOW_DEBUG, "do_one_work_cycle servicing.\n");
    /* go through and service each scheduled flow in order */
    while ((tmp_flow = flow_queue_shownext(scheduled_queue)))
    {
	ret =
	    active_flowproto_table[tmp_flow->flowproto_id]->
	    flowproto_service(tmp_flow);
	if (ret < 0)
	{
	    /* no good way to clean this up */
	    gossip_lerr("Error: Critical failure.\n");
	    return (ret);
	}
	/* take the flow out of this queue */
	flow_queue_remove(tmp_flow);
	/* put the flow in the correct queue based on the result */
	if (tmp_flow->state & FLOW_FINISH_MASK)
	{
	    flow_queue_add(completion_queue, tmp_flow);
	}
	else if (tmp_flow->state == FLOW_SVC_READY)
	{
	    flow_queue_add(need_svc_queue, tmp_flow);
	}
	else if (tmp_flow->state == FLOW_TRANSMITTING)
	{
	    flow_queue_add(transmitting_queue, tmp_flow);
	}
	else
	{
	    /* can't recover from this */
	    gossip_lerr("Error: invalid state reached.\n");
	    return (-EPROTO);
	}

    }

    gossip_ldebug(FLOW_DEBUG, "do_one_work_cycle done.\n");
    return (0);
}

/* setup_queues()
 *
 * puts all global internal queues into their initial state
 *
 * returns 0 on success, -errno on failure
 */
static int setup_flow_queues(void)
{
    completion_queue = flow_queue_new();
    transmitting_queue = flow_queue_new();
    need_svc_queue = flow_queue_new();
    scheduled_queue = flow_queue_new();

    if (!completion_queue || !transmitting_queue ||
	!need_svc_queue || !scheduled_queue)
    {
	if (completion_queue)
	    flow_queue_cleanup(completion_queue);
	if (transmitting_queue)
	    flow_queue_cleanup(transmitting_queue);
	if (need_svc_queue)
	    flow_queue_cleanup(need_svc_queue);
	if (scheduled_queue)
	    flow_queue_cleanup(scheduled_queue);
	return (-ENOMEM);
    }

    return (0);
}

/* teardown_queues()
 *
 * shuts down all global internal queues
 *
 * returns 0 on success, -errno on failure
 */
static int teardown_flow_queues(void)
{
    flow_queue_cleanup(completion_queue);
    flow_queue_cleanup(transmitting_queue);
    flow_queue_cleanup(need_svc_queue);
    flow_queue_cleanup(scheduled_queue);

    return (0);
}

/* flow_release()
 *
 * releases any resources associated with a flow before returning it to
 * the user
 *
 * no return value
 */
static void flow_release(flow_descriptor * flow_d)
{
    /* let go of the request processing state */
    if (flow_d->request_state)
    {
	PINT_Free_request_state(flow_d->request_state);
    }
    return;
}

/* map_endpoints_to_flowproto()
 *
 * finds the flow protocol capable of handling a particular pair of
 * endpoints
 *
 * returns flowprotocol id on success, -errno on failure
 */
static int map_endpoints_to_flowproto(int src_endpoint_id,
				      int dest_endpoint_id)
{
    struct flow_ref_entry *query_entry = NULL;
    int i = 0;
    int ret = -1;
    struct flowproto_type_support type_query;

    /* check cache first */
    query_entry = flow_ref_search(flow_mapping, src_endpoint_id,
				  dest_endpoint_id);
    if (query_entry)
    {
	return (query_entry->flowproto_id);
    }

    type_query.src_endpoint_id = src_endpoint_id;
    type_query.dest_endpoint_id = dest_endpoint_id;

    /* not in cache; query each active method */
    for (i = 0; i < active_flowproto_count; i++)
    {
	ret = active_flowproto_table[i]->flowproto_getinfo(NULL,
							   FLOWPROTO_SUPPORT_QUERY,
							   &type_query);
	if (ret >= 0)
	{
	    /* found a match; add it to the cache and return */
	    flow_ref_add(flow_mapping, src_endpoint_id, dest_endpoint_id, ret);
	    return (ret);
	}
    }

    /* didn't find it */
    return (-ENOPROTOOPT);
}

/* default_scheduler()
 *
 * responsible for making scheduling decisions.  It scans the need_svc
 * queue, picks the flows it thinks should be serviced, and moves them
 * to the scheduled queue (in whatever order it deems fit).  
 * NOTE: this one is just a placeholder.  It blindly moves _everything_
 * to the scheduled queue.
 *
 * no return value
 */
static void default_scheduler(void)
{
    flow_descriptor *tmp_flow = NULL;

    /* manually go through the entire queue and move it to the scheduled
     * queue 
     */
    while ((tmp_flow = flow_queue_shownext(need_svc_queue)))
    {
	/* move the flow */
	flow_queue_remove(tmp_flow);
	/* NOTE: I don't have to lock the scheduled queue, because the
	 * calling process can't touch it directly.
	 */
	flow_queue_add(scheduled_queue, tmp_flow);

    }

    return;
};


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
