/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "trove.h"
#include "trove-internal.h"
#include "dbpf.h"
#include "dbpf-op-queue.h"
#include "gossip.h"

static gen_mutex_t dbpf_context_mutex = GEN_MUTEX_INITIALIZER;
dbpf_op_queue_p dbpf_completion_queue_array[TROVE_MAX_CONTEXTS] = {NULL};
gen_mutex_t *dbpf_completion_queue_array_mutex[TROVE_MAX_CONTEXTS];

int dbpf_open_context(
    TROVE_coll_id coll_id,
    TROVE_context_id *context_id)
{
    int context_index = 0;

    assert(context_id);

    gen_mutex_lock(&dbpf_context_mutex);

    /* find an unused context id */
    for(context_index = 0; context_index < TROVE_MAX_CONTEXTS;
        context_index++)
    {
	if (dbpf_completion_queue_array[context_index] == NULL)
	{
	    break;
	}
    }

    if (context_index >= TROVE_MAX_CONTEXTS)
    {
	/* we don't have any more available! */
	gen_mutex_unlock(&dbpf_context_mutex);
	return(-EBUSY);
    }

    /* create a new completion queue for the context */
    dbpf_completion_queue_array[context_index] =
        dbpf_op_queue_new();
    if(!dbpf_completion_queue_array[context_index])
    {
	gen_mutex_unlock(&dbpf_context_mutex);
	return(-ENOMEM);
    }
    dbpf_completion_queue_array_mutex[context_index] = gen_mutex_build();

    *context_id = context_index;
    gen_mutex_unlock(&dbpf_context_mutex);
    return(0);
}

int dbpf_close_context(
    TROVE_coll_id coll_id,
    TROVE_context_id context_id)
{
    gen_mutex_lock(&dbpf_context_mutex);

    if (!dbpf_completion_queue_array[context_id])
    {
	gen_mutex_unlock(&dbpf_context_mutex);
	return 1;
    }

    /*
      FIXME: can't free these, as gen_mutex_destroy thinks it can
      so I have to resort to using pthread_mutex_destroy instead
    */
    gen_mutex_destroy(dbpf_completion_queue_array_mutex[context_id]);
    dbpf_op_queue_cleanup(dbpf_completion_queue_array[context_id]);

    dbpf_completion_queue_array[context_id] = NULL;

    gen_mutex_unlock(&dbpf_context_mutex);
    return 0;
}

/* dbpf_context_ops
 *
 * Structure holding pointers to all the context operations functions
 * for this storage interface implementation.
 */
struct TROVE_context_ops dbpf_context_ops =
{
    dbpf_open_context,
    dbpf_close_context
};
