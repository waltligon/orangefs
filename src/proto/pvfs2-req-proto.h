/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_REQ_PROTO_H
#define __PVFS2_REQ_PROTO_H

#include "pvfs2-types.h"
#include "pvfs2-attr.h"
#include "pvfs-distribution.h"
#include "pvfs-request.h"

/* release number:
 * This is a base-10, 5 digit number, with one digit for the most
 * significant version number and two for the last two (e.g. 1.5.6 => 10506)
 */
#define PVFS_RELEASE_NR 19901

/* enumeration of supported server operations */
enum PVFS_server_op
{
    PVFS_SERV_INVALID = 0,
    PVFS_SERV_CREATE = 1,
    PVFS_SERV_REMOVE = 2,
    PVFS_SERV_IO = 3,
    PVFS_SERV_GETATTR = 4,
    PVFS_SERV_SETATTR = 5,
    PVFS_SERV_LOOKUP_PATH = 6,
    PVFS_SERV_CREATEDIRENT = 7,
    PVFS_SERV_RMDIRENT = 8,
    PVFS_SERV_TRUNCATE = 9,
    PVFS_SERV_MKDIR = 10,
    PVFS_SERV_READDIR = 11,
    PVFS_SERV_GETCONFIG = 12,
    PVFS_SERV_WRITE_COMPLETION = 13
    /* IMPORTANT: please remember to modify PVFS_MAX_SERVER_OP define (below)
     * if you add a new operation to this list
     */

    /* not implemented:
     * PVFS_SERV_NOOP 
     * PVFS_SERV_GETEATTR
     * PVFS_SERV_SETEATTR,
     * PVFS_SERV_BATCH
     * PVFS_SERV_EXTENSION
     * PVFS_SERV_STATFS
     */
};
#define PVFS_MAX_SERVER_OP 13

/******************************************************************/
/* these values define limits on the maximum size of variable length
 * parameters used within the request protocol
 */

/* max size of opaque distribution parameters */
#define PVFS_REQ_LIMIT_DIST_BYTES         1024
/* max size of each configuration file transmitted to clients */
#define PVFS_REQ_LIMIT_CONFIG_FILE_BYTES  16384
/* max size of all path strings */
#define PVFS_REQ_LIMIT_PATH_NAME_BYTES    PVFS_NAME_MAX
/* max size of strings representing a single path element */
#define PVFS_REQ_LIMIT_SEGMENT_BYTES      PVFS_SEGMENT_MAX
/* max total size of I/O request descriptions */
#define PVFS_REQ_LIMIT_IOREQ_BYTES        8192
/* max count of segments allowed per path lookup (note that this governs 
 * the number of handles and attributes returned in lookup_path responses)
 */
#define PVFS_REQ_LIMIT_PATH_SEGMENT_COUNT 256
/* max count of datafiles associated with a logical file */
#define PVFS_REQ_LIMIT_DFILE_COUNT        1024
/* max count of directory entries per readdir request */
#define PVFS_REQ_LIMIT_DIRENT_COUNT       64


/* create *********************************************************/
/* - used to create new metafile and datafile objects */

struct PVFS_servreq_create
{
    PVFS_fs_id fs_id;
    PVFS_ds_type object_type;	    /* type of object to create */

    /*
      an array of handle extents that we use to suggest to
      the server from which handle range to allocate for the
      newly created handle(s).  To request a single handle,
      a single extent with first = last should be used.
    */
    PVFS_handle_extent_array handle_extent_array;
};

struct PVFS_servresp_create
{
    PVFS_handle handle;
};

/* remove *****************************************************/
/* - used to remove an existing metafile or datafile object */

struct PVFS_servreq_remove
{
    PVFS_handle handle;		    /* handle of object to remove */
    PVFS_fs_id fs_id;		    /* file system */
};

#define PINT_SERVREQ_REMOVE_FILL(__req,		\
				 __creds,	\
				 __fsid,	\
				 __handle)	\
do {						\
    memset(&(__req), 0, sizeof(__req));		\
    (__req).op = PVFS_SERV_REMOVE;		\
    (__req).credentials = (__creds);		\
    (__req).u.remove.fs_id = (__fsid);		\
    (__req).u.remove.handle = (__handle);	\
} while (0)

/* NOTE: no response structure; all necessary response info is 
 * returned in generic server response structure
 */

/* getattr ****************************************************/
/* - retreives attributes based on mask of PVFS_ATTR_XXX values */

struct PVFS_servreq_getattr
{
    PVFS_handle handle;		    /* handle of target object */
    PVFS_fs_id fs_id;		    /* file system */
    uint32_t attrmask;		    /* mask of desired attributes */
};

#define PINT_SERVREQ_GETATTR_FILL(__req,	\
				  __creds,	\
				  __fsid,	\
				  __handle,	\
				  __amask)	\
do {						\
    memset(&(__req), 0, sizeof(__req));		\
    (__req).op = PVFS_SERV_GETATTR;		\
    (__req).credentials = (__creds);		\
    (__req).u.getattr.fs_id = (__fsid);		\
    (__req).u.getattr.handle = (__handle);	\
    (__req).u.getattr.attrmask = (__amask);	\
} while (0)

struct PVFS_servresp_getattr
{
    PVFS_object_attr attr;	    /* attributes */
};

/* setattr ****************************************************/
/* - sets attributes specified by mask of PVFS_ATTR_XXX values */

struct PVFS_servreq_setattr
{
    PVFS_handle handle;		    /* handle of target object */
    PVFS_fs_id fs_id;		    /* file system */
    PVFS_object_attr attr;	    /* new attributes */
};

/* NOTE: no response structure; all necessary response info is 
 * returned in generic server response structure
 */

/* lookup path ************************************************/
/* - looks up as many elements of the specified path as possible */

struct PVFS_servreq_lookup_path
{
    char* path;			    /* path name */
    PVFS_fs_id fs_id;		    /* file system */
    PVFS_handle starting_handle;    /* handle of path parent */
    /* mask of attribs to return with lookup results */
    uint32_t attrmask;
};

#define PINT_SERVREQ_LOOKUP_PATH_FILL(__req,		\
				      __creds,		\
				      __path,		\
				      __fsid,		\
				      __handle,		\
				      __amask)		\
do {							\
    memset(&(__req), 0, sizeof(__req));			\
    (__req).op = PVFS_SERV_LOOKUP_PATH;			\
    (__req).credentials = (__creds);			\
    (__req).u.lookup_path.path = (__path);		\
    (__req).u.lookup_path.fs_id = (__fsid);		\
    (__req).u.lookup_path.starting_handle = (__handle);	\
    (__req).u.lookup_path.attrmask = (__amask);		\
} while (0)

struct PVFS_servresp_lookup_path
{
    /* array of handles for each successfully resolved path segment */
    PVFS_handle *handle_array;	    
    /* array of attributes for each path segment (when available) */
    PVFS_object_attr *attr_array;
    uint32_t handle_count;	    /* # of handles returned */
    uint32_t attr_count;	    /* # of attributes returned */
};


/* mkdir *******************************************************/
/* - makes a new directory object */

struct PVFS_servreq_mkdir
{
    PVFS_fs_id fs_id;		    /* file system */
    PVFS_object_attr attr;	    /* initial attributes */

    /*
      an array of handle extents that we use to suggest to
      the server from which handle range to allocate for the
      newly created handle(s).  To request a single handle,
      a single extent with first = last should be used.
    */
    PVFS_handle_extent_array handle_extent_array;
};

struct PVFS_servresp_mkdir
{
    PVFS_handle handle;		    /* handle of new directory */
};


/* create dirent ***********************************************/
/* - creates a new entry within an existing directory */

struct PVFS_servreq_createdirent
{
    char *name;			    /* name of new entry */
    PVFS_handle new_handle;	    /* handle of new entry */
    PVFS_handle parent_handle;	    /* handle of directory */
    PVFS_fs_id fs_id;		    /* file system */
};

/* NOTE: no response structure; all necessary response info is 
 * returned in generic server response structure
 */

/* rmdirent ****************************************************/
/* - removes an existing directory entry */

struct PVFS_servreq_rmdirent
{
    char *entry;		    /* name of entry to remove */
    PVFS_handle parent_handle;	    /* handle of directory */
    PVFS_fs_id fs_id;		    /* file system */
};

struct PVFS_servresp_rmdirent
{
    PVFS_handle entry_handle;	    /* handle of removed entry */
};

/* readdir *****************************************************/
/* - reads entries from a directory */

struct PVFS_servreq_readdir
{
    PVFS_handle handle;		    /* handle of dir object */
    PVFS_fs_id fs_id;		    /* file system */
    PVFS_ds_position token;	    /* dir offset */
    uint32_t dirent_count;	    /* desired # of entries */
};

struct PVFS_servresp_readdir
{
    PVFS_ds_position token;	    /* new dir offset */
    /* array of directory entries */
    PVFS_dirent *dirent_array;
    uint32_t dirent_count;	    /* # of entries retrieved */
};

/* getconfig ***************************************************/
/* - retrieves initial configuration information from server */

/* NOTE: no request structure; all necessary request info is
 * represented in generic server request structure
 */

struct PVFS_servresp_getconfig
{
    /* fs config data */
    char *fs_config_buf;
    /* size of fs config data */
    uint32_t fs_config_buf_size;
    /* server config data */
    char *server_config_buf;
    /* size of server specific config data */
    uint32_t server_config_buf_size;
};

/* truncate ****************************************************/
/* - resizes an existing datafile */

struct PVFS_servreq_truncate
{
    PVFS_handle handle;		    /* handle of obj to resize */
    PVFS_fs_id fs_id;		    /* file system */
    PVFS_size size;		    /* new size */
};

/* NOTE: no response structure; all necessary response info is 
 * returned in generic server response structure
 */

/* io **********************************************************/
/* - performs a read or write operation */

enum PVFS_servreq_io_type
{
    PVFS_IO_READ = 1,
    PVFS_IO_WRITE = 2
};

struct PVFS_servreq_io
{
    PVFS_handle handle;		    /* target datafile */
    PVFS_fs_id fs_id;		    /* file system */
    /* type of I/O operation to perform */
    enum PVFS_servreq_io_type io_type;
    /* type of flow protocol to use for I/O transfer */
    enum PVFS_flowproto_type flow_type;
    /* relative number of this I/O server in distribution */
    uint32_t iod_num;
    /* total number of I/O servers involved in distribution */
    uint32_t iod_count;
    /* distribution */
    PVFS_Dist *io_dist;
    /* I/O request description (file datatype) */
    PVFS_Request io_req;
    /* I/O request offset (into file datatype) */
    PVFS_offset io_req_offset;
};

struct PVFS_servresp_io
{
    PVFS_size bstream_size;	    /* size of datafile */
};

/* write operations require a second response to announce completion */
struct PVFS_servresp_write_completion
{
    PVFS_size total_completed;	    /* amount of data transfered */
};

/* server request *********************************************/
/* - generic request with union of all op specific structs */

struct PVFS_server_req
{
    enum PVFS_server_op op;
    PVFS_credentials credentials;
    union
    {
	struct PVFS_servreq_create create;
	struct PVFS_servreq_remove remove;
	struct PVFS_servreq_io io;
	struct PVFS_servreq_getattr getattr;
	struct PVFS_servreq_setattr setattr;
	struct PVFS_servreq_mkdir mkdir;
	struct PVFS_servreq_readdir readdir;
	struct PVFS_servreq_lookup_path lookup_path;
	struct PVFS_servreq_createdirent crdirent;
	struct PVFS_servreq_rmdirent rmdirent;
	struct PVFS_servreq_truncate truncate;
    }
    u;
};

/* server response *********************************************/
/* - generic response with union of all op specific structs */
struct PVFS_server_resp
{
    enum PVFS_server_op op;
    PVFS_error status;
    union
    {
	struct PVFS_servresp_create create;
	struct PVFS_servresp_getattr getattr;
	struct PVFS_servresp_mkdir mkdir;
	struct PVFS_servresp_readdir readdir;
	struct PVFS_servresp_lookup_path lookup_path;
	struct PVFS_servresp_rmdirent rmdirent;
	struct PVFS_servresp_getconfig getconfig;
	struct PVFS_servresp_io io;
	struct PVFS_servresp_write_completion write_completion;
    }
    u;
};

#endif /* __PVFS2_REQ_PROTO_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
