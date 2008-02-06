/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* preallocates buffers to be used for GM communication */

#include "gossip.h"
#include "quicklist.h"

#include<gm.h>

#ifndef __BMI_GM_BUFFERPOOL_H
#define __BMI_GM_BUFFERPOOL_H

struct bufferpool
{
    struct qlist_head cache_head;
    struct gm_port *local_port;
    int num_buffers;
};

struct bufferpool *bmi_gm_bufferpool_init(struct gm_port *current_port,
					  int num_buffers,
					  unsigned long buffer_size);
void bmi_gm_bufferpool_finalize(struct bufferpool *bp);
void *bmi_gm_bufferpool_get(struct bufferpool *bp);
void bmi_gm_bufferpool_put(struct bufferpool *bp,
			   void *buffer);
int bmi_gm_bufferpool_empty(struct bufferpool *bp);

#endif /* __BMI_GM_BUFFERPOOL_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
