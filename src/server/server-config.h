
#ifndef __SERVER_CONFIG_H
#define __SERVER_CONFIG_H

enum 
{
	GLOBAL_CONFIG = 1,
	FILESYSTEM_CONFIG = 2,
	PINT_SERVER_MAX_FILESYSTEMS = 5
};

enum
{
	ENABLE_BMI_TCP = 1,
	ENABLE_BMI_GM = 2
};

typedef struct filesystem_configuration_s {
	char *file_system_name;
	TROVE_coll_id coll_id;
	char *meta_server_list;
	char *io_server_list;
	int  count_meta_servers;
	int  count_io_servers;
} filesystem_configuration_s;

typedef struct server_configuration_s {
   char *host_id;
	char *storage_path;
	int  bmi_protocols;
   int  initial_unexpected_requests;
   int configuration_context;
	char *default_meta_server_list;
	char *default_io_server_list;
	int  default_count_meta_servers;
	int  default_count_io_servers;
	int  number_filesystems;
	filesystem_configuration_s **file_systems;
	char **file_system_names;
} server_configuration_s;

struct server_configuration_s *PINT_server_config(int argc,char **argv);

#endif  /* __SERVER_CONFIG_H */
