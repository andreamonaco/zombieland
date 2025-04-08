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



#define GRID_CELL_W 16
#define GRID_CELL_H 16

#define RECT_BY_GRID(x,y,w,h) {(x)*GRID_CELL_W, (y)*GRID_CELL_H, (w)*GRID_CELL_W, \
    (h)*GRID_CELL_H}


#define ZOMBIELAND_PORT 19894
#define FRAME_DURATION 33.333f   /* 30 hz */
#define CLIENT_TIMEOUT 1800
#define SERVER_TIMEOUT 1800
#define MAXMSGSIZE 512
#define MAX_LOGNAME_LEN 15

#define MAX_PLAYERS 128

#define MAX_ZOMBIES 10
#define SPAWN_INTERVAL 300

#define MAXTEXTSIZE 512
#define TEXTLINESIZE 30



enum
facing
  {
    FACING_DOWN,
    FACING_UP,
    FACING_RIGHT,
    FACING_LEFT,
  };



#define MSG_LOGIN              0
#define MSG_LOGINOK            1
#define MSG_LOGINFAIL          2
#define MSG_CLIENT_CHAR_STATE  3
#define MSG_SERVER_STATE       4
#define MSG_PLAYER_DIED        5
#define MSG_INTERACT           6


struct
login_args
{
  uint16_t portoff;
  char logname [MAX_LOGNAME_LEN+1];
};


struct
loginok_args
{
  uint32_t id;
};


struct
client_char_state_args
{
  uint32_t id;
  uint32_t frame_counter;
  int32_t char_speed_x, char_speed_y;
  enum facing char_facing;
  uint32_t do_interact;
  uint32_t do_shoot;
};


#define VISIBLE_PLAYER 0
#define VISIBLE_ZOMBIE 1
#define VISIBLE_SHOT   2

struct
visible
{
  uint32_t type;
  uint32_t x, y, w, h;
  enum facing facing;
  int32_t speed_x, speed_y;
};


struct
server_state_args
{
  uint32_t frame_counter;
  uint32_t areaid;
  uint32_t x, y, w, h;
  enum facing char_facing;
  int32_t life;
  uint32_t num_visibles;
  uint32_t textbox_lines_num;
};


union
args_union
{
  struct login_args login;
  struct loginok_args loginok;
  struct client_char_state_args client_char_state;
  struct server_state_args server_state;
};


struct
message
{
  uint32_t type;
  union args_union args;
};



void send_message (int sockfd, struct sockaddr_in *addr, uint16_t portoff,
		   uint32_t type, ...);
