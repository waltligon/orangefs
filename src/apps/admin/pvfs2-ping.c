/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

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

#include "pvfs2.h"
#include "pvfs2-mgmt.h"

/* we are going to break some API boundaries here to get to the information
 * that we need
 */
#include "server-config.h"
#include "quicklist.h"
#include "quickhash.h"
#include "pint-sysint.h"
extern struct server_configuration_s g_server_config;
extern struct qhash_table *PINT_fsid_config_cache_table;

#define DEFAULT_TAB "/etc/pvfs2tab"

struct options
{
    char* fs_path_hack;
    char* fs_path_real;
};

static struct options* parse_args(int argc, char* argv[]);
static void usage(int argc, char** argv);
static void print_mntent(struct pvfs_mntent_s* entry);
static int print_config(struct server_configuration_s* conf,
    PVFS_fs_id fsid);
static int noop_all_servers(struct server_configuration_s* conf,
    PVFS_fs_id fsid);

int main(int argc, char **argv)
{
    int ret = -1;
    PVFS_fs_id cur_fs;
    pvfs_mntlist mnt = {0,NULL};
    struct options* user_opts = NULL;
    int mnt_index = -1;
    char pvfs_path[PVFS_NAME_MAX] = {0};
    PVFS_sysresp_init resp_init;
    int i;
    PVFS_credentials creds;
    PVFS_sysresp_lookup resp_lookup;

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if(!user_opts)
    {
	fprintf(stderr, "Error: failed to parse command line arguments.\n");
	usage(argc, argv);
	return(-1);
    }

    printf("\n(1) Searching for %s in %s...\n", user_opts->fs_path_real,
	DEFAULT_TAB);

    /* look at pvfstab */
    ret = PVFS_util_parse_pvfstab(DEFAULT_TAB, &mnt);
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_parse_pvfstab", ret);
        fprintf(stderr, "Failure: could not parse pvfstab %s.\n", DEFAULT_TAB);
        return(-1);
    }

    /* see if the destination resides on any of the file systems
     * listed in the pvfstab; find the pvfs fs relative path
     */
    for(i=0; i<mnt.nr_entry; i++)
    {
	ret = PVFS_util_remove_dir_prefix(user_opts->fs_path_hack,
	    mnt.ptab_p[i].local_mnt_dir, pvfs_path, PVFS_NAME_MAX);
	if(ret == 0)
	{
	    mnt_index = i;
	    break;
	}
    }

    if(mnt_index == -1)
    {
	fprintf(stderr, "Failure: could not find filesystem for %s in pvfstab %s\n", 
	    user_opts->fs_path_real, DEFAULT_TAB);
	return(-1);
    }

    /* initialize only one file system, regardless of how many are present
     * in the pvfs2tab file 
     */
    mnt.ptab_p = &mnt.ptab_p[mnt_index];
    mnt.nr_entry = 1;
    mnt_index = 0;

    print_mntent(mnt.ptab_p);

    creds.uid = getuid();
    creds.gid = getgid();

    printf("\n(2) Initializing system interface and retrieving configuration from server...\n");
    memset(&resp_init, 0, sizeof(resp_init));
    ret = PVFS_sys_initialize(mnt, 0, &resp_init);
    if(ret < 0)
    {
	PVFS_perror("PVFS_sys_initialize", ret);
	fprintf(stderr, "Failure: could not initialize system interface.\n");
	return(-1);
    }

    cur_fs = resp_init.fsid_list[mnt_index];

    /* dump some key parts of the config file */
    ret = print_config(&g_server_config, cur_fs);
    if(ret < 0)
    {
	PVFS_perror("print_config", ret);
	fprintf(stderr, "Failure: could not print configuration.\n");
	return(-1);
    }

    printf("\n(3) Verifying that all servers are responding...\n");

    /* send noop to everyone */
    ret = noop_all_servers(&g_server_config, cur_fs);
    if(ret < 0)
    {
	PVFS_perror("noop_all_servers", ret);
	fprintf(stderr, "Failure: could not communicate with one of the servers.\n");
	return(-1);
    }

    printf("\n(4) Verifying that fsid %ld is acceptable to all servers...\n",
	(long)cur_fs);

    /* check that the fsid exists on all of the servers */
    /* TODO: we need a way to get information out about which server fails
     * in error cases here 
     */
    ret = PVFS_mgmt_setparam_all(cur_fs, creds, PVFS_SERV_PARAM_FSID_CHECK,
	(int64_t)cur_fs);
    if(ret < 0)
    {
	PVFS_perror("PVFS_mgmt_setparam_all", ret);
	fprintf(stderr, "Failure: not all servers accepted fsid %ld\n", 
	    (long)cur_fs);
	fprintf(stderr, "TODO: need a way to tell which servers couldn't find the fs_id...\n");
	return(-1);
    }
    printf("\n   Ok; all servers understand fs_id %ld\n", (long)cur_fs);

    printf("\n(5) Verifying that root handle is owned by one server...\n");    

    ret = PVFS_sys_lookup(cur_fs, "/", creds, &resp_lookup);
    if(ret != 0)
    {
	PVFS_perror("PVFS_sys_lookup", ret);
	fprintf(stderr, "Failure: could not lookup root handle.\n");
	return(-1);
    }
    printf("\n   Root handle: 0x%08Lx\n", resp_lookup.pinode_refn.handle);

    /* check that only one server controls root handle */
    /* TODO: we need a way to get information out about which server failed
     * in error cases here 
     */
    ret = PVFS_mgmt_setparam_all(cur_fs, creds, PVFS_SERV_PARAM_ROOT_CHECK,
	(int64_t)resp_lookup.pinode_refn.handle);

    /* check for understood error values */
    if(ret == -PVFS_ENOENT)
    {
	fprintf(stderr, "Failure: no servers claimed ownership of root handle.\n");
	return(-1);
    }
    if(ret == -PVFS_EALREADY)
    {
	fprintf(stderr, "Failure: more than one server appears to own root handle.\n");
	return(-1);
    }
    if(ret < 0)
    {
	PVFS_perror("PVFS_mgmt_setparam_all", ret);
	fprintf(stderr, "Failure: failed to check root handle.\n");
	return(-1);
    }

    /* if we hit this point, then everything is ok */
    printf("   Ok; root handle is owned by exactly one server.\n");
    printf("\n");

    PVFS_sys_finalize();

    printf("=============================================================\n");

    printf("\nThe PVFS filesystem at %s appears to be correctly configured.\n\n",
	user_opts->fs_path_real);
	
    return(ret);
}


/* noop_all_servers()
 *
 * sends a noop to all servers listed in the config file 
 *
 * returns -PVFS_error on failure, 0 on success
 */
static int noop_all_servers(struct server_configuration_s* conf,
    PVFS_fs_id fsid)
{
    struct qlist_head* hash_link = NULL;
    char* server_bmi_str = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;
    struct llist* tmp_server = NULL;
    struct host_handle_mapping_s *cur_mapping = NULL;
    PVFS_credentials creds;
    int ret = -1;

    creds.uid = getuid();
    creds.gid = getgid();

    hash_link = qhash_search(PINT_fsid_config_cache_table, &(fsid));
    if(!hash_link)
    {
	fprintf(stderr, "Failure: could not find fsid %ld in configuration.\n",
	    (long)fsid);
	return(-PVFS_EINVAL);
    }

    cur_config_cache =
	qlist_entry(hash_link, struct config_fs_cache_s,
		    hash_link);
    tmp_server = cur_config_cache->fs->meta_handle_ranges;
    if(!tmp_server)
    {
	fprintf(stderr, "Failure: could not find meta servers in configuration.\n");
	return(-PVFS_EINVAL);
    }

    printf("\n   meta servers (duplicates are normal):\n");
    while((cur_mapping = llist_head(tmp_server)))
    {
	tmp_server = llist_next(tmp_server);

	server_bmi_str = PINT_config_get_host_addr_ptr(
	    conf,cur_mapping->alias_mapping->host_alias);
	printf("   %s ", server_bmi_str);
	ret = PVFS_mgmt_noop(creds, server_bmi_str);
	if(ret == 0)
	{
	    printf("Ok\n");
	}
	else
	{
	    printf("Failure!\n");
	    return(ret);
	}
    }

    tmp_server = cur_config_cache->fs->data_handle_ranges;
    if(!tmp_server)
    {
	fprintf(stderr, "Failure: could not find data servers in configuration.\n");
	return(-PVFS_EINVAL);
    }

    printf("\n   data servers (duplicates are normal):\n");
    while((cur_mapping = llist_head(tmp_server)))
    {
	tmp_server = llist_next(tmp_server);

	server_bmi_str = PINT_config_get_host_addr_ptr(
	    conf,cur_mapping->alias_mapping->host_alias);
	printf("   %s ", server_bmi_str);
	ret = PVFS_mgmt_noop(creds, server_bmi_str);
	if(ret == 0)
	{
	    printf("Ok\n");
	}
	else
	{
	    printf("Failure!\n");
	    return(ret);
	}
    }

    return(0);
}


/* print_config()
 *
 * prints out config file information
 *
 * returns -PVFS_error on failure, 0 on success
 */
static int print_config(struct server_configuration_s* conf,
    PVFS_fs_id fsid)
{
    struct qlist_head* hash_link = NULL;
    char* server_bmi_str = NULL;
    struct config_fs_cache_s *cur_config_cache = NULL;
    struct llist* tmp_server = NULL;
    struct host_handle_mapping_s *cur_mapping = NULL;

    hash_link = qhash_search(PINT_fsid_config_cache_table, &(fsid));
    if(!hash_link)
    {
	fprintf(stderr, "Failure: could not find fsid %ld in configuration.\n",
	    (long)fsid);
	return(-PVFS_EINVAL);
    }

    cur_config_cache =
	qlist_entry(hash_link, struct config_fs_cache_s,
		    hash_link);
    tmp_server = cur_config_cache->fs->meta_handle_ranges;
    if(!tmp_server)
    {
	fprintf(stderr, "Failure: could not find meta servers in configuration.\n");
	return(-PVFS_EINVAL);
    }

    printf("\n   meta servers (duplicates are normal):\n");
    while((cur_mapping = llist_head(tmp_server)))
    {
	tmp_server = llist_next(tmp_server);

	server_bmi_str = PINT_config_get_host_addr_ptr(
	    conf,cur_mapping->alias_mapping->host_alias);
	printf("   %s\n", server_bmi_str);
    }

    tmp_server = cur_config_cache->fs->data_handle_ranges;
    if(!tmp_server)
    {
	fprintf(stderr, "Failure: could not find data servers in configuration.\n");
	return(-PVFS_EINVAL);
    }

    printf("\n   data servers (duplicates are normal):\n");
    while((cur_mapping = llist_head(tmp_server)))
    {
	tmp_server = llist_next(tmp_server);

	server_bmi_str = PINT_config_get_host_addr_ptr(
	    conf,cur_mapping->alias_mapping->host_alias);
	printf("   %s\n", server_bmi_str);
    }

    return(0);
}

/* print_mntent()
 *
 * prints out pvfstab information 
 *
 * no return value
 */
static void print_mntent(struct pvfs_mntent_s* entry)
{
    printf("\n   Initial server: %s\n", entry->meta_addr);
    printf("   Storage name: %s\n", entry->service_name);
    printf("   Local mount point: %s\n", entry->local_mnt_dir);
    return;
}

/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct options* parse_args(int argc, char* argv[])
{
    /* getopt stuff */
    extern char* optarg;
    extern int optind, opterr, optopt;
    char flags[] = "";
    char one_opt = ' ';

    struct options* tmp_opts = NULL;
    int ret = -1;

    /* create storage for the command line options */
    tmp_opts = (struct options*)malloc(sizeof(struct options));
    if(!tmp_opts){
	return(NULL);
    }
    memset(tmp_opts, 0, sizeof(struct options));

    /* look at command line arguments */
    while((one_opt = getopt(argc, argv, flags)) != EOF){
	switch(one_opt){
	    case('?'):
		usage(argc, argv);
		exit(EXIT_FAILURE);
	}
    }

    if(optind != (argc - 1))
    {
	usage(argc, argv);
	exit(EXIT_FAILURE);
    }

    /* get the path of the file system, this one has a trailing slash
     * tacked on, see comment below for why 
     */
    tmp_opts->fs_path_hack = (char*)malloc(strlen(argv[argc-1]) + 2);
    if(!tmp_opts->fs_path_hack)
    {
	free(tmp_opts);
	return(NULL);
    }
    ret = sscanf(argv[argc-1], "%s", tmp_opts->fs_path_hack);
    if(ret < 1)
    {
	free(tmp_opts->fs_path_hack);
	free(tmp_opts);
	return(NULL);
    }
    /* TODO: this is a hack... fix later.  The remove_dir_prefix()
     * function expects some trailing segments or at least a slash
     * off of the mount point
     */
    strcat(tmp_opts->fs_path_hack, "/");
    
    /* also preserve the real path, to use in print statements elsewhre */
    tmp_opts->fs_path_real = (char*)malloc(strlen(argv[argc-1]) + 2);
    if(!tmp_opts->fs_path_real)
    {
	free(tmp_opts->fs_path_hack);
	free(tmp_opts);
	return(NULL);
    }
    ret = sscanf(argv[argc-1], "%s", tmp_opts->fs_path_real);
    if(ret < 1)
    {
	free(tmp_opts->fs_path_hack);
	free(tmp_opts->fs_path_real);
	free(tmp_opts);
	return(NULL);
    }
 
    return(tmp_opts);
}


static void usage(int argc, char** argv)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage  : %s file_system_path\n",
	argv[0]);
    fprintf(stderr, "Example: %s /mnt/pvfs2\n",
	argv[0]);

    fprintf(stderr, "\nNote: this utility reads /etc/pvfs2tab for file system configuration.\n");
    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

