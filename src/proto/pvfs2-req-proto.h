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
#include "flow.h"

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
/* not implemented:
 * PVFS_SERV_NOOP 
 * PVFS_SERV_GETEATTR
 * PVFS_SERV_SETEATTR,
 * PVFS_SERV_BATCH
 * PVFS_SERV_EXTENSION
 * PVFS_SERV_STATFS
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

/* getattr
 *
 */
struct PVFS_servreq_getattr
{
    PVFS_handle handle;
    PVFS_fs_id fs_id;
    uint32_t attrmask;
};

struct PVFS_servresp_getattr
{
    PVFS_object_attr attr;
};

/* geteattr
 *
 */
struct PVFS_servreq_geteattr
{
    PVFS_handle handle;
    PVFS_fs_id fs_id;
};

struct PVFS_servresp_geteattr
{

};

/* setattr
 * 
 */
struct PVFS_servreq_setattr
{
    PVFS_handle handle;
    PVFS_fs_id fs_id;
    PVFS_object_attr attr;
};

/* No resonse for setattr */

/* Lookup_Path */
struct PVFS_servreq_lookup_path
{
    char* path;		/* full path to be traversed */
    PVFS_fs_id fs_id;		/* filesystem ID */
    PVFS_handle starting_handle;	/* handle of starting directory for path */
    uint32_t attrmask;	/* mask to restrict the attributes to be 
				   fetched */
};

struct PVFS_servresp_lookup_path
{
    PVFS_handle *handle_array;	/* ordered array of handles(1 for each 
				   element in path successfully traversed */
    PVFS_object_attr *attr_array;	/* array of object attributes */
    uint32_t count;		/*      count of number of handles returned */
};

/* Mkdir */
struct PVFS_servreq_mkdir
{
    PVFS_handle requested_handle;
    PVFS_fs_id fs_id;
    PVFS_object_attr attr;
};

struct PVFS_servresp_mkdir
{
    PVFS_handle handle;
};

/* Createdirent */
struct PVFS_servreq_createdirent
{
    char* name;		/* name of entry to create */
    PVFS_handle new_handle;	/* handle of new entry */
    PVFS_handle parent_handle;	/* handle of directory object to add entry to */
    PVFS_fs_id fs_id;		/* fs id of file system containing the directory */
};

/* No response for createdirent */

/* Rmdirent */
struct PVFS_servreq_rmdirent
{
    char* entry;		/* entry to remove */
    PVFS_handle parent_handle;	/* handle of directory to remove entry from */
    PVFS_fs_id fs_id;		/* fs id of file system containing directory */
};

struct PVFS_servresp_rmdirent
{
    PVFS_handle entry_handle;	/* handle of entry removed */
};

/* Generic ack */
struct PVFS_servresp_generic
{
    PVFS_handle handle;
};

/* Readdir */
struct PVFS_servreq_readdir
{
    PVFS_handle handle;		/* Handle of directory to read entries from */
    PVFS_fs_id fs_id;		/* Filesystem identifier of directory's FS */
    PVFS_ds_position token;	/* Opaque type to show current position in dir */
    uint32_t pvfs_dirent_count;	/* count of no of dirents client 
					   wants to read */
};

struct PVFS_servresp_readdir
{
    PVFS_ds_position token;
    uint32_t pvfs_dirent_count;
    PVFS_dirent *pvfs_dirent_array;
};

/* Getconfig */
struct PVFS_servreq_getconfig
{
    uint32_t max_strsize;	/* Expected size of string */
};

struct PVFS_servresp_getconfig
{
    uint32_t fs_config_buflen;	/* length of fs configuration file contents */
    char* fs_config_buf;	/* fs configuration file contents */
    uint32_t server_config_buflen;	/* length of fs configuration file contents */
    char* server_config_buf;	/* fs configuration file contents */
};

/* allocate */
struct PVFS_servreq_allocate
{

};

struct PVFS_servresp_allocate
{

};

/* truncate */
struct PVFS_servreq_truncate
{
    PVFS_handle handle;
    PVFS_fs_id fs_id;
    PVFS_size size;
};

struct PVFS_servresp_truncate
{

};

/* extension */
struct PVFS_servreq_extension
{
    /* ??? */
};

struct PVFS_servresp_extension
{
    /* ??? */
};

/* seteattr */
struct PVFS_servreq_seteattr
{
    /* ??? */
};

struct PVFS_servresp_seteattr
{
    /* ??? */
};

/* supported I/O operation types */
enum PVFS_servreq_io_type
{
    PVFS_IO_READ = 1,
    PVFS_IO_WRITE
};

/* PVFS I/O request */
struct PVFS_servreq_io
{
    PVFS_handle handle;		/* handle to operate on */
    PVFS_fs_id fs_id;		/* file system id */
    enum PVFS_servreq_io_type io_type;	/* type of I/O operation */
    enum PVFS_flowproto_type flow_type;	/* type of flow protocol */
    /* relative number of this I/O server in distribution */
    uint32_t iod_num;
    /* total number of I/O servers involved in distribution */
    uint32_t iod_count;
    PVFS_Dist *io_dist;		/* physical distribution */
    PVFS_Request io_req;	/* datatype pattern */
};

/* PVFS I/O response */
struct PVFS_servresp_io
{
    PVFS_size bstream_size;
};

/* PVFS write completion response (there is no req for this one,
 * it is sent from server to client after completing a write) 
 */
struct PVFS_servresp_write_completion
{
    PVFS_size total_completed;
};

/* PVFS Server Request
 *
 */
struct PVFS_server_req
{
    enum PVFS_server_op op;
    PVFS_size rsize;
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
	struct PVFS_servreq_geteattr geteattr;
	struct PVFS_servreq_seteattr seteattr;
	struct PVFS_servreq_lookup_path lookup_path;
	struct PVFS_servreq_createdirent crdirent;
	struct PVFS_servreq_getconfig getconfig;
	struct PVFS_servreq_rmdirent rmdirent;
	struct PVFS_servreq_allocate allocate;
	struct PVFS_servreq_truncate truncate;
	struct PVFS_servreq_extension extension;
    }
    u;
};

struct PVFS_server_resp
{
    enum PVFS_server_op op;
    PVFS_size rsize;
    PVFS_error status;
    union
    {
	struct PVFS_servresp_create create;
	struct PVFS_servresp_getattr getattr;
	struct PVFS_servresp_mkdir mkdir;
	struct PVFS_servresp_readdir readdir;
	struct PVFS_servresp_geteattr geteattr;
	struct PVFS_servresp_seteattr seteattr;
	struct PVFS_servresp_lookup_path lookup_path;
	struct PVFS_servresp_rmdirent rmdirent;
	struct PVFS_servresp_getconfig getconfig;
	struct PVFS_servresp_allocate allocate;
	struct PVFS_servresp_truncate truncate;
	struct PVFS_servresp_extension extension;
	struct PVFS_servresp_generic generic;
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
