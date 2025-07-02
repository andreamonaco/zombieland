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
#define R_BY_GR(x,y,w,h) RECT_BY_GRID(x, y, w, h)


#define ZOMBIELAND_PORT 19894
#define FRAME_DURATION 33.333f   /* 30 hz */
#define CLIENT_TIMEOUT 1800
#define SERVER_TIMEOUT 60000
#define MAXMSGSIZE 2048
#define MAX_LOGNAME_LEN 15

#define MAX_PLAYERS 128

#define MAX_ZOMBIES 10
#define ZOMBIE_SPAWN_INTERVAL 300

#define OBJECT_SPAWN_INTERVAL 300

#define MAX_PLAYER_HEALTH 30

#define WINDOW_WIDTH 256
#define WINDOW_HEIGHT 256

#define MAXTEXTLINES 10
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
#define MSG_LOGNAME_IN_USE     2
#define MSG_SERVER_FULL        3
#define MSG_CLIENT_CHAR_STATE  4
#define MSG_SERVER_STATE       5
#define MSG_PLAYER_DIED        6
#define MSG_INTERACT           7


struct
login_args
{
  uint16_t portoff;
  char logname [MAX_LOGNAME_LEN+1];
  uint32_t bodytype;
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
  uint32_t do_stab;
  uint32_t do_search;
  int32_t swap [2];
};


#define VISIBLE_PLAYER 0
#define VISIBLE_ZOMBIE 1
#define VISIBLE_SHOT   2
#define VISIBLE_HEALTH 3
#define VISIBLE_AMMO   4
#define VISIBLE_FOOD   5
#define VISIBLE_WATER  6
#define VISIBLE_FLESH  7
#define VISIBLE_SEARCHABLE 8
#define VISIBLE_SEARCHING 9

struct
visible
{
  uint32_t type;
  uint32_t subtype;
  uint32_t x, y, w, h;
  enum facing facing;
  int32_t speed_x, speed_y;
  uint32_t is_immortal;
};


#define BAG_SIZE 8

enum
object_type
  {
    OBJECT_NONE,
    OBJECT_HEALTH,
    OBJECT_AMMO,
    OBJECT_FOOD,
    OBJECT_WATER,
    OBJECT_FLESH
  };


#define MAX_VISIBLES 128

struct
server_state_args
{
  uint32_t frame_counter;
  uint32_t areaid;
  uint32_t x, y, w, h;
  enum facing char_facing;
  int32_t life;
  uint32_t is_immortal;
  uint32_t bullets;
  uint32_t hunger;
  uint32_t thirst;
  uint32_t just_shot;
  uint32_t just_stabbed;
  uint32_t is_searching;
  enum object_type bag [BAG_SIZE*2];
  int32_t npcid;
  uint32_t textbox_lines_num;
  char textbox [TEXTLINESIZE*MAXTEXTLINES+1];
  uint32_t num_visibles;
  struct visible visibles [MAX_VISIBLES];
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
