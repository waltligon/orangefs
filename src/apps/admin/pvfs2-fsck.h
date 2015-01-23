/*
 * (C) 2004 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_FSCK_H
#define __PVFS2_FSCK_H

/* processing functions */
struct handlelist *build_handlelist(PVFS_fs_id cur_fs,
				    PVFS_BMI_addr_t *addr_array,
				    int server_count,
				    PVFS_credential *creds);

int traverse_directory_tree(PVFS_fs_id cur_fs,
			    struct handlelist *hl,
			    PVFS_BMI_addr_t *addr_array,
			    int server_count,
			    PVFS_credential *creds);

int match_dirdata(struct handlelist *hl,
		  struct handlelist *alt_hl,
		  PVFS_object_ref dir_ref,
                  int dh_count,
		  PVFS_credential *creds);

int descend(PVFS_fs_id cur_fs,
	    struct handlelist *hl,
	    struct handlelist *alt_hl,
	    PVFS_object_ref pref,
	    PVFS_credential *creds);

int verify_datafiles(PVFS_fs_id cur_fs,
		     struct handlelist *hl,
		     struct handlelist *alt_hl,
		     PVFS_object_ref mf_ref,
		     int df_count,
		     PVFS_credential *creds);

struct handlelist *find_sub_trees(PVFS_fs_id cur_fs,
				  struct handlelist *hl,
				  PVFS_id_gen_t *addr_array,
				  PVFS_credential *creds);

struct handlelist *fill_lost_and_found(PVFS_fs_id cur_fs,
				       struct handlelist *hl,
				       PVFS_id_gen_t *addr_array,
				       PVFS_credential *creds);

void cull_leftovers(PVFS_fs_id cur_fs,
		    struct handlelist *hl,
		    PVFS_id_gen_t *addr_array,
		    PVFS_credential *creds);

/* fs modification functions */
int create_lost_and_found(PVFS_fs_id cur_fs,
			  PVFS_credential *creds);

int create_dirent(PVFS_object_ref dir_ref,
		  char *name,
		  PVFS_handle handle,
		  PVFS_credential *creds);

int remove_object(PVFS_object_ref obj_ref,
		  PVFS_ds_type obj_type,
		  PVFS_credential *creds);

int remove_directory_entry(PVFS_object_ref dir_ref,
			   PVFS_object_ref entry_ref,
			   char *name,
			   PVFS_credential *creds);

/* handlelist structure, functions */
struct handlelist {
    int server_ct;
    PVFS_handle **list_array;
    unsigned long *size_array;
    unsigned long *used_array;
};

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
