/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_BUCKET_H
#define __PINT_BUCKET_H

#include <pvfs2-types.h>

/* This is the interface to the bucket management component of the
 * system interface.  It is responsible for managing the list of meta
 * and i/o servers and mapping between buckets and servers.
 */

int PINT_bucket_initialize(void);

int PINT_bucket_finalize(void);

int PINT_bucket_load_mapping(
	char* meta_mapping,
	int meta_count,
	char* io_mapping,
	int io_count, 
	PVFS_handle handle_mask,
	PVFS_fs_id fsid);

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

#endif
