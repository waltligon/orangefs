/*
 * Copyright (c) Acxiom Corporation, 2005 
 *
 * See COPYING in top-level directory
 */

/* This program tests symlink permission functionality (are symlinks created
 * with proper permissions, and can you change the permissions?). 
 *
 * see usage() for details 
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

static int setup_symlink_perms(
    const char * file_name, 
    const char * link_name, 
    struct common_options* commonOpts);
static int destroy_symlink_perms(
    const char * file_name, 
    const char * symlink_name, 
    struct common_options* commonOpts);
static void cleanup(
    const char * pvfs2_tab_file, 
    struct common_options* commonOpts);

int main(int argc, char **argv)
{
    int    ret = 0;
    int    error_code = 0;
    char   fileName[PATH_MAX]      = "";
    char   linkName[PATH_MAX]      = "";
    char   PVFSPath[PVFS_NAME_MAX] = ""; /* fs relative path */
    char   pvfs2_tab_file[PATH_MAX]= ""; /* used to store pvfs2 tab file */
    struct file_ref stFileRef;
    struct common_options commonOpts;
    struct stat stStats;
    PVFS_fs_id cur_fs;
    
    memset(&stFileRef, 0, sizeof(stFileRef));
    memset(&stStats,   0, sizeof(stStats));
    memset(&cur_fs,    0, sizeof(cur_fs));
    memset(&commonOpts,0, sizeof(commonOpts));

    if(geteuid() == 0)
    {
        print_error("ERROR: test must be run as a non-root user.\n");
        exit(1);
    }
    
    ret = parse_common_args(argc, argv, &commonOpts);
    if(ret < 0)
    {
        exit(1);
    }
        
    if(commonOpts.exePath != NULL)
    {
        strcat(commonOpts.exePath, "/"); /* Make sure it ends with "/" */
        ret = set_util_path(commonOpts.exePath);
        
        if(ret != TEST_COMMON_SUCCESS)
        {
            print_error("Unable to set executable path location to [%s].\n", 
                        commonOpts.exePath);
            exit(1);
        }
    }
    
    if(commonOpts.printResults)
    {
        printf("-------------------- Starting symlink-perm tests --------------------\n");
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
    
    /* Setup the test files */
    sprintf(fileName, "%s/test-symlink-perms-file", commonOpts.directory);
    sprintf(linkName, "%s/test-symlink-perms-link", commonOpts.directory);
    ret = setup_symlink_perms(fileName, linkName, &commonOpts);
    if(ret == -1)
    {
        print_error("\tUnable to setup SETGID\n");
        cleanup(pvfs2_tab_file, &commonOpts);
        exit(1);  
    }      
    
    /* ---------- BEGIN TEST CASE  ---------- 
     * Check to see if the symbolic link permissons are correct to begin with 
     */
    ret = stat_file(linkName, 
                    &stStats, 
                    0, /* Don't follow link */
                    commonOpts.use_pvfs2_lib, 
                    commonOpts.verbose);
    
    if(ret != TEST_COMMON_SUCCESS)
    {
        print_error("\tFAILED. Couldn't execute stat on [%s]\n", linkName);
    }
    else
    {
        /* Check to make sure we see the setgid bit */
        if(stStats.st_mode == (stStats.st_mode | S_IRWXO |  S_IRWXG | S_IRWXU))
        {
            if(commonOpts.printResults)
            {
                printf("\tPASSED. Permissions are 777 for [%s]\n", linkName); 
            }
        }
        else
        {
            print_error("\tFAILED. Permissions are incorrect (before chmod) on [%s]\n",
                        linkName);
        }
    }
    
    /* ---------- BEGIN TEST CASE  ---------- 
     * Check to see if we can change permissions on a symbolic link
     */

    ret = change_mode(linkName, 0700, &error_code, commonOpts.use_pvfs2_lib, commonOpts.verbose);
        
    /* don't check return code */

    /* ---------- BEGIN TEST CASE  ---------- 
     * Check to see if the symbolic link permissons are still correct 
     */
    ret = stat_file(linkName, 
                    &stStats, 
                    0, /* Don't follow link */
                    commonOpts.use_pvfs2_lib, 
                    commonOpts.verbose);
    
    if(ret != TEST_COMMON_SUCCESS)
    {
        print_error("\tFAILED. Couldn't execute stat on [%s]\n", linkName);
    }
    else
    {
        /* Check to make sure we see the setgid bit */
        /* TODO: this test doesn't work right when using library */
        if(stStats.st_mode == (stStats.st_mode | S_IRWXO |  S_IRWXG | S_IRWXU))
        {
            if(commonOpts.printResults)
            {
                printf("\tPASSED. Permissions are 777 for [%s]\n", linkName); 
            }
        }
        else
        {
            print_error("\tFAILED. Permissions are incorrect (after chmod) on [%s]\n",
                        linkName);
        }
    }
 
    /* Remove any leftover test data */
    ret = destroy_symlink_perms(fileName, linkName, &commonOpts);
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
        printf("-------------------- Ending symlink-perm tests --------------------\n");
    }
    
    return 0;  
}

/**
 * \retval 0 Success
 * \retval !0 Failure 
 */
int setup_symlink_perms(
    const char * file_name,      /**< file name */
    const char * link_name,      /**< symbolic link (will point to file_name) */
    struct common_options * opts) /**< Options structure */
{
    int  ret  = 0;
    int  mode = S_IRWXO |  S_IRWXG | S_IRWXU; /* 0777 */
   
    /* Create the file */
    ret = create_file(file_name, 
                      mode, 
                      opts->use_pvfs2_lib,
                      opts->verbose);
   
    if(ret != TEST_COMMON_SUCCESS)
    {
        printf("\tUnable to create file [%s]\n", file_name);
        return(ret);
    }
 
    /* Create the symlink */
    ret = create_symlink(link_name, 
                         file_name, 
                         opts->use_pvfs2_lib,
                         opts->verbose);
   
    if(ret != TEST_COMMON_SUCCESS)
    {
        printf("\tUnable to create symlink [%s]\n", link_name);
        /* try to remove file */
        remove_file(file_name, opts->use_pvfs2_lib, opts->verbose);
        return(ret);
    }

    return(0);
}

/**
 * \retval 0 Success
 * \retval !0 Failure 
 */
int destroy_symlink_perms(const char * file_name, const char * symlink_name, 
    struct common_options* opts)
{
    int  ret = 0;
   
    ret = remove_file(file_name, opts->use_pvfs2_lib, opts->verbose);
    if(ret != TEST_COMMON_SUCCESS)
    {
        printf("\tUnable to remove file [%s]\n", file_name);
        return(ret);
    }
 
    ret = remove_symlink(symlink_name, opts->use_pvfs2_lib, opts->verbose);
    if(ret != TEST_COMMON_SUCCESS)
    {
        printf("\tUnable to remove symlink [%s]\n", symlink_name);
        return(ret);
    }

    return(0);
}

void cleanup(const char * pvfs2_tab_file, struct common_options* opts)
{
    /* Setup the PVFS2TAB file first thing */
    if(opts->hostname != NULL)
    {
        destroy_pvfs2tab_file(pvfs2_tab_file);
    }
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

