/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

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

int split_pathname( const char *path, char **directory, char **filename);

int pvfs_sys_init(); 

char *pvfs_qualify_path(const char *path);

int is_pvfs_path(const char *path); 

void pvfs_debug(char *fmt, ...); 

void load_glibc(void); 

int pvfs_split_pathname(const char *path,
                               char **directory,
                               char **filename); 

int pvfs_lookup_dir(const char *directory,
                           PVFS_object_ref *ref,
                           int *fs_id);

int pvfs_lookup_file(const char *filename,
                            int fs_id,
                            PVFS_object_ref parent_ref,
                            int follow_links,
                            PVFS_object_ref *ref);

pvfs_descriptor *pvfs_alloc_descriptor(posix_ops *fsops);

pvfs_descriptor *pvfs_find_descriptor(int fd);


int pvfs_create_file(const char *filename,
                            mode_t mode,
                            PVFS_object_ref parent_ref,
                            PVFS_object_ref *ref);
#endif
