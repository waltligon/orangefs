#include <sys/types.h>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <time.h> /* nanosleep() */

#include "karma.h"

#define GUI_COMM_PERF_HISTORY 5
#undef FAKE_STATS
#undef FAKE_PERF

/* statistics data structures */
static pvfs_mntlist mnt = {0, NULL};
static struct PVFS_mgmt_server_stat *visible_stats = NULL;
static struct PVFS_mgmt_server_stat *internal_stats = NULL;
static int *internal_errors = NULL;
static int visible_stat_ct;
static int internal_stat_ct;

static PVFS_id_gen_t *internal_addrs = NULL;
static int internal_addr_ct;

/* performance data structures */
static struct PVFS_mgmt_perf_stat **internal_perf;
static uint32_t *internal_perf_ids;
static uint64_t *internal_end_time_ms;
static struct gui_traffic_raw_data *visible_perf = NULL;

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

#ifdef FAKE_PERF
static struct gui_traffic_raw_data fake_perf[] = {
    { 100*1048576, 0, 100, 10, 1020 },
    { 120*1048576, 0, 100, 10, 1020 },
    { 80*1048576, 0, 100, 10, 1020 },
    { 10, 100*1048576, 0, 100, 1020 },
    { 100, 130*1048576, 10, 1023, 1020 },
    { 10*1048576, 0, 637, 20, 1020 },
    { 140*1048576, 0, 1300, 20, 1020 },
    { 1*1048576, 0, 100, 123, 1020 },
    { 10, 64*1048576, 0, 2100, 1020 },
    { 100, 137*1048576, 10, 523, 1020 },
    { 100*1048576, 0, 100, 10, 1020 },
    { 120*1048576, 0, 100, 10, 1020 },
    { 80*1048576, 0, 100, 10, 1020 },
    { 10, 100*1048576, 0, 100, 1020 },
    { 30*1048576, 40*1048576, 2110, 4023, 1020 },
    { 60*1048576, 130*1048576, 1230, 723, 1020 }
};
static int fake_perf_ct = sizeof(fake_perf) / sizeof(*fake_perf);
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
    int i, ret, outcount;
    char msgbuf[80];
    struct PVFS_mgmt_perf_stat *bigperfbuf;

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
	/* free all our dynamically allocated memory for resizing */
	free(internal_stats);
	free(internal_errors);
	free(internal_perf[0]);
	free(internal_perf);
	free(internal_perf_ids);
	free(internal_end_time_ms);
    }

    internal_stats   = (struct PVFS_mgmt_server_stat *)
	malloc(outcount * sizeof(struct PVFS_mgmt_server_stat));
    internal_errors  = (int *) malloc(outcount * sizeof(int));
    internal_stat_ct = outcount;

    /* save addresses of servers */
    if (internal_addrs != NULL) {
	free(internal_addrs);
    }
    internal_addrs = (PVFS_id_gen_t *)
	malloc(outcount * sizeof(PVFS_id_gen_t));
    internal_addr_ct = outcount;
    ret = PVFS_mgmt_get_server_array(cur_fsid,
				     creds,
				     PVFS_MGMT_IO_SERVER|PVFS_MGMT_META_SERVER,
				     internal_addrs,
				     &outcount);
    if (ret < 0)
    {
	PVFS_perror("PVFS_mgmt_get_server_array", ret);
	return;
    }

    /* allocate space for performance data */
    bigperfbuf = (struct PVFS_mgmt_perf_stat *)
	malloc(GUI_COMM_PERF_HISTORY * outcount *
	       sizeof(struct PVFS_mgmt_perf_stat));
    internal_perf = (struct PVFS_mgmt_perf_stat **)
	malloc(outcount * sizeof(struct PVFS_mgmt_perf_stat *));
    for (i=0; i < outcount; i++) {
	internal_perf[i] = &bigperfbuf[i * GUI_COMM_PERF_HISTORY];
    }
    internal_perf_ids = (uint32_t *) malloc(outcount * sizeof(uint32_t));
    memset(internal_perf_ids, 0, outcount * sizeof(uint32_t));
    internal_end_time_ms = (uint64_t *) malloc(outcount * sizeof(uint64_t));
    
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
 */
static int gui_comm_stats_collect(void)
{
    int ret;

#ifdef FAKE_STATS
    return 0;
#else
    assert(internal_addr_ct == internal_stat_ct);

    ret = PVFS_mgmt_statfs_list(cur_fsid,
				creds,
				internal_stats,
				internal_addrs,
				internal_errors,
				internal_stat_ct);
    if (ret < 0)
    {
	PVFS_perror("PVFS_mgmt_statfs_list", ret);
	return -1;
    }
#endif
    return 0;
}

/* gui_comm_perf_collect()
 */
static int gui_comm_perf_collect(void)
{
    int ret;

#ifndef FAKE_PERF
    ret = PVFS_mgmt_perf_mon_list(cur_fsid,
				  creds,
				  internal_perf,
				  internal_end_time_ms,
				  internal_addrs,
				  internal_perf_ids,
				  internal_addr_ct,
				  GUI_COMM_PERF_HISTORY);
    if (ret != 0) {
	PVFS_perror("PVFS_mgmt_perf_mon_list", ret);
	return -1;
    }
#endif

    return 0;
}

/* gui_comm_traffic_retrieve()
 *
 * Passes back pointer to "raw" traffic data, fills in # of servers.
 */
int gui_comm_traffic_retrieve(struct gui_traffic_raw_data **svr_traffic,
			      int *svr_traffic_ct)
{
    int ret, svr, idx;

#ifdef FAKE_PERF
    *svr_traffic = fake_perf;
    *svr_traffic_ct = fake_perf_ct;
    return 0;
#else
    ret = gui_comm_perf_collect();
    if (ret != 0) return ret;

    /* initialize visible_perf array if we haven't already */
    if (visible_perf == NULL) {
	visible_perf = (struct gui_traffic_raw_data *)
	    malloc(internal_addr_ct * sizeof(struct gui_traffic_raw_data));
	assert(visible_perf != NULL);
    }
    memset(visible_perf,
	   0,
	   internal_addr_ct * sizeof(struct gui_traffic_raw_data));

    /* summarize data and store in visible_perf array */
    for (svr=0; svr < internal_addr_ct; svr++) {
	int valid_start_time = 0;
	uint64_t start_time_ms;

	struct gui_traffic_raw_data *raw = &visible_perf[svr];

	for (idx=0; idx < GUI_COMM_PERF_HISTORY; idx++) {
	    if (!internal_perf[svr][idx].valid_flag) continue;
	    
	    if (!valid_start_time) {
		/* Q: should we just assume first entry is good? */
		valid_start_time = 1;
		start_time_ms = internal_perf[svr][idx].start_time_ms;
	    }

	    raw->data_write_bytes += internal_perf[svr][idx].write;
	    raw->data_read_bytes  += internal_perf[svr][idx].read;
	    raw->meta_write_ops    = internal_perf[svr][idx].metadata_write;
	    raw->meta_read_ops     = internal_perf[svr][idx].metadata_read;
	}

	raw->elapsed_time_ms  = internal_end_time_ms[svr] - start_time_ms;

	/* deal with format in which metadata is returned */
	if (internal_perf[svr][0].valid_flag) {
	    raw->meta_write_ops -= internal_perf[svr][0].metadata_write;
	    raw->meta_read_ops  -= internal_perf[svr][0].metadata_read;

	    /* simple, if somewhat inaccurate, handling of overflow */
	    if (raw->meta_write_ops < 0) raw->meta_write_ops = 0;
	    if (raw->meta_read_ops < 0) raw->meta_read_ops = 0;
	}
    }

    *svr_traffic    = visible_perf;
    *svr_traffic_ct = internal_addr_ct;
    return 0;
#endif
}
