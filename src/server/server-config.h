
#ifndef __SERVER_CONFIG_H
#define __SERVER_CONFIG_H

#define GLOBALCONFIG 1
#define IOSERVERCONFIG 2
#define METASERVERCONFIG 4

typedef struct server_configuration_s {
   char *host_id;
   char *tcp_path_bmi_library;
   char *gm_path_bmi_library;
	char *meta_server_list;
	char *io_server_list;
	char *storage_path;
	int  count_meta_servers;
	int  count_io_servers;
   int  initial_unexpected_requests;
   int configuration_context;
} server_configuration_s;

struct server_configuration_s *server_config(int argc,char **argv);

#endif  /* __SERVER_CONFIG_H */
