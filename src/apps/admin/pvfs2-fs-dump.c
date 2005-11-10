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
static void handlelist_initialize(int *handle_counts, int server_count);
static void handlelist_add_handles(PVFS_handle *handles,
				   int handle_count,
				   int server_idx);
static void handlelist_finished_adding_handles(void);
static int handlelist_find_handle(PVFS_handle handle, int *server_idx_p);
static void handlelist_remove_handle(PVFS_handle handle, int server_idx);
static int handlelist_return_handle(PVFS_handle *handle_p, int *server_idx_p);
static void handlelist_finalize(void);

static PVFS_handle **handlelist_list = NULL;
static int *handlelist_size;
static int *handlelist_used;
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

    /* put the servers into administrative mode */
    ret = PVFS_mgmt_setparam_list(cur_fs,
				  &creds,
				  PVFS_SERV_PARAM_MODE,
				  (uint64_t)PVFS_SERVER_ADMIN_MODE,
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
				(uint64_t)PVFS_SERVER_NORMAL_MODE,
				addr_array,
				NULL,
				server_count,
				NULL);
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

    PVFS_mgmt_setparam_list(
	cur_fs,
	&creds,
	PVFS_SERV_PARAM_MODE,
	(uint64_t)PVFS_SERVER_NORMAL_MODE,
	addr_array,
	NULL,
	server_count,
	NULL);

    PVFS_sys_finalize();

    return(ret);
}

int build_handlelist(PVFS_fs_id cur_fs,
		     PVFS_BMI_addr_t *addr_array,
		     int server_count,
		     PVFS_credentials *creds)
{
    int ret, i, j, more_flag;
    PVFS_handle **handle_matrix;
    int *handle_count_array;
    PVFS_ds_position *position_array;
    struct PVFS_mgmt_server_stat *stat_array;

    /* find out how many handles are in use on each */
    stat_array = (struct PVFS_mgmt_server_stat *)
	malloc(server_count * sizeof(struct PVFS_mgmt_server_stat));
    if (stat_array == NULL)
    {
	PVFS_mgmt_setparam_list(cur_fs,
				creds,
				PVFS_SERV_PARAM_MODE,
				(uint64_t)PVFS_SERVER_NORMAL_MODE,
				addr_array,
				NULL,
				server_count,
				NULL);
	return -1;
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
				(uint64_t)PVFS_SERVER_NORMAL_MODE,
				addr_array,
				NULL,
				server_count,
				NULL);
	return -1;
    }

    /* allocate a 2 dimensional array for handles from mgmt fn. */
    handle_matrix = (PVFS_handle **)
	malloc(server_count * sizeof(PVFS_handle));
    if (handle_matrix == NULL)
    {
	perror("malloc");
	return -1;
    }
    for (i=0; i < server_count; i++)
    {
	handle_matrix[i] = (PVFS_handle *)
	    malloc(HANDLE_BATCH * sizeof(PVFS_handle));
	if (handle_matrix[i] == NULL)
	{
	    perror("malloc");
	    return -1;
	}
    }

    /* allocate some arrays to keep up with state */
    handle_count_array = (int *) malloc(server_count * sizeof(int));
    if (handle_count_array == NULL)
    {
	perror("malloc");
	return -1;
    }
    position_array = (PVFS_ds_position *)
	malloc(server_count * sizeof(PVFS_ds_position));
    if (position_array == NULL)
    {
	perror("malloc");
	return -1;
    }

    for (i=0; i < server_count; i++) {
	handle_count_array[i] = stat_array[i].handles_total_count -
	    stat_array[i].handles_available_count;
    }

    free(stat_array);
    stat_array = NULL;

    handlelist_initialize(handle_count_array, server_count);

    for (i=0; i < server_count; i++)
    {
	handle_count_array[i] = HANDLE_BATCH;
	position_array[i] = PVFS_ITERATE_START;
    }

    /* iterate until we have retrieved all handles */
    do
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
				    (uint64_t)PVFS_SERVER_NORMAL_MODE,
				    addr_array,
				    NULL,
				    server_count,
				    NULL);
	    return -1;
	}

	for (i=0; i < server_count; i++)
	{
	    for (j=0; j<handle_count_array[i]; j++)
	    {
		/* it would be good to verify that handles are
		 * within valid ranges for the given server here.
		 */
	    }

	    handlelist_add_handles(handle_matrix[i],
				   handle_count_array[i],
				   i);
	}

	/* find out if any servers have more handles to dump */
	more_flag = 0;
	for (i=0; i < server_count; i++)
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
	free(handle_matrix[i]);
    }
    free(handle_matrix);
    free(handle_count_array);

    handlelist_finished_adding_handles();

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
                    &lookup_resp, PVFS2_LOOKUP_LINK_NO_FOLLOW);
    /* lookup_resp.pinode_refn.handle gets root handle */
    pref = lookup_resp.ref;

    PVFS_sys_getattr(pref,
		     PVFS_ATTR_SYS_ALL,
		     creds,
		     &getattr_resp);

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
    int i, count;
    PVFS_ds_position token = PVFS_READDIR_START;
    PVFS_sysresp_readdir readdir_resp;
    PVFS_sysresp_getattr getattr_resp;
    PVFS_object_ref entry_ref;

    count = 64;

    PVFS_sys_readdir(pref,
		     token,
		     count,
		     creds,
		     &readdir_resp);

    /* NOTE: IF WE COULD GET ATTRIBUTES ON AN ARRAY, THIS WOULD BE
     * A GOOD TIME TO DO IT...
     */

    for (i = 0; i < readdir_resp.pvfs_dirent_outcount; i++)
    {
	int server_idx;
	char *cur_file;
	PVFS_handle cur_handle;

	cur_handle = readdir_resp.dirent_array[i].handle;
	cur_file   = readdir_resp.dirent_array[i].d_name;

	entry_ref.handle = cur_handle;
	entry_ref.fs_id  = cur_fs;

	PVFS_sys_getattr(entry_ref,
			 PVFS_ATTR_SYS_ALL,
			 creds,
			 &getattr_resp);


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
	assert(0);
    }
    ret = PVFS_mgmt_get_dfile_array(mf_ref, creds, df_handles, df_count);
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
    int server_idx, tmp_type;

    if (!dot_fmt) {
	printf("remaining handles:\n");
    }

    while (handlelist_return_handle(&handle, &server_idx) == 0)
    {
	if (dot_fmt)
	{
	    printf("\tH%llu [shape=record, color=red, label = \"{(unknown) "
                   "| %llu (%d)}\"];\n",
		   llu(handle),
		   llu(handle),
		   server_idx);
	}
	else
	{
	    printf("\t%s: %llu\n",
		   PVFS_mgmt_map_addr(cur_fs,
				      creds,
				      addr_array[server_idx],
				      &tmp_type),
		   llu(handle));
	}
    }
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
static void handlelist_initialize(int *handle_counts, int server_count)
{
    int i;

    handlelist_server_count = server_count;
    handlelist_list = malloc(server_count * sizeof(PVFS_handle *));
    handlelist_size = malloc(server_count * sizeof(int));
    handlelist_used = malloc(server_count * sizeof(int));

    for (i = 0; i < server_count; i++) {
	handlelist_list[i] = malloc(handle_counts[i] * sizeof(PVFS_handle));
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
				   int handle_count,
				   int server_idx)
{
    int i, start_off;

    start_off = handlelist_used[server_idx];

    if ((handlelist_size[server_idx] - start_off) < handle_count)
    {
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
	printf("# %d handles for server %d\n", handlelist_size[i], i);
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
	int j;

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
    int i;

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
    /* getopt stuff */
    extern char* optarg;
    extern int optind, opterr, optopt;
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

