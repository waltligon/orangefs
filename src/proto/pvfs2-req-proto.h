/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* PVFS Server Request Protocol 
 * 
 * We keep a simple set of operations for the I/O server.  The
 * motivation here is that there is going to be a switch based on object
 * type anyway, so we use the same request types to perform operations
 * on specific objects using the semantics of the object.
 *
 * I think.
 *
 */

#ifndef _PVFS2_REQ_PROTO_H
#define _PVFS2_REQ_PROTO_H

#include <pvfs2-types.h>
#include <pvfs2-attr.h>

typedef enum {
	PVFS_SERV_INVALID = 0,
	PVFS_SERV_NOOP = 1,
	PVFS_SERV_CREATE = 2,
	PVFS_SERV_REMOVE = 3,
	PVFS_SERV_IO = 4,
	PVFS_SERV_BATCH = 6,
	PVFS_SERV_GETATTR = 7,
	PVFS_SERV_SETATTR = 8,
	PVFS_SERV_GETEATTR = 9,
	PVFS_SERV_SETEATTR = 10,
	PVFS_SERV_LOOKUP_PATH = 11,
	PVFS_SERV_GETDIST = 12,
	PVFS_SERV_CREATEDIRENT = 13,
	PVFS_SERV_RMDIRENT = 14,
	PVFS_SERV_REVLOOKUP = 15,
	PVFS_SERV_ALLOCATE = 16,
	PVFS_SERV_TRUNCATE = 17,
	PVFS_SERV_MKDIR = 18,
	PVFS_SERV_RMDIR = 19,
	PVFS_SERV_READDIR = 20,
	PVFS_SERV_STATFS = 21,
	PVFS_SERV_IOSTATFS = 22,
	PVFS_SERV_GETCONFIG = 23,
	PVFS_SERV_EXTENSION = 99,
} PVFS_server_op;
/*
Issue:In above struct is the distribution information included in the 
extended attributes?In this case we could name it getdattr(less vague) or
getdist.
Answer:The distribution information is in the geteattr request structure
but it is a separate struct from the eattr structure.
*/
typedef enum {
	PVFS_IO_INVALID = 0,
	PVFS_IO_READ_DATAFILE_CONTIG = 1,
	PVFS_IO_WRITE_DATAFILE_CONTIG = 2,
	PVFS_IO_DATAFILE_ALLOCATE = 3,
	PVFS_IO_READ_DIRECTORY_CONTIG = 4,
	PVFS_IO_WRITE_DIRECTORY_CONTIG = 5,
	PVFS_IO_DUPLICATE = 6,
	PVFS_IO_EXTENSION = 99,
} PVFS_io_op;
/*
Issue:Should we have both read and write extensions?Like what is done
in MPI for specifying non-contiguous regions.In that case should we
directly support the MPI-based request format.
*/
/* io
 *
 * Concept:
 * 1) All I/O operations are defined by a handle, some opaque starting
 *    position, and some object-specific region description
 * 2) The minimum atomicity of an I/O operation is specified using a
 *    flag.  Not all types of objects support atomic operations.
 * 3) Conditional operations are supported in some cases as well.
 *    Basically you pass in a vtag (version tag) which is compared on the
 *    server side to data it holds describing a region.  If the vtag it
 *    holds fails, a conditional write operation will also fail.
 *
 *    Ask me about this later.
 *
 * I/O operations are only defined on directories and datafiles at the
 * moment.
 *
 */
/* io_datafile_contig
 *
 */
struct PVFS_io_datafile_contig_s {
	PVFS_size size;
};
typedef struct PVFS_io_datafile_contig_s PVFS_io_datafile_contig;

/*PVFS_Extensions*/
typedef enum
{
	temp,
	temp1
} PVFS_Extensions;

/* io_directory_contig
 *
 */
struct PVFS_io_directory_contig_s {
	PVFS_count32 dcount;	/*# of directory entries???*/
};
typedef struct PVFS_io_directory_contig_s PVFS_io_directory_contig;

union PVFS_io_position_s {
	PVFS_offset df_position;
	/* directory position is something else */
};
typedef union PVFS_io_position_s PVFS_io_position;

union PVFS_io_region_s {
	PVFS_io_datafile_contig df_contig;
	PVFS_io_directory_contig dir_contig;
};
typedef union PVFS_io_region_s PVFS_io_region;

/* conditional op stuff (vtag) */
struct PVFS_io_conditional_s {
	/* vtag? */
};
typedef struct PVFS_io_conditional_s PVFS_io_conditional;

/* Metaserver status structure */
struct PVFS_mserv_stat_s {
	PVFS_count32 filetotal;
	/* What else do we need here ??? */
};
typedef struct PVFS_mserv_stat_s PVFS_mserv_stat;

/* I/O server status structure */
/* TODO: Are the block statistics in bytes ??? */
struct PVFS_ioserv_stat_s {
	PVFS_size blksize;    /* filesystem block size */
	PVFS_count64 blkfree; /* number of free blocks */
	PVFS_count64 blktotal;/* total no. of blocks available */
	PVFS_count32 filetotal; /* Max no. of files */  
	PVFS_count32 filefree;/* no. of free files */
};
typedef struct PVFS_ioserv_stat_s PVFS_ioserv_stat;

/* Statfs structure */
struct PVFS_serv_statfs_s {
	union {
		PVFS_mserv_stat mstat;
		PVFS_ioserv_stat iostat; 
	} u;
};
typedef struct PVFS_serv_statfs_s PVFS_serv_statfs;

/* Server statfs */
struct PVFS_servreq_statfs_s {
	int server_type;
	PVFS_fs_id fs_id;	
};
typedef struct PVFS_servreq_statfs_s PVFS_servreq_statfs;  

struct PVFS_servresp_statfs_s {
	int server_type;
	PVFS_serv_statfs stat;
};
typedef struct PVFS_servresp_statfs_s PVFS_servresp_statfs;  

/* create
 *
 */
struct PVFS_servreq_create_s {
	PVFS_handle bucket;		/* Bucket to create PVFS object in */
	PVFS_handle handle_mask; /* Mask that specifies most significant
										 bits used for bucket number */
	PVFS_fs_id fs_id; /* Filesystem ID */
	int object_type; /* Type of PVFS object */	
};
typedef struct PVFS_servreq_create_s PVFS_servreq_create;

struct PVFS_servresp_create_s {
	PVFS_handle handle; 
};
typedef struct PVFS_servresp_create_s PVFS_servresp_create;

/* remove
 *
 */
struct PVFS_servreq_remove_s {
	PVFS_handle handle;
	PVFS_fs_id fs_id;
};
typedef struct PVFS_servreq_remove_s PVFS_servreq_remove;

/*no data returned for remove*/

/* batch
 * A series of operations to perform.  Abort all operations following a
 * failed one.
 */
struct PVFS_servreq_batch_s {
	/* list of other requests; need semantics */
	PVFS_count32 rcount;
	/*void *req[rcount];This should be an array of ptrs that are 
							  ptrs to structs*/
};
typedef struct PVFS_servreq_batch_s PVFS_servreq_batch;

struct PVFS_servresp_batch_s {
	/* list of other requests; need semantics */
	PVFS_count32 rcount;
	/*void *req[rcount];This should be an array of ptrs that are 
							  ptrs to structs*/
};
typedef struct PVFS_servresp_batch_s PVFS_servresp_batch;

/* getattr
 *
 */
struct PVFS_servreq_getattr_s {
	PVFS_handle handle;
	PVFS_fs_id fs_id;
	PVFS_bitfield attrmask; 
};
typedef struct PVFS_servreq_getattr_s PVFS_servreq_getattr;

struct PVFS_servresp_getattr_s {
	PVFS_object_attr attr;
	PVFS_attr_extended extended;
};
typedef struct PVFS_servresp_getattr_s PVFS_servresp_getattr;

/* geteattr
 *
 */
struct PVFS_servreq_geteattr_s {
	PVFS_handle handle;
	PVFS_fs_id fs_id;
	PVFS_object_eattr extended;/*not in sysint*/
};
typedef struct PVFS_servreq_geteattr_s PVFS_servreq_geteattr;

struct PVFS_servresp_geteattr_s {

};
typedef struct PVFS_servresp_geteattr_s PVFS_servresp_geteattr;

/* setattr
 * 
 */
struct PVFS_servreq_setattr_s {
	PVFS_handle handle;
	PVFS_fs_id fs_id;
	PVFS_object_attr attr;
	PVFS_bitfield attrmask;
	PVFS_attr_extended extended;
};
typedef struct PVFS_servreq_setattr_s PVFS_servreq_setattr;

/* No resonse for setattr */

/* Lookup_Single */
/*
struct PVFS_servreq_lookup_single_s{
	PVFS_handle parent_handle; 
	PVFS_string name; 		
	PVFS_fs_id fs_id; 		
};
typedef struct PVFS_servreq_lookup_single_s PVFS_servreq_lookup_single;

struct PVFS_servresp_lookup_single_s {
	PVFS_handle handle;
	PVFS_object_attr obj_attr;
};
typedef struct PVFS_servresp_lookup_single_s PVFS_servresp_lookup_single;
*/

/* Lookup_Path */
struct PVFS_servreq_lookup_path_s {
	PVFS_string path; 	 		  /* full path to be traversed */ 
	PVFS_fs_id fs_id;				  /* filesystem ID */
	PVFS_handle starting_handle; /* handle of starting directory for path */
	PVFS_bitfield attrmask;   /* mask to restrict the attributes to be 
											  fetched */
};
typedef struct PVFS_servreq_lookup_path_s PVFS_servreq_lookup_path;

struct PVFS_servresp_lookup_path_s {
	PVFS_handle *handle_array; /* ordered array of handles(1 for each 
											element in path successfully traversed */
	PVFS_object_attr *attr_array;	/* array of object attributes */
	PVFS_count32 count; 			/*	count of number of handles returned */
};
typedef struct PVFS_servresp_lookup_path_s PVFS_servresp_lookup_path;

/* Mkdir */
struct PVFS_servreq_mkdir_s {
	PVFS_handle bucket;	
	PVFS_handle handle_mask;
	PVFS_fs_id fs_id;
	PVFS_object_attr attr;
	PVFS_bitfield attrmask; 
};
typedef struct PVFS_servreq_mkdir_s PVFS_servreq_mkdir;

struct PVFS_servresp_mkdir_s {
	PVFS_handle handle;	
};
typedef struct PVFS_servresp_mkdir_s PVFS_servresp_mkdir;

/* Rmdir */
struct PVFS_servreq_rmdir_s {
	PVFS_handle handle; /* Handle of directory to remove */
	PVFS_fs_id fs_id;	  /* File system identifier of directory to remove */
};
typedef struct PVFS_servreq_rmdir_s PVFS_servreq_rmdir;

/* Createdirent */
struct PVFS_servreq_createdirent_s {
	PVFS_string name;	/* name of entry to create */ 
	PVFS_handle new_handle;	/* handle of new entry */
	PVFS_handle parent_handle;	/* handle of directory object to add entry to */
	PVFS_fs_id fs_id;		/* fs id of file system containing the directory */
};
typedef struct PVFS_servreq_createdirent_s PVFS_servreq_createdirent;

/* No response for createdirent */

/* Rmdirent */
struct PVFS_servreq_rmdirent_s {
	PVFS_string entry; /* entry to remove */
	PVFS_handle parent_handle; /* handle of directory to remove entry from */
	PVFS_fs_id fs_id;	/* fs id of file system containing directory */
};
typedef struct PVFS_servreq_rmdirent_s PVFS_servreq_rmdirent;

struct PVFS_servresp_rmdirent_s {
	PVFS_handle entry_handle; /* handle of entry removed */
};
typedef struct PVFS_servresp_rmdirent_s PVFS_servresp_rmdirent;

/* Generic ack */
struct PVFS_servresp_generic_s {
	PVFS_handle handle;
};
typedef struct PVFS_servresp_generic_s PVFS_servresp_generic;

/* Readdir */
struct PVFS_servreq_readdir_s {
	PVFS_handle handle;	/* Handle of directory to read entries from */
	PVFS_fs_id fs_id;		/* Filesystem identifier of directory's FS */
	PVFS_token token;		/* Opaque type to show current position in dir */	
	PVFS_count32 pvfs_dirent_count;/* count of no of dirents client 
												 wants to read */
};
typedef struct PVFS_servreq_readdir_s PVFS_servreq_readdir;

struct PVFS_servresp_readdir_s {
	PVFS_token token;
	PVFS_count32 pvfs_dirent_count;
	PVFS_dirent *pvfs_dirent_array;
};
typedef struct PVFS_servresp_readdir_s PVFS_servresp_readdir;

/* Getconfig */
struct PVFS_servreq_getconfig_s {
	PVFS_string	fs_name;	 /* Name of file system to get config info for */
	PVFS_count32 max_strsize;/* Expected size of string */ 
};
typedef struct PVFS_servreq_getconfig_s PVFS_servreq_getconfig;

struct PVFS_servresp_getconfig_s {
	PVFS_fs_id fs_id;		/* Filesystem identifier */
	PVFS_handle root_handle; /* Root handle for the file system */
	PVFS_handle maskbits;	 /* number of most significant bits representing
										 buckets */	
	PVFS_count32 meta_server_count; /* No of metaservers in system */
	PVFS_string meta_server_mapping; /* Ordered list of metaservers(BMI URL) */
	PVFS_count32 io_server_count; /* No of I/O servers in system */
	PVFS_string io_server_mapping;/* Ordered list of I/O servers(BMI URL) */
};
typedef struct PVFS_servresp_getconfig_s PVFS_servresp_getconfig;

/* allocate */
struct PVFS_servreq_allocate_s {

};
typedef struct PVFS_servreq_allocate_s PVFS_servreq_allocate;

struct PVFS_servresp_allocate_s {

};
typedef struct PVFS_servresp_allocate_s PVFS_servresp_allocate;

/* truncate */
struct PVFS_servreq_truncate_s {
	PVFS_handle handle;
	PVFS_fs_id fs_id;
	PVFS_size size;
};
typedef struct PVFS_servreq_truncate_s PVFS_servreq_truncate;

struct PVFS_servresp_truncate_s {

};
typedef struct PVFS_servresp_truncate_s PVFS_servresp_truncate;

/* extension */
struct PVFS_servreq_extension_s {
	/* ??? */
};
typedef struct PVFS_servreq_extension_s PVFS_servreq_extension;

struct PVFS_servresp_extension_s {
	/* ??? */
};
typedef struct PVFS_servresp_extension_s PVFS_servresp_extension;

/* seteattr */
struct PVFS_servreq_seteattr_s {
	/* ??? */
};
typedef struct PVFS_servreq_seteattr_s PVFS_servreq_seteattr;

struct PVFS_servresp_seteattr_s {
	/* ??? */
};
typedef struct PVFS_servresp_seteattr_s PVFS_servresp_seteattr;

/*PVFS I/O request*/
struct PVFS_servreq_io_s {
	PVFS_handle handle;
	PVFS_fs_id fs_id;
	PVFS_io_op op;
	//PVFS_Request request;
	//PVFS_Dist dist;
	PVFS_boolean atomic;
	PVFS_boolean conditional;
	PVFS_Extensions *extensions;
};

typedef struct PVFS_servreq_io_s PVFS_servreq_io;
/*
Issue: We need a pointer for IO extensions.Hence, a ptr has been added
to the struct.
*/

/*PVFS I/O response*/
struct PVFS_servresp_io_s {
/*TODO:
 * how should the io responses look?
 */
};
typedef struct PVFS_servresp_io_s PVFS_servresp_io;

/* PVFS Server Request
 *
 */
struct PVFS_server_req_s {
	PVFS_server_op op;
	PVFS_size rsize;
	PVFS_credentials credentials;
	union {
		PVFS_servreq_create create;
		PVFS_servreq_remove remove;
		PVFS_servreq_io io;
		PVFS_servreq_batch batch;
		PVFS_servreq_getattr getattr;
		PVFS_servreq_setattr setattr;
		PVFS_servreq_mkdir mkdir;
		PVFS_servreq_rmdir rmdir;
		PVFS_servreq_readdir readdir;
		PVFS_servreq_statfs statfs;
		PVFS_servreq_geteattr geteattr;
		PVFS_servreq_seteattr seteattr;
		PVFS_servreq_lookup_path lookup_path;
		PVFS_servreq_createdirent crdirent;
		PVFS_servreq_getconfig getconfig;
		PVFS_servreq_rmdirent rmdirent;
		PVFS_servreq_allocate allocate;
		PVFS_servreq_truncate truncate;
		PVFS_servreq_extension extension;
	} u;
};

struct PVFS_server_resp_s {
	PVFS_server_op op;
	PVFS_size rsize;
	PVFS_error status;
	union {
		PVFS_servresp_create create;
		PVFS_servresp_batch batch;
		PVFS_servresp_getattr getattr;
		PVFS_servresp_mkdir mkdir;
		PVFS_servresp_readdir readdir;
		PVFS_servresp_statfs statfs;
		PVFS_servresp_geteattr geteattr;
		PVFS_servresp_seteattr seteattr;
		PVFS_servresp_lookup_path lookup_path;
		PVFS_servresp_rmdirent rmdirent;
		PVFS_servresp_getconfig getconfig;
		PVFS_servresp_allocate allocate;
		PVFS_servresp_truncate truncate;
		PVFS_servresp_extension extension;
		PVFS_servresp_generic generic;
	} u;
};

#endif /* __PVFS2_REQ_PROTO_H */


