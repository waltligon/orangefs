/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <gtk/gtk.h>

#include "karma.h"

static int gui_message_initialized = 0;
static GtkTextBuffer *messagebuffer;
static GtkTextIter iter;

/* gui_message_setup()
 *
 * Creates the message window.
 *
 * Returns pointer to the message window widget.
 */
GtkWidget *gui_message_setup(
    void)
{
    GtkWidget *messageframe;
    GtkWidget *scrollwindow;
    GtkWidget *textview;

    messageframe = gtk_frame_new("Messages");

    gtk_frame_set_label_align(GTK_FRAME(messageframe), 0.0, 0.0);

    scrollwindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(messageframe), scrollwindow);

    textview = gtk_text_view_new();
    messagebuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
    gtk_text_buffer_get_iter_at_offset(messagebuffer, &iter, 0);

    gtk_container_add(GTK_CONTAINER(scrollwindow), textview);

    gui_message_initialized = 1;

    return messageframe;
}

/* gui_message_new(message)
 *
 * Places a new message in the message window.
 */
void gui_message_new(
    char *message)
{
    if (gui_message_initialized)
    {
        gtk_text_buffer_insert(messagebuffer, &iter, message, -1);
    }

    /* drop messages sent before initialization for now */
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
