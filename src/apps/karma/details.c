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

char *column_name[] = { "Server Name",
			"Total RAM",
			"Available RAM",
			"Uptime",
			"Total Handles",
			"Available Handles",
			"Total Space",
			"Available Space",
			"Type" };

static int gui_details_initialized = 0;
static GtkListStore *gui_details_list;
static GtkWidget *gui_details_view;

GtkWidget *gui_details_setup(void)
{
    int i;

    gui_details_list = gtk_list_store_new(9,
					  G_TYPE_STRING,  /* name */
					  G_TYPE_FLOAT,   /* ram total */
					  G_TYPE_FLOAT,   /* ram avail */
					  G_TYPE_FLOAT,   /* uptime */
					  G_TYPE_FLOAT,   /* handles total */
					  G_TYPE_FLOAT,   /* handles avail */
					  G_TYPE_FLOAT,   /* space total */
					  G_TYPE_FLOAT,   /* space avail */
					  G_TYPE_STRING); /* server type */
    

    gui_details_view = gtk_tree_view_new();

    for (i=0; i < GUI_DETAILS_TYPE; i++) {
	GtkTreeViewColumn *col;
	GtkCellRenderer *renderer;
	
	col = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(col, column_name[i]);
	gtk_tree_view_append_column(GTK_TREE_VIEW(gui_details_view),
				    col);

	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_add_attribute(col,
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
    int i;
    GtkTreeModel *model;

    if (gui_details_initialized == 0) return;

    /* detach model from view until we're done updating */
    model = gtk_tree_view_get_model(GTK_TREE_VIEW(gui_details_view));
    g_object_ref(model);
    gtk_tree_view_set_model(GTK_TREE_VIEW(gui_details_view), NULL);

    /* just clear out the list and then refill it */
    gtk_list_store_clear(gui_details_list);

    for (i=0; i < server_stat_ct; i++) {
	GtkTreeIter iter;
	char *type, meta[] = "meta", data[] = "data", both[] = "both";

	gtk_list_store_append(gui_details_list, &iter);

	type = both;

	gtk_list_store_set(gui_details_list,
			   &iter,
			   GUI_DETAILS_NAME, "xxx",
			   GUI_DETAILS_RAM_TOT,
			   server_stat[i].ram_total_bytes,
			   GUI_DETAILS_RAM_AVAIL,
			   server_stat[i].ram_free_bytes,
			   GUI_DETAILS_UPTIME,
			   server_stat[i].uptime_seconds,
			   GUI_DETAILS_HANDLES_TOT,
			   server_stat[i].handles_total_count,
			   GUI_DETAILS_HANDLES_AVAIL,
			   server_stat[i].handles_available_count,
			   GUI_DETAILS_SPACE_TOT,
			   server_stat[i].bytes_total,
			   GUI_DETAILS_SPACE_AVAIL,
			   server_stat[i].bytes_available,
			   GUI_DETAILS_TYPE, type,
			   -1);
    }

    /* reattach model to view */
    gtk_tree_view_set_model(GTK_TREE_VIEW(gui_details_view), model);
    g_object_unref(model);
}
