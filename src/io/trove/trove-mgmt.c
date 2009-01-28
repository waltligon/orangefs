/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>

#include "gossip.h"
#include "trove.h"
#include "trove-internal.h"
#include "gen-locks.h"
#include "trove-handle-mgmt/trove-handle-mgmt.h"

TROVE_method_callback global_trove_method_callback;

static TROVE_method_id TROVE_default_method(TROVE_coll_id id);

extern struct TROVE_mgmt_ops dbpf_mgmt_ops;
extern struct TROVE_mgmt_ops dbpf_mgmt_direct_ops;
extern struct TROVE_dspace_ops dbpf_dspace_ops;
extern struct TROVE_keyval_ops dbpf_keyval_ops;
extern struct TROVE_bstream_ops dbpf_bstream_ops;
extern struct TROVE_context_ops dbpf_context_ops;

extern struct TROVE_bstream_ops alt_aio_bstream_ops;
extern struct TROVE_bstream_ops null_aio_bstream_ops;
extern struct TROVE_bstream_ops dbpf_bstream_direct_ops;

/* currently we only have one method for these tables to refer to */
struct TROVE_mgmt_ops *mgmt_method_table[] =
{
    &dbpf_mgmt_ops,
    &dbpf_mgmt_ops, /* alt-aio */
    &dbpf_mgmt_ops, /* null-aio */
    &dbpf_mgmt_direct_ops  /* direct-io */

};

struct TROVE_dspace_ops *dspace_method_table[] =
{
    &dbpf_dspace_ops,
    &dbpf_dspace_ops, /* alt-aio */
    &dbpf_dspace_ops, /* null-aio */
    &dbpf_dspace_ops  /* direct-io */
};

struct TROVE_keyval_ops *keyval_method_table[] =
{
    &dbpf_keyval_ops,
    &dbpf_keyval_ops, /* alt-aio */
    &dbpf_keyval_ops, /* null-aio */
    &dbpf_keyval_ops  /* direct-io */
};

struct TROVE_bstream_ops *bstream_method_table[] =
{
    &dbpf_bstream_ops,
    &alt_aio_bstream_ops,
    &null_aio_bstream_ops,
    &dbpf_bstream_direct_ops
};

struct TROVE_context_ops *context_method_table[] =
{
    &dbpf_context_ops,
    &dbpf_context_ops, /* alt-aio */
    &dbpf_context_ops, /* null-aio */
    &dbpf_context_ops  /* direct-io */
};

/* trove_init_mutex, trove_init_status
 *
 * These two are used to ensure that trove is only initialized once.
 *
 * A program is erroneous if it performs trove operations before calling
 * trove_initialize(), and we don't try to help in that case.  We do,
 * however, make sure that we don't destroy anything if initialize is called
 * more than once.
 */
static gen_mutex_t trove_init_mutex = GEN_MUTEX_INITIALIZER;
static int trove_init_status = 0;

/* Returns -TROVE_EALREADY on failure (already initialized), 1 on
 * success.  This is in keeping with the "1 is immediate succcess"
 * semantic for return values used throughout trove.
 */
int trove_initialize(TROVE_method_id method_id,
                     TROVE_method_callback method_callback,
                     char *stoname,
                     TROVE_ds_flags flags)
{
    int ret = -TROVE_EALREADY;

    gen_mutex_lock(&trove_init_mutex);
    if (trove_init_status)
    {
        return ret;
    }

    ret = trove_handle_mgmt_initialize();
    if (ret == -1)
    {
        return ret;
    }

    if(!method_callback)
    {
        global_trove_method_callback = TROVE_default_method;
    }
    else
    {
        global_trove_method_callback = method_callback;
    }

    /*
      for each underlying method, call its initialize function.
      initialize can fail if storage name isn't valid, but we want
      those op pointers to be right either way.
    */
    ret = mgmt_method_table[method_id]->initialize(
        stoname, flags);
    if (ret > -1)
    {
        ret = 1;
        trove_init_status = 1;
    }
    gen_mutex_unlock(&trove_init_mutex);
    return ret;
}

int trove_finalize(TROVE_method_id method_id)
{
    int ret = -TROVE_EALREADY;

    gen_mutex_lock(&trove_init_mutex);
    if (!trove_init_status)
    {
        gen_mutex_unlock(&trove_init_mutex);
        return ret;
    }
    else
    {
        trove_init_status = 0;
    }

    ret = mgmt_method_table[method_id]->finalize();

    ret = trove_handle_mgmt_finalize();

    gen_mutex_unlock(&trove_init_mutex);

    return ((ret < 0) ? ret : 1);
}

int trove_storage_create(TROVE_method_id method_id,
                         char *stoname,
                         void *user_ptr,
                         TROVE_op_id *out_op_id_p)
{
    int ret = mgmt_method_table[method_id]->storage_create(
        stoname, user_ptr, out_op_id_p);

    return ((ret < 0) ? ret : 1);
}


int trove_storage_remove(TROVE_method_id method_id,
                         char *stoname,
                         void *user_ptr,
                         TROVE_op_id *out_op_id_p)
{
    int ret = mgmt_method_table[method_id]->storage_remove(
        stoname, user_ptr, out_op_id_p);

    return ((ret < 0) ? ret : 1);
}

int trove_collection_create(char *collname,
                            TROVE_coll_id new_coll_id,
                            void *user_ptr,
                            TROVE_op_id *out_op_id_p)
{
    TROVE_method_id method_id;
    int ret = -TROVE_EINVAL;

    if (new_coll_id == TROVE_COLL_ID_NULL)
    {
        gossip_err("Error: invalid collection ID requested.\n");
        return ret;
    }

    method_id = global_trove_method_callback(new_coll_id);
    ret = mgmt_method_table[method_id]->collection_create(
        collname, new_coll_id, user_ptr, out_op_id_p);

    return ((ret < 0) ? ret : 1);
}

int trove_collection_remove(TROVE_method_id method_id,
                            char *collname,
                            void *user_ptr,
                            TROVE_op_id *out_op_id_p)
{
    int ret = mgmt_method_table[method_id]->collection_remove(
        collname, user_ptr, out_op_id_p);

    return ((ret < 0) ? ret : 1);
}

int trove_collection_lookup(TROVE_method_id method_id,
                            char *collname,
                            TROVE_coll_id *coll_id_p,
                            void *user_ptr,
                            TROVE_op_id *out_op_id_p)
{
    int ret = mgmt_method_table[method_id]->collection_lookup(
        collname, coll_id_p, user_ptr, out_op_id_p);

    return (ret < 0) ? ret : 1;
}

int trove_collection_iterate(TROVE_method_id method_id,
                             TROVE_ds_position *inout_position_p,
                             TROVE_keyval_s *name_array,
                             TROVE_coll_id *coll_id_array,
                             int *inout_count_p,
                             TROVE_ds_flags flags,
                             TROVE_vtag_s *vtag,
                             void *user_ptr,
                             TROVE_op_id *out_op_id_p)
{
    int ret = mgmt_method_table[method_id]->collection_iterate(
        inout_position_p, name_array, coll_id_array, inout_count_p,
        flags, vtag, user_ptr, out_op_id_p);

    return ((ret < 0) ? ret : 1);
}

int trove_open_context(
    TROVE_coll_id coll_id,
    TROVE_context_id *context_id)
{
    TROVE_method_id method_id;
    int ret = 0;

    method_id = global_trove_method_callback(coll_id);
    if(method_id < 0)
    {
        return -TROVE_EINVAL;
    }

    if (trove_init_status != 0)
    {
        ret = context_method_table[method_id]->open_context(
            coll_id, context_id);
    }
    return ret;
}

int trove_close_context(
    TROVE_coll_id coll_id,
    TROVE_context_id context_id)
{
    TROVE_method_id method_id;
    int ret = 0;

    method_id = global_trove_method_callback(coll_id);
    if(method_id < 0)
    {
        return -TROVE_EINVAL;
    }

    if (trove_init_status != 0)
    {
        ret = context_method_table[method_id]->close_context(
            coll_id, context_id);
    }
    return ret;
}

int trove_collection_clear(
    TROVE_method_id method_id,
    TROVE_coll_id coll_id)
{
    return mgmt_method_table[method_id]->collection_clear(coll_id);
}


static TROVE_method_id TROVE_default_method(TROVE_coll_id id)
{
    return TROVE_METHOD_DBPF;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
