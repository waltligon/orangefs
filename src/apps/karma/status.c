/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <gtk/gtk.h>

#include "karma.h"

/* status.c
 *
 * This file implements the "status" page of the notebook.  The functions
 * available externally for manipulating the status page are:
 * - gui_status_setup(void)
 * - gui_status_graph_update(graph_id, nr_bars, firstval[], secondval[],
 *                           bar_color[], footer)
 *
 * The first of these is called once at startup.  The second is called
 * repeatedly to update the data on a given graph.  The particular graph
 * to update is indicated by the graph_id field, which takes one of the
 * GUI_STATUS_* values enumerated in karma.h.
 */

/* struct gui_status_graph_state gui_status_graphs[]
 *
 * Holds graph state so that the graph can be redrawn on exposure
 * events.
 *
 */
#define GUI_STATUS_GRAPH_NR_TICS 5

static struct gui_status_graph_state
{
    GtkWidget *vbox;
    GtkWidget *frame;
    GtkWidget *xaxis_label[GUI_STATUS_GRAPH_NR_TICS];
    GtkWidget *footer_label;

    /* title, footer */
    char title[64];
    char footer[64];

    /* drawing particulars */
    GtkWidget *drawing_area;
    GdkPixmap *pixmap;
    GdkGC *green_gc, *blue_gc, *yellow_gc, *red_gc;

    /* data for graphs */
    int nr_bars;
    float *firstval, *secondval;
    int *bar_color;
} gui_status_graphs[6];

/* internal helper fn prototypes */
static void gui_status_graph_draw_stacked(
    struct gui_status_graph_state *g_state,
    char *title,
    int nr_bars,
    float *firstval,
    float *secondval,
    int *bar_color,
    char *footer);
static GtkWidget *gui_status_graph_setup(
    gint width,
    gint height,
    struct gui_status_graph_state *g_state);
/* internal callback fn prototypes */
static gint gui_status_graph_expose_callback(
    GtkWidget * drawing_area,
    GdkEventExpose * event,
    gpointer graph_state_ptr);
static gint gui_status_graph_configure_callback(
    GtkWidget * drawing_area,
    GdkEvent * event,
    gpointer graph_state_ptr);
static gint gui_status_graph_button_press_callback(
    GtkWidget * drawing_area,
    GdkEventButton * event,
    gpointer graph_state_ptr);

static void gui_status_server_popup(
    struct PVFS_mgmt_server_stat *svr_stat,
    int svr_stat_ct,
    int svr_index);

/* gui_status_setup()
 *
 * Creates the status table widget.
 *
 * Returns pointer to status table widget.
 */
GtkWidget *gui_status_setup(
    void)
{
    gint width = 300, height = 200;
    GtkWidget *table;
    GtkWidget *spaceframe, *uptimeframe, *mhandleframe, *dhandleframe,
        *memoryframe, *cpuframe;

    table = gtk_table_new(2, 3, 1);

    spaceframe = gui_status_graph_setup(width, height,
                                        &gui_status_graphs[GUI_STATUS_SPACE]);
    gtk_table_attach_defaults(GTK_TABLE(table), spaceframe, 0, 1, 0, 1);

    uptimeframe = gui_status_graph_setup(width, height,
                                         &gui_status_graphs[GUI_STATUS_UPTIME]);
    gtk_table_attach_defaults(GTK_TABLE(table), uptimeframe, 0, 1, 1, 2);

    mhandleframe = gui_status_graph_setup(width, height,
                                          &gui_status_graphs[GUI_STATUS_META]);
    gtk_table_attach_defaults(GTK_TABLE(table), mhandleframe, 1, 2, 0, 1);

    dhandleframe = gui_status_graph_setup(width, height,
                                          &gui_status_graphs[GUI_STATUS_DATA]);
    gtk_table_attach_defaults(GTK_TABLE(table), dhandleframe, 1, 2, 1, 2);

    memoryframe = gui_status_graph_setup(width, height,
                                         &gui_status_graphs[GUI_STATUS_MEMORY]);
    gtk_table_attach_defaults(GTK_TABLE(table), memoryframe, 2, 3, 0, 1);

    cpuframe = gui_status_graph_setup(width, height,
                                      &gui_status_graphs[GUI_STATUS_CPU]);
    gtk_table_attach_defaults(GTK_TABLE(table), cpuframe, 2, 3, 1, 2);

    return table;
}


/* gui_status_graph_update()
 *
 * External interface for updating graphs.
 */
void gui_status_graph_update(
    int graph_id,
    struct gui_status_graph_data *graph_data)
{
    int count;
    struct gui_status_graph_state *g_state;

    assert(graph_id >= 0 && graph_id < 6);
    assert(graph_data != NULL);

    count = graph_data->count;

    assert(count >= 0);

    g_state = &gui_status_graphs[graph_id];

    if (g_state->nr_bars != count ||
        (g_state->secondval == NULL && graph_data->has_second_val))
    {
        /* free old space */
        if (g_state->firstval != NULL)
        {
            free(g_state->firstval);
            free(g_state->bar_color);
        }
        if (g_state->secondval != NULL)
        {
            free(g_state->secondval);
            g_state->secondval = NULL;
        }

        /* allocate new */
        g_state->nr_bars = count;
        if (count > 0)
        {
            g_state->firstval = (float *) malloc(sizeof(float) * count);
            g_state->bar_color = (int *) malloc(sizeof(int) * count);
            if (graph_data->has_second_val)
            {
                g_state->secondval = (float *) malloc(sizeof(float) * count);
            }
        }
    }
    else if (g_state->secondval != NULL && graph_data->has_second_val == 0)
    {
        /* free unneeded secondval array */
        free(g_state->secondval);
        g_state->secondval = NULL;
    }

    /* copy new graph data */
    memcpy(g_state->firstval, graph_data->first_val, sizeof(float) * count);
    memcpy(g_state->bar_color, graph_data->bar_color, sizeof(int) * count);
    if (graph_data->has_second_val)
    {
        memcpy(g_state->secondval, graph_data->second_val,
               sizeof(float) * count);
    }

    /* copy title and footer */
    strncpy(g_state->title, graph_data->title, 63);
    strncpy(g_state->footer, graph_data->footer, 63);

    gui_status_graph_draw_stacked(&gui_status_graphs[graph_id],
                                  graph_data->title,
                                  count,
                                  graph_data->first_val,
                                  (graph_data->has_second_val) ? graph_data->
                                  second_val : NULL, graph_data->bar_color,
                                  graph_data->footer);
}

/******* internal helper functions ********/

/* gui_status_graph_draw_stacked()
 *
 * TODO: Allow graph to start at a non-zero value, so that we can get
 * more detail.
 */
static void gui_status_graph_draw_stacked(
    struct gui_status_graph_state *g_state,
    char *title,
    int nr_bars,
    float *firstval,
    float *secondval,
    int *bar_color,
    char *footer)
{
    GdkGC *gc;
    GdkRectangle update_rect;
    gint i, width, graphwidth, graphoffset, height, barheight, barspace,
        topspace = 20;
    float maxval;
    gchar *labelstring;

    /* grab width, height, and fill in update_rect */
    width = g_state->drawing_area->allocation.width;
    height = g_state->drawing_area->allocation.height;
    update_rect.width = width;
    update_rect.height = height;
    update_rect.x = 0;
    update_rect.y = 0;

    /* reset title */
    gtk_frame_set_label(GTK_FRAME(g_state->frame),
                        (title == NULL) ? "" : title);

    /* clear the graph */
    gdk_draw_rectangle(g_state->pixmap,
                       g_state->drawing_area->style->white_gc,
                       TRUE, 0, 0, width, height);

    /* handle no data case separately */
    if (nr_bars == 0)
    {
        return;
    }

    graphwidth = (int) (0.8 * (float) width);
    graphoffset = (width - graphwidth) / 2;

    barspace = (gint) (((float) ((height - topspace) / nr_bars)) * 0.30);
    barheight = ((height - topspace) - nr_bars * barspace) / nr_bars;
    assert(barheight > 0);

    /* aesthetics: limit maximum bar height */
    if (barheight > (height - topspace) / 8)
    {
        barheight = (height - topspace) / 8;
    }

    /* find largest composite value */
    if (secondval != NULL)
        maxval = firstval[0] + secondval[0];
    else
        maxval = firstval[0];

    for (i = 1; i < nr_bars; i++)
    {
        float tmpval;

        if (secondval != NULL)
            tmpval = firstval[i] + secondval[i];
        else
            tmpval = firstval[i];

        if (maxval < tmpval)
            maxval = tmpval;
    }

    maxval = maxval * 1.1;

    /* update labels on x-axis */
    labelstring = malloc(64);
    for (i = 0; i < GUI_STATUS_GRAPH_NR_TICS; i++)
    {
        snprintf(labelstring, 64, "%.2f",
                 (maxval * (float) i) / (float) (GUI_STATUS_GRAPH_NR_TICS - 1));

        gtk_label_set_text(GTK_LABEL(g_state->xaxis_label[i]), labelstring);
    }
    free(labelstring);


    /* draw in top tic marks */
    gdk_draw_line(g_state->pixmap,
                  g_state->drawing_area->style->black_gc,
                  graphoffset, 5, graphwidth + graphoffset, 5);
    for (i = 0; i < GUI_STATUS_GRAPH_NR_TICS; i++)
    {
        int xpos = (i * graphwidth) / (GUI_STATUS_GRAPH_NR_TICS - 1) +
            graphoffset;

        gdk_draw_line(g_state->pixmap,
                      g_state->drawing_area->style->black_gc,
                      xpos, 0, xpos, 10);
    }

    for (i = 0; i < nr_bars; i++)
    {
        gint barlength = (gint) (((float) graphwidth) * (firstval[i] / maxval));
        gint ystart = i * (barheight + barspace) + topspace;

        switch (bar_color[i])
        {
        case BAR_GREEN:
            gc = g_state->green_gc;
            break;
        case BAR_YELLOW:
            gc = g_state->yellow_gc;
            break;
        default:
        case BAR_RED:
            gc = g_state->red_gc;
            break;
        }

        /* aesthetics: always draw something */
        if (barlength == 0)
            barlength = 1;

        gdk_draw_rectangle(g_state->pixmap,
                           (secondval != NULL) ? g_state->blue_gc : gc,
                           TRUE, graphoffset, ystart, barlength, barheight);


        if (secondval != NULL)
        {
            gint secondbarlength = (gint) (((float) graphwidth) *
                                           (secondval[i] / maxval));

            /* aesthetics: always draw something */
            if (secondbarlength == 0)
                secondbarlength = 1;

            gdk_draw_rectangle(g_state->pixmap,
                               gc,
                               TRUE,
                               barlength + graphoffset,
                               ystart, secondbarlength, barheight);
        }
    }

    /* fill in footer, if any */
    if (footer != NULL)
    {
        gtk_label_set_text(GTK_LABEL(g_state->footer_label), footer);
        gtk_label_set_justify(GTK_LABEL(g_state->footer_label),
                              GTK_JUSTIFY_LEFT);
    }
    else
    {
        gtk_label_set_text(GTK_LABEL(g_state->footer_label), "");
    }

    /* force a redraw */
    gtk_widget_draw(g_state->drawing_area, &update_rect);

    return;
}

/* gui_status_graph_setup(width, height, g_state)
 *
 */
static GtkWidget *gui_status_graph_setup(
    gint width,
    gint height,
    struct gui_status_graph_state *g_state)
{
    int i;
    GtkWidget *hbox;
    gint events;

    /* create frame and vbox */
    g_state->frame = gtk_frame_new("");
    gtk_frame_set_label_align(GTK_FRAME(g_state->frame), 0.0, 0.0);

    g_state->vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(g_state->vbox), 5);
    gtk_container_add(GTK_CONTAINER(g_state->frame), g_state->vbox);

    /* create hbox, drop in vbox, add in labels */
    hbox = gtk_hbox_new(TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 1);
    gtk_box_pack_start(GTK_BOX(g_state->vbox), hbox, TRUE, FALSE, 0);
    for (i = 0; i < GUI_STATUS_GRAPH_NR_TICS; i++)
    {
        g_state->xaxis_label[i] = gtk_label_new("");
        gtk_box_pack_start(GTK_BOX(hbox),
                           g_state->xaxis_label[i], TRUE, TRUE, 0);
    }

    /* add in drawing area */
    g_state->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(GTK_WIDGET(g_state->drawing_area),
                                width, height);
    gtk_box_pack_start(GTK_BOX(g_state->vbox),
                       g_state->drawing_area, TRUE, TRUE, 0);

    /* add in footer label */
    g_state->footer_label = gtk_label_new("");
    gtk_label_set_justify(GTK_LABEL(g_state->footer_label), GTK_JUSTIFY_LEFT);
    gtk_box_pack_end(GTK_BOX(g_state->vbox),
                     g_state->footer_label, FALSE, TRUE, 0);

    g_state->pixmap = NULL;
    g_state->firstval = NULL;

    /* configure event, at startup, will do first drawing */
    g_signal_connect(G_OBJECT(g_state->drawing_area),
                     "configure_event",
                     G_CALLBACK(gui_status_graph_configure_callback),
                     (gpointer) g_state);
    g_signal_connect(G_OBJECT(g_state->drawing_area),
                     "expose_event",
                     G_CALLBACK(gui_status_graph_expose_callback),
                     (gpointer) g_state);

    events = gtk_widget_get_events(GTK_WIDGET(g_state->drawing_area));
    events |= GDK_BUTTON_PRESS_MASK;
    gtk_widget_set_events(GTK_WIDGET(g_state->drawing_area), events);

    g_signal_connect(G_OBJECT(g_state->drawing_area),
                     "button_press_event",
                     G_CALLBACK(gui_status_graph_button_press_callback),
                     (gpointer) g_state);

    return g_state->frame;
}

/******* internal callback functions *******/

static gint gui_status_graph_configure_callback(
    GtkWidget * drawing_area,
    GdkEvent * event,
    gpointer graph_state_ptr)
{
    struct gui_status_graph_state *g_state;

    g_state = (struct gui_status_graph_state *) graph_state_ptr;

    assert(g_state != NULL);
    assert(drawing_area == g_state->drawing_area);

    if (g_state->pixmap != NULL)
    {
        g_object_unref(g_state->pixmap);
        g_object_unref(g_state->red_gc);
        g_object_unref(g_state->green_gc);
        g_object_unref(g_state->blue_gc);
        g_object_unref(g_state->yellow_gc);
    }

    g_state->pixmap = gdk_pixmap_new(drawing_area->window,
                                     drawing_area->allocation.width,
                                     drawing_area->allocation.height, -1);

    /* create some graphics contexts for various colors */
    g_state->red_gc = gui_get_new_fg_color_gc(g_state->drawing_area, 255, 0, 0);
    g_state->green_gc = gui_get_new_fg_color_gc(g_state->drawing_area,
                                                0, 255, 0);
    g_state->blue_gc = gui_get_new_fg_color_gc(g_state->drawing_area,
                                               0, 0, 255);
    g_state->yellow_gc = gui_get_new_fg_color_gc(g_state->drawing_area,
                                                 255, 255, 0);
    gdk_draw_rectangle(g_state->pixmap,
                       drawing_area->style->white_gc,
                       TRUE,
                       0, 0,
                       drawing_area->allocation.width,
                       drawing_area->allocation.height);

    if (g_state->firstval != NULL)
    {
        gui_status_graph_draw_stacked(g_state,
                                      g_state->title,
                                      g_state->nr_bars,
                                      g_state->firstval,
                                      g_state->secondval,
                                      g_state->bar_color, NULL);
    }

    return 1;
}

/* Refill the screen from the backing pixmap */
static gint gui_status_graph_expose_callback(
    GtkWidget * drawing_area,
    GdkEventExpose * event,
    gpointer graph_state_ptr)
{
    struct gui_status_graph_state *g_state;

    g_state = (struct gui_status_graph_state *) graph_state_ptr;

    assert(g_state != NULL);
    assert(g_state->drawing_area == drawing_area);

    gdk_draw_pixmap(drawing_area->window,
                    drawing_area->style->fg_gc[GTK_WIDGET_STATE(drawing_area)],
                    g_state->pixmap,
                    event->area.x, event->area.y,
                    event->area.x, event->area.y,
                    event->area.width, event->area.height);
    return 0;
}

static gint gui_status_graph_button_press_callback(
    GtkWidget * drawing_area,
    GdkEventButton * event,
    gpointer graph_state_ptr)
{
    gint i, height, barspace, barheight;
    gint topspace = 20;
    struct gui_status_graph_state *g_state;
    struct PVFS_mgmt_server_stat *svr_stat;
    int svr_stat_ct;

    g_state = (struct gui_status_graph_state *) graph_state_ptr;

    assert(g_state != NULL);
    assert(g_state->drawing_area == drawing_area);

    /* ignore anything that isn't a double-click */
    if (event->type != GDK_2BUTTON_PRESS)
        return 0;

    /* calculate bar placement */
    height = drawing_area->allocation.height;
    barspace = (gint) (((float) ((height - topspace) / g_state->nr_bars)) *
                       0.30);
    barheight = ((height - topspace) - g_state->nr_bars * barspace) /
        g_state->nr_bars;
    assert(barheight > 0);

    /* aesthetics: limit maximum bar height */
    if (barheight > (height - topspace) / 8)
    {
        barheight = (height - topspace) / 8;
    }

    i = (((gint) event->y) - topspace) / (barheight + barspace);
    if (i >= g_state->nr_bars)
    {
        return 0;       /* button press past bars */
    }
    else if (((gint) event->y) - topspace - (i * (barheight + barspace)) >
             barheight)
    {
        return 0;       /* button press in space after a legitimate bar */
    }

    /* button was pressed on bar i */

    /* currently we display bars in server order, so no mapping necessary */
    gui_comm_stats_retrieve(&svr_stat, &svr_stat_ct);

    /* if # of servers just changed, just ignore the click for now */
    if (svr_stat_ct != g_state->nr_bars)
        return 0;

    gui_status_server_popup(svr_stat, svr_stat_ct, i);

    return 0;
}

static void gui_status_server_popup(
    struct PVFS_mgmt_server_stat *svr_stat,
    int svr_stat_ct,
    int svr_index)
{
    GtkWidget *dialog, *view;
    GtkListStore *list;
    GtkTreeViewColumn *col[GUI_DETAILS_TYPE + 1];
    GtkTreeSelection *selection;
    int svr_list[2];

    dialog = gtk_dialog_new_with_buttons("Server Details",
                                         GTK_WINDOW(main_window),
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_STOCK_OK, GTK_RESPONSE_NONE, NULL);
    g_signal_connect_swapped(GTK_OBJECT(dialog),
                             "response",
                             G_CALLBACK(gtk_widget_destroy),
                             GTK_OBJECT(dialog));

    /* create contents of popup */
    view = gui_details_view_new(&list, col, 0);

    svr_list[0] = svr_index;
    svr_list[1] = -1;

    gui_details_view_fill(view, list, col, svr_stat, svr_stat_ct, svr_list);

    /* make it so that one cannot select things in the list */
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_NONE);

    /* unref list so it will be freed when view is destroyed */
    g_object_unref(list);

    /* drop contents into popup; display */
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), view);

    /* TODO: COME UP WITH SOME WAY TO KEEP LIST FROM STEALING FOCUS! */
    gtk_widget_show_all(dialog);

    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
