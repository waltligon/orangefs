#include <sys/types.h>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>

#include "karma.h"

#undef FAKE_STATS

static pvfs_mntlist mnt = {0, NULL};
static struct PVFS_mgmt_server_stat *visible_stats = NULL;
static struct PVFS_mgmt_server_stat *internal_stats = NULL;
static int visible_stat_ct;
static int internal_stat_ct;

GtkListStore *gui_comm_fslist;

static PVFS_credentials creds;
static PVFS_fs_id cur_fsid = -1;

#ifdef FAKE_STATS
static struct PVFS_mgmt_server_stat fake_stats[] = {
    { 9, 1048576, 2*1048576, 2048, 1024, 102, 4096, 8192, "node0", 3 },
    { 9, 524288, 2*1048576, 1024, 512, 500, 256, 8192, "node2", 3 },
    { 9, 524288, 2*1048576, 2048, 512, 102, 512, 1024, "node1", 1 },
    { 9, 1048576, 2*1048576, 1024, 256, 302, 4096, 8192, "node3", 2 },
    { 9, 1048576, 2*1048576, 1024, 512, 102, 1024, 8192, "node4", 1 }
};
static int fake_stat_ct = sizeof(fake_stats) / sizeof(*fake_stats);
#endif

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
        PVFS_fs_id cur_fs_id;

	gtk_list_store_append(gui_comm_fslist, &iter);

	for (j=strlen(mnt.ptab_array[i].pvfs_config_server); j > 0; j--)
	{
	    if (mnt.ptab_array[i].pvfs_config_server[j] == '/') break;
	}

	assert(j < 128);
	strncpy(msgbuf, mnt.ptab_array[i].pvfs_config_server, j);
	msgbuf[j] = '\0';

        cur_fs_id = PINT_config_get_fs_id_by_fs_name(
            PINT_get_server_config_struct(),
            mnt.ptab_array[i].pvfs_fs_name);
        assert(cur_fs_id != (PVFS_fs_id) 0);

	gtk_list_store_set(gui_comm_fslist,
			   &iter,
			   GUI_FSLIST_MNTPT, mnt.ptab_array[i].mnt_dir,
			   GUI_FSLIST_SERVER, msgbuf,
			   GUI_FSLIST_FSNAME, mnt.ptab_array[i].pvfs_fs_name,
			   GUI_FSLIST_FSID, (gint) cur_fs_id,
			   -1);
    }

    creds.uid = getuid();
    creds.gid = getgid();

    /* print message indicating what file system we are monitoring */
    snprintf(msgbuf,
	     128,
	     "monitoring %s by default.",
	     mnt.ptab_array[0].pvfs_config_server);
    gui_message_new(msgbuf);

    /* prepare config server name for passing to
     * gui_comm_set_active_fs() - this is the only time that it isn't
     * quite in the right format.
     */
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

#ifdef FAKE_STATS
    return;
#else
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
#endif
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

#ifdef FAKE_STATS
    *svr_stat = fake_stats;
    *svr_stat_ct = fake_stat_ct;
    return 0;
#else
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
#endif
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

#ifdef FAKE_STATS
    return 0;
#else
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
#endif
    return 0;
}
