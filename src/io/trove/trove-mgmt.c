/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>

#include "trove.h"
#include "trove-internal.h"
#include "gen-locks.h"

/* Currently we only have one method for these tables to refer to */
struct TROVE_mgmt_ops    *mgmt_method_table[1];
struct TROVE_dspace_ops  *dspace_method_table[1];
struct TROVE_keyval_ops  *keyval_method_table[1];
struct TROVE_bstream_ops *bstream_method_table[1];
struct TROVE_fs_ops      *fs_method_table[1];

/* Currently DBPF is our only implementation */
extern struct TROVE_mgmt_ops    dbpf_mgmt_ops;
extern struct TROVE_dspace_ops  dbpf_dspace_ops;
extern struct TROVE_keyval_ops  dbpf_keyval_ops;
extern struct TROVE_bstream_ops dbpf_bstream_ops;
extern struct TROVE_fs_ops      dbpf_fs_ops;

/* NOTE: the collection get/set info/eattr functions are automatically 
 * generated.
 */

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

/* trove_initialize()
 *
 * Returns -1 on failure (already initialized), 1 on success.  This is in
 * keeping with the "1 is immediate succcess" semantic for return values used
 * throughout trove.
 */
int trove_initialize(char *stoname,
		     TROVE_ds_flags flags,
		     char **method_name_p,
		     int method_id)
{
    int ret;
    char *ret_method_name_p;

    ret = gen_mutex_lock(&trove_init_mutex);
    assert (!ret);
    if (trove_init_status) {
	gen_mutex_unlock(&trove_init_mutex);
	return -1;
    }

    /* for each underlying method, call its initialize function */

    /* currently all we have is dbpf */

    /* add mapping into method table */
    mgmt_method_table[0]    = &dbpf_mgmt_ops;
    dspace_method_table[0]  = &dbpf_dspace_ops;
    keyval_method_table[0]  = &dbpf_keyval_ops;
    bstream_method_table[0] = &dbpf_bstream_ops;
    fs_method_table[0]      = &dbpf_fs_ops;

    /* initialize can fail if storage name isn't valid, but we want those
     * ops pointers to be right either way.
     */
    ret = dbpf_mgmt_ops.initialize(stoname,
				   flags, 
				   &ret_method_name_p,
				   0); /* first and only method */

    /* TODO: do something with returned name? */

    if (ret >= 0) {
	ret = 1;
	trove_init_status = 1;
    }
    gen_mutex_unlock(&trove_init_mutex);
    return ret;
}

/* trove_finalize()
 */
int trove_finalize(void)
{
    int ret;

    ret = gen_mutex_lock(&trove_init_mutex);
    assert (!ret);
    if (!trove_init_status) {
	gen_mutex_unlock(&trove_init_mutex);
	return -1;
    }
    else {
	trove_init_status = 0;
    }

    ret = mgmt_method_table[0]->finalize();

    gen_mutex_unlock(&trove_init_mutex);
    if (ret < 0) return ret;
    else return 1;
}

int trove_storage_create(char *stoname,
			 void *user_ptr,
			 TROVE_op_id *out_op_id_p)
{
    int ret;

    ret = mgmt_method_table[0]->storage_create(stoname,
					       user_ptr,
					       out_op_id_p);
    if (ret < 0) return ret;

    return 1;
}


int trove_storage_remove(char *stoname,
			 void *user_ptr,
			 TROVE_op_id *out_op_id_p)
{
    int ret;

    ret = mgmt_method_table[0]->storage_remove(stoname,
					       user_ptr,
					       out_op_id_p);
    if (ret < 0) return ret;

    return 1;
}

int trove_collection_create(
			    /* char *stoname, */
			    char *collname,
			    TROVE_coll_id new_coll_id,
			    void *user_ptr,
			    TROVE_op_id *out_op_id_p)
{
    int ret;
    /* TODO: HOW DO I KNOW WHAT METHOD TO USE??? */

    ret = mgmt_method_table[0]->collection_create(collname,
						  new_coll_id,
						  user_ptr,
						  out_op_id_p);
    if (ret < 0) return ret;

    return 1;
}

int trove_collection_remove(
			    /* char *stoname, */
			    char *collname,
			    void *user_ptr,
			    TROVE_op_id *out_op_id_p)
{
    int ret;

    /* TODO: HOW DO I KNOW WHAT METHOD TO USE??? */
    ret = mgmt_method_table[0]->collection_remove(collname,
						  user_ptr,
						  out_op_id_p);
    if (ret < 0) return ret;

    return 1;
}

int trove_collection_lookup(
			    /* char *stoname, */
			    char *collname,
			    TROVE_coll_id *coll_id_p,
			    void *user_ptr,
			    TROVE_op_id *out_op_id_p)
{
    int ret;

    /* TODO: HOW DO I KNOW WHAT METHOD TO USE??? */

    ret = mgmt_method_table[0]->collection_lookup(collname,
						  coll_id_p,
						  user_ptr,
						  out_op_id_p);
    if (ret < 0) return ret;
    return 1;
}

/* map_coll_id_to_method()
 *
 * NOTE: this is a hack for now.
 */
int map_coll_id_to_method(int coll_id)
{
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=4
 */
