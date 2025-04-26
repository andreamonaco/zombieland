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


#define SIGN(x) ((x) > 0 ? 1 : -1)


#define CHAR_SPEED 2
#define ZOMBIE_SPEED 1


#define MAX_ZOMBIE_HEALTH 12

#define TOUCH_DAMAGE 1
#define STAB_DAMAGE  2
#define SHOOT_DAMAGE 6

#define IMMORTAL_DURATION 20

#define SHOOT_REST 10
#define STAB_REST 5


#define MAX_HUNGER 20
#define HUNGER_UP 1800
#define MAX_THIRST 20
#define THIRST_UP 1800


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

  int32_t bullets;

  uint32_t hunger, hunger_up, thirst, thirst_up;

  int interact;
  int npcid;
  char *textbox;
  int textbox_lines_num;

  int freeze;

  int shoot_rest;
  int stab_rest;

  int timeout;
};


struct zombie
{
  struct agent *agent;

  enum facing facing;

  int32_t speed_x, speed_y;
  int freeze;

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
  struct server_area *area;
  SDL_Rect place;

  int32_t life;
  int immortal;

  enum agent_type type;
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


enum
object_type
  {
    OBJECT_NONE,
    OBJECT_HEALTH,
    OBJECT_AMMO,
    OBJECT_FOOD,
    OBJECT_WATER
  };


struct
object
{
  struct server_area *area;
  SDL_Rect place;
  enum object_type type;
  struct object_spawn *spawn;
  struct object *next;
};


struct
object_spawn
{
  SDL_Rect place;
  struct object *content;
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
  struct interactible *npcs;

  struct zombie *zombies;
  int zombies_num;
  SDL_Rect *zombie_spawns;
  int zombie_spawns_num;

  struct object_spawn *object_spawns;
  int object_spawns_num, free_object_spawns_num;

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
  a->area = area;
  set_rect (&a->place, 96, 0, 16, 16);
  a->life = MAX_PLAYER_HEALTH;
  a->immortal = 0;
  a->type = AGENT_PLAYER;
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
  pls [i].bullets = 16;
  pls [i].hunger = 0;
  pls [i].hunger_up = HUNGER_UP;
  pls [i].thirst = 0;
  pls [i].thirst_up = THIRST_UP;
  pls [i].interact = 0;
  pls [i].textbox = NULL;
  pls [i].textbox_lines_num = 0;
  pls [i].freeze = 0;
  pls [i].shoot_rest = 0;
  pls [i].stab_rest = 0;
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

  a->area = area;
  set_rect (&a->place, placex, placey, GRID_CELL_W, GRID_CELL_H);
  a->life = MAX_ZOMBIE_HEALTH;
  a->immortal = 0;
  a->type = AGENT_ZOMBIE;
  a->data_ptr.zombie = ret;
  a->prev = NULL;
  a->next = *agents;

  if (*agents)
    (*agents)->prev = a;

  *agents = a;

  ret->agent = a;
  ret->facing = facing;
  ret->speed_x = ret->speed_y = 0;
  ret->freeze = 0;
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
move_character (struct player *pl, SDL_Rect walkable, SDL_Rect unwalkables [],
		int unwalkables_num, struct zombie *zs, int *character_hit)
{
  int collided, speed_x = pl->speed_x, speed_y = pl->speed_y;
  struct zombie *z;
  SDL_Rect charbox = pl->agent->place;

  *character_hit = 0;
  charbox.x += speed_x;
  charbox.y += speed_y;

 restart:
  charbox = check_and_resolve_collisions (charbox, &speed_x, &speed_y, unwalkables,
					  unwalkables_num, &collided);

  if (collided)
    goto restart;

  z = zs;

  while (z)
    {
      charbox = check_and_resolve_collision (charbox, &speed_x, &speed_y,
					     z->agent->place, NULL, 0,
					     &collided);

      if (collided)
	{
	  if (!pl->agent->immortal)
	    {
	      pl->agent->immortal = IMMORTAL_DURATION;
	      pl->agent->life -= TOUCH_DAMAGE;
	      pl->freeze = 6;
	      pl->speed_x = -pl->speed_x*2;
	      pl->speed_y = -pl->speed_y*2;
	    }

	  *character_hit = 1;
	  goto restart;
	}

      z = z->next;
    }

  return check_boundary (charbox, speed_x, speed_y, walkable);
}


SDL_Rect
move_zombie (SDL_Rect charbox, struct server_area *area, int speed_x, int speed_y,
	     SDL_Rect walkable, SDL_Rect unwalkables [], int unwalkables_num,
	     struct player pls [])
{
  int collided, i, sx = speed_x, sy = speed_y;

  charbox.x += speed_x;
  charbox.y += speed_y;

 restart:
  charbox = check_and_resolve_collisions (charbox, &speed_x, &speed_y, unwalkables,
					  unwalkables_num, &collided);

  if (collided)
    goto restart;

  for (i = 0; i < MAX_PLAYERS; i++)
    {
      if (pls [i].id == -1 || area != pls [i].agent->area)
	continue;

      charbox = check_and_resolve_collision (charbox, &speed_x, &speed_y,
					     pls [i].agent->place, NULL, 0,
					     &collided);

      if (collided)
	{
	  if (!pls [i].agent->immortal)
	    {
	      pls [i].agent->immortal = IMMORTAL_DURATION;
	      pls [i].agent->life -= TOUCH_DAMAGE;
	      pls [i].freeze = 6;
	      pls [i].speed_x = sx*4;
	      pls [i].speed_y = sy*4;
	    }

	  goto restart;
	}
    }

  return check_boundary (charbox, speed_x, speed_y, walkable);
}


int
is_target_hit (SDL_Rect charbox, enum facing facing, SDL_Rect target,
	       int is_agent, SDL_Rect *hitpart)
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
      if ((charbox.y>target.y || (!is_agent && charbox.y==target.y))
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
      if ((charbox.x>target.x || (!is_agent && charbox.x==target.x))
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
      if (is_target_hit (charbox, facing, area->unwalkables [i], 0, &hitpart))
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
	  && is_target_hit (charbox, facing, as->place, 1, &hitpart))
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
    }
  else switch (facing)
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

  switch (facing)
    {
    case FACING_DOWN:
      ret.y -= 8;
      break;
    case FACING_UP:
      ret.y += 8;
      break;
    case FACING_RIGHT:
      ret.x -= 8;
      break;
    case FACING_LEFT:
      ret.x += 8;
      break;
    }

  return ret;
}


struct agent *
get_stabbed_agent (SDL_Rect charbox, enum facing facing,
		   struct server_area *area, struct agent *as, int *speed_x,
		   int *speed_y)
{
  struct agent *ret = NULL;
  int dist, shift, retdist, retshift;

  while (as)
    {
      if (as->area == area)
	{
	  switch (facing)
	    {
	    case FACING_DOWN:
	    case FACING_UP:
	      dist = abs (charbox.y-as->place.y);
	      shift = as->place.x-charbox.x;
	      break;
	    case FACING_RIGHT:
	    case FACING_LEFT:
	      dist = abs (charbox.x-as->place.x);
	      shift = as->place.y-charbox.y;
	      break;
	    }

	  if (0 < dist && dist < 20 && abs (shift) < 8)
	    {
	      if (!ret || (dist < retdist && abs (shift) < abs (retshift)))
		{
		  ret = as;
		  retdist = dist;
		  retshift = shift;
		}
	    }
	}

      as = as->next;
    }

  if (ret)
    {
      switch (facing)
	{
	case FACING_DOWN:
	case FACING_UP:
	  *speed_y = 4 * (facing == FACING_UP ? -1 : 1);
	  *speed_x = abs (retshift) < 2 ? 0
	    : abs (retshift) < 4 ? SIGN (retshift)
	    : abs (retshift) < 6 ? 2 * SIGN (retshift) : 3 * SIGN (retshift);
	  break;
	case FACING_RIGHT:
	case FACING_LEFT:
	  *speed_x = 4 * (facing == FACING_LEFT ? -1 : 1);
	  *speed_y = abs (retshift) < 2 ? 0
	    : abs (retshift) < 4 ? SIGN (retshift)
	    : abs (retshift) < 6 ? 2 * SIGN (retshift) : 3 * SIGN (retshift);
	  break;
	}
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


int
does_agent_take_object (SDL_Rect charbox, SDL_Rect objbox)
{
  SDL_Rect inters;

  return SDL_IntersectRect (&charbox, &objbox, &inters)
    && inters.w > GRID_CELL_W/2 && inters.h > GRID_CELL_H/2;
}


void
send_server_state (int sockfd, uint32_t frame_counter, int id, struct player *pls,
		   struct agent *as, struct shot *ss, struct object *objs)
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
  msg->args.server_state.life = pls [id].agent->life;
  msg->args.server_state.bullets = pls [id].bullets;
  msg->args.server_state.hunger = pls [id].hunger;
  msg->args.server_state.thirst = pls [id].thirst;
  msg->args.server_state.num_visibles = 0;
  msg->args.server_state.npcid = pls [id].npcid;
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

  while (objs)
    {
      if (sizeof (msg)+msg->args.server_state.num_visibles*sizeof (struct visible)
	  > MAXMSGSIZE)
	break;

      if (objs->area == pls [id].agent->area)
	{
	  switch (objs->type)
	    {
	    case OBJECT_HEALTH:
	      vis.type = VISIBLE_HEALTH;
	      break;
	    case OBJECT_AMMO:
	      vis.type = VISIBLE_AMMO;
	      break;
	    case OBJECT_FOOD:
	      vis.type = VISIBLE_FOOD;
	      break;
	    case OBJECT_WATER:
	      vis.type = VISIBLE_WATER;
	      break;
	    default:
	      continue;
	    }

	  vis.x = objs->place.x;
	  vis.y = objs->place.y;
	  vis.w = objs->place.w;
	  vis.h = objs->place.h;

	  memcpy (&buf [sizeof (*msg)+msg->args.server_state.num_visibles
			*sizeof (vis)], &vis, sizeof (vis));
	  msg->args.server_state.num_visibles++;
	}

      objs = objs->next;
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
  struct agent *agents = NULL, *shotag, *stabbed;
  struct player players [MAX_PLAYERS];
  struct zombie *z, *prz;
  struct shot *shots = NULL, *s, *prs;
  struct object *objects = NULL, *obj, *probj;

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
    RECT_BY_GRID (2, 15, 1, 2), RECT_BY_GRID (3, 16, 1, 1),
    RECT_BY_GRID (5, 14, 0, 4), RECT_BY_GRID (21, 17, 0, 3),
    RECT_BY_GRID (27, 21, 0, 4), RECT_BY_GRID (30, 26, 0, 4)},
    field_zombie_spawns [] = {RECT_BY_GRID (13, 31, 1, 1),
    RECT_BY_GRID (31, 22, 1, 1), RECT_BY_GRID (16, 0, 1, 1),
    RECT_BY_GRID (0, 23, 1, 1)};

  struct server_area room = {0};
  SDL_Rect room_walkable = RECT_BY_GRID (0, 0, 12, 12),
    room_unwalkables [] = {RECT_BY_GRID (1, 6, 1, 3),
    RECT_BY_GRID (7, 2, 3, 3), RECT_BY_GRID (7, 5, 1, 1),
    RECT_BY_GRID (0, 11, 5, 1), RECT_BY_GRID (7, 11, 5, 1),
    RECT_BY_GRID (7, 7, 1, 1), RECT_BY_GRID (3, 9, 1, 2),
    RECT_BY_GRID (8, 9, 1, 2), RECT_BY_GRID (10, 8, 0, 2),
    RECT_BY_GRID (10, 10, 2, 0)};
  struct object_spawn room_object_spawns [] = {{RECT_BY_GRID (1, 1, 1, 1), NULL},
					       {RECT_BY_GRID (3, 1, 1, 1), NULL}};

  struct server_area basement = {0};
  SDL_Rect basement_walkable = RECT_BY_GRID (0, 0, 12, 11),
    basement_unwalkables [] = {RECT_BY_GRID (1, 0, 7, 2),
    RECT_BY_GRID (1, 4, 7, 2), RECT_BY_GRID (1, 8, 7, 2),
    RECT_BY_GRID (9, 0, 3, 3), RECT_BY_GRID (10, 7, 2, 0),
    RECT_BY_GRID (10, 7, 0, 3)};

  SDL_Event event;

  uint32_t frame_counter = 1, id;
  int char_hit, quit = 0, i, j, speedx, speedy, zombie_spawn_counter = 0,
    object_spawn_counter = 0;
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
  field.unwalkables_num = 47;
  field.warps = make_warp_by_grid (12, 13, 1, 1, &room, 5, 11, NULL);
  field.interactibles = NULL;
  field.npcs = NULL;
  field.zombies = NULL;
  field.zombies_num = 0;
  field.zombie_spawns = field_zombie_spawns;
  field.zombie_spawns_num = 4;
  field.object_spawns = NULL;
  field.object_spawns_num = field.free_object_spawns_num = 0;
  field.next = &room;

  room.id = 1;
  room.walkable = room_walkable;
  room.unwalkables = room_unwalkables;
  room.unwalkables_num = 10;
  room.warps = make_warp_by_grid (5, 11, 2, 1, &field, 12, 14,
				  make_warp_by_grid (10, 8, 2, 2, &basement, 10,
						     10, NULL));
  room.interactibles = make_interactible_by_grid
    (1, 6, 1, 3, "Can't sleep now!              "
     "There might be zombies around." "Better take a look            ", NULL);
  room.npcs = make_interactible_by_grid (7, 7, 1, 1,
					 "At that corner you will find  "
					 "health and ammo.              "
					 "If you have some patience,    "
					 "they will respawn.            ", NULL);
  room.zombies = NULL;
  room.zombies_num = 0;
  room.zombie_spawns = NULL;
  room.zombie_spawns_num = 0;
  room.object_spawns = room_object_spawns;
  room.object_spawns_num = room.free_object_spawns_num = 2;
  room.next = &basement;

  basement.id = 2;
  basement.walkable = basement_walkable;
  basement.unwalkables = basement_unwalkables;
  basement.unwalkables_num = 6;
  basement.warps = make_warp_by_grid (10, 7, 2, 3, &room, 10, 7, NULL);

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
		  if (!players [id].freeze)
		    {
		      players [id].speed_x =
			msg->args.client_char_state.char_speed_x > 0 ? CHAR_SPEED
			: msg->args.client_char_state.char_speed_x < 0
			? -CHAR_SPEED : 0;
		      players [id].speed_y =
			msg->args.client_char_state.char_speed_y > 0 ? CHAR_SPEED
			: msg->args.client_char_state.char_speed_y < 0
			? -CHAR_SPEED : 0;
		      players [id].facing
			= msg->args.client_char_state.char_facing;

		      players [id].interact
			= msg->args.client_char_state.do_interact;

		      if (msg->args.client_char_state.do_shoot
			  && !players [id].interact && players [id].bullets
			  && !players [id].shoot_rest)
			{
			  players [id].shoot_rest = SHOOT_REST;
			}

		      if (msg->args.client_char_state.do_stab
			  && !players [id].interact && !players [id].stab_rest)
			{
			  players [id].stab_rest = STAB_REST;
			}
		    }

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
	      if (!z->freeze)
		{
		  z->speed_x = (rand () % 3 - 1)*ZOMBIE_SPEED;
		  z->facing = z->speed_x > 0 ? FACING_RIGHT
		    : z->speed_x < 0 ? FACING_LEFT : z->facing;

		  z->speed_y = (rand () % 3 - 1)*ZOMBIE_SPEED;
		  z->facing = z->speed_y > 0 ? FACING_DOWN
		    : z->speed_y < 0 ? FACING_UP : z->facing;
		}
	      else
		z->freeze--;

	      if (z->agent->immortal)
		z->agent->immortal--;

	      z = z->next;
	    }

	  area = area->next;
	}

      if (zombie_spawn_counter == ZOMBIE_SPAWN_INTERVAL)
	{
	  zombie_spawn_counter = 0;

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

      if (object_spawn_counter == OBJECT_SPAWN_INTERVAL)
	{
	  object_spawn_counter = 0;

	  area = &field;

	  while (area)
	    {
	      if (area->free_object_spawns_num)
		{
		  for (i = 0; i < area->object_spawns_num; i++)
		    {
		      if (!area->object_spawns [i].content)
			{
			  obj = malloc_and_check (sizeof (*obj));
			  obj->area = area;
			  obj->place = area->object_spawns [i].place;
			  obj->type = rand () % 4 + 1;
			  obj->spawn = &area->object_spawns [i];
			  obj->next = objects;
			  area->object_spawns [i].content = obj;
			  objects = obj;
			  break;
			}
		    }
		}

	      area = area->next;
	    }
	}

      for (i = 0; i < MAX_PLAYERS; i++)
	{
	  if (players [i].id == -1)
	    continue;

	  players [i].agent->place =
	    move_character (&players [i], players [i].agent->area->walkable,
			    players [i].agent->area->unwalkables,
			    players [i].agent->area->unwalkables_num,
			    players [i].agent->area->zombies, &char_hit);

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
		      players [i].npcid = -1;
		      break;
		    }

		  in = in->next;
		}

	      in = players [i].agent->area->npcs, j = 0;

	      while (in)
		{
		  if (does_character_face_object (players [i].agent->place,
						  players [i].facing, in->place))
		    {
		      players [i].textbox = in->text;
		      players [i].textbox_lines_num = in->text_lines_num;
		      players [i].npcid = j;
		      break;
		    }

		  in = in->next, j++;
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

	      players [i].bullets--;

	      if (shotag && !shotag->immortal)
		{
		  shotag->immortal = IMMORTAL_DURATION;
		  shotag->life -= SHOOT_DAMAGE;
		}
	    }

	  if (players [i].stab_rest == STAB_REST)
	    {
	      stabbed = get_stabbed_agent (players [i].agent->place,
					   players [i].facing,
					   players [i].agent->area, agents,
					   &speedx, &speedy);

	      if (stabbed && !stabbed->immortal)
		{
		  stabbed->immortal = IMMORTAL_DURATION;
		  stabbed->life -= STAB_DAMAGE;

		  if (stabbed->type == AGENT_PLAYER)
		    {
		      stabbed->data_ptr.player->freeze =
			stabbed->data_ptr.player->id > i ? 4 : 5;
		      stabbed->data_ptr.player->speed_x = speedx;
		      stabbed->data_ptr.player->speed_y = speedy;
		    }
		  else
		    {
		      stabbed->data_ptr.zombie->freeze = 4;
		      stabbed->data_ptr.zombie->speed_x = speedx;
		      stabbed->data_ptr.zombie->speed_y = speedy;
		    }
		}
	    }

	  if (players [i].agent->immortal)
	    players [i].agent->immortal--;

	  if (players [i].shoot_rest)
	    players [i].shoot_rest--;

	  if (players [i].stab_rest)
	    players [i].stab_rest--;

	  if (players [i].hunger_up)
	    players [i].hunger_up--;
	  else
	    {
	      if (players [i].hunger < MAX_HUNGER)
		players [i].hunger++;
	      else
		players [i].agent->life--;

	      players [i].hunger_up = HUNGER_UP;
	    }

	  if (players [i].thirst_up)
	    players [i].thirst_up--;
	  else
	    {
	      if (players [i].thirst < MAX_THIRST)
		players [i].thirst++;
	      else
		players [i].agent->life--;

	      players [i].thirst_up = THIRST_UP;
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

	  obj = objects, probj = NULL;

	  while (obj)
	    {
	      if (players [i].agent->area == obj->area
		  && does_agent_take_object (players [i].agent->place, obj->place))
		{
		  switch (obj->type)
		    {
		    case OBJECT_HEALTH:
		      players [i].agent->life = MAX_PLAYER_HEALTH;
		      break;
		    case OBJECT_AMMO:
		      players [i].bullets = 16;
		      break;
		    case OBJECT_FOOD:
		      players [i].hunger = 0;
		      players [i].hunger_up = HUNGER_UP;
		      break;
		    case OBJECT_WATER:
		      players [i].thirst = 0;
		      players [i].thirst_up = THIRST_UP;
		      break;
		    default:
		      break;
		    }

		  if (probj)
		    probj->next = obj->next;
		  else
		    objects = obj->next;

		  if (obj->spawn)
		    obj->spawn->content = NULL;

		  free (obj);

		  obj = probj ? probj->next : objects;
		}
	      else
		{
		  probj = obj;
		  obj = obj->next;
		}
	    }
	}

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

      area = &field;

      while (area)
	{
	  z = area->zombies, prz = NULL;

	  while (z)
	    {
	      if (z->agent->life <= 0)
		{
		  i = rand () % 12;

		  if (i && i <= 4)
		    {
		      obj = malloc_and_check (sizeof (*obj));
		      obj->area = area;
		      obj->place = z->agent->place;
		      obj->type = i;
		      obj->spawn = NULL;
		      obj->next = objects;
		      objects = obj;
		    }

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
		  z->agent->place = move_zombie (z->agent->place, area,
						 z->speed_x, z->speed_y,
						 area->walkable,
						 area->unwalkables,
						 area->unwalkables_num, players);

		  prz = z;
		  z = z->next;
		}
	    }

	  area = area->next;
	}

      for (i = 0; i < MAX_PLAYERS; i++)
	{
	  if (players [i].id != -1 && players [i].agent->life <= 0)
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
				 shots, objects);

	      players [i].textbox = NULL;
	      players [i].textbox_lines_num = 0;
	    }
	}

      for (i = 0; i < MAX_PLAYERS; i++)
	{
	  if (players [i].id != -1)
	    {
	      if (players [i].freeze)
		{
		  players [i].freeze--;

		  if (!players [i].freeze)
		    {
		      players [i].speed_x = players [i].speed_y = 0;
		    }
		}

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

		  if (players [i].agent->next)
		    players [i].agent->next->prev = players [i].agent->prev;

		  free (players [i].agent);
		}
	    }
	}

      zombie_spawn_counter++;

      object_spawn_counter++;

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
