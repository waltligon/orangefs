#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <gtk/gtk.h>

#include "karma.h"

#define GUI_DETAILS_ENABLE_SORTING

static int gui_details_initialized    = 0;
static GtkListStore *gui_details_list = NULL;
static GtkWidget *gui_details_view    = NULL;

static GtkTreeViewColumn *gui_details_col[GUI_DETAILS_TYPE + 1];

/* THESE MUST MATCH WITH ENUM IN KARMA.H! */
static char *gui_details_column_name[] = { "Server Address (BMI)",
					   "Total RAM",
					   "Available RAM",
					   "Uptime",
					   "Total Handles",
					   "Available Handles",
					   "Total Space",
					   "Available Space",
					   "Type of Service" };

#ifdef GUI_DETAILS_ENABLE_SORTING
static gint gui_details_float_string_compare(GtkTreeModel *model,
					     GtkTreeIter *iter_a,
					     GtkTreeIter *iter_b,
					     gpointer col_id);

static gint gui_details_text_compare(GtkTreeModel *model,
				     GtkTreeIter *iter_a,
				     GtkTreeIter *iter_b,
				     gpointer col_id);
#endif

/* internal helper fn prototypes */
static void gui_details_view_insert(GtkListStore *list,
				    struct PVFS_mgmt_server_stat *s_stat,
				    float rt_div,
				    float ra_div,
				    float up_div,
				    float ht_div,
				    float ha_div,
				    float bt_div,
				    float ba_div);

/* gui_details_setup()
 */
GtkWidget *gui_details_setup(void)
{
    gui_details_view = gui_details_view_new(&gui_details_list,
					    gui_details_col,
#ifdef GUI_DETAILS_ENABLE_SORTING
					    1
#else
                                            0
#endif
					    );

    return gui_details_view;
}


/* gui_details_view_new()
 *
 * Parameters:
 * - list_p is a pointer to location to store reference to list store
 * - col is a pointer to array of tree view columns of size GUI_DETAILS_TYPE+1
 * - sortable is a boolean indicating if we should enable sorting of cols
 */
GtkWidget *gui_details_view_new(GtkListStore **list_p,
				GtkTreeViewColumn **col,
				gint sortable)
{
    int i;
    GtkListStore *list;
    GtkWidget *list_view;

    list = gtk_list_store_new(GUI_DETAILS_TYPE + 1,
			      G_TYPE_STRING,  /* name */
			      G_TYPE_STRING,  /* ram total */
			      G_TYPE_STRING,  /* ram avail */
			      G_TYPE_STRING,  /* uptime */
			      G_TYPE_STRING,  /* handles total */
			      G_TYPE_STRING,  /* handles avail */
			      G_TYPE_STRING,  /* space total */
			      G_TYPE_STRING,  /* space avail */
			      G_TYPE_STRING); /* server type */
   
    list_view = gtk_tree_view_new();

    for (i=0; i < GUI_DETAILS_TYPE + 1; i++) {
	GtkCellRenderer *renderer;
	
	col[i] = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(col[i], gui_details_column_name[i]);
	gtk_tree_view_append_column(GTK_TREE_VIEW(list_view), col[i]);

	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(col[i], renderer, TRUE);
	gtk_tree_view_column_add_attribute(col[i], renderer, "text", i);
    }

    gtk_tree_view_set_model(GTK_TREE_VIEW(list_view),
			    GTK_TREE_MODEL(list));

    if (sortable) {
	/* for all the numerical values, set up sorting */
	for (i=1; i < GUI_DETAILS_TYPE; i++) {
	    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(list),
					    i,
					    gui_details_float_string_compare,
					    GINT_TO_POINTER(i),
					    NULL);
	    
	    gtk_tree_view_column_set_sort_column_id(col[i], i);
	}
	
	/* for the text values, set up sorting */
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(list),
					GUI_DETAILS_NAME,
					gui_details_text_compare,
					GINT_TO_POINTER(GUI_DETAILS_NAME),
					NULL);
	
	gtk_tree_view_column_set_sort_column_id(col[GUI_DETAILS_NAME],
						GUI_DETAILS_NAME);
	
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(list),
					GUI_DETAILS_TYPE,
					gui_details_text_compare,
					GINT_TO_POINTER(GUI_DETAILS_TYPE),
					NULL);
	
	gtk_tree_view_column_set_sort_column_id(col[GUI_DETAILS_TYPE],
						GUI_DETAILS_TYPE);
    }

    gui_details_initialized = 1;

    *list_p = list;
    return list_view;
}

/* gui_details_update()
 *
 * Now just a wrapper for gui_details_view_fill(), used for 
 * setting up the details page.
 */
void gui_details_update(struct PVFS_mgmt_server_stat *s_stat,
			int s_stat_ct)
{
    gui_details_view_fill(gui_details_view,
			  gui_details_list,
			  gui_details_col,
			  s_stat,
			  s_stat_ct,
			  NULL);
			  
}

/* gui_details_view_fill()
 *
 * Parameters:
 * view
 * server_stat
 */
void gui_details_view_fill(GtkWidget *view,
			   GtkListStore *list,
			   GtkTreeViewColumn *col[],
			   struct PVFS_mgmt_server_stat *s_stat,
			   int s_stat_ct,
			   int *server_list)
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
    for (max=0, i=1; i < s_stat_ct; i++) {
	if (s_stat[i].ram_total_bytes > s_stat[max].ram_total_bytes) max = i;
    }
    rt_units = gui_units_size(s_stat[max].ram_total_bytes, &rt_div);

    for (max=0, i=1; i < s_stat_ct; i++) {
	if (s_stat[i].ram_free_bytes > s_stat[max].ram_free_bytes) max = i;
    }
    ra_units = gui_units_size(s_stat[max].ram_free_bytes, &ra_div);

    for (max=0, i=1; i < s_stat_ct; i++) {
	if (s_stat[i].uptime_seconds > s_stat[max].uptime_seconds) max = i;
    }
    up_units = gui_units_time(s_stat[max].uptime_seconds, &up_div);

    for (max=0, i=1; i < s_stat_ct; i++) {
	if (s_stat[i].handles_total_count > s_stat[max].handles_total_count) max = i;
    }
    ht_units = gui_units_count(s_stat[max].handles_total_count, &ht_div);

    for (max=0, i=1; i < s_stat_ct; i++) {
	if (s_stat[i].handles_available_count > s_stat[max].handles_available_count) max = i;
    }
    ha_units = gui_units_count(s_stat[max].handles_available_count, &ha_div);

    for (max=0, i=1; i < s_stat_ct; i++) {
	if (s_stat[i].bytes_total > s_stat[max].bytes_total) max = i;
    }
    bt_units = gui_units_size(s_stat[max].bytes_total, &bt_div);

    for (max=0, i=1; i < s_stat_ct; i++) {
	if (s_stat[i].bytes_available > s_stat[max].bytes_available) max = i;
    }
    ba_units = gui_units_size(s_stat[max].bytes_available, &ba_div);

    /* update column titles */
    snprintf(titlebuf, sizeof(titlebuf), "%s (%s)",
	     gui_details_column_name[GUI_DETAILS_RAM_TOT], rt_units);
    gtk_tree_view_column_set_title(col[GUI_DETAILS_RAM_TOT],
				   titlebuf);

    snprintf(titlebuf, sizeof(titlebuf), "%s (%s)",
	     gui_details_column_name[GUI_DETAILS_RAM_AVAIL], ra_units);
    gtk_tree_view_column_set_title(col[GUI_DETAILS_RAM_AVAIL],
				   titlebuf);

    snprintf(titlebuf, sizeof(titlebuf), "%s (%s)",
	     gui_details_column_name[GUI_DETAILS_UPTIME], up_units);
    gtk_tree_view_column_set_title(col[GUI_DETAILS_UPTIME],
				   titlebuf);

    snprintf(titlebuf, sizeof(titlebuf), "%s (%s)",
	     gui_details_column_name[GUI_DETAILS_HANDLES_TOT], ht_units);
    gtk_tree_view_column_set_title(col[GUI_DETAILS_HANDLES_TOT],
				   titlebuf);

    snprintf(titlebuf, sizeof(titlebuf), "%s (%s)",
	     gui_details_column_name[GUI_DETAILS_HANDLES_AVAIL], ha_units);
    gtk_tree_view_column_set_title(col[GUI_DETAILS_HANDLES_AVAIL],
				   titlebuf);

    snprintf(titlebuf, sizeof(titlebuf), "%s (%s)",
	     gui_details_column_name[GUI_DETAILS_SPACE_TOT], bt_units);
    gtk_tree_view_column_set_title(col[GUI_DETAILS_SPACE_TOT],
				   titlebuf);

    snprintf(titlebuf, sizeof(titlebuf), "%s (%s)",
	     gui_details_column_name[GUI_DETAILS_SPACE_AVAIL], ba_units);
    gtk_tree_view_column_set_title(col[GUI_DETAILS_SPACE_AVAIL],
				   titlebuf);

    /* update table of data */

    /* detach model from view until we're done updating */
    model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
    g_object_ref(model);
    gtk_tree_view_set_model(GTK_TREE_VIEW(view), NULL);

    /* just clear out the list and then refill it */
    gtk_list_store_clear(list);

    if (server_list == NULL) {
	for (i=0; i < s_stat_ct; i++) {
	    gui_details_view_insert(list,
				    &s_stat[i],
				    rt_div,
				    ra_div,
				    up_div,
				    ht_div,
				    ha_div,
				    bt_div,
				    ba_div);
	}
    }
    else {
	i = 0;
	while (server_list[i] >= 0) {
	    assert(server_list[i] < s_stat_ct);

	    gui_details_view_insert(list,
				    &s_stat[server_list[i]],
				    rt_div,
				    ra_div,
				    up_div,
				    ht_div,
				    ha_div,
				    bt_div,
				    ba_div);
	    i++;
	}
    }

    /* reattach model to view; remove our reference */
    gtk_tree_view_set_model(GTK_TREE_VIEW(view), model);
    g_object_unref(model);
}

static void gui_details_view_insert(GtkListStore *list,
				    struct PVFS_mgmt_server_stat *s_stat,
				    float rt_div,
				    float ra_div,
				    float up_div,
				    float ht_div,
				    float ha_div,
				    float bt_div,
				    float ba_div)
{
	GtkTreeIter iter;
	char *type, meta[] = "meta", data[] = "data", both[] = "both";
	char fmtbuf[GUI_DETAILS_TYPE][12]; /* don't need one for type */

	snprintf(fmtbuf[GUI_DETAILS_RAM_TOT], 12, "%.2f",
		 (float) s_stat->ram_total_bytes / rt_div);
	snprintf(fmtbuf[GUI_DETAILS_RAM_AVAIL], 12, "%.2f",
		 (float) s_stat->ram_free_bytes / ra_div);
	snprintf(fmtbuf[GUI_DETAILS_UPTIME], 12, "%.2f",
		 (float) s_stat->uptime_seconds / up_div);
	snprintf(fmtbuf[GUI_DETAILS_HANDLES_TOT], 12, "%.2f",
		 (float) s_stat->handles_total_count / ht_div);
	snprintf(fmtbuf[GUI_DETAILS_HANDLES_AVAIL], 12, "%.2f",
		 (float) s_stat->handles_available_count / ha_div);
	snprintf(fmtbuf[GUI_DETAILS_SPACE_TOT], 12, "%.2f",
		 (float) s_stat->bytes_total / bt_div);
	snprintf(fmtbuf[GUI_DETAILS_SPACE_AVAIL], 12, "%.2f",
		 (float) s_stat->bytes_available / ba_div);


	/* final formatting of data */
	if ((s_stat->server_type & PVFS_MGMT_IO_SERVER) &&
	    (s_stat->server_type & PVFS_MGMT_META_SERVER))
	{
	    type = both;
	}
	else if (s_stat->server_type & PVFS_MGMT_IO_SERVER)
	{
	    type = data;
	}
	else
	{
	    type = meta;
	}

	/* drop into list */
	gtk_list_store_append(list, &iter);

	gtk_list_store_set(list,
			   &iter,
			   GUI_DETAILS_NAME,
			   s_stat->bmi_address,
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

#ifdef GUI_DETAILS_ENABLE_SORTING
static gint gui_details_float_string_compare(GtkTreeModel *model,
					     GtkTreeIter *iter_a,
					     GtkTreeIter *iter_b,
					     gpointer col_id)
{
    gchar *string_a, *string_b;
    float a, b;

    gtk_tree_model_get(model, iter_a, (gint) col_id, &string_a, -1);
    gtk_tree_model_get(model, iter_b, (gint) col_id, &string_b, -1);

    a = (float) strtod(string_a, NULL);
    b = (float) strtod(string_b, NULL);

    g_free(string_a);
    g_free(string_b);

    if (a < b) return -1;
    else if (a == b) return 0;
    else return 1;
}

static gint gui_details_text_compare(GtkTreeModel *model,
				     GtkTreeIter *iter_a,
				     GtkTreeIter *iter_b,
				     gpointer col_id)
{
    int ret;
    gchar *string_a, *string_b;

    gtk_tree_model_get(model, iter_a, (gint) col_id, &string_a, -1);
    gtk_tree_model_get(model, iter_b, (gint) col_id, &string_b, -1);

    /* TODO: use some glib function instead? */
    ret = strcmp(string_a, string_b);

    g_free(string_a);
    g_free(string_b);

    return ret;
}
#endif
