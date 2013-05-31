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

#include "trie.h"
#include "commands.h"
#include "casemap.h"

#include <string.h>
#include <stdlib.h>

trie * isupport_tokens;
#define ISUPPORT_CHANTYPES      1
#define ISUPPORT_EXCEPTS        2
#define ISUPPORT_INVEX          3
#define ISUPPORT_CHANMODES      4
#define ISUPPORT_PREFIX         5
#define ISUPPORT_NETWORK        6
#define ISUPPORT_ELIST          7
#define ISUPPORT_CALLERID       8
#define ISUPPORT_CASEMAPPING    9

void init_numerics() {
    isupport_tokens = trie_new(trie_strtoupper);
    trie_set(isupport_tokens, "CHANTYPES",  (void*)ISUPPORT_CHANTYPES);
    trie_set(isupport_tokens, "EXCEPTS",    (void*)ISUPPORT_EXCEPTS);
    trie_set(isupport_tokens, "INVEX",      (void*)ISUPPORT_INVEX);
    trie_set(isupport_tokens, "CHANMODES",  (void*)ISUPPORT_CHANMODES);
    trie_set(isupport_tokens, "PREFIX",     (void*)ISUPPORT_PREFIX);
    trie_set(isupport_tokens, "NETWORK",    (void*)ISUPPORT_NETWORK);
    trie_set(isupport_tokens, "ELIST",      (void*)ISUPPORT_ELIST);
    trie_set(isupport_tokens, "CALLERID",   (void*)ISUPPORT_CALLERID);
    trie_set(isupport_tokens, "ACCEPT",     (void*)ISUPPORT_CALLERID);
    trie_set(isupport_tokens, "CASEMAPPING",(void*)ISUPPORT_CASEMAPPING);
}

void rpl_isupport(struct irc_network * network,
                  char * hostmask,          
                  short argc,
                  char * argv[],
                  char * trailing) {
    for (short i = 1; i < argc; i++) {
        char * saveptr;
        char * saveptr2;
        char * token = strtok_r(argv[i], "=", &saveptr);
        char * value = strtok_r(NULL, "=", &saveptr);
        switch ((int)trie_get(isupport_tokens, token)) {
            case ISUPPORT_CHANTYPES:
                free(network->chantypes);
                network->chantypes = strdup(value);
                break;
            case ISUPPORT_EXCEPTS:
                network->excepts = true;
                break;
            case ISUPPORT_INVEX:
                network->invex = true;
                break;
            case ISUPPORT_CHANMODES:
                network->chanmodes_a = strdup(strtok_r(value, ",", &saveptr2));
                network->chanmodes_b = strdup(strtok_r(NULL, ",", &saveptr2));
                network->chanmodes_c = strdup(strtok_r(NULL, ",", &saveptr2));
                network->chanmodes_d = strdup(strtok_r(NULL, ",", &saveptr2));
                break;
            case ISUPPORT_PREFIX:
                if (value[0] != '(') {
                    print_to_buffer(network->buffer,
                                    "Error parsing message: PREFIX parameters "
                                    "are invalid, server provided: %s\n",
                                    value);
                    continue;
                }
                // Eat the first (
                value++;
                
                network->prefix_chars = strdup(strtok_r(value, ")", &saveptr2));
                network->prefix_symbols = strdup(strtok_r(NULL, ")", &saveptr2));
                break;
            case ISUPPORT_NETWORK:
                free(network->name);
                network->name = strdup(value);
                
                // Update the network name in the network tree
                {
                    GtkTreeIter network_row;
                    GtkTreeModel * network_model = 
                        gtk_tree_row_reference_get_model(network->row);

                    gtk_tree_model_get_iter(network_model, &network_row,
                            gtk_tree_row_reference_get_path(network->row));

                    gtk_tree_store_set(GTK_TREE_STORE(network_model),
                                       &network_row, 0, network->name, -1);
                }
                //TODO: do this 
                break;
            case ISUPPORT_CALLERID:
                network->callerid = true;
                break;
            case ISUPPORT_CASEMAPPING:
                if (strcmp(value, "rfc1459") == 0) {
                    network->casemap_upper = trie_rfc1459_strtoupper;
                    network->casemap_lower = trie_rfc1459_strtolower;
                }
                else if (strcmp(value, "ascii") == 0) {
                    network->casemap_upper = trie_strtoupper;
                    network->casemap_lower = trie_strtolower;
                }
                else {
                    print_to_buffer(network->buffer,
                                    "WARNING: Unknown casemap \"%s\" specified "
                                    "by network. Defaulting to rfc1459.",
                                    value);
                    network->casemap_upper = trie_rfc1459_strtoupper;
                    network->casemap_lower = trie_rfc1459_strtolower;
                }
                break;
        }
    }
}

// vim: expandtab:tw=80:tabstop=4:shiftwidth=4:softtabstop=4