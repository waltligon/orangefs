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
#include <getopt.h>

#include "pvfs2.h"
#include "pvfs2-mgmt.h"
#include "pvfs2-internal.h"
#include "pint-cached-config.h"

#define HANDLE_BATCH 1000

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

struct options
{
    char* mnt_point;
    int mnt_point_set;
    int dot_format;
    int key;
    char *fontname;
};

static struct options* parse_args(int argc, char* argv[]);
static void usage(int argc, char** argv);

static char *get_type_str(int type);

int build_handlelist(PVFS_fs_id cur_fs,
		     PVFS_BMI_addr_t *addr_array,
		     int server_count,
		     PVFS_credentials *creds);

/* directory tree traversal functions */
int traverse_directory_tree(PVFS_fs_id cur_fs,
			    PVFS_BMI_addr_t *addr_array,
			    int server_count,
			    PVFS_credentials *creds,
			    struct options *opts_p);
int descend(PVFS_fs_id cur_fs,
	    PVFS_object_ref pref,
	    PVFS_credentials *creds,
	    struct options *opts_p);

void verify_datafiles(PVFS_fs_id cur_fs,
		      PVFS_object_ref mf_ref,
		      int df_count,
		      PVFS_credentials *creds,
		      struct options *opts_p);

void analyze_remaining_handles(PVFS_fs_id cur_fs,
			       PVFS_id_gen_t *addr_array,
			       PVFS_credentials *creds,
			       int dot_fmt);

/* print functions */
static void print_header(int dot_fmt, int key, char *fontname);
static void print_trailer(int dot_fmt, char *fontname);
static void print_root_entry(PVFS_handle handle,
			     int server_idx,
			     int dot_fmt,
			     char *fontname);
static void print_entry(char *name,
			PVFS_handle handle,
			PVFS_handle parent_handle,
			PVFS_ds_type objtype,
			int server_idx,
			int error,
			int dot_fmt,
			char *fontname);


/* handlelist functions and globals */
static void handlelist_initialize(unsigned long *handle_counts, int server_count);
static void handlelist_add_handles(PVFS_handle *handles,
				   unsigned long handle_count,
				   int server_idx);
static void handlelist_finished_adding_handles(void);
static int handlelist_find_handle(PVFS_handle handle, int *server_idx_p);
static void handlelist_remove_handle(PVFS_handle handle, int server_idx);
static int handlelist_return_handle(PVFS_handle *handle_p, int *server_idx_p);
static void handlelist_finalize(void);

static PVFS_handle **handlelist_list = NULL;
static unsigned long *handlelist_size;
static unsigned long *handlelist_used;
static int handlelist_server_count;

int main(int argc, char **argv)
{
    int ret = -1;
    PVFS_fs_id cur_fs;
    struct options* user_opts = NULL;
    char pvfs_path[PVFS_NAME_MAX] = {0};
    PVFS_credentials creds;
    int server_count;
    PVFS_BMI_addr_t *addr_array;
    struct PVFS_mgmt_setparam_value param_value;

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if (!user_opts)
    {
	fprintf(stderr, "Error: failed to parse command line arguments.\n");
	usage(argc, argv);
	return -1;
    }

    ret = PVFS_util_init_defaults();
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_init_defaults", ret);
	return(-1);
    }

    /* translate local path into pvfs2 relative path */
    ret = PVFS_util_resolve(user_opts->mnt_point,
        &cur_fs, pvfs_path, PVFS_NAME_MAX);
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_resolve", ret);
	return(-1);
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
    ret = PVFS_mgmt_get_server_array(cur_fs, &creds, 
	PVFS_MGMT_IO_SERVER|PVFS_MGMT_META_SERVER,
	addr_array, &server_count);
    if (ret != 0)
    {
	PVFS_perror("PVFS_mgmt_get_server_array", ret);
	return -1;
    }

    param_value.type = PVFS_MGMT_PARAM_TYPE_UINT64;
    param_value.u.value = PVFS_SERVER_ADMIN_MODE;

    /* put the servers into administrative mode */
    ret = PVFS_mgmt_setparam_list(cur_fs,
                                  &creds,
                                  PVFS_SERV_PARAM_MODE,
                                  &param_value,
                                  addr_array,
                                  server_count,
                                  NULL, /* detailed errors */
                                  NULL);
    if (ret != 0)
    {
        param_value.type = PVFS_MGMT_PARAM_TYPE_UINT64;
        param_value.u.value = PVFS_SERVER_NORMAL_MODE;

	PVFS_perror("PVFS_mgmt_setparam_list", ret);
	PVFS_mgmt_setparam_list(cur_fs,
				&creds,
				PVFS_SERV_PARAM_MODE,
				&param_value,
				addr_array,
				server_count,
				NULL, NULL);
	return(-1);
    }



    /* iterate through all servers' handles and store a local
     * representation of the lists
     */
    build_handlelist(cur_fs, addr_array, server_count, &creds);

    print_header(user_opts->dot_format, user_opts->key, user_opts->fontname);

    traverse_directory_tree(cur_fs,
			    addr_array,
			    server_count,
			    &creds,
			    user_opts);

    analyze_remaining_handles(cur_fs,
			      addr_array,
			      &creds,
			      user_opts->dot_format);

    print_trailer(user_opts->dot_format, user_opts->fontname);

    handlelist_finalize();

    param_value.type = PVFS_MGMT_PARAM_TYPE_UINT64;
    param_value.u.value = PVFS_SERVER_NORMAL_MODE;
    PVFS_mgmt_setparam_list(
	cur_fs,
	&creds,
	PVFS_SERV_PARAM_MODE,
	&param_value,
	addr_array,
	server_count,
	NULL, NULL);

    PVFS_sys_finalize();

    return(ret);
}

int build_handlelist(PVFS_fs_id cur_fs,
		     PVFS_BMI_addr_t *addr_array,
		     int server_count,
		     PVFS_credentials *creds)
{
    int ret, i, more_flag;
    unsigned long j;
    PVFS_handle **handle_matrix;
    int  *hcount_array;
    unsigned long *handle_count_array;
    unsigned long *total_count_array;
    PVFS_ds_position *position_array;
    struct PVFS_mgmt_server_stat *stat_array;
    struct PVFS_mgmt_setparam_value param_value;

    /* find out how many handles are in use on each */
    stat_array = (struct PVFS_mgmt_server_stat *)
	malloc(server_count * sizeof(struct PVFS_mgmt_server_stat));
    if (stat_array == NULL)
    {
        param_value.type = PVFS_MGMT_PARAM_TYPE_UINT64;
        param_value.u.value = PVFS_SERVER_NORMAL_MODE;
	PVFS_mgmt_setparam_list(cur_fs,
				creds,
				PVFS_SERV_PARAM_MODE,
				&param_value,
				addr_array,
				server_count,
				NULL, NULL);
	return -1;
    }

    ret = PVFS_mgmt_statfs_list(cur_fs,
				creds,
				stat_array,
				addr_array,
				server_count,
				NULL /* details */
                , NULL);
    if (ret != 0)
    {
        param_value.type = PVFS_MGMT_PARAM_TYPE_UINT64;
        param_value.u.value = PVFS_SERVER_NORMAL_MODE;
        PVFS_perror("PVFS_mgmt_statfs_list", ret);
	PVFS_mgmt_setparam_list(cur_fs,
				creds,
				PVFS_SERV_PARAM_MODE,
                                &param_value,
				addr_array,
				server_count,
				NULL, NULL);
	return -1;
    }

    /* allocate a 2 dimensional array for handles from mgmt fn. */
    handle_matrix = (PVFS_handle **) calloc(server_count, sizeof(PVFS_handle));
    if (handle_matrix == NULL)
    {
	perror("malloc");
	return -1;
    }
    for (i=0; i < server_count; i++)
    {
	handle_matrix[i] = (PVFS_handle *) calloc(HANDLE_BATCH, sizeof(PVFS_handle));
	if (handle_matrix[i] == NULL)
	{
	    perror("malloc");
	    return -1;
	}
    }

    /* allocate some arrays to keep up with state */
    handle_count_array = (unsigned long *) calloc(server_count, sizeof(unsigned long));
    if (handle_count_array == NULL)
    {
	perror("malloc");
	return -1;
    }
    position_array = (PVFS_ds_position *) calloc(server_count, sizeof(PVFS_ds_position));
    if (position_array == NULL)
    {
	perror("malloc");
	return -1;
    }
    /* total_count_array */
    total_count_array = (unsigned long *) calloc(server_count, sizeof(unsigned long));
    if (total_count_array == NULL)
    {
        perror("malloc");
        return -1;
    }
    /* hcount array */
    hcount_array = (int *) calloc(server_count, sizeof(int));
    if (hcount_array == NULL)
    {
        perror("malloc:");
        return -1;
    }

    for (i=0; i < server_count; i++) {
	handle_count_array[i] = stat_array[i].handles_total_count -
	    stat_array[i].handles_available_count;
        total_count_array[i] = 0;
    }


    handlelist_initialize(handle_count_array, server_count);

    for (i=0; i < server_count; i++)
    {
	hcount_array[i] = HANDLE_BATCH;
	position_array[i] = PVFS_ITERATE_START;
    }

    /* iterate until we have retrieved all handles */
    do
    {
	ret = PVFS_mgmt_iterate_handles_list(cur_fs,
					     creds,
					     handle_matrix,
					     hcount_array,
					     position_array,
					     addr_array,
					     server_count,
                                             0,
					     NULL /* details */,
                                             NULL /* hints */);
	if (ret < 0)
	{
            param_value.type = PVFS_MGMT_PARAM_TYPE_UINT64;
            param_value.u.value = PVFS_SERVER_NORMAL_MODE;
            PVFS_perror("PVFS_mgmt_iterate_handles_list", ret);
	    PVFS_mgmt_setparam_list(cur_fs,
				    creds,
				    PVFS_SERV_PARAM_MODE,
				    &param_value,
				    addr_array,
				    server_count,
				    NULL, NULL);
	    return -1;
	}

	for (i=0; i < server_count; i++)
	{
            total_count_array[i] += hcount_array[i];
	    for (j = 0; j < hcount_array[i]; j++)
	    {
                PVFS_BMI_addr_t tmp_addr;
		/* verify that handles are
		 * within valid ranges for the given server here.
		 */
                ret = PINT_cached_config_map_to_server(&tmp_addr, handle_matrix[i][j], cur_fs);
                if (ret || tmp_addr != addr_array[i])
                {
                    fprintf(stderr, "Ugh! handle does not seem to be owned by the server!\n");
                    return -1;
                }
	    }

	    handlelist_add_handles(handle_matrix[i],
				   hcount_array[i],
				   i);
	}

	/* find out if any servers have more handles to dump */
	more_flag = 0;
	for (i = 0; i < server_count; i++)
	{
	    if (position_array[i] != PVFS_ITERATE_END)
	    {
		more_flag = 1;
		break;
	    }
	}
    } while(more_flag);

    for (i = 0; i < server_count; i++)
    {
        unsigned long used_handles = handle_count_array[i];
        if (total_count_array[i] != used_handles)
        {
            fprintf(stderr, "Ugh! Server %d, Received %ld total handles instead of %ld\n",
                    i, total_count_array[i], used_handles);
            return -1;
        }
	free(handle_matrix[i]);
    }

    free(handle_matrix);
    free(handle_count_array);
    free(hcount_array);
    free(total_count_array);
    free(position_array);

    handlelist_finished_adding_handles();
    free(stat_array);
    stat_array = NULL;

    return 0;
}

int traverse_directory_tree(PVFS_fs_id cur_fs,
			    PVFS_BMI_addr_t *addr_array,
			    int server_count,
			    PVFS_credentials *creds,
			    struct options *opts_p)
{
    int server_idx;
    PVFS_sysresp_lookup lookup_resp;
    PVFS_sysresp_getattr getattr_resp;
    PVFS_object_ref pref;

    PVFS_sys_lookup(cur_fs, "/", creds,
                    &lookup_resp, PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);
    /* lookup_resp.pinode_refn.handle gets root handle */
    pref = lookup_resp.ref;

    PVFS_sys_getattr(pref,
		     PVFS_ATTR_SYS_ALL_NOHINT,
		     creds,
		     &getattr_resp, NULL);

    if (getattr_resp.attr.objtype != PVFS_TYPE_DIRECTORY)
    {
	fprintf(stderr, "Cannot traverse object at "
                "%llu,%d (Not a Valid Directory)\n",
                llu(pref.handle), pref.fs_id);
        return -1;
    }

    if (handlelist_find_handle(pref.handle, &server_idx) < 0)
    {
        printf("Handle %llu appears to be missing; skipping!\n",
               llu(pref.handle));
        return -1;
    }

    print_root_entry(pref.handle,
		     server_idx,
		     opts_p->dot_format,
		     opts_p->fontname);

    descend(cur_fs, pref, creds, opts_p);

    handlelist_remove_handle(pref.handle, server_idx);

    return 0;
}

int descend(PVFS_fs_id cur_fs,
	    PVFS_object_ref pref,
	    PVFS_credentials *creds,
	    struct options *opts_p)
{
    int i, count, ret;
    PVFS_ds_position token;
    PVFS_sysresp_readdir readdir_resp;
    PVFS_sysresp_getattr getattr_resp;
    PVFS_object_ref entry_ref;

    count = 64;

    token = 0;
    do {
        memset(&readdir_resp, 0, sizeof(PVFS_sysresp_readdir));
        ret = PVFS_sys_readdir(pref,
                         (!token ? PVFS_READDIR_START : token),
                         count,
                         creds,
                         &readdir_resp, NULL);

        for (i = 0; i < readdir_resp.pvfs_dirent_outcount; i++)
        {
            int server_idx;
            char *cur_file;
            PVFS_handle cur_handle;

            cur_handle = readdir_resp.dirent_array[i].handle;
            cur_file   = readdir_resp.dirent_array[i].d_name;

            entry_ref.handle = cur_handle;
            entry_ref.fs_id  = cur_fs;

            if ((ret = PVFS_sys_getattr(entry_ref,
                             PVFS_ATTR_SYS_ALL_NOHINT,
                             creds,
                             &getattr_resp, NULL)) != 0)
            {
                printf("Could not get attributes of handle %llu [%d]\n",
                        llu(cur_handle), ret);
                continue;
            }


            if (handlelist_find_handle(cur_handle, &server_idx) < 0)
            {
                printf("Handle %llu appears to be missing; skipping!\n",
                       llu(cur_handle));
                continue;
            }

            print_entry(cur_file,
                        cur_handle,
                        pref.handle, /* parent handle */
                        getattr_resp.attr.objtype,
                        server_idx,
                        0,
                        opts_p->dot_format,
                        opts_p->fontname);

            switch (getattr_resp.attr.objtype)
            {
                case PVFS_TYPE_METAFILE:
                    verify_datafiles(cur_fs,
                                     entry_ref,
                                     getattr_resp.attr.dfile_count,
                                     creds,
                                     opts_p);
                    break;
                case PVFS_TYPE_DIRECTORY:
                    descend(cur_fs, entry_ref, creds, opts_p);
                    break;
                default:
                    break;
            }

            handlelist_remove_handle(cur_handle, server_idx);
        }
        token = readdir_resp.token;
        if (readdir_resp.pvfs_dirent_outcount)
        {
            free(readdir_resp.dirent_array);
            readdir_resp.dirent_array = NULL;
        }
    } while (readdir_resp.pvfs_dirent_outcount == count);

    if (readdir_resp.pvfs_dirent_outcount)
    {
        free(readdir_resp.dirent_array);
        readdir_resp.dirent_array = NULL;
    }
    return 0;
}

/* verify_datafiles()
 *
 * Discovers the datafile handles for a given metafile,
 * verifies that they exist, and removes them from the handlelist.
 */
void verify_datafiles(PVFS_fs_id cur_fs,
		      PVFS_object_ref mf_ref,
		      int df_count,
		      PVFS_credentials *creds,
		      struct options *opts_p)
{
    int ret, i, server_idx;
    PVFS_handle *df_handles;

    df_handles = (PVFS_handle *) malloc(df_count * sizeof(PVFS_handle));
    if (df_handles == NULL)
    {
        printf("invalid value of number of datafiles = %d\n", df_count);
	assert(0);
    }
    ret = PVFS_mgmt_get_dfile_array(mf_ref, creds, df_handles, df_count, NULL);
    if (ret != 0)
    {
	assert(0);
    }

    for (i = 0; i < df_count; i++)
    {
	ret = handlelist_find_handle(df_handles[i], &server_idx);
	if (ret != 0)
	{
            printf("Datafile Handle %llu appears to be missing; "
                   "skipping!\n", llu(df_handles[i]));
            continue;
	}

	print_entry(NULL,
		    df_handles[i],
		    mf_ref.handle,
		    PVFS_TYPE_DATAFILE,
		    server_idx,
		    0,
		    opts_p->dot_format,
		    opts_p->fontname);

	handlelist_remove_handle(df_handles[i], server_idx);
    }

    free(df_handles);
}

void analyze_remaining_handles(PVFS_fs_id cur_fs,
			       PVFS_BMI_addr_t *addr_array,
			       PVFS_credentials *creds,
			       int dot_fmt)
{
    PVFS_handle handle;
    int server_idx, tmp_type, flag = 1;

    if (!dot_fmt) {
	printf("remaining handles:\n");
    }

    while (handlelist_return_handle(&handle, &server_idx) == 0)
    {
        PVFS_sysresp_getattr getattr_resp;
        PVFS_object_ref entry_ref;
        char* fmt_string;

        entry_ref.handle = handle;
        entry_ref.fs_id  = cur_fs;
        /* only remaining handles are dirdata */
        PVFS_sys_getattr(entry_ref,
                         PVFS_ATTR_SYS_ALL,
                         creds, &getattr_resp, NULL);
        if (getattr_resp.attr.objtype != PVFS_TYPE_DIRDATA)
        {
            flag = 0;
            if (dot_fmt && getattr_resp.attr.objtype != PVFS_TYPE_INTERNAL &&
                getattr_resp.attr.objtype != PVFS_TYPE_DATAFILE)
            {
                printf("\tH%llu [shape=record, color=red, label = \"{(unknown) "
                       "| %llu (%d)}\"];\n",
                       llu(handle),
                       llu(handle),
                       server_idx);
            }
            else if(!dot_fmt)
            {
                if(getattr_resp.attr.objtype == PVFS_TYPE_INTERNAL)
                    fmt_string = "\t%s: %llu (server internal use)\n";
                else if(getattr_resp.attr.objtype == PVFS_TYPE_DATAFILE)
                    fmt_string = "\t%s: %llu (datafile, probably preallocated)\n";
                else
                    fmt_string = "\t%s: %llu (unknown)\n";

                printf(fmt_string,
                       PVFS_mgmt_map_addr(cur_fs,
                                          creds,
                                          addr_array[server_idx],
                                          &tmp_type),
                       llu(handle));
            }
        }
    }
    if (flag) {
        printf("pvfs-fs-dump: All handles acounted for!\n");
    }
    return;
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
 */
static void handlelist_initialize(unsigned long *handle_counts, int server_count)
{
    int i;

    handlelist_server_count = server_count;
    handlelist_list = (PVFS_handle **) calloc(server_count, sizeof(PVFS_handle *));
    handlelist_size = (unsigned long *) calloc(server_count, sizeof(unsigned long));
    handlelist_used = (unsigned long *) calloc(server_count, sizeof(unsigned long));

    for (i = 0; i < server_count; i++) 
    {
	handlelist_list[i] = (PVFS_handle *) calloc(handle_counts[i],  sizeof(PVFS_handle));
	handlelist_size[i] = handle_counts[i];
	handlelist_used[i] = 0;
    }
}

/* handlelist_add_handles()
 *
 * Adds an array of new handle values to the list of handles for
 * a particular server.
 */
static void handlelist_add_handles(PVFS_handle *handles,
				   unsigned long handle_count,
				   int server_idx)
{
    unsigned long i, start_off;

    start_off = handlelist_used[server_idx];

    if ((handlelist_size[server_idx] - start_off) < handle_count)
    {
        fprintf(stderr, "server %d, exceeding number of handles it declared (%ld), currently (%ld)\n",
                server_idx, handlelist_size[server_idx], (start_off + handle_count));
	assert(0);
    }

    for (i = 0; i < handle_count; i++) {
	handlelist_list[server_idx][start_off + i] = handles[i];
    }

    handlelist_used[server_idx] += handle_count;
}

static void handlelist_finished_adding_handles(void)
{
    int i;
    
    for (i = 0; i < handlelist_server_count; i++) {
	if (handlelist_used[i] != handlelist_size[i]) assert(0);
	printf("# %ld handles for server %d\n", handlelist_size[i], i);
    }
}

/* handlelist_find_handle()
 *
 * Looks to see if a particular {handle, server} pair exists in the
 * handlelist for some server_idx.  Returns 0 on success (presence),
 * -1 on failure (absence).  On success also fills in server_idx.
 */
static int handlelist_find_handle(PVFS_handle handle, int *server_idx_p)
{
    int i;

    for (i = 0; i < handlelist_server_count; i++) {
	unsigned long j;

	for (j = 0; j < handlelist_used[i]; j++) {
	    if (handlelist_list[i][j] == handle) {
		*server_idx_p = i;
		return 0;
	    }
	}
    }

    return -1;
}

static void handlelist_remove_handle(PVFS_handle handle, int server_idx)
{
    unsigned long i;

    for (i = 0; i < handlelist_used[server_idx]; i++)
    {
	if (handlelist_list[server_idx][i] == handle)
	{
	    if (i < (handlelist_used[server_idx] - 1))
	    {
		/* move last entry to this position before decrement */
		handlelist_list[server_idx][i] =
		    handlelist_list[server_idx][handlelist_used[server_idx]-1];
		
	    }
	    handlelist_used[server_idx]--;
	    return;
	}
    }

    assert(0);
}

/* handlelist_return_handle()
 *
 * Returns some handle still in the handlelist, removing it from the list.
 *
 * Returns 0 on success, -1 on failure.
 */
static int handlelist_return_handle(PVFS_handle *handle_p, int *server_idx_p)
{
    int i;

    for (i = 0; i < handlelist_server_count; i++)
    {
	if (handlelist_used[i] > 0) {
	    *handle_p = handlelist_list[i][handlelist_used[i]-1];
	    handlelist_used[i]--;
	    *server_idx_p = i;
	    return 0;
	}
    }
    return -1;
}

static void handlelist_finalize(void)
{
}

/**********************************************/

static void print_header(int dot_fmt,
			 int key,
			 char *fontname)
{
    if (dot_fmt) {
	if (fontname != NULL) {
	    printf("digraph %d {\n\tnode [fontname = \"%s\"];\n",
		   getpid(),
		   fontname);
	}
	else {
	    printf("digraph %d {\n",
		   getpid());
	}
	if (key) {
	    printf("\tsubgraph cluster1 {\n\t\t\"Datafile\" [shape=ellipse, style=filled, fillcolor=violet];\n\t\t\"Metafile\" [shape=record, style=filled, fillcolor=aquamarine];\n\t\t\"Directory\" [shape=record, style=filled, fillcolor=grey];\n\t\t\"Missing Datafile\" [shape=ellipse, style=dashed, color=red];\n\t\t\"Datafile\" -> \"Metafile\" [style=invis];\n\t\t\"Metafile\" -> \"Directory\" [style=invis];\n\t\t\"Directory\" -> \"Missing Datafile\" [style=invis];\n\t\tstyle=dotted;\n\t\tlabel = \"Key\";\t}\n");
	}
    }
}

static void print_trailer(int dot_fmt,
			  char *fontname)
{
    if (dot_fmt) {
	printf("\t}\n");
    }
}

static void print_root_entry(PVFS_handle handle,
			     int server_idx,
			     int dot_fmt,
			     char *fontname)
{
    if (dot_fmt)
    {
	printf("\tH%llu [shape = record, fillcolor = grey, style = filled, label = \"{/ | %llu (%d)}\"];\n",
	       llu(handle),
	       llu(handle),
	       server_idx);
    }
    else
    {
	printf("File: <Root>\n  handle = %llu, type = %s, server = %d\n",
	       llu(handle),
	       get_type_str(PVFS_TYPE_DIRECTORY),
	       server_idx);
    }
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
 * dot_fmt       - boolean indicating if output should be in dot format
 */
static void print_entry(char *name,
			PVFS_handle handle,
			PVFS_handle parent_handle,
			PVFS_ds_type objtype,
			int server_idx,
			int error,
			int dot_fmt,
			char *fontname)
{
    if (dot_fmt)
    {
	/* always show connector */
	printf("\tH%llu -> H%llu [style = bold];\n",
	       llu(parent_handle),
	       llu(handle));
	switch (objtype) {
	    case PVFS_TYPE_DIRECTORY:
		printf("\tH%llu [shape = record, fillcolor = grey, style = filled, label = \"{%s/ | %llu (%d)}\"];\n",
		       llu(handle), name, llu(handle), server_idx);
		break;
	    case PVFS_TYPE_METAFILE:
		printf("\tH%llu [shape = record, fillcolor = aquamarine, style = filled, label = \"{%s | %llu (%d)}\"];\n",
		       llu(handle), name, llu(handle), server_idx);
		break;
	    case PVFS_TYPE_DATAFILE:
		printf("\tH%llu [shape = ellipse, fillcolor = violet, style = filled, label =\"%llu (%d)\"];\n",
		       llu(handle), llu(handle), server_idx);
		break;
	    case PVFS_TYPE_DIRDATA:
		break;
	    case PVFS_TYPE_SYMLINK:
		break;
	    default:
		break;
	}
    }
    else
    {
	switch (objtype)
        {
	    case PVFS_TYPE_DATAFILE:
		printf("  handle = %llu, type = %s, server = %d\n",
		       llu(handle),
		       get_type_str(objtype),
		       server_idx);
		break;
	    default:
		printf("File: %s\n  handle = %llu, type = %s, server = %d\n",
                       name,
		       llu(handle),
		       get_type_str(objtype),
		       server_idx);
		break;
	}
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
    char flags[] = "dvkm:f:";
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
	    case 'd':
		tmp_opts->dot_format = 1;
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
    fprintf(stderr, "  -d              output in format suitable for dot\n");
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

