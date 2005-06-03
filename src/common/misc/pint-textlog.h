/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_TEXTLOG_H
#define __PINT_TEXTLOG_H

#include "pvfs2-types.h"
#include "pvfs2-mgmt.h"
#include "pvfs2-event.h"

/* writes the events in the queue to a textlog */
void PINT_textlog_generate(
    struct PVFS_mgmt_event * events,
    int count,
    const char * filename);

#endif /* __PINT_TEXTLOG_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

