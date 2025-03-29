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



#define WINDOW_WIDTH 274
#define WINDOW_HEIGHT 236


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
  int id;
  SDL_Texture *texture;
  SDL_Rect display_src, walkable;
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

  struct client_area cave;
  SDL_Rect cave_src = {0, 0, 274, 236}, cave_walkable = {50, 50, 176, 160};

  SDL_Rect character_srcs [] = {{8, 7, 14, 21}, {8, 38, 14, 21}, {25, 38, 14, 21},
				{60, 7, 14, 21}, {59, 38, 14, 21}, {76, 38, 14, 21},
				{112, 9, 14, 21}, {110, 38, 14, 21}, {127, 38, 14, 21},
				{165, 8, 14, 21}, {161, 38, 14, 21}, {178, 38, 14, 21}},
    character_box = RECT_BY_GRID (2, 5, 1, 1), character_origin = {1, -5, 0, 0},
    character_dest = {0, 0, 14, 21}, pers;
  int32_t loc_char_speed_x = 0, loc_char_speed_y = 0;
  enum facing loc_char_facing = FACING_DOWN, srv_char_facing = FACING_DOWN;
  struct other_player opl;

  SDL_Window *win;
  SDL_Renderer *rend;
  SDL_Event event;

  SDL_Texture *cavetxtr, *charactertxtr;

  SDL_Rect screen_src, screen_dest;

  int quit = 0, i;
  uint32_t frame_counter = 1;
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
    case MSG_LOGINFAIL:
      fprintf (stderr, "logname is already taken\n");
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

  cavetxtr = IMG_LoadTexture (rend, "cave.png");

  if (!cavetxtr)
    {
      fprintf (stderr, "could not load art: %s\n", SDL_GetError ());
      SDL_Quit ();
      return 1;
    }

  charactertxtr = IMG_LoadTexture (rend, "main-character.png");

  if (!charactertxtr)
    {
      fprintf (stderr, "could not load art: %s\n", SDL_GetError ());
      SDL_Quit ();
      return 1;
    }

  cave.texture = cavetxtr;
  cave.display_src = cave_src;
  cave.walkable = cave_walkable;

  SDL_RenderCopy (rend, cave.texture, NULL, NULL);
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
		    loc_char_facing);

      while (1)
	{
	  buf = (char *)&buf1 == latest_srv_state ? (char *)&buf2 : (char *)&buf1;

	  recvlen = recvfrom (sockfd, buf, MAXMSGSIZE, MSG_DONTWAIT,
			      (struct sockaddr *) &recv_addr, &recv_addr_len);
	  state = (struct message *)buf;

	  if (recvlen < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
	    {
	      printf ("got no message\n");
	      break;
	    }

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

	  state->type = ntohl (state->type);

	  switch (state->type)
	    {
	    case MSG_SERVER_STATE:
	      printf ("received server char state x=%d y=%d\n",
		      state->args.server_state.x,
		      state->args.server_state.y);
	      if (!latest_srv_state ||
		  last_update < state->args.server_state.frame_counter)
		{
		  last_update = state->args.server_state.frame_counter;
		  latest_srv_state = buf;
		}
	      break;
	    default:
	      fprintf (stderr, "got wrong response from server (%d)\n", state->type);
	      return 1;
	    }
	}

      if (latest_srv_state)
	{
	  state = (struct message *)latest_srv_state;

	  character_box.x = state->args.server_state.x;
	  character_box.y = state->args.server_state.y;
	  character_box.w = state->args.server_state.w;
	  character_box.h = state->args.server_state.h;
	  srv_char_facing = state->args.server_state.char_facing;
	}

      /*screen_src.x = -WINDOW_WIDTH/2 + cave.display_src.x + character_box.w/2
	+ character_box.x + cave.walkable.x;
      screen_src.y = -WINDOW_HEIGHT/2 + cave.display_src.y + character_box.h/2
	+ character_box.y + cave.walkable.y;
      screen_src.w = WINDOW_WIDTH;
      screen_src.h = WINDOW_HEIGHT;*/

      screen_src.x = 0;
      screen_src.y = 0;
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
      SDL_RenderCopy (rend, cave.texture, &screen_src, &screen_dest);

      if (latest_srv_state)
	{
	  printf ("there are %d entities\n",
		  state->args.server_state.num_entities);
	  for (i = 0; i < state->args.server_state.num_entities; i++)
	    {
	      opl = *(struct other_player *)(latest_srv_state
					     +sizeof (struct message)
					     +i*sizeof (opl));
	      printf ("this entity is x=%d y=%d\n", opl.x, opl.y);

	      pers.x = screen_dest.x - screen_src.x + cave.display_src.x
		+ cave.walkable.x + opl.x + character_origin.x;
	      pers.y = screen_dest.y - screen_src.y + cave.display_src.y
		+ cave.walkable.y + opl.y + character_origin.y;
	      pers.w = character_dest.w;
	      pers.h = character_dest.h;
	      SDL_RenderCopy (rend, charactertxtr,
			      &character_srcs [opl.facing*3+
					       ((opl.speed_x || opl.speed_y)
						? 1+(frame_counter%12)/6 : 0)],
			      &pers);
	    }
	}

      printf ("character_box x=%d y=%d\n", character_box.x, character_box.y);

      character_dest.x = screen_dest.x - screen_src.x + cave.display_src.x
	+ cave.walkable.x + character_box.x + character_origin.x;
      character_dest.y = screen_dest.y - screen_src.y + cave.display_src.y
	+ cave.walkable.y + character_box.y + character_origin.y;
      SDL_RenderCopy (rend, charactertxtr,
		      &character_srcs [loc_char_facing*3+
				       ((loc_char_speed_x || loc_char_speed_y)
					? 1+(frame_counter%12)/6 : 0)],
		      &character_dest);

      SDL_RenderPresent (rend);

      frame_counter++;

      t2 = SDL_GetTicks ();

      delay = FRAME_DURATION - (t2 - t1);

      if (delay > 0)
	{
	  SDL_Delay (delay);
	  printf ("end of frame\n");
	}
      else
	printf ("warning: frame skipped by %f\n", delay);
    }

  SDL_Quit ();

  return 0;
}
