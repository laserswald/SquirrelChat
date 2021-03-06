/*
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

#include "buffer.h"
#include "../irc_network.h"
#include "chat_window.h"
#include "../commands.h"
#include "user_list.h"
#include "buffer_view.h"
#include "command_box.h"

#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static gboolean flush_buffer_output(struct sqchat_buffer * buffer);

struct sqchat_buffer * sqchat_buffer_new(const char * buffer_name,
                                         enum sqchat_buffer_type type,
                                         struct sqchat_network * network) {
    struct sqchat_buffer * buffer = malloc(sizeof(struct sqchat_buffer));
    buffer->type = type;
    buffer->buffer_name = (type != NETWORK) ? strdup(buffer_name) : NULL;

    buffer->row = NULL;
    buffer->network = network;
    buffer->window = network->window;
    
    buffer->buffer = gtk_text_buffer_new(NULL);
    buffer->command_box_buffer = gtk_entry_buffer_new(NULL, -1);
    
    buffer->buffer_view = sqchat_buffer_view_new(buffer->buffer);
    buffer->command_box_entry =
        sqchat_command_box_new(buffer->command_box_buffer, buffer);

    buffer->scrolled_container = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(buffer->scrolled_container),
                      buffer->buffer_view);

    g_object_ref(buffer->command_box_entry);
    g_object_ref(buffer->scrolled_container);
    /* The textview for the buffer is already referenced by the scrolled
     * container, so we don't need to reference that
     */

    buffer->out_queue_size = 0;
    buffer->out_queue = NULL;
    g_mutex_init(&buffer->output_mutex);

    // Add a userlist if the buffer is a channel buffer
    if (type == CHANNEL) {
        buffer->chan_data = malloc(sizeof(struct __sqchat_channel_data));
        buffer->chan_data->user_list_store =
            gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
        buffer->chan_data->users = sqchat_trie_new(sqchat_trie_strtolower);
    }
    else if (type == QUERY) {
        buffer->query_data = malloc(sizeof(struct __sqchat_query_data));
        buffer->query_data->away_msg = NULL;
    }
    else
        buffer->extra_data = NULL;

    if (type != NETWORK)
        sqchat_trie_set(network->buffers, buffer_name, buffer);

    return buffer;
}

static void destroy_users(GtkTreeRowReference * user,
                          struct sqchat_buffer * buffer) {
    if (buffer->network->multi_prefix) {
        GtkTreeIter user_row;
        gtk_tree_model_get_iter(GTK_TREE_MODEL(buffer->chan_data->user_list_store),
                                &user_row,
                                gtk_tree_row_reference_get_path(user));
        free(sqchat_user_list_user_get_prefixes(buffer, &user_row));
    }
    gtk_tree_row_reference_free(user);
}

void sqchat_buffer_destroy(struct sqchat_buffer * buffer) {
    if (buffer->type != NETWORK)
        sqchat_trie_del(buffer->network->buffers, buffer->buffer_name);
    sqchat_buffer_free(buffer);
}

void sqchat_buffer_free(struct sqchat_buffer * buffer) {
    if (buffer->type == CHANNEL) {
        g_object_unref(buffer->chan_data->user_list_store);
        sqchat_trie_free(buffer->chan_data->users, destroy_users, buffer);
    }
    g_object_unref(buffer->scrolled_container);
    g_object_unref(buffer->command_box_entry);

    free(buffer->buffer_name);
    gtk_tree_row_reference_free(buffer->row);
    free(buffer->extra_data);

    g_mutex_clear(&buffer->output_mutex);

    /* If there was still data waiting to be outputted, destroy it and remove
     * idle function from the event loop
     */
    if (g_idle_remove_by_data(buffer)) {
        struct __sqchat_queued_output * n;
        for (struct __sqchat_queued_output * c = buffer->out_queue;
             c != NULL;
             c = n) {
            free(c->msg);
            n = c->next;
            free(c);
        }
    }

    free(buffer);
}

void sqchat_buffer_print(struct sqchat_buffer * buffer,
                         const char * msg, ...) {
    va_list args;
    size_t parsed_msg_len;
    struct __sqchat_queued_output * parsed_msg = malloc(sizeof(struct __sqchat_queued_output));

    parsed_msg->next = NULL;

    // Create the string
    va_start(args, msg);
    parsed_msg_len = vsnprintf(NULL, 0, msg, args);

    va_start(args, msg);
    parsed_msg->msg = malloc(parsed_msg_len + 1);
    vsprintf(parsed_msg->msg, msg, args);
    va_end(args);

    // Add the message to the end of the queue and update the end pointer
    g_mutex_lock(&buffer->output_mutex);
    if (buffer->out_queue == NULL) {
        g_idle_add((GSourceFunc)flush_buffer_output, buffer);
        buffer->out_queue = parsed_msg;
        buffer->out_queue_end = parsed_msg;
    }
    else {
        buffer->out_queue_end->next = parsed_msg;
        buffer->out_queue_end = parsed_msg;
    }
    buffer->out_queue_size += parsed_msg_len;
    parsed_msg->msg_len = parsed_msg_len;
    g_mutex_unlock(&buffer->output_mutex);
}

static gboolean flush_buffer_output(struct sqchat_buffer * buffer) {
    g_mutex_lock(&buffer->output_mutex);

    char output_dump[buffer->out_queue_size + 1];
    char * dump_pos = &output_dump[0];
    GtkTextIter end_of_buffer;
    GtkAdjustment * scroll_adjustment =
        gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(buffer->buffer_view));

    struct __sqchat_queued_output * n;
    // Concatenate all the messages in the queue and clear it
    for (struct __sqchat_queued_output * c = buffer->out_queue;
         c != NULL;
         c = n) {
        strcpy(dump_pos, c->msg);
        dump_pos += c->msg_len;
        n = c->next;
        free(c->msg);
        free(c);
    }

    // Figure out where the end of the buffer is
    gtk_text_buffer_get_end_iter(buffer->buffer, &end_of_buffer);

    /* If the user has manually scrolled, don't adjust the scroll position,
     * otherwise scroll to the bottom when printing the message
     */
    if (gtk_adjustment_get_value(scroll_adjustment) >=
        gtk_adjustment_get_upper(scroll_adjustment) -
        gtk_adjustment_get_page_size(scroll_adjustment) - 1e-12) {
        gtk_text_buffer_insert(buffer->buffer, &end_of_buffer, &output_dump[0],
                               buffer->out_queue_size);
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(buffer->buffer_view),
                                     gtk_text_buffer_get_mark(buffer->buffer,
                                                              "insert"),
                                     0.0, false, 0.0, 0.0);
    }
    else
        gtk_text_buffer_insert(buffer->buffer, &end_of_buffer, &output_dump[0],
                               buffer->out_queue_size);

    buffer->out_queue = NULL;
    buffer->out_queue_size = 0;
    g_mutex_unlock(&buffer->output_mutex);
    return false;
}

// vim: set expandtab tw=80 shiftwidth=4 softtabstop=4 cinoptions=(0,W4:
