#include <sys/types.h>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>

#include "karma.h"

static pvfs_mntlist mnt = {0, NULL};
static struct PVFS_mgmt_server_stat *visible_stats = NULL;
static struct PVFS_mgmt_server_stat *internal_stats = NULL;
static int visible_stat_ct;
static int internal_stat_ct;

GtkListStore *gui_comm_fslist;

static PVFS_credentials creds;
static PVFS_fs_id cur_fsid = -1;

/* internal fn prototypes */
static int gui_comm_stats_collect(void);

/* gui_comm_setup()
 *
 * Initializes PVFS2 library and allocates memory for lists of stats from servers.
 *
 * Returns 0 on success, -1 on failure (some error initializing).
 */
int gui_comm_setup(void)
{
    char msgbuf[128];
    int ret, i, j;
    PVFS_sysresp_init resp_init;


    /* PVFS2 init */
    if (PVFS_util_parse_pvfstab(&mnt))
    {
	return -1;
    }

    /* create the fslist that we're going to use all over the place */
    gui_comm_fslist = gtk_list_store_new(4,
					 G_TYPE_STRING,
					 G_TYPE_STRING,
					 G_TYPE_STRING,
					 G_TYPE_INT);

    ret = PVFS_sys_initialize(mnt, 0, &resp_init);
    if (ret < 0) {
	return -1;
    }

    for (i=0; i < mnt.ptab_count; i++) {
	GtkTreeIter iter;

	gtk_list_store_append(gui_comm_fslist, &iter);

	for (j=strlen(mnt.ptab_array[i].pvfs_config_server); j > 0; j--)
	{
	    if (mnt.ptab_array[i].pvfs_config_server[j] == '/') break;
	}

	assert(j < 128);
	strncpy(msgbuf, mnt.ptab_array[i].pvfs_config_server, j);
	msgbuf[j] = '\0';

	gtk_list_store_set(gui_comm_fslist,
			   &iter,
			   0, mnt.ptab_array[i].mnt_dir,
			   1, msgbuf,
			   2, mnt.ptab_array[i].pvfs_fs_name,
			   3, (gint) resp_init.fsid_list[i],
			   -1);
    }

    creds.uid = getuid();
    creds.gid = getgid();

    for (j=strlen(mnt.ptab_array[0].pvfs_config_server); j > 0; j--)
    {
	if (mnt.ptab_array[0].pvfs_config_server[j] == '/') break;
    }
    
    assert(j < 128);
    strncpy(msgbuf, mnt.ptab_array[0].pvfs_config_server, j);
    msgbuf[j] = '\0';

    gui_comm_set_active_fs(msgbuf,
			   mnt.ptab_array[0].pvfs_fs_name,
			   resp_init.fsid_list[0]);

    return 0;
}

/* gui_comm_set_active_fsid(contact_server, fsname, fsid)
 */
void gui_comm_set_active_fs(char *contact_server,
			    char *fs_name,
			    PVFS_fs_id new_fsid)
{
    int ret, outcount;
    char msgbuf[80];

    snprintf(msgbuf, 80, "Karma: %s/%s", contact_server, fs_name);
    gui_set_title(msgbuf);

    if (new_fsid == cur_fsid) return;

    cur_fsid = new_fsid;

    ret = PVFS_mgmt_count_servers(cur_fsid,
				  creds,
				  PVFS_MGMT_IO_SERVER | PVFS_MGMT_META_SERVER,
				  &outcount);
    if (ret < 0) {
	return;
    }

    assert(outcount > 0);
    
    /* allocate space for our stats if we need to */
    if (internal_stats != NULL && internal_stat_ct == outcount) return;
    else if (internal_stats != NULL) {
	free(internal_stats);
    }

    internal_stats   = (struct PVFS_mgmt_server_stat *)
	malloc(outcount * sizeof(struct PVFS_mgmt_server_stat));
    internal_stat_ct = outcount;

}

/* gui_comm_stats_retrieve(**svr_stat, *svr_stat_ct)
 *
 * Copies latest internal data into visible structures and returns pointers to
 * visible structures storing server statistics.  These should not be modified
 * by the caller, but are guaranteed not to be modified between calls to this
 * function.
 *
 * Returns 0 on success, -1 on failure (some error grabbing data).
 */
int gui_comm_stats_retrieve(struct PVFS_mgmt_server_stat **svr_stat,
			     int *svr_stat_ct)
{
    int ret;

    /* for now, call gui_comm_stats_collect() to get new data */
    ret = gui_comm_stats_collect();
    if (ret != 0) return ret;

    if (visible_stats == NULL) {
	visible_stats   = (struct PVFS_mgmt_server_stat *)
	    malloc(internal_stat_ct * sizeof(struct PVFS_mgmt_server_stat));
	visible_stat_ct = internal_stat_ct;
    }

    memcpy(visible_stats,
	   internal_stats,
	   visible_stat_ct * sizeof(struct PVFS_mgmt_server_stat));

    *svr_stat    = visible_stats;
    *svr_stat_ct = visible_stat_ct;

    return 0;
}

/* gui_comm_stats_collect()
 *
 * Updates internal stat structures.
 *
 * Assumes number of servers does not change over time.
 */
static int gui_comm_stats_collect(void)
{
    int ret, outcount;
    PVFS_id_gen_t *addr_array;

    addr_array = (PVFS_id_gen_t*) malloc(internal_stat_ct * sizeof(PVFS_id_gen_t));
    if (!addr_array)
    {
	perror("malloc");
	return -1;
    }

    outcount = internal_stat_ct;
    ret = PVFS_mgmt_get_server_array(cur_fsid,
				     creds,
				     PVFS_MGMT_IO_SERVER | PVFS_MGMT_META_SERVER,
				     addr_array,
				     &outcount);
    if (ret < 0)
    {
	PVFS_perror("PVFS_mgmt_get_server_array", ret);
	return -1;
    }
    
    ret = PVFS_mgmt_statfs_list(cur_fsid,
				creds,
				internal_stats,
				addr_array,
				internal_stat_ct);
    if (ret < 0)
    {
	PVFS_perror("PVFS_mgmt_statfs_list", ret);
	return -1;
    }

    free(addr_array);

    return 0;
}
