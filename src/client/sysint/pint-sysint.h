/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This file contains the declarations for the PVFS system interface
 */
#ifndef __PINT_SYSINT_H
#define __PINT_SYSINT_H

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include <assert.h>

#include "pvfs2-types.h"
#include "pvfs2-attr.h"
#include "gossip.h"
#include "job.h"
#include "bmi.h"
#include "pvfs2-sysint.h"
#include "gen-locks.h"
#include "pcache.h"

#include "dotconf.h"
#include "trove.h"
#include "server-config.h"

/* TODO: this should be read from config information, probably per fs */
#define PINT_CLIENT_ENC_TYPE PINT_ENC_DIRECT

/* converts common fields between sys attr and obj attr structures */
#define PINT_CONVERT_ATTR(dest, src, attrmask)	\
do{						\
    (dest)->owner = (src)->owner;		\
    (dest)->group = (src)->group;		\
    (dest)->perms = (src)->perms;		\
    (dest)->atime = (src)->atime;		\
    (dest)->mtime = (src)->mtime;		\
    (dest)->ctime = (src)->ctime;		\
    (dest)->objtype = (src)->objtype;		\
    (dest)->mask = ((src)->mask & attrmask);	\
}while(0)

/* TODO: this function is a hack- will be removed later */
int PINT_sys_getattr(PVFS_pinode_reference pinode_refn, uint32_t attrmask, 
    PVFS_credentials credentials, PVFS_object_attr *out_attr);

/* Bucket related info */
typedef struct {
    PVFS_handle bucket_st;
    PVFS_handle bucket_end;
} bucket_info;

/* maps bmi address to handle ranges/extents */
typedef struct bmi_host_extent_table_s
{
    char *bmi_address;

    /* ptrs are type struct extent */
    struct llist *extent_list;
} bmi_host_extent_table_s;

typedef struct config_fs_cache_s
{
    struct qlist_head hash_link;
    struct filesystem_configuration_s *fs;

    /* ptrs are type bmi_host_extent_table_s */
    struct llist *bmi_host_extent_tables;

    /* index into fs->meta_handle_ranges obj (see server-config.h) */
    struct llist *meta_server_cursor;

    /* index into fs->data_handle_ranges obj (see server-config.h) */
    struct llist *data_server_cursor;
} config_fs_cache_s;

/* send_info */
typedef struct 
{
    bmi_addr_t addr;
    void *buffer;
    bmi_size_t size;
    bmi_size_t expected_size;
    bmi_msg_tag_t tag;
    int flag;
    int unexpected;
    void *user_ptr;
    job_status_s *status_p;
    job_id_t *id;
    int *failure;
    PVFS_error errval;
} post_info;

/* Error_stats */
struct error_stats
{
    int failure;
    int id;
    int num_pending;
};


/* PVFS Object - File name + Collection ID */

/* PVFStab parameters */

/* Public Interface */
void free_pvfstab_entry(pvfs_mntlist *e_p);
int sysjob_free(bmi_addr_t server,void *tmp_job,bmi_size_t size,const int op,
		int (*func)(void *,int));
void job_postack(post_info *rinfo);
void job_postreq(post_info *sinfo);
void job_waitblock(job_id_t *id_array,
		   int pending,
		   void **user_ptr_array,
		   job_status_s *status_array,
		   int *failure);
int server_getconfig(pvfs_mntlist mntent_list);
int get_next_path(char *path, char **newpath, int skip);
int check_perms(PVFS_object_attr attr,PVFS_permissions mode,int uid,int gid);

int PINT_do_lookup (char* name,PVFS_pinode_reference parent,
                PVFS_credentials cred,PVFS_pinode_reference *entry);

int get_path_element(char *path, char** segment, int element);

int PINT_server_get_config(
    struct server_configuration_s *config,
    pvfs_mntlist mntent_list);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif
