/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_BUCKET_H
#define __PINT_BUCKET_H

#include "pvfs2-types.h"
#include "bmi.h"

/* This is the interface to the bucket management component of the
 * system interface.  It is responsible for managing the list of meta
 * and i/o servers and mapping between buckets and servers.
 */

int PINT_bucket_initialize(void);

int PINT_bucket_finalize(void);

/*
  NOTE: the incoming pointer MUST be a valid
  struct filesystem_configuration_s * object.

  e.g.
  int PINT_handle_load_mapping(struct filesystem_configuration_s *fs);

  it's declared void to be used in llist_doall, as well as
  relaxing the requirement that this file should know what
  that datatype is.
*/
int PINT_handle_load_mapping(void *fs);

int PINT_bucket_get_next_meta(
	PVFS_fs_id fsid,
	bmi_addr_t* meta_addr,
	PVFS_handle* bucket,
	PVFS_handle* handle_mask);

int PINT_bucket_get_next_io(
	PVFS_fs_id fsid,
	int num_servers,
	bmi_addr_t* io_addr_array,
	PVFS_handle* bucket_array,
	PVFS_handle* handle_mask);

int PINT_bucket_map_to_server(
	bmi_addr_t* server_addr,
	PVFS_handle bucket,
	PVFS_fs_id fsid);

int PINT_bucket_map_from_server(
	char* server_name,
	int* inout_count, 
	PVFS_handle* bucket_array,
	PVFS_handle* handle_mask);

int PINT_bucket_get_num_meta(
	PVFS_fs_id fsid,
	int* num_meta);

int PINT_bucket_get_num_io(
	PVFS_fs_id fsid,
	int* num_io);

int PINT_bucket_get_server_name(
	char* server_name,
	int max_server_name_len,
	PVFS_handle bucket,
	PVFS_fs_id fsid);

int PINT_bucket_get_root_handle(
        PVFS_fs_id fsid,
        PVFS_handle *fh_root);

#endif
