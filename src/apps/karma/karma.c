#include <stdio.h>
#include <assert.h>
#include <gtk/gtk.h>

#include "karma.h"

#include "pvfs2.h"
#include "pvfs2-mgmt.h"

gint timer_callback(gpointer data);

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



static GtkWidget *get_notebook_pages(GtkWidget *window)
{
    GtkWidget *notebook;
    GtkWidget *statslabel;
#if 0
    GtkWidget *summarylabel, *perflabel;
#endif
    GtkWidget *statspage;

    notebook = gtk_notebook_new();

#if 0
    summarylabel = gtk_label_new("Summary");
#endif

    statslabel = gtk_label_new("Status");

    statspage = gui_status_setup();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), statspage, statslabel);

#if 0
    perflabel = gtk_label_new("Perf");
#endif

    return notebook;
}


int main(int   argc,
	 char *argv[])
{
    /* GtkWidget is the storage type for widgets */
    GtkWidget *window;
    GtkWidget *main_vbox;
    GtkWidget *menubar, *messageframe, *notepagesframe;

    /* This is called in all GTK applications. Arguments are parsed
     * from the command line and are returned to the application. */
    gtk_init(&argc, &argv);

    /* create a new window */
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Karma");

    g_signal_connect(G_OBJECT(window),
		     "delete_event",
		     G_CALLBACK(delete_event),
		     NULL);

    g_signal_connect(G_OBJECT(window),
		     "destroy",
		     G_CALLBACK(destroy_event),
		     NULL);

    menubar = gui_menu_setup(window);

    notepagesframe = get_notebook_pages(window);

    messageframe = gui_message_setup();

    if (gui_comm_setup()) return 1;

    /* create vbox, drop in menus, notepages, and message window */
    main_vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER (main_vbox), 1);
    gtk_container_add(GTK_CONTAINER(window), main_vbox);

    gtk_box_pack_start(GTK_BOX(main_vbox), menubar, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(main_vbox), notepagesframe, FALSE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(main_vbox), messageframe, TRUE, TRUE, 0);

    /* show the window */
    gtk_widget_show_all(window);

    timer_callback(NULL);

    gtk_timeout_add(4000 /* 4 seconds */,
		    timer_callback,
		    NULL);

    /* handle events */
    gtk_main();

    return 0;
}

gint timer_callback(gpointer data)
{
    struct PVFS_mgmt_server_stat *svr_stat;
    int i, ret, svr_stat_ct = 0;
    struct gui_graph_data *graph_data;

    ret = gui_comm_stats_retrieve(&svr_stat, &svr_stat_ct);
    if (ret != 0) {
	return -1;
    }

    gui_data_prepare(svr_stat, svr_stat_ct, &graph_data);

    /* note: relying on graph data values going from 0...5 */
    for (i=0; i < 6; i++) {
	gui_status_graph_update(i,
				&graph_data[i]);
    }

    return 1; /* schedule it again */
}
