/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>

#include "pvfs2.h"
#include "pvfs2-mgmt.h"

#define HANDLE_BATCH 1000

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

struct options
{
    char* mnt_point;
    int mnt_point_set;
    int key;
    char *fontname;
};

struct handlelist {
    int server_ct;
    PVFS_handle **list_array;
    int *size_array;
    int *used_array;
};

static struct options* parse_args(int argc, char* argv[]);
static void usage(int argc, char** argv);

static char *get_type_str(int type);

struct handlelist *build_handlelist(PVFS_fs_id cur_fs,
				    PVFS_BMI_addr_t *addr_array,
				    int server_count,
				    PVFS_credentials *creds);

/* directory tree traversal functions */
int traverse_directory_tree(PVFS_fs_id cur_fs,
			    struct handlelist *hl,
			    PVFS_BMI_addr_t *addr_array,
			    int server_count,
			    PVFS_credentials *creds,
			    struct options *opts_p);
int descend(PVFS_fs_id cur_fs,
	    struct handlelist *hl,
	    struct handlelist *alt_hl,
	    PVFS_object_ref pref,
	    PVFS_credentials *creds);

int verify_datafiles(PVFS_fs_id cur_fs,
		     struct handlelist *hl,
		     struct handlelist *alt_hl,
		     PVFS_object_ref mf_ref,
		     int df_count,
		     PVFS_credentials *creds);

struct handlelist *second_pass(PVFS_fs_id cur_fs,
			       struct handlelist *hl,
			       PVFS_id_gen_t *addr_array,
			       PVFS_credentials *creds);

struct handlelist *third_pass(PVFS_fs_id cur_fs,
			      struct handlelist *hl,
			      PVFS_id_gen_t *addr_array,
			      PVFS_credentials *creds);

/* print functions */
static void print_root_entry(PVFS_handle handle,
			     int server_idx,
			     char *fontname);
static void print_entry(char *name,
			PVFS_handle handle,
			PVFS_handle parent_handle,
			PVFS_ds_type objtype,
			int server_idx,
			int error,
			char *fontname);


/* handlelist functions */
static struct handlelist *handlelist_initialize(int *handle_counts,
						int server_count);
static void handlelist_add_handle(struct handlelist *hl,
				  PVFS_handle handles,
				  int server_idx);
static void handlelist_add_handles(struct handlelist *hl,
				   PVFS_handle *handles,
				   int handle_count,
				   int server_idx);
static void handlelist_finished_adding_handles(struct handlelist *hl);
static int handlelist_find_handle(struct handlelist *hl,
				  PVFS_handle handle,
				  int *server_idx_p);
static void handlelist_remove_handle(struct handlelist *hl,
				     PVFS_handle handle,
				     int server_idx);
static int handlelist_return_handle(struct handlelist *hl,
				    PVFS_handle *handle_p,
				    int *server_idx_p);
static void handlelist_finalize(struct handlelist **hl);
static void handlelist_print(struct handlelist *hl);

int main(int argc, char **argv)
{
    int ret = -1;
    PVFS_fs_id cur_fs;
    struct options *user_opts = NULL;
    char pvfs_path[PVFS_NAME_MAX] = {0};
    PVFS_credentials creds;
    int server_count;
    PVFS_BMI_addr_t *addr_array;
    struct handlelist *hl_all, *hl_second;

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if (!user_opts)
    {
	fprintf(stderr, "Error: failed to parse command line arguments.\n");
	usage(argc, argv);
	return -1;
    }

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
	PVFS_perror("PVFS_util_init_defaults", ret);
	return -1;
    }

    /* translate local path into pvfs2 relative path */
    ret = PVFS_util_resolve(user_opts->mnt_point,
			    &cur_fs,
			    pvfs_path,
			    PVFS_NAME_MAX);
    if (ret < 0)
    {
	PVFS_perror("PVFS_util_resolve", ret);
	return -1;
    }

    PVFS_util_gen_credentials(&creds);

    /* count how many servers we have */
    ret = PVFS_mgmt_count_servers(cur_fs, &creds, 
	PVFS_MGMT_IO_SERVER|PVFS_MGMT_META_SERVER,
	&server_count);
    if (ret != 0)
    {
	PVFS_perror("PVFS_mgmt_count_servers", ret);
	return -1;
    }

    /* build a list of servers to talk to */
    addr_array = (PVFS_BMI_addr_t *)
	malloc(server_count * sizeof(PVFS_BMI_addr_t));
    if (addr_array == NULL)
    {
	perror("malloc");
	return -1;
    }
    ret = PVFS_mgmt_get_server_array(cur_fs,
				     &creds, 
				     PVFS_MGMT_IO_SERVER|PVFS_MGMT_META_SERVER,
				     addr_array,
				     &server_count);
    if (ret != 0)
    {
	PVFS_perror("PVFS_mgmt_get_server_array", ret);
	return -1;
    }

    /* put the servers into administrative mode */
    ret = PVFS_mgmt_setparam_list(cur_fs,
				  &creds,
				  PVFS_SERV_PARAM_MODE,
				  (int64_t)PVFS_SERVER_ADMIN_MODE,
				  addr_array,
				  NULL,
				  server_count,
				  NULL /* detailed errors */);
    if (ret != 0)
    {
	PVFS_perror("PVFS_mgmt_setparam_list", ret);
	PVFS_mgmt_setparam_list(cur_fs,
				&creds,
				PVFS_SERV_PARAM_MODE,
				(int64_t)PVFS_SERVER_NORMAL_MODE,
				addr_array,
				NULL,
				server_count,
				NULL);
	return(-1);
    }

    /* iterate through all servers' handles and store a local
     * representation of the lists
     */
    hl_all = build_handlelist(cur_fs, addr_array, server_count, &creds);
    assert(hl_all);

    printf("# fsid is %Lu.\n", Lu(cur_fs));

    /* traverse the directory tree, cleaning up any dangling direntries
     * and fixing or removing any files that are missing datafiles.
     */
    printf("# traversing directory tree.\n");
    traverse_directory_tree(cur_fs,
			    hl_all,
			    addr_array,
			    server_count,
			    &creds,
			    user_opts);

    /* try to make sense of any handles that remain, moving complete
     * directories and files into lost+found.
     */
    printf("# analyzing remaining handles.\n");
    hl_second = second_pass(cur_fs,
			    hl_all,
			    addr_array,
			    &creds);

    handlelist_finalize(&hl_all);

    /* drop orphaned dir trees and files into lost and found */
    third_pass(cur_fs, hl_second, addr_array, &creds);

    /* get us out of admin mode */
    PVFS_mgmt_setparam_list(cur_fs,
			    &creds,
			    PVFS_SERV_PARAM_MODE,
			    (int64_t) PVFS_SERVER_NORMAL_MODE,
			    addr_array,
			    NULL,
			    server_count,
			    NULL);
    
    PVFS_sys_finalize();

    return(ret);
}

struct handlelist *build_handlelist(PVFS_fs_id cur_fs,
				    PVFS_BMI_addr_t *addr_array,
				    int server_count,
				    PVFS_credentials *creds)
{
    int ret, i, j, more_flag;
    PVFS_handle **handle_matrix;
    int *handle_count_array;
    PVFS_ds_position *position_array;
    struct PVFS_mgmt_server_stat *stat_array;
    struct handlelist *hl;

    /* find out how many handles are in use on each */
    stat_array = (struct PVFS_mgmt_server_stat *)
	malloc(server_count * sizeof(struct PVFS_mgmt_server_stat));
    if (stat_array == NULL)
    {
	PVFS_mgmt_setparam_list(cur_fs,
				creds,
				PVFS_SERV_PARAM_MODE,
				(int64_t)PVFS_SERVER_NORMAL_MODE,
				addr_array,
				NULL,
				server_count,
				NULL);
	return NULL;
    }

    ret = PVFS_mgmt_statfs_list(cur_fs,
				creds,
				stat_array,
				addr_array,
				server_count,
				NULL /* details */);
    if (ret != 0)
    {
	PVFS_perror("PVFS_mgmt_statfs_list", ret);
	PVFS_mgmt_setparam_list(cur_fs,
				creds,
				PVFS_SERV_PARAM_MODE,
				(int64_t)PVFS_SERVER_NORMAL_MODE,
				addr_array,
				NULL,
				server_count,
				NULL);
	return NULL;
    }

    /* allocate a 2 dimensional array for handles from mgmt fn. */
    handle_matrix = (PVFS_handle **)
	malloc(server_count * sizeof(PVFS_handle));
    if (handle_matrix == NULL)
    {
	perror("malloc");
	return NULL;
    }
    for (i=0; i < server_count; i++)
    {
	handle_matrix[i] = (PVFS_handle *)
	    malloc(HANDLE_BATCH * sizeof(PVFS_handle));
	if (handle_matrix[i] == NULL)
	{
	    perror("malloc");
	    return NULL;
	}
    }

    /* allocate some arrays to keep up with state */
    handle_count_array = (int *) malloc(server_count * sizeof(int));
    if (handle_count_array == NULL)
    {
	perror("malloc");
	return NULL;
    }
    position_array = (PVFS_ds_position *)
	malloc(server_count * sizeof(PVFS_ds_position));
    if (position_array == NULL)
    {
	perror("malloc");
	return NULL;
    }

    for (i=0; i < server_count; i++) {
	handle_count_array[i] = stat_array[i].handles_total_count -
	    stat_array[i].handles_available_count;

	printf("# server %d has %d used handles.\n",
	       i,
	       handle_count_array[i]);
    }

    free(stat_array);
    stat_array = NULL;

    hl = handlelist_initialize(handle_count_array, server_count);

    for (i=0; i < server_count; i++)
    {
	handle_count_array[i] = HANDLE_BATCH;
	position_array[i] = PVFS_ITERATE_START;
    }

    /* iterate until we have retrieved all handles */
    more_flag = 1;
    while (more_flag)
    {
	ret = PVFS_mgmt_iterate_handles_list(cur_fs,
					     creds,
					     handle_matrix,
					     handle_count_array,
					     position_array,
					     addr_array,
					     server_count,
					     NULL /* details */);
	if (ret < 0)
	{
	    PVFS_perror("PVFS_mgmt_iterate_handles_list", ret);
	    PVFS_mgmt_setparam_list(cur_fs,
				    creds,
				    PVFS_SERV_PARAM_MODE,
				    (int64_t)PVFS_SERVER_NORMAL_MODE,
				    addr_array,
				    NULL,
				    server_count,
				    NULL);
	    return NULL;
	}

	for (i=0; i < server_count; i++)
	{
	    for (j=0; j < handle_count_array[i]; j++)
	    {
		/* it would be good to verify that handles are
		 * within valid ranges for the given server here.
		 */
	    }

	    printf("# adding %d handles for server %d.\n",
		   handle_count_array[i],
		   i);

	    handlelist_add_handles(hl,
				   handle_matrix[i],
				   handle_count_array[i],
				   i);
	}

	/* find out if any servers have more handles to dump */
	more_flag = 0;
	for (i=0; i < server_count; i++)
	{
	    if (position_array[i] != PVFS_ITERATE_END)
	    {
		printf("# ... more handles to retrieve.\n");

		more_flag = 1;
		break;
	    }
	}
    }

    for (i = 0; i < server_count; i++)
    {
	free(handle_matrix[i]);
    }
    free(handle_matrix);
    free(handle_count_array);

    handlelist_finished_adding_handles(hl); /* sanity check */

    return hl;
}

int traverse_directory_tree(PVFS_fs_id cur_fs,
			    struct handlelist *hl,
			    PVFS_BMI_addr_t *addr_array,
			    int server_count,
			    PVFS_credentials *creds,
			    struct options *opts_p)
{
    int server_idx;
    PVFS_sysresp_lookup lookup_resp;
    PVFS_sysresp_getattr getattr_resp;
    PVFS_object_ref pref;

    PVFS_sys_lookup(cur_fs,
		    "/",
		    creds,
                    &lookup_resp,
		    PVFS2_LOOKUP_LINK_NO_FOLLOW);

    /* lookup_resp.pinode_refn.handle gets root handle */
    pref = lookup_resp.ref;

    PVFS_sys_getattr(pref,
		     PVFS_ATTR_SYS_ALL,
		     creds,
		     &getattr_resp);

    assert(getattr_resp.attr.objtype == PVFS_TYPE_DIRECTORY);

    if (handlelist_find_handle(hl, pref.handle, &server_idx) < 0)
    {
	assert(0);
    }

    handlelist_remove_handle(hl, pref.handle, server_idx);

    /* TODO: FIND AND REMOVE DIRDATA HANDLE FROM LIST */
    printf("# looking for dirdata match to %Lu.\n", Lu(pref.handle));

    descend(cur_fs, hl, NULL, pref, creds);

    return 0;
}

/* descend()
 *
 * Return -1 if you want the directory itself to be removed.
 */
int descend(PVFS_fs_id cur_fs,
	    struct handlelist *hl,
	    struct handlelist *alt_hl,
	    PVFS_object_ref dir_ref,
	    PVFS_credentials *creds)
{
    int i, count;
    PVFS_ds_position token = PVFS_READDIR_START;
    PVFS_sysresp_readdir readdir_resp;
    PVFS_sysresp_getattr getattr_resp;
    PVFS_object_ref entry_ref;

    count = 64;

    PVFS_sys_readdir(dir_ref,
		     token,
		     count,
		     creds,
		     &readdir_resp);

    /* NOTE: IF WE COULD GET ATTRIBUTES ON AN ARRAY, THIS WOULD BE
     * A GOOD TIME TO DO IT...
     */

    for (i = 0; i < readdir_resp.pvfs_dirent_outcount; i++)
    {
	int server_idx, ret, in_main_list = 0, in_alt_list = 0;
	char *cur_file;
	PVFS_handle cur_handle;

	cur_handle = readdir_resp.dirent_array[i].handle;
	cur_file   = readdir_resp.dirent_array[i].d_name;

	entry_ref.handle = cur_handle;
	entry_ref.fs_id  = cur_fs;

	if (handlelist_find_handle(hl, cur_handle, &server_idx) == 0)
	{
	    in_main_list = 1;
	}
	if (!in_main_list &&
	    alt_hl &&
	    handlelist_find_handle(alt_hl,
				   cur_handle,
				   &server_idx) == 0)
	{
	    in_alt_list = 1;
	}
	if (!in_main_list && !in_alt_list) {
	    printf("* delete directory entry for %Lu with missing metafile/directory.\n", Lu(cur_handle));
	    /* delete directory entry -- missing metafile/dir */
	    continue;
	}

	ret = PVFS_sys_getattr(entry_ref,
			       PVFS_ATTR_SYS_ALL,
			       creds,
			       &getattr_resp);
	if (ret != 0) {
	    printf("! PVFS_sys_getattr blew up on %Lu.\n", Lu(cur_handle));
	    getattr_resp.attr.objtype = 0;

	    printf("* remove directory entry for %Lu (no such thing)\n",
		   Lu(cur_handle));
	}
	else
	{
	    switch (getattr_resp.attr.objtype)
	    {
		case PVFS_TYPE_METAFILE:
		    if (verify_datafiles(cur_fs,
					 hl,
					 alt_hl,
					 entry_ref,
					 getattr_resp.attr.dfile_count,
					 creds) < 0)
		    {
			/* verify_datafiles() removed the datafiles */
			
			/* remove the handle */
			printf("* remove metafile %Lu with missing datafiles.\n",
			       Lu(cur_handle));
			
			/* remove the directory entry */
			printf("* remove directory entry for %Lu with missing metafile.\n", Lu(cur_handle));
		    }
		    
		    break;
		case PVFS_TYPE_DIRECTORY:
		    printf("# looking for dirdata match to %Lu.\n",
			   Lu(entry_ref.handle));

		    if (descend(cur_fs,
				hl,
				alt_hl,
				entry_ref,
				creds) < 0)
		    {
			/* remove directory */
			printf("* remove directory %Lu with serious problems.\n",
			       Lu(cur_handle));
		    }
		    break;
		default:
		    /* delete directory entry -- bad type for what should be
		     * metafile
		     */
		    printf("* delete directory entry for handle %Lu (bad type).\n",
			   Lu(cur_handle));
		    /* delete on handle -- unknown handle type */
		    printf("* remove handle %Lu, unknown type.\n",
			   Lu(cur_handle));
		    break;
	    }
	}

	/* remove from appropriate handle list */
	if (in_alt_list) {
	    handlelist_remove_handle(alt_hl, cur_handle, server_idx);
	}
	else if (in_main_list) {
	    handlelist_remove_handle(hl, cur_handle, server_idx);
	}

    }
    return 0;
}

/* verify_datafiles()
 *
 * Discovers the datafile handles for a given metafile,
 * verifies that they exist, and removes them from the handlelist.
 *
 * TODO: RENAME AS I FIGURE OUT WHAT EXACTLY I WANT THIS TO DO?
 */
int verify_datafiles(PVFS_fs_id cur_fs,
		     struct handlelist *hl,
		     struct handlelist *alt_hl,
		     PVFS_object_ref mf_ref,
		     int df_count,
		     PVFS_credentials *creds)
{
    int ret, i, server_idx, error = 0;
    PVFS_handle *df_handles;

    df_handles = (PVFS_handle *) malloc(df_count * sizeof(PVFS_handle));
    if (df_handles == NULL)
    {
	assert(0);
    }
    ret = PVFS_mgmt_get_dfile_array(mf_ref, creds, df_handles, df_count);
    if (ret != 0)
    {
	/* what does this mean? */
	assert(0);
    }

    for (i = 0; i < df_count; i++)
    {
	int in_main_list = 0, in_alt_list = 0;

	if (handlelist_find_handle(hl, df_handles[i], &server_idx) == 0)
	{
	    in_main_list = 1;
	}
	else if (alt_hl &&
		 (handlelist_find_handle(alt_hl, df_handles[i], &server_idx) == 0))
	{
	    in_alt_list = 1;
	}

	if ((!in_main_list) && (!in_alt_list))
	{
	    printf("# datafile handle %Lu missing from list\n",
		   Lu(df_handles[i]));
	    /* if possible, rebuild the datafile. */
	    /* otherwise delete datafiles, return error to get 
	     * handle and dirent removed.
	     */
	    df_handles[i] = PVFS_HANDLE_NULL;
	    error++;
	}

    }

    for (i = 0; i < df_count; i++)
    {
	if (df_handles[i] != PVFS_HANDLE_NULL) {
	    /* TODO: THIS IS A HACK; NEED BETTER WAY TO REMOVE FROM
	     * ONE OF TWO LISTS...
	     */

	    if (handlelist_find_handle(hl, df_handles[i], &server_idx) == 0)
	    {
		handlelist_remove_handle(hl,
					 df_handles[i],
					 server_idx);
	    }
	    else {
		handlelist_remove_handle(alt_hl,
					 df_handles[i],
					 server_idx);
	    }
	}
    }

    free(df_handles);
    return (error) ? -1 : 0;
}

/* analyze_remaining_handles
 *
 * At this point we have a list of handles that weren't referenced in
 * the directory tree.
 *
 * Plan:
 * - Make a pass to figure out what everything is
 * - For each directory, do a descend, clean up, and put it in lost+found
 *   with a name based on the handle (for uniqueness)
 *   - Later we might be smarter about keeping the structure by looking
 *     for the topmost directories in related trees, but not now?
 * - After that, for each file, verify the datafiles and if everything is ok
 *   drop them (with a name based on the handle) in lost+found
 * - After that get rid of the remaining datafiles, because they are orphans
 */
struct handlelist *second_pass(PVFS_fs_id cur_fs,
			       struct handlelist *hl_all,
			       PVFS_BMI_addr_t *addr_array,
			       PVFS_credentials *creds)
{
    int ret;
    int server_idx;
    PVFS_handle handle;
    struct handlelist *alt_hl;

    /* TODO: DON'T DIRECTLY USE THESE MEMBERS... */
    alt_hl = handlelist_initialize(hl_all->size_array,
				   hl_all->server_ct);

    /* make a pass working on directories first */
    /* Q: do we want to try to figure out who the root of the tree
     *    really is?  that could be tricky.  we could build this though.
     */
    while (handlelist_return_handle(hl_all,
				    &handle,
				    &server_idx) == 0)
    {
	PVFS_object_ref handle_ref;
	PVFS_sysresp_getattr getattr_resp;

	handle_ref.handle = handle;
	handle_ref.fs_id  = cur_fs;

	ret = PVFS_sys_getattr(handle_ref,
			       PVFS_ATTR_SYS_ALL,
			       creds,
			       &getattr_resp);
	if (ret) {
	    printf("warning: problem calling getattr on %Lu; assuming datafile for now.\n",
		   Lu(handle));
	    getattr_resp.attr.objtype = PVFS_TYPE_DATAFILE;
	}

	switch (getattr_resp.attr.objtype)
	{
	    case PVFS_TYPE_METAFILE:
		/* just hold onto this for now */
		printf("# saving %Lu (metafile) for later.\n", Lu(handle));
		handlelist_add_handle(alt_hl, handle, server_idx);
		break;
	    case PVFS_TYPE_DIRECTORY:
		/* add to directory list */
		printf("# looking for dirdata match to %Lu.\n", Lu(handle));

		descend(cur_fs,
			hl_all,
			alt_hl,
			handle_ref,
			creds);
		break;
	    case PVFS_TYPE_DATAFILE:
		/* save for later */
		printf("# saving %Lu (datafile) for later.\n", Lu(handle));
		handlelist_add_handle(alt_hl, handle, server_idx);
		break;
	    case PVFS_TYPE_DIRDATA:
		printf("# saving %Lu (dirdata) for later.\n", Lu(handle));
		handlelist_add_handle(alt_hl, handle, server_idx);
		break;
		break;
	    default:
		/* delete on handle -- unknown type */
		printf("* delete handle %Lu (unknown type).\n",
		       Lu(handle));
		break;
	}

    }

    printf("# finished second pass.\n");

    return alt_hl;
}


/* third_pass()
 *
 * In this pass we take orphaned metafiles and directories and
 * place them in lost+found.
 */
struct handlelist *third_pass(PVFS_fs_id cur_fs,
			      struct handlelist *hl_all,
			      PVFS_BMI_addr_t *addr_array,
			      PVFS_credentials *creds)
{
    int ret;
    int server_idx;
    PVFS_handle handle;
    struct handlelist *alt_hl;
    static char filename[64] = "lost+found/lostfile.";
    static char dirname[64] = "list+found/listdir.";

    /* TODO: DON'T DIRECTLY USE THESE MEMBERS... */
    alt_hl = handlelist_initialize(hl_all->size_array,
				   hl_all->server_ct);

    /* recall that return_handle removes from list */
    while (handlelist_return_handle(hl_all,
				    &handle,
				    &server_idx) == 0)
    {
	PVFS_object_ref handle_ref;
	PVFS_sysresp_getattr getattr_resp;

	handle_ref.handle = handle;
	handle_ref.fs_id  = cur_fs;

	ret = PVFS_sys_getattr(handle_ref,
			       PVFS_ATTR_SYS_ALL,
			       creds,
			       &getattr_resp);
	if (ret) {
	    printf("warning: problem calling getattr on %Lu; assuming datafile for now.\n",
		   Lu(handle));
	    getattr_resp.attr.objtype = PVFS_TYPE_DATAFILE;
	}

	switch (getattr_resp.attr.objtype)
	{
	    case PVFS_TYPE_METAFILE:
		printf("# trying to salvage %Ld (metafile).\n",
		       Lu(handle));
		if (verify_datafiles(cur_fs,
				     hl_all,
				     alt_hl,
				     handle_ref, 
				     getattr_resp.attr.dfile_count,
				     creds) != 0)
		{
		    printf("* killing broken metafile %Lu.\n", Lu(handle));
		}
		else
		{
		    sprintf(filename + strlen(filename), "%Lu", Lu(handle));
		    printf("* saving %Lu (metafile) as %s.\n",
			   Lu(handle),
			   filename);
		}
		break;
	    case PVFS_TYPE_DIRECTORY:
		/* verify dirdata */
		if (1) {
		    sprintf(dirname + strlen(dirname), "%Lu", Lu(handle));
		    printf("* saving %Lu (directory) as %s.\n",
			   Lu(handle),
			   dirname);
		}
		break;
	    case PVFS_TYPE_DATAFILE:
		/* save for later */
		printf("# saving %Lu (datafile) for later.\n", Lu(handle));
		handlelist_add_handle(alt_hl, handle, server_idx);
		break;
	    case PVFS_TYPE_DIRDATA:
		printf("# saving %Lu (dirdata) for later.\n", Lu(handle));
		handlelist_add_handle(alt_hl, handle, server_idx);
		break;
	    default:
		/* delete on handle -- unknown type */
		printf("* delete handle %Lu (unknown type).\n",
		       Lu(handle));
		break;
	}

    }

    printf("# finished third pass.\n");

    return alt_hl;
}

/********************************************/

/* handlelist_initialize()
 *
 * Does whatever is necessary to initialize the handlelist structures
 * prior to adding handles.
 *
 * handle_counts - array of counts per server
 * server_count  - number of servers
 *
 * TODO: ADD IN SUPPORT FOR TELLING THIS ABOUT RANGES?
 * TODO: REDO THIS USING QUICKLIST CODE
 */
static struct handlelist *handlelist_initialize(int *handle_counts,
						int server_count)
{
    int i;
    struct handlelist *hl;

    hl = (struct handlelist *) malloc(sizeof(struct handlelist));

    hl->server_ct = server_count;
    hl->list_array = (PVFS_handle **) malloc(server_count * sizeof(PVFS_handle *));
    hl->size_array = (int *) malloc(server_count * sizeof(int));
    hl->used_array = (int *) malloc(server_count * sizeof(int));

    for (i = 0; i < server_count; i++) {
	hl->list_array[i] = (PVFS_handle *) malloc(handle_counts[i] *
						   sizeof(PVFS_handle));
	hl->size_array[i] = handle_counts[i];
	hl->used_array[i] = 0;
    }

    /* TODO: CHECK ALL MALLOC RETURN VALUES */

    printf("initialized for %d server(s).\n", server_count);

    return hl;
}

/* handlelist_add_handles()
 *
 * Adds an array of new handle values to the list of handles for
 * a particular server.
 */
static void handlelist_add_handles(struct handlelist *hl,
				   PVFS_handle *handles,
				   int handle_count,
				   int server_idx)
{
    int i, start_off;

    start_off = hl->used_array[server_idx];

    if ((hl->size_array[server_idx] - start_off) < handle_count)
    {
	assert(0);
    }

    for (i = 0; i < handle_count; i++) {
	hl->list_array[server_idx][start_off + i] = handles[i];

	printf("adding %Ld for server %d\n", handles[i], server_idx);
    }

    hl->used_array[server_idx] += handle_count;

#if 0
    for (i=0; i < hl->used_array[server_idx]; i++) {
	printf("s[%d] = %Lu\n", server_idx, Lu(hl->list_array[server_idx][i]));
    }
#endif
}


static void handlelist_add_handle(struct handlelist *hl,
				  PVFS_handle handle,
				  int server_idx)
{
    int start_off;

    start_off = hl->used_array[server_idx];

    if ((hl->size_array[server_idx] - start_off) < 1)
    {
	assert(0);
    }

    hl->list_array[server_idx][start_off] = handle;
    hl->used_array[server_idx]++;

    printf("after add: ");
    handlelist_print(hl);
}

/* handlelist_finished_adding_handles()
 */
static void handlelist_finished_adding_handles(struct handlelist *hl)
{
    int i;
    
    for (i = 0; i < hl->server_ct; i++) {
	printf("# %d handles for server %d\n", hl->size_array[i], i);

	if (hl->used_array[i] != hl->size_array[i]) {
	    printf("warning: only found %d of %d handles for server %d.\n",
		   hl->used_array[i],
		   hl->size_array[i],
		   i);
	}
    }
}

/* handlelist_find_handle()
 *
 * Looks to see if a particular {handle, server} pair exists in the
 * handlelist for some server_idx.  Returns 0 on success (presence),
 * -1 on failure (absence).  On success also fills in server_idx.
 */
static int handlelist_find_handle(struct handlelist *hl,
				  PVFS_handle handle,
				  int *server_idx_p)
{
    int i;

    for (i = 0; i < hl->server_ct; i++) {
	int j;

	for (j = 0; j < hl->used_array[i]; j++) {
	    if (hl->list_array[i][j] == handle) {
		*server_idx_p = i;
		return 0;
	    }
	}
    }

    return -1;
}

static void handlelist_remove_handle(struct handlelist *hl,
				     PVFS_handle handle,
				     int server_idx)
{
    int i;

    printf("# before: ");
    handlelist_print(hl);

    for (i = 0; i < hl->used_array[server_idx]; i++)
    {
	if (hl->list_array[server_idx][i] == handle)
	{
	    if (i < (hl->used_array[server_idx] - 1))
	    {
		/* move last entry to this position before decrement */
		hl->list_array[server_idx][i] =
		    hl->list_array[server_idx][hl->used_array[server_idx]-1];
		
	    }
	    hl->used_array[server_idx]--;
	    break;
	}
    }

    printf("# after: ");
    handlelist_print(hl);

    assert(i < hl->used_array[server_idx]);
}

static void handlelist_print(struct handlelist *hl)
{
    int i;

    for (i=0; i < hl->used_array[0]; i++) {
	printf("%Lu ", Lu(hl->list_array[0][i]));
    }
    printf("\n");
}

/* handlelist_return_handle()
 *
 * Returns some handle still in the handlelist, removing it from the list.
 *
 * Returns 0 on success, -1 on failure.
 */
static int handlelist_return_handle(struct handlelist *hl,
				    PVFS_handle *handle_p,
				    int *server_idx_p)
{
    int i;

    for (i = 0; i < hl->server_ct; i++)
    {
	if (hl->used_array[i] > 0) {
	    *handle_p = hl->list_array[i][hl->used_array[i]-1];
	    hl->used_array[i]--;
	    *server_idx_p = i;
	    return 0;
	}
    }
    return -1;
}

static void handlelist_finalize(struct handlelist **hlp)
{
    int i;
    struct handlelist *hl = *hlp;

    for (i=0; i < hl->server_ct; i++)
    {
	free(hl->list_array[i]);
    }

    free(hl->list_array);
    free(hl->size_array);
    free(hl->used_array);

    free(hl);

    *hlp = NULL;

    return;
}

/**********************************************/

static void print_root_entry(PVFS_handle handle,
			     int server_idx,
			     char *fontname)
{
    printf("File: <Root>\n  handle = %Lu, type = %s, server = %d\n",
	   Lu(handle),
	   get_type_str(PVFS_TYPE_DIRECTORY),
	   server_idx);
}

/* print_entry()
 *
 * Parameters:
 * name          - name of object, ignored for datafiles
 * handle        - handle of object
 * parent_handle - handle of parent, for drawing connections
 * objtype       - type of object (e.g. PVFS_TYPE_DIRECTORY)
 * server_idx    - index into list of servers for location of this handle
 * error         - boolean indicating if there is an error with this entry
 */
static void print_entry(char *name,
			PVFS_handle handle,
			PVFS_handle parent_handle,
			PVFS_ds_type objtype,
			int server_idx,
			int error,
			char *fontname)
{
    switch (objtype)
    {
	case PVFS_TYPE_DATAFILE:
	    printf("  handle = %Lu, type = %s, server = %d\n",
		   Lu(handle),
		   get_type_str(objtype),
		   server_idx);
	    break;
	default:
	    printf("File: %s\n  handle = %Lu, type = %s, server = %d\n",
		   name,
		   Lu(handle),
		   get_type_str(objtype),
		   server_idx);
	    break;
    }
}

/**********************************************/

/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct options* parse_args(int argc, char* argv[])
{
    /* getopt stuff */
    extern char* optarg;
    extern int optind, opterr, optopt;
    char flags[] = "vkm:f:";
    int one_opt = 0;
    int len = 0;

    struct options* tmp_opts = NULL;
    int ret = -1;

    /* create storage for the command line options */
    tmp_opts = (struct options*) malloc(sizeof(struct options));
    if (tmp_opts == NULL)
    {
	return NULL;
    }
    memset(tmp_opts, 0, sizeof(struct options));

    /* look at command line arguments */
    while((one_opt = getopt(argc, argv, flags)) != EOF){
	switch(one_opt)
        {
            case 'v':
                printf("%s\n", PVFS2_VERSION);
                exit(0);
	    case 'm':
		len = strlen(optarg)+1;
		tmp_opts->mnt_point = (char *) malloc(len + 1);
		if (tmp_opts->mnt_point == NULL)
		{
		    free(tmp_opts);
		    return NULL;
		}
		memset(tmp_opts->mnt_point, 0, len+1);
		ret = sscanf(optarg, "%s", tmp_opts->mnt_point);
		if (ret < 1)
		{
		    free(tmp_opts);
		    return NULL;
		}
		/* TODO: dirty hack... fix later.  The remove_dir_prefix()
		 * function expects some trailing segments or at least
		 * a slash off of the mount point
		 */
		strcat(tmp_opts->mnt_point, "/");
		tmp_opts->mnt_point_set = 1;
		break;
	    case 'k':
		tmp_opts->key = 1;
		break;
	    case 'f':
		if ((len = strlen(optarg)) > 0)
		{
		    tmp_opts->fontname = (char *) malloc(len + 1);
		    strncpy(tmp_opts->fontname, optarg, len);
		}
		else {
		    usage(argc, argv);
		    exit(EXIT_FAILURE);
		}
		break;
	    case '?':
		usage(argc, argv);
		exit(EXIT_FAILURE);
	}
    }

    if(!tmp_opts->mnt_point_set)
    {
	free(tmp_opts);
	return NULL;
    }

    return tmp_opts;
}


static void usage(int argc, char** argv)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage  : %s [-dv] [-m fs_mount_point]\n",
	argv[0]);
    fprintf(stderr, "Display information about contents of file system.\n");
    fprintf(stderr, "    -k            when used with -d, prints key\n");
    fprintf(stderr, "    -f <font>     when used with -d, specifies a font name (e.g. Helvetica)\n");
    fprintf(stderr, "  -v              print version and exit\n");
    fprintf(stderr, "Example: %s -m /mnt/pvfs2\n",
	argv[0]);
    return;
}

static char *get_type_str(int type)
{
    char *ret = "Unknown (<== ERROR)";
    static char *type_strs[] =
    {
        "None", "Metafile", "Datafile",
        "Directory", "Symlink", "DirData"
    };

    switch(type)
    {
        case PVFS_TYPE_NONE:
            ret = type_strs[0];
            break;
        case PVFS_TYPE_METAFILE:
            ret = type_strs[1];
            break;
        case PVFS_TYPE_DATAFILE:
            ret = type_strs[2];
            break;
        case PVFS_TYPE_DIRECTORY:
            ret = type_strs[3];
            break;
        case PVFS_TYPE_SYMLINK:
            ret = type_strs[4];
            break;
        case PVFS_TYPE_DIRDATA:
            ret = type_strs[5];
            break;
    }
    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

