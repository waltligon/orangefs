/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

/* Extended Attributes
 *
 * These functions provide checking of extended attributes formats
 * and namespaces
 */

/* PINT_eattr_list_verify
 *
 * Verify the extended attribute using the check access structure array
 */
int PINT_eattr_list_access(PVFS_ds_keyval *key, PVFS_ds_keyval *val);

/* PINT_eattr_check_access
 *
 * Check that a request extended attribute is correctly formatted and
 * within a valid namespace
 */
int PINT_eattr_check_access(PVFS_ds_keyval *key, PVFS_ds_keyval *val);

/* PINT_eattr_namespace_verify
 *
 * Checks that the eattr has a valid namespace that PVFS accepts
 */
int PINT_eattr_namespace_verify(PVFS_ds_keyval *k, PVFS_ds_keyval *v);

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */

