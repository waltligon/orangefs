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

/* traffic.c
 *
 * This file implements the "traffic" page of the notebook.  The functions
 * available externally for manipulating the traffic page are:
 * - gui_traffic_setup(void)
 * - gui_traffic_update()
 *
 * The first of these is called once at startup.  The second is called
 * repeatedly to update the data on the graph.
 */

#define GUI_TRAFFIC_GRAPH_NR_TICS 11

/* gui_traffic_graph_configured is used to prevent drawing to graph until
 * it has been configured -- before then the resources aren't really
 * allocated.
 *
 * As I understand it we don't have any race problems here. -- RobR
 */
static int gui_traffic_graph_configured = 0;

/* gui_traffic_graph_state keeps the data needed to reproduce the graph
 * in case we are resized and need to draw again.
 */
static struct gui_traffic_graph_state
{
    GtkWidget *xaxis_io_label[GUI_TRAFFIC_GRAPH_NR_TICS];
    GtkWidget *xaxis_meta_label[GUI_TRAFFIC_GRAPH_NR_TICS];
    GtkWidget *io_label, *meta_label;

    /* drawing particulars */
    GtkWidget *drawing_area;
    GdkPixmap *pixmap;
    GdkGC *green_gc, *blue_gc, *yellow_gc, *orange_gc, *purple_gc;

    /* data for graph */
    int svr_ct;
    float *read;
    float *write;
    float *rmeta;
    float *wmeta;

    /* history data */
    float io_max, meta_max;
} gui_traffic_graph;

static void gui_traffic_graph_draw(
    void);

/* internal callback fn prototypes */
static gint gui_traffic_graph_configure_callback(
    GtkWidget * drawing_area,
    GdkEvent * event,
    gpointer nullptr);

static gint gui_traffic_graph_expose_callback(
    GtkWidget * drawing_area,
    GdkEventExpose * event,
    gpointer nullptr);

#if 0
static gint gui_traffic_graph_visibility_callback(
    GtkWidget * drawing_area,
    GdkEventVisibility * event,
    gpointer nullptr);
#endif


/* gui_traffic_setup()
 *
 * Creates the traffic graph widget.
 *
 * Returns a pointer to the traffic graph widget.
 */
GtkWidget *gui_traffic_setup(
    void)
{
    int i;
    gint width = 850, height = 450;
    GtkWidget *vbox, *labelvbox, *hbox;

    /* initialize gui_traffic_graph structure */
    gui_traffic_graph.drawing_area = NULL;
    gui_traffic_graph.pixmap = NULL;
    gui_traffic_graph.green_gc = NULL;
    gui_traffic_graph.blue_gc = NULL;
    gui_traffic_graph.yellow_gc = NULL;
    gui_traffic_graph.orange_gc = NULL;
    gui_traffic_graph.purple_gc = NULL;
    gui_traffic_graph.svr_ct = 0;

    vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

    /* create vbox, drop in hbox, add in io labels */
    labelvbox = gtk_vbox_new(TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(labelvbox), 1);
    gtk_box_pack_start(GTK_BOX(hbox), labelvbox, TRUE, FALSE, 0);

    for (i = 0; i < GUI_TRAFFIC_GRAPH_NR_TICS; i++)
    {
        gui_traffic_graph.xaxis_io_label[i] = gtk_label_new("");
        gtk_box_pack_end(GTK_BOX(labelvbox),
                         gui_traffic_graph.xaxis_io_label[i], TRUE, TRUE, 0);
    }

    /* add in drawing area */
    gui_traffic_graph.drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(GTK_WIDGET(gui_traffic_graph.drawing_area),
                                width, height);
    gtk_box_pack_start(GTK_BOX(hbox),
                       gui_traffic_graph.drawing_area, TRUE, TRUE, 0);

    /* create vbox, drop in hbox, add in meta labels */
    labelvbox = gtk_vbox_new(TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(labelvbox), 1);
    gtk_box_pack_start(GTK_BOX(hbox), labelvbox, TRUE, FALSE, 0);

    for (i = 0; i < GUI_TRAFFIC_GRAPH_NR_TICS; i++)
    {
        gui_traffic_graph.xaxis_meta_label[i] = gtk_label_new("");
        gtk_box_pack_end(GTK_BOX(labelvbox),
                         gui_traffic_graph.xaxis_meta_label[i], TRUE, TRUE, 0);
    }

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, FALSE, 0);

    /* add in io main label */
    gui_traffic_graph.io_label = gtk_label_new("I/O Bandwidth ()");
    gtk_label_set_justify(GTK_LABEL(gui_traffic_graph.io_label),
                          GTK_JUSTIFY_LEFT);
    gtk_box_pack_start(GTK_BOX(hbox),
                       gui_traffic_graph.io_label, FALSE, TRUE, 0);

    /* add in meta main label */
    gui_traffic_graph.meta_label = gtk_label_new("Metadata Ops ()");
    gtk_label_set_justify(GTK_LABEL(gui_traffic_graph.meta_label),
                          GTK_JUSTIFY_RIGHT);
    gtk_box_pack_end(GTK_BOX(hbox),
                     gui_traffic_graph.meta_label, FALSE, TRUE, 0);

    /* zero axis history values */
    gui_traffic_graph.io_max = 0.0;
    gui_traffic_graph.meta_max = 0.0;

    /* configure event, at startup, will do first drawing of drawing area */
    g_signal_connect(G_OBJECT(gui_traffic_graph.drawing_area),
                     "configure_event",
                     G_CALLBACK(gui_traffic_graph_configure_callback), NULL);
    g_signal_connect(G_OBJECT(gui_traffic_graph.drawing_area),
                     "expose_event",
                     G_CALLBACK(gui_traffic_graph_expose_callback), NULL);
#if 0
    g_signal_connect(G_OBJECT(hbox),
                     "visibility_event",
                     G_CALLBACK(gui_traffic_graph_visibility_callback), NULL);
#endif

    return vbox;
}

/* gui_traffic_graph_update()
 *
 * Stores new data for traffic graph for display at first opportunity.
 */
void gui_traffic_graph_update(
    struct gui_traffic_graph_data *data)
{
    int i, svr_ct = data->svr_ct;

    if (gui_traffic_graph.svr_ct != svr_ct)
    {
        if (gui_traffic_graph.svr_ct != 0)
        {
            free(gui_traffic_graph.read);
            free(gui_traffic_graph.write);
            free(gui_traffic_graph.rmeta);
            free(gui_traffic_graph.wmeta);
        }
        gui_traffic_graph.read = (float *) malloc(svr_ct * sizeof(float));
        gui_traffic_graph.write = (float *) malloc(svr_ct * sizeof(float));
        gui_traffic_graph.rmeta = (float *) malloc(svr_ct * sizeof(float));
        gui_traffic_graph.wmeta = (float *) malloc(svr_ct * sizeof(float));

        gui_traffic_graph.io_max = 0.0;
        gui_traffic_graph.meta_max = 0.0;
    }

    for (i = 0; i < svr_ct; i++)
    {
        gui_traffic_graph.read[i] = data->svr_data[i].data_read;
        gui_traffic_graph.write[i] = data->svr_data[i].data_write;
        gui_traffic_graph.rmeta[i] = data->svr_data[i].meta_read;
        gui_traffic_graph.wmeta[i] = data->svr_data[i].meta_write;
    }

    gui_traffic_graph.svr_ct = svr_ct;

    /* set main labels here rather than storing text */
    gtk_label_set_text(GTK_LABEL(gui_traffic_graph.io_label), data->io_label);
    gtk_label_set_text(GTK_LABEL(gui_traffic_graph.meta_label),
                       data->meta_label);

    if (gui_traffic_graph_configured)
        gui_traffic_graph_draw();
}

/* gui_traffic_graph_draw() actually draws the graph.
 *
 * This should only be called after we have received a configure event.
 */
static void gui_traffic_graph_draw(
    void)
{
    int i, graphheight, graphoffset, barwidth, svrspace,
        leftspace = 20, rightspace = 20;
    char *io_string, *meta_string;
    gint width, height;
    float tmp = 0.0, max_magnitude, max_r, max_w, max_rm, max_wm, max_m, max_bw;
    GdkRectangle update_rect;

    /* grab width, height */
    width = gui_traffic_graph.drawing_area->allocation.width;
    height = gui_traffic_graph.drawing_area->allocation.height;
    update_rect.width = width;
    update_rect.height = height;
    update_rect.x = 0;
    update_rect.y = 0;

    gdk_draw_rectangle(gui_traffic_graph.pixmap,
                       gui_traffic_graph.drawing_area->style->white_gc,
                       TRUE, 0, 0, width, height);

    /* drop out early if no data */
    if (gui_traffic_graph.svr_ct == 0)
        return;

    /* aesthetics: getting the graphheight just right to match labels */
    graphheight = (int) (0.90 * (float) height);
    graphoffset = (height - graphheight) / 2;

    svrspace = (int) ((float) ((width - leftspace - rightspace) /
                               gui_traffic_graph.svr_ct));

    /* aesthetics: limit maximum svrspace */
    if (svrspace > (width - leftspace - rightspace) / 6)
    {
        svrspace = (width - leftspace - rightspace) / 6;
    }

    barwidth = (int) (0.8 * (float) svrspace / 4.0);

    assert(barwidth > 0);

    max_r = gui_traffic_graph.read[0];
    max_w = gui_traffic_graph.write[0];
    max_rm = gui_traffic_graph.rmeta[0];
    max_wm = gui_traffic_graph.wmeta[0];
    for (i = 1; i < gui_traffic_graph.svr_ct; i++)
    {
        if (max_r < gui_traffic_graph.read[i])
            max_r = gui_traffic_graph.read[i];
        if (max_w < gui_traffic_graph.write[i])
            max_w = gui_traffic_graph.write[i];
        if (max_rm < gui_traffic_graph.rmeta[i])
            max_rm = gui_traffic_graph.rmeta[i];
        if (max_wm < gui_traffic_graph.wmeta[i])
            max_wm = gui_traffic_graph.wmeta[i];
    }
    max_bw = (max_r < max_w) ? max_w : max_r;
    max_m = (max_rm < max_wm) ? max_wm : max_rm;

    /* aesthetics: keep axis values at consistent points.
     *
     * we do this by:
     * - saving previous maximums and using a weighted average to
     *   keep from decreasing axis maximum too quickly
     * - forcing the axis maximum to be a power of 10
     *   - well, not exactly.
     *
     * Note: this works in conjunction with prep.c code that
     *       keeps the divisor fixed for a longer period so our
     *       units aren't changing all the time too.
     */
    if (gui_traffic_graph.io_max < max_bw)
    {
        gui_traffic_graph.io_max = max_bw;
    }
    else
    {
        gui_traffic_graph.io_max = 0.8 * gui_traffic_graph.io_max +
            0.2 * max_bw;
        tmp = gui_traffic_graph.io_max;
    }
    if (tmp < 1.0)
    {
        max_magnitude = 1.0;
    }
    else
    {
        max_magnitude = 10.0;
        while (tmp / 10.0 > 1.0)
        {
            tmp = tmp / 10.0;
            max_magnitude = max_magnitude * 10.0;
        }
    }
    if (max_magnitude > gui_traffic_graph.io_max * 5.0)
    {
        max_magnitude = max_magnitude / 5.0;
    }
    max_bw = max_magnitude;

    if (gui_traffic_graph.meta_max < max_m)
    {
        gui_traffic_graph.meta_max = max_m;
    }
    else
    {
        gui_traffic_graph.meta_max = 0.8 * gui_traffic_graph.meta_max +
            0.2 * max_m;
        tmp = gui_traffic_graph.meta_max;
    }
    if (tmp < 1.0)
    {
        max_magnitude = 1.0;
    }
    else
    {
        max_magnitude = 10.0;
        while (tmp / 10.0 > 1.0)
        {
            tmp = tmp / 10.0;
            max_magnitude = max_magnitude * 10.0;
        }
    }
    if (max_magnitude > gui_traffic_graph.meta_max * 5.0)
    {
        max_magnitude = max_magnitude / 5.0;
    }
    max_m = max_magnitude;

    /* update labels on x-axis (top and bottom) */
    io_string = malloc(64);
    meta_string = malloc(64);
    for (i = 0; i < GUI_TRAFFIC_GRAPH_NR_TICS; i++)
    {
        snprintf(io_string, 64, "%.2f",
                 (max_bw * (float) i) / (float) (GUI_TRAFFIC_GRAPH_NR_TICS -
                                                 1));
        snprintf(meta_string, 64, "%.2f",
                 (max_m * (float) i) / (float) (GUI_TRAFFIC_GRAPH_NR_TICS - 1));
        gtk_label_set_text(GTK_LABEL(gui_traffic_graph.xaxis_io_label[i]),
                           io_string);
        gtk_label_set_text(GTK_LABEL(gui_traffic_graph.xaxis_meta_label[i]),
                           meta_string);
    }
    free(io_string);
    free(meta_string);

    /* draw in top and bottom tic marks */
    gdk_draw_line(gui_traffic_graph.pixmap,
                  gui_traffic_graph.drawing_area->style->black_gc,
                  5, graphoffset, 5, graphheight + graphoffset);
    gdk_draw_line(gui_traffic_graph.pixmap,
                  gui_traffic_graph.drawing_area->style->black_gc,
                  width - 6, graphoffset, width - 6, graphheight + graphoffset);

    for (i = 0; i < GUI_TRAFFIC_GRAPH_NR_TICS; i++)
    {
        int ypos = (i * graphheight) / (GUI_TRAFFIC_GRAPH_NR_TICS - 1) +
            graphoffset;

        gdk_draw_line(gui_traffic_graph.pixmap,
                      gui_traffic_graph.drawing_area->style->black_gc,
                      0, ypos, 10, ypos);
        gdk_draw_line(gui_traffic_graph.pixmap,
                      gui_traffic_graph.drawing_area->style->black_gc,
                      width - 11, ypos, width - 1, ypos);
    }

    /* redraw rectangles that are the bars themselves */
    for (i = 0; i < gui_traffic_graph.svr_ct; i++)
    {
        int rlen = (int) (((float) graphheight) *
                          (gui_traffic_graph.read[i] / max_bw));
        int wlen = (int) (((float) graphheight) *
                          (gui_traffic_graph.write[i] / max_bw));
        int mrlen = (int) (((float) graphheight) *
                           (gui_traffic_graph.rmeta[i] / max_m));
        int mwlen = (int) (((float) graphheight) *
                           (gui_traffic_graph.wmeta[i] / max_m));
        int xstart = i * svrspace + leftspace;

        /* aesthetics: always draw something */
        if (rlen == 0)
            rlen = 1;
        if (wlen == 0)
            wlen = 1;
        if (mrlen == 0)
            mrlen = 1;
        if (mwlen == 0)
            mwlen = 1;

        gdk_draw_rectangle(gui_traffic_graph.pixmap,
                           gui_traffic_graph.orange_gc,
                           TRUE,
                           xstart,
                           graphoffset + graphheight - rlen, barwidth, rlen);

        gdk_draw_rectangle(gui_traffic_graph.pixmap,
                           gui_traffic_graph.blue_gc,
                           TRUE,
                           xstart + barwidth,
                           graphoffset + graphheight - wlen, barwidth, wlen);

        gdk_draw_rectangle(gui_traffic_graph.pixmap,
                           gui_traffic_graph.green_gc,
                           TRUE,
                           xstart + 2 * barwidth,
                           graphoffset + graphheight - mrlen, barwidth, mrlen);

        gdk_draw_rectangle(gui_traffic_graph.pixmap,
                           gui_traffic_graph.purple_gc,
                           TRUE,
                           xstart + 3 * barwidth,
                           graphoffset + graphheight - mwlen, barwidth, mwlen);
    }

    /* force a redraw */
    gtk_widget_draw(gui_traffic_graph.drawing_area, &update_rect);
}

/******* internal callback functions *******/

static gint gui_traffic_graph_configure_callback(
    GtkWidget * drawing_area,
    GdkEvent * event,
    gpointer nullptr)
{
    assert(drawing_area == gui_traffic_graph.drawing_area);

    if (gui_traffic_graph.pixmap != NULL)
    {
        g_object_unref(gui_traffic_graph.pixmap);
        g_object_unref(gui_traffic_graph.green_gc);
        g_object_unref(gui_traffic_graph.blue_gc);
        g_object_unref(gui_traffic_graph.yellow_gc);
        g_object_unref(gui_traffic_graph.orange_gc);
        g_object_unref(gui_traffic_graph.purple_gc);
    }

    gui_traffic_graph.pixmap = gdk_pixmap_new(drawing_area->window,
                                              drawing_area->allocation.width,
                                              drawing_area->allocation.height,
                                              -1);

    /* create some graphics contexts for our colors */
    gui_traffic_graph.green_gc = gui_get_new_fg_color_gc(drawing_area,
                                                         0, 255, 0);
    gui_traffic_graph.blue_gc = gui_get_new_fg_color_gc(drawing_area,
                                                        0, 0, 255);
    gui_traffic_graph.yellow_gc = gui_get_new_fg_color_gc(drawing_area,
                                                          255, 255, 0);
    /* stock orange is 255, 165, 0 */
    gui_traffic_graph.orange_gc = gui_get_new_fg_color_gc(drawing_area,
                                                          255, 135, 0);
    gui_traffic_graph.purple_gc = gui_get_new_fg_color_gc(drawing_area,
                                                          160, 32, 240);

    gdk_draw_rectangle(gui_traffic_graph.pixmap,
                       gui_traffic_graph.drawing_area->style->white_gc,
                       TRUE,
                       0, 0,
                       drawing_area->allocation.width,
                       drawing_area->allocation.height);

    gui_traffic_graph_draw();

    gui_traffic_graph_configured = 1;

    return 1;
}

static gint gui_traffic_graph_expose_callback(
    GtkWidget * drawing_area,
    GdkEventExpose * event,
    gpointer nullptr)
{
    assert(drawing_area == gui_traffic_graph.drawing_area);

    gdk_draw_pixmap(drawing_area->window,
                    drawing_area->style->fg_gc[GTK_WIDGET_STATE(drawing_area)],
                    gui_traffic_graph.pixmap,
                    event->area.x, event->area.y,
                    event->area.x, event->area.y,
                    event->area.width, event->area.height);

    return 0;
}

/* NOTE on visibility callback:
 *
 * What I was trying to do was find a way to tell if the traffic page was
 * visible or not, so that I could do perf grabbing only while it was
 * visible.  This didn't work, but I'm keeping this code here for the
 * moment in case I figure that out.
 */
#if 0
static gint gui_traffic_graph_visibility_callback(
    GtkWidget * widget,
    GdkEventVisibility * event,
    gpointer nullptr)
{
    printf("visibility event; %s\n",
           (event->state == GDK_VISIBILITY_FULLY_OBSCURED) ? "obscured" :
           "visible");

    return 0;
}
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
