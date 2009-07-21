/*
 * (C) 2009 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */       

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "pvfs2-internal.h"
#include "trove.h"
#include "gossip.h"
#include "trove-dbpf/dbpf.h"

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
static int migrate_collection_0_1_3 (TROVE_coll_id coll_id,const char* stoname);

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
    int (*migrate)(TROVE_coll_id coll_id, const char* stoname);
};

struct migration_s migration_table[] =
{
    { 0, 1, 3, migrate_collection_0_1_3 },
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
 *   stoname   - path to storage
 *
 * Iterate over all collections and migrate each one.
 * \return 0 on success, non-zero on failure
 */
int trove_migrate (TROVE_method_id method_id, const char* stoname)
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
                    ret = migrate_p->migrate(coll_id, stoname);
                    if (ret < 0)
                    {
                        gossip_err("migrate failed: \
ret=%d coll=%d stoname=%s major=%d minor=%d incremental=%d\n",
                                   ret, coll_id, stoname, migrate_p->major,
                                   migrate_p->minor, migrate_p->incremental);
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
 *   coll_id - collection id
 *   stoname - path to storage
 *
 * For each datafile handle, check the file length and update the
 * b_size attribute.
 * \return 0 on success, non-zero on failure
 */
static int migrate_collection_0_1_3 (TROVE_coll_id coll_id, const char* stoname)
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
                                          stoname,
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
