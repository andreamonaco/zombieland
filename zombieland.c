/*  Copyright (C) 2025 Andrea Monaco
 *
 *  This file is part of zombieland, an MMO game.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */



#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "zombieland.h"



void
send_message (int sockfd, struct sockaddr_in *addr, uint16_t portoff,
	      uint32_t type, ...)
{
  struct message msg = {0};
  va_list valist;

  if (portoff != (uint16_t)-1)
    addr->sin_port = htons (ZOMBIELAND_PORT+portoff);

  msg.type = htonl (type);

  switch (type)
    {
    case MSG_CLIENT_CHAR_STATE:
      va_start (valist, type);
      msg.args.client_char_state.id = va_arg (valist, uint32_t);
      msg.args.client_char_state.frame_counter = va_arg (valist, uint32_t);
      msg.args.client_char_state.char_speed_x = va_arg (valist, int32_t);
      msg.args.client_char_state.char_speed_y = va_arg (valist, int32_t);
      msg.args.client_char_state.char_facing = va_arg (valist, enum facing);
      va_end (valist);
      break;
    case MSG_SERVER_STATE:
      va_start (valist, type);
      msg.args.server_state.frame_counter = va_arg (valist, uint32_t);
      msg.args.server_state.x = va_arg (valist, uint32_t);
      msg.args.server_state.y = va_arg (valist, uint32_t);
      msg.args.server_state.w = va_arg (valist, uint32_t);
      msg.args.server_state.h = va_arg (valist, uint32_t);
      msg.args.server_state.char_facing = va_arg (valist, enum facing);
      va_end (valist);
      break;
    }

  if (sendto (sockfd, (char *)&msg, sizeof (msg), 0, (struct sockaddr *) addr,
	      sizeof (*addr)) < 0)
    {
      fprintf (stderr, "could not send data\n");
      exit (1);
    }
}
