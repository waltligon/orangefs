/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup sysint
 *
 *  PVFS2 system interface bootstrapping routine to tell the interface
 *  about available file systems.
 */

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>

#include "acache.h"
#include "ncache.h"
#include "pint-cached-config.h"
#include "pvfs2-sysint.h"
#include "pvfs2-util.h"
#include "pint-sysint-utils.h"
#include "gen-locks.h"
#include "pint-servreq.h"
#include "PINT-reqproto-encode.h"
#include "dotconf.h"
#include "trove.h"
#include "server-config-mgr.h"
#include "client-state-machine.h"

gen_mutex_t mt_config = GEN_MUTEX_INITIALIZER;

/** Tell the system interface about the location of a PVFS2 file system.
 *
 * \return 0 on success, -PVFS_error on failure.
 */
int PVFS_sys_fs_add(struct PVFS_sys_mntent *mntent)
{
    int ret = -PVFS_EINVAL;
    int i;
    struct server_configuration_s *new_server_config = NULL;
    PVFS_BMI_addr_t test_addr;

    gen_mutex_lock(&mt_config);

    /* Normally the fs_id value has not been resolved yet at this point, and
     * will be zero.  If it is a non-zero value (and this get_config call
     * succeeds) then it indicates someone has already added this mntent
     * instance.  It is ok to add the same _file system_ twice, but not the
     * same mntent instance.
     */
    new_server_config = PINT_server_config_mgr_get_config(mntent->fs_id);
    if (new_server_config)
    {
        PINT_server_config_mgr_put_config(new_server_config);
        PVFS_perror_gossip("Configuration for fs already exists", ret);
        return -PVFS_EEXIST;
    }

    /* make sure BMI knows how to handle this method, else fail quietly */
    for(i = 0; i < mntent->num_pvfs_config_servers; i++)
    {
        ret = BMI_addr_lookup(&test_addr, mntent->pvfs_config_servers[i]);
        if (ret == 0)
        {
            break;
        }
    }

    if (i == mntent->num_pvfs_config_servers)
    {
        gossip_err("%s: Failed to initialize any appropriate "
                   "BMI methods.\n", __func__);
        goto error_exit;
    }
    mntent->the_pvfs_config_server = mntent->pvfs_config_servers[i];

    new_server_config = (struct server_configuration_s *)malloc(
        sizeof(struct server_configuration_s));
    if (!new_server_config)
    {
        ret = -PVFS_ENOMEM;
        PVFS_perror_gossip("Failed to allocate configuration object", ret);
        goto error_exit;
    }
    memset(new_server_config, 0, sizeof(struct server_configuration_s));

    /* get configuration parameters from server */
    ret = PINT_server_get_config(new_server_config, mntent);
    if (ret < 0)
    {
        PVFS_perror_gossip("PINT_server_get_config failed", ret);
        goto error_exit;
    }

#ifdef USE_TRUSTED
    /* once we know about the server configuration, we need to tell BMI */
    BMI_set_info(0, BMI_TRUSTED_CONNECTION, (void *) new_server_config);
    gossip_debug(GOSSIP_SERVER_DEBUG, "Enabling trusted connections!\n");
#endif

    /*
      clear out all configuration information about file systems that
      aren't matching the one being added now.  this ensures no
      erroneous handle mappings are added next
    */
    ret = PINT_config_trim_filesystems_except(
        new_server_config, mntent->fs_id);
    if (ret < 0)
    {
        PVFS_perror_gossip(
            "PINT_config_trim_filesystems_except failed", ret);
        goto error_exit;
    }

    /*
      add the mntent to the internal mount tables; it's okay if it's
      already there, as the return value will tell us and we can
      ignore it.  in short, if the mntent was from a pvfstab file, it
      should already exist in the tables.  in any other case, it needs
      to be added properly.
    */
    ret = PVFS_util_add_dynamic_mntent(mntent);
    if (ret < 0)
    {
        PVFS_perror_gossip("PVFS_util_add_mnt failed", ret);
        goto error_exit;
    }

    /* finally, try to add the new config to the server config manager */
    ret = PINT_server_config_mgr_add_config(
        new_server_config, mntent->fs_id);
    if (ret < 0)
    {
        PVFS_perror_gossip("PINT_server_config_mgr_add_config failed", ret);
        goto error_exit;
    }

    /*
      reload all handle mappings as well as the interface with the new
      configuration information
    */
    PINT_server_config_mgr_reload_cached_config_interface();

    gen_mutex_unlock(&mt_config);
    return 0;

  error_exit:
    gen_mutex_unlock(&mt_config);
    if (new_server_config)
    {
        PINT_config_release(new_server_config);
        free(new_server_config);
    }
    return ret;
}

/* PVFS_sys_fs_remove()
 *
 * tells the system interface to dynamically "unmount" a mounted file
 * system by removing the configuration info and reloading the cached
 * configuration interface
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PVFS_sys_fs_remove(struct PVFS_sys_mntent *mntent)
{
    int ret = -PVFS_EINVAL;

    if (mntent)
    {
        gen_mutex_lock(&mt_config);
        ret = PVFS_util_remove_internal_mntent(mntent);
        if (ret == 0)
        {
            ret = PINT_server_config_mgr_remove_config(mntent->fs_id);
            if (ret < 0)
            {
                PVFS_perror_gossip("PINT_server_config_mgr_remove_config "
                            "failed", ret);
            }

            /*
              reload all handle mappings as well as the interface with
              the new configuration information
            */
            PINT_server_config_mgr_reload_cached_config_interface();
        }
        gen_mutex_unlock(&mt_config);
    }
    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
