/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <trove.h>
#include <trove-internal.h>

/* Currently we only have one method for these tables to refer to */
struct TROVE_mgmt_ops    *mgmt_method_table[1];
struct TROVE_dspace_ops  *dspace_method_table[1];
struct TROVE_keyval_ops  *keyval_method_table[1];
struct TROVE_bstream_ops *bstream_method_table[1];

/* Currently DBPF is our only implementation */
extern struct TROVE_mgmt_ops    dbpf_mgmt_ops;
extern struct TROVE_dspace_ops  dbpf_dspace_ops;
extern struct TROVE_keyval_ops  dbpf_keyval_ops;
extern struct TROVE_bstream_ops dbpf_bstream_ops;

/* NOTE: the collection get/set info/eattr functions are automatically 
 * generated.
 */

int trove_initialize(char *stoname,
		     TROVE_ds_flags flags,
		     char **method_name_p,
		     int method_id)
{
    int ret;
    char *ret_method_name_p;

    /* for each underlying method, call its initialize function */

    /* currently all we have is dbpf */


    /* add mapping into method table */
    mgmt_method_table[0]    = &dbpf_mgmt_ops;
    dspace_method_table[0]  = &dbpf_dspace_ops;
    keyval_method_table[0]  = &dbpf_keyval_ops;
    bstream_method_table[0] = &dbpf_bstream_ops;

    /* initialize can fail if storage name isn't valid, but we want those
     * ops pointers to be right either way.
     */
    ret = dbpf_mgmt_ops.initialize(stoname,
				   flags, 
				   &ret_method_name_p,
				   0); /* first and only method */
    if (ret < 0) return ret;

    /* TODO: do something with returned name? */

    return 1;
}

int trove_finalize(void)
{
    int ret;

    ret = mgmt_method_table[0]->finalize();
    if (ret < 0) return ret;

    return 1;
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
