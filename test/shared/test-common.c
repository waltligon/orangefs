/*
 * Copyright (c) Acxiom Corporation, 2005 
 *
 * See COPYING in top-level directory
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <limits.h>
#include <string.h>
#include <getopt.h>
#include <assert.h>

#include "test-common.h"
#include "libgen.h"

/** \file
 * Implementation of the test-common wrapper functions.
 */

static char pvfsEXELocation[PATH_MAX]; /* Used to prefix the command line 
                                        * utilities for pvfs2 utils 
                                        */
static void copy_pvfs2_to_stat(
    const PVFS_sys_attr * attr,
    struct stat         * fileStats);
static void display_common_usage(char* exeName);
 

/**
 * Sets a local char array containing the path to prefix all pvfs2 utilities
 * \retval TEST_COMMON_SUCCESS Success
 * \retval TEST_COMMON_FAIL Failure 
 */
int set_util_path(const char * utility_path) /**< NULL terminated path. MUST end with "/" */
{
    int retval = 0; 
    
    /* Validate we have enough space */
    if (sizeof(pvfsEXELocation) < (strlen(utility_path)+1))
    {
        print_error("Unable to create pvfs2tab file: "
                    "Specified pvfsEXELocation length (%d bytes) too short.\n"
                    "pvfsEXELocation length needs to be at least %d bytes.\n",
                    sizeof(pvfsEXELocation), strlen(utility_path));
        return(TEST_COMMON_FAIL);
    }

    retval = snprintf(pvfsEXELocation, sizeof(pvfsEXELocation), "%s", utility_path);

    if ((retval+1) > sizeof(pvfsEXELocation))  /* retval does not include NULL terminator */
    {
        print_error("Internal variable \"pvfsEXELocation\" too short.\n"
                    "This array needs to be at least %d bytes in length.\n",
                    retval+1);
        return(TEST_COMMON_FAIL);
    }
    return(TEST_COMMON_SUCCESS);
}

/**
 * creates the pvfs2tab file and sets the environment variable appropriately 
 * for you to use the PVFS2 API.
 *
 * \retval TEST_COMMON_SUCCESS Success
 * \retval TEST_COMMON_FAIL Failure 
 */
int create_pvfs2tab_file(
    char* pvfs2tab_name, /**< Placeholder for file name of file created */
    int len,             /**< Maximum length of pvfs2tab_name */
    const int   port,    /**< Port number for PVFS2 file system */
    const char* networkProto, /**< Network Protocol, i.e. tcp */
    const char* hostname, /**< Hostname of main metaserver */
    const char* fsname,  /**< file system name in pvfs2 configuration */
    const char* fs_file) /**< fs_file as defined in fstab (mount point) */
{
    FILE * pvfs2tab = NULL;
    char   buffer[512] = "";
    char   fs_spec[PATH_MAX]= ""; /* fs_spec as defined in fstab (remote file system) */
    int    retval = TEST_COMMON_SUCCESS;

    retval = snprintf(fs_spec, sizeof(fs_spec), "%s://%s:%d/%s", 
                      networkProto, 
                      hostname,
                      port,
                      fsname);

    if ((retval+1) > sizeof(fs_spec))  /* ret does not include NULL terminator */
    {
        print_error("Internal variable \"fs_spec\" too short.\n"
                    "This array needs to be at least %d bytes in length.\n",
                    retval+1);
        return(TEST_COMMON_FAIL);
    }


    /* Construct unique name for pvfstab file (use the pid to make it unique) */
    retval = snprintf(buffer, sizeof(buffer), "/usr/tmp/pvfs2tab_%d", getpid());
    if ((retval+1) > sizeof(buffer))  /* retval does not include NULL terminator */
    {
        print_error("Internal variable \"buffer\" too short.\n"
                    "This array needs to be at least %d bytes in length.\n",
                    retval+1);
        return(TEST_COMMON_FAIL);
    }
   
    if (len < (strlen(buffer)+1))
    {
        print_error("Unable to create pvfs2tab file: "
                    "Specified buffer length (%d bytes) too short.\n"
                    "Buffer length needs to be at least %d bytes.\n",
                    len, strlen(buffer));
        return(TEST_COMMON_FAIL);
    }
   
    strncpy(pvfs2tab_name, buffer, len);

    pvfs2tab = fopen(pvfs2tab_name, "w");
    if (pvfs2tab == NULL)
    {
        print_error("Could not open pvfs2tab file [%s]: %s\n", 
                    pvfs2tab_name, strerror_r(errno, buffer, sizeof(buffer)));
        return(TEST_COMMON_FAIL);
    }

    fprintf(pvfs2tab, "%s %s pvfs2 default,noauto 0 0\n", fs_spec, fs_file);  
   
    fclose(pvfs2tab);

    /* The PVFS2TAB_FILE environment variable must be set to use this file */
    if (setenv("PVFS2TAB_FILE", pvfs2tab_name, 1))
    {
        print_error("Unable to set PVFS2TAB_FILE environment variable.\n");
        return(TEST_COMMON_FAIL);
    }
    return(TEST_COMMON_SUCCESS);
}

/**
 * Deletes the pvfstab file and un-sets the environment variable PVFS2TAB_FILE
 *
 * \retval TEST_COMMON_SUCCESS Success
 * \retval TEST_COMMON_FAIL Failure 
 */
int destroy_pvfs2tab_file(const char * pvfs2tab_name) /**< name of file to delete */
{
    if(unlink(pvfs2tab_name))
    {
        print_error("Unable to unlink [%s].\n"
                    "\tError Number = [%d]\n"
                    "\tError Desc   = [%s]\n",
                    pvfs2tab_name,
                    errno,
                    strerror(errno));
        return(TEST_COMMON_FAIL);
    }

    if(unsetenv("PVFS2TAB_FILE"))
    {
        print_error("Unable to un-set PVFS2TAB_FILE environment variable.\n");
        return(TEST_COMMON_FAIL);
    }
    return(TEST_COMMON_SUCCESS); 
}

/** Initializes the PVFS2 API  
 *
 * \retval TEST_COMMON_SUCCESS Success
 * \retval TEST_COMMON_FAIL Failure 
 */
int initialize(
    const int use_pvfs2_lib, /**< determines use of pvfs2 library  */
    const int verbose)       /**< Turns on verbose prints if set to non-zero value */
{
    int ret = 0;

    if(verbose) { printf("Initializing PVFS2 API\n"); }
   
    if(use_pvfs2_lib)
    {
        ret = PVFS_util_init_defaults();
        if(ret < 0)
        {
            PVFS_perror("PVFS_util_init_defaults", ret);
            print_error("Unable to initialize PVFS2 API\n");
            return(TEST_COMMON_FAIL);
        }
    }
    return(TEST_COMMON_SUCCESS);
}

/**
 * \note initialize must be called prior to this function. This function will 
 *       fill in the cur_fs and relativeName on success
 *
 * \retval TEST_COMMON_SUCCESS Success (PVFS2 file)
 * \retval TEST_COMMON_FAIL Failure (NOT PVFS2 file)
 */
int is_pvfs2(
    const char * fileName,         /**< Null terminated file name */
    PVFS_fs_id * cur_fs,           /**< container for return value for fs_id */
    char       * relativeName,     /**< container for return value for relative path */
    const int    relativeNameSize, /**< size of relativeName */
    const int    use_pvfs2_lib,    /**< determines use of pvfs2 library */
    const int    verbose)          /**< Turns on verbose prints if set to non-zero value */
{
    int ret=0;

    if(verbose) { printf("Checking to see if [%s] is on a pvfs2 file system\n", fileName); }
   
    if(use_pvfs2_lib)
    {
        ret = PVFS_util_resolve(fileName,
                                cur_fs, 
                                relativeName, 
                                relativeNameSize);
        if(ret < 0)
        {
            PVFS_perror("PVFS_util_resolve", ret);
            print_error("Unable to deteremine if [%s] resides on PVFS2\n", fileName);
            return(TEST_COMMON_FAIL);
        }
    }

    return(TEST_COMMON_SUCCESS);
}

/**
 * \retval TEST_COMMON_SUCCESS Success
 * \retval TEST_COMMON_FAIL Failure 
 */
int finalize(int use_pvfs2_lib) /**< determines use of pvfs2 library */
{
    int ret;

    if(use_pvfs2_lib)
    {
        ret = PVFS_sys_finalize();
        if(ret < 0)
        {
            PVFS_perror("PVFS_sys_finalize", ret);
            print_error("Unable to finalize PVFS2 API\n");
            return(TEST_COMMON_FAIL);
        }
    }
    return(TEST_COMMON_SUCCESS);
}

/**
 * \retval  0  SUCCESS
 * \retval  errno FAILURE using VFS layer
 * \retval  -ENODATA FAILURE using PVFS2 API
 */
int close_file(
    struct file_ref *stFileRef, /**< File descriptor (or handle) for an open file */
    const int use_pvfs2_lib,    /**< determines use of pvfs2 library */
    const int verbose)          /**< Turns on verbose prints if set to non-zero value */
{
    int ret=0;
   
    if(verbose)
    {
        if(use_pvfs2_lib) 
        {
            printf("\tClosing [%Ld]\n", stFileRef->handle);
        }
        else
        {
            printf("\tClosing [%d]\n", stFileRef->fd);
        }
    }

    if(use_pvfs2_lib)
    {
        printf("close_file not implemented for PVFS2 API\n");
        return(-ENODATA);
    }
    else
    {
        ret = close(stFileRef->fd);
        if(ret != 0)
        {
            ret = errno; /* Save the error number */
            if(verbose)
            {
                print_error("\tError closing file descriptor [%d].\n"   
                            "\t\tError Number = [%d]\n"
                            "\t\tError Desc   = [%s]\n",
                            stFileRef->fd,
                            errno,
                            strerror(errno));
            }
        }
        stFileRef->fd = 0;
    }
    return(ret); 
}

/**
 * \retval TEST_COMMON_SUCCESS Success
 * \retval TEST_COMMON_FAIL Failure 
 */
int stat_file(
    const char  * fileName,      /**< File Name */
    struct stat * fileStats,     /**< stat structure to place results from stat call */
    const int     followLink,    /**< Determines whether to stat link or link target */
    const int     use_pvfs2_lib, /**< determines use of pvfs2 library */
    const int     verbose)       /**< Turns on verbose prints if set to non-zero value */
{
   int  ret=0;
   char szPvfsPath[PVFS_NAME_MAX] = "";
   PVFS_sysresp_lookup  lk_response;
   PVFS_object_ref      ref;
   PVFS_sysresp_getattr getattr_response;
   PVFS_credentials     credentials;
   PVFS_fs_id           fs_id;
  
    if(verbose) { printf("\tPerforming stat on [%s]\n", fileName); }
   
    if(use_pvfs2_lib)
    {
        ret = PVFS_util_resolve(fileName, 
                                &fs_id, 
                                szPvfsPath, 
                                sizeof(szPvfsPath));

        if (ret < 0)
        {
            print_error("Error: could not find file system for [%s] in pvfstab\n", fileName);
            return(TEST_COMMON_FAIL);
        }

        PVFS_util_gen_credentials(&credentials);
 
        if(followLink)
        {
            ret = PVFS_sys_lookup(fs_id, 
                                  szPvfsPath, 
                                  &credentials, 
                                  &lk_response, 
                                  PVFS2_LOOKUP_LINK_FOLLOW);
        }
        else
        {
            ret = PVFS_sys_lookup(fs_id, 
                                  szPvfsPath, 
                                  &credentials, 
                                  &lk_response, 
                                  PVFS2_LOOKUP_LINK_NO_FOLLOW);
        }
   
        if(ret < 0)
        {
            PVFS_perror("PVFS_sys_lookup", ret);
            return(TEST_COMMON_FAIL);
        }
     
        ref.handle = lk_response.ref.handle;
        ref.fs_id  = fs_id;
      
        ret = PVFS_sys_getattr(ref, 
                               PVFS_ATTR_SYS_ALL,
                               &credentials, 
                               &getattr_response);

        if(ret < 0)
        {                          
            PVFS_perror("PVFS_sys_getattr", ret);
            return(TEST_COMMON_FAIL);
        }
        copy_pvfs2_to_stat(&getattr_response.attr, fileStats);
    }
    else
    {
        if(followLink)
        {
            ret = stat(fileName, fileStats);
        }
        else
        {
            ret = lstat(fileName, fileStats);
        }

        if( ret  != 0)
        {
            if(verbose)
            {
                print_error("\tError performing stat on [%s].\n"   
                            "\t\tError Number = [%d]\n"
                            "\t\tError Desc   = [%s]\n",
                            fileName,
                            errno,
                            strerror(errno));
            }
            return(TEST_COMMON_FAIL);
        }
    }
    return(TEST_COMMON_SUCCESS);  
}

/**
 * \retval TEST_COMMON_SUCCESS Success
 * \retval TEST_COMMON_FAIL Failure 
 */
int open_file(
    const char      * fileName,    /**< File Name */
    const int         accessFlags, /**< open access flags */
    const int         mode,        /**< open mode flags */
    const int         use_pvfs2_lib,   /**< determines use of pvfs2 library */
    const int         verbose,     /**< Turns on verbose prints if set to non-zero value */
    const int         followLink,  /**< Follow symbolic link to target? */
    struct file_ref * pstFileRef)  /**< File descriptor (or handle) for an open file */
{
    int ret=0;
   
    if(verbose) { printf("\tOpening [%s]\n", fileName); }
   
    if(use_pvfs2_lib)
    {
        ret = pvfs2_open(fileName, 
                         accessFlags, 
                         mode,
                         verbose,
                         followLink,
                         pstFileRef);

        if(ret != TEST_COMMON_SUCCESS)
        {
            print_error("Unable to open [%s]\n", fileName);
            return(TEST_COMMON_FAIL);  
        }     
    }
    else
    {
        pstFileRef->fd = open(fileName, accessFlags, mode);
    }


    if(pstFileRef->handle == -1 || pstFileRef->fd == -1)
    {
        return(TEST_COMMON_FAIL);
    }
    return(TEST_COMMON_SUCCESS); 
}

/**
 * \retval TEST_COMMON_SUCCESS Success
 * \retval TEST_COMMON_FAIL Failure 
 */
int create_file(
    const char * fileName,  /**< File Name */
    const int    mode,      /**< directory permissions */
    const int    use_pvfs2_lib, /**< determines use of pvfs2 library */
    const int    verbose)   /**< Turns on verbose prints if set to non-zero value */
{
    int  ret=0;
    char szPvfsPath[PVFS_NAME_MAX] = "";
    PVFS_fs_id fs_id;
    PVFS_credentials credentials;
    struct file_ref stFileRef;
   
    if(verbose) { printf("\tCreating [%s] using mode [%o]\n", fileName, mode); }
   
    if(use_pvfs2_lib)
    {
        ret = PVFS_util_resolve(fileName, 
                                &fs_id, 
                                szPvfsPath, 
                                sizeof(szPvfsPath));

        if (ret < 0)
        {
            print_error("Error: could not find file system for [%s] in pvfstab\n", fileName);
            return(TEST_COMMON_FAIL);
        }

        PVFS_util_gen_credentials(&credentials);

        ret = pvfs2_create_file(szPvfsPath,
                                fs_id,
                                &credentials,
                                mode,
                                verbose,
                                &stFileRef);

        if (ret < 0)
        {
            print_error("Error: could not create test file [%s]\n", fileName);
            return(TEST_COMMON_FAIL);
        }
    }
    else
    {
        stFileRef.fd = open(fileName, (O_WRONLY|O_CREAT|O_EXCL), mode);
        if(stFileRef.fd < 0)
        {
            print_error("Error: could not create test file [%s]: %s\n", 
                        fileName, strerror(errno));
            return(TEST_COMMON_FAIL);
        }

        ret = close(stFileRef.fd);
        if(ret < 0)
        {
            print_error("Error: could not close test file [%s]: %s\n",
                        fileName, strerror(errno));
            return(TEST_COMMON_FAIL);
        }
    }
    return(TEST_COMMON_SUCCESS); 
}

/**
 * \retval  0  SUCCESS
 * \retval  errno FAILURE using VFS layer
 * \retval  -ENODATA FAILURE using PVFS2 API
 */
int create_directory(
    const char * directory, /**< Directory Name */
    const int    mode,      /**< directory permissions */
    const int    use_pvfs2_lib, /**< determines use of pvfs2 library */
    const int    verbose)   /**< Turns on verbose prints if set to non-zero value */
{
    int  ret=0;
    char cmd[PATH_MAX] = "";
   
    if(verbose) { printf("\tCreating [%s] using mode [%o]:\n", directory, mode); }
   
    if(use_pvfs2_lib)
    {
        if(verbose) 
        {
            snprintf(cmd, sizeof(cmd), "%spvfs2-mkdir -m %o %s", pvfsEXELocation, mode, directory);
        }
        else
        {
            /* Make sure nothing prints to STDOUT/STDERR if verbose mode is off */
            snprintf(cmd, sizeof(cmd), "%spvfs2-mkdir -m %o %s >/dev/null 2>&1", pvfsEXELocation, mode, directory);
        }
        ret = system(cmd);
        if(ret != 0)
        {
            ret = -ENODATA; /* Set to a generic return code, since errno not 
                             * appropriate for command line utility */
            if(verbose)  
            {
                print_error("\tUnable to create [%s] using mode [%o]\n", directory, mode);
            }
        }
    }
    else
    {
        ret = mkdir(directory, mode);
        if(ret != 0)
        {
            ret = errno; /* Save errno for return */
            if(verbose)  
            {
                print_error("\tUnable to create [%s] using mode [%o]\n"
                            "\t\tError Number = [%d]\n"
                            "\t\tError Desc   = [%s]\n",
                            directory,
                            mode,
                            errno,
                            strerror(errno));
            }
        }
    }

   return ret; 
}

/**
 * \retval  0  SUCCESS
 * \retval  errno FAILURE using VFS layer
 * \retval  -ENODATA FAILURE using PVFS2 API
 */
int remove_directory(
    const char * directory, /**< Directory Name */
    const int    use_pvfs2_lib, /**< determines use of pvfs2 library */
    const int    verbose)   /**< Turns on verbose prints if set to non-zero value */
{
    int  ret=0;
    char cmd[PATH_MAX] = "";
    
    if(verbose) { printf("\tRemoving [%s]\n", directory); }
    
    if(use_pvfs2_lib)
    {
        if(verbose)
        {
            snprintf(cmd, sizeof(cmd), "%spvfs2-rm %s", pvfsEXELocation, directory);
        }
        else
        {
            snprintf(cmd, sizeof(cmd), "%spvfs2-rm %s  >/dev/null 2>&1", pvfsEXELocation, directory);
        }

        ret = system(cmd);
        if(ret != 0)
        {
            ret = -ENODATA; /* Save the error number */
            if(verbose)
            {
                print_error("\tUnable to remove [%s]\n", directory);
            }
        }
    }
    else
    {
        ret = rmdir(directory);
        if(ret != 0)
        {
            ret = errno; /* Save the error number */
            if(verbose)
            {
                print_error("\tUnable to remove [%s]\n"
                            "\t\tError Number = [%d]\n"
                            "\t\tError Desc   = [%s]\n",
                            directory,
                            errno,
                            strerror(errno));
            }
        }
    }
    return ret; 
}

/**
 * \retval  0  SUCCESS
 * \retval  errno FAILURE using VFS layer
 * \retval  -ENODATA FAILURE using PVFS2 API
 */
int remove_symlink(
    const char * linkName,  /**< Symlink Name */
    const int    use_pvfs2_lib, /**< determines use of pvfs2 library */
    const int    verbose)   /**< Turns on verbose prints if set to non-zero value */
{
    /* the remove_file() function should work fine on symbolic links */
    return (remove_file(linkName, use_pvfs2_lib, verbose)); 
}


/**
 * \retval  0  SUCCESS
 * \retval  errno FAILURE using VFS layer
 * \retval  -ENODATA FAILURE using PVFS2 API
 */
int remove_file(
    const char * fileName,  /**< File Name */
    const int    use_pvfs2_lib, /**< determines use of pvfs2 library */
    const int    verbose)   /**< Turns on verbose prints if set to non-zero value */
{
    int  ret=0;
    char cmd[PATH_MAX] = "";
   
    if(verbose) { printf("\tRemoving [%s]\n", fileName); }
   
    if(use_pvfs2_lib)
    {
        if(verbose)
        {
            snprintf(cmd, sizeof(cmd), "%spvfs2-rm %s", pvfsEXELocation, fileName);
        }
        else
        {
            snprintf(cmd, sizeof(cmd), "%spvfs2-rm %s >/dev/null 2>&1", pvfsEXELocation, fileName);
        }
        ret = system(cmd);
        if(ret != 0)
        {
            ret = -ENODATA;
            if(verbose) { print_error("\tUnable to remove [%s]\n", fileName); }
        }
    }
    else
    {
        ret = unlink(fileName);
        if(ret != 0)
        {
            ret = errno;
            if(verbose)
            {
                print_error("\tUnable to remove [%s]\n"
                            "\t\tError Number = [%d]\n"
                            "\t\tError Desc   = [%s]\n",
                            fileName,
                            errno,
                            strerror(errno));
            }
        }
    }
    return(ret); 
}

/**
 * \retval  0  SUCCESS
 * \retval  errno FAILURE using VFS layer
 * \retval  -ENODATA FAILURE using PVFS2 API
 */
int change_mode(
    const char * fileName,  /**< Target Name */
    const int    mode,      /**< Mode to set  */
          int*   error_code, /**< Not implemented, should be return code from call */
    const int    use_pvfs2_lib, /**< determines use of pvfs2 library */
    const int    verbose)   /**< Turns on verbose prints if set to non-zero value */
{
    int  ret=0;
    char cmd[PATH_MAX] = "";
   
    if(verbose) { printf("\tChanging mode on [%s] to [%o]\n", fileName, mode); }
   
    if(use_pvfs2_lib)
    {
        if(verbose)
        {
            snprintf(cmd, sizeof(cmd), "%spvfs2-chmod %o %s", 
                     pvfsEXELocation, mode, fileName);
        }
        else
        {
            snprintf(cmd, sizeof(cmd), "%spvfs2-chmod %o %s >/dev/null 2>&1", 
                     pvfsEXELocation, mode, fileName);
        }
        ret = system(cmd);
        if(ret != 0)
        {
            ret = -ENODATA; 
            if(verbose)
            {
                print_error("\tUnable to chmod [%s] to [%o]\n", fileName, mode);
            }
        }
    }
    else
    {
        ret = chmod(fileName, mode);
        if(ret != 0)
        {
            ret = errno; /* Save the error number */
            if(verbose)
            {
                print_error("\tUnable to chmod [%s] to [%o]\n"
                            "\t\tError Number = [%d]\n"
                            "\t\tError Desc   = [%s]\n",
                            fileName,
                            mode,
                            errno,
                            strerror(errno));
            }
        }
    }
    return(ret);
}

/**
 * \retval TEST_COMMON_SUCCESS Success
 * \retval TEST_COMMON_FAIL Failure 
 */
int change_owner(
    const char * fileName,  /**< Target Name */
    const char * ownerName, /**< Owner Name to change to */  
    const uid_t  owner_id,  /**< The uid of the new owner  */     
    const char * groupName, /**< group Name to change to */
    const gid_t  group_id,  /**< The uid of the new group */
    const int    use_pvfs2_lib, /**< determines use of pvfs2 library */
    const int    verbose)   /**< Turns on verbose prints if set to non-zero value */
{
    int  ret=0;
    char cmd[PATH_MAX] = "";
   
    if(verbose)
    {
        if(use_pvfs2_lib)
        {
            printf("\tChanging owner on [%s] to [%s].[%s]\n", fileName, ownerName, groupName);
        }
        else
        {
            printf("\tChanging owner on [%s] to [%d].[%d]\n", fileName, owner_id, group_id);
        }
    }
   
    if(use_pvfs2_lib)
    {
        /* Determine if we need to use sudo to change owner/group */
        if(geteuid() != owner_id ||
           geteuid() != group_id)
        {
            if(verbose)
            {
                snprintf(cmd, sizeof(cmd), "sudo %spvfs2-chown %s %s %s",  
                         pvfsEXELocation, ownerName, groupName, fileName);
            }
            else
            {
                snprintf(cmd, sizeof(cmd), "sudo %spvfs2-chown %s %s %s >/dev/null 2>&1",  
                         pvfsEXELocation, ownerName, groupName, fileName);
            }
        }
        else
        {
            if(verbose)
            {
                snprintf(cmd, sizeof(cmd), "%spvfs2-chown %s %s %s", 
                         pvfsEXELocation, ownerName, groupName, fileName);
            }
            else
            {
                snprintf(cmd, sizeof(cmd), "%spvfs2-chown %s %s %s >/dev/null 2>&1", 
                         pvfsEXELocation, ownerName, groupName, fileName);
            }
        }
    }
    else
    {
        /* Determine if we need to use sudo to change owner/group */
        if(geteuid() != owner_id ||
           geteuid() != group_id)
        {
            if(verbose)
            {
                snprintf(cmd, sizeof(cmd), "sudo chown %s:%s %s", 
                         ownerName, groupName, fileName);
            }
            else
            {
                snprintf(cmd, sizeof(cmd), "sudo chown %s:%s %s >/dev/null 2>&1", 
                         ownerName, groupName, fileName);
            }
        }
        else
        {
            if(verbose)
            {
                snprintf(cmd, sizeof(cmd), "chown %s:%s %s", 
                         ownerName, groupName, fileName);
            }
            else
            {
                snprintf(cmd, sizeof(cmd), "chown %s:%s %s >/dev/null 2>&1", 
                         ownerName, groupName, fileName);
            }
        }
    }
  
    ret = system(cmd);
    if(ret != 0)
    {
        if(use_pvfs2_lib)
        {
            if(verbose)
            {
                print_error("\tUnable to change owner of [%s] to [%s].[%s]\n"
                            "\t\tError Number = [%d]\n"
                            "\t\tError Desc   = [%s]\n",
                            fileName,
                            ownerName,
                            groupName,
                            errno,
                            strerror(errno));
            }
        }
        else
        {
            if(verbose)
            {
                print_error("\tUnable to change owner of [%s] to [%d]\n"
                            "\t\tError Number = [%d]\n"
                            "\t\tError Desc   = [%s]\n",
                            fileName,
                            owner_id,
                            errno,
                            strerror(errno));
            }          
        }
        return(TEST_COMMON_FAIL);
    }      
    return(TEST_COMMON_SUCCESS);
}

/**
 * \retval  0  SUCCESS
 * \retval  errno FAILURE using VFS layer
 * \retval  -ENODATA FAILURE using PVFS2 API
 */
int change_group(
    const char * fileName,  /**< Target Name */
    const uid_t  group_id,  /**< The gid of the new group */
    const int    use_pvfs2_lib, /**< determines use of pvfs2 library */
    const int    verbose)   /**< Turns on verbose prints if set to non-zero value*/
{
    int ret=0;
   
    if(verbose) { printf("\tChanging group on [%s] to [%o]\n", fileName, group_id); }
   
    if(use_pvfs2_lib)
    {
        if(verbose)
        {
            printf("\tchange_group not appropriate for PVFS2 API. call change_owner\n");
        }
        return(-ENODATA);
    }
    else
    {
        ret = chown(fileName, -1, group_id);

        if(ret == -1)
        {
            ret = errno; /* save the error number */
            if(verbose)
            {
                print_error("\tUnable to change group of [%s] to [%d]\n"
                            "\t\tError Number = [%d]\n"
                            "\t\tError Desc   = [%s]\n",
                            fileName,
                            group_id,
                            errno,
                            strerror(errno));
            }
        }      
    }
  
    return ret;      
}

void print_stats(
    const struct stat stats, /**< Structure contains file stats */
    const int verbose)       /**< Turns on verbose prints if set to non-zero value */
{
    char a_time[100], m_time[100], c_time[100];
    struct passwd * user;
    struct group  * gid;

    if(verbose)
    {
        snprintf(a_time, sizeof(a_time), "%s", ctime(&stats.st_atime));
        snprintf(m_time, sizeof(m_time), "%s", ctime(&stats.st_mtime));
        snprintf(c_time, sizeof(c_time), "%s", ctime(&stats.st_ctime));
      
        a_time[strlen(a_time)-1] = 0;
        m_time[strlen(m_time)-1] = 0;
        c_time[strlen(c_time)-1] = 0;
      
        user  = getpwuid(stats.st_uid);
        gid   = getgrgid(stats.st_gid);
      
        printf("\t st_size    = %Lu\n",      stats.st_size);
        printf("\t st_ino     = %Lu\n",      stats.st_ino);
        printf("\t atime      = %lu (%s)\n", stats.st_atime, a_time);
        printf("\t mtime      = %lu (%s)\n", stats.st_mtime, m_time);
        printf("\t ctime      = %lu (%s)\n", stats.st_ctime, c_time);
        printf("\t st_mode    = 0%o\n",      stats.st_mode);
        printf("\t st_uid     = %d (%s)\n",  stats.st_uid, user->pw_name);
        printf("\t st_gid     = %d (%s)\n",  stats.st_gid, gid->gr_name);
    }
}

/**
 * \retval  0  SUCCESS
 * \retval  -errno FAILURE using PVFS2 API
 */
int pvfs2_open(
    const char * fileName, /**< File Name         */
    const int accessFlags, /**< open LDAP access flags */
    const int mode,        /**< open LDAP mode flags   */
    const int verbose,     /**< Turns on verbose prints if set to non-zero value */
    const int followLink,  /**< Follow symbolic link to target */
    struct file_ref * pstFileRef)  /**< File descriptor (or handle) for an open file */
{
    int                  ret=0;
    char                 szPvfsPath[PVFS_NAME_MAX] = "";
    PVFS_fs_id           fs_id;
    PVFS_credentials     credentials;
    PVFS_sysresp_lookup  resp_lookup;

    /* Initialize memory */
    memset(&fs_id,        0, sizeof(fs_id));
    memset(&credentials,  0, sizeof(credentials));
    memset(&resp_lookup,  0, sizeof(resp_lookup));

    ret = PVFS_util_resolve(fileName, 
                            &fs_id, 
                            szPvfsPath, 
                            sizeof(szPvfsPath));

    if (ret < 0)
    {
        print_error("Error: could not find file system for [%s] in pvfstab\n", fileName);
        return(ret);
    }

    PVFS_util_gen_credentials(&credentials);

    if(followLink)
    {
        ret = PVFS_sys_lookup(fs_id, 
                              szPvfsPath, 
                              &credentials, 
                              &resp_lookup, 
                              PVFS2_LOOKUP_LINK_FOLLOW);
    }
    else
    {
        ret = PVFS_sys_lookup(fs_id, 
                              szPvfsPath, 
                              &credentials, 
                              &resp_lookup, 
                              PVFS2_LOOKUP_LINK_NO_FOLLOW);
    }

    if( (ret < 0) && 
        (ret != -PVFS_ENOENT))
    {
        PVFS_perror("PVFS_sys_lookup", ret);
        return(ret);
    }
   
    /* If the file doesn't exist, and the O_CREAT flag is specified */
    if( (ret == -PVFS_ENOENT) &&
        (accessFlags & O_CREAT))
    {
        ret = pvfs2_create_file(szPvfsPath,
                                fs_id,
                                &credentials,
                                mode,
                                verbose,
                                pstFileRef);

        if(ret != 0)
        {
            print_error("Unable to create [%s]\n", fileName);
            return(ret);
        }                              
    }

    return ret;
}


/**
 * \retval  0  SUCCESS
 * \retval  -errno FAILURE using PVFS2 API
 */
int pvfs2_create_file(const char             * fileName,    /**< File Name */
                      const PVFS_fs_id         fs_id,       /**< PVFS2 files sytem ID for the fileName parm */
                      const PVFS_credentials * credentials, /**< Struct with user/group permissions for operation */
                      const int                mode,        /**< open mode flags */
                      const int                verbose,     /**< Turns on verbose prints if set to non-zero value */
                      struct file_ref        * pstFileRef)  /**< File descriptor (or handle) for an open file*/
{
    int                 ret=0;
    char                szParentDir[PVFS_NAME_MAX] = "";
    char                szBaseName[PVFS_NAME_MAX]  = "";
    char              * parentDirectory            = NULL;
    char              * baseName                   = NULL;
    PVFS_sysresp_create resp_create;
    PVFS_object_ref     parent_ref;
    PVFS_sys_attr       attr;
    PVFS_sysresp_lookup resp_lookup;

    memset(&resp_create, 0, sizeof(resp_create));
    memset(&resp_lookup, 0, sizeof(resp_lookup));
    memset(&parent_ref,  0, sizeof(parent_ref));
    memset(&attr,        0, sizeof(attr));
   
    attr.owner = credentials->uid; 
    attr.group = credentials->gid;
    attr.perms = PVFS2_translate_mode(mode);
    attr.atime = time(NULL);
    attr.mtime = attr.atime;
    attr.ctime = attr.atime;
    attr.mask  = PVFS_ATTR_SYS_ALL_SETABLE; /* All things setable after file 
                                            * creation 
                                            */

    /* Copy the file name into structures to be passed to dirname and basename
     * These calls change the parameter, so we don't want to mess with original
     */
    strcpy(szParentDir, fileName);
    strcpy(szBaseName,  fileName);
   
    parentDirectory = dirname(szParentDir);
    baseName  = basename(szBaseName);

    ret = PVFS_sys_lookup(fs_id, 
                          parentDirectory, 
                          (PVFS_credentials *) credentials, 
                          &resp_lookup, 
                          PVFS2_LOOKUP_LINK_FOLLOW);
  
    if (ret < 0)
    {
        PVFS_perror("PVFS_sys_lookup", ret);
        return(ret); 
    }
   
    parent_ref.handle = resp_lookup.ref.handle;
    parent_ref.fs_id  = fs_id;

    /* Copy the file name into structures to be passed to dirname and basename
     * These calls change the parameter, so we don't want to mess with original
     */
    strcpy(szParentDir, fileName);
    strcpy(szBaseName,  fileName);
   
    parentDirectory = dirname(szParentDir);
    baseName  = basename(szBaseName);
    
    ret = PVFS_sys_create(baseName, 
                          parent_ref,     /* handle & fs_id of parent  */
                          attr, 
                          (PVFS_credentials *) credentials,
                          NULL,           /* Accept default distribution for fs */
                          &resp_create);

    if (ret < 0)
    {
        PVFS_perror("PVFS_sys_create", ret); 
        return(ret); 
    }

    pstFileRef->handle = resp_create.ref.handle;
   
    return(ret);
}

void copy_pvfs2_to_stat(const PVFS_sys_attr * attr,
                        struct stat         * fileStats)
{
    /* We blindly say we have all the data in the attr struct without checking 
    * for validity against the masks 
    */
    memcpy(&fileStats->st_atime, &attr->atime, sizeof(fileStats->st_atime));
    memcpy(&fileStats->st_mtime, &attr->mtime, sizeof(fileStats->st_mtime));
    memcpy(&fileStats->st_ctime, &attr->ctime, sizeof(fileStats->st_ctime));
    memcpy(&fileStats->st_uid,   &attr->owner, sizeof(fileStats->st_uid));
    memcpy(&fileStats->st_gid,   &attr->group, sizeof(fileStats->st_gid));
    memcpy(&fileStats->st_mode,  &attr->perms, sizeof(fileStats->st_mode));   
}

/**
 * \retval  0  SUCCESS
 * \retval  -1 Generic FAILURE
 * \retval  -errno FAILURE using PVFS2 API
 */
int lookup_parent(char             * filename,    /**< File Name */
                  PVFS_fs_id         fs_id,       /**< PVFS2 files sytem ID for the fileName parm */
                  PVFS_credentials * credentials, /**< Struct with user/group permissions for operation */
                  PVFS_handle      * handle,      /**< PVFS2 handle  */
                  int                verbose)     /**< Turns on verbose prints if set to non-zero value */
{
    int                 ret=0;
    char                szSegment[PVFS_SEGMENT_MAX] = "";
    PVFS_sysresp_lookup resp_look;

    memset(&resp_look, 0, sizeof(PVFS_sysresp_lookup));

    if ( get_base_dir(filename, szSegment, sizeof(szSegment)) )
    {
        if (filename[0] != '/' && verbose)
        {
            print_error("Invalid filename [%s] (no leading '/')\n", filename);
        }
        *handle = PVFS_HANDLE_NULL;
        return TEST_COMMON_FAIL;
    }

    ret = PVFS_sys_lookup(fs_id, 
                          szSegment, 
                          credentials, 
                          &resp_look, 
                          PVFS2_LOOKUP_LINK_FOLLOW);
    if (ret < 0)
    {
        if(verbose)
        {
            print_error("Lookup failed on [%s]\n", szSegment);
        }
        *handle = PVFS_HANDLE_NULL;
        return(ret);
    }

    *handle = resp_look.ref.handle;
    return 0;
}

/**
 * \retval TEST_COMMON_SUCCESS Success
 * \retval TEST_COMMON_FAIL     Failure 
 * \note
 * Example inputs and outputs/return values:
 * 
 * pathname: /tmp         - out_base_dir: /         - returns  0
 * pathname: /tmp/foo     - out_base_dir: /tmp      - returns  0
 * pathname: /tmp/foo/bar - out_base_dir: /tmp/foo  - returns  0
 * 
 * invalid pathname input examples:
 * pathname: /            - out_base_dir: undefined - returns -1
 * pathname: NULL         - out_base_dir: undefined - returns -1
 * pathname: foo          - out_base_dir: undefined - returns -1
 */
int get_base_dir(
    char * pathName,    /**< absolute path name */
    char * baseDir,     /**< pointer to memory to place discovered base directory */
    int    baseDirSize) /**< maximum size base directory could be */
{
    int    ret = TEST_COMMON_SUCCESS,
           len = 0;
    char * start = NULL,   
         * end   = NULL;

    /* Validate parameters */
    if (pathName && baseDir && baseDirSize)
    {
        if ( (strcmp(pathName,"/") == 0) || 
             (pathName[0]          != '/') )
        {
            return TEST_COMMON_FAIL;
        }

        start = pathName;
        end = (char *)(pathName + strlen(pathName));

        while( end              && 
               (end > start) && 
               (*(--end) != '/') );

        /*
          get rid of trailing slash unless we're handling
          the case where parent is the root directory
          (in root dir case, len == 1)
        */
        len = ++end - start;
        if (len != 1)
        {
            len--;
        }
        if (len < baseDirSize)
        {
            memcpy(baseDir, start, len);
            baseDir[len] = '\0';
        }
    }
    else
    {
        ret = TEST_COMMON_FAIL;   
    }
    return ret;
}

/**
 * \retval TEST_COMMON_SUCCESS Success
 * \retval TEST_COMMON_FAIL Failure 
 * \note
 * Example inputs and outputs/return values:                     
 *                                                               
 * pathname: /tmp/foo     - out_base_dir: foo       - returns  0 
 * pathname: /tmp/foo/bar - out_base_dir: bar       - returns  0 
 *                                                               
 * invalid pathname input examples:                              
 * pathname: /            - out_base_dir: undefined - returns -1 
 * pathname: NULL         - out_base_dir: undefined - returns -1 
 * pathname: foo          - out_base_dir: undefined - returns -1 
 * 
 */
int remove_base_dir(
    char * pathName,    /**< absolute path name */
    char * baseDir,     /**< pointer to memory to place discovered base directory */
    int    baseDirSize) /**< maximum size base directory could be */
{
    int    len       = 0;
    char * start  = NULL,   
         * end    = NULL,
         * endRef = NULL;

    /* Validate parameters */
    if (pathName && baseDir && baseDirSize)
    {
        if ( (strcmp(pathName, "/") == 0) || 
             (pathName[0]           != '/') )
        {
            return TEST_COMMON_FAIL;
        }

        start = pathName;
        end = (char *) (pathName + strlen(pathName));
        endRef = end;

        while( end             &&  
              (end > start) && 
              (*(--end) != '/') );

        len = endRef - ++end;
        if (len < baseDirSize)
        {
            memcpy(baseDir, end, len);
            baseDir[len] = '\0';
        }
    }
    else
    {
        return TEST_COMMON_FAIL;
    }
    
    return TEST_COMMON_SUCCESS;
}

/**
 * \retval  0  SUCCESS
 * \retval  errno FAILURE using VFS layer
 * \retval  -ENODATA FAILURE using PVFS2 API
*/
int create_symlink(
    const char * linkName,   /**< The link name to create */
    const char * linkTarget, /**< The string the link will contain */
    const int    use_pvfs2_lib,  /**< determines use of pvfs2 library */
    const int    verbose)    /**< Turns on verbose prints if set to non-zero value */
{
    int  ret=0;
    char cmd[PATH_MAX] = "";
    
    if(verbose) { printf("\tCreating symlink [%s] to [%s]:\n", linkName, linkTarget); }
    
    if(use_pvfs2_lib)
    {
        if(verbose)
        {
            snprintf(cmd, sizeof(cmd), "%spvfs2-ln -s %s %s", 
                     pvfsEXELocation, linkTarget, linkName);
        }
        else
        {
            snprintf(cmd, sizeof(cmd), "%spvfs2-ln -s %s %s >/dev/null 2>&1", 
                     pvfsEXELocation, linkTarget, linkName);
        }
        ret = system(cmd);
        if(ret != 0)
        {
            ret = -ENODATA;
            if(verbose)
            {
                print_error("\tUnable to create symlink [%s] to [%s]\n",
                            linkName,
                            linkTarget);      
            }
        }
    }
    else
    {
        ret = symlink(linkTarget, linkName);
        if(ret != 0)
        {
            ret = errno;
            if(verbose)
            {
                print_error("\tUnable to create symlink [%s] to [%s]\n"
                            "\t\tError Number = [%d]\n"
                            "\t\tError Desc   = [%s]\n",
                            linkName,
                            linkTarget,
                            errno,
                            strerror(errno));      
            }
        }
    }
    return ret; 
}

/**
 * Parses command line options for the most common arguments 
 * /retval TEST_COMMON_SUCCESS Indicates successfull return
 * /retval TEST_COMMON_FAIL    Indicates failure return
 */
int parse_common_args(
    int argc,                    /**< Number of arguments */
    char** argv,                 /**< argument list */
    struct common_options* opts) /**< structure to hold the common arguments */
{
    int ret          = 0;
    int option_index = 0;
    char *cur_option = NULL;

    static struct option long_opts[] = 
    {
        {"help",0,0,0},
        {"directory",1,0,0},
        {"use-lib",0,0,0},
        {"print-results",0,0,0},
        {"verbose",0,0,0},
        {"network-proto",1,0,0},
        {"port",1,0,0},
        {"fs-name",1,0,0},
        {"hostname",1,0,0},
        {"exe-path",1,0,0},
        {0,0,0,0}
    };

    memset(opts, 0, sizeof(struct common_options));

    while((ret = getopt_long_only(argc, argv, "", long_opts, &option_index)) != -1)
    {
        switch (ret)
        {
            case 0:
                 cur_option = (char*)long_opts[option_index].name;
   
                 if(strcmp("help", cur_option) == 0)
                 {
                     display_common_usage(argv[0]);
                     return(TEST_COMMON_FAIL);
                 }
                 if(strcmp("verbose", cur_option) == 0)
                 {
                     opts->verbose      = 1;
                     opts->printResults = 1;
                 }
                 if(strcmp("print-results", cur_option) == 0)
                 {
                     opts->printResults = 1;
                 }
                 if(strcmp("directory", cur_option) == 0)
                 {
                     opts->directory = (char*) malloc(strlen(optarg)+1);
                     assert(opts->directory);
                     strcpy(opts->directory, optarg);
                 }
                 if(strcmp("hostname", cur_option) == 0)
                 {
                     opts->hostname = (char*) malloc(strlen(optarg)+1);
                     assert(opts->hostname);
                     strcpy(opts->hostname, optarg);
                 }
                 if(strcmp("fs-name", cur_option) == 0)
                 {
                     opts->fsname = (char*) malloc(strlen(optarg)+1);
                     assert(opts->fsname);
                     strcpy(opts->fsname, optarg);
                 }
                 if(strcmp("network-proto", cur_option) == 0)
                 {
                     opts->networkProto = (char*) malloc(strlen(optarg)+1);
                     assert(opts->networkProto);
                     strcpy(opts->networkProto, optarg);
                 }
                 if(strcmp("exe-path", cur_option) == 0)
                 {
                     opts->exePath = (char*) malloc(strlen(optarg)+1);
                     assert(opts->exePath);
                     strcpy(opts->exePath, optarg);
                 }
                 if(strcmp("port", cur_option) == 0)
                 {
                    opts->port = atoi(optarg);
                 }
                 if(strcmp("use-lib", cur_option) == 0)
                 {
                     opts->use_pvfs2_lib = 1;
                 }
                 break;
        }
    }
   
    if(opts->directory == NULL)
    {
        display_common_usage(argv[0]);
        return(TEST_COMMON_FAIL);
    }

    /* defaults values for options */
    if(opts->fsname == NULL)
    {
        opts->fsname = (char *) malloc(strlen("pvfs2-fs")+1);
        strncpy(opts->fsname, "pvfs2-fs", strlen("pvfs2-fs"));
    }
    if(opts->port == 0)
    {
        opts->port=3334;
    }
    if(opts->networkProto == NULL)
    {
        opts->networkProto= (char *) malloc(strlen("tcp")+1);
        strncpy(opts->networkProto, "tcp", strlen("tcp"));
    }
    if(opts->exePath != NULL)
    {
        strcat(opts->exePath, "/"); /* Make sure it ends with "/" */
        ret = set_util_path(opts->exePath);
        
        if(ret != TEST_COMMON_SUCCESS)
        {
            print_error("Unable to set executable path location to [%s].\n", opts->exePath);
            exit(1);
        }
    }

    if(opts->verbose)
    {
        printf("Common Options: "
               "\t--directory      = [%s]\n" 
               "\t--use-lib        = [%d]\n" 
               "\t--print-results  = [%d]\n"
               "\t--verbose        = [%d]\n"
               "\t--network-proto  = [%s]\n"
               "\t--port           = [%d]\n"
               "\t--fs-name        = [%s]\n"
               "\t--hostname       = [%s]\n"
               "\t--exe-path       = [%s]\n",
               (opts->directory==NULL)?"NULL":opts->directory,
               opts->use_pvfs2_lib,
               opts->printResults,
               opts->verbose,
               opts->networkProto,
               opts->port,
               opts->fsname,
               opts->hostname,
               (opts->exePath==NULL)?"NULL":opts->exePath);
    }
    return(TEST_COMMON_SUCCESS);
}

/** Displays common argument lists */
void display_common_usage(char* exeName)
{
    fprintf(stderr, "%s:\n", exeName);
    fprintf(stderr, "COMMON mandatory arguments:\n"
        "  --directory     : name of file/directory to open\n");
    fprintf(stderr, "COMMON optional arguments:\n"
        "  --help          : prints help\n"
        "  --verbose       : turns on verbose output\n"
        "  --print-results : Always prints results (PASS or FAIL)\n"
        "  --use-lib       : uses the pvfs2 API instead of the kernel module\n"
        "  --hostname      : PVFS2 metaserver\n"
        "  --fs-name       : PVFS2 filesystem name\n"
        "  --network-proto : PVFS2 network protocol to use. Defaults to tcp\n"
        "  --port          : PVFS2 port for filesystem. Defaults to 3334\n"
        "  --exe-path      : Path where PVFS2 utilities reside\n");
}
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

