#include <stdio.h>
#include <assert.h>
#include <gtk/gtk.h>

#include "karma.h"

enum {
    GUI_DETAILS_NAME = 0,
    GUI_DETAILS_RAM_TOT,
    GUI_DETAILS_RAM_AVAIL,
    GUI_DETAILS_UPTIME,
    GUI_DETAILS_HANDLES_TOT,
    GUI_DETAILS_HANDLES_AVAIL,
    GUI_DETAILS_SPACE_TOT,
    GUI_DETAILS_SPACE_AVAIL,
    GUI_DETAILS_TYPE
};

char *column_name[] = { "Server Address (BMI)",
			"Total RAM",
			"Available RAM",
			"Uptime",
			"Total Handles",
			"Available Handles",
			"Total Space",
			"Available Space",
			"Type of Service" };

static int gui_details_initialized = 0;
static GtkListStore *gui_details_list;
static GtkWidget *gui_details_view;

static GtkTreeViewColumn *gui_details_col[GUI_DETAILS_TYPE];

GtkWidget *gui_details_setup(void)
{
    int i;

    gui_details_list = gtk_list_store_new(9,
					  G_TYPE_STRING,  /* name */
					  G_TYPE_STRING,  /* ram total */
					  G_TYPE_STRING,  /* ram avail */
					  G_TYPE_STRING,  /* uptime */
					  G_TYPE_STRING,  /* handles total */
					  G_TYPE_STRING,  /* handles avail */
					  G_TYPE_STRING,  /* space total */
					  G_TYPE_STRING,  /* space avail */
					  G_TYPE_STRING); /* server type */
   

    gui_details_view = gtk_tree_view_new();

    for (i=0; i < GUI_DETAILS_TYPE + 1; i++) {
	GtkCellRenderer *renderer;
	
	gui_details_col[i] = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(gui_details_col[i], column_name[i]);
	gtk_tree_view_append_column(GTK_TREE_VIEW(gui_details_view),
				    gui_details_col[i]);

	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(gui_details_col[i], renderer, TRUE);
	gtk_tree_view_column_add_attribute(gui_details_col[i],
					   renderer,
					   "text",
					   i);
    }

    gtk_tree_view_set_model(GTK_TREE_VIEW(gui_details_view),
			    GTK_TREE_MODEL(gui_details_list));

    gui_details_initialized = 1;

    return gui_details_view;
}

void gui_details_update(struct PVFS_mgmt_server_stat *server_stat,
			int server_stat_ct)
{
    int i, max;
    GtkTreeModel *model;
    char titlebuf[64];

    /* divisors and units for all our data columns */
    float rt_div, ra_div, up_div, ht_div, ha_div, bt_div, ba_div;
    char *rt_units, *ra_units, *up_units, *ht_units, *ha_units, *bt_units,
	*ba_units;

    if (gui_details_initialized == 0) return;

    /* calculate units and divisors for everything (find max; call unit fn) */
    for (max=0, i=1; i < server_stat_ct; i++) {
	if (server_stat[i].ram_total_bytes > server_stat[max].ram_total_bytes) max = i;
    }
    rt_units = gui_units_size(server_stat[max].ram_total_bytes, &rt_div);

    for (max=0, i=1; i < server_stat_ct; i++) {
	if (server_stat[i].ram_free_bytes > server_stat[max].ram_free_bytes) max = i;
    }
    ra_units = gui_units_size(server_stat[max].ram_free_bytes, &ra_div);

    for (max=0, i=1; i < server_stat_ct; i++) {
	if (server_stat[i].uptime_seconds > server_stat[max].uptime_seconds) max = i;
    }
    up_units = gui_units_time(server_stat[max].uptime_seconds, &up_div);

    for (max=0, i=1; i < server_stat_ct; i++) {
	if (server_stat[i].handles_total_count > server_stat[max].handles_total_count) max = i;
    }
    ht_units = gui_units_count(server_stat[max].handles_total_count, &ht_div);

    for (max=0, i=1; i < server_stat_ct; i++) {
	if (server_stat[i].handles_available_count > server_stat[max].handles_available_count) max = i;
    }
    ha_units = gui_units_count(server_stat[max].handles_available_count, &ha_div);

    for (max=0, i=1; i < server_stat_ct; i++) {
	if (server_stat[i].bytes_total > server_stat[max].bytes_total) max = i;
    }
    bt_units = gui_units_size(server_stat[max].bytes_total, &bt_div);

    for (max=0, i=1; i < server_stat_ct; i++) {
	if (server_stat[i].bytes_available > server_stat[max].bytes_available) max = i;
    }
    ba_units = gui_units_size(server_stat[max].bytes_available, &ba_div);

    /* update column titles */
    snprintf(titlebuf, sizeof(titlebuf), "%s (%s)",
	     column_name[GUI_DETAILS_RAM_TOT], rt_units);
    gtk_tree_view_column_set_title(gui_details_col[GUI_DETAILS_RAM_TOT],
				   titlebuf);

    snprintf(titlebuf, sizeof(titlebuf), "%s (%s)",
	     column_name[GUI_DETAILS_RAM_AVAIL], ra_units);
    gtk_tree_view_column_set_title(gui_details_col[GUI_DETAILS_RAM_AVAIL],
				   titlebuf);

    snprintf(titlebuf, sizeof(titlebuf), "%s (%s)",
	     column_name[GUI_DETAILS_UPTIME], up_units);
    gtk_tree_view_column_set_title(gui_details_col[GUI_DETAILS_UPTIME],
				   titlebuf);

    snprintf(titlebuf, sizeof(titlebuf), "%s (%s)",
	     column_name[GUI_DETAILS_HANDLES_TOT], ht_units);
    gtk_tree_view_column_set_title(gui_details_col[GUI_DETAILS_HANDLES_TOT],
				   titlebuf);

    snprintf(titlebuf, sizeof(titlebuf), "%s (%s)",
	     column_name[GUI_DETAILS_HANDLES_AVAIL], ha_units);
    gtk_tree_view_column_set_title(gui_details_col[GUI_DETAILS_HANDLES_AVAIL],
				   titlebuf);

    snprintf(titlebuf, sizeof(titlebuf), "%s (%s)",
	     column_name[GUI_DETAILS_SPACE_TOT], bt_units);
    gtk_tree_view_column_set_title(gui_details_col[GUI_DETAILS_SPACE_TOT],
				   titlebuf);

    snprintf(titlebuf, sizeof(titlebuf), "%s (%s)",
	     column_name[GUI_DETAILS_SPACE_AVAIL], ba_units);
    gtk_tree_view_column_set_title(gui_details_col[GUI_DETAILS_SPACE_AVAIL],
				   titlebuf);

    /* update table of data */

    /* detach model from view until we're done updating */
    model = gtk_tree_view_get_model(GTK_TREE_VIEW(gui_details_view));
    g_object_ref(model);
    gtk_tree_view_set_model(GTK_TREE_VIEW(gui_details_view), NULL);

    /* just clear out the list and then refill it */
    gtk_list_store_clear(gui_details_list);

    for (i=0; i < server_stat_ct; i++) {
	GtkTreeIter iter;
	char *type, meta[] = "meta", data[] = "data", both[] = "both";
	char fmtbuf[GUI_DETAILS_TYPE][12];

	snprintf(fmtbuf[GUI_DETAILS_RAM_TOT], 12, "%.2f",
		 (float) server_stat[i].ram_total_bytes / rt_div);
	snprintf(fmtbuf[GUI_DETAILS_RAM_AVAIL], 12, "%.2f",
		 (float) server_stat[i].ram_free_bytes / ra_div);
	snprintf(fmtbuf[GUI_DETAILS_UPTIME], 12, "%.2f",
		 (float) server_stat[i].uptime_seconds / up_div);
	snprintf(fmtbuf[GUI_DETAILS_HANDLES_TOT], 12, "%.2f",
		 (float) server_stat[i].handles_total_count / ht_div);
	snprintf(fmtbuf[GUI_DETAILS_HANDLES_AVAIL], 12, "%.2f",
		 (float) server_stat[i].handles_available_count / ha_div);
	snprintf(fmtbuf[GUI_DETAILS_SPACE_TOT], 12, "%.2f",
		 (float) server_stat[i].bytes_total / bt_div);
	snprintf(fmtbuf[GUI_DETAILS_SPACE_AVAIL], 12, "%.2f",
		 (float) server_stat[i].bytes_available / ba_div);


	/* final formatting of data */
	if ((server_stat[i].server_type & PVFS_MGMT_IO_SERVER) &&
	    (server_stat[i].server_type & PVFS_MGMT_META_SERVER))
	{
	    type = both;
	}
	else if (server_stat[i].server_type & PVFS_MGMT_IO_SERVER)
	{
	    type = data;
	}
	else
	{
	    type = meta;
	}

	/* drop into list */
	gtk_list_store_append(gui_details_list, &iter);

	gtk_list_store_set(gui_details_list,
			   &iter,
			   GUI_DETAILS_NAME,
			   server_stat[i].bmi_address,
			   GUI_DETAILS_RAM_TOT,
			   fmtbuf[GUI_DETAILS_RAM_TOT],
			   GUI_DETAILS_RAM_AVAIL,
			   fmtbuf[GUI_DETAILS_RAM_AVAIL],
			   GUI_DETAILS_UPTIME,
			   fmtbuf[GUI_DETAILS_UPTIME],
			   GUI_DETAILS_HANDLES_TOT,
			   fmtbuf[GUI_DETAILS_HANDLES_TOT],
			   GUI_DETAILS_HANDLES_AVAIL,
			   fmtbuf[GUI_DETAILS_HANDLES_AVAIL],
			   GUI_DETAILS_SPACE_TOT,
			   fmtbuf[GUI_DETAILS_SPACE_TOT],
			   GUI_DETAILS_SPACE_AVAIL,
			   fmtbuf[GUI_DETAILS_SPACE_AVAIL],
			   GUI_DETAILS_TYPE, type,
			   -1);
    }

    /* reattach model to view */
    gtk_tree_view_set_model(GTK_TREE_VIEW(gui_details_view), model);
    g_object_unref(model);
}
