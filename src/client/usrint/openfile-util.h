/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include "posix-ops.h"

/** \file
 *  \ingroup usrint
 *
 *  PVFS2 user interface routines
 */
#ifndef OPENFILE_UTIL_H
#define OPENFILE_UTIL_H 1

//Define success and error return values
#define PVFS_FD_SUCCESS 0
#define PVFS_FD_FAILURE -1

extern int split_pathname(const char *path, 
		   int dirflag,
		   char **directory,
		   char **filename);

int pvfs_ucache_enabled(void);

extern void pvfs_sys_init(void); 

extern char *pvfs_qualify_path(const char *path);

extern int is_pvfs_path(const char *path); 

extern void pvfs_debug(char *fmt, ...); 

extern void load_glibc(void); 

extern int pvfs_lookup_dir(const char *directory,
                           PVFS_object_ref *ref,
                           int *fs_id);

extern int pvfs_lookup_file(const char *filename,
                            int fs_id,
                            PVFS_object_ref parent_ref,
                            int follow_links,
                            PVFS_object_ref *ref);

extern pvfs_descriptor *pvfs_alloc_descriptor(posix_ops *fsops, 
                                              int fd, 
                                              PVFS_object_ref *file_ref,
                                              int use_cache);

extern pvfs_descriptor *pvfs_find_descriptor(int fd);

extern int pvfs_dup_descriptor(int oldfd, int newfd);

extern int pvfs_free_descriptor(int fd);

extern int pvfs_descriptor_table_size(void);

extern int pvfs_create_file(const char *filename,
                            mode_t mode,
                            PVFS_object_ref parent_ref,
                            PVFS_object_ref *ref);

extern void PINT_initrand(void);

extern long int PINT_random(void);
#endif
