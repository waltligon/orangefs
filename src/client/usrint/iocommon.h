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
#ifndef IOCOMMON_H
#define IOCOMMON_H 1

#include <pvfs2.h>
#include <pvfs2-types.h>
#include <pvfs2-request.h>
#include <pvfs2-debug.h>

/* Define GNU's O_NOFOLLOW flag to be false if its not set */
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

/* Base pvfs file handle info:
 * native-lib and mpi-io both wrap around
 * pvfs_descriptor for their file table entries
 */

#define PVFS_NULL_OBJ ((PVFS_object_ref *)NULL)

/* this global is set when a pvfs specific error is returned
 * and errno is set to EIO
 */
extern int pvfs_errno;

/* prototypes */

/* Perform PVFS initialization if not already finished */
void iocommon_ensure_init(void);

void iocommon_cred(PVFS_credentials **credentials);

int iocommon_fsync(pvfs_descriptor *pvfs_info);

/*
 * Find the PVFS handle to an object (file, dir sym) 
 * assumes an absoluate path
 */
int iocommon_lookup_absolute(const char *abs_path,
                             PVFS_object_ref *ref,
                             char *error_path,
                             int error_path_size);

/*
 * Lookup a file via the PVFS system interface
 */
int iocommon_lookup_relative(const char *rel_path,
                             PVFS_object_ref parent_ref,
                             int follow_links,
                             PVFS_object_ref *ref,
                             char *error_path,
                             int error_path_size);

/*
 * Create a file via the PVFS system interface
 */
int iocommon_create_file(const char *filename,
			 mode_t file_permission,
			 PVFS_hint file_creation_param,
                         PVFS_object_ref parent_ref,
                         PVFS_object_ref *ref);


/* pvfs_open implementation, return file info in fd */
/* assumes path is fully qualified */
/* if pdir is not NULL, it is the parent directory */
pvfs_descriptor *iocommon_open(const char *pathname, int flag,
                               PVFS_hint file_creation_param,
                               mode_t file_permission,
                               pvfs_descriptor *pdir);

int iocommon_truncate(PVFS_object_ref file_ref,
                      off64_t length);

off64_t iocommon_lseek(pvfs_descriptor *pd,
                       off64_t offset,
                       PVFS_size unit_size,
                       int whence);

/*
 * pvfs_unlink implementation
 * need to verify this is a file or symlink
 * use rmdir for directory
 */
int iocommon_remove (const char *pathname, PVFS_object_ref *pdir, int dirflag);

int iocommon_unlink(const char *pathname, PVFS_object_ref *pdir);

int iocommon_rmdir(const char *pathname, PVFS_object_ref *pdir);

/* if dir(s) are NULL, assume name is absolute */
int iocommon_rename(PVFS_object_ref *oldpdir, const char *oldname,
                    PVFS_object_ref *newpdir, const char *newname);

/* R/W Wrapper Functions, possibly utilizing user cache */
/* do a blocking read or write
 */
int iocommon_readorwrite(enum PVFS_io_type which,
	                 pvfs_descriptor *pd,
                         PVFS_size offset,
                         void *buf,
                         PVFS_Request mem_req,
                         PVFS_Request file_req,
                         size_t count,
                         const struct iovec *vector);

/* Read or write block data to/from cache */
int place_data(enum PVFS_io_type which, void *block, const struct iovec *vector, 
              int *iovec_ndx, unsigned char *scratch, void **scratch_ptr, 
                                                  uint64_t *scratch_left);


/* [Do a nonblocking read or write] */
int iocommon_ireadorwrite_nocache(enum PVFS_io_type which,
		          pvfs_descriptor *pd,
                          PVFS_size extra_offset,
                          void *buf,
                          PVFS_Request etype_req,
                          PVFS_Request file_req,
                          size_t count,
		          PVFS_sys_op_id *ret_op_id,
                          PVFS_sysresp_io *ret_resp,
                          PVFS_Request *ret_memory_req);


/* do a blocking read or write
 * extra_offset = extra padding to the pd's offset, independent of the pd's offset */
int iocommon_readorwrite_nocache(enum PVFS_io_type which,
		         pvfs_descriptor *pd,
                         PVFS_size offset,
                         void *buf,
                         PVFS_Request mem_req,
                         PVFS_Request file_req);
        //returned by nonblocking operations

/*
 * [Do a nonblocking read or write]
 * extra_offset = extra padding to the pd's offset,
 * independent of the pd's offset
 * Returns an op_id, response, and ret_mem_request
 * (which represents an etype_req*count region)
 * Note that the none of the PVFS_Requests are freed
 */
int iocommon_ireadorwrite_nocache(enum PVFS_io_type which,
		          pvfs_descriptor *pd,
                          PVFS_size extra_offset,
                          void *buf,
                          PVFS_Request etype_req,
                          PVFS_Request file_req,
                          size_t count,
		          PVFS_sys_op_id *ret_op_id,
                          PVFS_sysresp_io *ret_resp,
                          PVFS_Request *ret_memory_req);

int iocommon_getattr(PVFS_object_ref obj, PVFS_sys_attr *attr, uint32_t mask);

int iocommon_setattr(PVFS_object_ref obj, PVFS_sys_attr *attr);

int iocommon_stat(pvfs_descriptor *pd, struct stat *buf, uint32_t mask);

/*
 * The only difference here is that buf is stat64 which
 * means some of its fields are defined as different types
 */
int iocommon_stat64(pvfs_descriptor *pd, struct stat64 *buf, uint32_t mask);

int iocommon_statfs(pvfs_descriptor *pd, struct statfs *buf);

int iocommon_statfs64(pvfs_descriptor *pd, struct statfs64 *buf);

int iocommon_seteattr(pvfs_descriptor *pd, const char *key, const void *val, int size, int flag);

int iocommon_geteattr(pvfs_descriptor *pd, const char *key, void *val, int size);

int iocommon_listeattr(pvfs_descriptor *pd, char *list, int size, int *numkeys);

int iocommon_deleattr(pvfs_descriptor *pd, const char * key);

int iocommon_chown(pvfs_descriptor *pd, uid_t owner, gid_t group);

int iocommon_chmod(pvfs_descriptor *pd, mode_t mode);

int iocommon_make_directory(const char *pvfs_path,
                            int mode,
                            PVFS_object_ref *pdir);

int iocommon_readlink(pvfs_descriptor *pd, char *buf, int size);

int iocommon_symlink(const char *pvfs_path,
                     const char *link_target,
                     PVFS_object_ref *pdir);

int iocommon_getdents(pvfs_descriptor *pd,
                      struct dirent *dirp,
                      unsigned int count);

int iocommon_getdents64(pvfs_descriptor *pd,
                      struct dirent64 *dirp,
                      unsigned int count);

int iocommon_access(const char *pvfs_path,
                    int mode,
                    int flags,
                    PVFS_object_ref *pdir);

int iocommon_sendfile(int sockfd,
                      pvfs_descriptor *pd,
                      off64_t *offset,
                      size_t count);
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif
