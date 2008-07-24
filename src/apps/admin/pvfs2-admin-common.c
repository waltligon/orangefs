/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */
#include "job.h"
#include "server-config.h"

job_context_id server_job_context;
static server_configuration_s server_config;

server_configuration_s *get_server_config_struct()
{
    return &server_config;
}

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */
