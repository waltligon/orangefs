/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <gtk/gtk.h>
#include "karma.h"

static void gui_menu_about_popup(
    void);

static GtkItemFactoryEntry menu_items[] = {
    {"/_File",
     NULL,
     NULL, 0,
     "<Branch>"},
    {"/File/_Select file system",
     "<CTRL>S",
     gui_fsview_popup, 0,
     "<Item>"},
#if 0
    {"/File/_Re-read Fstab",
     NULL,
     NULL, 0,
     "<Item>"},
#endif
    {"/File/_Quit",
     "<CTRL>Q",
     gtk_main_quit, 0,
     "<StockItem>",
     GTK_STOCK_QUIT},
#if 0
    {"/_Options",
     NULL,
     NULL, 0,
     "<Branch>"},
#endif
    {"/_Help",
     NULL,
     NULL, 0,
     "<LastBranch>"},
    {"/_Help/About",
     NULL,
     gui_menu_about_popup, 0,
     "<Item>"},
};

GtkWidget *main_window = NULL;

/* gui_menu_setup(window)
 *
 * Creates the menubar and attaches accelerators to the window passed in.
 *
 * Returns pointer to menu bar widget.
 */
GtkWidget *gui_menu_setup(
    GtkWidget * window)
{
    gint nmenu_items = sizeof(menu_items) / sizeof(menu_items[0]);

    GtkItemFactory *item_factory;
    GtkAccelGroup *accel_group;

    accel_group = gtk_accel_group_new();

    item_factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<main>",
                                        accel_group);

    /* This function generates the menu items. Pass the item factory,
       the number of items in the array, the array itself, and any
       callback data for the the menu items. */
    gtk_item_factory_create_items(item_factory, nmenu_items, menu_items, NULL);

    /* Attach the new accelerator group to the window. */
    gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);

    /* Save the main window widget so we can attach dialogs to it, etc. */
    main_window = window;

    /* Finally, return the actual menu bar created by the item factory. */
    return gtk_item_factory_get_widget(item_factory, "<main>");
}

static void gui_menu_about_popup(
    void)
{
    GtkWidget *dialog, *label;

    dialog = gtk_dialog_new_with_buttons("About Karma",
                                         GTK_WINDOW(main_window),
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_STOCK_OK, GTK_RESPONSE_NONE, NULL);
    label =
        gtk_label_new("Karma\nA Graphical Interface for Monitoring PVFS2\n");

    g_signal_connect_swapped(GTK_OBJECT(dialog),
                             "response",
                             G_CALLBACK(gtk_widget_destroy),
                             GTK_OBJECT(dialog));

    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), label);
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
