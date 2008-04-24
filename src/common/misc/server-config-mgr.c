/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <ctype.h>

#include "pvfs2.h"
#include "server-config-mgr.h"
#include "quickhash.h"
#include "gen-locks.h"
#include "gossip.h"
#include "pint-cached-config.h"

/*
  this is an internal structure and shouldn't be used by anyone except
  this module
*/
typedef struct
{
    struct qlist_head hash_link;

    PVFS_fs_id fs_id;

    struct server_configuration_s *server_config;
    int ref_count; /* allows same config to be added multiple times */
} server_config_t;

static struct qhash_table *s_fsid_to_config_table = NULL;
static gen_mutex_t s_server_config_mgr_mutex = GEN_MUTEX_INITIALIZER;
/*
  while loading configuration settings for all known file systems
  (across all configured servers), we keep track of the minimum handle
  recycle timeout for *any* file system, and expose this value since
  this is the only place that has access to all of this information.
*/
static int s_min_handle_recycle_timeout_in_sec = -1;

static int hash_fsid(void *key, int table_size);
static int hash_fsid_compare(void *key, struct qlist_head *link);

#define SC_MGR_INITIALIZED() \
(s_fsid_to_config_table)

/*
  this is a check that needs to be made each time the
  s_server_config_mgr_mutex is acquired.  it handles the case of a
  caller waiting on a mutex and acquiring it after a different caller
  finalized the interface
*/
#define SC_MGR_ASSERT_OK(err)   \
do {                            \
    if (!SC_MGR_INITIALIZED()) {\
        return err;             \
    }                           \
} while(0)

int PINT_server_config_mgr_initialize(void)
{
    int ret = 0;

    if (s_fsid_to_config_table == NULL)
    {
        s_fsid_to_config_table =
            qhash_init(hash_fsid_compare, hash_fsid, 17);
        if (s_fsid_to_config_table)
        {
            s_min_handle_recycle_timeout_in_sec = -1;
            ret = 0;
        }
        else
        {
            ret = -PVFS_ENOMEM;
        }
    }
    return ret;
}

int PINT_server_config_mgr_finalize(void)
{
    int ret = -PVFS_EINVAL, i = 0;
    server_config_t *config = NULL;
    struct qlist_head *hash_link = NULL;

    if (SC_MGR_INITIALIZED())
    {
        gen_mutex_lock(&s_server_config_mgr_mutex);
        SC_MGR_ASSERT_OK(ret);

        for (i = 0; i < s_fsid_to_config_table->table_size; i++)
        {
            do
            {
                hash_link = qhash_search_and_remove_at_index(
                    s_fsid_to_config_table, i);
                if (hash_link)
                {
                    config = qlist_entry(
                        hash_link, server_config_t, hash_link);
                    assert(config);
                    assert(config->server_config);

                    PINT_config_release(config->server_config);
                    free(config->server_config);
                    free(config);
                }
            } while(hash_link);
        }
        qhash_finalize(s_fsid_to_config_table);
        s_fsid_to_config_table = NULL;

        gen_mutex_unlock(&s_server_config_mgr_mutex);
        gen_mutex_destroy(&s_server_config_mgr_mutex);
        s_min_handle_recycle_timeout_in_sec = -1;

        ret = 0;
    }
    return ret;
}

int PINT_server_config_mgr_reload_cached_config_interface(void)
{
    int ret = -PVFS_EINVAL, i = 0;
    server_config_t *config = NULL;
    struct qlist_head *hash_link = NULL;
    PINT_llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs = NULL;

    if (SC_MGR_INITIALIZED())
    {
        gen_mutex_lock(&s_server_config_mgr_mutex);
        SC_MGR_ASSERT_OK(ret);

        PINT_cached_config_finalize();
        ret = PINT_cached_config_initialize();
        if (ret)
        {
            PVFS_perror("PINT_cached_config_initialize failed", ret);
            gen_mutex_unlock(&s_server_config_mgr_mutex);
            return ret;
        }

        /*
          reset the min_handle_recycle_timeout_in_sec since it's going
          to be re-determined at this point
        */
        s_min_handle_recycle_timeout_in_sec = -1;

        for (i = 0; i < s_fsid_to_config_table->table_size; i++)
        {
            qhash_for_each(hash_link, &s_fsid_to_config_table->array[i])
            {
                config = qlist_entry(
                    hash_link, server_config_t, hash_link);
                assert(config);
                assert(config->server_config);

                assert(PINT_llist_count(
                           config->server_config->file_systems) == 1);

                cur = config->server_config->file_systems;
                assert(cur);

                cur_fs = PINT_llist_head(cur);
                assert(cur_fs);
                assert(cur_fs->handle_recycle_timeout_sec.tv_sec > -1);

                /* find the minimum handle recycle timeout here */
                if ((cur_fs->handle_recycle_timeout_sec.tv_sec <
                     s_min_handle_recycle_timeout_in_sec) ||
                    (s_min_handle_recycle_timeout_in_sec == -1))
                {
                    s_min_handle_recycle_timeout_in_sec =
                        cur_fs->handle_recycle_timeout_sec.tv_sec;

                    gossip_debug(GOSSIP_CLIENT_DEBUG, "Set min handle "
                                 "recycle time to %d seconds\n",
                                 s_min_handle_recycle_timeout_in_sec);
                }

                gossip_debug(GOSSIP_CLIENT_DEBUG,
                             "Reloading handle mappings for fs_id %d\n",
                             cur_fs->coll_id);

                ret = PINT_cached_config_handle_load_mapping(cur_fs);
                if (ret)
                {
                    PVFS_perror(
                        "PINT_cached_config_handle_load_mapping failed", ret);
                    gen_mutex_unlock(&s_server_config_mgr_mutex);
                    return ret;
                }
            }
        }
        gen_mutex_unlock(&s_server_config_mgr_mutex);
        ret = 0;
    }
    return ret;
}

int PINT_server_config_mgr_add_config(
    struct server_configuration_s *config_s,
    PVFS_fs_id fs_id,
    int* free_config_flag)
{
    int ret = -PVFS_EINVAL;
    server_config_t *config = NULL;
    struct qlist_head *hash_link = NULL;

    *free_config_flag = 0;

    gossip_debug(GOSSIP_CLIENT_DEBUG, "PINT_server_config_mgr_add_"
                 "config: adding config %p\n", config_s);

    if (SC_MGR_INITIALIZED() && config_s)
    {
        gen_mutex_lock(&s_server_config_mgr_mutex);
        SC_MGR_ASSERT_OK(ret);

        hash_link = qhash_search(s_fsid_to_config_table, &fs_id);
        if (hash_link)
        {
            gossip_debug(GOSSIP_CLIENT_DEBUG, "PINT_server_config_mgr_add_"
                         "config: increasing reference count.\n");
            /* config is already stored here, increase reference count */
            config = qlist_entry(hash_link, server_config_t, hash_link);
            assert(config);
            assert(config->server_config);
            config->ref_count++;
            /* set a flag to inform caller that we aren't using the config
             * structure
             */
            *free_config_flag = 1;
            gen_mutex_unlock(&s_server_config_mgr_mutex);
            return(0);
        }

        config = (server_config_t *)malloc(sizeof(server_config_t));
        if (!config)
        {
            ret = -PVFS_ENOMEM;
            goto add_failure;
        }
        memset(config, 0, sizeof(server_config_t));

        config->server_config = config_s;
        config->fs_id = fs_id;
        config->ref_count = 1;

        qhash_add(s_fsid_to_config_table, &fs_id,
                  &config->hash_link);

        gossip_debug(GOSSIP_CLIENT_DEBUG, "\tmapped fs_id %d => "
                     "config %p\n", fs_id, config_s);

        gen_mutex_unlock(&s_server_config_mgr_mutex);

        ret = 0;
    }
    return ret;

  add_failure:
    gossip_debug(GOSSIP_CLIENT_DEBUG, "PINT_server_config_mgr_add_"
                 "config: add_failure reached\n");

    if (config)
    {
        qhash_search_and_remove(s_fsid_to_config_table, &fs_id);
        free(config);
    }
    gen_mutex_unlock(&s_server_config_mgr_mutex);
    return ret;
}

int PINT_server_config_mgr_remove_config(
    PVFS_fs_id fs_id)
{
    int ret = -PVFS_EINVAL;
    server_config_t *config = NULL;
    struct qlist_head *hash_link = NULL;

    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: Trying to "
                 "remove config obj for fs_id %d\n", __func__, fs_id);

    if (SC_MGR_INITIALIZED())
    {
        gen_mutex_lock(&s_server_config_mgr_mutex);
        SC_MGR_ASSERT_OK(ret);

        hash_link = qhash_search(
            s_fsid_to_config_table, &fs_id);
        if (hash_link)
        {
            config = qlist_entry(hash_link, server_config_t, hash_link);
            assert(config);
            assert(config->server_config);
            assert(config->fs_id == fs_id);

            config->ref_count--;

            if(config->ref_count == 0)
            {
                gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: "
                             "Removed config object %p with fs_id %d\n",
                             __func__, config, fs_id);
                qhash_del(&config->hash_link);

                /*
                 * config objects are allocated by fs-add.c:PVFS_sys_fs_add
                 * but we free them here
                 */
                PINT_config_release(config->server_config);
                free(config->server_config);

                free(config);
                config = NULL;
            }
            else
            {
                gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: "
                             "Config object %p with fs_id %d still in use.\n",
                             __func__, config, fs_id);
            }

            ret = 0;
        }
        gen_mutex_unlock(&s_server_config_mgr_mutex);
    }
    return ret;
}

struct server_configuration_s *__PINT_server_config_mgr_get_config(
    PVFS_fs_id fs_id)
{
    struct server_configuration_s *ret = NULL;
    server_config_t *config = NULL;
    struct qlist_head *hash_link = NULL;

    if (SC_MGR_INITIALIZED())
    {
        gen_mutex_lock(&s_server_config_mgr_mutex);
        SC_MGR_ASSERT_OK(ret);

        hash_link = qhash_search(s_fsid_to_config_table, &fs_id);
        if (hash_link)
        {
            config = qlist_entry(hash_link, server_config_t, hash_link);
            assert(config);
            assert(config->server_config);
#if 0
            gossip_debug(
                GOSSIP_CLIENT_DEBUG, "server_config_mgr: LOCKING config "
                "object %p with fs_id %d\n", config, fs_id);
#endif
            ret = config->server_config;
        }
        /* if we find a match, then old onto the mutex and let the caller 
         * release it in a put_config call
         */
        if(!ret)
        {
            gen_mutex_unlock(&s_server_config_mgr_mutex);
        }
    }
    return ret;
}

void __PINT_server_config_mgr_put_config(
    struct server_configuration_s *config_s)
{
    PINT_llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs = NULL;
    server_config_t *config = NULL;
    struct qlist_head *hash_link = NULL;

    if (SC_MGR_INITIALIZED() && config_s)
    {
        SC_MGR_ASSERT_OK( );

        cur = config_s->file_systems;
        assert(PINT_llist_count(config_s->file_systems) == 1);

        cur_fs = PINT_llist_head(cur);
        assert(cur_fs);

        hash_link = qhash_search(
            s_fsid_to_config_table, &cur_fs->coll_id);
        if (hash_link)
        {
            config = qlist_entry(hash_link, server_config_t, hash_link);
            assert(config);
            assert(config->server_config);
#if 0
            gossip_debug(
                GOSSIP_CLIENT_DEBUG, "server_config_mgr: "
                "UNLOCKING config object %p\n", config);
#endif
        }
        gen_mutex_unlock(&s_server_config_mgr_mutex);
    }
}

int PINT_server_config_mgr_get_abs_min_handle_recycle_time(void)
{
    return s_min_handle_recycle_timeout_in_sec;
}

static int hash_fsid(void *key, int table_size)
{
    PVFS_fs_id fs_id = *((PVFS_fs_id *)key);
    return (int)(fs_id % table_size);
}

static int hash_fsid_compare(void *key, struct qlist_head *link)
{
    server_config_t *config = NULL;
    PVFS_fs_id fs_id = *((PVFS_fs_id *)key);

    config = qlist_entry(link, server_config_t, hash_link);
    assert(config);

    return ((config->fs_id == fs_id) ? 1 : 0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
