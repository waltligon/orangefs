/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <gtk/gtk.h>
#include <stdio.h>

#include "karma.h"

static void gui_fsview_response(
    GtkDialog * dialog,
    gint response_id,
    gpointer user_data);
void gui_fsview_popup(
    void)
{
    GtkWidget *fsview;
    GtkTreeViewColumn *col;
    GtkCellRenderer *renderer;
    GtkTreeSelection *selection;
    GtkWidget *dialog;

    fsview = gtk_tree_view_new();

    /* for each column, set title, add column, get renderer,
     * pack renderer, associate renderer with text
     */
    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col, "Mount Point");
    gtk_tree_view_append_column(GTK_TREE_VIEW(fsview), col);
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_add_attribute(col, renderer, "text", GUI_FSLIST_MNTPT);

    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col, "Contact Server");
    gtk_tree_view_append_column(GTK_TREE_VIEW(fsview), col);
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_add_attribute(col,
                                       renderer, "text", GUI_FSLIST_SERVER);

    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col, "File System Name");
    gtk_tree_view_append_column(GTK_TREE_VIEW(fsview), col);
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_add_attribute(col,
                                       renderer, "text", GUI_FSLIST_FSNAME);

    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col, "FSID");
    gtk_tree_view_append_column(GTK_TREE_VIEW(fsview), col);
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_add_attribute(col, renderer, "text", GUI_FSLIST_FSID);

    gtk_tree_view_set_model(GTK_TREE_VIEW(fsview),
                            GTK_TREE_MODEL(gui_comm_fslist));

    /* get a reference to the selection for the view, set so only one
     * thing may be selected.
     */
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(fsview));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);

    /* drop the view in a container and go to town */
    dialog = gtk_dialog_new_with_buttons("Select File System",
                                         GTK_WINDOW(main_window),
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_STOCK_OK,
                                         GTK_RESPONSE_OK,
                                         GTK_STOCK_CANCEL,
                                         GTK_RESPONSE_CANCEL, NULL);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), fsview);

    g_signal_connect(GTK_OBJECT(dialog),
                     "response", G_CALLBACK(gui_fsview_response), fsview);

    gtk_widget_show_all(dialog);

}

static void gui_fsview_response(
    GtkDialog * dialog,
    gint response_id,
    gpointer fsview)
{
    GtkTreeSelection *selection;
    GtkTreeIter iter;
    GtkTreeModel *model;

    switch (response_id)
    {
    case GTK_RESPONSE_OK:
        selection =
            gtk_tree_view_get_selection(GTK_TREE_VIEW((GtkTreeView *) fsview));

        if (gtk_tree_selection_get_selected(selection, &model /* output */ ,
                                            &iter))
        {
            gchar *server, *fsname;
            gint fsid;

            gtk_tree_model_get(model, &iter,
                               GUI_FSLIST_SERVER, &server,
                               GUI_FSLIST_FSNAME, &fsname,
                               GUI_FSLIST_FSID, &fsid, -1);

            gui_comm_set_active_fs(server, fsname, fsid);
            g_free(server);
            g_free(fsname);
        }

        /* fall through to destroy */
    case GTK_RESPONSE_CANCEL:
        gtk_widget_destroy(GTK_WIDGET(dialog));
        break;
    case GTK_RESPONSE_DELETE_EVENT:
    case GTK_RESPONSE_NONE:
        /* GTK_RESPONSE_NONE might indicated destroyed? */
        break;
    }
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
