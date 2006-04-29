/*
 * (C) 2006 The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#ifndef _VEC_PROT_CLIENT_H
#define _VEC_PROT_CLIENT_H

#include <sys/socket.h>
#include "vec_prot.h"
#include "vec_config.h"

struct vec_options {
	int tcp;
	struct sockaddr *vec_addr;
};

int vec_ping(struct vec_options *opt);
int vec_get(struct vec_options *opt,
		vec_handle_t handle, vec_mode_t mode,
		vec_offset_t offset, vec_size_t size,
        vec_stripe_size_t stripe_size, vec_server_count_t nservers,
        int max_nvectors, vec_vectors_t *vector,
		vec_svectors_t *svector);
int vec_put(struct vec_options *opt,
		vec_handle_t handle, vec_mode_t mode,
		vec_offset_t offset, vec_size_t size,
        vec_stripe_size_t stripe_size, vec_server_count_t nservers,
        vec_vectors_t *vector);
#endif
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
