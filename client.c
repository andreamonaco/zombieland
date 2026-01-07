/*  Copyright (C) 2025-2026 Andrea Monaco
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
#include <stdarg.h>
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

#include "malloc.h"
#include "zombieland.h"



#define DURATION_OF_DISPLAY_FRAME 33

#define INTERVAL_BETWEEN_SENDING_CLIENT_STATES 33

#define AREA_FRAME_DURATION 130

#define RESEND_ACTION 3


#define HUD_FONT_SIZE 12


struct
walking_sfx
{
  SDL_Rect *places;
  int places_num;

  Mix_Chunk *sfx;

  int channel;
};


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

  SDL_Texture *texture [3];
  int respects_time;
  SDL_Rect *display_srcs;
  int area_frames_num;

  SDL_Rect *overlay_srcs;
  int overlay_frames_num;

  SDL_Rect walkable;

  struct walking_sfx *walk_sfxs;
  int walk_sfxs_num;

  struct npc *npcs;
  int npcs_num;

  struct client_area *next;
};


enum
player_action
  {
    ACTION_DO_NOTHING,
    ACTION_PAUSE,
    ACTION_MOVE_LEFT,
    ACTION_MOVE_RIGHT,
    ACTION_MOVE_UP,
    ACTION_MOVE_DOWN,
    ACTION_INTERACT,
    ACTION_SHOOT,
    ACTION_STAB,
    ACTION_SEARCH
  };



SDL_Texture *
render_string (const char *string, TTF_Font *font, SDL_Color col,
	       SDL_Renderer *rend, int *w, int *h)
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

  *w = surf->w;
  *h = surf->h;

  SDL_FreeSurface (surf);

  return txtr;
}


void
display_string (const char *string, SDL_Rect rect, TTF_Font *font, SDL_Color col,
		SDL_Renderer *rend)
{
  int w, h;
  SDL_Texture *txtr = render_string (string, font, col, rend, &w, &h);

  rect.w = w;
  rect.h = h;

  SDL_RenderCopy (rend, txtr, NULL, &rect);
  SDL_DestroyTexture (txtr);
}


void
display_strings_centrally (TTF_Font *font, int scaling, SDL_Color col,
			   SDL_Renderer *rend, int cursor, ...)
{
  SDL_Texture *str;
  SDL_Rect rect, cursorrect;
  va_list valist;
  char *s;
  int n = 0, xcenter = WINDOW_WIDTH*scaling/2, ystep;

  va_start (valist, cursor);

  while ((s = va_arg (valist, char *)))
    {
      n++;
    }

  va_end (valist);

  ystep = WINDOW_HEIGHT*scaling/n;
  rect.y = ystep/2-HUD_FONT_SIZE*scaling/2;

  n = 0;

  va_start (valist, cursor);

  while ((s = va_arg (valist, char *)))
    {
      if (*s)
	{
	  str = render_string (s, font, col, rend, &rect.w, &rect.h);

	  rect.x = xcenter-rect.w/2;

	  SDL_RenderCopy (rend, str, NULL, &rect);

	  if (n == cursor)
	    {
	      cursorrect.x = rect.x-20*scaling;
	      cursorrect.y = rect.y-10*scaling;
	      cursorrect.w = rect.w+40*scaling;
	      cursorrect.h = rect.h+20*scaling;

	      SDL_SetRenderDrawColor (rend, 0, 0, 0, 255);
	      SDL_RenderDrawRect (rend, &cursorrect);
	    }

	  SDL_DestroyTexture (str);
	}

      n++;
      rect.y += ystep;
    }

  va_end (valist);
}


void
exit_game (void)
{
  printf ("you quit the game.  Make sure that no client is "
	  "started from this system in the next 60 seconds\n");
  exit (0);
}


void
display_death_screen_and_exit (TTF_Font *font, int scaling, SDL_Color col,
			       SDL_Renderer *rend)
{
  SDL_Event event;
  SDL_Rect screen = {0, 0, WINDOW_WIDTH*scaling, WINDOW_HEIGHT*scaling};
  int ticks, last_refresh = 0;

  SDL_SetRenderDrawColor (rend, 255, 255, 255, 255);

  while (1)
    {
      ticks = SDL_GetTicks ();

      while (SDL_PollEvent (&event))
	{
	  switch (event.type)
	    {
	    case SDL_KEYDOWN:
	    case SDL_QUIT:
	      exit_game ();
	      break;
	    default:
	      break;
	    }
	}

      if (ticks-last_refresh > DURATION_OF_DISPLAY_FRAME)
	{
	  SDL_RenderFillRect (rend, &screen);

	  display_strings_centrally (font, scaling, col, rend, -1, "", "YOU DIED",
				     "Press any key to quit...", "", (char *) NULL);

	  SDL_RenderPresent (rend);

	  last_refresh = ticks;
	}
    }
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
configure_keys (enum player_action controls [])
{
  SDL_Event ev;
  char *prompts [] = {"move left: ", "move right: ", "move up: ", "move down: ",
		      "interact: ", "shoot: ", "stab: ", "search: "};
  int i, ret;

  printf ("\nconfiguring keys... for each action, please press the key of your "
	  "choice, while the windows has focus:\n");

  for (i = 0; i < sizeof (prompts) / sizeof (prompts [0]); i++)
    {
      printf (prompts [i]);
      fflush (stdout);

      while ((ret = SDL_WaitEvent (&ev)))
	{
	  if (ev.type == SDL_KEYDOWN)
	    {
	      controls [ev.key.keysym.scancode] = i+2;
	      printf (SDL_GetKeyName (ev.key.keysym.sym));
	      break;
	    }
	}

      if (!ret)
	{
	  printf ("error while reading an event\n");
	  exit (1);
	}

      printf ("\n");
    }

  printf ("\n");
}


void
scale_rect (SDL_Rect *rect, int factor)
{
  rect->x *= factor;
  rect->y *= factor;
  rect->w *= factor;
  rect->h *= factor;
}


char *
concatenate_strings (const char *s1, const char *s2)
{
  char *ret = malloc_and_check (strlen (s1)+strlen (s2)+1);

  strcpy (ret, s1);
  strcpy (ret+strlen (s1), s2);

  return ret;
}


SDL_Texture *
load_texture (const char *name, SDL_Renderer *rend)
{
  SDL_Texture *ret;
  char *path = concatenate_strings ("./assets/", name);

  ret = IMG_LoadTexture (rend, path);

  if (!ret)
    {
      fprintf (stderr, "could not load texture %s: %s\n", path, SDL_GetError ());
      SDL_Quit ();
      exit (1);
    }

  free (path);

  return ret;
}


TTF_Font *
load_font (const char *name, int size)
{
  TTF_Font *ret;
  char *path = concatenate_strings ("./assets/", name);

  ret = TTF_OpenFont (path, size);

  if (!ret)
    {
      fprintf (stderr, "could not load font %s: %s\n", path, TTF_GetError ());
      SDL_Quit ();
      exit (1);
    }

  free (path);

  return ret;
}


Mix_Chunk *
load_wav (const char *name)
{
  Mix_Chunk *ret;
  char *path = concatenate_strings ("./assets/", name);

  ret = Mix_LoadWAV (path);

  if (!ret)
    {
      fprintf (stderr, "could not load sound file %s: %s\n", path, Mix_GetError ());
      SDL_Quit ();
      exit (1);
    }

  free (path);

  return ret;
}


void
print_help_and_exit (void)
{
  printf ("Usage: zombieland [OPTIONS] SERVER_ADDRESS PLAYER_NAME\n"
	  "Options:\n"
	  "\t-b, --body-type NUM   body type, must be between 0 and 6\n"
	  "\t-d, --double-size     double the resolution through upscaling\n"
	  "\t-f, --fullscreen      display in fullscreen\n"
	  "\t-u, --dont-limit-fps  don't limit display fps, otherwise it's 30 fps\n"
	  "\t-v, --verbose         if limiting fps, print a warning for each missed frame\n"
	  "\t-k, --configure-keys  configure controls before playing\n"
	  "\t--                    stop parsing options\n"
	  "\t-h, --help            display this help and exit\n");
  exit (0);
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

  uint32_t id, latest_update = 0;

  enum player_action controls [SDL_NUM_SCANCODES] = {0};

  struct client_area field;
  SDL_Rect field_srcs [] = {RECT_BY_GRID (0, 0, 72, 64)},
    field_overlays [] = {RECT_BY_GRID (0, 64, 72, 64),
    RECT_BY_GRID (72, 64, 72, 64), RECT_BY_GRID (144, 64, 72, 64),
    RECT_BY_GRID (216, 64, 72, 64), RECT_BY_GRID (288, 64, 72, 64)},
    field_walkable = {0, 0, 512, 512},
    field_pond [] = {RECT_BY_GRID (63, 10, 2, 2), RECT_BY_GRID (65, 11, 4, 1),
		     RECT_BY_GRID (69, 6, 3, 7)};
  struct walking_sfx field_sfx = {field_pond, 3, NULL, -1};

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
    sh = {0, 0, GRID_CELL_W, GRID_CELL_H}, *zombierects,
    zombie_srcs [] = {{0, 6, 16, 21}, {16, 6, 16, 21}, {48, 6, 16, 21},
		      {0, 69, 16, 21}, {16, 69, 16, 21}, {48, 69, 16, 21},
		      {0, 38, 16, 21}, {16, 38, 16, 21}, {48, 38, 16, 21},
		      {0, 102, 16, 21}, {16, 102, 16, 21}, {48, 102, 16, 21}},
    zombie_origin = {0, -5, 16, 21},
    blob_srcs [] = {{0, 3, 32, 32}, {31, 3, 32, 32}, {95, 3, 32, 32},
		    {0, 96, 32, 32}, {31, 96, 32, 32}, {95, 96, 32, 32},
		    {0, 68, 32, 32}, {31, 68, 32, 32}, {95, 68, 32, 32},
		    {0, 36, 32, 32}, {31, 36, 32, 32}, {95, 36, 32, 32}},
    blob_origin = {0, 0, 0, 0};

  int32_t loc_char_speed_x = 0, loc_char_speed_y = 0, do_interact = 0,
    do_shoot = 0, do_stab = 0, do_search = 0, bodytype = 0,
    life = MAX_PLAYER_HEALTH, is_immortal = 0, bullets = 16, hunger = 0,
    thirst = 0, num_visibles, just_shot = 0, just_stabbed = 0;
  int last_shoot = 0, last_stab = 0, time, phase;
  enum facing loc_char_facing = FACING_DOWN, srv_char_facing = FACING_DOWN;
  struct visible vis;

  SDL_Window *win;
  SDL_Renderer *rend;
  SDL_Event event;

  SDL_Texture *overworldtxtr, *overworld2txtr, *overworld3txtr, *interiorstxtr,
    *charactertxtr, *zombietxtr, *blobtxtr, *npctxtr, *effectstxtr, *texttxtr,
    *bagtxtr, *objectstxtr;
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
    leftscreen = {0, 0, WINDOW_WIDTH/2, WINDOW_HEIGHT},
    singlebagsrc = {0, 0, WINDOW_WIDTH/2, WINDOW_HEIGHT},
    doublebagsrc = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT},
    viewport;

  Mix_Chunk *shootsfx, *stabsfx, *healsfx, *reloadsfx, *eatsfx, *drinksfx,
    *pondsfx;

  char *servername = NULL, *playername = NULL;
  int quit = 0, i, j, scaling = 1, fullscreen = 0, limit_fps = 1, verbose = 0,
    config_keys = 0, pause = 0, menu_cursor = 0, need_arg = 0, options_finished = 0;
  Uint32 frame_counter = 1, fc, latest_update_ticks = 0, last_sent_update = 0,
    last_display = 0, ticks;


  for (i = 1; i < argc; i++)
    {
      if (need_arg)
	{
	  switch (need_arg)
	    {
	    case 'b':
	      if (strlen (argv [i]) != 1 || *argv [i] < '0' || *argv [i] > '6')
		{
		  fprintf (stderr, "option 'b' requires an integer argument "
			   "between 0 and 6\n");
		  print_help_and_exit ();
		}
	      bodytype = *argv [i]-'0';
	      break;
	    }

	  need_arg = 0;
	}
      else
	{
	  if (options_finished)
	    goto parse_arg;
	  else if (!strcmp (argv [i], "--body-type") || !strcmp (argv [i], "-b"))
	    need_arg = 'b';
	  else if (!strcmp (argv [i], "--double-size") || !strcmp (argv [i], "-d"))
	    scaling = 2;
	  else if (!strcmp (argv [i], "--fullscreen") || !strcmp (argv [i], "-f"))
	    fullscreen = 1;
	  else if (!strcmp (argv [i], "--dont-limit-fps") || !strcmp (argv [i], "-u"))
	    limit_fps = 0;
	  else if (!strcmp (argv [i], "--verbose") || !strcmp (argv [i], "-v"))
	    verbose = 1;
	  else if (!strcmp (argv [i], "--configure-keys") || !strcmp (argv [i], "-k"))
	    config_keys = 1;
	  else if (!strcmp (argv [i], "--"))
	    options_finished = 1;
	  else if (!strcmp (argv [i], "--help") || !strcmp (argv [i], "-h"))
	    print_help_and_exit ();
	  else
	    {
	      options_finished = 1;

	    parse_arg:
	      if (!servername)
		servername = argv [i];
	      else if (!playername)
		playername = argv [i];
	      else
		{
		  fprintf (stderr, "too many command-line arguments\n");
		  print_help_and_exit ();
		}
	    }
	}
    }

  if (need_arg)
    {
      fprintf (stderr, "option '%c' requires an argument\n", need_arg);
      print_help_and_exit ();
    }

  if (!servername || !playername)
    {
      fprintf (stderr, "need a server address and a login name as arguments!\n");
      print_help_and_exit ();
    }


  print_welcome_message ();


  if (strlen (playername) > MAX_LOGNAME_LEN)
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

  server = gethostbyname (servername);

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
  strcpy (msg.args.login.logname, playername);
  msg.args.login.bodytype = htonl (bodytype);

  printf ("contacting server %s... ", servername);
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

  win = SDL_CreateWindow ("ZombieLand", SDL_WINDOWPOS_CENTERED,
			  SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH*scaling,
			  WINDOW_HEIGHT*scaling, SDL_WINDOW_OPENGL
			  | (fullscreen ? SDL_WINDOW_FULLSCREEN : 0));

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


  overworldtxtr = load_texture ("overworld.png", rend);
  overworld2txtr = load_texture ("overworld2.png", rend);
  overworld3txtr = load_texture ("overworld3.png", rend);
  interiorstxtr = load_texture ("interiors.png", rend);
  charactertxtr = load_texture ("character.png", rend);
  zombietxtr = load_texture ("NPC_test.png", rend);
  blobtxtr = load_texture ("jumblysprite.png", rend);
  npctxtr = load_texture ("log.png", rend);
  effectstxtr = load_texture ("effects.png", rend);
  bagtxtr = load_texture ("bag.png", rend);
  objectstxtr = load_texture ("objects.png", rend);

  iconsurf = IMG_Load ("assets/icon.png");

  if (!iconsurf)
    {
      fprintf (stderr, "could not load image ./assets/icon.png: %s\n", SDL_GetError ());
      SDL_Quit ();
      return 1;
    }

  SDL_SetWindowIcon (win, iconsurf);

  hudfont = load_font ("Boxy-Bold.ttf", HUD_FONT_SIZE*scaling);
  textfont = load_font ("DigitalJots.ttf", 20*scaling);

  shootsfx = load_wav ("bang_01.ogg");
  stabsfx = load_wav ("knifesharpener1.flac");
  healsfx = load_wav ("heartbeat.flac");
  reloadsfx = load_wav ("reload.wav");
  eatsfx = load_wav ("eat.wav");
  drinksfx = load_wav ("bottle.wav");
  pondsfx = load_wav ("pond.wav");


  field.id = 0;
  field.texture [0] = overworldtxtr;
  field.texture [1] = overworld2txtr;
  field.texture [2] = overworld3txtr;
  field.respects_time = 1;
  field.display_srcs = field_srcs;
  field.area_frames_num = 1;
  field.overlay_srcs = field_overlays;
  field.overlay_frames_num = 5;
  field.walkable = field_walkable;
  field_sfx.sfx = pondsfx;
  field.walk_sfxs = &field_sfx;
  field.walk_sfxs_num = 1;
  field.npcs = NULL;
  field.npcs_num = 0;
  field.next = &room;

  room.id = 1;
  room.texture [0] = interiorstxtr;
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
  basement.texture [0] = interiorstxtr;
  basement.display_srcs = &basement_src;
  basement.area_frames_num = 1;
  basement.walkable = basement_walkable;
  basement.next = &hotel_ground;

  hotel_ground.id = 3;
  hotel_ground.texture [0] = interiorstxtr;
  hotel_ground.display_srcs = &hotel_ground_src;
  hotel_ground.area_frames_num = 1;
  hotel_ground.walkable = hotel_ground_walkable;
  hotel_ground_npcs [0].texture = npctxtr;
  hotel_ground.npcs = hotel_ground_npcs;
  hotel_ground.npcs_num = 1;
  hotel_ground.next = &hotel_room;

  hotel_room.id = 4;
  hotel_room.texture [0] = interiorstxtr;
  hotel_room.display_srcs = &hotel_room_src;
  hotel_room.area_frames_num = 1;
  hotel_room.walkable = hotel_room_walkable;


  if (scaling > 1)
    {
      scale_rect (&screen_dest, scaling);
      scale_rect (&leftscreen, scaling);

      scale_rect (&textbackrect, scaling);
      scale_rect (&textrect [0], scaling);
      scale_rect (&textrect [1], scaling);

      scale_rect (&charliferect, scaling);
      scale_rect (&bulletsrect, scaling);
      scale_rect (&hungerrect, scaling);
      scale_rect (&thirstrect, scaling);

      scale_rect (&bagcursordest, scaling);

      for (i = 0; i < 16; i++)
	scale_rect (&bagslotsrects [i], scaling);

      scale_rect (&objcaptionrect, scaling);
    }

  if (fullscreen)
    {
      SDL_GetRendererOutputSize (rend, &viewport.w, &viewport.h);

      viewport.x = viewport.w/2-screen_dest.w/2;
      viewport.y = viewport.h/2-screen_dest.h/2;
      viewport.w = screen_dest.w;
      viewport.h = screen_dest.h;

      SDL_RenderSetViewport (rend, &viewport);
    }


  if (!config_keys)
    {
      controls [SDL_SCANCODE_A] = controls [SDL_SCANCODE_LEFT] = ACTION_MOVE_LEFT;
      controls [SDL_SCANCODE_D] = controls [SDL_SCANCODE_RIGHT] = ACTION_MOVE_RIGHT;
      controls [SDL_SCANCODE_W] = controls [SDL_SCANCODE_UP] = ACTION_MOVE_UP;
      controls [SDL_SCANCODE_S] = controls [SDL_SCANCODE_DOWN] = ACTION_MOVE_DOWN;
      controls [SDL_SCANCODE_SPACE] = ACTION_INTERACT;
      controls [SDL_SCANCODE_F] = ACTION_SHOOT;
      controls [SDL_SCANCODE_R] = ACTION_STAB;
      controls [SDL_SCANCODE_Q] = ACTION_SEARCH;
    }
  else
    configure_keys (controls);

  controls [SDL_SCANCODE_ESCAPE] = ACTION_PAUSE;


  SDL_RenderCopy (rend, field.texture [0], NULL, NULL);
  character_dest.x = character_box.x + character_origin [bodytype].x;
  character_dest.y = character_box.y + character_origin [bodytype].y;
  character_dest.w = character_origin [bodytype].w;
  character_dest.h = character_origin [bodytype].h;
  SDL_RenderCopy (rend, charactertxtr, &character_srcs [loc_char_facing],
		  &character_dest);

  SDL_ShowCursor (SDL_DISABLE);

  SDL_RenderPresent (rend);


  while (!quit)
    {
      ticks = SDL_GetTicks ();

      while (SDL_PollEvent (&event))
	{
	  switch (event.type)
	    {
	    case SDL_KEYDOWN:
	      switch (controls [event.key.keysym.scancode])
		{
		case ACTION_PAUSE:
		  pause = !pause;

		  if (pause)
		    menu_cursor = 0;
		  else
		    SDL_SetRenderDrawColor (rend, 100, 100, 100, 255);
		  break;
		case ACTION_MOVE_LEFT:
		  if (pause);
		  else if (!is_searching)
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
		case ACTION_MOVE_RIGHT:
		  if (pause);
		  else if (!is_searching)
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
		case ACTION_MOVE_UP:
		  if (pause)
		    {
		      menu_cursor = !menu_cursor;
		    }
		  else if (!is_searching)
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
		case ACTION_MOVE_DOWN:
		  if (pause)
		    {
		      menu_cursor = !menu_cursor;
		    }
		  else if (!is_searching)
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
		case ACTION_INTERACT:
		  if (pause)
		    {
		      if (!menu_cursor)
			{
			  pause = 0;
			  SDL_SetRenderDrawColor (rend, 100, 100, 100, 255);
			}
		      else
			exit_game ();
		    }
		  else if (is_searching)
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
		    do_interact = RESEND_ACTION;
		  else
		    {
		      textcursor += 2;

		      if (textcursor >= textlines)
			textlines = 0;
		    }
		  break;
		case ACTION_SHOOT:
		  if (SHOOT_REST*FRAME_DURATION < ticks-last_shoot)
		    {
		      do_shoot = RESEND_ACTION;
		      last_shoot = ticks;
		    }
		  break;
		case ACTION_STAB:
		  if (STAB_REST*FRAME_DURATION < ticks-last_stab)
		    {
		      do_stab = RESEND_ACTION;
		      last_stab = ticks;
		    }
		  break;
		case ACTION_SEARCH:
		  do_search = !do_search;
		  break;
		default:
		  break;
		}
	      break;
	    case SDL_KEYUP:
	      switch (controls [event.key.keysym.scancode])
		{
		case ACTION_MOVE_LEFT:
		  if (loc_char_speed_x == -2)
		    loc_char_speed_x = 0;
		  if (loc_char_speed_y)
		    loc_char_facing = (loc_char_speed_y > 0) ? FACING_DOWN
		      : FACING_UP;
		  break;
		case ACTION_MOVE_RIGHT:
		  if (loc_char_speed_x == 2)
		    loc_char_speed_x = 0;
		  if (loc_char_speed_y)
		    loc_char_facing = loc_char_speed_y > 0 ? FACING_DOWN
		      : FACING_UP;
		  break;
		case ACTION_MOVE_UP:
		  if (loc_char_speed_y == -2)
		    loc_char_speed_y = 0;
		  if (loc_char_speed_x)
		    loc_char_facing = loc_char_speed_x > 0 ? FACING_RIGHT
		      : FACING_LEFT;
		  break;
		case ACTION_MOVE_DOWN:
		  if (loc_char_speed_y == 2)
		    loc_char_speed_y = 0;
		  if (loc_char_speed_x)
		    loc_char_facing = loc_char_speed_x > 0 ? FACING_RIGHT
		      : FACING_LEFT;
		  break;
		default:
		  break;
		}
	      break;
	    case SDL_QUIT:
	      exit_game ();
	      break;
	    }
	}


      fc = SDL_GetTicks ();

      if (fc-last_sent_update > INTERVAL_BETWEEN_SENDING_CLIENT_STATES)
	{
	  last_sent_update = fc;

	  if (loc_char_speed_x || loc_char_speed_y)
	    do_interact = 0;

	  send_message (sockfd, &server_addr, -1, MSG_CLIENT_CHAR_STATE, id, fc,
			loc_char_speed_x, loc_char_speed_y, loc_char_facing,
			do_interact, do_shoot, do_stab, do_search, bagswap1,
			bagswap2);

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
	}


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


	  state->type = ntohl (state->type);

	  switch (state->type)
	    {
	    case MSG_SERVER_STATE:
	      if (!latest_srv_state ||
		  latest_update < ntohl (state->args.server_state.frame_counter))
		{
		  latest_update = ntohl (state->args.server_state.frame_counter);
		  latest_srv_state = buf;
		  latest_update_ticks = fc;
		}
	      break;
	    case MSG_PLAYER_DIED:
	      display_death_screen_and_exit (hudfont, scaling, textcol, rend);
	      break;
	    default:
	      fprintf (stderr, "got wrong response from server (%d)\n", state->type);
	      return 1;
	    }
	}

      if (fc-latest_update_ticks > SERVER_TIMEOUT)
	{
	  fprintf (stderr, "reached timeout with no data from server\n");
	  return 1;
	}


      frame_counter = SDL_GetTicks ();

      if (latest_srv_state
	  && (!limit_fps || frame_counter-last_display > DURATION_OF_DISPLAY_FRAME))
	{
	  if (limit_fps && verbose
	      && frame_counter-last_display > DURATION_OF_DISPLAY_FRAME*2)
	    printf ("warning: at least one display frame was skipped\n");

	  last_display = frame_counter;


	  if (pause)
	    {
	      SDL_SetRenderDrawColor (rend, 255, 255, 255, 255);
	      SDL_RenderFillRect (rend, &screen_dest);

	      display_strings_centrally (hudfont, scaling, textcol, rend,
					 menu_cursor+2, "PAUSE", "", "Continue",
					 "Quit", (char *) NULL);

	      SDL_RenderPresent (rend);

	      continue;
	    }


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

	  time = (latest_update % 43200) / 1800;
	  phase = (time+15)%24/6;
	  phase = (phase == 3) ? 1 : phase;

	  SDL_RenderCopy (rend, area->texture [area->respects_time ? phase : 0],
			  &back_src, &screen_dest);

	  for (i = 0; i < area->npcs_num; i++)
	    {
	      pers.x = (-camera_src.x + area->walkable.x + area->npcs [i].place.x
			+ area->npcs [i].origin.x)*scaling;
	      pers.y = (-camera_src.y + area->walkable.y + area->npcs [i].place.y
			+ area->npcs [i].origin.y)*scaling;
	      pers.w = area->npcs [i].srcs [0].w*scaling;
	      pers.h = area->npcs [i].srcs [0].h*scaling;
	      SDL_RenderCopy (rend, area->npcs [i].texture,
			      &area->npcs [i].srcs [area->npcs [i].facing], &pers);
	    }

	  for (i = 0; i < num_visibles; i++)
	    {
	      vis = state->args.server_state.visibles [i];
	      vis.type = ntohl (vis.type);

	      if (vis.type < VISIBLE_HEALTH || vis.type > VISIBLE_FLESH)
		continue;

	      pers.x = (-camera_src.x + area->walkable.x + ntohl (vis.x))*scaling;
	      pers.y = (-camera_src.y + area->walkable.y + ntohl (vis.y))*scaling;
	      pers.w = GRID_CELL_W*scaling;
	      pers.h = GRID_CELL_H*scaling;
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

	      pers.x = (-camera_src.x + area->walkable.x + ntohl (vis.x)
			+ zombie_origin.x)*scaling;
	      pers.y = (-camera_src.y + area->walkable.y + ntohl (vis.y)
			+ zombie_origin.y)*scaling;

	      if (vis.is_immortal)
		{
		  if (frame_counter % 200 < 100)
		    pers.x += (frame_counter%99/33-1)*5;
		  else
		    pers.y += (frame_counter%99/33-1)*5;
		}

	      if (vis.subtype == ZOMBIE_WALKER)
		{
		  pers.w = zombie_origin.w*scaling;
		  pers.h = zombie_origin.h*scaling;
		}
	      else
		{
		  pers.w = 32*scaling;
		  pers.h = 32*scaling;
		}

	      zombierects = vis.subtype == ZOMBIE_WALKER ? zombie_srcs : blob_srcs;

	      SDL_RenderCopy (rend, vis.subtype == ZOMBIE_WALKER ? zombietxtr : blobtxtr,
			      &zombierects [ntohl (vis.facing)*3+
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

	      pers.x = (-camera_src.x + area->walkable.x + ntohl (vis.x)
			+ character_origin [vis.subtype].x)*scaling;
	      pers.y = (-camera_src.y + area->walkable.y + ntohl (vis.y)
			+ character_origin [vis.subtype].y)*scaling;
	      pers.w = character_origin [vis.subtype].w*scaling;
	      pers.h = character_origin [vis.subtype].h*scaling;
	      SDL_RenderCopy (rend, charactertxtr,
			      &character_srcs [vis.subtype*12
					       +ntohl (vis.facing)*3+
					       ((vis.speed_x || vis.speed_y)
						? 1+(frame_counter%400)/200 : 0)],
			      &pers);
	    }

	  if (!is_immortal || frame_counter%130 < 65)
	    {
	      character_dest.x = (-camera_src.x + area->walkable.x + character_box.x
				  + character_origin [bodytype].x)*scaling;
	      character_dest.y = (-camera_src.y + area->walkable.y + character_box.y
				  + character_origin [bodytype].y)*scaling;
	      character_dest.w = character_origin [bodytype].w*scaling;
	      character_dest.h = character_origin [bodytype].h*scaling;
	      SDL_RenderCopy (rend, charactertxtr,
			      &character_srcs [bodytype*12
					       +loc_char_facing*3+
					       ((loc_char_speed_x || loc_char_speed_y)
						? 1+(frame_counter%400)/200 : 0)],
			      &character_dest);
	    }

	  for (i = 0; i < num_visibles; i++)
	    {
	      vis = state->args.server_state.visibles [i];
	      vis.type = ntohl (vis.type);

	      if (vis.type != VISIBLE_SEARCHABLE
		  && vis.type != VISIBLE_SEARCHING)
		continue;

	      pers.x = (-camera_src.x + area->walkable.x + ntohl (vis.x))*scaling;
	      pers.y = (-camera_src.y + area->walkable.y + ntohl (vis.y))*scaling;
	      pers.w = GRID_CELL_W*scaling;
	      pers.h = GRID_CELL_H*scaling;
	      SDL_RenderCopy (rend, objectstxtr,
			      vis.type == VISIBLE_SEARCHABLE ? &searchableiconrect
			      : &searchingiconrect, &pers);
	    }

	  if (area->overlay_frames_num)
	    {
	      screen_overlay.x = area->overlay_srcs
		[frame_counter%(area->overlay_frames_num*AREA_FRAME_DURATION)
		 /AREA_FRAME_DURATION].x + camera_src.x;
	      screen_overlay.y = area->overlay_srcs
		[frame_counter%(area->overlay_frames_num*AREA_FRAME_DURATION)
		 /AREA_FRAME_DURATION].y + camera_src.y;
	      SDL_RenderCopy (rend, area->texture [area->respects_time ? phase : 0],
			      &screen_overlay, &screen_dest);
	    }

	  for (i = 0; i < num_visibles; i++)
	    {
	      vis = state->args.server_state.visibles [i];
	      vis.type = ntohl (vis.type);

	      if (vis.type != VISIBLE_SHOT)
		continue;

	      sh.x = (-camera_src.x + area->walkable.x + ntohl (vis.x))*scaling;
	      sh.y = (-camera_src.y + area->walkable.y + ntohl (vis.y))*scaling;
	      sh.w = GRID_CELL_W*scaling;
	      sh.h = GRID_CELL_H*scaling;
	      SDL_RenderCopy (rend, effectstxtr, &shot_src, &sh);
	    }

	  if (state->args.server_state.textbox_lines_num)
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
	  display_string (hudtext, charliferect, hudfont, textcol, rend);

	  sprintf (hudtext, "AMMO %2d/16", bullets);
	  display_string (hudtext, bulletsrect, hudfont, textcol, rend);

	  sprintf (hudtext, "HUNGER %2d/20", hunger);
	  display_string (hudtext, hungerrect, hudfont, textcol, rend);

	  sprintf (hudtext, "THIRST %2d/20", thirst);
	  display_string (hudtext, thirstrect, hudfont, textcol, rend);

	  if (state->args.server_state.is_searching)
	    {
	      loc_char_speed_x = loc_char_speed_y = 0;

	      if (!is_searching)
		{
		  bagcursor = 0, bagswap1 = -1, bagswap2 = -1;
		  is_searching = ntohl (state->args.server_state.is_searching);
		}

	      SDL_RenderCopy (rend, bagtxtr,
			      is_searching == 1 ? &singlebagsrc : &doublebagsrc,
			      is_searching == 1 ? &leftscreen : &screen_dest);

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

	      bagcursordest.x = bagslotsrects [bagcursor].x-3*scaling;
	      bagcursordest.y = bagslotsrects [bagcursor].y-3*scaling;
	      SDL_RenderCopy (rend, bagtxtr, &bagcursorsrc, &bagcursordest);

	      if (bagswap1 >= 0)
		{
		  bagcursordest.x = bagslotsrects [bagswap1].x-3*scaling;
		  bagcursordest.y = bagslotsrects [bagswap1].y-3*scaling;
		  SDL_RenderCopy (rend, bagtxtr, &bagswapsrc, &bagcursordest);
		}

	      display_string (objcaptions [ntohl (state->args.server_state.bag [bagcursor])],
			      objcaptionrect, hudfont, textcol, rend);
	    }
	  else
	    is_searching = 0;

	  SDL_RenderPresent (rend);

	  for (i = 0; i < area->walk_sfxs_num; i++)
	    {
	      if (!loc_char_speed_x && !loc_char_speed_y)
		{
		  if (area->walk_sfxs [i].channel >= 0)
		    {
		      Mix_HaltChannel (area->walk_sfxs [i].channel);
		      area->walk_sfxs [i].channel = -1;
		    }
		}
	      else
		{
		  for (j = 0; j < area->walk_sfxs [i].places_num; j++)
		    {
		      if (RECT_INTERSECT (character_box,
					  area->walk_sfxs [i].places [j]))
			{
			  if (area->walk_sfxs [i].channel < 0)
			    area->walk_sfxs [i].channel
			      = Mix_PlayChannel (-1, area->walk_sfxs [i].sfx, -1);

			  break;
			}
		    }

		  if (j == area->walk_sfxs [i].places_num
		      && area->walk_sfxs [i].channel >= 0)
		    {
		      Mix_HaltChannel (area->walk_sfxs [i].channel);
		      area->walk_sfxs [i].channel = -1;
		    }
		}
	    }

	  if (just_shot == frame_counter)
	    Mix_PlayChannel (-1, shootsfx, 0);

	  if (just_stabbed == frame_counter)
	    Mix_PlayChannel (-1, stabsfx, 0);
	}
    }

  SDL_Quit ();

  return 0;
}
