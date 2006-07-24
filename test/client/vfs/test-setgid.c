/*
 * Copyright (c) Acxiom Corporation, 2005 
 *
 * See COPYING in top-level directory
 */

/* This program tests the SETGID bit functionality. 
 * 
 * note: To run this program as a user, the user must be setup in the sudoers 
 *       file, and must be able to sudo withOUT a password. 
 *       The directories are created with 777 permissions, for ease of creating
 *       and removing directories regardless of user permissions.
 *
 * see usage() and test-common.c for details 
 */
 
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <assert.h>
#include <time.h>
#include <limits.h>
#include <fcntl.h>
#include "test-common.h"

#include <pvfs2.h>

static char* user_names[]  = {"nobody", "daemon", "mail", "news"};
static char* group_names[] = {"nobody", "daemon", "mail", "news"};

/* parsed command line options */
struct setgid_options
{
    char * userName;
    uid_t  user_id;
    char * groupName;
    gid_t  group_id;
};

/* Function Prototypes */
static void usage(void);
static int parse_args(
    int argc, 
    char** argv, 
    struct setgid_options* opts,
    struct common_options* commonOpts);
static int setup_setgid(
    const char * directory, 
    struct setgid_options* opts,
    struct common_options* commonOpts);
static int destroy_setgid(
    const char * directory, 
    struct common_options* commonOpts);
static int determine_group(char** groupName, gid_t* group_id);
static int determine_user(char** userName, uid_t* user_id);
static void cleanup(
    const char* pvfs2_tab_file, 
    struct common_options* commonOpts);

int main(int argc, char **argv)
{
    int    ret = 0;
    char   fileName[PATH_MAX]      = "";
    char   testDir[PATH_MAX]       = ""; /* Directory where all the test files will reside */
    char   PVFSPath[PVFS_NAME_MAX] = ""; /* fs relative path */
    char   pvfs2_tab_file[PATH_MAX]= ""; /* used to store pvfs2 tab file */
    struct file_ref stFileRef;
    struct setgid_options opts;
    struct common_options commonOpts;
    struct stat stStats;
    PVFS_fs_id cur_fs;
    
    memset(&stFileRef, 0, sizeof(stFileRef));
    memset(&stStats,   0, sizeof(stStats));
    memset(&cur_fs,    0, sizeof(cur_fs));
    memset(&opts,      0, sizeof(opts));
    memset(&commonOpts, 0, sizeof(commonOpts));
    
    if(geteuid() == 0)
    {
        print_error("ERROR: test must be run as a non-root user.\n");
        exit(1);
    }
    
    ret = parse_args(argc, argv, &opts, &commonOpts);
    if(ret < 0)
    {
        exit(1);
    }

    /* Get the user for directory permissions */
    ret = determine_user(&(opts.userName), &opts.user_id);
    if(ret != 0) /* On failure, get the uid of the running process */
    {
        struct passwd * user = NULL;
        
        user = getpwuid(geteuid());
        assert(user);
        opts.userName = (char *) calloc(1, strlen(user->pw_name)+1);
        assert(opts.userName);
        strcpy(opts.userName, user->pw_name);
        opts.user_id = user->pw_uid;
    }

    /* Get the group for directory permissions */
    ret = determine_group(&(opts.groupName), &opts.group_id);
    if(ret != 0) /* On failure, get the gid of the running process */
    {
        struct group * group = NULL;
        
        group = getgrgid(geteuid());
        assert(group);
        opts.groupName = (char *) calloc(1, strlen(group->gr_name)+1);
        assert(opts.groupName);
        strcpy(opts.groupName, group->gr_name);
        opts.group_id = group->gr_gid;
    }
    
    /* Make sure group doesn't match user... We need to have a different group
     * to make sure test works
     */
    if(opts.group_id == opts.user_id)
    {
        print_error("\tERROR: Group [%s]:[%d] must be different than user [%s]:[%d]\n",
                    opts.groupName,
                    opts.group_id,
                    opts.userName,
                    opts.user_id);
        exit(1);
    }
    
    if(commonOpts.printResults)
    {
        printf("-------------------- Starting setgid tests --------------------\n");
    }
    
    if(commonOpts.verbose)
    {
        printf("\t--username       = [%s]\n" 
               "\t--groupname      = [%s]\n",
               (opts.userName==NULL)?"NULL":opts.userName,
               (opts.groupName==NULL)?"NULL":opts.groupName);
    }
    
    /* Setup the PVFS2TAB file first thing */
    if(commonOpts.hostname != NULL)
    {
        ret =  create_pvfs2tab_file(pvfs2_tab_file, 
                                    sizeof(pvfs2_tab_file),
                                    (const int) commonOpts.port,
                                    (const char *)commonOpts.networkProto,
                                    (const char *)commonOpts.hostname,
                                    (const char *)commonOpts.fsname,
                                    (const char *)commonOpts.directory);
                                    
        if(ret != TEST_COMMON_SUCCESS)
        {
            print_error("\tERROR: Unable to create pvfs2_tab_file\n");
            cleanup(pvfs2_tab_file, &commonOpts);
            exit(1);
        }
    }

    ret = initialize(commonOpts.use_pvfs2_lib, commonOpts.verbose);
    
    if(ret < 0)
    {
        print_error("\tERROR: initialize failed\n");
        cleanup(pvfs2_tab_file, &commonOpts);
        exit(1);
    }
    
    if(commonOpts.use_pvfs2_lib)
    {
        /* Check to see if this is a PVFS2 filesytem */
        ret = is_pvfs2(commonOpts.directory, 
                       &cur_fs, 
                       PVFSPath,
                       sizeof(PVFSPath),
                       commonOpts.use_pvfs2_lib, 
                       commonOpts.verbose);
        
        if(ret != TEST_COMMON_SUCCESS)
        {
            print_error("[%s] does NOT reside on a PVFS2 filesystem.\n", 
                        commonOpts.directory);
            cleanup(pvfs2_tab_file, &commonOpts);
            exit(1);
        }
    }
    
    /* Setup the main directory which will have the setgid bit set */
    sprintf(testDir, "%s/%s-%s", commonOpts.directory, opts.userName, "setgid-test");
    ret = setup_setgid(testDir, &opts, &commonOpts);
    if(ret == -1)
    {
        printf("\tUnable to setup SETGID\n");
        cleanup(pvfs2_tab_file, &commonOpts);
        exit(1);  
    }      
    
    /* ---------- BEGIN TEST CASE  ---------- 
     * Check to see if we determine if setgid bit is set on the directory 
     */
    ret = stat_file(testDir, 
                    &stStats, 
                    0, /* Don't follow link */
                    commonOpts.use_pvfs2_lib, 
                    commonOpts.verbose);
    
    if(ret != TEST_COMMON_SUCCESS)
    {
        print_error("\tFAILED. Couldn't execute stat on [%s]\n", testDir);
    }
    else
    {
        /* Check to make sure we see the setgid bit */
        if(stStats.st_mode & S_ISGID)
        {
            if(commonOpts.printResults)
            {
                printf("\tPASSED. setgid bit IS set for [%s]\n", testDir); 
            }
        }
        else
        {
            print_error("\tFAILED. setgid bit not set on [%s]\n", testDir);
        }
    }
    
    /* ---------- BEGIN TEST CASE  ---------- 
     * Check to see if we can create a file an it inherit the setgid bit
     */
    sprintf(fileName, "%s/file_to_inherit_setgid", testDir);
    
    ret = open_file(fileName, 
                    O_RDONLY | O_CREAT, 
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, 
                    commonOpts.use_pvfs2_lib,
                    commonOpts.verbose,
                    1,               /* Follow Link */
                    &stFileRef);
    
    if(ret != TEST_COMMON_SUCCESS)
    {
      print_error("\tUnable to open [%s]\n"
                  "\t\tError Number = [%d]\n"
                  "\t\tError Desc   = [%s]\n",
                  fileName,
                  errno,
                  strerror(errno));
    }
    
    /* Check to see if the group ownership was inherited by this file */
    ret = stat_file(fileName, 
                    &stStats,
                    0, /* Don't follow link */ 
                    commonOpts.use_pvfs2_lib, 
                    commonOpts.verbose);
    
    if(ret != TEST_COMMON_SUCCESS)
    {
        print_error("\tFAILED. Couldn't execute stat on [%s]\n", fileName);
    }
    else
    {
        /* Check to make sure the group was inherited */
        if(stStats.st_gid == opts.group_id)
        {
            if(commonOpts.printResults)
            {
                printf("\tPASSED. group [%s] inherited by [%s]\n", opts.groupName, fileName); 
            }
        }
        else
        {
            print_error("\tFAILED. group [%s] NOT inherited by [%s]\n", opts.groupName, fileName);
        }
    }
 
    ret = remove_file(fileName, commonOpts.use_pvfs2_lib, commonOpts.verbose); 
    
    /* ---------- BEGIN TEST CASE  ---------- 
     * Check to see if we can create a directory and it inherit the setgid bit
     */
    sprintf(fileName, "%s/directory_to_inherit_setgid", testDir);
    ret = create_directory(fileName, 
                           S_IRWXO |  S_IRWXG | S_IRWXU, /* 0777 */
                           commonOpts.use_pvfs2_lib,
                           commonOpts.verbose);
    
    /* Check to see if the group ownership was inherited by this directory */
    ret = stat_file(fileName, 
                    &stStats, 
                    0, /* Don't follow link */
                    commonOpts.use_pvfs2_lib, 
                    commonOpts.verbose);
     
    if(ret != TEST_COMMON_SUCCESS)
    {
        print_error("\tFAILED. Couldn't execute stat on [%s]\n", fileName);
    }
    else
    {
        /* Check to make sure we see the setgid bit */
        if(stStats.st_mode & S_ISGID)
        {
            if(commonOpts.printResults)
            {
                printf("\tPASSED. setgid bit IS set for [%s]\n", fileName); 
            }
        }
        else
        {
            print_error("\tFAILED. setgid bit not set on [%s]\n", fileName);
        }
    }
    
    ret = remove_directory(fileName, commonOpts.use_pvfs2_lib, commonOpts.verbose); 
    
    /* Remove any leftover test data */
    ret = destroy_setgid(testDir, &commonOpts);
    
    if(ret != 0)
    {
        print_error("\tSETGID destroy failed\n");
        cleanup(pvfs2_tab_file, &commonOpts);
        exit(1);
    }
    
    ret = finalize(commonOpts.use_pvfs2_lib);
    if(ret < 0)
    {
        print_error("\tERROR: finalize failed\n");
        cleanup(pvfs2_tab_file, &commonOpts);
        exit(1);
    }
    
    cleanup(pvfs2_tab_file, &commonOpts);

    if(commonOpts.printResults)
    {
        printf("-------------------- Ending   setgid tests --------------------\n");
    }
    
    return 0;  
}

/**
 * \retval 0 Success
 * \retval !0 Failure 
 */
int setup_setgid(
    const char * directory, /**< main directory to test in */
    struct setgid_options* opts, /**< setgid_options */
    struct common_options* commonOpts) /**< common_options */
{
    int  ret  = 0;
    int  error_code = 0;
    int  mode = S_IRWXO |  S_IRWXG | S_IRWXU; /* 0777 */
   
    /* Create the directory */
    ret = create_directory(directory, 
                           mode, 
                           commonOpts->use_pvfs2_lib,
                           commonOpts->verbose);
   
    if(ret != TEST_COMMON_SUCCESS)
    {
        printf("\tUnable to create directory [%s]\n", directory);
    }
    else
    {
        /* Set the setgid bit on the directory */
        ret = change_mode(directory, 
                          mode | S_ISGID, 
                          &error_code,
                          commonOpts->use_pvfs2_lib, 
                          commonOpts->verbose);
      
        if(ret != TEST_COMMON_SUCCESS)
        {
            printf("\tUnable to change mode on [%s]\n", directory);
        }
        else
        {
            /* Set the setgid bit on the directory */
            ret = change_owner(directory, 
                               opts->userName,
                               opts->user_id,
                               opts->groupName,
                               opts->group_id,
                               commonOpts->use_pvfs2_lib, 
                               commonOpts->verbose);
            
            if(ret != TEST_COMMON_SUCCESS)
            {
                printf("\tUnable to change ownership on [%s]\n", directory);
            }
        
        }
   }

   return ret;
}

/**
 * \retval 0 Success
 * \retval !0 Failure 
 */
int destroy_setgid(const char * directory, struct common_options* opts)
{
    int  ret = 0;
   
    ret = remove_directory(directory, opts->use_pvfs2_lib, opts->verbose);
   
    if(ret != TEST_COMMON_SUCCESS)
    {
        printf("\tUnable to remove directory [%s]\n", directory);
    }

    return ret;
}

void cleanup(const char * pvfs2_tab_file, struct common_options* opts)
{
    /* Setup the PVFS2TAB file first thing */
    if(opts->hostname != NULL)
    {
        destroy_pvfs2tab_file(pvfs2_tab_file);
    }
}

/**
 * \retval 0 Success
 * \retval !0 Failure 
 */
int parse_args(
    int argc, 
    char** argv, 
    struct setgid_options* opts,
    struct common_options* commonOpts)
{
    int ret          = 0;
    int option_index = 0;
    char *cur_option = NULL;

    static struct option long_opts[] = 
    {
        {"group",1,0,0},
        {0,0,0,0}
    };

    /* Since we accept more options than the generic parse_common_args function
     * looks for, we need to set this opterr variable to zero so it doesn't 
     * complain about the extra options
     */
    opterr=0;
    
    /* Get the common arguments first */
    ret = parse_common_args(argc, argv, commonOpts);

    if(ret != TEST_COMMON_SUCCESS)
    {
        usage();
        return(-1);
    }

    memset(opts, 0, sizeof(struct setgid_options));
    optind = 0;
    while((ret = getopt_long_only(argc, argv, "", long_opts, &option_index)) != -1)
    {
        switch (ret)
        {
            case 0:
                 cur_option = (char*)long_opts[option_index].name;

                 if(strcmp("group", cur_option) == 0)
                 {
                     opts->groupName = (char*) malloc(strlen(optarg)+1);
                     assert(opts->groupName);
                     strcpy(opts->groupName, optarg);
                 }

                 break;
        }
    }

    return(0);
}

/** 
 * Returns a valid system user that can be used to test SETGID bit. Defaults
 * to the current running process's effective user_id
 * \returns 0 on success, non-zero on error 
 */
int determine_user(
    char** userName, /**< user name */
    uid_t* user_id) /**< Placeholder for user id */
{
    int num_avail_users  = 0,
        counter = 0;
    struct passwd *user = NULL;

    if(*userName == NULL)
    {
        /* First get the userid running script */
        user = getpwuid( geteuid() );
        if(user == NULL) /* If we failed to get userid of user running script */
        {
            num_avail_users = sizeof(user_names)/sizeof(char *); /* How many users do we check for */
            for(counter=0; counter < num_avail_users; counter++)
            {
                user = getpwnam(user_names[counter]);
                if(user != NULL)
                {
                    break;
                }
            }
        }

        if(user == NULL)
        {
            print_error("\tUnable to find a local user account\n"
                        "\t\tError Number = [%d]\n"
                        "\t\tError Desc   = [%s]\n",
                        errno,
                        strerror(errno));
            return(-1);
        }

        *userName = (char *) calloc(1, strlen(user->pw_name)+1);
        assert(userName);
        strcpy(*userName, user->pw_name);
    }
    else
    {
        user = getpwnam(*userName);
        if(user == NULL)
        {
            print_error("\tUnable to get account info for user:[%s]\n"
                        "\t\tError Number = [%d]\n"
                        "\t\tError Desc   = [%s]\n",
                        *userName,
                        errno,
                        strerror(errno));
            return(-1);
        }
    }
    
    *user_id = user->pw_uid;
    return(0);
}

/** 
 * Returns a valid system group that can be used if no groupName is specified
 * via the command line arguments
 * \returns 0 on success, non-zero on error 
 */
int determine_group(
    char** groupName, /**< user name */
    gid_t* group_id) /**< Placeholder for group id*/
{
    int num_avail_groups = 0,
        counter = 0;
    struct group *group = NULL;

    if(*groupName == NULL)
    {
        num_avail_groups = sizeof(group_names)/sizeof(char *); /* How many groups do we check for */
        for(counter=0; counter < num_avail_groups; counter++)
        {
            group = getgrnam(group_names[counter]);
            if(group != NULL)
            {
                break;
            }
        }

        if(group == NULL)
        {
            print_error("\tUnable to find a local groupd account\n"
                        "\t\tError Number = [%d]\n"
                        "\t\tError Desc   = [%s]\n",
                        errno,
                        strerror(errno));
            return(-1);
        }

        *groupName = (char *) calloc(1, strlen(group->gr_name)+1);
        assert(groupName);
        strcpy(*groupName, group->gr_name);
    }
    else
    {
        group = getgrnam(*groupName);
        if(group == NULL)
        {
            print_error("\tUnable to get account info for group:[%s]\n"
                        "\t\tError Number = [%d]\n"
                        "\t\tError Desc   = [%s]\n",
                        *groupName,
                        errno,
                        strerror(errno));
            return(-1);
        }
    }
    
    *group_id = group->gr_gid;
    return(0);
}

void usage()
{
    fprintf(stderr, "other optional arguments:\n"
        "  --group         : Set this group ownership of the directory with the setgid bit\n");
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

