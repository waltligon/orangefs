/*
 * (C) 2009 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */       

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "pvfs2-internal.h"
#include "trove.h"
#include "gossip.h"
#include "trove-dbpf/dbpf.h"
#include "pint-cached-config.h"
#include "server-config-mgr.h"

#undef DEBUG_MIGRATE_PERF

/*
 * Macros
 */
#define TROVE_DSPACE_WAIT(ret, coll_id, op_id,                  \
                          context_id, op_count, state, label)   \
    while (ret == 0)                                            \
    {                                                           \
        ret = trove_dspace_test(coll_id,                        \
                                op_id,                          \
                                context_id,                     \
                                &op_count,                      \
                                NULL,                           \
                                NULL,                           \
                                &state,                         \
                                TROVE_DEFAULT_TEST_TIMEOUT);    \
    }                                                           \
    if (ret < 0)                                                \
    {                                                           \
        gossip_err("trove_dspace_test failed: err=%d coll=%d    \
op=%lld context=%lld count=%d state=%d\n",                      \
                    ret, coll_id, llu(op_id),                   \
                    llu(context_id), op_count, state);          \
        goto label;                                             \
    }

/*
 * Prototypes
 */
static int migrate_collection_0_1_3 (TROVE_coll_id coll_id,
				     const char* data_path,
				     const char* meta_path);
static int migrate_collection_0_1_4 (TROVE_coll_id coll_id,
				     const char* data_path,
				     const char* meta_path);

/*
 * Migration Table
 *
 *  Migration routines should be listed in ascending order.
 */
struct migration_s
{
    int major;
    int minor;
    int incremental;
    int (*migrate)(TROVE_coll_id coll_id,
		 const char* data_path,
		 const char* meta_path);
};

/* format: major, minor, incremental, function to migrate.
 * NOTE: this defines the version to migratem *FROM*. In other words,
 * if currently running the version defined in the table, run the
 * associated function. */
struct migration_s migration_table[] =
{
    { 0, 1, 3, migrate_collection_0_1_3 },
    { 0, 1, 4, migrate_collection_0_1_4 },
    { 0, 0, 0, NULL }
};

/*
 * trove_get_version
 *   coll_id     - collection id
 *   major       - return major version
 *   minor       - return minor version
 *   incremental - return incremental version
 *
 * Return the major, minor and incremental digits of the dbpf storage version.
 * \return 0 on success, non-zero otherwise
 */
int trove_get_version (TROVE_coll_id coll_id,
                       int* major,
                       int* minor,
                       int* incremental)
{
    TROVE_context_id context_id = PVFS_CONTEXT_NULL;
    TROVE_op_id      op_id;
    TROVE_ds_state   state;
    TROVE_keyval_s   key;
    TROVE_keyval_s   data;
    char             version[32] = {0};
    int              ret;
    int              count;

    memset (&key,  0, sizeof(key));
    memset (&data, 0, sizeof(data));

    key.buffer     = TROVE_DBPF_VERSION_KEY;
    key.buffer_sz  = strlen(TROVE_DBPF_VERSION_KEY);
    data.buffer    = version;
    data.buffer_sz = sizeof(version);

    ret = trove_open_context(coll_id, &context_id);
    if (ret < 0)
    {
        gossip_err("trove_open_context failed: ret=%d coll=%d\n",
                   ret, coll_id);
        goto complete;
    }

    ret = trove_collection_geteattr(coll_id, &key, &data, 0, NULL,
                                    context_id, &op_id);
    if (ret < 0)
    {
        gossip_err("trove_collection_geteattr failed: ret=%d coll=%d \
context=%lld op=%lld\n",
                   ret, coll_id, llu(context_id), llu(op_id));
        goto complete;
    }

    TROVE_DSPACE_WAIT(ret, coll_id, op_id, context_id, count, state, complete);

    ret = sscanf(version, "%d.%d.%d", major, minor, incremental);
    if (ret != 3)
    {
        gossip_err("sscanf failed: ret=%d errno=%d version=%s\n",
                   ret, errno, version);
        ret = -1;
        goto complete;
    }

    ret = 0;

complete:
    if (context_id != PVFS_CONTEXT_NULL)
    {
        int rc = trove_close_context(coll_id, context_id);
        if (rc < 0)
        {
            ret = rc;
            gossip_err("trove_context_close failed: ret=%d coll=%d \
context=%lld\n",
                       ret, coll_id, llu(context_id));
        }
    }

    return ret;
}

/*
 * trove_put_version
 *   coll_id     - collection id
 *   major       - major version
 *   minor       - minor version
 *   incremental - incremental version
 *
 * Set the major, minor and incremental digits of the dbpf storage version.
 * \return 0 on success, non-zero otherwise
 */
int trove_put_version (TROVE_coll_id coll_id,
                       int major, int minor, int incremental)
{
    TROVE_context_id context_id = PVFS_CONTEXT_NULL;
    TROVE_op_id      op_id;
    TROVE_ds_state   state;
    TROVE_keyval_s   key;
    TROVE_keyval_s   data;
    char             version[32] = {0};
    int              ret;
    int              count;

    memset (&key,  0, sizeof(key));
    memset (&data, 0, sizeof(data));

    key.buffer     = TROVE_DBPF_VERSION_KEY;
    key.buffer_sz  = strlen(TROVE_DBPF_VERSION_KEY);
    data.buffer    = version;
    data.buffer_sz = sizeof(version);

    ret = trove_open_context(coll_id, &context_id);
    if (ret < 0)
    {
        gossip_err("trove_open_context failed: ret=%d coll=%d\n",
                   ret, coll_id);
        goto complete;
    }

    ret = trove_collection_geteattr(coll_id, &key, &data, 0, NULL,
                                    context_id, &op_id);
    if (ret < 0)
    {
        gossip_err("trove_collection_geteattr failed: ret=%d coll=%d \
context=%lld op=%lld\n",
                   ret, coll_id, llu(context_id), llu(op_id));
        goto complete;
    }

    TROVE_DSPACE_WAIT(ret, coll_id, op_id, context_id, count, state, complete);

    ret = snprintf (version, sizeof(version), "%d.%d.%d",
                    major, minor, incremental);
    if ((ret < 0) || (ret >= 32))
    {
        gossip_err("snprintf failed: ret=%d errno=%d version=%s\n",
                   ret, errno, version);
        ret = -1;
        goto complete;
    }

    /* set the size to a correct value, not 32 */
    data.buffer_sz = strlen(data.buffer);
    ret = trove_collection_seteattr(coll_id, &key, &data, 0, NULL,
                                    context_id, &op_id);
    if (ret < 0)
    {
        gossip_err("trove_collection_seteattr failed: ret=%d coll=%d \
context=%lld op=%lld\n",
                   ret, coll_id, llu(context_id), llu(op_id));
        goto complete;
    }

    TROVE_DSPACE_WAIT(ret, coll_id, op_id, context_id, count, state, complete);

complete:
    if (context_id != PVFS_CONTEXT_NULL)
    {
        int rc = trove_close_context(coll_id, context_id);
        if (rc < 0)
        {
            ret = rc;
            gossip_err("trove_context_close failed: ret=%d coll=%d \
context=%lld\n",
                       ret, coll_id, llu(context_id));
        }
    }

    return ret;
}

#ifdef DEBUG_MIGRATE_PERF
static double wtime(void)
{
    struct timeval t;

    gettimeofday(&t, NULL);
    return((double)t.tv_sec + (double)t.tv_usec / 1000000);
}
#endif

/*
 * trove_migrate
 *   method_id - method used to for trove access
 *   data_path - path to data storage
 *   meta_path - path to metadata storage
 *
 * Iterate over all collections and migrate each one.
 * \return 0 on success, non-zero on failure
 */
int trove_migrate (TROVE_method_id method_id, const char* data_path,
		   const char* meta_path)
{
    TROVE_ds_position pos;
    TROVE_coll_id     coll_id;
    TROVE_op_id       op_id;
    TROVE_keyval_s    name;
    struct migration_s *migrate_p;
    int               count;
    int               ret = 0;
    int               major;
    int               minor;
    int               incremental;
    int               i;
    int               migrated;
#ifdef DEBUG_MIGRATE_PERF
    double            s,e;
    s = wtime();
#endif

    count          = 10;
    pos            = TROVE_ITERATE_START;
    name.buffer    = malloc(PATH_MAX);
    name.buffer_sz = PATH_MAX;

    if (!name.buffer)
    {
        ret = errno;
        gossip_err("malloc failed: errno=%d\n", errno);
        goto complete;
    }

    while (count > 0)
    {
        ret = trove_collection_iterate(method_id,
                                       &pos,
                                       &name,
                                       &coll_id,
                                       &count,
                                       0,
                                       NULL,
                                       NULL,
                                       &op_id);
        if (ret < 0)
        {
            gossip_err("trove_collection_iterate failed: \
ret=%d method=%d pos=%lld name=%p coll=%d count=%d op=%lld\n",
                       ret, method_id, llu(pos), &name,
                       coll_id, count, llu(op_id));
            goto complete;
        }

        for (i=0; i<count; i++)
        {
            ret = trove_get_version(coll_id, &major, &minor, &incremental);
            if (ret < 0)
            {
                gossip_err("trove_get_version failed: ret=%d coll=%d\n",
                           ret, coll_id);
                goto complete;
            }

            migrated = 0;
            for (migrate_p = &(migration_table[0]);
                 migrate_p->migrate != NULL;
                 migrate_p++)
            {
                if ((major <= migrate_p->major) &&
                    (minor <= migrate_p->minor) &&
                    (incremental <= migrate_p->incremental))
                {
                    gossip_err("Trove Migration Started: Ver=%d.%d.%d\n",
                               migrate_p->major,
                               migrate_p->minor,
                               migrate_p->incremental);
                    ret = migrate_p->migrate(coll_id, data_path, meta_path);
                    if (ret < 0)
                    {
                        gossip_err("migrate failed: \
ret=%d coll=%d metadir=%s datadir=%s major=%d minor=%d incremental=%d\n",
                                   ret, coll_id, meta_path, data_path,
				   migrate_p->major, migrate_p->minor, 
				   migrate_p->incremental);
                        goto complete;
                    }
                    gossip_err("Trove Migration Complete: Ver=%d.%d.%d\n",
                               migrate_p->major,
                               migrate_p->minor,
                               migrate_p->incremental);
                    migrated = 1;
                }
            }

            if (migrated)
            {
                ret = sscanf(TROVE_DBPF_VERSION_VALUE, "%d.%d.%d",
                             &major, &minor, &incremental);
                if (ret !=3)
                {
                    gossip_err("sscanf failed: ret=%d\n", ret);
                    goto complete;
                }

                ret = trove_put_version (coll_id, major, minor, incremental);
                if (ret < 0)
                {
                    gossip_err("trove_put_version failed: ret=%d coll=%d \
ver=%d.%d.%d\n",
                               ret, coll_id, major, minor, incremental);
                    goto complete;
                }

                gossip_err("Trove Version Set: %d.%d.%d\n",
                           major, minor, incremental);
            }
        }
    }

complete:
    if (name.buffer)
    {
        free(name.buffer);
    }
#ifdef DEBUG_MIGRATE_PERF
    e = wtime();
    gossip_err("migrate time: %lf seconds\n", (e-s));
#endif
    return ret;
}

/*
 * migrate_collection_0_1_3
 *   coll_id   - collection id
 *   data_path - path to data storage
 *   meta_path - path to metadata storage
 *
 * For each datafile handle, check the file length and update the
 * b_size attribute.
 * \return 0 on success, non-zero on failure
 */
static int migrate_collection_0_1_3 (TROVE_coll_id coll_id, 
				     const char* data_path,
				     const char* meta_path)
{
    TROVE_context_id  context_id = PVFS_CONTEXT_NULL;
    TROVE_ds_position pos;
    TROVE_ds_state    state;
    TROVE_op_id       iterate_op_id;
    TROVE_op_id       setattr_op_id;
    TROVE_op_id       getattr_op_id;
    TROVE_handle*     handles;
    TROVE_ds_attributes_s *attrs;
    TROVE_ds_state    *states;
    TROVE_ds_state    *completed_states;
    TROVE_op_id       *completed_ids;
    void              **user;
    int               base_count;
    int               handle_count;
    int               completed_count;
    int               op_count;
    int               ret;
    int               i, j, k;
    int               outstanding_op_count;
    int               immediate_completion;

    base_count = 10000;
    
    handles = malloc(sizeof(TROVE_handle)*base_count);
    if (!handles)
    {
        gossip_err("malloc failed: errno=%d size=%d\n",
                   errno, (int)(sizeof(TROVE_handle)*base_count));
        return -1;
    }

    attrs = malloc(sizeof(TROVE_ds_attributes_s)*base_count);
    if (!attrs)
    {
        gossip_err("malloc failed: errno=%d size=%d\n",
                   errno, (int)(sizeof(TROVE_ds_attributes)*base_count));
        return -1;
    }

    states = malloc(sizeof(TROVE_ds_state)*base_count);
    if (!states)
    {
        gossip_err("malloc failed: errno=%d size=%d\n",
                   errno, (int)(sizeof(TROVE_ds_state)*base_count));
        return -1;
    }

    completed_states = malloc(sizeof(TROVE_ds_state)*base_count);
    if (!completed_states)
    {
        gossip_err("malloc failed: errno=%d size=%d\n",
                   errno, (int)(sizeof(TROVE_ds_state)*base_count));
        return -1;
    }

    completed_ids = malloc(sizeof(TROVE_op_id)*base_count);
    if (!completed_ids)
    {
        gossip_err("malloc failed: errno=%d size=%d\n",
                   errno, (int)(sizeof(TROVE_op_id)*base_count));
        return -1;
    }

    user = (void**) malloc(sizeof(void*)*base_count);
    if (!user)
    {
        gossip_err("malloc failed: errno=%d size=%d\n",
                   errno, (int)(sizeof(void*)*base_count));
        return -1;
    }
    for (i = 0; i < base_count; i++)
    {
        user[i] = NULL;
    }

    ret = trove_open_context(coll_id, &context_id);
    if (ret < 0)
    {
        gossip_err("trove_open_context failed: ret=%d coll=%d\n",
                   ret, coll_id);
        goto complete;
    }

    immediate_completion = 1;
    ret = trove_collection_setinfo(coll_id, context_id,
                                   TROVE_COLLECTION_IMMEDIATE_COMPLETION,
                                    &immediate_completion);
    if (ret < 0)
    {
        gossip_err("trove_collection_setinfo failed: ret=%d coll=%d context=%lld\n",
                   ret, coll_id, lld(context_id));
        goto complete;
    }

    pos = TROVE_ITERATE_START;

    do
    {
        outstanding_op_count = 0;
        handle_count         = base_count;

        ret = trove_dspace_iterate_handles(coll_id,
                                           &pos,
                                           handles,
                                           &handle_count,
                                           0,
                                           NULL,
                                           NULL,
                                           context_id,
                                           &iterate_op_id);
        if (ret < 0)
        {
            gossip_err("trove_dspace_iterate_handles failed: \
ret=%d coll=%d pos=%lld handles=%p count=%d context=%lld op=%lld\n",
                       ret, coll_id, llu(pos), handles, handle_count,
                       llu(context_id), llu(iterate_op_id));
            goto complete;
        }
        TROVE_DSPACE_WAIT(ret, coll_id, iterate_op_id, context_id, \
                          op_count, state, complete);

        ret = trove_dspace_getattr_list(coll_id,
                                        handle_count,
                                        handles,
                                        attrs,
                                        states,
                                        0,
                                        NULL,
                                        context_id,
                                        &getattr_op_id,
                                        PVFS_HINT_NULL); 
        if (ret < 0)
        {
            gossip_err("trove_dspace_getattr_list failed: \
ret=%d coll=%d handles=%p attrs=%p states=%p count=%d context=%lld op=%lld\n",
                       ret, coll_id, handles, attrs, states, handle_count,
                       llu(context_id), llu(getattr_op_id));
            goto complete;
        }

        TROVE_DSPACE_WAIT(ret, coll_id, getattr_op_id, context_id, \
                          op_count, state, complete);
        for (i = 0; i < handle_count; i++)
        {
            if (states[i] != 0)
            {
                ret = -1;
                gossip_err("trove_dspace_getattr_list failure: \
coll=%d context=%lld handle=%llu state=%d\n",
                           coll_id, lld(context_id),
                           llu(handles[i]), states[i]);
                goto complete;
            }

            if (attrs[i].type == PVFS_TYPE_DATAFILE)
            {
                struct stat  stat_data;
                char         filename[PATH_MAX];
                TROVE_size   b_size;

                DBPF_GET_BSTREAM_FILENAME(filename,
                                          PATH_MAX,
                                          data_path,
                                          coll_id,
                                          llu(handles[i]));
                ret = stat(filename, &stat_data);
                if ((ret != 0) && (errno == ENOENT))
                {
                    /* The bstream does not exist, assume this is due
                     *  to lazy creation.
                     */
                    b_size = 0;
                }
                else if (ret != 0)
                {
                    gossip_err("stat failed: ret=%d errno=%d fname=%s\n",
                               ret, errno, filename);
                    goto complete;
                }
                else
                {
                    b_size = (TROVE_size) stat_data.st_size;
                }

                /*
                 * Set bstream size
                 */
                attrs[i].u.datafile.b_size = b_size;

                ret = trove_dspace_setattr(coll_id,
                                           handles[i],
                                           &(attrs[i]),
                                           0,
                                           NULL,
                                           context_id,
                                           &setattr_op_id,
                                           PVFS_HINT_NULL);
                if (ret < 0)
                {
                    gossip_err("trove_dspace_setattr failed: \
ret=%d handle=%lld context=%lld op=%lld\n",
                               ret, llu(handles[i]),
                               lld(context_id), lld(setattr_op_id));
                    goto complete;
                }

                if (ret == 0)
                {
                    outstanding_op_count++;
                }
            }
        }

        for (j = outstanding_op_count; j > 0; )
        {
            completed_count = base_count;

            ret = trove_dspace_testcontext(coll_id,
                                           completed_ids,
                                           &completed_count,
                                           completed_states,
                                           user,
                                           10,
                                           context_id);
            if (ret < 0)
            {
                gossip_err("trove_dspace_testcontext failed: ret=%d \
coll=%d ids=%p count=%d states=%p context=%lld\n",
                           ret, coll_id, completed_ids,
                           completed_count, completed_states,
                           lld(context_id));
                goto complete;
            }

            j -= completed_count;

            for (k = 0; k < completed_count; k++)
            {
                if (completed_states[k] != 0)
                {
                    gossip_err("trove_dspace_testcontext failure: \
coll=%d id=%lld state=%d\n",
                               coll_id, lld(completed_ids[k]),
                               completed_states[k]);
                    goto complete;
                }
            }
        }
    } while (handle_count > 0);

complete:

    if (context_id != PVFS_CONTEXT_NULL)
    {
        int rc = trove_close_context(coll_id, context_id);
        if (rc < 0)
        {
            ret = rc;
            gossip_err("trove_close_context failed: ret=%d coll=%d \
context=%lld\n",
                       ret, coll_id, llu(context_id));
        }
    }

    if (handles)
    {
        free(handles);
    }

    if (attrs)
    {
        free(attrs);
    }

    if (states)
    {
        free(states);
    }

    if (completed_states)
    {
        free(completed_states);
    }

    if (completed_ids)
    {
        free(completed_ids);
    }

    if (user)
    {
        free(user);
    }

    return ret;
}


/*
 * migrate_collection_0_1_4
 *   coll_id   - collection id
 *   data_path - path to data storage
 *   meta_path - path to metadata storage
 *
 * Migrate existing precreate pool keys held in the collection attributes
 * to include the handle type (PVFS_TYPE_DATAFILE) in the key. Since prior
 * to this version only PVFS_TYPE_DATAFILE handles existed in a pool this
 * is an easy conversion to make 
 *
 * \return 0 on success, non-zero on failure
 */
static int migrate_collection_0_1_4 (TROVE_coll_id coll_id, 
				     const char* data_path,
				     const char* meta_path)
{
    int ret=0, i=0, server_count=0, server_type=0, count=0, pool_key_len=0;
    const char *host;
    /* hostname + pool key string + handle type size */
    char pool_key[PVFS_MAX_SERVER_ADDR_LEN + 28] = { 0 };
    char type_string[11] = { 0 };
    TROVE_context_id  context_id = PVFS_CONTEXT_NULL;
    TROVE_keyval_s key, data;
    TROVE_op_id delattr_op_id, getattr_op_id, setattr_op_id;
    TROVE_ds_state state;
    PVFS_BMI_addr_t* addr_array = NULL;
    PVFS_handle handle = PVFS_HANDLE_NULL;

    struct server_configuration_s *user_opts = get_server_config_struct();
    assert(user_opts);

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s: %d, %s, %s\n", 
                 __func__, coll_id, data_path, meta_path);

    ret = trove_open_context(coll_id, &context_id);
    if (ret < 0)
    {
        gossip_err("%s: trove_open_context failed: ret=%d coll=%d\n", __func__, 
                    ret, coll_id);
        return ret;
    }

    /* for completeness we will check even if this server claims it's not a 
     * metadata server to make sure we get all precreate pool handles updated.
     * If it doesn't have any defined then our geteattr calls will just return
     * with no record, Also check all peer servers for a precreate pool for
     * the same reason (and it's easier anyway). */
    ret = PINT_cached_config_count_servers( coll_id, PINT_SERVER_TYPE_ALL, 
                                            &server_count);
    if(ret < 0)
    {
        gossip_err("%s: error: unable to count servers for fsid: %d\n",
                   __func__, (int)coll_id);
        return ret;
    }

    addr_array = calloc(server_count, sizeof(PVFS_BMI_addr_t));
    if(!addr_array)
    {
        gossip_err("%s: error: unable to allocate addr array for precreate "
                   "pools.\n", __func__);
        ret = -PVFS_ENOMEM;
        goto complete;
    }

    /* resolve addrs for each I/O server */
    ret = PINT_cached_config_get_server_array(coll_id, PINT_SERVER_TYPE_ALL, 
                                              addr_array, &server_count);
    if(ret < 0)
    {
        gossip_err("%s: error: unable retrieve servers addrs\n", __func__);
        goto complete;
    }
       
    /* check each server for a precreate pool and check for only one pool since
     * that's all there was prior to this version */
    for(i=0; i<server_count; i++)
    {
        host = PINT_cached_config_map_addr(coll_id, addr_array[i], 
                                           &server_type);
        /* potential host with precreate pool entry */
        memset(&key,  0, sizeof(key));
        memset(&data, 0, sizeof(data));
        memset(pool_key, 0, PVFS_MAX_SERVER_ADDR_LEN + 28);

        pool_key_len = strlen(host) + strlen("precreate-pool-") + 1;
        key.buffer = pool_key;
        key.buffer_sz = pool_key_len;
        key.read_sz = 0;

        snprintf((char*)key.buffer, key.buffer_sz, "precreate-pool-%s", host);
        data.buffer    = &handle;
        data.buffer_sz = sizeof(handle);
        data.read_sz = 0;

        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: looking for pool key\n", 
                     __func__);
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: key(%s)(%d)(%d)\n", 
                     __func__, (char *)key.buffer, key.buffer_sz, key.read_sz);
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: data(%s)(%d)(%d)\n", 
                   __func__, (char *)data.buffer, data.buffer_sz, data.read_sz);
        ret = trove_collection_geteattr(coll_id, &key, &data, 0, NULL, 
                                        context_id, &getattr_op_id);
        if (ret < 0)
        {
            gossip_err("%s: trove_collection_getattr failed for pool key %s "
                       "ret=%d coll=%d context=%lld op=%lld\n", __func__,
                       pool_key, ret, coll_id, llu(context_id), 
                       llu(getattr_op_id));
            continue;
        } 
        TROVE_DSPACE_WAIT(ret, coll_id, getattr_op_id, context_id, count, state,
                          complete);
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: found pool key\n", __func__);
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: key(%s)(%d)(%d)\n", 
                     __func__, (char *)key.buffer, key.buffer_sz, key.read_sz);
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: data(%llu)(%d)(%d)\n", 
                     __func__, llu(*(PVFS_handle *)data.buffer), data.buffer_sz,
                     data.read_sz);

        ret = trove_collection_deleattr(coll_id, &key, 0, NULL, context_id, 
                                        &delattr_op_id);
        if (ret < 0)
        {
            gossip_err("%s: trove_collection_delattr failed: \
                       ret=%d coll=%d context=%lld op=%lld\n", __func__,
                       ret, coll_id, llu(context_id), llu(delattr_op_id));
            goto complete;
        } 
        TROVE_DSPACE_WAIT(ret, coll_id, delattr_op_id, context_id, count, state,
                          complete);
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: removed old pool key\n", 
                     __func__);

        /* munge to new key */
        snprintf(type_string, 11, "%u", PVFS_TYPE_DATAFILE);
        memset(pool_key, 0, PVFS_MAX_SERVER_ADDR_LEN + 28);
        snprintf(pool_key, PVFS_MAX_SERVER_ADDR_LEN + 28,
                 "precreate-pool-%s-%s", host, type_string);

        /* reset the length to include room for the type */
        pool_key_len = strlen(host) + strlen(type_string) +
                       strlen("precreate-pool-") + 2;
        key.buffer_sz  = pool_key_len;
 
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: adding new pool key (%s) -> "
                     "(%llu)\n", __func__, (char *)key.buffer, 
                     llu(*(PVFS_handle *)data.buffer));
        ret = trove_collection_seteattr(coll_id, &key, &data, 0, NULL, 
                                        context_id, &setattr_op_id);
        if (ret < 0)
        {
            gossip_err("%s: trove_collection_setattr failed: \
                       ret=%d coll=%d context=%lld op=%lld\n", __func__,
                       ret, coll_id, llu(context_id), llu(setattr_op_id));
            goto complete;
        } 
        TROVE_DSPACE_WAIT(ret, coll_id, setattr_op_id, context_id, count, state,
                          complete);
        gossip_debug(GOSSIP_TROVE_DEBUG, "%s: successfully migrated pool %s\n",
                     __func__, (char *)key.buffer);
    } // for each server

    /* if we just came out of the loop force ret to 0, we don't want a bad
     * key lookup to spoil the whole migration (since it's expected) */
    ret = 0; 

complete:
    if (context_id != PVFS_CONTEXT_NULL)
    {
        int rc = trove_close_context(coll_id, context_id);
        if (rc < 0)
        {
            ret = rc;
            gossip_err("%s: trove_close_context failed: ret=%d coll=%d " \
                       "context=%lld\n", __func__, ret, coll_id, 
                       llu(context_id));
        }
    }

    if( addr_array )
    {
        free(addr_array);
    }

    return ret;
}
