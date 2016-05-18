/* 
 * (C) 2010 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 *
 * security-stubs.c - this file is used instead of pint-security.c when
 *    --enable-security is NOT used with configure
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "pvfs2-config.h"
#include "pvfs2-types.h"
#include "pint-util.h"
#include "server-config.h"
#include "pint-cached-config.h"

#include "pint-security.h"
#include "security-util.h"


int PINT_security_initialize(void)
{
    return 0;
}

int PINT_security_finalize(void)
{
    return 0;
}

int PINT_init_capability(PVFS_capability *cap)
{
    memset(cap, 0, sizeof(*cap));
    
    return 0;
}

int PINT_sign_capability(PVFS_capability *cap, PVFS_time *force_timeout)
{
    const struct server_configuration_s *config = PINT_get_server_config();

    
    /* if we want to set a particular timeout, send in a value for
     * force_timeout.  We do this for batch-create, the internal server
     * process that gathers handles from other servers.  Otherwise, use
     * the default timeout set in the config file.
     */ 
    if ( force_timeout )
    {
       cap->timeout = PINT_util_get_current_time() + *force_timeout;
    }
    else
    {
       cap->timeout = PINT_util_get_current_time() + config->capability_timeout;
    }
    cap->sig_size = 0;
    cap->signature = NULL;

    return 0;
}

int PINT_server_to_server_capability(PVFS_capability *capability,
                                     PVFS_fs_id fs_id,
                                     int num_handles,
                                     PVFS_handle *handle_array)
{   
    int ret = -PVFS_EINVAL;
    server_configuration_s *config = PINT_get_server_config();
        
    ret = PINT_init_capability(capability);
    if (ret < 0)
    {
        return -PVFS_ENOMEM;
    }
    capability->issuer =
        malloc(strlen(config->server_alias) + 3);
    if (capability->issuer == NULL)
    {
        return -PVFS_ENOMEM;
    }
    strcpy(capability->issuer, "S:");
    strcat(capability->issuer, config->server_alias);

    capability->fsid = fs_id;
    capability->timeout =
        PINT_util_get_current_time() + config->capability_timeout;
    capability->op_mask = ~((uint32_t)0);
    capability->num_handles = num_handles;
    capability->handle_array = handle_array;
    ret = PINT_sign_capability(capability,NULL);
    if (ret < 0)
    {
        return -PVFS_EINVAL;
    }
    return 0;
}

int PINT_verify_capability(const PVFS_capability *cap)
{
    struct server_configuration_s *config = PINT_get_server_config();

    if (!cap)
    {
        return 0;
    }

    if (PINT_capability_is_null(cap))
    {
        return 1;
    }

    /* Are we suppose to check for timeouts? */
    if ( !config->bypass_timeout_check )
    {
       /* check capability timeout */
       if (PINT_util_get_current_time() > cap->timeout)
       {
           return 0;
       }
    }

    
    return 1;
}

int PINT_init_credential(PVFS_credential *cred)
{
    memset(cred, 0, sizeof(*cred));

    return 0;
}

int PINT_sign_credential(PVFS_credential *cred)
{
    const struct server_configuration_s *config;

    config = PINT_get_server_config();
    assert(config && config->server_alias);
    
    if (cred->issuer == NULL)
    {
        return -PVFS_ENOMEM;
    }
    strcpy(cred->issuer, "S:");
    strcat(cred->issuer, config->server_alias);

    cred->timeout = PINT_util_get_current_time() + config->credential_timeout;

    cred->sig_size = 0;
    cred->signature = NULL;

    return 0;
}

int PINT_verify_credential(const PVFS_credential *cred)
{
    struct server_configuration_s *config = PINT_get_server_config();

    if (!cred)
    {
        return 0;
    }

    /* Are we suppose to check for timeouts? */
    if ( !config->bypass_timeout_check )
    {
       /* Check credential timeout */
       if (PINT_util_get_current_time() > cred->timeout)
       {
           return 0;
       }
    }

    return 1;
}

