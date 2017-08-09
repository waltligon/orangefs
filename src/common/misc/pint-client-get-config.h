/* 
 * (C) 2003 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#ifndef PINT_CLIENT_GET_CONFIG_H
#define PINT_CLIENT_GET_CONFIG_H

#include <string.h>
#include <assert.h>
#ifndef WIN32
#include <unistd.h>
#endif

/* I'm sure we need some of these for the arg types, but probably not all
 * at some point we can figure out what we need
 */
#include "client-state-machine.h"
#include "pvfs2-debug.h"
#include "pvfs2-util.h"
#include "job.h"
#include "gossip.h"
#include "str-utils.h"
#include "pint-cached-config.h"
#include "PINT-reqproto-encode.h"
#include "security-util.h"
#include "sid.h"

/* I cannot find any code that references this function, or a header
 * for it.  Maybe it is not needed any more. If you find that it IS used please
 * change this comment to reflect where it is called from
 */

/* <====================== PUBLIC FUNCTIONS =====================> */

/*
  given mount information, retrieve the server's configuration by
  issuing a getconfig operation.  on successful response, we parse the
  configuration and fill in the config object specified.

  returns 0 on success, -errno on error
*/
int PINT_client_get_config(struct server_configuration_s *config,
                           struct PVFS_sys_mntent* mntent_p,
                           const PVFS_credential *credential,
                           PVFS_hint hints);

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */
#endif
