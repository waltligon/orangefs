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

#include <pvfs2-types.h>
#include <pvfs2-attr.h>
#include <gossip.h>
#include <job.h>
#include <bmi.h>
#include <pvfs2-sysint.h>
#include <gen-locks.h>

#include "dotconf.h"
#include "trove.h"
#include "server-config.h"

enum
{
    SIZE_INVALID = 0,
    SIZE_VALID = 1
};

/* Update Pinode Flags
 * Mention specific timestamps to be updated
 */
#define HANDLE_TSTAMP 1
#define ATTR_TSTAMP 2
#define SIZE_TSTAMP 4
/* Revalidate handle/attributes? */
#define HANDLE_VALIDATE 2	/* Indicate handle is to be revalidated */
#define ATTR_VALIDATE 4		/* Indicate attributes are to be revalidated */
#define SIZE_VALIDATE 8		/* Indicates size is to be revalidated */

/* PCache Flags */
#define SEARCH_NO_FETCH 1/* Check only if pinode is present,don't fill it up */

/* Pinode structure */
typedef struct
{
    pinode_reference pinode_ref; /* pinode reference - entity to

                                    uniquely identify a pinode */
    gen_mutex_t *pinode_mutex;        /* mutex lock */
    struct PVFS_object_attr attr;   /* attributes of PVFS object */
    uint32_t mask;                       /* attribute mask */
    PVFS_size size;                           /* PVFS object size */
    struct timeval tstamp;  /* timestamp for consistency */
    int size_flag;	    /*flag so we know if the size is valid*/
} pinode, *pinode_p;


/* Max No. of Entries in the cache */
#define MAX_ENTRIES 64 /* 32k entries for the cache */

/* Bucket related info */
typedef struct {
    PVFS_handle bucket_st;
    PVFS_handle bucket_end;
} bucket_info;

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

    /* index into fs->meta_server_list (see server-config.h) */
    struct llist *meta_server_cursor;

    /* index into fs->data_server_list (see server-config.h) */
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
    bmi_flag_t flag;
    bmi_flag_t unexpected;
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
int parse_pvfstab(char *filename, pvfs_mntlist *mnt);
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
#if 0
int get_next_segment(char *inout,char **output,int *start);
#endif
int check_perms(PVFS_object_attr attr,PVFS_permissions mode,int uid,int gid);

int PINT_do_lookup (char* name,pinode_reference parent,uint32_t mask,
                PVFS_credentials cred,pinode_reference *entry);

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
