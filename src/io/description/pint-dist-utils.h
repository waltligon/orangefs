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
 *   - register the default distributions
 */
int PINT_dist_initialize(void);

/**
 * Perform cleanup actions before exiting
 */
void PINT_dist_finalize(void);

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
 * Sets the specified parameter.  The parameter must have been registered
 * with PINT_dist_register_param or PINT_dist_register_param_offset, so
 * implementation may not work correctly with complex structs or pointer
 * values.
 */
int PINT_dist_default_set_param(const char* dist_name, void* params,
                                const char* param_name, void* value);

/**
 * Register the parameter offset.
 *
 * Use PINT_dist_register_param to avoid having to determine the offset
 * and field size.
 */
int PINT_dist_register_param_offset(const char* dist_name,
                             const char* param_name,
                             size_t offset,
                             size_t field_size);

/**
 * Wrapper macro to make adding parameter fields easy.
 *
 * Uses the offsetof trick to locate the offset for the struct field
 * and the sizeof operator to determine the size.  This is only reliable
 * with POD data, complex structs and pointers may not work as expected.
 */
#define PINT_dist_register_param(dname, pname, param_type, param_member) \
    PINT_dist_register_param_offset(dname, pname, \
                                    (size_t)&((param_type*)0)->param_member, \
                                    sizeof(((param_type*)0)->param_member))


#endif

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */
