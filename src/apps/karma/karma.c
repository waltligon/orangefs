#include <stdio.h>
#include <assert.h>
#include <gtk/gtk.h>

#include "karma.h"

#include "pvfs2.h"
#include "pvfs2-mgmt.h"

static gint status_timer_callback(gpointer data);
static gint traffic_timer_callback(gpointer data);

static GtkWidget *main_window;

static gint delete_event(GtkWidget *widget,
			 GdkEvent  *event,
			 gpointer   data)
{
    return 0; /* call destroy */
}

static void destroy_event(GtkWidget *widget,
			  gpointer   data)
{
    gtk_main_quit();
}

static GtkWidget *get_notebook_pages(void)
{
    GtkWidget *notebook;
    GtkWidget *statslabel, *detailslabel, *trafficlabel;
    GtkWidget *statspage, *detailspage, *trafficpage;

    notebook = gtk_notebook_new();

    statslabel = gtk_label_new("Status");
    statspage  = gui_status_setup();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			     statspage,
			     statslabel);

    detailslabel = gtk_label_new("Details");
    detailspage  = gui_details_setup();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			     detailspage,
			     detailslabel);

    trafficlabel = gtk_label_new("Traffic");
    trafficpage  = gui_traffic_setup();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			     trafficpage,
			     trafficlabel);

    return notebook;
}

void gui_set_title(char *title)
{
    gtk_window_set_title(GTK_WINDOW(main_window), title);
}

int main(int   argc,
	 char *argv[])
{
    /* GtkWidget is the storage type for widgets */
    GtkWidget *main_vbox;
    GtkWidget *menubar, *messageframe, *notepagesframe;

    /* This is called in all GTK applications. Arguments are parsed
     * from the command line and are returned to the application. */
    gtk_init(&argc, &argv);

    /* create a new window */
    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "Karma");

    g_signal_connect(G_OBJECT(main_window),
		     "delete_event",
		     G_CALLBACK(delete_event),
		     NULL);

    g_signal_connect(G_OBJECT(main_window),
		     "destroy",
		     G_CALLBACK(destroy_event),
		     NULL);

    menubar = gui_menu_setup(main_window);

    if (gui_comm_setup()) return 1;

    notepagesframe = get_notebook_pages();

    messageframe = gui_message_setup();

    /* create vbox, drop in menus, notepages, and message window */
    main_vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(main_vbox), 1);
    gtk_container_add(GTK_CONTAINER(main_window), main_vbox);

    gtk_box_pack_start(GTK_BOX(main_vbox), menubar, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(main_vbox), notepagesframe, FALSE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(main_vbox), messageframe, TRUE, TRUE, 0);

    /* show the window */
    gtk_widget_show_all(main_window);

    status_timer_callback(NULL);
    traffic_timer_callback(NULL);

    gtk_timeout_add(5000 /* 5 seconds */,
		    status_timer_callback,
		    NULL);

    gtk_timeout_add(1000 /* 1 second */,
		    traffic_timer_callback,
		    NULL);

    /* handle events */
    gtk_main();

    return 0;
}

static gint status_timer_callback(gpointer data)
{
    struct PVFS_mgmt_server_stat *svr_stat;
    int i, ret, svr_stat_ct = 0;
    struct gui_status_graph_data *graph_data;

    ret = gui_comm_stats_retrieve(&svr_stat, &svr_stat_ct);
    if (ret != 0) {
	return -1;
    }

    gui_status_data_prepare(svr_stat, svr_stat_ct, &graph_data);

    /* note: relying on graph data values going from 0...5 */
    for (i=0; i < 6; i++) {
	gui_status_graph_update(i,
				&graph_data[i]);
    }

    gui_details_update(svr_stat, svr_stat_ct);

    return 1; /* schedule it again */
}

static gint traffic_timer_callback(gpointer data)
{
    int ret, svr_stat_ct = 0;
    struct gui_traffic_raw_data *raw_traffic_data;

    static struct gui_traffic_graph_data *traffic_graph = NULL;

    ret = gui_comm_traffic_retrieve(&raw_traffic_data, &svr_stat_ct);

    /* we are responsible for allocating the traffic graph data storage;
     * this way we only have to do it once.
     */
    if (traffic_graph == NULL) {
	traffic_graph = (struct gui_traffic_graph_data *)
	    malloc(sizeof(struct gui_traffic_graph_data));
	traffic_graph->svr_data = (struct gui_traffic_server_data *)
	    malloc(svr_stat_ct * sizeof(struct gui_traffic_server_data));
	traffic_graph->svr_ct = svr_stat_ct;
    }

    if (traffic_graph->svr_ct != svr_stat_ct) {
	free(traffic_graph->svr_data);
	traffic_graph->svr_data = (struct gui_traffic_server_data *)
	    malloc(svr_stat_ct * sizeof(struct gui_traffic_server_data));
    }

    gui_traffic_data_prepare(raw_traffic_data, svr_stat_ct, traffic_graph);

    gui_traffic_graph_update(traffic_graph);

    return 1; /* schedule it again */
}
