/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This file contains the declarations for the PVFS system interface
 */
#ifndef __PVFS_SYSINT_H
#define __PVFS_SYSINT_H

#ifndef __KERNEL__
#include <limits.h>
#endif

#include "pvfs2-types.h"
#include "pvfs2-request.h"

/* attributes */
struct PVFS_sys_attr_s
{
    PVFS_uid owner;
    PVFS_gid group;
    PVFS_permissions perms;
    PVFS_time atime;
    PVFS_time mtime;
    PVFS_time ctime;
    PVFS_size size;
    char *link_target;		/* NOTE: caller must free if valid */
    int dfile_count;
    PVFS_ds_type objtype;
    uint32_t mask;

    /* TODO: we may want to later add some sort of enumerated value
     * that can be used to determine distribution type? 
     */
};
typedef struct PVFS_sys_attr_s PVFS_sys_attr;

/* PVFS2 tab file entries */
struct pvfs_mntent
{
    char *pvfs_config_server;	/* address of server with config info */
    char *pvfs_fs_name;		/* name of PVFS2 file system */
    char *mnt_dir;		/* local mount path */
    char *mnt_opts;		/* full option list */
    enum PVFS_flowproto_type flowproto;	/* flow protocol */
    enum PVFS_encoding_type encoding;   /* wire data encoding */
};

struct pvfs_mntlist_s
{
    int ptab_count;		/* number of tab file entries */
    struct pvfs_mntent *ptab_array;	/* array of entries */
};
typedef struct pvfs_mntlist_s pvfs_mntlist;

/* response from init */
struct PVFS_sysresp_init_s
{
    int nr_fsid;		/*Number of fs_id's that we're returning */
    PVFS_fs_id *fsid_list;
};
typedef struct PVFS_sysresp_init_s PVFS_sysresp_init;

/* lookup (request and response) */

struct PVFS_sysresp_lookup_s
{
    PVFS_pinode_reference pinode_refn;
};
typedef struct PVFS_sysresp_lookup_s PVFS_sysresp_lookup;

/* getattr */
struct PVFS_sysresp_getattr_s
{
    PVFS_sys_attr attr;
};
typedef struct PVFS_sysresp_getattr_s PVFS_sysresp_getattr;

/* setattr */
/* no data returned in setattr response */

/* mkdir */
struct PVFS_sysresp_mkdir_s
{
    PVFS_pinode_reference pinode_refn;
};
typedef struct PVFS_sysresp_mkdir_s PVFS_sysresp_mkdir;

/* create */
struct PVFS_sysresp_create_s
{
    PVFS_pinode_reference pinode_refn;
};
typedef struct PVFS_sysresp_create_s PVFS_sysresp_create;

/* remove */
/* no data returned in remove response */

/* rename */
/* no data returned in rename response */

/* symlink */
struct PVFS_sysresp_symlink_s
{
    PVFS_pinode_reference pinode_refn;
};
typedef struct PVFS_sysresp_symlink_s PVFS_sysresp_symlink;

/* readlink */
struct PVFS_sysresp_readlink_s
{
    char *target;
};
typedef struct PVFS_sysresp_readlink_s PVFS_sysresp_readlink;

/* read/write */
struct PVFS_sysresp_io_s
{
    PVFS_size total_completed;
};
typedef struct PVFS_sysresp_io_s PVFS_sysresp_io;

/* readdir */
struct PVFS_sysresp_readdir_s
{
    PVFS_ds_position token;
    int pvfs_dirent_outcount;
    PVFS_dirent *dirent_array;
};
typedef struct PVFS_sysresp_readdir_s PVFS_sysresp_readdir;

/* truncate */
/* no data returned in truncate response */

/* statfs */
struct PVFS_sysresp_statfs_s
{
    PVFS_statfs statfs_buf;
    int server_count;
};
typedef struct PVFS_sysresp_statfs_s PVFS_sysresp_statfs;

struct PVFS_sysresp_getparent_s
{
    PVFS_pinode_reference parent_refn;
    char basename[PVFS_NAME_MAX];
};
typedef struct PVFS_sysresp_getparent_s PVFS_sysresp_getparent;

/*declarations*/

/* PVFS System Request Prototypes
 *
 * That's fine, except that we KNOW that this interface is just a
 * virtual one; this interface will be converting the input parameters
 * into requests for servers.  So we can avoid some parsing and checking
 * at this level by using a number of calls instead.  We'd probably have
 * a function per system operation anyway, so what we're really doing is
 * avoiding an extra function call.
 */
int PVFS_sys_initialize(
    pvfs_mntlist mntent_list,
    int debug_mask,
    PVFS_sysresp_init * resp);

int PVFS_sys_finalize(
    void);

/*
  NOTE: the following values are to be used by
  PVFS_sys(.*)_lookup as the "follow_link" argument.

  All symlinks are resolved if they are part of a
  longer pathname. (i.e. partial path symlink resolution)

  These values dictate what to do when the final object
  looked up is a symlink.  Using PVFS2_LOOKUP_LINK_NO_FOLLOW,
  the symlink object (i.e. handle) will be returned.
  Using PVFS2_LOOKUP_LINK_FOLLOW, the symlink target will be
  continue to be resolved until a non symlink object type
  is resolved -- and this resolved object will be returned
*/
#define PVFS2_LOOKUP_LINK_NO_FOLLOW 0
#define PVFS2_LOOKUP_LINK_FOLLOW    1

int PVFS_sys_ref_lookup(
    PVFS_fs_id fs_id,
    char *relative_pathname,
    PVFS_pinode_reference parent,
    PVFS_credentials credentials,
    PVFS_sysresp_lookup * resp,
    int follow_link);

int PVFS_sys_lookup(
    PVFS_fs_id fs_id,
    char *name,
    PVFS_credentials credentials,
    PVFS_sysresp_lookup * resp,
    int follow_link);

int PVFS_sys_getattr(
    PVFS_pinode_reference pinode_refn,
    uint32_t attrmask,
    PVFS_credentials credentials,
    PVFS_sysresp_getattr * resp);

int PVFS_sys_setattr(
    PVFS_pinode_reference pinode_refn,
    PVFS_sys_attr attr,
    PVFS_credentials credentials);

int PVFS_sys_mkdir(
    char *entry_name,
    PVFS_pinode_reference parent_refn,
    PVFS_sys_attr attr,
    PVFS_credentials credentials,
    PVFS_sysresp_mkdir * resp);

int PVFS_sys_readdir(
    PVFS_pinode_reference pinode_refn,
    PVFS_ds_position token,
    int pvfs_dirent_incount,
    PVFS_credentials credentials,
    PVFS_sysresp_readdir * resp);

int PVFS_sys_create(
    char *entry_name,
    PVFS_pinode_reference parent_refn,
    PVFS_sys_attr attr,
    PVFS_credentials credentials,
    PVFS_sysresp_create * resp);

int PVFS_sys_remove(
    char *entry_name,
    PVFS_pinode_reference parent_refn,
    PVFS_credentials credentials);

int PVFS_sys_rename(
    char *old_entry,
    PVFS_pinode_reference old_parent_refn,
    char *new_entry,
    PVFS_pinode_reference new_parent_refn,
    PVFS_credentials credentials);

int PVFS_sys_symlink(
    char *entry_name,
    PVFS_pinode_reference parent_refn,
    char *target,
    PVFS_sys_attr attr,
    PVFS_credentials credentials,
    PVFS_sysresp_symlink * resp);

int PVFS_sys_readlink(
    PVFS_pinode_reference pinode_refn,
    PVFS_credentials credentials,
    PVFS_sysresp_readlink * resp);

int PVFS_sys_io(
    PVFS_pinode_reference pinode_refn,
    PVFS_Request file_req,
    PVFS_offset file_req_offset,
    void *buffer,
    PVFS_Request mem_req,
    PVFS_credentials credentials,
    PVFS_sysresp_io * resp,
    enum PVFS_io_type type);

#define PVFS_sys_read(x1,x2,x3,x4,x5,x6,y) \
PVFS_sys_io(x1,x2,x3,x4,x5,x6,y,PVFS_IO_READ)

#define PVFS_sys_write(x1,x2,x3,x4,x5,x6,y) \
PVFS_sys_io(x1,x2,x3,x4,x5,x6,y,PVFS_IO_WRITE)

int PVFS_sys_truncate(
    PVFS_pinode_reference pinode_refn,
    PVFS_size size,
    PVFS_credentials credentials);

int PVFS_sys_getparent(
    PVFS_fs_id fs_id,
    char *entry_name,
    PVFS_credentials credentials,
    PVFS_sysresp_getparent * resp);

int PVFS_sys_flush(
    PVFS_pinode_reference pinode_refn,
    PVFS_credentials credentials);

int PVFS_sys_statfs(
    PVFS_fs_id fs_id,
    PVFS_credentials credentials,
    PVFS_sysresp_statfs* resp);

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
