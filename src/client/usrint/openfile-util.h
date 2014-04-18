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

#include "pvfs2-internal.h"
#include "posix-ops.h"

/* used in stio.c and openfile-util.c */
#define _P_IO_MAGIC 0xF0BD0000

/*Define success and error return values */
#define PVFS_FD_SUCCESS 0
#define PVFS_FD_FAILURE -1

/* Default attribute mask */
#define PVFS_ATTR_DEFAULT_MASK \
        (PVFS_ATTR_SYS_COMMON_ALL | PVFS_ATTR_SYS_SIZE |\
         PVFS_ATTR_SYS_BLKSIZE | PVFS_ATTR_SYS_LNK_TARGET |\
         PVFS_ATTR_SYS_DIRENT_COUNT)

int pvfs_ucache_enabled(void);

extern int pvfs_sys_init(void);

extern void pvfs_debug(char *fmt, ...); 

extern void load_glibc(void); 

extern char *pvfs_dpath_insert(const char *p);

extern void pvfs_dpath_remove(char *p);

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

extern int pvfs_dup_descriptor(int oldfd, int newfd, int flags, int fcntl_dup);

extern int pvfs_free_descriptor(int fd);

extern int pvfs_descriptor_table_size(void);

extern int pvfs_descriptor_table_next(int start);

extern int pvfs_put_cwd(char *buf, int size);

extern int pvfs_len_cwd(void);

extern int pvfs_get_cwd(char *buf, int size);

extern int pvfs_create_file(const char *filename,
                            mode_t mode,
                            PVFS_object_ref parent_ref,
                            PVFS_object_ref *ref);

extern void PINT_initrand(void);

extern long int PINT_random(void);

#endif
