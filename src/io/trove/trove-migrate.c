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
#include "dist-dir-utils.h"

#undef DEBUG_MIGRATE_PERF

/*
 * Macros
 */
/* from src/io/trove/trove-dbpf/dbpf-keyval.c */
#define DBPF_MAX_KEY_LENGTH PVFS_NAME_MAX

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
 * keyval struct and variables
 */

/** default size of buffers to use for reading old db keys */
int DEF_KEY_SIZE = 4096;
/** default size of buffers to use for reading old db values */
int DEF_DATA_SIZE = 8192;

/* dbpf_keyval_db_entry in 0.1.5, no type field */
struct dbpf_keyval_db_entry_0_1_5
{
    TROVE_handle handle;
    char key[DBPF_MAX_KEY_LENGTH];
};

/*
 * Prototypes
 */
static int migrate_collection_0_1_3 (TROVE_coll_id coll_id,
				     const char* data_path,
				     const char* meta_path);
static int migrate_collection_0_1_4 (TROVE_coll_id coll_id,
				     const char* data_path,
				     const char* meta_path);
static int migrate_collection_0_1_5 (TROVE_coll_id coll_id,
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
    { 0, 1, 5, migrate_collection_0_1_5 },
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
    TROVE_keyval_s    name = {0};
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

    count          = 1;
    pos            = TROVE_ITERATE_START;
    name.buffer    = malloc(PATH_MAX);
    name.buffer_sz = PATH_MAX;

    if (!name.buffer)
    {
        ret = errno;
        gossip_err("malloc failed: errno=%d\n", errno);
        goto complete;
    }
    memset(name.buffer,0,PATH_MAX);

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

/* copy from src/io/trove/trove-dbpf/dbpf-mgmt.c
 *
 * dbpf_db_open()
 *
 * Internal function for opening the databases that are used to store
 * basic information on a storage region.
 *
 * Returns NULL on error, passing a trove error type back in the
 * integer pointed to by error_p.
 */
static DB *dbpf_db_open(
    char *dbname, DB_ENV *envp, int *error_p,
    int (*compare_fn) (DB *db, const DBT *dbt1, const DBT *dbt2),
    uint32_t flags)
{
    int ret = -TROVE_EINVAL;
    DB *db_p = NULL;

    if ((ret = db_create(&db_p, envp, 0)) != 0)
    {
        *error_p = -dbpf_db_error_to_trove_error(ret);
        return NULL;
    }

    db_p->set_errpfx(db_p, "TROVE:DBPF:Berkeley DB");

    if(compare_fn)
    {
        db_p->set_bt_compare(db_p, compare_fn);
    }

    if (flags && (ret = db_p->set_flags(db_p, flags)) != 0)
    {
        db_p->err(db_p, ret, "%s: set_flags", dbname);
        *error_p = -dbpf_db_error_to_trove_error(ret);
        db_close(db_p);
        return NULL;
    }

    if ((ret = db_open(db_p, dbname, TROVE_DB_OPEN_FLAGS, 0)) != 0) 
    {
        *error_p = -dbpf_db_error_to_trove_error(ret);
        db_close(db_p);
        return NULL;
    }
    return db_p;
}


/*
 * write_distdir_keyvals_0_1_5
 *
 * internal function to write distdir keyvals for dir handle and dirdata handle
 *
 * given a DIRENT_ENTRY key of a directory handle, write dist_dir_struct
 * to both directory handle and dirdata handle, currently set the number 
 * of handles to just include the local dirdata handle. More dirdata handles
 * will be added during first split.
 *
 */
static int write_distdir_keyvals_0_1_5 (
	TROVE_coll_id coll_id, 
	TROVE_context_id context_id,
	DBT key, 
	DBT val,
	TROVE_ds_attributes_s *dir_ds_attr_p)
{
    PVFS_handle		    dir_handle, dirdata_handle;
    TROVE_ds_attributes_s   dirdata_ds_attr;
    struct dbpf_keyval_db_entry_0_1_5 *k;
    TROVE_ds_state	    state;
    TROVE_op_id		    op_id;
    int			    count, keyval_count;
    int			    ret;

    TROVE_keyval_s	    *key_a = NULL, *val_a = NULL;
    PVFS_dist_dir_attr	    meta_dist_dir_attr;
    PVFS_dist_dir_attr	    dirdata_dist_dir_attr;
    PVFS_dist_dir_bitmap    dist_dir_bitmap = NULL;

    k = key.data;
    dir_handle = k->handle;
    dirdata_handle = *(PVFS_handle *)val.data;

    /* copy and set dirdata_ds_attr */
    memcpy(&dirdata_ds_attr, dir_ds_attr_p, sizeof(TROVE_ds_attributes_s));
    dirdata_ds_attr.type = PVFS_TYPE_DIRDATA;

    ret = trove_dspace_setattr(
	    coll_id, dirdata_handle, &dirdata_ds_attr, TROVE_SYNC, NULL,
	    context_id, &op_id, NULL);
    if (ret < 0)
    {
	gossip_err("trove_dspace_setattr failed: \
		ret=%d coll=%d handle=%llu context=%lld op=%lld\n",
		ret, coll_id, llu(dirdata_handle), 
		llu(context_id), llu(op_id));
	goto complete;
    }

    TROVE_DSPACE_WAIT(ret, coll_id, op_id, context_id, \
	    count, state, complete);


    /* write distdir keyvals to both dir_handle and dirdata_handle */

    /* init meta_dis_dir_attr and dirdata_dist_dir_attr 
     * num_servers=1
     * both have server_no=0, where metadata is never used */
    ret = PINT_init_dist_dir_state(&meta_dist_dir_attr,
	    &dist_dir_bitmap, 1, 0, 1, 1);
    assert(ret == 0);
    PINT_dist_dir_attr_copyto(dirdata_dist_dir_attr, meta_dist_dir_attr);

    keyval_count = 3;
    key_a = malloc(sizeof(TROVE_keyval_s) * keyval_count);
    if(!key_a)
    {
	gossip_err("keyval space create failed.\n");
	ret = -1;
	goto complete;
    }
    memset(key_a, 0, sizeof(TROVE_keyval_s) * keyval_count);

    val_a = malloc(sizeof(TROVE_keyval_s) * keyval_count);
    if(!val_a)
    {
	gossip_err("keyval space create failed.\n");
	ret = -1;
	goto complete;
    }
    memset(val_a, 0, sizeof(TROVE_keyval_s) * keyval_count);

    /* set keyval for directory meta handle */
    key_a[0].buffer = DIST_DIR_ATTR_KEYSTR;
    key_a[0].buffer_sz = DIST_DIR_ATTR_KEYLEN;

    val_a[0].buffer = &meta_dist_dir_attr;
    val_a[0].buffer_sz =
	sizeof(meta_dist_dir_attr);

    key_a[1].buffer = DIST_DIRDATA_BITMAP_KEYSTR;
    key_a[1].buffer_sz = DIST_DIRDATA_BITMAP_KEYLEN;

    val_a[1].buffer_sz =
	meta_dist_dir_attr.bitmap_size *  /* bitmap_size = 1 */
	sizeof(PVFS_dist_dir_bitmap_basetype);
    val_a[1].buffer = dist_dir_bitmap;

    key_a[2].buffer = DIST_DIRDATA_HANDLES_KEYSTR;
    key_a[2].buffer_sz = DIST_DIRDATA_HANDLES_KEYLEN;

    val_a[2].buffer = &dirdata_handle; /* only one dirdata server */
    val_a[2].buffer_sz = meta_dist_dir_attr.num_servers * 
	sizeof(dirdata_handle);

    /* write to directory meta handle keyval space */
    ret = trove_keyval_write_list(
	    coll_id, dir_handle, key_a, val_a, keyval_count,
	    TROVE_SYNC, 0, NULL,
	    context_id, &op_id, NULL);
    if (ret < 0)
    {
	gossip_err("trove_keyval_write_list failed: \
		ret=%d coll=%d handle=%llu context=%lld op=%lld\n",
		ret, coll_id, llu(dir_handle), 
		llu(context_id), llu(op_id));
	goto complete;
    }

    TROVE_DSPACE_WAIT(ret, coll_id, op_id, context_id, \
	    count, state, complete);

    /* adjust dist_dir_attr val_a */
    val_a[0].buffer = &dirdata_dist_dir_attr;

    /* write to dirdata handle keyval space */
    ret = trove_keyval_write_list(
	    coll_id, dirdata_handle, key_a, val_a, keyval_count,
	    TROVE_SYNC, 0, NULL,
	    context_id, &op_id, NULL);
    if (ret < 0)
    {
	gossip_err("trove_keyval_write_list failed: \
		ret=%d coll=%d handle=%llu context=%lld op=%lld\n",
		ret, coll_id, llu(dirdata_handle), 
		llu(context_id), llu(op_id));
	goto complete;
    }

    TROVE_DSPACE_WAIT(ret, coll_id, op_id, context_id, \
	    count, state, complete);

complete:

    if (dist_dir_bitmap)
    {
	free(dist_dir_bitmap);
    }

    if(key_a)
    {
	free(key_a);
    }

    if(val_a)
    {
	free(val_a);
    }

    return ret;
}


/*
 * migrate_collection_0_1_5
 *   coll_id   - collection id
 *   data_path - path to data storage
 *   meta_path - path to metadata storage
 *
 * rename old keyval db and create new keyval db.
 * For each keyval db entry, add a type field in the key structure. 
 * add distributed directory structure for directory and dirdata handle
 * only set as one dirdata handle, will expand when splitting dirents.
 *
 * \return 0 on success, non-zero on failure
 */
static int migrate_collection_0_1_5 (TROVE_coll_id coll_id, 
				     const char* data_path,
				     const char* meta_path)
{

    TROVE_context_id  context_id = PVFS_CONTEXT_NULL;
    TROVE_ds_state    state;
    TROVE_op_id       keyval_op_id;
    TROVE_op_id       getattr_op_id;
    TROVE_ds_attributes_s ds_attr;
    int               op_count;
    int               ret, ret_db;

    struct dbpf_collection  *coll_p = NULL;
    char	    keyval_db_name[PATH_MAX];
    char	    old_keyval_db_name[PATH_MAX];
    DB		    *db_p = NULL;
    DBT		    key, data;
    DBC		    *dbc_p = NULL;
    struct dbpf_keyval_db_entry_0_1_5 *k;
    TROVE_keyval_s  t_key, t_val;

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s:enter.\n ", __func__);

    /* rename old keyval db, create new one */
    DBPF_GET_KEYVAL_DBNAME(keyval_db_name, PATH_MAX, meta_path, coll_id);
    snprintf(old_keyval_db_name, PATH_MAX, "%s.old.1.5.0", keyval_db_name);

    ret = access(old_keyval_db_name, F_OK);
    if(ret == 0)
    {
	gossip_err("Error: %s already exist, please make sure the migration from "
		"1.5.0 has not been performed!\n", old_keyval_db_name);
	return -1;
    }

    /* close keyval db */
    coll_p = dbpf_collection_find_registered(coll_id);

    if ((ret = coll_p->keyval_db->sync(coll_p->keyval_db, 0)) != 0)
    {
        gossip_err("db_sync(coll_keyval_db): %s\n", db_strerror(ret));
    }

    if ((ret = db_close(coll_p->keyval_db)) != 0) 
    {
        gossip_lerr("db_close(coll_keyval_db): %s\n", db_strerror(ret));
    }

    /* rename old keyval db */
    gossip_debug(GOSSIP_TROVE_DEBUG, "Renaming old keyval db.\n ");

    ret = rename(keyval_db_name, old_keyval_db_name);
    if(ret < 0)
    {
	gossip_err("%s: error when renaming keyval db.\n", 
		__func__);
	return ret;
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "Creating new keyval db.\n ");

    if ((ret = db_create(&db_p, NULL, 0)) != 0)
    {
        gossip_err("db_create: %s\n", db_strerror(ret));
        return -1;
    }
    
    if ((ret = db_open(db_p, keyval_db_name, TROVE_DB_CREATE_FLAGS, 
		    TROVE_DB_MODE)) != 0)
    {
        db_p->err(db_p, ret, "%s", keyval_db_name);
        db_close(db_p);
        gossip_err("fail to create new keyval db: %s\n", keyval_db_name);
        return -1;
    }

    if ((ret = db_close(db_p)) != 0)
    {
        gossip_err("close db: %s\n", db_strerror(ret));
        return -1;
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "Linking new keyval db.\n ");

    coll_p->keyval_db = dbpf_db_open(keyval_db_name, coll_p->coll_env,
                                     &ret, PINT_trove_dbpf_keyval_compare, 0);
    if(coll_p->keyval_db == NULL)
    {
        return ret;
    }

    /* open old keyval db and make it ready for reading */

    gossip_debug(GOSSIP_TROVE_DEBUG, "Opening old 1.5.0 keyval db.\n ");

    db_p = NULL;

    ret = db_create(&db_p, NULL, 0);
    if(ret != 0)
    {
        gossip_err("Error: db_create: %s.\n", db_strerror(ret));
        return(-1);
    }
     
    ret = db_open(db_p, old_keyval_db_name, TROVE_DB_OPEN_FLAGS, 0);

    if(ret != 0)
    {
        gossip_err("Error: db_p->open: %s.\n", db_strerror(ret));
        return(-1);
    }

    ret = db_p->cursor(db_p, NULL, &dbc_p, 0);
    if (ret != 0)
    {
        gossip_err("Error: db_p->cursor: %s.\n", db_strerror(ret));
        db_p->close(db_p, 0);
        return(-1);
    }

    /* setup keys */

    memset(&key, 0, sizeof(key));
    key.data = malloc(DEF_KEY_SIZE);
    if(!key.data)
    {
        gossip_err("malloc failed!\n");    
        dbc_p->c_close(dbc_p);
        db_p->close(db_p, 0);
        return(-1);
    }
    key.size = key.ulen = DEF_KEY_SIZE;
    key.flags |= DB_DBT_USERMEM;

    memset(&data, 0, sizeof(data));
    data.data = malloc(DEF_DATA_SIZE);
    if(!data.data)
    {
        gossip_err("malloc failed!\n");    
        free(key.data);
        dbc_p->c_close(dbc_p);
        db_p->close(db_p, 0);
        return(-1);
    }
    data.size = data.ulen = DEF_DATA_SIZE;
    data.flags |= DB_DBT_USERMEM;


    gossip_debug(GOSSIP_TROVE_DEBUG, "Opening trove context.\n ");

    /* open trove context */
    ret = trove_open_context(coll_id, &context_id);
    if (ret < 0)
    {
        gossip_err("trove_open_context failed: ret=%d coll=%d\n",
                   ret, coll_id);
        goto complete;
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "Iterate through each record in old db!.\n ");

    int count_r = 0;

    do
    {
        /* iterate through keys in the old keyval db */
        ret_db = dbc_p->c_get(dbc_p, &key, &data, DB_NEXT);
        if (ret_db != DB_NOTFOUND && ret_db != 0)
        {
            gossip_err("Error: dbc_p->c_get: %s.\n", db_strerror(ret_db));
	    ret = -1;
	    goto complete;
        }
        if(ret_db == 0)
        {
	    PVFS_handle cur_handle;
            PVFS_ds_flags trove_flags = TROVE_SYNC;

	    count_r++;

	    gossip_debug(GOSSIP_TROVE_DEBUG, 
		    " *** start processing #%d record.\n ", count_r);

	    k = key.data;
	    cur_handle = k->handle;

	    if( key.size == 8 ) 
	    {
		/* the count record, no insertion */
		continue;
	    }

	    /* get attributes to determine type */
	    ret = trove_dspace_getattr(coll_id,
		    cur_handle,
		    &ds_attr,
		    0,
		    NULL,
		    context_id,
		    &getattr_op_id,
		    PVFS_HINT_NULL); 
	    if (ret < 0)
	    {
		gossip_err("trove_dspace_getattr failed: \
			ret=%d coll=%d handle=%llu context=%lld op=%lld\n",
			ret, coll_id, llu(cur_handle), 
			llu(context_id), llu(getattr_op_id));
		goto complete;
	    }

	    TROVE_DSPACE_WAIT(ret, coll_id, getattr_op_id, context_id, \
		    op_count, state, complete);

	    switch(ds_attr.type)
	    {
		case PVFS_TYPE_DIRDATA:
		    trove_flags |= TROVE_KEYVAL_HANDLE_COUNT;
		    trove_flags |= TROVE_NOOVERWRITE;
		    trove_flags |= TROVE_KEYVAL_DIRECTORY_ENTRY;
		    break;
		case PVFS_TYPE_INTERNAL:
		    trove_flags |= TROVE_BINARY_KEY;
		    trove_flags |= TROVE_NOOVERWRITE;
		    trove_flags |= TROVE_KEYVAL_HANDLE_COUNT;
		    break;
		case PVFS_TYPE_DIRECTORY:
		    if(strncmp(k->key, DIRECTORY_ENTRY_KEYSTR,
			DIRECTORY_ENTRY_KEYLEN) == 0)
		    {
			/* directory entry record, 
			   insert distributed directory structure instead */
			ret = write_distdir_keyvals_0_1_5(coll_id,
				context_id, key, data, &ds_attr);
			if(ret < 0)
			{
			    goto complete;
			}

			continue;
		    }
		default:
		    /* other types ? */
		    break;
	    }

	    /* init t_key and t_val */
            memset(&t_key, 0, sizeof(t_key));
            memset(&t_val, 0, sizeof(t_val));
	    t_key.buffer = k->key;
	    t_key.buffer_sz = key.size - 8; /* excluding the handle */
	    t_val.buffer = data.data;
	    t_val.buffer_sz = data.size;

            /* write out new keyval pair */
            state = 0;
            ret = trove_keyval_write(
                coll_id, cur_handle, &t_key, &t_val, trove_flags, 0, NULL,
                context_id, &keyval_op_id, NULL);
	    if (ret < 0)
	    {
		gossip_err("trove_keyval_write failed: \
			ret=%d coll=%d handle=%llu context=%lld op=%lld\n",
			ret, coll_id, llu(cur_handle), 
			llu(context_id), llu(keyval_op_id));
		goto complete;
	    }

	    TROVE_DSPACE_WAIT(ret, coll_id, keyval_op_id, context_id, \
		    op_count, state, complete);

        }
    }while(ret_db != DB_NOTFOUND);

    gossip_debug(GOSSIP_TROVE_DEBUG, "Iterate done, totally %d records.\n ", count_r);

    /* success */

complete:

    /* close db, close trove context, cleanup */

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

    if(dbc_p != NULL)
    {
	dbc_p->c_close(dbc_p);
    }

    if(db_p != NULL)
    {
	db_p->close(db_p, 0);
    }

    if(data.data != NULL)
    {
	free(data.data);
    }

    if(key.data != NULL)
    {
	free(key.data);
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "%s:exit.\n ", __func__);
    return ret;
}
