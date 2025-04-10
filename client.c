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



#define WINDOW_WIDTH 256
#define WINDOW_HEIGHT 256


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

#include "zombieland.h"



struct
client_area
{
  uint32_t id;
  SDL_Texture *texture;
  SDL_Rect display_src, overlay_src, walkable;
  struct client_area *next;
};



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

  struct message msg, *state;
  char buf1 [MAXMSGSIZE], buf2 [MAXMSGSIZE];
  char *buf, *latest_srv_state = NULL;

  uint32_t id, last_update = 0;

  struct client_area field;
  SDL_Rect field_src = {0, 0, 256, 256}, field_overlay = {256, 0, 256, 256},
    field_walkable = {0, 0, 256, 256};

  struct client_area room;
  SDL_Rect room_src = {0, 256, 256, 256}, room_overlay = {0},
    room_walkable = RECT_BY_GRID (2, 2, 12, 12);

  struct client_area *areas = &field, *area = &field, *ar;

  SDL_Rect character_srcs [] = {{0, 6, 16, 21}, {16, 6, 16, 21}, {48, 6, 16, 21},
				{0, 69, 16, 21}, {16, 69, 16, 21}, {48, 69, 16, 21},
				{0, 38, 16, 21}, {16, 38, 16, 21}, {48, 38, 16, 21},
				{0, 102, 16, 21}, {16, 102, 16, 21}, {48, 102, 16, 21}},
    character_box = RECT_BY_GRID (6, 1, 1, 1), character_origin = {0, -5, 0, 0},
    character_dest = {0, 0, 16, 21}, pers, shot_src = {40, 18, 16, 16}, sh,
    zombie_srcs [] = {{0, 6, 16, 21}, {16, 6, 16, 21}, {48, 6, 16, 21},
		      {0, 69, 16, 21}, {16, 69, 16, 21}, {48, 69, 16, 21},
		      {0, 38, 16, 21}, {16, 38, 16, 21}, {48, 38, 16, 21},
		      {0, 102, 16, 21}, {16, 102, 16, 21}, {48, 102, 16, 21}},
    zombie_origin = {0, -5, 0, 0};

  int32_t loc_char_speed_x = 0, loc_char_speed_y = 0, do_interact = 0,
    do_shoot = 0, life = 10;
  enum facing loc_char_facing = FACING_DOWN, srv_char_facing = FACING_DOWN;
  struct visible vis;

  SDL_Window *win;
  SDL_Renderer *rend;
  SDL_Event event;

  SDL_Texture *areatxtr, *charactertxtr, *zombietxtr, *effectstxtr, *texttxtr,
    *charlifetxtr;
  SDL_Surface *textsurf, *charlifesurf;
  TTF_Font *hudfont, *textfont;
  char textbox [MAXTEXTSIZE], charlifetext [20];
  int textlines = 0, textcursor;
  char tmpch;
  SDL_Rect charliferect = {10, 10, 40, 40},
    textbackrect = {0, WINDOW_HEIGHT-50, WINDOW_WIDTH, 50},
    textrect [] = {{10, WINDOW_HEIGHT-40, 0, 0}, {10, WINDOW_HEIGHT-20, 0, 0}};

  SDL_Color textcol = {0, 0, 0, 255};
  Uint8 colr, colg, colb, cola;

  SDL_Rect screen_src, screen_dest;

  int quit = 0, i, got_update;
  uint32_t frame_counter = 1, timeout = SERVER_TIMEOUT;
  Uint32 t1, t2;
  double delay;


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

  msg.type = htonl (MSG_LOGIN);
  tmp = htons (portoff);
  memcpy (&msg.args.login.portoff, &tmp, sizeof (tmp));
  strcpy (msg.args.login.logname, argv [2]);

  printf ("contacting server %s... ", argv [1]);

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


  if (SDL_Init (SDL_INIT_VIDEO) < 0)
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
      fprintf (stderr, "could not initialize SDL_ttf: %s\n", TTF_GetError());
      SDL_Quit ();
      return 1;
    }

  rend = SDL_CreateRenderer (win, -1, SDL_RENDERER_PRESENTVSYNC);

  if (!rend)
    {
      fprintf (stderr, "could not create renderer: %s\n", SDL_GetError ());
      SDL_Quit ();
      return 1;
    }

  SDL_SetRenderDrawColor (rend, 100, 100, 100, 255);
  SDL_RenderClear (rend);

  areatxtr = IMG_LoadTexture (rend, "area.png");

  if (!areatxtr)
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

  effectstxtr = IMG_LoadTexture (rend, "effects.png");

  if (!effectstxtr)
    {
      fprintf (stderr, "could not load art: %s\n", SDL_GetError ());
      SDL_Quit ();
      return 1;
    }

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

  field.id = 0;
  field.texture = areatxtr;
  field.display_src = field_src;
  field.overlay_src = field_overlay;
  field.walkable = field_walkable;
  field.next = &room;

  room.id = 1;
  room.texture = areatxtr;
  room.display_src = room_src;
  room.overlay_src = room_overlay;
  room.walkable = room_walkable;
  room.next = NULL;

  SDL_RenderCopy (rend, field.texture, NULL, NULL);
  character_dest.x = character_box.x + character_origin.x;
  character_dest.y = character_box.y + character_origin.y;
  SDL_RenderCopy (rend, charactertxtr, &character_srcs [loc_char_facing],
		  &character_dest);
  SDL_RenderPresent (rend);

  while (!quit)
    {
      t1 = SDL_GetTicks ();

      while (SDL_PollEvent (&event))
	{
	  switch (event.type)
	    {
	    case SDL_KEYDOWN:
	      switch (event.key.keysym.sym)
		{
		case SDLK_ESCAPE:
		  quit = 1;
		  break;
		case SDLK_LEFT:
		  loc_char_speed_x = -2;
		  if (!loc_char_speed_y || loc_char_facing == FACING_RIGHT)
		    loc_char_facing = FACING_LEFT;
		  break;
		case SDLK_RIGHT:
		  loc_char_speed_x = 2;
		  if (!loc_char_speed_y || loc_char_facing == FACING_LEFT)
		    loc_char_facing = FACING_RIGHT;
		  break;
		case SDLK_UP:
		  loc_char_speed_y = -2;
		  if (!loc_char_speed_x || loc_char_facing == FACING_DOWN)
		    loc_char_facing = FACING_UP;
		  break;
		case SDLK_DOWN:
		  loc_char_speed_y = 2;
		  if (!loc_char_speed_x || loc_char_facing == FACING_UP)
		    loc_char_facing = FACING_DOWN;
		  break;
		case SDLK_SPACE:
		  if (!textlines)
		    do_interact = 1;
		  else
		    {
		      textcursor += 2;

		      if (textcursor >= textlines)
			textlines = 0;
		    }
		  break;
		case SDLK_f:
		  do_shoot = 1;
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
	      quit = 1;
	      break;
	    }
	}

      send_message (sockfd, &server_addr, -1, MSG_CLIENT_CHAR_STATE, id,
		    frame_counter, loc_char_speed_x, loc_char_speed_y,
		    loc_char_facing, do_interact, do_shoot);

      do_interact = 0;
      do_shoot = 0;

      got_update = 0;

      while (1)
	{
	  buf = (char *)&buf1 == latest_srv_state ? (char *)&buf2 : (char *)&buf1;

	  recvlen = recvfrom (sockfd, buf, MAXMSGSIZE, MSG_DONTWAIT,
			      (struct sockaddr *) &recv_addr, &recv_addr_len);
	  state = (struct message *)buf;

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
		  last_update < state->args.server_state.frame_counter)
		{
		  last_update = state->args.server_state.frame_counter;
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

      if (latest_srv_state)
	{
	  state = (struct message *)latest_srv_state;

	  ar = areas;

	  while (ar)
	    {
	      if (ar->id == state->args.server_state.areaid)
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

	  if ((character_box.x != state->args.server_state.x
	       || character_box.y != state->args.server_state.y)
	      && textlines)
	    {
	      textlines = 0;
	    }

	  character_box.x = state->args.server_state.x;
	  character_box.y = state->args.server_state.y;
	  character_box.w = state->args.server_state.w;
	  character_box.h = state->args.server_state.h;
	  srv_char_facing = state->args.server_state.char_facing;
	  life = state->args.server_state.life;
	}

      /*screen_src.x = -WINDOW_WIDTH/2 + cave.display_src.x + character_box.w/2
	+ character_box.x + cave.walkable.x;
      screen_src.y = -WINDOW_HEIGHT/2 + cave.display_src.y + character_box.h/2
	+ character_box.y + cave.walkable.y;
      screen_src.w = WINDOW_WIDTH;
      screen_src.h = WINDOW_HEIGHT;*/

      screen_src.x = area->display_src.x;
      screen_src.y = area->display_src.y;
      screen_src.w = WINDOW_WIDTH;
      screen_src.h = WINDOW_HEIGHT;

      screen_dest.x = 0;
      screen_dest.y = 0;
      screen_dest.w = WINDOW_WIDTH;
      screen_dest.h = WINDOW_HEIGHT;

      /*if (screen_src.x < cave.display_src.x)
	{
	  screen_dest.w -= cave.display_src.x-screen_src.x;
	  screen_src.w = screen_dest.w;
	  screen_dest.x += cave.display_src.x-screen_src.x;
	  screen_src.x = cave.display_src.x;
	}
      else if (screen_src.x+WINDOW_WIDTH > cave.display_src.w)
	{
	  screen_dest.w = cave.display_src.w-screen_src.x+cave.display_src.x;
	  screen_src.w = screen_dest.w;
	}

      if (screen_src.y < cave.display_src.y)
	{
	  screen_dest.h -= cave.display_src.y-screen_src.y;
	  screen_src.h = screen_dest.h;
	  screen_dest.y += cave.display_src.y-screen_src.y;
	  screen_src.y = cave.display_src.y;
	}
      else if (screen_src.y+WINDOW_HEIGHT > cave.display_src.h)
	{
	  screen_dest.h = cave.display_src.h-screen_src.y+cave.display_src.y;
	  screen_src.h = screen_dest.h;
	  }*/

      SDL_RenderClear (rend);
      SDL_RenderCopy (rend, area->texture, &screen_src, &screen_dest);

      if (latest_srv_state)
	{
	  for (i = 0; i < state->args.server_state.num_visibles; i++)
	    {
	      vis = *(struct visible *)(latest_srv_state+sizeof (struct message)
					+i*sizeof (vis));

	      if (vis.type != VISIBLE_ZOMBIE)
		continue;

	      pers.x = screen_dest.x - screen_src.x + area->display_src.x
		+ area->walkable.x + vis.x + zombie_origin.x;
	      pers.y = screen_dest.y - screen_src.y + area->display_src.y
		+ area->walkable.y + vis.y + zombie_origin.y;
	      pers.w = character_dest.w;
	      pers.h = character_dest.h;
	      SDL_RenderCopy (rend, zombietxtr,
			      &zombie_srcs [vis.facing*3+
					    ((vis.speed_x || vis.speed_y)
					     ? 1+(frame_counter%12)/6 : 0)],
			      &pers);
	    }

	  for (i = 0; i < state->args.server_state.num_visibles; i++)
	    {
	      vis = *(struct visible *)(latest_srv_state+sizeof (struct message)
					+i*sizeof (vis));

	      if (vis.type != VISIBLE_PLAYER)
		continue;

	      pers.x = screen_dest.x - screen_src.x + area->display_src.x
		+ area->walkable.x + vis.x + character_origin.x;
	      pers.y = screen_dest.y - screen_src.y + area->display_src.y
		+ area->walkable.y + vis.y + character_origin.y;
	      pers.w = character_dest.w;
	      pers.h = character_dest.h;
	      SDL_RenderCopy (rend, charactertxtr,
			      &character_srcs [vis.facing*3+
					       ((vis.speed_x || vis.speed_y)
						? 1+(frame_counter%12)/6 : 0)],
			      &pers);
	    }
	}

      character_dest.x = screen_dest.x - screen_src.x + area->display_src.x
	+ area->walkable.x + character_box.x + character_origin.x;
      character_dest.y = screen_dest.y - screen_src.y + area->display_src.y
	+ area->walkable.y + character_box.y + character_origin.y;
      SDL_RenderCopy (rend, charactertxtr,
		      &character_srcs [loc_char_facing*3+
				       ((loc_char_speed_x || loc_char_speed_y)
					? 1+(frame_counter%12)/6 : 0)],
		      &character_dest);

      if (area->overlay_src.w)
	{
	  SDL_RenderCopy (rend, area->texture, &area->overlay_src, &screen_dest);
	}

      if (latest_srv_state)
	{
	  for (i = 0; i < state->args.server_state.num_visibles; i++)
	    {
	      vis = *(struct visible *)(latest_srv_state+sizeof (struct message)
					+i*sizeof (vis));

	      if (vis.type != VISIBLE_SHOT)
		continue;

	      sh.x = screen_dest.x - screen_src.x + area->display_src.x
		+ area->walkable.x + vis.x;
	      sh.y = screen_dest.y - screen_src.y + area->display_src.y
		+ area->walkable.y + vis.y;
	      sh.w = GRID_CELL_W;
	      sh.h = GRID_CELL_H;
	      SDL_RenderCopy (rend, effectstxtr, &shot_src, &sh);
	    }
	}

      if (latest_srv_state && state->args.server_state.textbox_lines_num)
	{
	  strcpy (textbox, latest_srv_state+sizeof (struct message)
		  +sizeof (struct visible)
		  *state->args.server_state.num_visibles);
	  textlines = state->args.server_state.textbox_lines_num;
	  textcursor = 0;
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

      sprintf (charlifetext, "LIFE %2d/10", life);
      charlifesurf = TTF_RenderText_Solid (hudfont, charlifetext, textcol);

      if (!charlifesurf)
	{
	  fprintf (stderr, "could not print text: %s\n", TTF_GetError ());
	  return 1;
	}

      charlifetxtr = SDL_CreateTextureFromSurface (rend, charlifesurf);

      if (!charlifetxtr)
	{
	  fprintf (stderr, "could not create texture for text: %s\n",
		   SDL_GetError ());
	  return 1;
	}

      charliferect.w = charlifesurf->w;
      charliferect.h = charlifesurf->h;

      SDL_RenderCopy (rend, charlifetxtr, NULL, &charliferect);
      SDL_DestroyTexture (charlifetxtr);
      SDL_FreeSurface (charlifesurf);

      SDL_RenderPresent (rend);

      frame_counter++;

      t2 = SDL_GetTicks ();

      delay = FRAME_DURATION - (t2 - t1);

      if (delay > 0)
	SDL_Delay (delay);
      else
	printf ("warning: frame skipped by %f\n", -delay);
    }

  SDL_Quit ();

  return 0;
}
