#include <gtk/gtk.h>

#include "pvfs2.h"
#include "pvfs2-mgmt.h"
#include "pint-sysint-utils.h"
#include "server-config.h"

/* reference to the main window, for popups etc. */
extern GtkWidget *main_window;

/* setting window title (from karma.c) */
void gui_set_title(char *title);

/* menu interface */
GtkWidget *gui_menu_setup(GtkWidget  *window);

/* message window interface */
GtkWidget *gui_message_setup(void);
void       gui_message_new(char *message);

/* color grabbing interface */
GdkGC *gui_get_new_fg_color_gc(GtkWidget *drawing_area,
			       gint red,
			       gint green,
			       gint blue);


/* traffic page interface (traffic.c) */
struct gui_traffic_raw_data {
    int64_t data_write_bytes;
    int64_t data_read_bytes;
    int64_t meta_write_ops;
    int64_t meta_read_ops;
    int64_t elapsed_time_ms;
};

struct gui_traffic_server_data {
    float data_write;
    float data_read;
    float meta_write;
    float meta_read;
};

struct gui_traffic_graph_data {
    char io_label[64];
    char meta_label[64];
    int svr_ct;
    struct gui_traffic_server_data *svr_data;
};

GtkWidget *gui_traffic_setup(void);
void gui_traffic_graph_update(struct gui_traffic_graph_data *data);

/* data preparation interface */
struct gui_status_graph_data {
    char title[64];
    int count;
    int has_second_val;
    float *first_val;
    float *second_val;
    int *bar_color;
    char footer[64];
};


void gui_status_data_prepare(struct PVFS_mgmt_server_stat *svr_stat,
			     int svr_stat_ct,
			     struct gui_status_graph_data **out_graph_data);
void gui_traffic_data_prepare(struct gui_traffic_raw_data *raw,
			      int svr_ct,
			      struct gui_traffic_graph_data *graph);

/* communication interface */
int gui_comm_setup(void);
void gui_comm_set_active_fs(char *contact_server,
			    char *fs_name,
			    PVFS_fs_id new_fsid);
int gui_comm_stats_retrieve(struct PVFS_mgmt_server_stat **svr_stat,
			    int *svr_stat_ct);
int gui_comm_traffic_retrieve(struct gui_traffic_raw_data **svr_traffic,
			      int *svr_traffic_ct);

/* communication interface builds list of file systems as well */
enum {
    GUI_FSLIST_MNTPT = 0,
    GUI_FSLIST_SERVER,
    GUI_FSLIST_FSNAME,
    GUI_FSLIST_FSID
};
extern GtkListStore *gui_comm_fslist;

/* details page interface (details.c) */
GtkWidget *gui_details_setup(void);
void gui_details_update(struct PVFS_mgmt_server_stat *s_stat,
			int s_stat_ct);

/* details view functions (details.c) - more generic */
void gui_details_view_fill(GtkWidget *view,
			   GtkListStore *list,
			   GtkTreeViewColumn *col[],
			   struct PVFS_mgmt_server_stat *server_stat,
			   int server_stat_ct,
			   int *server_list);
GtkWidget *gui_details_view_new(GtkListStore **list_p,
				GtkTreeViewColumn **col,
				gint sortable);

/* status page interface (status.c) */
GtkWidget *gui_status_setup(void);

enum {
    BAR_RED    = 1,
    BAR_YELLOW = 2,
    BAR_GREEN  = 3
};

/* graph identifiers to use with gui_status_graph_update */
enum {
    GUI_STATUS_SPACE  = 0,
    GUI_STATUS_UPTIME = 1,
    GUI_STATUS_META   = 2,
    GUI_STATUS_DATA   = 3,
    GUI_STATUS_MEMORY = 4,
    GUI_STATUS_CPU    = 5
};

void gui_status_graph_update(int graph_id,
			     struct gui_status_graph_data *graph_data);

/* file system view/selection interface (fsview.c) */
void gui_fsview_popup(void);

/* unit conversion functions (units.c) */
char *gui_units_time(uint64_t time_sec, float *divisor);
char *gui_units_size(PVFS_size size_bytes, float *divisor);
char *gui_units_count(uint64_t count, float *divisor);
char *gui_units_ops(PVFS_size ops, float *divisor);

