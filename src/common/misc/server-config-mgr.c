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

    gen_mutex_t *server_config_mutex;
    struct server_configuration_s *server_config;
} server_config_t;

static struct qhash_table *s_fsid_to_config_table = NULL;
static gen_mutex_t *s_server_config_mgr_mutex = NULL;

static int hash_fsid(void *key, int table_size);
static int hash_fsid_compare(void *key, struct qlist_head *link);

#define SC_MGR_INITIALIZED() \
(s_fsid_to_config_table && s_server_config_mgr_mutex)

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
            s_server_config_mgr_mutex = gen_mutex_build();
            if (s_server_config_mgr_mutex)
            {
                ret = 0;
            }
            else
            {
                qhash_finalize(s_fsid_to_config_table);
                ret = -PVFS_ENOMEM;
            }
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
        gen_mutex_lock(s_server_config_mgr_mutex);
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

                    config->server_config = NULL;
                    gen_mutex_destroy(config->server_config_mutex);
                    free(config);
                }
            } while(hash_link);
        }
        qhash_finalize(s_fsid_to_config_table);
        s_fsid_to_config_table = NULL;

        gen_mutex_unlock(s_server_config_mgr_mutex);
        gen_mutex_destroy(s_server_config_mgr_mutex);
        s_server_config_mgr_mutex = NULL;

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
        gen_mutex_lock(s_server_config_mgr_mutex);
        SC_MGR_ASSERT_OK(ret);

        PINT_cached_config_finalize();
        ret = PINT_cached_config_initialize();
        if (ret)
        {
            PVFS_perror("PINT_cached_config_initialize failed", ret);
            gen_mutex_unlock(s_server_config_mgr_mutex);
            return ret;
        }

        for (i = 0; i < s_fsid_to_config_table->table_size; i++)
        {
            hash_link = qhash_search_at_index(
                s_fsid_to_config_table, i);
            if (hash_link)
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

                gossip_debug(GOSSIP_CLIENT_DEBUG,
                             "Reloading handle mappings for fs_id %d\n",
                             cur_fs->coll_id);

                ret = PINT_handle_load_mapping(
                    config->server_config, cur_fs);
                if (ret)
                {
                    PVFS_perror("PINT_handle_load_mapping failed", ret);
                    gen_mutex_unlock(s_server_config_mgr_mutex);
                    return ret;
                }
            }
        }
        gen_mutex_unlock(s_server_config_mgr_mutex);
        ret = 0;
    }
    return ret;
}

int PINT_server_config_mgr_add_config(
    struct server_configuration_s *config_s,
    PVFS_fs_id fs_id)
{
    int ret = -PVFS_EINVAL;
    server_config_t *config = NULL;
    struct qlist_head *hash_link = NULL;

    gossip_debug(GOSSIP_CLIENT_DEBUG, "server_config_mgr: Trying to "
                 "add config obj %p\n", config_s);

    if (SC_MGR_INITIALIZED() && config_s)
    {
        gen_mutex_lock(s_server_config_mgr_mutex);
        SC_MGR_ASSERT_OK(ret);

        hash_link = qhash_search(s_fsid_to_config_table, &fs_id);
        if (hash_link)
        {
            ret = -PVFS_EEXIST;
            goto add_failure;
        }

        config = (server_config_t *)malloc(sizeof(server_config_t));
        if (!config)
        {
            ret = -PVFS_ENOMEM;
            goto add_failure;
        }
        memset(config, 0, sizeof(server_config_t));

        config->server_config_mutex = gen_mutex_build();
        if (!config->server_config_mutex)
        {
            ret = -PVFS_ENOMEM;
            goto add_failure;
        }

        config->server_config = config_s;
        config->fs_id = fs_id;

        qhash_add(s_fsid_to_config_table, &fs_id,
                  &config->hash_link);

        gossip_debug(
            GOSSIP_CLIENT_DEBUG, "server_config_mgr: mapped "
            "fs_id %d to config object %p\n", fs_id, config_s);

        gen_mutex_unlock(s_server_config_mgr_mutex);

        ret = 0;
    }
    return ret;

  add_failure:
    gossip_debug(GOSSIP_CLIENT_DEBUG,
                 "server_config_mgr: add_failure reached\n");

    if (config)
    {
        qhash_search_and_remove(s_fsid_to_config_table, &fs_id);
        if (config->server_config_mutex)
        {
            gen_mutex_destroy(config->server_config_mutex);
            config->server_config_mutex = NULL;
        }
        free(config);
    }
    gen_mutex_unlock(s_server_config_mgr_mutex);
    return ret;
}

int PINT_server_config_mgr_remove_config(
    PVFS_fs_id fs_id)
{
    int ret = -PVFS_EINVAL;
    server_config_t *config = NULL;
    struct qlist_head *hash_link = NULL;

    gossip_debug(GOSSIP_CLIENT_DEBUG, "server_config_mgr: Trying to "
                 "remove config obj for fs_id %d\n", fs_id);

    if (SC_MGR_INITIALIZED())
    {
        gen_mutex_lock(s_server_config_mgr_mutex);
        SC_MGR_ASSERT_OK(ret);

        hash_link = qhash_search_and_remove(
            s_fsid_to_config_table, &fs_id);
        if (hash_link)
        {
            config = qlist_entry(hash_link, server_config_t, hash_link);
            assert(config);
            assert(config->server_config);
            assert(config->fs_id == fs_id);

            gossip_debug(GOSSIP_CLIENT_DEBUG, "server_config_mgr: "
                         "Removed config object %p with fs_id %d\n",
                         config, fs_id);

            /*
              config objects are allocated by fs-add.c:PVFS_sys_fs_add
              but we free them here
            */
            PINT_config_release(config->server_config);
            free(config->server_config);

            if (gen_mutex_trylock(config->server_config_mutex) == EBUSY)
            {
                gossip_err("FIXME: Destroying mutex that is in use!\n");
            }
            gen_mutex_unlock(config->server_config_mutex);
            gen_mutex_destroy(config->server_config_mutex);

            free(config);
            config = NULL;

            ret = 0;
        }
        gen_mutex_unlock(s_server_config_mgr_mutex);
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
        gen_mutex_lock(s_server_config_mgr_mutex);
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
            gen_mutex_lock(config->server_config_mutex);
            ret = config->server_config;
        }
        gen_mutex_unlock(s_server_config_mgr_mutex);
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
        gen_mutex_lock(s_server_config_mgr_mutex);
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
            gen_mutex_unlock(config->server_config_mutex);
        }
        gen_mutex_unlock(s_server_config_mgr_mutex);
    }
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
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
