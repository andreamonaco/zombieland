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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <SDL2/SDL.h>

#include "malloc.h"
#include "zombieland.h"


#define RECT_X_INTERSECT(i,j) ((i).x+(i).w>(j).x&&(j).x+(j).w>(i).x)

#define RECT_Y_INTERSECT(i, j) ((i).y+(i).h>(j).y&&(j).y+(j).h>(i).y)

#define RECT_INTERSECT(x,y) (RECT_X_INTERSECT(x,y) && RECT_Y_INTERSECT(x,y))


#define IS_RECT_CONTAINED(i,j) ((i).x>=(j).x&&(i).x+(i).w<=(j).x+(j).w	\
				&&(i).y>=(j).y&&(i).y+(i).h<=(j).y+(j).h)


#define CHAR_SPEED 2

#define ZOMBIE_SPEED 1


#define SHOOT_REST 10



struct
player
{
  uint32_t id;
  struct agent *agent;

  struct sockaddr_in address;
  uint16_t portoffset;
  uint32_t last_update;

  char name [MAX_LOGNAME_LEN+1];
  int32_t speed_x, speed_y;
  enum facing facing;

  int interact;
  char *textbox;
  int textbox_lines_num;

  int32_t life, shoot_rest;

  int timeout;
};


struct zombie
{
  struct agent *agent;

  enum facing facing;
  int32_t speed_x, speed_y;
  int life;

  struct zombie *next;
};


enum
agent_type
  {
    AGENT_PLAYER,
    AGENT_ZOMBIE
  };


union
agent_data_ptr
{
  struct player *player;
  struct zombie *zombie;
};


struct
agent
{
  enum agent_type type;
  struct server_area *area;
  SDL_Rect place;
  union agent_data_ptr data_ptr;

  struct agent *prev;
  struct agent *next;
};


struct
shot
{
  uint32_t areaid;
  SDL_Rect target;
  int duration;
  struct shot *next;
};


struct
interactible
{
  SDL_Rect place;
  char *text;
  int text_lines_num;
  struct interactible *next;
};


struct
warp
{
  SDL_Rect place;
  struct server_area *dest;
  SDL_Rect spawn;
  struct warp *next;
};


struct
server_area
{
  uint32_t id;
  SDL_Rect walkable;
  SDL_Rect *unwalkables;
  int unwalkables_num;

  struct warp *warps;
  struct interactible *interactibles;

  struct zombie *zombies;
  int zombies_num;
  SDL_Rect *zombie_spawns;
  int zombie_spawns_num;

  struct server_area *next;
};



void
set_rect (SDL_Rect *rect, int x, int y, int w, int h)
{
  rect->x = x;
  rect->y = y;
  rect->w = w;
  rect->h = h;
}


uint32_t
create_player (char name[], struct sockaddr_in *addr, uint16_t portoff,
	       struct server_area *area, struct player pls [],
	       struct agent **agents)
{
  int i;
  struct agent *a;

  for (i = 0; i < MAX_PLAYERS; i++)
    {
      if (pls [i].id == -1)
	break;
    }

  if (i == MAX_PLAYERS)
    return -1;

  a = malloc_and_check (sizeof (*a));
  a->type = AGENT_PLAYER;
  a->area = area;
  set_rect (&a->place, 96, 0, 16, 16);
  a->data_ptr.player = &pls [i];
  a->prev = NULL;
  a->next = *agents;

  if (*agents)
    (*agents)->prev = a;

  *agents = a;

  pls [i].id = i;
  pls [i].agent = a;
  memcpy (&pls [i].address, addr, sizeof (*addr));
  pls [i].address.sin_port = htons (ZOMBIELAND_PORT+portoff);
  pls [i].portoffset = portoff;
  pls [i].last_update = 0;
  strcpy (pls [i].name, name);
  pls [i].speed_x = pls [i].speed_y = pls [i].facing = 0;
  pls [i].interact = 0;
  pls [i].textbox = NULL;
  pls [i].textbox_lines_num = 0;
  pls [i].life = 10;
  pls [i].shoot_rest = 0;
  pls [i].timeout = CLIENT_TIMEOUT;

  return i;
}


struct interactible *
make_interactible_by_grid (int placex, int placey, int placew, int placeh,
			   char *text, struct interactible *next)
{
  struct interactible *ret = malloc_and_check (sizeof (*ret));

  ret->place.x = placex * GRID_CELL_W;
  ret->place.y = placey * GRID_CELL_H;
  ret->place.w = placew * GRID_CELL_W;
  ret->place.h = placeh * GRID_CELL_H;
  ret->text = text;
  ret->text_lines_num = strlen (text) / TEXTLINESIZE;
  ret->next = next;

  return ret;
}


struct warp *
make_warp_by_grid (int placex, int placey, int placew, int placeh,
		   struct server_area *dest, int spawnx, int spawny,
		   struct warp *next)
{
  struct warp *ret = malloc_and_check (sizeof (*ret));

  ret->place.x = placex * GRID_CELL_W;
  ret->place.y = placey * GRID_CELL_H;
  ret->place.w = placew * GRID_CELL_W;
  ret->place.h = placeh * GRID_CELL_H;
  ret->dest = dest;
  ret->spawn.x = spawnx * GRID_CELL_W;
  ret->spawn.y = spawny * GRID_CELL_H;
  ret->next = next;

  return ret;
}


struct zombie *
make_zombie (int placex, int placey, enum facing facing,
	     struct server_area *area, struct zombie *next,
	     struct agent **agents)
{
  struct zombie *ret = malloc_and_check (sizeof (*ret));
  struct agent *a = malloc_and_check (sizeof (*a));

  a->type = AGENT_ZOMBIE;
  a->area = area;
  set_rect (&a->place, placex, placey, GRID_CELL_W, GRID_CELL_H);
  a->data_ptr.zombie = ret;
  a->prev = NULL;
  a->next = *agents;

  if (*agents)
    (*agents)->prev = a;

  *agents = a;

  ret->agent = a;
  ret->facing = facing;
  ret->speed_x = ret->speed_y = 0;
  ret->life = 2;
  ret->next = next;

  return ret;
}


int
is_rect_free (SDL_Rect charbox, int speed_x, int speed_y, SDL_Rect unwalkables [],
	      int unwalkables_num)
{
  int i;

  charbox.x += speed_x;
  charbox.y += speed_y;

  for (i = 0; i < unwalkables_num; i++)
    {
      if (RECT_INTERSECT (charbox, unwalkables [i]))
	return 0;
    }

  return 1;
}


SDL_Rect
check_and_resolve_collision (SDL_Rect charbox, int *speed_x, int *speed_y,
			     SDL_Rect unwalkable, SDL_Rect unwalkables [],
			     int unwalkables_num, int *did_collide)
{
  int new, can_move_x, can_move_y;

  *did_collide = 0;

  if (RECT_INTERSECT (charbox, unwalkable))
    {
      *did_collide = 1;
      charbox.x -= *speed_x;
      charbox.y -= *speed_y;

      if (RECT_X_INTERSECT (charbox, unwalkable))
	{
	  charbox.x += *speed_x;

	  if (*speed_y > 0)
	    {
	      new = unwalkable.y-charbox.h;
	    }
	  else
	    {
	      new = unwalkable.y+unwalkable.h;
	    }

	  *speed_y = new-charbox.y;
	  charbox.y = new;
	}
      else if (RECT_Y_INTERSECT (charbox, unwalkable))
	{
	  charbox.y += *speed_y;

	  if (*speed_x > 0)
	    {
	      new = unwalkable.x-charbox.w;
	    }
	  else
	    {
	      new = unwalkable.x+unwalkable.w;
	    }

	  *speed_x = new-charbox.x;
	  charbox.x = new;
	}
      else
	{
	  can_move_x = is_rect_free (charbox, *speed_x > 0 ? 1 : -1, 0,
				     unwalkables, unwalkables_num);
	  can_move_y = is_rect_free (charbox, 0, *speed_y > 0 ? 1 : -1,
				     unwalkables, unwalkables_num);

	  if (can_move_x && !can_move_y)
	    {
	      charbox.x += *speed_x;

	      if (*speed_y > 0)
		{
		  new = unwalkable.y-charbox.h;
		}
	      else
		{
		  new = unwalkable.y+unwalkable.h;
		}

	      *speed_y = new-charbox.y;
	      charbox.y = new;
	    }
	  else if (!can_move_x && can_move_y)
	    {
	      charbox.y += *speed_y;

	      if (*speed_x > 0)
		{
		  new = unwalkable.x-charbox.w;
		}
	      else
		{
		  new = unwalkable.x+unwalkable.w;
		}

	      *speed_x = new-charbox.x;
	      charbox.x = new;
	    }
	  else
	    {
	      if (*speed_y > 0)
		{
		  new = unwalkable.y-charbox.h;
		}
	      else
		{
		  new = unwalkable.y+unwalkable.h;
		}

	      *speed_y = new-charbox.y;
	      charbox.y = new;

	      if (*speed_x > 0)
		{
		  new = unwalkable.x-charbox.w;
		}
	      else
		{
		  new = unwalkable.x+unwalkable.w;
		}

	      *speed_x = new-charbox.x;
	      charbox.x = new;
	    }
	}
    }

  return charbox;
}


SDL_Rect
check_and_resolve_collisions (SDL_Rect charbox, int *speed_x, int *speed_y,
			      SDL_Rect unwalkables [], int unwalkables_num,
			      int *did_collide)
{
  int i, collided;

  *did_collide = 0;

  for (i = 0; i < unwalkables_num; i++)
    {
      charbox = check_and_resolve_collision (charbox, speed_x, speed_y,
					     unwalkables [i], unwalkables,
					     unwalkables_num, &collided);

      if (collided)
	{
	  *did_collide = 1;
	  return charbox;
	}
    }

  return charbox;
}


SDL_Rect
check_boundary (SDL_Rect charbox, int speed_x, int speed_y, SDL_Rect walkable)
{
  if (charbox.x+charbox.w > walkable.w)
    charbox.x = walkable.w-charbox.w;

  if (charbox.x < 0)
    charbox.x = 0;

  if (charbox.y+charbox.h > walkable.h)
    charbox.y = walkable.h-charbox.h;

  if (charbox.y < 0)
    charbox.y = 0;

  return charbox;
}


SDL_Rect
move_character (SDL_Rect charbox, int speed_x, int speed_y, SDL_Rect walkable,
		SDL_Rect unwalkables [], int unwalkables_num, struct zombie *z,
		int *character_hit)
{
  int collided;

  *character_hit = 0;
  charbox.x += speed_x;
  charbox.y += speed_y;

 restart:
  charbox = check_and_resolve_collisions (charbox, &speed_x, &speed_y, unwalkables,
					  unwalkables_num, &collided);

  if (collided)
    goto restart;

  /*while (z)
    {
      charbox = check_and_resolve_collision (charbox, &speed_x, &speed_y, z->place,
					     &collided);

      if (collided)
	*character_hit = 1;

      z = z->next;
      }*/

  return check_boundary (charbox, speed_x, speed_y, walkable);
}


SDL_Rect
move_zombie (SDL_Rect charbox, int speed_x, int speed_y, SDL_Rect walkable,
	     SDL_Rect unwalkables [], int unwalkables_num)
{
  int collided;

  charbox.x += speed_x;
  charbox.y += speed_y;

  charbox = check_and_resolve_collisions (charbox, &speed_x, &speed_y, unwalkables,
					  unwalkables_num, &collided);

  return check_boundary (charbox, speed_x, speed_y, walkable);
}


int
is_target_hit (SDL_Rect charbox, enum facing facing, SDL_Rect target,
	       SDL_Rect *hitpart)
{
  switch (facing)
    {
    case FACING_DOWN:
      if (charbox.y<target.y
	  && target.x<=charbox.x+charbox.w/2
	  && charbox.x+charbox.w/2 <= target.x+target.w)
	{
	  hitpart->x = charbox.x;
	  hitpart->y = target.y;
	  return 1;
	}
      break;
    case FACING_UP:
      if (charbox.y>target.y
	  && target.x<=charbox.x+charbox.w/2
	  && charbox.x+charbox.w/2 <= target.x+target.w)
	{
	  hitpart->x = charbox.x;
	  hitpart->y = target.y+target.h-GRID_CELL_H;
	  return 1;
	}
      break;
    case FACING_RIGHT:
      if (charbox.x<target.x
	  && target.y<=charbox.y+charbox.h/2
	  && charbox.y+charbox.h/2 <= target.y+target.h)
	{
	  hitpart->x = target.x;
	  hitpart->y = charbox.y;
	  return 1;
	}
      break;
    case FACING_LEFT:
      if (charbox.x>target.x
	  && target.y<=charbox.y+charbox.h/2
	  && charbox.y+charbox.h/2 <= target.y+target.h)
	{
	  hitpart->x = target.x+target.w-GRID_CELL_W;
	  hitpart->y = charbox.y;
	  return 1;
	}
      break;
    }

  return 0;
}


int
is_closer (enum facing facing, SDL_Rect rect1, SDL_Rect rect2)
{
  switch (facing)
    {
    case FACING_DOWN:
      return rect1.y<rect2.y;
    case FACING_UP:
      return rect1.y>rect2.y;
    case FACING_RIGHT:
      return rect1.x<rect2.x;
    case FACING_LEFT:
      return rect1.x>rect2.x;
    }

  return 0;
}


SDL_Rect
get_shot_rect (SDL_Rect charbox, enum facing facing, struct server_area *area,
	       struct agent *as, struct agent **shotag)
{
  int i, found = 0;
  SDL_Rect ret, hitpart;

  *shotag = NULL;

  for (i = 0; i < area->unwalkables_num; i++)
    {
      if (is_target_hit (charbox, facing, area->unwalkables [i], &hitpart))
	{
	  if (!found || is_closer (facing, hitpart, ret))
	    {
	      found = 1;
	      ret = hitpart;
	    }
	}
    }

  while (as)
    {
      if (area == as->area
	  && is_target_hit (charbox, facing, as->place, &hitpart))
	{
	  if (!found || is_closer (facing, hitpart, ret))
	    {
	      found = 1;
	      *shotag = as;
	      ret = hitpart;
	    }
	}

      as = as->next;
    }

  if (found)
    {
      ret.w = GRID_CELL_W;
      ret.h = GRID_CELL_H;
      return ret;
    }

  switch (facing)
    {
    case FACING_DOWN:
      ret.x = charbox.x;
      ret.y = area->walkable.h;
      break;
    case FACING_UP:
      ret.x = charbox.x;
      ret.y = -GRID_CELL_H;
      break;
    case FACING_RIGHT:
      ret.x = area->walkable.w;
      ret.y = charbox.y;
      break;
    case FACING_LEFT:
      ret.x = -GRID_CELL_W;
      ret.y = charbox.y;
      break;
    }

  return ret;
}


int
does_character_face_object (SDL_Rect character, enum facing facing,
			    SDL_Rect square)
{
  switch (facing)
    {
    case FACING_DOWN:
      if (character.x > square.x-square.w/2 && character.x < square.x+square.w*3/2
	  && character.y+character.h == square.y)
	return 1;
      break;
    case FACING_UP:
      if (character.x > square.x-square.w/2 && character.x < square.x+square.w*3/2
	  && character.y == square.y+square.h)
	return 1;
      break;
    case FACING_RIGHT:
      if (character.y > square.y-square.h/2 && character.y < square.y+square.h*3/2
	  && character.x+character.w == square.x)
	return 1;
      break;
    case FACING_LEFT:
      if (character.y > square.y-square.h/2 && character.y < square.y+square.h*3/2
	  && character.x == square.x+square.w)
	return 1;
      break;
    }

  return 0;
}


void
send_server_state (int sockfd, uint32_t frame_counter, int id, struct player *pls,
		   struct agent *as, struct shot *ss)
{
  char buf [MAXMSGSIZE];
  struct message *msg = (struct message *) &buf;
  struct visible vis;

  msg->type = htonl (MSG_SERVER_STATE);
  msg->args.server_state.frame_counter = frame_counter;
  msg->args.server_state.areaid = pls [id].agent->area->id;
  msg->args.server_state.x = pls [id].agent->place.x;
  msg->args.server_state.y = pls [id].agent->place.y;
  msg->args.server_state.w = pls [id].agent->place.w;
  msg->args.server_state.h = pls [id].agent->place.h;
  msg->args.server_state.char_facing = pls [id].facing;
  msg->args.server_state.life = pls [id].life;
  msg->args.server_state.num_visibles = 0;
  msg->args.server_state.textbox_lines_num = pls [id].textbox_lines_num;

  while (as)
    {
      if (sizeof (msg)+(msg->args.server_state.num_visibles+1)
	  *sizeof (struct visible) > MAXMSGSIZE)
	break;

      if ((as->type != AGENT_PLAYER || as->data_ptr.player != &pls [id])
	  && as->area == pls [id].agent->area)
	{
	  vis.type = as->type == AGENT_PLAYER ? VISIBLE_PLAYER : VISIBLE_ZOMBIE;
	  vis.x = as->place.x;
	  vis.y = as->place.y;
	  vis.w = as->place.w;
	  vis.h = as->place.h;

	  if (as->type == AGENT_PLAYER)
	    {
	      vis.facing = as->data_ptr.player->facing;
	      vis.speed_x = as->data_ptr.player->speed_x;
	      vis.speed_y = as->data_ptr.player->speed_y;
	    }
	  else
	    {
	      vis.facing = as->data_ptr.zombie->facing;
	      vis.speed_x = as->data_ptr.zombie->speed_x;
	      vis.speed_y = as->data_ptr.zombie->speed_y;
	    }

	  memcpy (&buf [sizeof (*msg)+msg->args.server_state.num_visibles
			*sizeof (vis)], &vis, sizeof (vis));
	  msg->args.server_state.num_visibles++;
	}

      as = as->next;
    }

  while (ss)
    {
      if (sizeof (msg)+msg->args.server_state.num_visibles*sizeof (struct visible)
	  > MAXMSGSIZE)
	break;

      if (ss->areaid == pls [id].agent->area->id)
	{
	  vis.type = VISIBLE_SHOT;
	  vis.x = ss->target.x;
	  vis.y = ss->target.y;
	  vis.w = ss->target.w;
	  vis.h = ss->target.h;
	  memcpy (&buf [sizeof (*msg)+msg->args.server_state.num_visibles
			*sizeof (vis)], &vis, sizeof (vis));
	  msg->args.server_state.num_visibles++;
	}

      ss = ss->next;
    }

  if (pls [id].textbox
      && sizeof (msg)+msg->args.server_state.num_visibles*sizeof (struct visible)
      +msg->args.server_state.textbox_lines_num*TEXTLINESIZE+1 <= MAXMSGSIZE)
    {
      strcpy (&buf [sizeof (*msg)+msg->args.server_state.num_visibles
		    *sizeof (vis)], pls [id].textbox);
    }
  else
    msg->args.server_state.textbox_lines_num = 0;

  if (sendto (sockfd, buf,
	      sizeof (*msg)+msg->args.server_state.num_visibles*sizeof (vis)
	      +msg->args.server_state.textbox_lines_num*TEXTLINESIZE+1, 0,
	      (struct sockaddr *)&pls [id].address, sizeof (pls [id].address)) < 0)
    {
      fprintf (stderr, "could not send data\n");
      exit (1);
    }
}


void
print_welcome_message (void)
{
  puts ("zombieland server " PACKAGE_VERSION "\n"
	"Copyright (C) 2025 Andrea Monaco\n"
	"License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/"
	"gpl.html>\n"
	"This is free software: you are free to change and redistribute it.\n"
	"There is NO WARRANTY, to the extent permitted by law.\n");
}



int
main (int argc, char *argv[])
{
  struct agent *agents = NULL, *shotag;
  struct player players [MAX_PLAYERS];
  struct zombie *z, *prz;
  struct shot *shots = NULL, *s, *prs;

  int sockfd;
  fd_set fdset;
  struct timeval timeout = {0};
  char buffer [MAXMSGSIZE];
  ssize_t recvlen;
  struct sockaddr_in local_addr, client_addr;
  socklen_t client_addr_sz = sizeof (client_addr);

  struct message *msg;

  struct interactible *in;
  struct warp *w;

  struct server_area field = {0}, *area;
  SDL_Rect field_walkable = {0, 0, 512, 512},
    field_unwalkables [] = {RECT_BY_GRID (1, 3, 4, 4),
    RECT_BY_GRID (1, 10, 3, 3), RECT_BY_GRID (10, 10, 2, 4),
    RECT_BY_GRID (13, 10, 2, 4), RECT_BY_GRID (12, 10, 1, 2),
    RECT_BY_GRID (2, 0, 2, 2), RECT_BY_GRID (6, 1, 2, 2),
    RECT_BY_GRID (7, 4, 2, 2), RECT_BY_GRID (10, 0, 3, 1),
    RECT_BY_GRID (7, 7, 8, 1), RECT_BY_GRID (7, 9, 1, 5),
    RECT_BY_GRID (27, 1, 2, 2), RECT_BY_GRID (29, 2, 2, 2),
    RECT_BY_GRID (13, 15, 4, 1), RECT_BY_GRID (18, 10, 1, 2),
    RECT_BY_GRID (19, 12, 1, 2), RECT_BY_GRID (20, 14, 1, 2),
    RECT_BY_GRID (21, 8, 1, 2), RECT_BY_GRID (25, 7, 1, 1),
    RECT_BY_GRID (0, 18, 5, 1), RECT_BY_GRID (7, 19, 1, 1),
    RECT_BY_GRID (22, 18, 1, 1), RECT_BY_GRID (10, 24, 3, 1),
    RECT_BY_GRID (20, 22, 1, 2), RECT_BY_GRID (20, 26, 2, 6),
    RECT_BY_GRID (24, 22, 2, 2), RECT_BY_GRID (24, 25, 2, 2),
    RECT_BY_GRID (24, 28, 2, 2), RECT_BY_GRID (28, 26, 2, 2),
    RECT_BY_GRID (27, 25, 3, 1), RECT_BY_GRID (30, 30, 2, 1),
    RECT_BY_GRID (21, 20, 6, 1), RECT_BY_GRID (1, 21, 1, 1),
    RECT_BY_GRID (2, 25, 1, 1), RECT_BY_GRID (6, 25, 1, 1),
    RECT_BY_GRID (6, 29, 1, 1), RECT_BY_GRID (2, 27, 3, 3),
    RECT_BY_GRID (16, 11, 1, 3), RECT_BY_GRID (26, 14, 3, 1),
    RECT_BY_GRID (28, 16, 3, 1), RECT_BY_GRID (1, 14, 2, 1),
    RECT_BY_GRID (2, 15, 1, 2), RECT_BY_GRID (3, 16, 1, 1)},
    field_zombie_spawns [] = {RECT_BY_GRID (13, 31, 1, 1),
    RECT_BY_GRID (31, 22, 1, 1), RECT_BY_GRID (16, 0, 1, 1),
    RECT_BY_GRID (0, 23, 1, 1)};

  struct server_area room = {0};
  SDL_Rect room_walkable = RECT_BY_GRID (0, 0, 12, 12),
    room_unwalkables [] = {RECT_BY_GRID (1, 6, 1, 3),
    RECT_BY_GRID (7, 2, 3, 3), RECT_BY_GRID (7, 5, 1, 2),
    RECT_BY_GRID (0, 11, 5, 1), RECT_BY_GRID (7, 11, 5, 1)};

  SDL_Event event;

  uint32_t frame_counter = 1, id;
  int char_hit, quit = 0, i, spawn_counter = 0;
  Uint32 t1, t2;
  double delay;


  for (i = 0; i < MAX_PLAYERS; i++)
    {
      players [i].id = -1;
    }

  print_welcome_message ();

  if (SDL_Init (SDL_INIT_VIDEO) < 0)
    {
      fprintf (stderr, "could not initialise SDL: %s\n", SDL_GetError ());
      return 1;
    }

  sockfd = socket (AF_INET, SOCK_DGRAM, 0);

  if (sockfd < 0)
    {
      fprintf (stderr, "could not open socket\n");
      return 1;
    }

  bzero ((char *) &local_addr, sizeof(local_addr));
  local_addr.sin_family = AF_INET;
  local_addr.sin_port = htons (ZOMBIELAND_PORT);
  local_addr.sin_addr.s_addr = INADDR_ANY;

  if (bind (sockfd, (struct sockaddr *) &local_addr, sizeof (local_addr)) < 0)
    {
      fprintf (stderr, "could not bind socket, maybe another program is bound to "
	       "the same port?\n");
      return 1;
    }

  printf ("listening on port %d...\n", ZOMBIELAND_PORT);

  field.id = 0;
  field.walkable = field_walkable;
  field.unwalkables = field_unwalkables;
  field.unwalkables_num = 43;
  field.warps = make_warp_by_grid (12, 13, 1, 1, &room, 5, 11, NULL);
  field.interactibles = NULL;
  field.zombies = NULL;
  field.zombies_num = 0;
  field.zombie_spawns = field_zombie_spawns;
  field.zombie_spawns_num = 4;
  field.next = &room;

  room.id = 1;
  room.walkable = room_walkable;
  room.unwalkables = room_unwalkables;
  room.unwalkables_num = 5;
  room.warps = make_warp_by_grid (5, 11, 2, 1, &field, 12, 14, NULL);
  room.interactibles = make_interactible_by_grid
    (1, 6, 1, 3, "Can't sleep now!              "
     "There might be zombies around." "Better take a look            ", NULL);
  room.zombies = NULL;
  room.zombies_num = 0;
  room.zombie_spawns = NULL;
  room.zombie_spawns_num = 0;
  room.next = NULL;

  srand (time (NULL));

  while (!quit)
    {
      t1 = SDL_GetTicks ();

      while (SDL_PollEvent (&event))
	{
	  switch (event.type)
	    {
	    case SDL_QUIT:
	      quit = 1;
	      break;
	    }
	}

      while (1)
	{
	get_new_message:
	  FD_ZERO (&fdset);
	  FD_SET (sockfd, &fdset);

	  if (select (sockfd+1, &fdset, NULL, NULL, &timeout) < 0)
	    {
	      fprintf (stderr, "could not poll socket\n");
	      return 1;
	    }

	  if (!FD_ISSET (sockfd, &fdset))
	    break;

	  recvlen =
	    recvfrom (sockfd, buffer, MAXMSGSIZE, 0,
		      (struct sockaddr *) &client_addr, &client_addr_sz);

	  if (recvlen < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
	    break;

	  if (recvlen < 0)
	    {
	      fprintf (stderr, "could not receive data from the client\n");
	      return 1;
	    }

	  if (recvlen < 5)
	    {
	      fprintf (stderr, "got a message too short from client\n");
	      return 1;
	    }

	  if (recvlen > sizeof (*msg))
	    {
	      fprintf (stderr, "got a message too long from client\n");
	      return 1;
	    }

	  msg = (struct message *) buffer;
	  msg->type = ntohl (msg->type);

	  switch (msg->type)
	    {
	    case MSG_LOGIN:
	      msg->args.login.logname [MAX_LOGNAME_LEN] = 0;

	      for (i = 0; i < MAX_PLAYERS; i++)
		{
		  if (players [i].id != -1
		      && !strcmp (players [i].name, msg->args.login.logname))
		    {
		      fprintf (stderr, "username %s already log in\n",
			       msg->args.login.logname);
		      send_message (sockfd, &client_addr,
				    ntohs (msg->args.login.portoff),
				    MSG_LOGNAME_IN_USE);
		      goto get_new_message;
		    }
		}

	      id = create_player (msg->args.login.logname, &client_addr,
				  ntohs (msg->args.login.portoff), &field,
				  players, &agents);

	      if (id == -1)
		{
		  fprintf (stderr,
			   "client tried login but there are too many players\n");
		  send_message (sockfd, &client_addr,
				ntohs (msg->args.login.portoff), MSG_SERVER_FULL);
		  break;
		}

	      printf ("created player %s with port offset %d\n",
		      msg->args.login.logname, players [id].portoffset);

	      msg->type = htonl (MSG_LOGINOK);
	      msg->args.loginok.id = htonl (id);

	      client_addr.sin_port = htons (ZOMBIELAND_PORT
					    +players [id].portoffset);

	      if (sendto (sockfd, (char *)msg, sizeof (*msg), 0,
			  (struct sockaddr *) &client_addr, client_addr_sz) < 0)
		{
		  fprintf (stderr, "could not send data\n");
		  return 1;
		}
	      break;
	    case MSG_CLIENT_CHAR_STATE:
	      id = msg->args.client_char_state.id;

	      if (players [id].id == -1)
		{
		  fprintf (stderr, "got state from unknown id %d\n", id);
		}
	      else if (players [id].last_update
		       < msg->args.client_char_state.frame_counter)
		{
		  players [id].speed_x =
		    msg->args.client_char_state.char_speed_x > 0 ? CHAR_SPEED
		    : msg->args.client_char_state.char_speed_x < 0 ? -CHAR_SPEED
		    : 0;
		  players [id].speed_y =
		    msg->args.client_char_state.char_speed_y > 0 ? CHAR_SPEED
		    : msg->args.client_char_state.char_speed_y < 0 ? -CHAR_SPEED
		    : 0;
		  players [id].facing = msg->args.client_char_state.char_facing;

		  players [id].interact = msg->args.client_char_state.do_interact;

		  if (!players [id].interact && !players [id].shoot_rest
		      && msg->args.client_char_state.do_shoot)
		    players [id].shoot_rest = SHOOT_REST;

		  players [id].last_update
		    = msg->args.client_char_state.frame_counter;
		  players [id].timeout = CLIENT_TIMEOUT;
		}
	      break;
	    default:
	      fprintf (stderr, "message type not known (%d)\n", msg->type);
	      return 1;
	    }
	}


      area = &field;

      while (area)
	{
	  z = area->zombies;

	  while (z)
	    {
	      z->speed_x = (rand () % 3 - 1)*ZOMBIE_SPEED;
	      z->facing = z->speed_x > 0 ? FACING_RIGHT
		: z->speed_x < 0 ? FACING_LEFT : z->facing;

	      z->speed_y = (rand () % 3 - 1)*ZOMBIE_SPEED;
	      z->facing = z->speed_y > 0 ? FACING_DOWN
		: z->speed_y < 0 ? FACING_UP : z->facing;

	      z = z->next;
	    }

	  area = area->next;
	}

      if (spawn_counter == SPAWN_INTERVAL)
	{
	  spawn_counter = 0;

	  area = &field;

	  while (area)
	    {
	      if (area->zombie_spawns_num && area->zombies_num < MAX_ZOMBIES)
		{
		  i = rand () % area->zombie_spawns_num;
		  area->zombies = make_zombie (area->zombie_spawns [i].x,
					       area->zombie_spawns [i].y,
					       FACING_DOWN, area, area->zombies,
					       &agents);
		  area->zombies_num++;
		}

	      area = area->next;
	    }
	}


      for (i = 0; i < MAX_PLAYERS; i++)
	{
	  if (players [i].id == -1)
	    continue;

	  players [i].agent->place =
	    move_character (players [i].agent->place, players [i].speed_x,
			    players [i].speed_y, players [i].agent->area->walkable,
			    players [i].agent->area->unwalkables,
			    players [i].agent->area->unwalkables_num, NULL,
			    &char_hit);

	  if (players [i].interact)
	    {
	      in = players [i].agent->area->interactibles;

	      while (in)
		{
		  if (does_character_face_object (players [i].agent->place,
						  players [i].facing, in->place))
		    {
		      players [i].textbox = in->text;
		      players [i].textbox_lines_num = in->text_lines_num;
		      break;
		    }

		  in = in->next;
		}

	      players [i].interact = 0;
	    }

	  if (players [i].shoot_rest == SHOOT_REST)
	    {
	      s = malloc_and_check (sizeof (*s));
	      s->areaid = players [i].agent->area->id;
	      s->target = get_shot_rect (players [i].agent->place,
					 players [i].facing,
					 players [i].agent->area, agents,
					 &shotag);
	      s->duration = 10;
	      s->next = shots;
	      shots = s;

	      if (shotag)
		{
		  if (shotag->type == AGENT_PLAYER)
		    shotag->data_ptr.player->life--;
		  else
		    shotag->data_ptr.zombie->life--;
		}
	    }

	  if (players [i].shoot_rest)
	    players [i].shoot_rest--;

	  s = shots, prs = NULL;

	  while (s)
	    {
	      s->duration--;

	      if (!s->duration)
		{
		  if (prs)
		    prs->next = s->next;
		  else
		    shots = s->next;

		  free (s);
		  s = prs ? prs->next : shots;
		}
	      else
		{
		  prs = s;
		  s = s->next;
		}
	    }

	  w = players [i].agent->area->warps;

	  while (w)
	    {
	      if (IS_RECT_CONTAINED (players [i].agent->place, w->place))
		{
		  players [i].agent->area = w->dest;
		  players [i].agent->place.x = w->spawn.x;
		  players [i].agent->place.y = w->spawn.y;
		  break;
		}

	      w = w->next;
	    }
	}

      area = &field;

      while (area)
	{
	  z = area->zombies, prz = NULL;

	  while (z)
	    {
	      if (z->life <= 0)
		{
		  if (prz)
		    prz->next = z->next;
		  else
		    area->zombies = z->next;

		  if (z->agent->prev)
		    z->agent->prev->next = z->agent->next;
		  else
		    agents = z->agent->next;

		  if (z->agent->next)
		    z->agent->next->prev = z->agent->prev;

		  free (z->agent);
		  free (z);

		  area->zombies_num--;

		  z = prz ? prz->next : area->zombies;
		}
	      else
		{
		  z->agent->place = move_zombie (z->agent->place, z->speed_x,
						 z->speed_y, area->walkable,
						 area->unwalkables,
						 area->unwalkables_num);

		  prz = z;
		  z = z->next;
		}
	    }

	  area = area->next;
	}

      for (i = 0; i < MAX_PLAYERS; i++)
	{
	  if (players [i].id != -1 && players [i].life <= 0)
	    {
	      send_message (sockfd, &players [i].address, -1, MSG_PLAYER_DIED);
	      players [i].id = -1;

	      if (players [i].agent->prev)
		players [i].agent->prev->next = players [i].agent->next;
	      else
		agents = players [i].agent->next;

	      if (players [i].agent->next)
		players [i].agent->next->prev = players [i].agent->prev;

	      free (players [i].agent);
	    }
	  else if (players [i].id != -1)
	    {
	      send_server_state (sockfd, frame_counter, i, players, agents,
				 shots);

	      players [i].textbox = NULL;
	      players [i].textbox_lines_num = 0;
	    }
	}

      for (i = 0; i < MAX_PLAYERS; i++)
	{
	  if (players [i].id != -1)
	    {
	      players [i].timeout--;

	      if (!players [i].timeout)
		{
		  printf ("player %s disconnected due to timeout\n",
			  players [i].name);
		  players [i].id = -1;

		  if (players [i].agent->prev)
		    players [i].agent->prev->next = players [i].agent->next;
		  else
		    agents = players [i].agent->next;

		  free (players [i].agent);
		}
	    }
	}

      spawn_counter++;

      frame_counter++;

      t2 = SDL_GetTicks ();

      delay = FRAME_DURATION - (t2 - t1);

      if (delay > 0)
	SDL_Delay (delay);
      else
	printf ("warning: frame skipped\n");
    }

  SDL_Quit ();

  return 0;
}
