#include <sys/types.h>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "karma.h"

static pvfs_mntlist mnt = {0, NULL};
static struct PVFS_mgmt_server_stat *visible_stats = NULL;
static struct PVFS_mgmt_server_stat *internal_stats = NULL;
static int visible_stat_ct;
static int internal_stat_ct;

static PVFS_credentials creds;
static PVFS_fs_id cur_fs;

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
    char msgbuf[64];
    int ret, outcount;
    PVFS_sysresp_init resp_init;

    /* PVFS2 init */
    if (PVFS_util_parse_pvfstab(&mnt))
    {
	return -1;
    }

    ret = PVFS_sys_initialize(mnt, 0, &resp_init);
    if (ret < 0) {
	return -1;
    }

    cur_fs = resp_init.fsid_list[0];

    creds.uid = getuid();
    creds.gid = getgid();

    ret = PVFS_mgmt_count_servers(cur_fs,
				  creds,
				  PVFS_MGMT_IO_SERVER | PVFS_MGMT_META_SERVER,
				  &outcount);
    if (ret < 0) {
	return -1;
    }

    assert(outcount > 0);

    /* print a quick status message at startup */
    if (outcount > 1) {
	snprintf(msgbuf, 64, "%d servers in file system.\n", outcount);
    }
    else {
	snprintf(msgbuf, 64, "1 server in file system.\n");
    }
    gui_message_new(msgbuf);

    /* allocate space for our stats */
    visible_stats  = (struct PVFS_mgmt_server_stat *)
	malloc(outcount * sizeof(struct PVFS_mgmt_server_stat));
    internal_stats = (struct PVFS_mgmt_server_stat *)
	malloc(outcount * sizeof(struct PVFS_mgmt_server_stat));
    visible_stat_ct  = outcount;
    internal_stat_ct = outcount;

    return 0;
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
    ret = PVFS_mgmt_get_server_array(cur_fs,
				     creds,
				     PVFS_MGMT_IO_SERVER | PVFS_MGMT_META_SERVER,
				     addr_array,
				     &outcount);
    if (ret < 0)
    {
	PVFS_perror("PVFS_mgmt_get_server_array", ret);
	return -1;
    }
    
    ret = PVFS_mgmt_statfs_list(cur_fs,
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
