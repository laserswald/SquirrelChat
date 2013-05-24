/* Functions for setting up the main menu bar in a chat window
 *
 * Copyright (C) 2013 Stephen Chandler Paul
 *
 * This file is free software: you may copy it, redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 2 of this License or (at your option) any
 * later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <gtk/gtk.h>
#include "chat_window.h"
#include "buffer.h"
#include "../irc_network.h"
#include "network_tree.h"
#include "main_menu_bar.h"

void create_main_menu_bar(struct chat_window * window) {
    /* Menu items and submenus that don't need to be referenced outside of this
     * scope
     */

    window->main_menu_bar = gtk_menu_bar_new();
    window->main_menu = gtk_menu_new();

    window->main_menu_item = gtk_menu_item_new_with_label("SquirrelChat");
    window->connect_menu_item = gtk_menu_item_new_with_label("Connect");
    window->new_server_buffer_menu_item =
        gtk_menu_item_new_with_label("New Network Tab");
    window->exit_menu_item = gtk_menu_item_new_with_label("Exit");

    window->help_menu = gtk_menu_new();
    window->help_menu_item = gtk_menu_item_new_with_label("Help");
    window->about_menu_item = gtk_menu_item_new_with_label("About SquirrelChat");

    // Build the menu structure
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(window->main_menu_item),
                              window->main_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->main_menu),
                          window->connect_menu_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->main_menu),
                          window->new_server_buffer_menu_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->main_menu),
                          window->exit_menu_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->main_menu_bar),
                          window->main_menu_item);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(window->help_menu_item),
                              window->help_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->help_menu),
                          window->about_menu_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->main_menu_bar),
                          window->help_menu_item);
}

void new_network_menu_item_callback(GtkMenuItem * menuitem,
                                          struct chat_window * window) {
    add_network(window, new_irc_network());
}

void about_menu_item_callback(GtkMenuItem * menuitem,
                              struct chat_window * window) {
    char * authors[] = { "Stephen Chandler Paul",
                         NULL };
    char * copyright = "Copyright ©2013 Stephen Chandler Paul\n"
                       "Copyright ©2013 SquirrelChat Developers\n"
                       "Some components ©2013 Alex Iadicicco";

    gtk_show_about_dialog(GTK_WINDOW(window->window),
                          "license-type", GTK_LICENSE_GPL_2_0,
                          "wrap-license", TRUE,
                          "authors", authors,
                          "copyright", copyright,
                          NULL);
}

// Callback used by the "Connect" menu item
void connect_current_network(GtkMenuItem * menuitem,
                             struct chat_window * window) {
    GtkTreeIter selected_row;
    struct irc_network * network;

    /* Find out if there's a row selected and if so, connect the associated
     * network
     */
    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(
                                        GTK_TREE_VIEW(window->network_tree)),
                                        &window->network_tree_store,
                                        &selected_row)) {
        // Get the network buffer for the selected row
        gtk_tree_model_get(GTK_TREE_MODEL(window->network_tree_store),
                           &selected_row, 1, &network, -1);

        connect_irc_network(network);
    }
}

// Connects all the signals for the items in the menu bar
void connect_main_menu_bar_signals(struct chat_window * window) {
    g_signal_connect(window->connect_menu_item, "activate",
                     G_CALLBACK(connect_current_network), window);
    g_signal_connect(window->new_server_buffer_menu_item, "activate",
                     G_CALLBACK(new_network_menu_item_callback), window);
    g_signal_connect(window->exit_menu_item, "activate", G_CALLBACK(gtk_main_quit),
                     NULL);

    g_signal_connect(window->about_menu_item, "activate",
                     G_CALLBACK(about_menu_item_callback), window);
}

// vim: expandtab:tw=80:tabstop=4:shiftwidth=4:softtabstop=4