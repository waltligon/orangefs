/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <trove.h>
#include <trove-internal.h>

/* file system operations -- these are simply helper functions (I think) */
extern struct TROVE_fs_ops **fs_method_table;

int trove_filesystem_create(
	/* char *stoname, */
	char *collname,
	TROVE_coll_id new_coll_id,
	TROVE_handle root_dir_handle,
	void *user_ptr,
	TROVE_op_id *out_op_id_p)
{
	/* TODO: HOW DO I KNOW WHAT METHOD TO USE??? */
	return -1;
}

int trove_filesystem_remove(
	char *collname,
	void *user_ptr,
	TROVE_op_id *out_op_id_p)
{
	/* TODO: HOW DO I KNOW WHAT METHOD TO USE??? */
	return -1;
}

int trove_filesystem_lookup(
	char *collname,
	TROVE_coll_id *coll_id_p,
	void *user_ptr,
	TROVE_op_id *out_op_id_p)
{
	/* TODO: HOW DO I KNOW WHAT METHOD TO USE??? */
	return -1;
}

int trove_filesystem_get_root(
	TROVE_coll_id coll_id,
	TROVE_handle *root_handle_p,
	void *user_ptr,
	TROVE_op_id *out_op_id_p)
{
	int method_id;

	method_id = map_coll_id_to_method(coll_id);
	if (method_id < 0) {
		return -1; /* NEED STATUS TYPE FOR THIS */
	}

	return fs_method_table[method_id]->fs_get_root(
		coll_id,
		root_handle_p,
		user_ptr,
		out_op_id_p);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=4
 */
