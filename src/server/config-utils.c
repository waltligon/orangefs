/*
 * (C) 2017 Clemson University and Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 */

#include <stddef.h>

#include "config-utils.h"

static struct server_configuration_s *server_config = NULL;


struct server_configuration_s *PINT_get_server_config(void)
{
    return server_config;
}

void PINT_set_server_config(struct server_configuration_s *cfg_p)
{
    server_config = cfg_p;
    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 * 
 * vim: ts=8 sts=4 sw=4 expandtab
 */
