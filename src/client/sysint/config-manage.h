/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __CONFIG_MANAGE_H
#define __CONFIG_MANAGE_H

#include <string.h>
#include <gen-locks.h>
#include <pvfs2-types.h>
#include <pvfs2-attr.h>
#include <pint-sysint.h>

/* public interface */
/* Note: The functions under the configuration management interface are
 * grouped together by functionality 
 */
/* Bucket Table Management Functions */
int config_bt_initialize(pvfs_mntlist mntent_list);
int config_bt_finalize(void);
/*int config_bt_load_mapping(char *meta_mapping, int meta_count,\
	char *io_mapping, int io_count, PVFS_handle handle_mask, PVFS_fs_id fsid);
*/
int config_bt_get_next_meta_bucket(PVFS_fs_id fsid, char **meta_name,\
	PVFS_handle *bucket, PVFS_handle *handle_mask);
int config_bt_get_next_io_bucket_array(PVFS_fs_id fsid, int num_servers,\
	char **io_name_array, PVFS_handle **bucket_array, PVFS_handle *handle_mask);
int config_bt_map_bucket_to_server(char **server_name, PVFS_handle bucket,\
	PVFS_fs_id fsid);
int config_bt_map_server_to_bucket_array(char **server_name,\
	PVFS_handle **bucket_array, PVFS_handle *handle_mask);
int config_bt_get_num_meta(PVFS_fs_id fsid, int *num_meta);
int config_bt_get_num_io(PVFS_fs_id fsid, int *num_io);
	
/* File System Specific Info */
int config_fsi_get_root_handle(PVFS_fs_id fsid,PVFS_handle *fh_root);
int config_fsi_get_io_server(PVFS_fs_id fsid,char **io_server_array,\
		int *num_io);
int config_fsi_get_meta_server(PVFS_fs_id fsid,char **meta_server_array,\
		int *num_meta);
int config_fsi_get_fsid(PVFS_fs_id *fsid, char *mnt_dir );

#endif
