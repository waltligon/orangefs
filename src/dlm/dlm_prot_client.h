/*
 * (C) 2006 The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef _DLM_PROT_CLIENT_H
#define _DLM_PROT_CLIENT_H

#include <sys/socket.h>
#include "dlm_prot.h"
#include "dlm_config.h"

struct dlm_options {
	int tcp;
	struct sockaddr *dlm_addr;
};
int dlm_ping(struct dlm_options *opt);
int dlm_lock(struct dlm_options *opt,
		dlm_handle_t handle, dlm_mode_t mode,
		dlm_offset_t offset, dlm_size_t size, dlm_token_t *token);
int dlm_lockv(struct dlm_options *opt,
		dlm_handle_t handle, dlm_mode_t mode,
		int nregions, 
		dlm_offset_t *offset, dlm_size_t *size, dlm_token_t *token);
int dlm_unlock(struct dlm_options *opt,
		dlm_token_t token);

#endif
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
