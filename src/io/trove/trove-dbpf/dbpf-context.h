/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __DBPF_CONTEXT_H__
#define __DBPF_CONTEXT_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include "trove-types.h"

int dbpf_open_context(TROVE_context_id *context_id);
int dbpf_close_context(TROVE_context_id context_id);

#if defined(__cplusplus)
}
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif
