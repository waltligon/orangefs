/*
 * (C) 2002 Clemson University and The University of Chicago.
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_DIST_UTILS_H
#define __PINT_DIST_UTILS_H

#include "pint-distribution.h"

/**
 * Perform initialization tasks for distributions
 *   register the default distributions
 */
int PINT_dist_intialize(void);

/**
 * Utility default implmentation for PINT_dist_methods get_num_dfiles
 *
 * Returns the number of dfiles suggested by hint parameter if non-zero
 * else returns the number of servers requested
 */
int PINT_dist_default_get_num_dfiles(void* params,
                                     uint32_t num_servers_requested,
                                     uint32_t num_dfiles_requested);

/**
 * Utility default implmentation for PINT_dist_methods set_param
 *
 * Returns the number of dfiles suggested by hint parameter
 */
int PINT_dist_default_set_param(const char* dist_name, void* params,
                                const char* param_name, void* value);

/**
 *
 */
#define PINT_dist_register_parameter(dname, pname, params, field) NULL

#endif

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 noexpandtab
 */
