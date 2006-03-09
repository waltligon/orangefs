/*
 * Copyright (c) Acxiom Corporation, 2005 
 *
 * See COPYING in top-level directory
 */

/** 
 *
 * This file provides wrapper functions for things that are common across many 
 * of the test programs. Each call can execute via the kernel module or the 
 * PVFS2 API, depending on the use_pvfs2_lib flag.
 *
 * @{
 */

/** \file
 * Declarations for the test-common interface.
 */

#ifndef __TEST_COMMON_H
#define __TEST_COMMON_H

#include <sys/types.h>
#include <unistd.h>
#include <grp.h>
#include <pwd.h>

#include <pvfs2.h>

/** Encapsulates the file descriptor. PVFS2 uses file handles (64 bit integer).
 *  Key off the use_pvfs2_lib flag to determine which value to use for the file 
 *  descriptor. 
 */
struct file_ref
{
   int         fd;     /**< Used for all VFS layer calls */
   PVFS_handle handle; /**< Used for all PVFS2 API calls */
};

struct common_options
{
    char * directory;   /**< --directory      argument
                          * Root directory for tests */
    int    verbose;      /**< --verbose       option. 
                           * Turns on verbose logging. Also turns on --print-results */
    int    printResults; /**< --print-results option. 
                           * Always print results of tests                  */
    int    use_pvfs2_lib;/**< --use-lib       option. 
                           * Enables use of PVFS2 API. Defaults to zero     */
    char * hostname;     /**< --hostname      option. 
                           * PVFS2 metasever hostname. Defaults to NULL     */
    char * fsname;       /**< --fs-name       option. 
                           * PVFS2 filesystem.         Defaults to pvfs2-fs */
    int    port;         /**< --port          option. 
                           * PVFS2 metaserver port.    Defaults to 3334     */
    char * networkProto; /**< --network-proto option. 
                           * PVFS2 network protocol.   Defaults to "tcp"    */ 
    char * exePath;      /**< --exe-path      option. 
                           * Path location of PVFS2 utils. Defaults to NULL */
};

#define TEST_COMMON_SUCCESS 0  /**< Generic success return code */
#define TEST_COMMON_FAIL   -1 /**< Generic failure return code */

int set_util_path(const char * utility_path);

int create_pvfs2tab_file(char* pvfs2tab_name,
                         int len,
                         const int   port,
                         const char* networkProto, 
                         const char* hostname,
                         const char* fsname,
                         const char* fs_file);

int destroy_pvfs2tab_file(const char* pvfs2tab_name);

 /* Definition of the print_error macro */
#define print_error(format...)                                    \
   fprintf(stderr, "----------------------------------------\n"); \
   fprintf(stderr, format);                                       \
   fprintf(stderr, "\n");                                         \
   fprintf(stderr, "Function : %s\n", __FUNCTION__);              \
   fprintf(stderr, "File     : %s\n", __FILE__);                  \
   fprintf(stderr, "Line     : %d\n", __LINE__);                  \
   fprintf(stderr, "----------------------------------------\n");

int initialize(const int use_pvfs2_lib, const int verbose);

int is_pvfs2(const char * fileName, 
             PVFS_fs_id * cur_fs,
             char       * relativeName,
             const int    relativeNameSize,
             const int    use_pvfs2_lib, 
             const int    verbose);

int finalize(int use_pvfs2_lib);

int close_file(struct file_ref *stFileRef, 
               const int        use_pvfs2_lib, 
               const int        verbose);

int stat_file(const char  * fileName,
              struct stat * fileStats, 
              const int     followLink,
              const int     use_pvfs2_lib, 
              const int     verbose);

int pvfs2_open(const char      * fileName, 
               const int         accessFlags, 
               const int         mode,
               const int         verbose,
               const int         followLink,
               struct file_ref * stFileRef);

int pvfs2_create_file(const char             * fileName,
                      const PVFS_fs_id         fs_id, 
                      const PVFS_credentials * credentials,
                      const int                mode,
                      const int                verbose,
                      struct file_ref        * pstFileRef);
                      
int open_file(const char      * fileName, 
              const int         accessFlags, 
              const int         mode,
              const int         use_pvfs2_lib,
              const int         verbose,
              const int         followLink,
              struct file_ref * stFileRef);
              
int create_directory(const char * directory,
                     const int    mode,
                     const int    use_pvfs2_lib,
                     const int    verbose);

int remove_directory(const char * directory, 
                     const int    use_pvfs2_lib,
                     const int    verbose);

int create_file(const char * fileName,
                const int    mode,
                const int    use_pvfs2_lib,
                const int    verbose);

int remove_file(const char * fileName, 
                const int    use_pvfs2_lib,
                const int    verbose);

int remove_symlink(const char * linkName, 
                   const int    use_pvfs2_lib,
                   const int    verbose);

int change_mode(const char * fileName, 
                const int    mode,
                      int*   error_code,
                const int    use_pvfs2_lib,
                const int    verbose);

int change_owner(const char * fileName,        
                 const char * ownerName,   
                 const uid_t  owner_id,       
                 const char * groupName,
                 const gid_t  group_id,
                 const int    use_pvfs2_lib,
                 const int    verbose);

int change_group(const char * fileName,
                 const uid_t  group_id,
                 const int    use_pvfs2_lib,
                 const int    verbose);

void print_stats(const struct stat stats, const int verbose);

int lookup_parent(char             * filename,
                  PVFS_fs_id         fs_id,
                  PVFS_credentials * credentials,
                  PVFS_handle      * handle,
                  int                verbose);

int get_base_dir(char * pathName, 
                 char * baseDir, 
                 int    baseDirSize);

int remove_base_dir(char * pathName,
                    char * baseDir,
                    int    baseDirSize);

int create_symlink(const char * linkName, 
                   const char * linkTarget,
                   const int    use_pvfs2_lib,
                   const int    verbose);

int parse_common_args(int argc, char** argv, struct common_options* opts);

#endif /* __TEST_COMMON_H */

/* @} */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
