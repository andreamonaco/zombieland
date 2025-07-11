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



#include "config.h"



#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>

#include "zombieland.h"


#define AREA_FRAME_DURATION 130

struct
npc
{
  SDL_Rect place;
  SDL_Texture *texture;
  SDL_Rect *srcs;
  SDL_Rect origin;
  enum facing facing;
};


struct
client_area
{
  uint32_t id;

  SDL_Texture *texture;
  SDL_Rect *display_srcs;
  int area_frames_num;

  SDL_Rect *overlay_srcs;
  int overlay_frames_num;

  SDL_Rect walkable;

  struct npc *npcs;
  int npcs_num;

  struct client_area *next;
};



void
render_string (const char *string, SDL_Rect rect, TTF_Font *font, SDL_Color col,
	       SDL_Renderer *rend)
{
  SDL_Surface *surf;
  SDL_Texture *txtr;

  surf = TTF_RenderText_Solid (font, string, col);

  if (!surf)
    {
      fprintf (stderr, "could not print text: %s\n", TTF_GetError ());
      exit (1);
    }

  txtr = SDL_CreateTextureFromSurface (rend, surf);

  if (!txtr)
    {
      fprintf (stderr, "could not create texture for text: %s\n",
	       SDL_GetError ());
      exit (1);
    }

  rect.w = surf->w;
  rect.h = surf->h;

  SDL_RenderCopy (rend, txtr, NULL, &rect);
  SDL_DestroyTexture (txtr);
  SDL_FreeSurface (surf);
}


int
move_bag_cursor (int command, int pos, int is_double)
{
  switch (command)
    {
    case SDLK_LEFT:
      if (pos % 2)
	return pos-1;
      else if (pos >= BAG_SIZE)
	return pos-BAG_SIZE+1;
      else
	return pos;
    case SDLK_RIGHT:
      if (!(pos % 2))
	return pos+1;
      else if (pos < BAG_SIZE && is_double)
	return pos+BAG_SIZE-1;
      else
	return pos;
    case SDLK_UP:
      return (pos >= 2 && pos < 8) || pos > 9 ? pos-2 : pos;
    case SDLK_DOWN:
      return pos < 6 || (pos >= 8 && pos < 14) ? pos+2 : pos;
    }
}


void
print_welcome_message (void)
{
  puts ("zombieland client " PACKAGE_VERSION "\n"
	"Copyright (C) 2025 Andrea Monaco\n"
	"License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/"
	"gpl.html>\n"
	"This is free software: you are free to change and redistribute it.\n"
	"There is NO WARRANTY, to the extent permitted by law.\n");
}



int
main (int argc, char *argv[])
{
  int sockfd;
  struct sockaddr_in local_addr, server_addr, recv_addr;
  ssize_t recvlen;
  socklen_t recv_addr_len = sizeof (recv_addr);
  uint16_t portoff = 0, tmp;
  struct hostent *server;

  struct message msg, *state, buf1, buf2, *buf, *latest_srv_state = NULL;

  uint32_t id, last_update = 0;

  struct client_area field;
  SDL_Rect field_srcs [] = {RECT_BY_GRID (0, 0, 72, 64)},
    field_overlays [] = {RECT_BY_GRID (0, 64, 72, 64),
    RECT_BY_GRID (72, 64, 72, 64), RECT_BY_GRID (144, 64, 72, 64),
    RECT_BY_GRID (216, 64, 72, 64), RECT_BY_GRID (288, 64, 72, 64)},
    field_walkable = {0, 0, 512, 512};

  struct client_area room;
  SDL_Rect room_src = {0, 0, 256, 256},
    room_walkable = RECT_BY_GRID (2, 2, 12, 12);
  SDL_Rect room_npc_srcs [] = {{4, 5, 24, 24}, {4, 37, 24, 24}, {4, 69, 24, 24},
			       {4, 101, 24, 24}};
  struct npc room_npcs [] = {{RECT_BY_GRID (7, 7, 1, 1), NULL, room_npc_srcs,
			      {-4, -4, 0, 0}, FACING_DOWN}};

  struct client_area basement = {0};
  SDL_Rect basement_src = {0, 256, 256, 256},
    basement_walkable = RECT_BY_GRID (2, 2, 12, 11);

  struct client_area hotel_ground = {0};
  SDL_Rect hotel_ground_src = {256, 0, 256, 256},
    hotel_ground_walkable = RECT_BY_GRID (2, 2, 12, 12);
  struct npc hotel_ground_npcs [] = {{RECT_BY_GRID (1, 6, 1, 1), NULL, room_npc_srcs,
				      {-4, -4, 0, 0}, FACING_RIGHT}};

  struct client_area hotel_room = {0};
  SDL_Rect hotel_room_src = {256, 256, 256, 256},
    hotel_room_walkable = RECT_BY_GRID (6, 4, 12, 12);

  struct client_area *areas = &field, *area = &field, *ar;

  SDL_Rect character_srcs [] = {{0, 6, 16, 21}, {16, 6, 16, 21}, {48, 6, 16, 21},
				{0, 69, 16, 21}, {16, 69, 16, 21}, {48, 69, 16, 21},
				{0, 38, 16, 21}, {16, 38, 16, 21}, {48, 38, 16, 21},
				{0, 102, 16, 21}, {16, 102, 16, 21}, {48, 102, 16, 21},

				{20, 129, 16, 16}, {20, 146, 16, 16}, {20, 163, 16, 16},
				{37, 129, 16, 16}, {37, 146, 16, 16}, {37, 163, 16, 16},
				{54, 129, 16, 16}, {54, 146, 16, 16}, {54, 163, 16, 16},
				{3, 129, 16, 16}, {3, 146, 16, 16}, {3, 163, 16, 16},

				{20, 180, 16, 16}, {20, 197, 16, 16}, {20, 214, 16, 16},
				{37, 180, 16, 16}, {37, 197, 16, 16}, {37, 214, 16, 16},
				{54, 180, 16, 16}, {54, 197, 16, 16}, {54, 214, 16, 16},
				{3, 180, 16, 16}, {3, 197, 16, 16}, {3, 214, 16, 16},

				{20, 231, 16, 16}, {20, 248, 16, 16}, {20, 265, 16, 16},
				{37, 231, 16, 16}, {37, 248, 16, 16}, {37, 265, 16, 16},
				{54, 231, 16, 16}, {54, 248, 16, 16}, {54, 265, 16, 16},
				{3, 231, 16, 16}, {3, 248, 16, 16}, {3, 265, 16, 16},

				{20, 282, 16, 16}, {20, 299, 16, 16}, {20, 316, 16, 16},
				{37, 282, 16, 16}, {37, 299, 16, 16}, {37, 316, 16, 16},
				{54, 282, 16, 16}, {54, 299, 16, 16}, {54, 316, 16, 16},
				{3, 282, 16, 16}, {3, 299, 16, 16}, {3, 316, 16, 16},

				{20, 333, 16, 16}, {20, 350, 16, 16}, {20, 367, 16, 16},
				{37, 333, 16, 16}, {37, 350, 16, 16}, {37, 367, 16, 16},
				{54, 333, 16, 16}, {54, 350, 16, 16}, {54, 367, 16, 16},
				{3, 333, 16, 16}, {3, 350, 16, 16}, {3, 367, 16, 16},

				{20, 384, 16, 16}, {20, 401, 16, 16}, {20, 418, 16, 16},
				{37, 384, 16, 16}, {37, 401, 16, 16}, {37, 418, 16, 16},
				{54, 384, 16, 16}, {54, 401, 16, 16}, {54, 418, 16, 16},
				{3, 384, 16, 16}, {3, 401, 16, 16}, {3, 418, 16, 16}},
    character_origin []= {{0, -5, 16, 21}, {0, 0, 16, 16}, {0, 0, 16, 16},
			  {0, 0, 16, 16}, {0, 0, 16, 16}, {0, 0, 16, 16},
			  {0, 0, 16, 16}},
    character_box = RECT_BY_GRID (6, 0, 1, 1),
    character_dest = {0, 0, 16, 21}, pers, shot_src = {40, 18, 16, 16},
    sh = {0, 0, GRID_CELL_W, GRID_CELL_H},
    zombie_srcs [] = {{0, 6, 16, 21}, {16, 6, 16, 21}, {48, 6, 16, 21},
		      {0, 69, 16, 21}, {16, 69, 16, 21}, {48, 69, 16, 21},
		      {0, 38, 16, 21}, {16, 38, 16, 21}, {48, 38, 16, 21},
		      {0, 102, 16, 21}, {16, 102, 16, 21}, {48, 102, 16, 21}},
    zombie_origin = {0, -5, 16, 21};

  int32_t loc_char_speed_x = 0, loc_char_speed_y = 0, do_interact = 0,
    do_shoot = 0, do_stab = 0, do_search = 0, bodytype = 0,
    life = MAX_PLAYER_HEALTH, is_immortal = 0, bullets = 16, hunger = 0,
    thirst = 0, num_visibles, just_shot = 0, just_stabbed = 0;
  enum facing loc_char_facing = FACING_DOWN, srv_char_facing = FACING_DOWN;
  struct visible vis;

  SDL_Window *win;
  SDL_Renderer *rend;
  SDL_Event event;

  SDL_Texture *overworldtxtr, *interiorstxtr, *charactertxtr, *zombietxtr,
    *npctxtr, *effectstxtr, *texttxtr, *bagtxtr, *objectstxtr;
  SDL_Surface *iconsurf, *textsurf;
  TTF_Font *hudfont, *textfont;
  char textbox [TEXTLINESIZE*MAXTEXTLINES+1], hudtext [20];
  int textlines = 0, textcursor, is_searching = 0, bagcursor, bagswap1, bagswap2,
    swaprest = 0;
  char tmpch;
  SDL_Rect charliferect = {10, 10, 40, 40}, bulletsrect = {10, 25, 40, 40},
    hungerrect = {WINDOW_WIDTH/2+10, 10, 40, 40},
    thirstrect = {WINDOW_WIDTH/2+10, 25, 40, 40},
    textbackrect = {0, WINDOW_HEIGHT-50, WINDOW_WIDTH, 50},
    textrect [] = {{10, WINDOW_HEIGHT-40, 0, 0}, {10, WINDOW_HEIGHT-20, 0, 0}},
    objcaptionrect = {10, WINDOW_HEIGHT-35, 0, 0},
    healthobjrect = {0, 0, 16, 16}, bulletobjrect = {16, 0, 16, 16},
    foodobjrect = {32, 0, 16, 16}, waterobjrect = {48, 0, 16, 16},
    fleshobjrect = {0, 16, 16, 16}, searchableiconrect = {16, 16, 16, 16},
    searchingiconrect = {32, 16, 16, 16},
    bagslotsrects [] = {{30, 48, 16, 16}, {82, 48, 16, 16}, {30, 96, 16, 16},
			{82, 96, 16, 16}, {30, 144, 16, 16}, {82, 144, 16, 16},
			{30, 192, 16, 16}, {82, 192, 16, 16},
			{158, 48, 16, 16}, {210, 48, 16, 16}, {158, 96, 16, 16},
			{210, 96, 16, 16}, {158, 144, 16, 16}, {210, 144, 16, 16},
			{158, 192, 16, 16}, {210, 192, 16, 16}},
    bagcursorsrc = {256, 0, 22, 22}, bagswapsrc = {256, 22, 22, 22},
    bagcursordest = {0, 0, 22, 22};

  char *objcaptions [] = {" ", "", "", "", "", "Rotten meat"};

  SDL_Color textcol = {0, 0, 0, 255};
  Uint8 colr, colg, colb, cola;

  SDL_Rect camera_src = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT},
    back_src = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT},
    screen_dest = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT},
    screen_overlay = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT},
    halfscreen = {0, 0, WINDOW_WIDTH/2, WINDOW_HEIGHT};

  Mix_Chunk *shootsfx, *stabsfx, *healsfx, *reloadsfx, *eatsfx, *drinksfx;

  int quit = 0, i, got_update;
  Uint32 frame_counter = 1, timeout = SERVER_TIMEOUT;


  print_welcome_message ();

  if (argc < 3)
    {
      fprintf (stderr, "need a server address and a login name as arguments!\n");
      return 1;
    }

  if (strlen (argv [2]) > MAX_LOGNAME_LEN)
    {
      fprintf (stderr, "login name can't exceed %d bytes!\n", MAX_LOGNAME_LEN);
      return 1;
    }

  if (argc >= 4)
    {
      if (strlen (argv [3]) == 1 && '0' <= *argv [3] && *argv [3] <= '6')
	bodytype = *argv [3]-'0';
      else
	fprintf (stderr, "warning: body type must be between 0 and 6\n");
    }

  sockfd = socket (AF_INET, SOCK_DGRAM, 0);

  if (sockfd < 0)
    {
      fprintf (stderr, "could not open socket\n");
      return 1;
    }

  bzero ((char *) &local_addr, sizeof (local_addr));
  local_addr.sin_family = AF_INET;
  local_addr.sin_addr.s_addr = INADDR_ANY;

  while (portoff < 16)
    {
      local_addr.sin_port = htons (ZOMBIELAND_PORT+portoff);

      if (!bind (sockfd, (struct sockaddr *) &local_addr, sizeof (local_addr)))
	break;

      portoff++;
    }

  if (portoff == 16)
    {
      fprintf (stderr, "could not bind socket, maybe another program is bound to "
	       "the same port?\n");
      return 1;
    }

  printf ("listening on port %d...\n", ZOMBIELAND_PORT+portoff);

  server = gethostbyname (argv [1]);

  if (!server)
    {
      fprintf (stderr, "could not resolve server name\n");
      return 1;
    }

  bzero ((char *) &server_addr, sizeof (server_addr));
  server_addr.sin_family = AF_INET;
  bcopy ((char *) server->h_addr, (char *) &server_addr.sin_addr.s_addr,
	 server->h_length);
  server_addr.sin_port = htons (ZOMBIELAND_PORT);

  bzero ((char *) &msg, sizeof (msg));
  msg.type = htonl (MSG_LOGIN);
  tmp = htons (portoff);
  memcpy (&msg.args.login.portoff, &tmp, sizeof (tmp));
  strcpy (msg.args.login.logname, argv [2]);
  msg.args.login.bodytype = htonl (bodytype);

  printf ("contacting server %s... ", argv [1]);
  fflush (stdout);

  if (sendto (sockfd, (char *)&msg, sizeof (msg), 0,
	      (struct sockaddr *) &server_addr, sizeof (server_addr)) < 0)
    {
      fprintf (stderr, "could not send data to server\n");
      return 1;
    }

  recvlen = recvfrom (sockfd, (char *)&msg, sizeof (msg), 0,
		      (struct sockaddr *) &recv_addr, &recv_addr_len);

  if (recvlen < 0)
    {
      fprintf (stderr, "could not receive data from the server\n");
      return 1;
    }

  if (recvlen < 5)
    {
      fprintf (stderr, "got a message too short from server\n");
      return 1;
    }

  msg.type = ntohl (msg.type);

  switch (msg.type)
    {
    case MSG_LOGINOK:
      id = ntohl (msg.args.loginok.id);
      printf ("got id %d\n", id);
      break;
    case MSG_LOGNAME_IN_USE:
      fprintf (stderr, "logname is already taken\n");
      exit (1);
      break;
    case MSG_SERVER_FULL:
      fprintf (stderr, "server has reached maximum players\n");
      exit (1);
      break;
    default:
      fprintf (stderr, "got wrong response from server (%d)\n", msg.type);
      return 1;
    }


  if (SDL_Init (SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
      fprintf (stderr, "could not initialise SDL: %s\n", SDL_GetError ());
      return 1;
    }

  win = SDL_CreateWindow ("ZombieLand", SDL_WINDOWPOS_UNDEFINED,
			  SDL_WINDOWPOS_UNDEFINED, WINDOW_WIDTH, WINDOW_HEIGHT,
			  0);

  if (!win)
    {
      fprintf (stderr, "could not create window: %s\n", SDL_GetError ());
      SDL_Quit ();
      return 1;
    }

  if (IMG_Init (IMG_INIT_PNG) != IMG_INIT_PNG)
    {
      fprintf (stderr, "could not initialize SDL_image: %s\n", IMG_GetError ());
      SDL_Quit ();
      return 1;
    }

  if (TTF_Init() < 0)
    {
      fprintf (stderr, "could not initialize SDL_ttf: %s\n", TTF_GetError ());
      SDL_Quit ();
      return 1;
    }

  if (Mix_OpenAudio (44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0)
    {
      fprintf (stderr,  "could not initialize SDL_mixer: %s\n", Mix_GetError ());
      SDL_Quit ();
      return 1;
    }

  rend = SDL_CreateRenderer (win, -1, SDL_RENDERER_ACCELERATED
			     | SDL_RENDERER_PRESENTVSYNC);

  if (!rend)
    {
      fprintf (stderr, "could not create renderer: %s\n", SDL_GetError ());
      SDL_Quit ();
      return 1;
    }

  SDL_SetRenderDrawColor (rend, 100, 100, 100, 255);
  SDL_RenderClear (rend);

  overworldtxtr = IMG_LoadTexture (rend, "overworld.png");

  if (!overworldtxtr)
    {
      fprintf (stderr, "could not load art: %s\n", SDL_GetError ());
      SDL_Quit ();
      return 1;
    }

  interiorstxtr = IMG_LoadTexture (rend, "interiors.png");

  if (!interiorstxtr)
    {
      fprintf (stderr, "could not load art: %s\n", SDL_GetError ());
      SDL_Quit ();
      return 1;
    }

  charactertxtr = IMG_LoadTexture (rend, "character.png");

  if (!charactertxtr)
    {
      fprintf (stderr, "could not load art: %s\n", SDL_GetError ());
      SDL_Quit ();
      return 1;
    }

  zombietxtr = IMG_LoadTexture (rend, "NPC_test.png");

  if (!zombietxtr)
    {
      fprintf (stderr, "could not load art: %s\n", SDL_GetError ());
      SDL_Quit ();
      return 1;
    }

  npctxtr = IMG_LoadTexture (rend, "log.png");

  if (!npctxtr)
    {
      fprintf (stderr, "could not load art: %s\n", SDL_GetError ());
      SDL_Quit ();
      return 1;
    }

  effectstxtr = IMG_LoadTexture (rend, "effects.png");

  if (!effectstxtr)
    {
      fprintf (stderr, "could not load art: %s\n", SDL_GetError ());
      SDL_Quit ();
      return 1;
    }

  bagtxtr = IMG_LoadTexture (rend, "bag.png");

  if (!bagtxtr)
    {
      fprintf (stderr, "could not load art: %s\n", SDL_GetError ());
      SDL_Quit ();
      return 1;
    }

  objectstxtr = IMG_LoadTexture (rend, "objects.png");

  if (!objectstxtr)
    {
      fprintf (stderr, "could not load art: %s\n", SDL_GetError ());
      SDL_Quit ();
      return 1;
    }

  iconsurf = IMG_Load ("icon.png");

  if (!iconsurf)
    {
      fprintf (stderr, "could not load art: %s\n", SDL_GetError ());
      SDL_Quit ();
      return 1;
    }

  SDL_SetWindowIcon (win, iconsurf);

  hudfont = TTF_OpenFont ("Boxy-Bold.ttf", 12);

  if (!hudfont)
    {
      fprintf (stderr, "could not load font: %s\n", TTF_GetError ());
      SDL_Quit ();
      return 1;
    }

  textfont = TTF_OpenFont ("DigitalJots.ttf", 20);

  if (!textfont)
    {
      fprintf (stderr, "could not load font: %s\n", TTF_GetError ());
      SDL_Quit ();
      return 1;
    }

  shootsfx = Mix_LoadWAV ("bang_01.ogg");

  if (!shootsfx)
    {
      fprintf (stderr, "could not load sound effect: %s\n", Mix_GetError ());
      SDL_Quit ();
      return 1;
    }

  stabsfx = Mix_LoadWAV ("knifesharpener1.flac");

  if (!stabsfx)
    {
      fprintf (stderr, "could not load sound effect: %s\n", Mix_GetError ());
      SDL_Quit ();
      return 1;
    }

  healsfx = Mix_LoadWAV ("heartbeat.flac");

  if (!healsfx)
    {
      fprintf (stderr, "could not load sound effect: %s\n", Mix_GetError ());
      SDL_Quit ();
      return 1;
    }

  reloadsfx = Mix_LoadWAV ("reload.wav");

  if (!reloadsfx)
    {
      fprintf (stderr, "could not load sound effect: %s\n", Mix_GetError ());
      SDL_Quit ();
      return 1;
    }

  eatsfx = Mix_LoadWAV ("eat.wav");

  if (!eatsfx)
    {
      fprintf (stderr, "could not load sound effect: %s\n", Mix_GetError ());
      SDL_Quit ();
      return 1;
    }

  drinksfx = Mix_LoadWAV ("bottle.wav");

  if (!drinksfx)
    {
      fprintf (stderr, "could not load sound effect: %s\n", Mix_GetError ());
      SDL_Quit ();
      return 1;
    }

  field.id = 0;
  field.texture = overworldtxtr;
  field.display_srcs = field_srcs;
  field.area_frames_num = 1;
  field.overlay_srcs = field_overlays;
  field.overlay_frames_num = 5;
  field.walkable = field_walkable;
  field.npcs = NULL;
  field.npcs_num = 0;
  field.next = &room;

  room.id = 1;
  room.texture = interiorstxtr;
  room.display_srcs = &room_src;
  room.area_frames_num = 1;
  room.overlay_srcs = NULL;
  room.overlay_frames_num = 0;
  room.walkable = room_walkable;
  room_npcs [0].texture = npctxtr;
  room.npcs = room_npcs;
  room.npcs_num = 1;
  room.next = &basement;

  basement.id = 2;
  basement.texture = interiorstxtr;
  basement.display_srcs = &basement_src;
  basement.area_frames_num = 1;
  basement.walkable = basement_walkable;
  basement.next = &hotel_ground;

  hotel_ground.id = 3;
  hotel_ground.texture = interiorstxtr;
  hotel_ground.display_srcs = &hotel_ground_src;
  hotel_ground.area_frames_num = 1;
  hotel_ground.walkable = hotel_ground_walkable;
  hotel_ground_npcs [0].texture = npctxtr;
  hotel_ground.npcs = hotel_ground_npcs;
  hotel_ground.npcs_num = 1;
  hotel_ground.next = &hotel_room;

  hotel_room.id = 4;
  hotel_room.texture = interiorstxtr;
  hotel_room.display_srcs = &hotel_room_src;
  hotel_room.area_frames_num = 1;
  hotel_room.walkable = hotel_room_walkable;

  SDL_RenderCopy (rend, field.texture, NULL, NULL);
  character_dest.x = character_box.x + character_origin [bodytype].x;
  character_dest.y = character_box.y + character_origin [bodytype].y;
  character_dest.w = character_origin [bodytype].w;
  character_dest.h = character_origin [bodytype].h;
  SDL_RenderCopy (rend, charactertxtr, &character_srcs [loc_char_facing],
		  &character_dest);
  SDL_RenderPresent (rend);

  while (!quit)
    {
      while (SDL_PollEvent (&event))
	{
	  switch (event.type)
	    {
	    case SDL_KEYDOWN:
	      switch (event.key.keysym.sym)
		{
		case SDLK_ESCAPE:
		  printf ("you quit the game.  Make sure that no client is "
			  "started from this system in the next 60 seconds\n");
		  quit = 1;
		  break;
		case SDLK_LEFT:
		  if (!is_searching)
		    {
		      loc_char_speed_x = -2;
		      if (!loc_char_speed_y || loc_char_facing == FACING_RIGHT)
			loc_char_facing = FACING_LEFT;
		    }
		  else
		    {
		      bagcursor = move_bag_cursor (SDLK_LEFT, bagcursor,
						   is_searching == 2);
		    }
		  break;
		case SDLK_RIGHT:
		  if (!is_searching)
		    {
		      loc_char_speed_x = 2;
		      if (!loc_char_speed_y || loc_char_facing == FACING_LEFT)
			loc_char_facing = FACING_RIGHT;
		    }
		  else
		    {
		      bagcursor = move_bag_cursor (SDLK_RIGHT, bagcursor,
						   is_searching == 2);
		    }
		  break;
		case SDLK_UP:
		  if (!is_searching)
		    {
		      loc_char_speed_y = -2;
		      if (!loc_char_speed_x || loc_char_facing == FACING_DOWN)
			loc_char_facing = FACING_UP;
		    }
		  else
		    {
		      bagcursor = move_bag_cursor (SDLK_UP, bagcursor,
						   is_searching == 2);
		    }
		  break;
		case SDLK_DOWN:
		  if (!is_searching)
		    {
		      loc_char_speed_y = 2;
		      if (!loc_char_speed_x || loc_char_facing == FACING_UP)
			loc_char_facing = FACING_DOWN;
		    }
		  else
		    {
		      bagcursor = move_bag_cursor (SDLK_DOWN, bagcursor,
						   is_searching == 2);
		    }
		  break;
		case SDLK_SPACE:
		  if (is_searching)
		    {
		      if (bagswap1 >= 0)
			{
			  if (bagcursor == bagswap1)
			    bagswap1 = -1;
			  else
			    {
			      bagswap2 = bagcursor;
			      swaprest = 3;
			    }
			}
		      else
			bagswap1 = bagcursor;
		    }
		  else if (!textlines)
		    do_interact = 10;
		  else
		    {
		      textcursor += 2;

		      if (textcursor >= textlines)
			textlines = 0;
		    }
		  break;
		case SDLK_f:
		  do_shoot = 9;
		  break;
		case SDLK_r:
		  do_stab = 4;
		  break;
		case SDLK_q:
		  do_search = !do_search;
		  break;
		}
	      break;
	    case SDL_KEYUP:
	      switch (event.key.keysym.sym)
		{
		case SDLK_LEFT:
		  if (loc_char_speed_x == -2)
		    loc_char_speed_x = 0;
		  if (loc_char_speed_y)
		    loc_char_facing = (loc_char_speed_y > 0) ? FACING_DOWN
		      : FACING_UP;
		  break;
		case SDLK_RIGHT:
		  if (loc_char_speed_x == 2)
		    loc_char_speed_x = 0;
		  if (loc_char_speed_y)
		    loc_char_facing = loc_char_speed_y > 0 ? FACING_DOWN
		      : FACING_UP;
		  break;
		case SDLK_UP:
		  if (loc_char_speed_y == -2)
		    loc_char_speed_y = 0;
		  if (loc_char_speed_x)
		    loc_char_facing = loc_char_speed_x > 0 ? FACING_RIGHT
		      : FACING_LEFT;
		  break;
		case SDLK_DOWN:
		  if (loc_char_speed_y == 2)
		    loc_char_speed_y = 0;
		  if (loc_char_speed_x)
		    loc_char_facing = loc_char_speed_x > 0 ? FACING_RIGHT
		      : FACING_LEFT;
		  break;
		}
	      break;
	    case SDL_QUIT:
	      printf ("you quit the game.  Make sure that no client is "
		      "started from this system in the next 60 seconds\n");
	      quit = 1;
	      break;
	    }
	}

      if (loc_char_speed_x || loc_char_speed_y)
	do_interact = 0;

      send_message (sockfd, &server_addr, -1, MSG_CLIENT_CHAR_STATE, id,
		    frame_counter, loc_char_speed_x, loc_char_speed_y,
		    loc_char_facing, do_interact, do_shoot, do_stab, do_search,
		    bagswap1, bagswap2);

      if (do_interact)
	do_interact--;

      if (do_shoot)
	do_shoot--;

      if (do_stab)
	do_stab--;

      if (swaprest)
	{
	  swaprest--;

	  if (!swaprest)
	    bagswap1 = bagswap2 = -1;
	}

      got_update = 0;

      while (1)
	{
	  buf = &buf1 == latest_srv_state ? &buf2 : &buf1;

	  recvlen = recvfrom (sockfd, buf, MAXMSGSIZE, MSG_DONTWAIT,
			      (struct sockaddr *) &recv_addr, &recv_addr_len);
	  state = buf;

	  if (recvlen < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
	    break;

	  if (recvlen < 0)
	    {
	      fprintf (stderr, "could not receive data from the server\n");
	      return 1;
	    }

	  if (recvlen < 5)
	    {
	      fprintf (stderr, "got a message too short from server\n");
	      return 1;
	    }

	  got_update = 1;

	  state->type = ntohl (state->type);

	  switch (state->type)
	    {
	    case MSG_SERVER_STATE:
	      if (!latest_srv_state ||
		  last_update < ntohl (state->args.server_state.frame_counter))
		{
		  last_update = ntohl (state->args.server_state.frame_counter);
		  latest_srv_state = buf;
		}
	      break;
	    case MSG_PLAYER_DIED:
	      printf ("you died!\n");
	      return 1;
	    default:
	      fprintf (stderr, "got wrong response from server (%d)\n", state->type);
	      return 1;
	    }
	}

      if (!got_update)
	{
	  timeout--;

	  if (!timeout)
	    {
	      fprintf (stderr, "no data from server until timeout\n");
	      return 1;
	    }
	}
      else
	timeout = SERVER_TIMEOUT;

      frame_counter = SDL_GetTicks ();

      if (latest_srv_state)
	{
	  state = (struct message *)latest_srv_state;

	  ar = areas;

	  while (ar)
	    {
	      if (ar->id == ntohl (state->args.server_state.areaid))
		{
		  area = ar;
		  break;
		}

	      ar = ar->next;
	    }

	  if (!ar)
	    {
	      fprintf (stderr, "got unknown area id from server (%d)\n",
		       state->args.server_state.areaid);
	      return 1;
	    }

	  if ((character_box.x != ntohl (state->args.server_state.x)
	       || character_box.y != ntohl (state->args.server_state.y))
	      && textlines)
	    {
	      textlines = 0;
	    }

	  character_box.x = ntohl (state->args.server_state.x);
	  character_box.y = ntohl (state->args.server_state.y);
	  character_box.w = ntohl (state->args.server_state.w);
	  character_box.h = ntohl (state->args.server_state.h);
	  srv_char_facing = state->args.server_state.char_facing;

	  if (ntohl (state->args.server_state.life) > life)
	    Mix_PlayChannel (-1, healsfx, 0);

	  life = ntohl (state->args.server_state.life);
	  is_immortal = state->args.server_state.is_immortal;

	  if (ntohl (state->args.server_state.bullets) > bullets)
	    Mix_PlayChannel (-1, reloadsfx, 0);

	  bullets = ntohl (state->args.server_state.bullets);

	  if (ntohl (state->args.server_state.hunger) < hunger)
	    Mix_PlayChannel (-1, eatsfx, 0);

	  hunger = ntohl (state->args.server_state.hunger);

	  if (ntohl (state->args.server_state.thirst) < thirst)
	    Mix_PlayChannel (-1, drinksfx, 0);

	  thirst = ntohl (state->args.server_state.thirst);
	  num_visibles = ntohl (state->args.server_state.num_visibles);


	  if (state->args.server_state.just_shot && frame_counter-just_shot>100)
	    {
	      just_shot = frame_counter;
	    }

	  if (state->args.server_state.just_stabbed && frame_counter-just_stabbed>66)
	    {
	      just_stabbed = frame_counter;
	    }
	}

      camera_src.x = -WINDOW_WIDTH/2 + area->walkable.x + character_box.x
	+ character_box.w/2;
      camera_src.y = -WINDOW_HEIGHT/2 + area->walkable.y + character_box.y
	+ character_box.h/2;

      if (camera_src.x < 0)
	{
	  camera_src.x = 0;
	}
      else if (camera_src.x+WINDOW_WIDTH > area->display_srcs->w)
	{
	  camera_src.x = area->display_srcs->w-WINDOW_WIDTH;
	}

      if (camera_src.y < 0)
	{
	  camera_src.y = 0;
	}
      else if (camera_src.y+WINDOW_HEIGHT > area->display_srcs->h)
	{
	  camera_src.y = area->display_srcs->h-WINDOW_HEIGHT;
	}

      back_src.x = area->display_srcs
	[frame_counter%(area->area_frames_num*AREA_FRAME_DURATION)
	 /AREA_FRAME_DURATION].x + camera_src.x;
      back_src.y = area->display_srcs
	[frame_counter%(area->area_frames_num*AREA_FRAME_DURATION)
	 /AREA_FRAME_DURATION].y + camera_src.y;

      SDL_RenderClear (rend);
      SDL_RenderCopy (rend, area->texture, &back_src, &screen_dest);

      for (i = 0; i < area->npcs_num; i++)
	{
	  pers.x = -camera_src.x + area->walkable.x + area->npcs [i].place.x
	    + area->npcs [i].origin.x;
	  pers.y = -camera_src.y + area->walkable.y + area->npcs [i].place.y
	    + area->npcs [i].origin.y;
	  pers.w = area->npcs [i].srcs [0].w;
	  pers.h = area->npcs [i].srcs [0].h;
	  SDL_RenderCopy (rend, area->npcs [i].texture,
			  &area->npcs [i].srcs [area->npcs [i].facing], &pers);
	}

      if (latest_srv_state)
	{
	  for (i = 0; i < num_visibles; i++)
	    {
	      vis = state->args.server_state.visibles [i];
	      vis.type = ntohl (vis.type);

	      if (vis.type < VISIBLE_HEALTH || vis.type > VISIBLE_FLESH)
		continue;

	      pers.x = -camera_src.x + area->walkable.x + ntohl (vis.x);
	      pers.y = -camera_src.y + area->walkable.y + ntohl (vis.y);
	      pers.w = GRID_CELL_W;
	      pers.h = GRID_CELL_H;
	      SDL_RenderCopy (rend, objectstxtr,
			      vis.type == VISIBLE_HEALTH ? &healthobjrect
			      : vis.type == VISIBLE_AMMO ? &bulletobjrect
			      : vis.type == VISIBLE_FOOD ? &foodobjrect
			      : vis.type == VISIBLE_WATER ? &waterobjrect
			      : &fleshobjrect, &pers);
	    }

	  for (i = 0; i < num_visibles; i++)
	    {
	      vis = state->args.server_state.visibles [i];
	      vis.type = ntohl (vis.type);

	      if (vis.type != VISIBLE_ZOMBIE)
		continue;

	      pers.x = -camera_src.x + area->walkable.x + ntohl (vis.x)
		+ zombie_origin.x;
	      pers.y = -camera_src.y + area->walkable.y + ntohl (vis.y)
		+ zombie_origin.y;

	      if (vis.is_immortal)
		{
		  if (frame_counter % 200 < 100)
		    pers.x += (frame_counter%99/33-1)*5;
		  else
		    pers.y += (frame_counter%99/33-1)*5;
		}

	      pers.w = zombie_origin.w;
	      pers.h = zombie_origin.h;
	      SDL_RenderCopy (rend, zombietxtr,
			      &zombie_srcs [ntohl (vis.facing)*3+
					    ((vis.speed_x || vis.speed_y)
					     ? 1+(frame_counter%400)/200 : 0)],
			      &pers);
	    }

	  for (i = 0; i < num_visibles; i++)
	    {
	      vis = state->args.server_state.visibles [i];
	      vis.type = ntohl (vis.type);
	      vis.subtype = ntohl (vis.subtype);

	      if (vis.subtype < 0 || vis.subtype > 6)
		vis.subtype = 0;

	      if (vis.type != VISIBLE_PLAYER)
		continue;

	      pers.x = -camera_src.x + area->walkable.x + ntohl (vis.x)
		+ character_origin [vis.subtype].x;
	      pers.y = -camera_src.y + area->walkable.y + ntohl (vis.y)
		+ character_origin [vis.subtype].y;
	      pers.w = character_origin [vis.subtype].w;
	      pers.h = character_origin [vis.subtype].h;
	      SDL_RenderCopy (rend, charactertxtr,
			      &character_srcs [vis.subtype*12
					       +ntohl (vis.facing)*3+
					       ((vis.speed_x || vis.speed_y)
						? 1+(frame_counter%400)/200 : 0)],
			      &pers);
	    }
	}

      if (!is_immortal || frame_counter%130 < 65)
	{
	  character_dest.x = -camera_src.x + area->walkable.x + character_box.x
	    + character_origin [bodytype].x;
	  character_dest.y = -camera_src.y + area->walkable.y + character_box.y
	    + character_origin [bodytype].y;
	  character_dest.w = character_origin [bodytype].w;
	  character_dest.h = character_origin [bodytype].h;
	  SDL_RenderCopy (rend, charactertxtr,
			  &character_srcs [bodytype*12
					   +loc_char_facing*3+
					   ((loc_char_speed_x || loc_char_speed_y)
					    ? 1+(frame_counter%400)/200 : 0)],
			  &character_dest);
	}

      if (latest_srv_state)
	{
	  for (i = 0; i < num_visibles; i++)
	    {
	      vis = state->args.server_state.visibles [i];
	      vis.type = ntohl (vis.type);

	      if (vis.type != VISIBLE_SEARCHABLE
		  && vis.type != VISIBLE_SEARCHING)
		continue;

	      pers.x = -camera_src.x + area->walkable.x + ntohl (vis.x);
	      pers.y = -camera_src.y + area->walkable.y + ntohl (vis.y);
	      pers.w = GRID_CELL_W;
	      pers.h = GRID_CELL_H;
	      SDL_RenderCopy (rend, objectstxtr,
			      vis.type == VISIBLE_SEARCHABLE ? &searchableiconrect
			      : &searchingiconrect, &pers);
	    }
	}

      if (area->overlay_frames_num)
	{
	  screen_overlay.x = area->overlay_srcs
	    [frame_counter%(area->overlay_frames_num*AREA_FRAME_DURATION)
	     /AREA_FRAME_DURATION].x + camera_src.x;
	  screen_overlay.y = area->overlay_srcs
	    [frame_counter%(area->overlay_frames_num*AREA_FRAME_DURATION)
	     /AREA_FRAME_DURATION].y + camera_src.y;
	  SDL_RenderCopy (rend, area->texture, &screen_overlay, &screen_dest);
	}

      if (latest_srv_state)
	{
	  for (i = 0; i < num_visibles; i++)
	    {
	      vis = state->args.server_state.visibles [i];
	      vis.type = ntohl (vis.type);

	      if (vis.type != VISIBLE_SHOT)
		continue;

	      sh.x = -camera_src.x + area->walkable.x + ntohl (vis.x);
	      sh.y = -camera_src.y + area->walkable.y + ntohl (vis.y);
	      SDL_RenderCopy (rend, effectstxtr, &shot_src, &sh);
	    }
	}

      if (latest_srv_state && state->args.server_state.textbox_lines_num)
	{
	  do_interact = 0;

	  strcpy (textbox, state->args.server_state.textbox);
	  textlines = ntohl (state->args.server_state.textbox_lines_num);
	  textcursor = 0;

	  if ((int32_t) ntohl (state->args.server_state.npcid) >= 0)
	    {
	      area->npcs [ntohl (state->args.server_state.npcid)].facing
		= loc_char_facing == FACING_DOWN ? FACING_UP
		: loc_char_facing == FACING_UP ? FACING_DOWN
		: loc_char_facing == FACING_RIGHT ? FACING_LEFT
		: FACING_RIGHT;
	    }
	}

      if (textlines)
	{
	  SDL_GetRenderDrawColor (rend, &colr, &colg, &colb, &cola);
	  SDL_SetRenderDrawColor (rend, 255, 255, 255, 255);
	  SDL_RenderFillRect (rend, &textbackrect);
	  SDL_SetRenderDrawColor (rend, colr, colg, colb, cola);

	  for (i = 0; i <= 1; i++)
	    {
	      if (textcursor+i>=textlines)
		break;

	      tmpch = textbox [TEXTLINESIZE*(textcursor+i+1)];
	      textbox [TEXTLINESIZE*(textcursor+i+1)] = 0;
	      textsurf = TTF_RenderText_Solid (textfont,
					       &textbox [TEXTLINESIZE*(textcursor+i)],
					       textcol);
	      textbox [TEXTLINESIZE*(textcursor+i+1)] = tmpch;

	      if (!textsurf)
		{
		  fprintf (stderr, "could not print text: %s\n", TTF_GetError ());
		  return 1;
		}

	      texttxtr = SDL_CreateTextureFromSurface (rend, textsurf);

	      if (!texttxtr)
		{
		  fprintf (stderr, "could not create texture for text: %s\n",
			   SDL_GetError ());
		  return 1;
		}

	      textrect [i].w = textsurf->w;
	      textrect [i].h = textsurf->h;

	      SDL_RenderCopy (rend, texttxtr, NULL, &textrect [i]);
	      SDL_DestroyTexture (texttxtr);
	      SDL_FreeSurface (textsurf);
	    }
	}

      sprintf (hudtext, "LIFE %2d/%2d", life, MAX_PLAYER_HEALTH);
      render_string (hudtext, charliferect, hudfont, textcol, rend);

      sprintf (hudtext, "AMMO %2d/16", bullets);
      render_string (hudtext, bulletsrect, hudfont, textcol, rend);

      sprintf (hudtext, "HUNGER %2d/20", hunger);
      render_string (hudtext, hungerrect, hudfont, textcol, rend);

      sprintf (hudtext, "THIRST %2d/20", thirst);
      render_string (hudtext, thirstrect, hudfont, textcol, rend);

      if (latest_srv_state && state->args.server_state.is_searching)
	{
	  loc_char_speed_x = loc_char_speed_y = 0;

	  if (!is_searching)
	    {
	      bagcursor = 0, bagswap1 = -1, bagswap2 = -1;
	      is_searching = ntohl (state->args.server_state.is_searching);
	    }

	  SDL_RenderCopy (rend, bagtxtr,
			  is_searching == 1 ? &halfscreen : &screen_dest,
			  is_searching == 1 ? &halfscreen : &screen_dest);

	  for (i = 0; i < BAG_SIZE*is_searching; i++)
	    {
	      switch (ntohl (state->args.server_state.bag [i]))
		{
		case OBJECT_HEALTH:
		  SDL_RenderCopy (rend, objectstxtr, &healthobjrect,
				  &bagslotsrects [i]);
		  break;
		case OBJECT_AMMO:
		  SDL_RenderCopy (rend, objectstxtr, &bulletobjrect,
				  &bagslotsrects [i]);
		  break;
		case OBJECT_FOOD:
		  SDL_RenderCopy (rend, objectstxtr, &foodobjrect,
				  &bagslotsrects [i]);
		  break;
		case OBJECT_WATER:
		  SDL_RenderCopy (rend, objectstxtr, &waterobjrect,
				  &bagslotsrects [i]);
		  break;
		case OBJECT_FLESH:
		  SDL_RenderCopy (rend, objectstxtr, &fleshobjrect,
				  &bagslotsrects [i]);
		  break;
		default:
		  break;
		}
	    }

	  bagcursordest.x = bagslotsrects [bagcursor].x-3;
	  bagcursordest.y = bagslotsrects [bagcursor].y-3;
	  SDL_RenderCopy (rend, bagtxtr, &bagcursorsrc, &bagcursordest);

	  if (bagswap1 >= 0)
	    {
	      bagcursordest.x = bagslotsrects [bagswap1].x-3;
	      bagcursordest.y = bagslotsrects [bagswap1].y-3;
	      SDL_RenderCopy (rend, bagtxtr, &bagswapsrc, &bagcursordest);
	    }

	  render_string (objcaptions [ntohl (state->args.server_state.bag [bagcursor])],
			 objcaptionrect, hudfont, textcol, rend);
	}
      else
	is_searching = 0;

      SDL_RenderPresent (rend);

      if (just_shot == frame_counter)
	Mix_PlayChannel (-1, shootsfx, 0);

      if (just_stabbed == frame_counter)
	Mix_PlayChannel (-1, stabsfx, 0);
    }

  SDL_Quit ();

  return 0;
}
