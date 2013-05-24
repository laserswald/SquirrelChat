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
 * A PARTICULAR PRUPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "irc_network.h"
#include "net_io.h"

// TODO: Add support for splitting messages over 512 chars
void send_privmsg(struct irc_network * network,
                  char * recepient,
                  char * msg) {
    send_to_network(network, "PRIVMSG %s :%s\r\n",
                    recepient, msg);
}

// vim: expandtab:tw=80:tabstop=4:shiftwidth=4:softtabstop=4