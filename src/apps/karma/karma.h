#include <gtk/gtk.h>

#include "pvfs2.h"
#include "pvfs2-mgmt.h"

/* menu interface */
GtkWidget *gui_menu_setup(GtkWidget  *window);

/* message window interface */
GtkWidget *gui_message_setup(void);
void       gui_message_new(char *message);

/* communication interface */
int gui_comm_setup(void);
int gui_comm_stats_retrieve(struct PVFS_mgmt_server_stat **svr_stat,
			    int *svr_stat_ct);

/* data preparation interface */
struct gui_graph_data {
    char title[64];
    int count;
    int has_second_val;
    float *first_val;
    float *second_val;
    int *bar_color;
    char footer[64];
};

void gui_data_prepare(struct PVFS_mgmt_server_stat *svr_stat,
		      int svr_stat_ct,
		      struct gui_graph_data **out_graph_data);

/* status page interface */
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
			     struct gui_graph_data *graph_data);
