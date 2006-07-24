/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* preallocates buffers to be used for GM communication */

#include<errno.h>
#include<stdlib.h>
#include<gm.h>

#include "gossip.h"
#include "quicklist.h"
#include "bmi-gm-bufferpool.h"

struct cache_entry
{
    struct qlist_head link;
};

/* bmi_gm_bufferpool_init()
 * 
 * initializes the ctrl buffer cache
 *
 * returns pointer to new buffer pool on success, NULL on failure
 */
struct bufferpool *bmi_gm_bufferpool_init(struct gm_port *current_port,
					  int num_buffers,
					  unsigned long buffer_size)
{
    int i = 0;
    struct cache_entry *tmp_entry = NULL;
    struct bufferpool *tmp_bp = NULL;

    if (buffer_size < sizeof(struct cache_entry))
    {
	return (NULL);
    }

    tmp_bp = malloc(sizeof(struct bufferpool));
    if (!tmp_bp)
    {
	return (NULL);
    }
    INIT_QLIST_HEAD(&tmp_bp->cache_head);
    tmp_bp->local_port = current_port;
    tmp_bp->num_buffers = num_buffers;

    for (i = 0; i < num_buffers; i++)
    {
	tmp_entry = (struct cache_entry *) gm_dma_malloc(tmp_bp->local_port,
							 buffer_size);
	if (!tmp_entry)
	{
	    bmi_gm_bufferpool_finalize(tmp_bp);
	    return (NULL);
	}
	qlist_add(&(tmp_entry->link), &tmp_bp->cache_head);
    }
    return (tmp_bp);
}

/* bmi_gm_bufferpool_finalize()
 *
 * shuts down the ctrl buffer cache 
 *
 * returns 0 on success, -errno on failure
 */
void bmi_gm_bufferpool_finalize(struct bufferpool *bp)
{
    struct cache_entry *tmp_entry = NULL;
    struct qlist_head *tmp_link = NULL;

    while (bp->cache_head.next != &bp->cache_head)
    {
	tmp_link = bp->cache_head.next;
	qlist_del(tmp_link);
	tmp_entry = qlist_entry(tmp_link, struct cache_entry,
				link);
	gm_dma_free(bp->local_port, tmp_entry);
    }

    free(bp);

    return;
}

/* bmi_gm_bufferpool_get()
 *
 * gets a buffer from the control cache
 *
 * returns pointer to buffer
 */
void *bmi_gm_bufferpool_get(struct bufferpool *bp)
{
    struct cache_entry *tmp_entry = NULL;
    struct qlist_head *tmp_link = NULL;

    if (bp->cache_head.next == &bp->cache_head)
    {
	return (NULL);
    }

    tmp_link = bp->cache_head.next;
    qlist_del(tmp_link);
    tmp_entry = qlist_entry(tmp_link, struct cache_entry, link);

    return ((void *) tmp_entry);
}

/* bmi_gm_bufferpool_put()
 *
 * places a buffer back into the ctrl cache
 *
 * no return value
 */
void bmi_gm_bufferpool_put(struct bufferpool *bp,
			   void *buffer)
{
    struct cache_entry *tmp_entry = NULL;

    /* NOTE: we are using qlist_add (and not qlist_add_tail) because we
     * want to reuse recently used buffers if possible
     */

    tmp_entry = (struct cache_entry *) buffer;
    qlist_add(&(tmp_entry->link), &bp->cache_head);

    return;
}

/* bmi_gm_bufferpool_empty()
 *
 * checks to see if a buffer pool is empty
 *
 * returns 1 if empty, 0 otherwise
 */
int bmi_gm_bufferpool_empty(struct bufferpool *bp)
{
    return (qlist_empty(&bp->cache_head));
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
