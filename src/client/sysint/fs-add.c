/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>

#include "acache.h"
#include "ncache.h"
#include "pint-bucket.h"
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

/* PVFS_sys_fs_add()
 *
 * tells the system interface to dynamically "mount" a new file system
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PVFS_sys_fs_add(struct PVFS_sys_mntent *mntent)
{
    int ret = -PVFS_EINVAL;
    struct server_configuration_s *new_server_config = NULL;

    gen_mutex_lock(&mt_config);

    new_server_config = PINT_server_config_mgr_get_config(mntent->fs_id);
    if (new_server_config)
    {
        PINT_server_config_mgr_put_config(new_server_config);
        PVFS_perror("Configuration for fs already exists", ret);
        return -PVFS_EEXIST;
    }

    new_server_config = (struct server_configuration_s *)malloc(
        sizeof(struct server_configuration_s));
    if (!new_server_config)
    {
        ret = -PVFS_ENOMEM;
        PVFS_perror("Failed to allocate configuration object", ret);
        goto error_exit;
    }
    memset(new_server_config, 0, sizeof(struct server_configuration_s));

    /* get configuration parameters from server */
    ret = PINT_server_get_config(new_server_config, mntent);
    if (ret < 0)
    {
        PVFS_perror("PINT_server_get_config failed", ret);
        goto error_exit;
    }

    /*
      clear out all configuration information about file systems that
      aren't matching the one being added now.  this ensures no
      erroneous handle mappings are added next
    */
    ret = PINT_config_trim_filesystems_except(
        new_server_config, mntent->fs_id);
    if (ret < 0)
    {
        PVFS_perror(
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
        PVFS_perror("PVFS_util_add_mnt failed", ret);
        goto error_exit;
    }

    /* finally, try to add the new config to the server config manager */
    ret = PINT_server_config_mgr_add_config(
        new_server_config, mntent->fs_id);
    if (ret < 0)
    {
        PVFS_util_remove_internal_mntent(mntent);
        PVFS_perror("PINT_server_config_mgr_add_config failed", ret);
        goto error_exit;
    }

    /*
      reload all handle mappings as well as the interface with the new
      configuration information
    */
    PINT_server_config_mgr_reload_bucket_interface();

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

/* PVFS_sys_fs_add()
 *
 * tells the system interface to dynamically "unmount" a mounted file
 * system
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
                PVFS_perror("PINT_server_config_mgr_remove_config "
                            "failed", ret);
            }
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
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
