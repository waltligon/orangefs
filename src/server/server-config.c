#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/types.h>
#include <linux/dirent.h>


#include <dotconf.h>

#include <trove.h>
#include <server-config.h>
#include <pvfs2-storage.h>
#include <job.h>
#include <gossip.h>

static DOTCONF_CB(get_pvfs_server_id);
static DOTCONF_CB(enable_tcp);
static DOTCONF_CB(get_unexp_req);
static DOTCONF_CB(enable_gm);
static DOTCONF_CB(get_metaserver_list);
static DOTCONF_CB(get_storage_space);
static DOTCONF_CB(get_ioserver_list);
static DOTCONF_CB(get_filesystem_name);
static DOTCONF_CB(enter_filesystem_context);
static DOTCONF_CB(exit_filesystem_context);
static DOTCONF_CB(get_bucket_table_list);
static DOTCONF_CB(get_bucket_table);

static const configoption_t options[] = {
	{"HostID",ARG_STR, get_pvfs_server_id,NULL,CTX_ALL},
	{"BMI_TCP_Enable",ARG_TOGGLE, enable_tcp,NULL,CTX_ALL},
	{"BMI_GM_Enable",ARG_STR, enable_gm,NULL,CTX_ALL},
	{"StorageSpace",ARG_STR, get_storage_space,NULL,CTX_ALL},
	{"<FileSystem>",ARG_NONE, enter_filesystem_context,NULL,CTX_ALL},
	{"</FileSystem>",ARG_NONE, exit_filesystem_context,NULL,CTX_ALL},
	{"MetaServerList",ARG_LIST, get_metaserver_list,NULL,CTX_ALL},
	{"IoServerList",ARG_LIST, get_ioserver_list,NULL,CTX_ALL},
	{"UnexpectedRequests",ARG_INT, get_unexp_req,NULL,CTX_ALL},
	{"BucketTable",ARG_LIST, get_bucket_table,NULL,CTX_ALL},
	{"BucketTableList",ARG_INT, get_bucket_table_list,NULL,CTX_ALL},
	{"FS_Name",ARG_STR, get_filesystem_name,NULL,CTX_ALL},
	/*{"<MetaServerConfig>", ARG_NONE, start_meta_server_config, NULL, CTX_ALL },*/
	/*{"</MetaServerConfig>", ARG_NONE, end_meta_server_config, NULL, CTX_ALL },*/
	/*{"<IOServerConfig>", ARG_NONE, start_io_server_config, NULL, CTX_ALL },*/
	/*{"</IOServerConfig>", ARG_NONE, end_io_server_config, NULL, CTX_ALL },*/
	LAST_OPTION
};

static struct server_configuration_s *config_s;  /* For the configuration */

DOTCONF_CB(get_bucket_table_list)
{
	return NULL;
}
DOTCONF_CB(get_bucket_table)
{
	return NULL;
}
/***********************************/
/***********************************/
/* System Verified from here down! */
/***********************************/
/***********************************/
	
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

struct server_configuration_s *PINT_server_config(int argc, char **argv)
{
	configfile_t *configfile;

	config_s = (server_configuration_s *) malloc(sizeof(server_configuration_s));
	memset(config_s,0,sizeof(server_configuration_s));

	/* Lets support a max of PINT_SERVER_MAX_FILESYSTEMS */
	config_s->file_system_names = (char **) malloc(sizeof(char *)*PINT_SERVER_MAX_FILESYSTEMS);
	config_s->file_systems = (filesystem_configuration_s **) \
				malloc(sizeof(filesystem_configuration_s *)*PINT_SERVER_MAX_FILESYSTEMS);

	config_s->configuration_context = GLOBAL_CONFIG;

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

	/* TODO: Fix the default pointers for meta and io servers here ! */

#ifndef DEBUG_SERVER_CONFIG

	gossip_debug(SERVER_DEBUG,"\n\nConfig File Read: \nValues:\n=========\n");
	gossip_debug(SERVER_DEBUG,"Registering Server as: %s\n",config_s->host_id);
	if(config_s->bmi_protocols & ENABLE_BMI_TCP)
		gossip_debug(SERVER_DEBUG,"BMI_TCP Enabled\n");
	if(config_s->bmi_protocols & ENABLE_BMI_GM)
		gossip_debug(SERVER_DEBUG,"BMI_GM Enabled\n");
	gossip_debug(SERVER_DEBUG,"Total file_systems registered is %d\n",config_s->number_filesystems);
#if 0
	gossip_debug(SERVER_DEBUG,"Meta Servers Were: %s\n",config_s->meta_server_list);
	gossip_debug(SERVER_DEBUG,"Meta Server Count Was: %d\n",config_s->count_meta_servers);
	gossip_debug(SERVER_DEBUG,"IO Servers Were: %s\n",config_s->io_server_list);
	gossip_debug(SERVER_DEBUG,"IO Server Count Was: %d\n",config_s->count_io_servers);
#endif

#endif

	return config_s;
}
	
/*
 * Function: get_filesystem_name
 *
 * Params:   command_t *cmd,
 *           context_t *ctx
 *
 * Returns:  const char *
 *
 * Synopsis: 
 *           
 */

DOTCONF_CB(get_filesystem_name)
{
	char *name;

	if(config_s->configuration_context != FILESYSTEM_CONFIG)
	{
		gossip_lerr("FS_Name Tags can only be within filesystem tags");
		return NULL;
	}
	name = config_s->file_systems[config_s->number_filesystems]->file_system_name = 
		(char *) malloc(strlen(cmd->data.str)+1);
	memset(name,0,strlen(cmd->data.str)+1);
	strcpy(name,cmd->data.str);
	return NULL;
}

/*
 * Function: get_storage_space
 *
 * Params:   command_t *cmd,
 *           context_t *ctx
 *
 * Returns:  const char *
 *
 * Synopsis: 
 *           
 */
DOTCONF_CB(get_storage_space)
{
	char *name;
	if(config_s->configuration_context != GLOBAL_CONFIG)
	{
		gossip_lerr("Storage Space Tag can only be global");
		return NULL;
	}
	if(config_s->storage_path)
	{
		gossip_lerr("Storage Space Tag being overwritten... You prolly don't wanna be doing that.");
		free(config_s->storage_path);
	}
	name = config_s->storage_path = (char *) malloc(strlen(cmd->data.str)+1);
	memset(name,0,strlen(cmd->data.str)+1);
	strcpy(name,cmd->data.str);
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
	char *meta_server_location;

	/* Calculate String Size */
	for(i=0;i<cmd->arg_count;i++)
	{
		str_size+=strlen(cmd->data.list[i]) +1;
	}

	if(config_s->configuration_context == GLOBAL_CONFIG)
	{
		if(!config_s->default_meta_server_list)
		{
			gossip_debug(SERVER_DEBUG,"Setting Default Meta_servers\n");
			meta_server_location = config_s->default_meta_server_list = (char *) malloc(str_size);
		}
		else
		{
			gossip_lerr("WARNING: Overwriting default metaservers.  I am sure this is NOT\
what you want to do.\n");
			free(config_s->default_meta_server_list);
			meta_server_location = config_s->default_meta_server_list = (char *) malloc(str_size);
		}
		config_s->default_count_meta_servers = cmd->arg_count;
	}
	else
	{
		if(!config_s->file_systems[config_s->number_filesystems]->meta_server_list)
		{
			
			meta_server_location = 
				config_s->file_systems[config_s->number_filesystems]->meta_server_list =
				(char *) malloc(str_size);
		}
		else
		{
			gossip_lerr("WARNING: Overwriting default metaservers.  I am sure this is NOT\
what you want to do.\n");
			free(config_s->file_systems[config_s->number_filesystems]->meta_server_list);
			meta_server_location = 
				config_s->file_systems[config_s->number_filesystems]->meta_server_list =
				(char *) malloc(str_size);
		}
		config_s->file_systems[config_s->number_filesystems]->count_meta_servers = cmd->arg_count;
	}


	memset(meta_server_location,0,str_size);
	current_index=-1;
	for(i=0;i<cmd->arg_count;i++)
	{
		meta_server_location[current_index > 0 ? current_index : 0] = ';';

		memcpy(&meta_server_location[current_index+1],cmd->data.list[i],strlen(cmd->data.list[i])+1);

		/* find the null character and tell me the length */
		current_index = strlen(meta_server_location); 
	}

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
	char *io_server_location;

	/* Calculate String Size */
	for(i=0;i<cmd->arg_count;i++)
	{
		str_size+=strlen(cmd->data.list[i]) +1;
	}

	if(config_s->configuration_context == GLOBAL_CONFIG)
	{
		if(!config_s->default_io_server_list)
		{
			gossip_debug(SERVER_DEBUG,"Setting Default IO_servers\n");
			io_server_location = config_s->default_io_server_list = (char *) malloc(str_size);
		}
		else
		{
			gossip_lerr("WARNING: Overwriting default ioservers.  I am sure this is NOT\
what you want to do.\n");
			free(config_s->default_io_server_list);
			io_server_location = config_s->default_io_server_list = (char *) malloc(str_size);
		}
		config_s->default_count_io_servers = cmd->arg_count;
	}
	else
	{
		if(!config_s->file_systems[config_s->number_filesystems]->io_server_list)
		{
			
			io_server_location = 
				config_s->file_systems[config_s->number_filesystems]->io_server_list =
				(char *) malloc(str_size);
		}
		else
		{
			gossip_lerr("WARNING: Overwriting default ioservers.  I am sure this is NOT\
what you want to do.\n");
			free(config_s->file_systems[config_s->number_filesystems]->io_server_list);
			io_server_location = 
				config_s->file_systems[config_s->number_filesystems]->io_server_list =
				(char *) malloc(str_size);
		}
		config_s->file_systems[config_s->number_filesystems]->count_io_servers = cmd->arg_count;
	}


	memset(io_server_location,0,str_size);
	current_index=-1;
	for(i=0;i<cmd->arg_count;i++)
	{
		io_server_location[current_index > 0 ? current_index : 0] = ';';

		memcpy(&io_server_location[current_index+1],cmd->data.list[i],strlen(cmd->data.list[i])+1);

		/* find the null character and tell me the length */
		current_index = strlen(io_server_location); 
	}

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
 * Function: enable_gm
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

DOTCONF_CB(enable_gm)
{

	if(cmd->data.value == 1)
		config_s->bmi_protocols = config_s->bmi_protocols || ENABLE_BMI_GM;
	return NULL;

}

/*
 * Function: enable_tcp
 *
 * Params:   command_t *cmd,
 *           context_t *ctx
 *
 * Returns:  const char *
 *
 * Synopsis: Sets the flag to enable TCP communication for BMI
 *           
 *           
 */

DOTCONF_CB(enable_tcp)
{

	if(cmd->data.value == 1)
		config_s->bmi_protocols = config_s->bmi_protocols || ENABLE_BMI_TCP;
	return NULL;

}

/*
 * Function: enter_filesystem_context
 *
 * Params:   command_t *cmd,
 *           context_t *ctx
 *
 * Returns:  const char *
 *
 * Synopsis: Allocate a new struct for a filesystem.  
 *           Note we do not increment the number of
 *           file systems until we are done.
 *           
 *           
 */

DOTCONF_CB(enter_filesystem_context)
{

	if(config_s->configuration_context != GLOBAL_CONFIG)
	{
		gossip_lerr("Error in context.  Cannot have filesystem tag here\n");
		return NULL;
	}

	config_s->file_systems[config_s->number_filesystems] = (filesystem_configuration_s *)
			malloc(sizeof(filesystem_configuration_s));

	memset(config_s->file_systems[config_s->number_filesystems],0,sizeof(filesystem_configuration_s));
	
	config_s->configuration_context = FILESYSTEM_CONFIG;

	return NULL;

}

/*
 * Function: exit_filesystem_context
 *
 * Params:   command_t *cmd,
 *           context_t *ctx
 *
 * Returns:  const char *
 *
 * Synopsis: So the only tricky part here is how to set the metaserver stuff...
 *           TODO: When do we update the default pointers?
 *           
 *           
 */

DOTCONF_CB(exit_filesystem_context)
{

	if(config_s->configuration_context != FILESYSTEM_CONFIG)
	{
		gossip_lerr("Error in context.  Cannot have /filesystem tag here\n");
		return NULL;
	}
	if(!config_s->file_systems[config_s->number_filesystems]->file_system_name)
	{
		gossip_lerr("File system must have name %ld\n",cmd->configfile->line);
		return NULL;
	}
	if(!config_s->file_systems[config_s->number_filesystems]->meta_server_list)
	{
		gossip_lerr("Warning: Using default metaservers for filesystem %s\n",
				config_s->file_systems[config_s->number_filesystems]->file_system_name);
	}
	if(!config_s->file_systems[config_s->number_filesystems]->io_server_list)
	{
		gossip_lerr("Warning: Using default ioservers for filesystem %s\n",
				config_s->file_systems[config_s->number_filesystems]->file_system_name);
	}
	config_s->number_filesystems++;
	config_s->configuration_context = GLOBAL_CONFIG;
	return NULL;

}

/*
  vim:set ts=4:
  vim:set shiftwidth=4:
*/
