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
#include "server-config.h"
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
    struct server_configuration_s *server_config = NULL;

    gen_mutex_lock(&mt_config);

    /* get exclusive access to the (global) server config object */
    server_config = PINT_get_server_config_struct();

    PINT_config_release(server_config);

    /* get configuration parameters from server */
    ret = PINT_server_get_config(server_config, mntent);
    if (ret < 0)
    {
        PVFS_perror("PINT_server_get_config failed", ret);
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
    if (ret && (ret != -PVFS_EEXIST))
    {
        PVFS_perror("PVFS_util_add_mnt failed", ret);
        goto error_exit;
    }

    /*
      reload all handle mappings as well as the interface with the new
      configuration information
    */
    PINT_bucket_reinitialize(server_config);

    gen_mutex_unlock(&mt_config);
    PINT_put_server_config_struct(server_config);
    return 0;

  error_exit:
    gen_mutex_unlock(&mt_config);
    if (server_config)
    {
        PINT_put_server_config_struct(server_config);
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
