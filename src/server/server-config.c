#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/types.h>
#include <linux/dirent.h>


#include <dotconf.h>

#include <server-config.h>
#include <pvfs2-storage.h>
#include <job.h>
#include <gossip.h>

static DOTCONF_CB(get_pvfs_server_id);
static DOTCONF_CB(get_tcp_path);
static DOTCONF_CB(get_unexp_req);
static DOTCONF_CB(start_meta_server_config);
static DOTCONF_CB(end_meta_server_config);
static DOTCONF_CB(start_io_server_config);
static DOTCONF_CB(end_io_server_config);
static DOTCONF_CB(get_gm_path);
static DOTCONF_CB(get_metaserver_list);
static DOTCONF_CB(get_storage_space);
static DOTCONF_CB(get_ioserver_list);
static DOTCONF_CB(get_filesystem_name);
static DOTCONF_CB(get_increment_filesystems);

static const configoption_t options[] = {
	{"HostID",ARG_STR, get_pvfs_server_id,NULL,CTX_ALL},
	{"BMI_TCP_Path",ARG_STR, get_tcp_path,NULL,CTX_ALL},
	{"BMI_GM_Path",ARG_STR, get_gm_path,NULL,CTX_ALL},
	{"StorageSpace",ARG_STR, get_storage_space,NULL,CTX_ALL},
	{"<FileSystem>",ARG_STR, get_increment_filesystems,NULL,CTX_ALL},
	{"MetaServerList",ARG_LIST, get_metaserver_list,NULL,CTX_ALL},
	{"IoServerList",ARG_LIST, get_ioserver_list,NULL,CTX_ALL},
	{"UnexpectedRequests",ARG_INT, get_unexp_req,NULL,CTX_ALL},
	{"<MetaServerConfig>", ARG_NONE, start_meta_server_config, NULL, CTX_ALL },
	{"</MetaServerConfig>", ARG_NONE, end_meta_server_config, NULL, CTX_ALL },
	{"<IOServerConfig>", ARG_NONE, start_io_server_config, NULL, CTX_ALL },
	{"</IOServerConfig>", ARG_NONE, end_io_server_config, NULL, CTX_ALL },
	LAST_OPTION
};

static struct server_configuration_s *config_s;  /* For the configuration */

/*
 * Function: server_config
 *
 * Params:   int argc,
 *           char **argv
 *
 * Returns:  struct server_configuration_s*
 *
 * Synopsis: Parse the config file according to parameters set
 *           configuration struct above.
 *           
 */

struct server_configuration_s *server_config(int argc, char **argv)
{
	configfile_t *configfile;

	config_s = (server_configuration_s *) malloc(sizeof(server_configuration_s));
	memset(config_s,0,sizeof(server_configuration_s));

	/* Lets support a max of PINT_SERVER_MAX_FILESYSTEMS */
	config_s->file_system_names = (char *) malloc(sizeof(char *)*PINT_SERVER_MAX_FILESYSTEMS)

	configfile = dotconf_create(argv[1] ? argv[1] : "simple.conf",
					options, NULL, CASE_INSENSITIVE);

	if (!configfile)
	{
		gossip_err("Error opening config file\n");
		return NULL;
	}

	if (dotconf_command_loop(configfile) == 0)
		gossip_err("Error reading config file\n");

	dotconf_cleanup(configfile);

#ifndef DEBUG_SERVER_CONFIG

	gossip_debug(SERVER_DEBUG,"Config File Read: Values\n");
	gossip_debug(SERVER_DEBUG,"Registering Server as: %s\n",config_s->host_id);
	gossip_debug(SERVER_DEBUG,"Path to TCP Module was: %s\n",config_s->tcp_path_bmi_library);
	gossip_debug(SERVER_DEBUG,"Meta Servers Were: %s\n",config_s->meta_server_list);
	gossip_debug(SERVER_DEBUG,"Meta Server Count Was: %d\n",config_s->count_meta_servers);
	gossip_debug(SERVER_DEBUG,"IO Servers Were: %s\n",config_s->io_server_list);
	gossip_debug(SERVER_DEBUG,"IO Server Count Was: %d\n",config_s->count_io_servers);

#endif

	return config_s;
}

DOTCONF_CB(get_storage_space)
{
	config_s->storage_path = (char *) malloc(strlen(cmd->data.str)+1);
	memset(config_s->storage_path,0,strlen(cmd->data.str)+1);
	strcpy(config_s->storage_path,cmd->data.str);
	return NULL;
}


	
/*
 * Function: get_metaserver_list
 *
 * Params:   command_t *cmd,
 *           context_t *ctx
 *
 * Returns:  const char *
 *
 * Synopsis: Take the list of meta servers and put them into a 
 *           contiguous string, delimiting by ;
 *           
 */

DOTCONF_CB(get_metaserver_list)
{

	int str_size = 0;
	int current_index = 0;
	int i;

	for(i=0;i<cmd->arg_count;i++)
	{
		str_size+=strlen(cmd->data.list[i]) +1;
	}

	config_s->meta_server_list = (char *) malloc(str_size);
	memset(config_s->meta_server_list,0,str_size);

	current_index=-1;
	for(i=0;i<cmd->arg_count;i++)
	{
		config_s->meta_server_list[current_index > 0 ? current_index : 0] = ';';
		memcpy(&(config_s->meta_server_list)[current_index+1],cmd->data.list[i],strlen(cmd->data.list[i])+1);
		current_index = strlen(config_s->meta_server_list); /* find the null character and tell me the length */
	}

	config_s->count_meta_servers = cmd->arg_count;
	return NULL;

}

/*
 * Function: get_ioserver_list
 *
 * Params:   command_t *cmd,
 *           context_t *ctx
 *
 * Returns:  const char *
 *
 * Synopsis: Take the list of io servers and put them into a 
 *           contiguous string, delimiting by ;
 *           
 */

DOTCONF_CB(get_ioserver_list)
{

	int str_size = 0;
	int current_index = 0;
	int i;

	for(i=0;i<cmd->arg_count;i++)
	{
		str_size+=strlen(cmd->data.list[i]) +1;
	}

	config_s->io_server_list = (char *) malloc(str_size);
	memset(config_s->io_server_list,0,str_size);

	current_index=-1;
	for(i=0;i<cmd->arg_count;i++)
	{
		config_s->io_server_list[current_index > 0 ? current_index : 0] = ';';
		memcpy(&(config_s->io_server_list)[current_index+1],cmd->data.list[i],strlen(cmd->data.list[i])+1);
		current_index = strlen(config_s->io_server_list); /* find the null character and tell me the length */
	}

	config_s->count_io_servers = cmd->arg_count;
	return NULL;

}

/*
 * Function: get_pvfs_server_id
 *
 * Params:   command_t *cmd,
 *           context_t *ctx
 *
 * Returns:  const char *
 *
 * Synopsis: get my host name!
 *           
 *           
 */

DOTCONF_CB(get_pvfs_server_id)
{

	config_s->host_id = (char *) malloc(strlen(cmd->data.str)+1);
	memset(config_s->host_id,0,strlen(cmd->data.str));
	memcpy(config_s->host_id,cmd->data.str,strlen(cmd->data.str)+1);
	return NULL;

}

/*
 * Function: get_unexp_req
 *
 * Params:   command_t *cmd,
 *           context_t *ctx
 *
 * Returns:  const char *
 *
 * Synopsis: Sets the number of initial unexpected requests
 *           TODO: do we need this?
 *           
 */

DOTCONF_CB(get_unexp_req)
{

	config_s->initial_unexpected_requests = cmd->data.value;
	return NULL;

}

/*
 * Function: get_gm_path
 *
 * Params:   command_t *cmd,
 *           context_t *ctx
 *
 * Returns:  const char *
 *
 * Synopsis: gets the location of the GM library path
 *           
 *           
 */

DOTCONF_CB(get_gm_path)
{

	/* +1 for the NULL Terminator =) */
	config_s->gm_path_bmi_library = (char *) malloc(strlen(cmd->data.str)+1);
	memcpy(config_s->gm_path_bmi_library,cmd->data.str,strlen(cmd->data.str)+1);
	return NULL;

}

/*
 * Function: get_tcp_path
 *
 * Params:   command_t *cmd,
 *           context_t *ctx
 *
 * Returns:  const char *
 *
 * Synopsis: gets the location of the tcp_bmi_library
 *           
 *           
 */

DOTCONF_CB(get_tcp_path)
{

	/* +1 for the NULL Terminator =) */
	config_s->tcp_path_bmi_library = (char *) malloc(strlen(cmd->data.str)+1);
	memcpy(config_s->tcp_path_bmi_library,cmd->data.str,strlen(cmd->data.str)+1);
	return NULL;

}

/*
 * Function: end_io_server_config
 *
 * Params:   command_t *cmd,
 *           context_t *ctx
 *
 * Returns:  const char *
 *
 * Synopsis: unused
 *           
 *           
 */

DOTCONF_CB(end_io_server_config)
{

	config_s->configuration_context = GLOBALCONFIG;
	gossip_ldebug(SERVER_DEBUG,"Exiting IO Server Config\n");
	return NULL;

}

/*
 * Function: start_io_server_config
 *
 * Params:   command_t *cmd,
 *           context_t *ctx
 *
 * Returns:  const char *
 *
 * Synopsis: unused
 *           
 *           
 */

DOTCONF_CB(start_io_server_config)
{

	config_s->configuration_context = IOSERVERCONFIG;
	gossip_ldebug(SERVER_DEBUG,"Entering IO Server Config\n");
	return NULL;

}

/*
 * Function: end_meta_server_config
 *
 * Params:   command_t *cmd,
 *           context_t *ctx
 *
 * Returns:  const char *
 *
 * Synopsis: unused
 *           
 *           
 */

DOTCONF_CB(end_meta_server_config)
{

	config_s->configuration_context = GLOBALCONFIG;
	return NULL;

}

/*
 * Function: start_meta_server_config
 *
 * Params:   command_t *cmd,
 *           context_t *ctx
 *
 * Returns:  const char *
 *
 * Synopsis: unused
 *           
 *           
 */

DOTCONF_CB(start_meta_server_config)
{

	config_s->configuration_context = METASERVERCONFIG;
	return NULL;

}

/*
  vim:set ts=4:
  vim:set shiftwidth=4:
*/
