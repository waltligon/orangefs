/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_CACHED_CONFIG_H
#define __PINT_CACHED_CONFIG_H

#include "pvfs2-types.h"
#include "pvfs2-storage.h"
#include "pvfs2-mgmt.h"
#include "bmi.h"
#include "dotconf.h"
#include "trove.h"
#include "server-config.h"

/* This is the interface to the cached_config management component of the
 * system interface.  It is responsible for managing the list of meta
 * and data servers and mapping between handle ranges and servers.
 */
int PINT_cached_config_initialize(void);

int PINT_cached_config_finalize(void);

int PINT_handle_load_mapping(struct server_configuration_s *config,
			     struct filesystem_configuration_s *fs);

int PINT_cached_config_get_next_meta(struct server_configuration_s *config,
			      PVFS_fs_id fsid,
			      PVFS_BMI_addr_t *meta_addr,
			      PVFS_handle_extent_array *meta_extent_array);

int PINT_cached_config_get_next_io(struct server_configuration_s *config,
			    PVFS_fs_id fsid,
			    int num_servers,
			    PVFS_BMI_addr_t *io_addr_array,
			    PVFS_handle_extent_array *io_handle_extent_array);

#define PINT_BUCKET_IO PVFS_MGMT_IO_SERVER
#define PINT_BUCKET_META PVFS_MGMT_META_SERVER
#define PINT_BUCKET_ALL (PINT_BUCKET_META|PINT_BUCKET_IO)

const char* PINT_cached_config_map_addr(struct server_configuration_s* config,
				 PVFS_fs_id fsid,
				 PVFS_BMI_addr_t addr,
				 int* server_type);

int PINT_cached_config_get_server_array(struct server_configuration_s* config,
				 PVFS_fs_id fsid,
				 int server_type,
				 PVFS_BMI_addr_t *addr_array,
				 int* inout_count_p);

int PINT_cached_config_count_servers(struct server_configuration_s* config,
			      PVFS_fs_id fsid,
			      int server_type,
			      int* count);

int PINT_cached_config_map_to_server(PVFS_BMI_addr_t *server_addr,
			      PVFS_handle handle,
			      PVFS_fs_id fsid);

int PINT_cached_config_get_num_dfiles(PVFS_fs_id fsid,
                               PINT_dist* dist,
                               PVFS_sys_attr attr,
                               int* num_dfiles);

int PINT_cached_config_get_num_meta(PVFS_fs_id fsid,
			     int *num_meta);

int PINT_cached_config_get_num_io(PVFS_fs_id fsid,
			   int *num_io);

int PINT_cached_config_get_server_name(char *server_name,
				int max_server_name_len,
				PVFS_handle handle,
				PVFS_fs_id fsid);

int PINT_cached_config_get_server_handle_count(const char* server_addr_str,
					PVFS_fs_id fs_id,
					uint64_t* handle_count);
    
int PINT_cached_config_get_root_handle(PVFS_fs_id fsid,
				PVFS_handle *fh_root);

#define map_handle_range_to_extent_list(hrange_list)             \
do { cur = hrange_list;                                          \
 while(cur) {                                                    \
     cur_mapping = PINT_llist_head(cur);                         \
     if (!cur_mapping) break;                                    \
     assert(cur_mapping->alias_mapping);                         \
     assert(cur_mapping->alias_mapping->host_alias);             \
     assert(cur_mapping->handle_range);                          \
     cur_host_extent_table = (bmi_host_extent_table_s *)malloc(  \
         sizeof(bmi_host_extent_table_s));                       \
     if (!cur_host_extent_table) {                               \
         ret = -ENOMEM;                                          \
         break;                                                  \
     }                                                           \
     cur_host_extent_table->bmi_address =                        \
         PINT_config_get_host_addr_ptr(                          \
             config,cur_mapping->alias_mapping->host_alias);     \
     assert(cur_host_extent_table->bmi_address);                 \
     cur_host_extent_table->extent_list =                        \
         PINT_create_extent_list(cur_mapping->handle_range);     \
     if (!cur_host_extent_table->extent_list) {                  \
         free(cur_host_extent_table);                            \
         ret = -ENOMEM;                                          \
         break;                                                  \
     }                                                           \
     /*                                                          \
       add this host to extent list mapping to                   \
       config cache object's host extent table                   \
     */                                                          \
     ret = PINT_llist_add_to_tail(                               \
         cur_config_fs_cache->bmi_host_extent_tables,            \
         cur_host_extent_table);                                 \
     assert(ret == 0);                                           \
     cur = PINT_llist_next(cur);                                 \
 } } while(0)


#endif /* __PINT_CACHED_CONFIG_H */
