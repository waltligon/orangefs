/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_REQ_PROTO_H
#define __PVFS2_REQ_PROTO_H

#include "pvfs2-types.h"
#include "pvfs2-attr.h"
#include "pvfs2-storage.h"

/* valid PVFS2 request types */
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
    PVFS_SERV_STATFS = 12,
    PVFS_SERV_IOSTATFS = 13,
    PVFS_SERV_GETCONFIG = 14,
    PVFS_SERV_WRITE_COMPLETION = 15
/* not implemented: 
 * NOOP
 * GETEATTR
 * SETEATTR
 * BATCH
 * EXTENSION
 * STATFS
 */
};

/* create *********************************************************/
/* - used to create new metafile and datafile objects */

struct PVFS_servreq_create
{
    /* suggestion for what handle to allocate */
    PVFS_handle requested_handle;
    PVFS_fs_id fs_id;		    /* file system */
    PVFS_ds_type object_type;	    /* type of object to create */
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
    uint32_t attrmask;		    /* mask of attribs to set */
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
    /* suggestion for what handle to use */
    PVFS_handle requested_handle;
    PVFS_fs_id fs_id;		    /* file system */
    PVFS_object_attr attr;	    /* initial attributes */
    uint32_t attrmask;		    /* attribute mask */
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
    uint32_t pvfs_dirent_count;	    /* # of entries retrieved */
    /* array of directory entries */
    struct PVFS_dirent *pvfs_dirent_array;
};

/* getconfig ***************************************************/
/* - retrieves initial configuration information from server */

struct PVFS_servreq_getconfig
{
    /* maximum allowed size of configuration information to retrieve */
    uint32_t config_buf_size;
};

struct PVFS_servresp_getconfig
{
    /* size of fs config data */
    uint32_t fs_config_buf_size;
    /* fs config data */
    char *fs_config_buf;
    /* size of server specific config data */
    uint32_t server_config_buf_size;
    /* server config data */
    char *server_config_buf;
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
    /* TODO: removed temporarily */
    /* PVFS_Dist *io_dist; */
    /* PVFS_Request io_req; */
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
    enum PVFS_server_op op;	    /* type of operation */
    PVFS_credentials credentials;   /* permission information */
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
	struct PVFS_servreq_getconfig getconfig;
	struct PVFS_servreq_rmdirent rmdirent;
	struct PVFS_servreq_truncate truncate;
    }
    u;
};

/* server response *********************************************/
/* - generic response with union of all op specific structs */
struct PVFS_server_resp_s
{
    enum PVFS_server_op op;	    /* type of operation */
    PVFS_error status;		    /* resulting status */
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
