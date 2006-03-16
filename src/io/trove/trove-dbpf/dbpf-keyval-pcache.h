/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __DBPF_KEYVAL_PCACHE_H
#define __DBPF_KEYVAL_PCACHE_H

#if defined(__cplusplus)
extern "C" {
#endif

#include "gen-locks.h"
#include "tcache.h"
#include "trove.h"

typedef struct PINT_dbpf_keyval_pcache_s
{
    struct PINT_tcache * tcache;
    gen_mutex_t * mutex;
} PINT_dbpf_keyval_pcache;

PINT_dbpf_keyval_pcache * PINT_dbpf_keyval_pcache_initialize(void);
void PINT_dbpf_keyval_pcache_finalize(PINT_dbpf_keyval_pcache * cache);

int PINT_dbpf_keyval_pcache_lookup(
    PINT_dbpf_keyval_pcache *pcache,
    TROVE_handle handle,
    TROVE_ds_position pos,
    const char ** keyname,
    int * length);

int PINT_dbpf_keyval_pcache_insert( 
    PINT_dbpf_keyval_pcache *pcache,
    TROVE_handle handle,
    TROVE_ds_position pos,
    const char * keyname,
    int length);

#if defined(__cplusplus)
} /* extern C */
#endif 

#endif /* __DBPF_KEYVAL_PCACHE_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */    

