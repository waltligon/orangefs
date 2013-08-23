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
#include <pvfs-path.h>

/* Define GNU's O_NOFOLLOW flag to be false if its not set */
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

/* Base pvfs file handle info:
 * native-lib and mpi-io both wrap around
 * pvfs_descriptor for their file table entries
 */

#define PVFS_NULL_OBJ ((PVFS_object_ref *)NULL)

/** A structure used in the cache enabled version of iocommon_readorwrite.
 */
struct ucache_req_s
{
    uint64_t ublk_tag; /* ucache block tag (byte index into file) */
    void *ublk_ptr; /* where in ucache memory to read block from or write to */
    uint16_t ublk_index; /* index of ucache block in shared memory segment */
};

struct ucache_copy_s
{
    void * cache_pos;
    void * buff_pos;
    size_t size;
    uint16_t blk_index;
};


/* this global is set when a pvfs specific error is returned
 * and errno is set to EIO
 */
extern int pvfs_errno;

/* prototypes */

/* Perform PVFS initialization if not already finished */
void iocommon_ensure_init(void);

int iocommon_cred(PVFS_credential **credential);

void calc_copy_ops( off64_t offset,
                    size_t req_size,
                    struct ucache_req_s *ureq,
                    struct ucache_copy_s *ucop,
                    int copy_count,
                    const struct iovec *vector
);

int calc_req_blk_cnt( uint64_t offset,
                      size_t req_size
);

size_t sum_iovec_lengths( size_t iovec_count,
                          const struct iovec *vector
);

unsigned char read_full_block_into_ucache( pvfs_descriptor *pd,
                                           PVFS_size offset,
                                           struct ucache_req_s *req,
                                           int req_index,
                                           uint64_t *fent_size,
                                           size_t *req_size,
                                           int *req_blk_cnt
);

extern int iocommon_fsync(pvfs_descriptor *pvfs_info);

int iocommon_expand_path (PVFS_path_t *Ppath,
                          int follow_flag, 
                          int flags,
                          mode_t mode,
                          pvfs_descriptor **pdp);

int iocommon_lookup(char *path,
                    int followflag,
                    PVFS_object_ref *pref,
                    PVFS_object_ref *fref,
                    char **filename,
                    PVFS_object_ref *pdir);

/*
 * Find the PVFS handle to an object (file, dir sym) 
 * assumes an absoluate path
 */
extern int iocommon_lookup_absolute(const char *abs_path,
                             int follow_flag,
                             PVFS_object_ref *ref,
                             char *error_path,
                             int error_path_size);

/*
 * Lookup a file via the PVFS system interface
 */
extern int iocommon_lookup_relative(const char *rel_path,
                             PVFS_object_ref parent_ref,
                             int follow_links,
                             PVFS_object_ref *ref,
                             char *error_path,
                             int error_path_size);

/*
 * Create a file via the PVFS system interface
 */
extern int iocommon_create_file(const char *filename,
			 mode_t file_permission,
			 PVFS_hint file_creation_param,
                         PVFS_object_ref parent_ref,
                         PVFS_object_ref *ref);


/* pvfs_open implementation, return file info in fd */
/* assumes path is fully qualified */
/* if pdir is not NULL, it is the parent directory */
extern pvfs_descriptor *iocommon_open(const char *pathname, int flag,
                               PVFS_hint file_creation_param,
                               mode_t file_permission,
                               pvfs_descriptor *pdir);

extern int iocommon_truncate(PVFS_object_ref file_ref,
                      off64_t length);

extern off64_t iocommon_lseek(pvfs_descriptor *pd,
                       off64_t offset,
                       PVFS_size unit_size,
                       int whence);

/*
 * pvfs_unlink implementation
 * need to verify this is a file or symlink
 * use rmdir for directory
 */
extern int iocommon_remove (const char *pathname, PVFS_object_ref *pdir, int dirflag);

extern int iocommon_unlink(const char *pathname, PVFS_object_ref *pdir);

extern int iocommon_rmdir(const char *pathname, PVFS_object_ref *pdir);

/* if dir(s) are NULL, assume name is absolute */
extern int iocommon_rename(PVFS_object_ref *oldpdir,
                         const char *oldname,
                         PVFS_object_ref *newpdir,
                         const char *newname);

/* R/W Wrapper Functions, possibly utilizing user cache
 * do a blocking read or write
 */
extern int iocommon_readorwrite(enum PVFS_io_type which,
	                 pvfs_descriptor *pd,
                         PVFS_size offset,
                         size_t iovec_count,
                         const struct iovec *vector);

extern int iocommon_vreadorwrite(enum PVFS_io_type which,
	                 PVFS_object_ref *por,
                         PVFS_size offset,
                         size_t count,
                         const struct iovec *vector);

extern int iocommon_ireadorwrite(enum PVFS_io_type which,
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
 */
extern int iocommon_readorwrite_nocache(enum PVFS_io_type which,
		          PVFS_object_ref *por,
                          PVFS_size offset,
                          void *buf,
                          PVFS_Request mem_req,
                          PVFS_Request file_req);

/*
 * Do a nonblocking read or write
 * extra_offset = extra padding to the pd's offset,
 * independent of the pd's offset
 * Returns an op_id, response, and ret_mem_request
 * (which represents an etype_req*count region)
 * Note that the none of the PVFS_Requests are freed
 */
extern int iocommon_ireadorwrite_nocache(enum PVFS_io_type which,
		          pvfs_descriptor *pd,
                          PVFS_size extra_offset,
                          void *buf,
                          PVFS_Request etype_req,
                          PVFS_Request file_req,
                          size_t count,
		          PVFS_sys_op_id *ret_op_id,
                          PVFS_sysresp_io *ret_resp,
                          PVFS_Request *ret_memory_req);

extern int iocommon_getattr(PVFS_object_ref obj, PVFS_sys_attr *attr, uint32_t mask);

extern int iocommon_setattr(PVFS_object_ref obj, PVFS_sys_attr *attr);

extern int iocommon_stat(pvfs_descriptor *pd, struct stat *buf, uint32_t mask);

/*
 * The only difference here is that buf is stat64 which
 * means some of its fields are defined as different types
 */
extern int iocommon_stat64(pvfs_descriptor *pd, struct stat64 *buf, uint32_t mask);

extern int iocommon_statfs(pvfs_descriptor *pd, struct statfs *buf);

extern int iocommon_statfs64(pvfs_descriptor *pd, struct statfs64 *buf);

extern int iocommon_seteattr(pvfs_descriptor *pd, const char *key, const void *val, int size, int flag);

extern int iocommon_geteattr(pvfs_descriptor *pd, const char *key, void *val, int size);

extern int iocommon_atomiceattr(pvfs_descriptor *pd, const char *key, void *val, int valsize, void *response, int respsize, int flag, int opcode);

extern int iocommon_listeattr(pvfs_descriptor *pd, char *list, int size, int *numkeys);

extern int iocommon_deleattr(pvfs_descriptor *pd, const char * key);

extern int iocommon_chown(pvfs_descriptor *pd, uid_t owner, gid_t group);

extern int iocommon_getmod(pvfs_descriptor *pd, mode_t *mode);

extern int iocommon_chmod(pvfs_descriptor *pd, mode_t mode);

extern int iocommon_make_directory(const char *pvfs_path,
                            int mode,
                            PVFS_object_ref *pdir);

extern int iocommon_readlink(pvfs_descriptor *pd, char *buf, int size);

extern int iocommon_symlink(const char *pvfs_path,
                      const char *link_target,
                      PVFS_object_ref *pdir);

extern int iocommon_getdents(pvfs_descriptor *pd,
                      struct dirent *dirp,
                      unsigned int count);

extern int iocommon_getdents64(pvfs_descriptor *pd,
                      struct dirent64 *dirp,
                      unsigned int count);

extern int iocommon_access(const char *pvfs_path,
                      int mode,
                      int flags,
                      PVFS_object_ref *pdir);

extern int iocommon_sendfile(int sockfd,
                      pvfs_descriptor *pd,
                      off64_t *offset,
                      size_t count);


/* Functions in this file generally define a label errorout
 * for cleanup before exit and return an int rc which is -1
 * on error with the error code in errno, 0 on success.
 * IOCOMMON_RETURN_ERR checks a return code from a function
 * returns the same protocol and goto errorout: if less than 0
 * IOCOMMON_CHECK_ERR assumes the return code contains the
 * negative of the error code as encoded by PVFS sysint
 * functions and decodes these before jumping to errorout.
 * PVFS sysint calls always return error codes in the return
 * value, but system calls inside them might set errno to
 * a value that may or may not have meaning for the programmer
 * calling this library.  Steps are taken to ensure errno
 * is not modified unless the code in this lib wants to
 * modify it.  CHECK_ERR should be called after each sysint
 * call to correctly pass error codes.
 */
extern PVFS_error PINT_errno_mapping[];
#define IOCOMMON_RETURN_ERR(rc)                                 \
do {                                                            \
    if ((rc) < 0)                                               \
    {                                                           \
        goto errorout;                                          \
    }                                                           \
} while (0)

#define IOCOMMON_CHECK_ERR(rc)                                  \
do {                                                            \
    errno = orig_errno;                                         \
    if ((rc) < 0)                                               \
    {                                                           \
        if (IS_PVFS_NON_ERRNO_ERROR(-(rc)))                     \
        {                                                       \
            pvfs_errno = -rc;                                   \
            errno = EIO;                                        \
        }                                                       \
        else if (IS_PVFS_ERROR(-(rc)))                          \
        {                                                       \
            errno = PINT_errno_mapping[(-(rc)) & 0x7f];         \
        }                                                       \
        rc = -1;                                                \
        goto errorout;                                          \
    }                                                           \
} while (0)


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif
