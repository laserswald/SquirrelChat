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
#include "cmd_responses.h"
#include "irc_numerics.h"
#include "errors.h"
#include "message_parser.h"
#include "net_io.h"
#include "ui/user_list.h"
#include "ui/network_tree.h"

#include <errno.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>

sqchat_trie * isupport_tokens;
#define ISUPPORT_CHANTYPES      1
#define ISUPPORT_EXCEPTS        2
#define ISUPPORT_INVEX          3
#define ISUPPORT_CHANMODES      4
#define ISUPPORT_PREFIX         5
#define ISUPPORT_NETWORK        6
#define ISUPPORT_ELIST          7
#define ISUPPORT_CALLERID       8
#define ISUPPORT_CASEMAPPING    9

void sqchat_init_numerics() {
    isupport_tokens = sqchat_trie_new(sqchat_trie_strtoupper);
    sqchat_trie_set(isupport_tokens, "CHANTYPES",  (void*)ISUPPORT_CHANTYPES);
    sqchat_trie_set(isupport_tokens, "EXCEPTS",    (void*)ISUPPORT_EXCEPTS);
    sqchat_trie_set(isupport_tokens, "INVEX",      (void*)ISUPPORT_INVEX);
    sqchat_trie_set(isupport_tokens, "CHANMODES",  (void*)ISUPPORT_CHANMODES);
    sqchat_trie_set(isupport_tokens, "PREFIX",     (void*)ISUPPORT_PREFIX);
    sqchat_trie_set(isupport_tokens, "NETWORK",    (void*)ISUPPORT_NETWORK);
    sqchat_trie_set(isupport_tokens, "ELIST",      (void*)ISUPPORT_ELIST);
    sqchat_trie_set(isupport_tokens, "CALLERID",   (void*)ISUPPORT_CALLERID);
    sqchat_trie_set(isupport_tokens, "ACCEPT",     (void*)ISUPPORT_CALLERID);
    sqchat_trie_set(isupport_tokens, "CASEMAPPING",(void*)ISUPPORT_CASEMAPPING);
}

#define NUMERIC_CB(func_name)                               \
    short func_name(struct sqchat_network * network,        \
                    char * hostmask,                        \
                    short argc,                             \
                    char * argv[])

// Used for numerics that just give us a message requiring no special handling
NUMERIC_CB(sqchat_echo_argv_1) {
    sqchat_buffer_print(network->buffer, "%s\n", argv[1]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_myinfo) {
    if (argc < 5)
        return SQCHAT_MSG_ERR_ARGS;
    else {
        network->server_name = strdup(argv[1]);
        network->version = strdup(argv[2]);
        network->usermodes = strdup(argv[3]);
        network->chanmodes = strdup(argv[4]);
        return 0;
    }
}

NUMERIC_CB(sqchat_rpl_isupport) {
    for (short i = 1; i < argc; i++) {
        char * saveptr;
        char * saveptr2;
        char * token = strtok_r(argv[i], "=", &saveptr);
        char * value = strtok_r(NULL, "=", &saveptr);
        switch ((int)sqchat_trie_get(isupport_tokens, token)) {
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
                    sqchat_buffer_print(network->buffer,
                                        "Error parsing message: PREFIX "
                                        "parameters are invalid, server "
                                        "provided: %s\n",
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
                        gtk_tree_row_reference_get_model(network->buffer->row);

                    gtk_tree_model_get_iter(network_model, &network_row,
                            gtk_tree_row_reference_get_path(network->buffer->row));

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
                    network->casemap_upper = sqchat_trie_rfc1459_strtoupper;
                    network->casemap_lower = sqchat_trie_rfc1459_strtolower;
                    network->casecmp = sqchat_rfc1459_strcasecmp;
                }
                else if (strcmp(value, "ascii") == 0) {
                    network->casemap_upper = sqchat_trie_strtoupper;
                    network->casemap_lower = sqchat_trie_strtolower;
                    network->casecmp = strcasecmp;
                }
                else {
                    sqchat_buffer_print(network->buffer,
                                    "WARNING: Unknown casemap \"%s\" specified "
                                    "by network. Defaulting to rfc1459.",
                                    value);
                    network->casemap_upper = sqchat_trie_rfc1459_strtoupper;
                    network->casemap_lower = sqchat_trie_rfc1459_strtolower;
                }
                break;
        }
    }
    return 0;
}

NUMERIC_CB(sqchat_rpl_namreply) {
    struct sqchat_buffer * channel;
    // The first and second parameter aren't important

    // Check if we're in the channel
    if ((channel = sqchat_trie_get(network->buffers, argv[2])) != NULL) {
        // Add every single person in the reply to the list
        char * saveptr;
        for (char * nick = strtok_r(argv[3], " ", &saveptr);
             nick != NULL;
             nick = strtok_r(NULL, " ", &saveptr)) {
            char * prefix;
            GtkTreeIter user;

            // Check if the user has a user prefix
            if (strchr(network->prefix_symbols, nick[0]) != NULL) {
                prefix = nick;
                if (network->multi_prefix)
                    for (nick++;
                         strchr(network->prefix_symbols, nick[0]) != NULL;
                         nick++);
                else
                    nick++;
            }

            // Check to see if we already have the user in the list
            if (sqchat_user_list_user_row_find(channel, nick, &user) != -1)
                /* Since this will really only happen on non-multi-prefix
                 * networks, we don't need to worry about updating the user's
                 * full prefix string
                 */
                sqchat_user_list_user_set_visible_prefix(channel,
                                                         &user,
                                                         prefix ? *prefix : '\0');
            else
                sqchat_user_list_user_add(channel, nick, prefix,
                                 (size_t)(nick - prefix));
        }
    }
    return 0;
    // TODO: Print results to current buffer if we're not in the channel
}

NUMERIC_CB(sqchat_rpl_endofnames) {
    // TODO: Do something here
    return 0;
}

NUMERIC_CB(sqchat_rpl_motdstart) {
    // Check if the motd was requested in a different window

    sqchat_buffer_print((network->claimed_responses) ?
                        network->claimed_responses->buffer :
                        network->buffer,
                        "---Start of MOTD---\n");
    return 0;
}

NUMERIC_CB(sqchat_rpl_motd) {
    sqchat_buffer_print((network->claimed_responses) ?
                        network->claimed_responses->buffer :
                        network->buffer,
                        "%s\n", argv[1]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_endofmotd) {
    if (network->claimed_responses == NULL)
        sqchat_buffer_print(network->buffer, "---End of MOTD---\n");
    else {
        sqchat_buffer_print(network->claimed_responses->buffer,
                            "---End of MOTD---\n");
        sqchat_remove_last_response_claim(network);
    }
    return 0;
}

NUMERIC_CB(sqchat_rpl_topic) {
    if (argc < 3)
        return SQCHAT_MSG_ERR_ARGS;

    struct sqchat_buffer * output;

    // Check if the topic was requested in a different window
    if (network->claimed_responses != NULL)
        output = network->claimed_responses->buffer;
    else if ((output = sqchat_trie_get(network->buffers, argv[1])) == NULL)
        output = network->buffer;

    sqchat_buffer_print(output, "* Topic for %s is \"%s\"\n", argv[1], argv[2]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_notopic) {
    if (argc < 2)
        return SQCHAT_MSG_ERR_ARGS;

    struct sqchat_buffer * output;
    if (network->claimed_responses != NULL) {
        output = network->claimed_responses->buffer;
        sqchat_remove_last_response_claim(network);
    }
    else if ((output = sqchat_trie_get(network->buffers, argv[1])) == NULL)
        output = network->buffer;

    sqchat_buffer_print(output, "* No topic set for %s\n", argv[1]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_topicwhotime) {
    if (argc < 3)
        return SQCHAT_MSG_ERR_ARGS;

    struct sqchat_buffer * output;
    char * nickname;
    char * address;

    // Check if the response was requested in another buffer
    if (network->claimed_responses != NULL) {
        // RPL_TOPICWHOTIME is the last response for /topic
        output = network->claimed_responses->buffer;
        sqchat_remove_last_response_claim(network);
    }
    else if ((output = sqchat_trie_get(network->buffers, argv[1])) == NULL)
        output = network->buffer;

    sqchat_split_hostmask(argv[2], &nickname, &address);

    sqchat_buffer_print(output, "* Set by %s (%s)\n", nickname, address);
    return 0;
}

NUMERIC_CB(sqchat_rpl_channelmodeis) {
    if (argc < 3)
        return SQCHAT_MSG_ERR_ARGS;

    struct sqchat_buffer * output;
    /* Check if the response was requested in another channel
     * We don't remove the claimed response since most networks will follow up
     * with a RPL_CREATIONTIME response
     */
    if (network->claimed_responses)
        output = network->claimed_responses->buffer;
    else if ((output = sqchat_trie_get(network->buffers, argv[1])) == NULL)
        output = network->buffer;

    sqchat_buffer_print(output, "The modes for %s are: %s\r\n",
                        argv[1], argv[2]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_creationtime) {
    if (argc < 3)
        return SQCHAT_MSG_ERR_ARGS;

    struct sqchat_buffer * output;
    unsigned long epoch_time;

    if (network->claimed_responses) {
        output = network->claimed_responses->buffer;
        sqchat_remove_last_response_claim(network);
    }
    else if ((output = sqchat_trie_get(network->buffers, argv[1])) == NULL)
        output = network->buffer;

    errno = 0;
    if ((epoch_time = strtoul(argv[2], NULL, 10)) == 0 &&
        errno != 0) {
        sqchat_buffer_print(network->buffer,
                            "Error parsing message: Received invalid EPOCH "
                            "time in RPL_CREATIONTIME.\n"
                            "strtoul() returned the following error: %s\n",
                            strerror(errno));
        return SQCHAT_MSG_ERR_MISC;
    }

    sqchat_buffer_print(output, "* Channel created on %s",
                        ctime((const long *)&epoch_time));
    return 0;
}

NUMERIC_CB(sqchat_rpl_whoisuser) {
    if (argc < 6)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl(network),
                        "[%s] address is: %s@%s\n"
                        "[%s] real name is: %s\n",
                        argv[1], argv[2], argv[3], argv[1], argv[5]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_whoisserver) {
    if (argc < 3)
        return SQCHAT_MSG_ERR_ARGS;

    struct sqchat_buffer * output = sqchat_route_rpl(network);

    if (argc == 3)
        sqchat_buffer_print(output, "[%s] connected to %s\n", argv[1],
                            argv[2]);
    else
        sqchat_buffer_print(output, "[%s] connected to %s: %s\n", argv[1],
                            argv[2], argv[3]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_whoisoperator) {
    if (argc < 2)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl(network), "[%s] %s\n", argv[1], argv[2]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_whoisidle) {
    if (argc < 2)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl(network),
                        "[%s] has been idle for %s seconds\n",
                        argv[1], argv[2]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_whoischannels) {
    if (argc < 3)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl(network),
                        "[%s] channels: %s\n", argv[1], argv[2]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_whoissecure) {
    sqchat_buffer_print(sqchat_route_rpl(network),
                        "[%s] %s\n", argv[1], argv[2]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_whoisaccount) {
    if (argc < 4)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl(network), "[%s] %s %s\n",
                        argv[1], argv[3], argv[2]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_whoisactually) {
    if (argc < 4)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl(network), "[%s] %s %s\n",
                        argv[1], argv[3], argv[2]);
    return 0;
}

// Used for all whois replies that pretty much just echo back their arguments
NUMERIC_CB(sqchat_rpl_whois_generic) {
    if (argc < 3)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl(network), "[%s] %s\n",
                        argv[1], argv[2]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_endofwhois) {
    if (argc < 2)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl_end(network),
                        "[%s] End of WHOIS.\n", argv[1]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_whowasuser) {
    if (argc < 6)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl(network),
                        "[%s] address was: %s@%s\n"
                        "[%s] real name was: %s\n",
                        argv[1], argv[2], argv[3], argv[1], argv[5]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_endofwhowas) {
    if (argc < 2)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl_end(network),
                        "[%s] End of WHOWAS.\n", argv[1]);
    return 0;
}

// Used for generic errors with only an error message
NUMERIC_CB(sqchat_generic_error) {
    struct sqchat_buffer * output = sqchat_route_rpl_end(network);

    sqchat_buffer_print(output, "Error: %s\n", argv[1]);
    return 0;
}

// Used for errors that can potentially affect the status of the connection
NUMERIC_CB(sqchat_generic_network_error) {
    sqchat_buffer_print(network->buffer, "Error: %s\n", argv[1]);
    return 0;
}

// Used for generic errors that come with a channel argument
NUMERIC_CB(sqchat_generic_channel_error) {
    if (argc < 3)
        return SQCHAT_MSG_ERR_ARGS;

    struct sqchat_buffer * output;
    if (network->claimed_responses) {
        output = network->claimed_responses->buffer;
        sqchat_remove_last_response_claim(network);
    }
    else if ((output = sqchat_trie_get(network->buffers, argv[1])) == NULL)
        output = network->buffer;

    sqchat_buffer_print(output, "Error: %s: %s\n", argv[1], argv[2]);
    return 0;
}

// Used for generic errors that come with a command argument
NUMERIC_CB(sqchat_generic_command_error) {
    if (argc < 3)
        return SQCHAT_MSG_ERR_ARGS;

    struct sqchat_buffer * output = sqchat_route_rpl_end(network);

    sqchat_buffer_print(output, "Error: %s: %s\n", argv[1], argv[2]);
    return 0;
}

// Used for errors with a single non-channel argument
NUMERIC_CB(sqchat_generic_target_error) {
    if (argc < 3)
        return SQCHAT_MSG_ERR_ARGS;

    struct sqchat_buffer * output = sqchat_route_rpl_end(network);

    sqchat_buffer_print(output, "Error: %s: %s\n", argv[1], argv[2]);
    return 0;
}

// Used for errors with a user argument and a channel argument
NUMERIC_CB(sqchat_generic_user_channel_error) {
    if (argc < 4)
        return SQCHAT_MSG_ERR_ARGS;

    struct sqchat_buffer * output = sqchat_route_rpl_end(network);

    sqchat_buffer_print(output, "Error: %s with %s: %s\n",
                        argv[1], argv[0], argv[2]);
    return 0;
}

NUMERIC_CB(sqchat_generic_lusers_rpl) {
    if (argc < 3)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl(network), "%s %s\n", argv[1], argv[2]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_localglobalusers) {
    /* There's no standard for which argument contains the string we need, so we
     * have to just assume it's the last argument (which it should be)
     */
    sqchat_buffer_print(sqchat_route_rpl(network), "%s\n", argv[argc-1]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_inviting) {
    if (argc < 3)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl_end(network),
                        "* Invitation for %s to join %s was successfully sent.\n",
                        argv[1], argv[2]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_time) {
    if (argc < 3)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl_end(network),
                        "The local time for %s is: %s\n", argv[1], argv[2]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_version) {
    if (argc < 4)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl_end(network),
                        "The server %s is running %s: %s\n",
                        argv[2], argv[1], argv[3]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_info) {
    if (argc < 2)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl(network), "* %s\n", argv[1]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_endofinfo) {
    sqchat_buffer_print(sqchat_route_rpl_end(network), "--- End of INFO ---\n");
    return 0;
}

NUMERIC_CB(sqchat_rpl_nowaway) {
    if (argc < 2)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl_end(network), "* %s\n", argv[1]);
    network->away = true;
    return 0;
}

NUMERIC_CB(sqchat_rpl_unaway) {
    if (argc < 2)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl_end(network), "* %s\n", argv[1]);
    network->away = false;
    return 0;
}

// TODO: Add polling and all that good stuff for when a user becomes away
NUMERIC_CB(sqchat_rpl_away) {
    if (argc < 3)
        return SQCHAT_MSG_ERR_ARGS;

    struct sqchat_buffer * buffer;
    // If we don't have a buffer open with the user, create one
    if ((buffer = sqchat_trie_get(network->buffers, argv[1])) == NULL) {
        buffer = sqchat_buffer_new(argv[1], QUERY, network);
        sqchat_network_tree_buffer_add(buffer, network);
        buffer->query_data->away_msg = strdup(argv[2]);
        sqchat_buffer_print(buffer, "[%s is away: %s]\n", argv[1], argv[2]);
    }
    else if (strcmp(buffer->query_data->away_msg, argv[2]) != 0) {
        free(buffer->query_data->away_msg);
        buffer->query_data->away_msg = strdup(argv[2]);
        sqchat_buffer_print(buffer, "[%s is away: %s]\n", argv[1], argv[2]);
    }
    return 0;
}

NUMERIC_CB(sqchat_rpl_whoreply) {
    if (argc < 8)
        return SQCHAT_MSG_ERR_ARGS;

    // TODO: Check if the claim was made by the client, or the user
    if (network->claimed_responses)
        sqchat_buffer_print(network->claimed_responses->buffer,
                            "[WHO %s] %s: %s%s (%s@%s) on %s: %s\n",
                            argv[1], (argv[6][0] == 'H') ? "Here" : "Gone",
                            (argv[6][1]) ? &argv[6][1] : "",
                            argv[5], argv[2], argv[3], argv[4], argv[7]);
    // TODO:Add an else here.
    return 0;
}

NUMERIC_CB(sqchat_rpl_endofwho) {
    if (argc < 2)
        return SQCHAT_MSG_ERR_ARGS;

    if (SQCHAT_IS_CHAN(network, argv[1]))
            sqchat_buffer_print(sqchat_route_rpl_end(network),
                                "--- End of WHO for %s ---\n", argv[1]);
    return 0;
}

// TODO: Convert the RPL_LINKS input into a tree
NUMERIC_CB(sqchat_rpl_links) {
    if (argc < 4)
        return SQCHAT_MSG_ERR_ARGS;
    
    sqchat_buffer_print(sqchat_route_rpl(network),
                        "* %s %s :%s\n", argv[1], argv[2], argv[3]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_endoflinks) {
    if (argc < 2)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl_end(network),
                        "--- End of LINKS for %s ---\n",
                        (argv[1][0] == '*') ? network->server_name : argv[1]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_liststart) {
    if (argc < 3)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl(network),
                        "* %s %s\n", argv[1], argv[2]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_list) {
    if (argc < 3)
        return SQCHAT_MSG_ERR_ARGS;

    if (strcmp(argv[1], "Prv") == 0)
        sqchat_buffer_print(sqchat_route_rpl(network),
                            "* <Private> %s\n", argv[2]);
    else {
        if (argc < 4)
            return SQCHAT_MSG_ERR_ARGS;
        sqchat_buffer_print(sqchat_route_rpl(network),
                            "* %s %s \"%s\"\n",
                            argv[1], argv[2], argv[3]);
    }
    return 0;
}

NUMERIC_CB(sqchat_rpl_listend) {
    sqchat_buffer_print(sqchat_route_rpl_end(network),
                        "--- End of channel listing ---\n");
    return 0;
}

NUMERIC_CB(sqchat_rpl_hosthidden) {
    if (argc < 3)
        return SQCHAT_MSG_ERR_ARGS;
    sqchat_buffer_print(network->buffer, "%s %s\n", argv[1], argv[2]);
    return 0;
}

NUMERIC_CB(sqchat_generic_rpl_trace) {
    if (argc < 4)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl(network),
                        "%s %s %s\n",
                        argv[1], argv[2], argv[3]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_traceoperator) {
    if (argc < 4)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl(network),
                        "%s is logged in as an operator\n",
                        argv[3]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_traceuser) {
    if (argc < 4)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl(network),
                        "%s is logged in as a normal "
                        "user\n",
                        argv[3]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_tracelink) {
    if (argc < 5)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl(network), "%s %s %s %s\n",
                        argv[1], argv[2], argv[3], argv[4]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_traceserver) {
    if (argc < 7)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl(network), "%s %s %s %s %s %s\n",
                        argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]);

    return 0;
}

NUMERIC_CB(sqchat_rpl_traceservice) {
    if (argc < 5)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl(network), "%s %s %s %s\n",
                        argv[1], argv[2], argv[3], argv[4]);
    return 0;
}

NUMERIC_CB(sqchat_rpl_traceend) {
    sqchat_buffer_print(sqchat_route_rpl_end(network),
                        "--- End of TRACE ---\n");
    return 0;
}

NUMERIC_CB(sqchat_rpl_snomask) {
    if (argc < 3)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(network->buffer, "%s %s\r\n", argv[2], argv[1]);
    return 0;
}

NUMERIC_CB(sqchat_generic_echo_rpl) {
    if (argc < 2)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl(network), "%s\n", argv[1]);
    return 0;
}

NUMERIC_CB(sqchat_generic_echo_rpl_end) {
    if (argc < 2)
        return SQCHAT_MSG_ERR_ARGS;

    sqchat_buffer_print(sqchat_route_rpl_end(network), "%s\n", argv[1]);
    return 0;
}

NUMERIC_CB(sqchat_nick_change_error) {
    if (network->claimed_responses == NULL)
        return 0;

    sqchat_buffer_print(network->claimed_responses->buffer,
                        "Could not change nickname to %s: %s\n",
                        (char*)network->claimed_responses->data, argv[2]);
    sqchat_remove_last_response_claim(network);
    return 0;
}
// vim: set expandtab tw=80 shiftwidth=4 softtabstop=4 cinoptions=(0,W4:
