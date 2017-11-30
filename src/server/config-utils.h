/*
 * (C) 2017 Clemson University and Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 */
#ifndef __CONFIG_UTILS_H
#define __CONFIG_UTILS_H

struct server_configuration_s *PINT_get_server_config(void);

void PINT_set_server_config(struct server_configuration_s *cfg_p);

#endif /* __CONFIG_UTILS_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 * 
 * vim: ts=8 sts=4 sw=4 expandtab
 */
