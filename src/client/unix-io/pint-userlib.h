/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */

/* PVFS user library internal definitions */

#ifndef PVFS_USERLIB_H
#define PVFS_USERLIB_H

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <alloca.h>

#include "llist.h"
#include "pvfs2-sysint.h"
#include "pvfs2-attr.h"
#include "gen-locks.h"


#define PVFS_NR_OPEN 1024 /* No. of files open in PVFS */

/* FS TYPE DEFINES -- USED IN FDESC STRUCTUES */
#define FS_UNIX 0 /* UNIX file/directory */
#define FS_PVFS 1 /* PVFS file */
#define FS_NFS 2 /* PVFS file */
#define FS_RESV 3 /* reserved (socket connected to PVFS daemon) */
#define FS_PDIR 4 /* PVFS directory */

/* PVFS file descriptor structure */
struct fdesc {
    int32_t collid; /* Collection ID */
    int64_t off;	 /* File offset */
    int ftype;		 /* File type - UNIX/PVFS */
    PVFS_object_attr attr; /* File attributes */
    int32_t cap; 	 /* capability */
    int flag; 		 /* flags used to open file */
    PVFS_handle handle; /* PVFS file handle */
};
typedef struct fdesc fdesc, *fdesc_p;

/* PVFS File Info */
/*struct file_info {
	char *fname;
	PVFS_fs_id collid; 
};
typedef struct file_info finfo;
*/


/* Free List Node */
struct flist_node {
    PVFS_handle fd;
};
typedef struct flist_node flist_node;

/* The fd Management structure */
struct pvfs_fd_manage {
    llist_p *fd_list;		 /* Free list for fds */
    fdesc *fdesc_p;		 /* Array of ptrs for Fdesc structures */
    int nr_fds;  /* Number of files open */
    gen_mutex_t *fd_lock; /* Mutex Lock */
};
typedef struct pvfs_fd_manage pfd_manage;

/* Function Prototypes */
int parse_pvfstab(char *filename,pvfs_mntlist *pvfstab_p);
int search_pvfstab(char *fname,pvfs_mntlist mnt,pvfs_mntent *mntent);
void free_pvfstab_entry(pvfs_mntlist *e_p);
int pvfs_getfsid(const char *fname, int *result, char **abs_fname, 
		 PVFS_fs_id *collid);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif
