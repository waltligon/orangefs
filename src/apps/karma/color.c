/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <gtk/gtk.h>

#include "karma.h"

/* gui_get_new_fg_color_gc()
 *
 * Colors are from 0 to 255.
 */
GdkGC *gui_get_new_fg_color_gc(
    GtkWidget * drawing_area,
    gint red,
    gint green,
    gint blue)
{
    GdkGC *gc;
    GdkColor *color;

    assert(red >= 0 && red <= 255 &&
           green >= 0 && green <= 255 && blue >= 0 && blue <= 255);

    color = (GdkColor *) malloc(sizeof(GdkColor));
    color->red = red * (65535 / 255);
    color->green = green * (65535 / 255);
    color->blue = blue * (65535 / 255);
    color->pixel = (gulong) (red * 65536 + green * 256 + blue);

    gc = gdk_gc_new(drawing_area->window);
    gdk_color_alloc(gtk_widget_get_colormap(drawing_area), color);
    gdk_gc_set_foreground(gc, color);

    return gc;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
