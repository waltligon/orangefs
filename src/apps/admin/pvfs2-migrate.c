#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <getopt.h>
  
#include "pvfs2.h"
#include "pvfs2-mgmt.h"
#include "str-utils.h"
#include "pvfs2-internal.h"
#include "pint-sysint-utils.h"
      
#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

/*
struct options
{
	char *serv_alias;
	char *in_file;
};	
*/
//static struct options* parse_args(int argc, char* argv[]);
//int pvfs2_migrate(char *destfile);


int main(int argc, char **argv)
{
	int ret = -1;
	char pvfs_path[PVFS_NAME_MAX] = {0};
	char str_buf[PVFS_NAME_MAX] = {0};
	PVFS_fs_id fs_id;
	PVFS_credentials credentials;
	PVFS_object_ref parent_ref;
	PVFS_sysresp_lookup resp_lookup;
	char *filename;
	char *serv_alias;
	
	if( argc != 3)
	{
			fprintf(stderr,"Usage: %s filename server alais\n", argv[0]);
			return ret;
	}		


	filename = argv[1];
	serv_alias = argv[2];

	/*
	struct options* user_opts = NULL;
	
	user_opts = parse_args(argc, argv);
	
	if(!user_opts)
	{
		fprintf(stderr,"Error: failed to parse "
						"command line arguments.\n");
		return -1;
	}
	*/					
	
	/************************/
	
	ret = PVFS_util_init_defaults();
	
	if(ret < 0)
	{
		PVFS_perror("PVFS_util_init_defaults", ret);
		return(-1);
	}
	
	/*
	ret = pvfs2_migrate(user_opts->in_file);

	if(ret != 0)
	{
			PVFS_perror("pvfs2_migrate", ret);
	}	
	*/
		
	
	ret = PVFS_util_resolve(filename, &fs_id, pvfs_path, PVFS_NAME_MAX);
	if(ret < 0)
	{
			PVFS_perror("PVFS_util_resolve", ret);
			return(-1);
	}
	
	
	
	PVFS_util_gen_credentials(&credentials);	
	


	if (strcmp(pvfs_path,"/") == 0)
    {
    	memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
        ret = PVFS_sys_lookup(fs_id, pvfs_path,
                              &credentials, &resp_lookup,
                              PVFS2_LOOKUP_LINK_FOLLOW, NULL);
        if (ret < 0)
        {
   	       PVFS_perror("PVFS_sys_lookup", ret);
           return -1;
        }
        parent_ref.handle = resp_lookup.ref.handle;
        parent_ref.fs_id = resp_lookup.ref.fs_id;
    }
    else
    {
         /* get the absolute path on the pvfs2 file system */
         if (PINT_remove_base_dir(pvfs_path,str_buf,PVFS_NAME_MAX))
         {
           if (pvfs_path[0] != '/')
           {
             fprintf(stderr, "Error: poorly formatted path.\n");
           }
       		fprintf(stderr, "Error: cannot retrieve entry name for "
               "creation on %s\n",pvfs_path);
      		return -1;
    	  }
	
		ret = PINT_lookup_parent(pvfs_path, fs_id, &credentials, &parent_ref.handle);
	
		if(ret < 0)
		{
			PVFS_perror("PINT_loopup_parent", ret);
			return -1;
		}
		else
		{
			parent_ref.fs_id = fs_id;
		}
    }
    memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
    	
	ret = PVFS_sys_ref_lookup(parent_ref.fs_id, str_buf, parent_ref, &credentials, 
						&resp_lookup, PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);
								
	if( ret != 0)
	{
		fprintf(stderr,"Target '%s' does not exist!\n", str_buf);
		return -1;
	}
	
	ret = PVFS_mgmt_migrate(resp_lookup.ref, &credentials, serv_alias, NULL);
	
	if(ret < 0)
	{
			PVFS_perror("PVFS_mgmt_migrate failed with errcode", ret);
			return -1;
	}				
	
	ret = PVFS_sys_finalize();
	if( ret < 0)
	{
		printf("Finalizing sysint failed with errcode = %d\n", ret);
		return -1;
	}		
	
	return 0;
}			

/*
int pvfs2_migrate(char* destfile){
	
	int ret = -1;
	char pvfs_path[PVFS_NAME_MAX] = {0};
	
	PVFS_fs_id fs_id;
	PVFS_credentials credentials;
	PVFS_object_ref ref;
	
	
	ret = PVFS_util_resolve(destfile, &fs_id, pvfs_path, PVFS_NAME_MAX);
	if(ret < 0)
	{
			PVFS_perror("PVFS_util_resolve", ret);
			return(-1);
	}
	
	PVFS_util_gen_credentials(&credentials);	
	
	
}
*/

/*
static struct options* parse_agrs(int argc, char* argv[])
{
	struct options* temp_opts = NULL:
	
	temp_opts = (struct options*)malloc(sizeof(struct options));
	if(!temp_opt)
	{
		return NULL;
	}
	
	
	memset(temp_pts, 0 ,sizeof(strct options));
	
	temp_opts->serv_alias = NULL;
	temp_opts->in_file = NULL;
}	
*/	
	
		
		
	
