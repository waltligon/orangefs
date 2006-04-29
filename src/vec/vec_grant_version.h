#ifndef _VEC_GRANT_VERSION_H
#define _VEC_GRANT_VERSION_H

#include <stdio.h>
#include "pvfs2.h"
#include "pvfs2-types.h"
#include "vec_prot.h"

struct handle_vec_cache_s {
	PVFS_object_ref ref;
	PVFS_offset 	 offset;
	PVFS_size       size;
	int             stripe_size;
	int             nservers;
};

typedef struct handle_vec_cache_s handle_vec_cache_args;

extern int 	PINT_handle_vec_cache_init(void);
extern void PINT_handle_vec_cache_finalize(void);
extern int 	PINT_inc_vec(handle_vec_cache_args *req, vec_vectors_t *new_vec);
extern int 	PINT_get_vec(handle_vec_cache_args *req, vec_svectors_t *svec);

#endif
