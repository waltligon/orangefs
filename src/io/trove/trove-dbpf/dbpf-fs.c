/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <db.h>
#include <time.h>
#include <malloc.h>

#include <trove.h>
#include <trove-internal.h>
#include <dbpf.h>

extern struct TROVE_mgmt_ops dbpf_mgmt_ops;
extern struct TROVE_dspace_ops dbpf_dspace_ops;
extern struct dbpf_collection *my_coll_p;

/* dbpf_filesystem_create()
 *
 */
static int dbpf_filesystem_create(
				  /* char *stoname, */
				  char *collname,
				  TROVE_coll_id new_coll_id,
				  TROVE_handle root_dir_handle,
				  void *user_ptr,
				  TROVE_op_id *out_op_id_p)
{
    int ret;
    DB *db_p;
    DBT key, data;

    /* create the collection */
    ret = dbpf_mgmt_ops.collection_create(
					  /* stoname, */
					  collname,
					  new_coll_id,
					  user_ptr,
					  out_op_id_p);
    /* TODO: handle in progress case */
    if (ret != 1) return -1;

    /* create the root directory dspace */
    ret = dbpf_dspace_ops.dspace_create(
					new_coll_id,
					&root_dir_handle,
					0xffffffff, /* force this handle */
					1, /* TODO: define types! */
					NULL, /* hint */
					user_ptr,
					out_op_id_p);
    /* TODO: handle in progress case */
    if (ret != 1) return -1;

    /* TODO: make this a generic set attribute on the collection */
    /* store root handle value as an attribute on the collection */
    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data = ROOT_HANDLE_STRING;
    key.size = sizeof(ROOT_HANDLE_STRING);
    data.data = &root_dir_handle;
    data.size = sizeof(root_dir_handle);
    ret = db_p->put(db_p, NULL, &key, &data, 0);
    if (ret != 0) return -1;

    if ((ret = db_p->close(db_p, 0)) != 0) {
	fprintf(stderr, "dbpf_storage_create: %s\n",
		db_strerror(ret));
	return -1;
    }

    return 1;
}

/* dbpf_filesystem_remove()
 *
 * Passes straight through to the collection remove routine.
 */
static int dbpf_filesystem_remove(
				  char *collname,
				  void *user_ptr,
				  TROVE_op_id *out_op_id_p)
{
    return dbpf_mgmt_ops.collection_remove(
					   collname,
					   user_ptr,
					   out_op_id_p);
}

/* dbpf_filesystem_lookup()
 *
 * Passes straight through to the collection lookup routine.
 */
static int dbpf_filesystem_lookup(
				  char *collname,
				  TROVE_coll_id *coll_id_p,
				  void *user_ptr,
				  TROVE_op_id *out_op_id_p)
{
    return dbpf_mgmt_ops.collection_lookup(
					   collname,
					   coll_id_p,
					   user_ptr,
					   out_op_id_p);
}

/* dbpf_filesystem_get_root()
 */
static int dbpf_filesystem_get_root(
				    TROVE_coll_id coll_id,
				    TROVE_handle *root_handle_p,
				    void *user_ptr,
				    TROVE_op_id *out_op_id_p)
{
    int ret;
    TROVE_handle root_handle;
    DBT key, data;
    struct dbpf_collection *coll_p;

    /* TODO: find collection in memory */
    if (my_coll_p == NULL) return -1;
    coll_p = my_coll_p;

    /* TODO: make this a generic get attribute on the collection */
    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data = ROOT_HANDLE_STRING;
    key.size = sizeof(ROOT_HANDLE_STRING);
    data.data = &root_handle;
    data.ulen = sizeof(root_handle);
    data.flags = DB_DBT_USERMEM; /* put the data in our space */
    ret = coll_p->coll_attr_db->get(coll_p->coll_attr_db,
				    NULL,
				    &key,
				    &data,
				    0);

    *root_handle_p = root_handle;
    return 1;
}

struct TROVE_fs_ops dbpf_fs_ops =
{
    dbpf_filesystem_create,
    dbpf_filesystem_remove,
    dbpf_filesystem_lookup,
    dbpf_filesystem_get_root
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sw=4 noexpandtab
 */
