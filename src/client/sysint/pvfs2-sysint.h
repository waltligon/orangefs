/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This file contains the declarations for the PVFS system interface
 */
#ifndef __PVFS_SYSINT_H
#define __PVFS_SYSINT_H

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <limits.h>

#include <pvfs2-types.h>
#include <pvfs2-attr.h>
#include <llist.h>
#include <gossip.h>
#include <errno.h>
#include <pvfs-request.h>

/* TODO: note that this should be a derived value eventually.  For
 * now it is hard coded to match the definition of
 * TROVE_ITERATE_START in trove.h
 */
#define PVFS2_READDIR_START (INT_MAX-1)

/*#define NUM_SERVER 10*/

typedef enum {
	PVFS_SYS_INVALID = 0,
	PVFS_SYS_LOOKUP = 1,
	PVFS_SYS_GETATTR = 2,
	PVFS_SYS_SETATTR = 3,
	PVFS_SYS_MKDIR = 4,
	PVFS_SYS_RMDIR = 5, /* Q: leave out? */
	PVFS_SYS_CREATE = 6,
	PVFS_SYS_REMOVE = 7,
	PVFS_SYS_RENAME = 8,
	PVFS_SYS_SYMLINK = 9,
	PVFS_SYS_READLINK = 10,
	/* Q: function for removing a link?  REMOVE DOES IT ALL? */
#if 0
	PVFS_SYS_READ = 11,
	PVFS_SYS_WRITE = 12,
#endif
	PVFS_SYS_ALLOCATE = 13,
	PVFS_SYS_CLONE = 14,
	PVFS_SYS_LOCK = 15,
	PVFS_SYS_UNLOCK = 16,
	PVFS_SYS_STATFS = 17,
	PVFS_SYS_CONFIG = 18,
	PVFS_SYS_HINT = 19,
	PVFS_SYS_GETDIST = 20,
	PVFS_SYS_CREATEDIRENT = 21,
	PVFS_SYS_ADDDIRENT = 22,
	PVFS_SYS_NEWDIRENT = 23,
	PVFS_SYS_RMDIRENT = 24,
	PVFS_SYS_REVLOOKUP = 25,
	PVFS_SYS_READDIR = 26,
	PVFS_SYS_TRUNCATE = 27,
	PVFS_SYS_IO = 28,
	PVFS_SYS_EXTENSION = 99,
} PVFS_system_op;

/* IO server stats */
struct PVFS_io_stat_s {
	/*PVFS_volume_id vid;*/
	PVFS_size blksize;
	uint32_t blkfree;
	uint32_t blktotal;
	uint32_t filetotal;
	uint32_t filefree;
};
typedef struct PVFS_io_stat_s PVFS_io_stat;

/* Meta server stats */
struct PVFS_meta_stat_s {
	uint32_t filetotal;
};
typedef struct PVFS_meta_stat_s PVFS_meta_stat;

/* statfs structure -- used in system interface only (?) */
struct PVFS_statfs_s {
	PVFS_meta_stat mstat;
	PVFS_io_stat iostat;
};
typedef struct PVFS_statfs_s PVFS_statfs;

enum {
	STRING = 1,
	INT = 2
};

/*
 * Here's the format of the pvfstab file that the client expects:
 *
 * FORMAT:
 * BMI_ADDRESS/SERVICE_NAME LOCAL_MNT_DIR FILESYSTEM_TYPE OPT1 OPT2
 *
 * EXAMPLE:
 * pvfs-tcp://localhost:3334/pvfs1 /mnt/pvfs pvfs 0 0
 *
 * where:
 * BMI_ADDRESS = "pvfs-tcp://localhost:3334"
 * SERVICE_NAME = "pvfs1"
 * LOCAL_MNT_DIR = "/mnt/pvfs"
 * OPT1 = "0"
 * OPT2 = "0"
 * 
 * we must have atleast one line for every pvfs filesystem type the client 
 * mounts.
 */

/* PVFStab parameters */
struct pvfs_mntent_s {
   char* meta_addr; /* metaserver address */
   char* service_name; /* Service name for the remote pvfs filesystem */
   char* local_mnt_dir;/* Local mount point */
   char* fs_type;    /* Type of filesystem - pvfs */
   char* opt1;    /* Mount Option */
   char* opt2;    /* Mount Option */
};
typedef struct pvfs_mntent_s pvfs_mntent;

struct pvfs_mntlist_s {
  int nr_entry; /* Number of entries in PVFStab */
  pvfs_mntent *ptab_p;
};
typedef struct pvfs_mntlist_s pvfs_mntlist;

/* response from init */
struct PVFS_sysresp_init_s {
	int nr_fsid; /*Number of fs_id's that we're returning*/
	PVFS_fs_id *fsid_list; 
};
typedef struct PVFS_sysresp_init_s PVFS_sysresp_init;

/* lookup (request and response) */

struct PVFS_sysresp_lookup_s {
	pinode_reference pinode_refn;
};
typedef struct PVFS_sysresp_lookup_s PVFS_sysresp_lookup;

/* getattr */
struct PVFS_sysresp_getattr_s {
	PVFS_object_attr attr;
	PVFS_attr_extended extended;
};
typedef struct PVFS_sysresp_getattr_s PVFS_sysresp_getattr;

/* setattr */
/* no data returned in setattr response */

/* mkdir */
struct PVFS_sysreq_mkdir_s {
	char* entry_name; /* single segment */
	pinode_reference parent_refn;
	PVFS_object_attr attr;
	uint32_t attrmask;
	PVFS_credentials credentials;
};
typedef struct PVFS_sysreq_mkdir_s PVFS_sysreq_mkdir;

struct PVFS_sysresp_mkdir_s {
	pinode_reference pinode_refn;
};
typedef struct PVFS_sysresp_mkdir_s PVFS_sysresp_mkdir;

/* create */
struct PVFS_sysresp_create_s {
	pinode_reference pinode_refn;
};
typedef struct PVFS_sysresp_create_s PVFS_sysresp_create;

/* remove */
struct PVFS_sysreq_remove_s {
	char* entry_name; /* single path segment */
	pinode_reference parent_refn;
	PVFS_credentials credentials;
};
typedef struct PVFS_sysreq_remove_s PVFS_sysreq_remove;

/* no data returned in remove response */

/* rename */
struct PVFS_sysreq_rename_s {
	char* old_entry; /* single path segment */
	pinode_reference old_parent_refn;
	char* new_entry;
	pinode_reference new_parent_refn;
	PVFS_credentials credentials;
};
typedef struct PVFS_sysreq_rename_s PVFS_sysreq_rename;

/* no data returned in rename response */

/* symlink */
struct PVFS_sysresp_symlink_s {
	pinode_reference pinode_refn;
};
typedef struct PVFS_sysresp_symlink_s PVFS_sysresp_symlink;

/* readlink */
struct PVFS_sysresp_readlink_s {
	char* target;
};
typedef struct PVFS_sysresp_readlink_s PVFS_sysresp_readlink;

/* read/write */
struct PVFS_sysreq_io_s {
	pinode_reference pinode_refn;
	PVFS_credentials credentials;
	PVFS_Request io_req;
	void* buffer;
	int buffer_size;
};
typedef struct PVFS_sysreq_io_s PVFS_sysreq_io;

struct PVFS_sysresp_io_s {
	PVFS_size total_completed;
};
typedef struct PVFS_sysresp_io_s PVFS_sysresp_io;

/* allocate */
/* Q: SHOULD THIS BE A TRUNCATE INSTEAD? */
/* no data returned in allocate response */

/* Duplicate (only on a file) */
struct PVFS_sysresp_duplicate_s {
	pinode_reference pinode_refn; /* handle,fs id of new file */
};
typedef struct PVFS_sysresp_duplicate_s PVFS_sysresp_duplicate;

/* lock */
struct PVFS_sysreq_lock_s {
	pinode_reference pinode_refn; 
	PVFS_credentials credentials;
	/* region extension */
};
typedef struct PVFS_sysreq_lock_s PVFS_sysreq_lock;

struct PVFS_sysresp_lock_s {
	pinode_reference pinode_refn; 
	/* lock id? */
	/* region actually locked? */
};
typedef struct PVFS_sysresp_lock_s PVFS_sysresp_lock;

/* unlock */
struct PVFS_sysreq_unlock_s {
	pinode_reference pinode_refn; 
	PVFS_credentials credentials;
	/* lock id? */
};
typedef struct PVFS_sysreq_unlock_s PVFS_sysreq_unlock;

/* no data returned in unlock response */

/* statfs */
struct PVFS_sysreq_statfs_s {
	PVFS_fs_id fs_id; /* Filesystem ID */
	PVFS_credentials credentials;

};
typedef struct PVFS_sysreq_statfs_s PVFS_sysreq_statfs;

struct PVFS_sysresp_statfs_s {
	PVFS_statfs statfs;
};
typedef struct PVFS_sysresp_statfs_s PVFS_sysresp_statfs;

/* config */

struct PVFS_sysresp_config_s {
	/* config info... */
};
typedef struct PVFS_sysresp_config_s PVFS_sysresp_config;

/* readdir */
struct PVFS_sysreq_readdir_s {
	pinode_reference pinode_refn;
	PVFS_ds_position token;
	int pvfs_dirent_incount;
	PVFS_credentials credentials;
	
};
typedef struct PVFS_sysreq_readdir_s PVFS_sysreq_readdir;

struct PVFS_sysresp_readdir_s {
	PVFS_ds_position token;  
	int pvfs_dirent_outcount;
	PVFS_dirent *dirent_array;
};
typedef struct PVFS_sysresp_readdir_s PVFS_sysresp_readdir;

/* hint */
struct PVFS_sysresp_hint_s {
	/* ??? */
};
typedef struct PVFS_sysresp_hint_s PVFS_sysresp_hint;

/* truncate */
struct PVFS_sysreq_truncate_s {
	pinode_reference pinode_refn;
	PVFS_size size;
	PVFS_credentials credentials;
};
typedef struct PVFS_sysreq_truncate_s PVFS_sysreq_truncate;

/* no data returned in truncate response */


/* extension */
struct PVFS_sysresp_extension_s {
	/* ??? */
};
typedef struct PVFS_sysresp_extension_s PVFS_sysresp_extension;

/* PVFS system request structure
 */
struct PVFS_system_req_s {
	PVFS_system_op op;
	int32_t req_tag; /* Tag to group reqs+acks */
	int32_t verno;	  /* Version number */
	union {
		PVFS_sysreq_mkdir mkdir;
		PVFS_sysreq_remove remove;
		PVFS_sysreq_rename rename;
		PVFS_sysreq_io io;
		PVFS_sysreq_lock lock;
		PVFS_sysreq_unlock unlock;
		PVFS_sysreq_statfs statfs;
		PVFS_sysreq_readdir readdir;
	} u;
};

/* PVFS system response structure
 */
struct PVFS_system_resp_s {
	PVFS_system_op op;
	int32_t resp_tag; /* Tag to group reqs+acks */
	int32_t verno;		/* Version number */
	union {
		PVFS_sysresp_lookup lookup;
		PVFS_sysresp_getattr getattr;
		PVFS_sysresp_mkdir mkdir;
		PVFS_sysresp_create create;
		PVFS_sysresp_symlink symlink;
		PVFS_sysresp_readlink readlink;
		PVFS_sysresp_io io;
		PVFS_sysresp_duplicate duplicate;
		PVFS_sysresp_lock lock;
		PVFS_sysresp_statfs statfs;
		PVFS_sysresp_config config;
		PVFS_sysresp_readdir readdir;
		PVFS_sysresp_hint hint;
		PVFS_sysresp_extension extension;
	} u;
};

/*declarations*/

/*struct metaserv_table
{
	char **table_p;
	int number;
};*/

/* an enumeration that controls what type of operation is performed in
 * PVFS_sys_io()
 */
enum PVFS_sys_io_type
{
	PVFS_SYS_IO_READ,
	PVFS_SYS_IO_WRITE
};

/* PVFS System Request Prototypes
 *
 * That's fine, except that we KNOW that this interface is just a
 * virtual one; this interface will be converting the input parameters
 * into requests for servers.  So we can avoid some parsing and checking
 * at this level by using a number of calls instead.  We'd probably have
 * a function per system operation anyway, so what we're really doing is
 * avoiding an extra function call.
 */
int PVFS_sys_initialize(pvfs_mntlist mntent_list, PVFS_sysresp_init *resp);
int PVFS_sys_finalize(void);
int PVFS_sys_lookup(PVFS_fs_id fs_id, char* name, PVFS_credentials 
				credentials, PVFS_sysresp_lookup *resp);
int PVFS_sys_getattr(pinode_reference pinode_refn, uint32_t attrmask, 
		PVFS_credentials credentials, PVFS_sysresp_getattr *resp);
int PVFS_sys_setattr(pinode_reference pinode_refn, PVFS_object_attr attr,
		uint32_t attrmask, PVFS_credentials credentials, 
		PVFS_attr_extended extended);
int PVFS_sys_mkdir(PVFS_sysreq_mkdir *req, PVFS_sysresp_mkdir *resp);
int PVFS_sys_readdir(PVFS_sysreq_readdir *req, PVFS_sysresp_readdir *resp);
int PVFS_sys_create(char* entry_name, pinode_reference parent_refn, 
		uint32_t attrmask, PVFS_object_attr attr, 
		PVFS_credentials credentials, PVFS_sysresp_create *resp);
int PVFS_sys_remove(PVFS_sysreq_remove *req);
int PVFS_sys_rename(PVFS_sysreq_rename *req);
int PVFS_sys_symlink(PVFS_fs_id fs_id, char* name, char* target, 
		uint32_t attrmask, PVFS_object_attr attr, 
		PVFS_credentials credentials, PVFS_sysresp_symlink *resp);
int PVFS_sys_readlink(pinode_reference pinode_refn, 
		PVFS_credentials credentials, PVFS_sysresp_readlink *resp);
int PVFS_sys_io(PVFS_sysreq_io *req, PVFS_sysresp_io *resp, 
	enum PVFS_sys_io_type type);
#define PVFS_sys_read(x,y) PVFS_sys_io(x,y,PVFS_SYS_IO_READ)
#define PVFS_sys_write(x,y) PVFS_sys_io(x,y,PVFS_SYS_IO_WRITE)
int PVFS_sys_allocate(pinode_reference pinode_refn, PVFS_size size);
int PVFS_sys_truncate(PVFS_sysreq_truncate *req);
int PVFS_sys_duplicate(PVFS_fs_id fs_id, pinode_reference old_reference, 
		char* new_entry, pinode_reference new_parent_reference, 
		PVFS_sysresp_duplicate *resp);
int PVFS_sys_lock(PVFS_sysreq_lock *req, PVFS_sysresp_lock *resp);
int PVFS_sys_unlock(PVFS_sysreq_unlock *req);
int PVFS_sys_statfs(PVFS_sysreq_statfs *req, PVFS_sysresp_statfs *resp);
int PVFS_sys_config(PVFS_handle handle, PVFS_sysresp_config *resp);
int PVFS_sys_hint(int undefined,  PVFS_sysresp_hint *resp);
int PVFS_sys_extension(int undefined, PVFS_sysresp_extension *resp);

#endif
