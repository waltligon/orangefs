/*
 * Copyright (c) Acxiom Corporation, 2005 
 *
 * See COPYING in top-level directory
 */

/* This program tests the ability to make directories on a pvfs2 file system. 
 * 
 */
 
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <assert.h>
#include <time.h>
#include <limits.h>
#include "test-common.h"

#include <pvfs2.h>

static int is_file_mode_correct(
    mode_t fileMode, 
    mode_t requestedMode, 
    struct common_options* opts);
static void cleanup(
    const char* pvfs2_tab_file, 
    struct common_options* commonOpts);

int main(int argc, char **argv)
{
    int    ret=0,
           mode=0;
    char   filename[PATH_MAX]= "",
           testDir[PATH_MAX]= "", /* Directory where all the test files will reside */
           pvfsPath[PVFS_NAME_MAX]= "", /* fs relative path */
           cmd[PATH_MAX]       = "",
           pvfs2_tab_file[PATH_MAX]= ""; /* used to store pvfs2 tab file */
    struct file_ref stFileRef;
    struct common_options commonOpts;
    struct stat stStats;
    PVFS_fs_id cur_fs;

    memset(&stFileRef, 0, sizeof(stFileRef));
    memset(&commonOpts,0, sizeof(commonOpts));
    memset(&stStats,   0, sizeof(stStats));
    memset(&cur_fs,    0, sizeof(cur_fs));
   
    if(getuid() == 0)
    {
        print_error("ERROR: test must be run as a non-root user.\n");
        return(-1);
    }

    ret = parse_common_args(argc, argv, &commonOpts);
    if(ret < 0)
    {
       exit(1);
    }

    if(commonOpts.printResults)
    {
        printf("-------------------- Starting mkdir tests --------------------\n");
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
                       pvfsPath,
                       sizeof(pvfsPath),
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
   
    /* ---------- BEGIN TEST CASE  ---------- 
     * Setup the main directory which will have the setgid bit set 
     * Create with 777 permissions
     */
    sprintf(testDir, "%s/%d-%s", commonOpts.directory, geteuid(), "mkdir-test");
    mode = S_IRWXO | S_IRWXG | S_IRWXU;  /* 0777 */
    ret = create_directory(testDir, 
                           mode,
                           commonOpts.use_pvfs2_lib,
                           commonOpts.verbose);

    if(ret != 0)
    {
      fprintf(stderr, "\t FAILED. Couldn't create [%s]\n", testDir);
      cleanup(pvfs2_tab_file, &commonOpts);
      exit(1);
    }

    /* Verify that the permissions are at 777 */
    ret = stat_file(testDir, 
                    &stStats,
                    0, /* Don't follow link */ 
                    commonOpts.use_pvfs2_lib, 
                    commonOpts.verbose);
   
    if(ret != TEST_COMMON_SUCCESS)
    {
      fprintf(stderr, "\t FAILED. Couldn't execute stat on [%s]\n", testDir);
      /* Let's not quit here, we may find more errors by running more tests */
    }
    else
    {
        print_stats(stStats, commonOpts.verbose);
        /* Check to make sure the permissions are correct */
        if(!is_file_mode_correct(stStats.st_mode, mode, &commonOpts))
        {
            if(commonOpts.verbose)
            {
                fprintf(stdout, "\t [%s] has mode [%o]\n", testDir, stStats.st_mode);
            }
            fprintf(stderr, "\t FAILED. [%s] NOT created using mode [%o]\n", testDir, mode); 
            /* Let's not quit here, we may find more errors by running more tests */
        }
        else
        {
            if(commonOpts.printResults)
            {
                fprintf(stdout, "\t PASSED. [%s] created using mode [%o]\n", testDir, mode); 
            }
        }
    }

    /* ---------- BEGIN TEST CASE  ---------- 
     * The main directory must already be created. Both child and main directory
     * will need to be removed later
     */
    sprintf(filename, "%s/%s", testDir, "child_dir");
    mode = S_IRWXO | S_IRWXG | S_IRWXU;  /* 0777 */
    ret = create_directory(filename, 
                           mode,
                           commonOpts.use_pvfs2_lib,
                           commonOpts.verbose);

    if(ret != 0)
    {
      fprintf(stderr, "\t FAILED. Couldn't create [%s]\n", filename);
      cleanup(pvfs2_tab_file, &commonOpts);
      exit(1);
    }

    /* Verify that the directory exists */
    ret = stat_file(filename, 
                    &stStats,
                    0, /* Don't follow link */ 
                    commonOpts.use_pvfs2_lib, 
                    commonOpts.verbose);
   
    if(ret != TEST_COMMON_SUCCESS)
    {
      fprintf(stderr, "\t FAILED. Couldn't execute stat on [%s]\n", filename);
      /* Let's not quit here, we may find more errors by running more tests */
    }
    else
    {
        print_stats(stStats, commonOpts.verbose);
        if(commonOpts.printResults)
        {
            fprintf(stdout, "\t PASSED. [%s] created using mode [%o]\n", filename, mode); 
        }
    }

    /* Remove the child_dir */
    ret = remove_directory(filename, commonOpts.use_pvfs2_lib, commonOpts.verbose);
    
    if(ret != 0)
    {
        fprintf(stderr, "\t FAILED. Unable to remove [%s]\n", filename);
    }

    /* Remove the main directory */
    ret = remove_directory(testDir, commonOpts.use_pvfs2_lib, commonOpts.verbose);
    
    if(ret != 0)
    {
        fprintf(stderr, "\t FAILED. Unable to remove [%s]\n", testDir);
    }
   
    /* ---------- BEGIN TEST CASE  ---------- 
     * Make sure we can create parent directories. Send a directory to create
     * whose parent doesn't exist either. Only do this for PVFS2 
     */
    if(commonOpts.use_pvfs2_lib)
    {
        sprintf(filename, "%s/%s", testDir, "child_dir");
        mode = S_IRWXO | S_IRWXG | S_IRWXU;  /* 0777 */

        sprintf(cmd, "pvfs2-mkdir -p -m %o %s", mode, filename);
        ret = system(cmd);
    
        if(ret != TEST_COMMON_SUCCESS)
        {
          fprintf(stderr, "\t FAILED. Couldn't create [%s]\n", filename);
          /* Let's not quit here, we may find more errors by running more tests */
        }
    
        /* Verify that the directory exists */
        ret = stat_file(filename, 
                        &stStats,
                        0, /* Don't follow link */ 
                        commonOpts.use_pvfs2_lib, 
                        commonOpts.verbose);
       
        if(ret != TEST_COMMON_SUCCESS)
        {
          fprintf(stderr, "\t FAILED. Couldn't execute stat on [%s]\n", filename);
          /* Let's not quit here, we may find more errors by running more tests */
        }
        else
        {
            print_stats(stStats, commonOpts.verbose);
            if(commonOpts.printResults)
            {
                fprintf(stdout, "\t PASSED. [%s] created with parent mode set and mode [%o]\n", 
                        filename, 
                        mode); 
            }
        }
    
        /* Remove the child directory */
        ret = remove_directory(filename, commonOpts.use_pvfs2_lib, commonOpts.verbose);
        
        if(ret != 0)
        {
            fprintf(stderr, "\t FAILED. Unable to remove [%s]\n", filename);
            cleanup(pvfs2_tab_file, &commonOpts);
            exit(1);
        }

        /* Remove the main directory */
        sprintf(filename, "%s", testDir);
        ret = remove_directory(filename, commonOpts.use_pvfs2_lib, commonOpts.verbose);
        
        if(ret != 0)
        {
            fprintf(stderr, "\t FAILED. Unable to remove [%s]\n", filename);
            /* Let's not quit here, we may find more errors by running more tests */
        }
    }

    /* ---------- BEGIN TEST CASE  ---------- 
     * Pass more than one directory to the pvfs2-mkdir command. Only run if 
     * this is using the PVFS2 API
     */
    if(commonOpts.use_pvfs2_lib)
    {
        sprintf(filename, "%s/%s", testDir, "child_dir");
        mode = S_IRWXO | S_IRWXG | S_IRWXU;  /* 0777 */

        sprintf(cmd, "pvfs2-mkdir -p -m %o %s %s", mode, testDir, filename);
        ret = system(cmd);
    
        if(ret != TEST_COMMON_SUCCESS)
        {
          fprintf(stderr, "\t FAILED. Couldn't create [%s] and [%s]\n", 
                  testDir,
                  filename);
        }
    
        /* Verify that the directory exists */
        ret = stat_file(filename, 
                        &stStats,
                        0, /* Don't follow link */ 
                        commonOpts.use_pvfs2_lib, 
                        commonOpts.verbose);
       
        if(ret != TEST_COMMON_SUCCESS)
        {
          fprintf(stderr, "\t FAILED. Couldn't execute stat on [%s]\n", filename);
        }
        else
        {
            print_stats(stStats, commonOpts.verbose);
            if(commonOpts.printResults)
            {
                fprintf(stdout, "\t PASSED. created two directories via command line\n"); 
            }
        }
    
        /* Remove the child directory */
        ret = remove_directory(filename, commonOpts.use_pvfs2_lib, commonOpts.verbose);
        
        if(ret != 0)
        {
            fprintf(stderr, "\t FAILED. Unable to remove [%s]\n", filename);
            exit(1);
        }

        /* Remove the main directory */
        sprintf(filename, "%s", testDir);
        ret = remove_directory(filename, commonOpts.use_pvfs2_lib, commonOpts.verbose);
        
        if(ret != 0)
        {
            fprintf(stderr, "\t FAILED. Unable to remove [%s]\n", filename);
        }
    }


    /* Remove any leftover test data */
    ret = finalize(commonOpts.use_pvfs2_lib);
    if(ret < 0)
    {
        fprintf(stderr, "\t ERROR: finalize failed\n");
        return(-1);
    }
    
    if(commonOpts.verbose)
    {
        printf("-------------------- Ending   mkdir tests --------------------\n");
    }

    return 0;  
}

/*
 * Checks to see if the file contains the mode requested. This takes into 
 * account the file creation of mode & ~umask
 * \retval 1 on success
 * \retval 0 on failure
 */
int is_file_mode_correct(mode_t fileMode, mode_t requestedMode, struct common_options* opts)
{
    mode_t mask = 0;

    /* We need to figure out what the file would have had the mode set to */
    mask = (int)umask(0);
    umask(mask);
    /* The pvfs2-mkdir command doesn't use the umask to determine permissions */
    if(!opts->use_pvfs2_lib)
    {
        requestedMode = requestedMode & ~mask;
    }
    
    /* We need to drop all high level mode bits mode & (0777) */
    fileMode = fileMode & (S_IRWXO | S_IRWXG | S_IRWXU);
    
    if(opts->verbose)
    {
        fprintf(stdout, "Checking file mode against requested mode\n");
        fprintf(stdout, "\t fileMode      = [%o]\n", fileMode);
        fprintf(stdout, "\t requestedMode = [%o]\n", requestedMode);
    }
        
    if(fileMode != requestedMode)
    { 
        return (0);
    }
    
    return(1);
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
