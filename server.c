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
#define ZOMBIE_SPEED 2


#define GUN_RANGE 120


#define ZOMBIE_SIGHT 110

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
object
{
  struct server_area *area;
  SDL_Rect place;
  enum object_type type;
  struct object_spawn *spawn;
  struct object *next;
};


struct
player
{
  uint32_t id;
  struct agent *agent;

  struct sockaddr_in address;
  uint16_t portoffset;
  uint32_t last_update;

  char name [MAX_LOGNAME_LEN+1];
  uint32_t bodytype;
  int32_t speed_x, speed_y;
  enum facing facing;

  uint32_t bullets;

  uint32_t is_searching;
  struct bag *might_search_at;
  int32_t swap1, swap2;
  int swap_rest;

  struct object bag [BAG_SIZE];

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

  int next_thinking;

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
  struct private_server_area *private_area;
  SDL_Rect place;

  struct private_server_area *priv_areas;

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


struct
object_spawn
{
  SDL_Rect place;
  struct object *content;
};


struct
bag
{
  SDL_Rect place;
  SDL_Rect icon;
  struct object content [BAG_SIZE];

  struct player *searched_by;

  struct bag *next;
};


struct
server_area
{
  uint32_t id;
  SDL_Rect walkable;
  SDL_Rect *full_obstacles;
  int full_obstacles_num;
  SDL_Rect *half_obstacles;
  int half_obstacles_num;

  struct warp *warps;
  struct interactible *interactibles;
  struct interactible *npcs;

  struct zombie *zombies;
  int zombies_num;
  SDL_Rect *zombie_spawns;
  int zombie_spawns_num;

  int is_peaceful;
  int is_private;

  struct object_spawn *object_spawns;
  int object_spawns_num, free_object_spawns_num;

  struct bag *bags;

  struct server_area *next;
};


struct
private_server_area
{
  uint32_t id;
  struct server_area *area;

  struct object_spawn *object_spawns;
  int object_spawns_num, free_object_spawns_num;

  struct object *objects;

  struct bag *bags;

  struct private_server_area *next;
};



void
set_rect (SDL_Rect *rect, int x, int y, int w, int h)
{
  rect->x = x;
  rect->y = y;
  rect->w = w;
  rect->h = h;
}


struct private_server_area *
allocate_private_areas (struct server_area *areas)
{
  struct private_server_area *ret = NULL, *par;
  struct bag *bs, *b;
  int i;

  while (areas)
    {
      if (areas->is_private)
	{
	  if (ret)
	    {
	      par->next = malloc_and_check (sizeof (*par));
	      par = par->next;
	    }
	  else
	    {
	      ret = par = malloc_and_check (sizeof (*par));
	    }

	  par->id = areas->id;
	  par->area = areas;

	  par->object_spawns = calloc_and_check (areas->object_spawns_num,
						 sizeof (*par->object_spawns));
	  par->object_spawns_num = par->free_object_spawns_num
	    = areas->object_spawns_num;

	  for (i = 0; i < par->object_spawns_num; i++)
	    par->object_spawns [i].place = areas->object_spawns [i].place;

	  par->objects = NULL;

	  par->bags = NULL;
	  bs = areas->bags;
	  while (bs)
	    {
	      if (par->bags)
		{
		  b->next = calloc_and_check (1, sizeof (*par->bags));
		  b = b->next;
		}
	      else
		{
		  par->bags = b = calloc_and_check (1, sizeof (*b));
		}

	      b->place = bs->place;
	      b->icon = bs->icon;

	      bs = bs->next;
	    }

	  par->next = NULL;
	}

      areas = areas->next;
    }

  return ret;
}


uint32_t
create_player (char name[], uint32_t bodytype, struct sockaddr_in *addr,
	       uint16_t portoff, struct server_area *area, struct player pls [],
	       struct agent **agents)
{
  int i, j;
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
  a->private_area = NULL;
  set_rect (&a->place, 96, 0, 16, 16);
  a->priv_areas = allocate_private_areas (area);
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
  pls [i].bodytype = bodytype;
  pls [i].speed_x = pls [i].speed_y = pls [i].facing = 0;
  pls [i].bullets = 16;
  pls [i].is_searching = 0;
  pls [i].might_search_at = NULL;
  pls [i].swap_rest = 0;

  for (j = 0; j < BAG_SIZE; j++)
    pls [i].bag [j].type = OBJECT_NONE;

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
  ret->next_thinking = 0;
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
move_character (struct player *pl, SDL_Rect walkable, SDL_Rect full_obstacles [],
		int full_obstacles_num, SDL_Rect half_obstacles [],
		int half_obstacles_num, struct zombie *zs, int *character_hit)
{
  int collided, speed_x = pl->speed_x, speed_y = pl->speed_y;
  struct zombie *z;
  SDL_Rect charbox = pl->agent->place;

  *character_hit = 0;
  charbox.x += speed_x;
  charbox.y += speed_y;

 restart:
  if (!speed_x && !speed_y)
    return charbox;

  charbox = check_and_resolve_collisions (charbox, &speed_x, &speed_y,
					  full_obstacles, full_obstacles_num,
					  &collided);

  if (collided)
    goto restart;

  charbox = check_and_resolve_collisions (charbox, &speed_x, &speed_y,
					  half_obstacles, half_obstacles_num,
					  &collided);

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
	     SDL_Rect walkable, SDL_Rect full_obstacles [],
	     int full_obstacles_num, SDL_Rect half_obstacles [],
	     int half_obstacles_num, struct player pls [])
{
  int collided, i, sx = speed_x, sy = speed_y;

  charbox.x += speed_x;
  charbox.y += speed_y;

 restart:
  if (!speed_x && !speed_y)
    return charbox;

  charbox = check_and_resolve_collisions (charbox, &speed_x, &speed_y,
					  full_obstacles, full_obstacles_num,
					  &collided);

  if (collided)
    goto restart;

  charbox = check_and_resolve_collisions (charbox, &speed_x, &speed_y,
					  half_obstacles, half_obstacles_num,
					  &collided);

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


uint32_t
compute_nearest_player (struct zombie *z, struct player pls [], int *distance)
{
  uint32_t i, nearest = -1, dist;

  for (i = 0; i < MAX_PLAYERS; i++)
    {
      if (pls [i].id != -1 && z->agent->area == pls [i].agent->area)
	{
	  dist = abs (z->agent->place.x-pls[i].agent->place.x)
	    + abs (z->agent->place.y-pls[i].agent->place.y);

	  if (nearest == -1 || dist < *distance)
	    {
	      nearest = i;
	      *distance = dist;
	    }
	}
    }

  return nearest;
}


int
is_target_hit (SDL_Rect charbox, enum facing facing, SDL_Rect target,
	       int is_agent, int *distance, SDL_Rect *hitpart)
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
	  *distance = target.y-charbox.h-charbox.y;
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
	  *distance = charbox.y-target.h-target.y;
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
	  *distance = target.x-charbox.w-charbox.x;
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
	  *distance = charbox.x-target.w-target.x;
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
	       struct agent *as, int *hit, struct agent **shotag)
{
  int i, dist;
  SDL_Rect ret, hitpart;

  *hit = 0, *shotag = NULL;

  for (i = 0; i < area->full_obstacles_num; i++)
    {
      if (is_target_hit (charbox, facing, area->full_obstacles [i], 0, &dist,
			 &hitpart) && dist <= GUN_RANGE)
	{
	  if (!*hit || is_closer (facing, hitpart, ret))
	    {
	      *hit = 1;
	      ret = hitpart;
	    }
	}
    }

  while (as)
    {
      if (area == as->area
	  && is_target_hit (charbox, facing, as->place, 1, &dist, &hitpart)
	  && dist <= GUN_RANGE)
	{
	  if (!*hit || is_closer (facing, hitpart, ret))
	    {
	      *hit = 1;
	      *shotag = as;
	      ret = hitpart;
	    }
	}

      as = as->next;
    }

  if (*hit)
    {
      ret.w = GRID_CELL_W;
      ret.h = GRID_CELL_H;
    }
  else switch (facing)
    {
    case FACING_DOWN:
      if (area->walkable.h-charbox.h-charbox.y <= GUN_RANGE)
	{
	  *hit = 1;
	  ret.x = charbox.x;
	  ret.y = area->walkable.h;
	}
      break;
    case FACING_UP:
      if (charbox.y <= GUN_RANGE)
	{
	  *hit = 1;
	  ret.x = charbox.x;
	  ret.y = -GRID_CELL_H;
	}
      break;
    case FACING_RIGHT:
      if (area->walkable.w-charbox.w-charbox.x <= GUN_RANGE)
	{
	  *hit = 1;
	  ret.x = area->walkable.w;
	  ret.y = charbox.y;
	}
      break;
    case FACING_LEFT:
      if (charbox.x <= GUN_RANGE)
	{
	  *hit = 1;
	  ret.x = -GRID_CELL_W;
	  ret.y = charbox.y;
	}
      break;
    }

  if (*hit) switch (facing)
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
swap_objects (enum object_type *t1, enum object_type *t2)
{
  enum object_type tmp = *t1;
  *t1 = *t2;
  *t2 = tmp;
}


int
is_visible_by_player (SDL_Rect charbox, SDL_Rect entity)
{
  return abs (charbox.x-entity.x) < WINDOW_WIDTH
    && abs (charbox.y-entity.y) < WINDOW_HEIGHT;
}


void
send_server_state (int sockfd, uint32_t frame_counter, int id, struct player *pls,
		   struct agent *as, struct shot *ss, struct object *objs)
{
  static struct message msg;
  struct visible vis = {0};
  int i;

  msg.type = htonl (MSG_SERVER_STATE);
  msg.args.server_state.frame_counter = htonl (frame_counter);
  msg.args.server_state.areaid = htonl (pls [id].agent->area->id);
  msg.args.server_state.x = htonl (pls [id].agent->place.x);
  msg.args.server_state.y = htonl (pls [id].agent->place.y);
  msg.args.server_state.w = htonl (pls [id].agent->place.w);
  msg.args.server_state.h = htonl (pls [id].agent->place.h);
  msg.args.server_state.char_facing = pls [id].facing;
  msg.args.server_state.life = htonl (pls [id].agent->life);
  msg.args.server_state.is_immortal = !!pls [id].agent->immortal;
  msg.args.server_state.bullets = htonl (pls [id].bullets);
  msg.args.server_state.hunger = htonl (pls [id].hunger);
  msg.args.server_state.thirst = htonl (pls [id].thirst);
  msg.args.server_state.just_shot = pls [id].shoot_rest > 6;
  msg.args.server_state.just_stabbed = pls [id].stab_rest > 2;
  msg.args.server_state.is_searching = htonl (pls [id].is_searching);

  if (pls [id].is_searching)
    {
      for (i = 0; i < BAG_SIZE; i++)
	{
	  msg.args.server_state.bag [i] = htonl (pls [id].bag [i].type);
	}

      if (pls [id].might_search_at
	  && pls [id].might_search_at->searched_by == &pls [id])
	{
	  msg.args.server_state.is_searching
	    = htonl (ntohl (msg.args.server_state.is_searching)+1);

	  for (i = 0; i < BAG_SIZE; i++)
	    {
	      msg.args.server_state.bag [BAG_SIZE+i]
		= htonl (pls [id].might_search_at->content [i].type);
	    }
	}
    }

  msg.args.server_state.num_visibles = 0;
  msg.args.server_state.npcid = htonl (pls [id].npcid);
  msg.args.server_state.textbox_lines_num = htonl (pls [id].textbox_lines_num);

  while (!pls [id].agent->area->is_private && as)
    {
      if (msg.args.server_state.num_visibles == MAX_VISIBLES)
	{
	  fprintf (stderr, "too many visibles to send to player %d, skipping some\n",
		   id);
	  goto send;
	}

      if ((as->type != AGENT_PLAYER || as->data_ptr.player != &pls [id])
	  && as->area == pls [id].agent->area
	  && is_visible_by_player (pls [id].agent->place, as->place))
	{
	  vis.type = htonl (as->type == AGENT_PLAYER ? VISIBLE_PLAYER
			    : VISIBLE_ZOMBIE);
	  vis.subtype = as->type == AGENT_PLAYER
	    ? htonl (as->data_ptr.player->bodytype) : 0;
	  vis.x = htonl (as->place.x);
	  vis.y = htonl (as->place.y);
	  vis.w = htonl (as->place.w);
	  vis.h = htonl (as->place.h);

	  if (as->type == AGENT_PLAYER)
	    {
	      vis.facing = htonl (as->data_ptr.player->facing);
	      vis.speed_x = htonl (as->data_ptr.player->speed_x);
	      vis.speed_y = htonl (as->data_ptr.player->speed_y);
	    }
	  else
	    {
	      vis.facing = htonl (as->data_ptr.zombie->facing);
	      vis.speed_x = htonl (as->data_ptr.zombie->speed_x);
	      vis.speed_y = htonl (as->data_ptr.zombie->speed_y);
	      vis.is_immortal = !!as->immortal;
	    }

	  memcpy (&msg.args.server_state.visibles
		  [msg.args.server_state.num_visibles], &vis, sizeof (vis));
	  msg.args.server_state.num_visibles++;
	}

      as = as->next;
    }

  for (i = 0; i < MAX_PLAYERS; i++)
    {
      if (pls [i].id != -1 && !pls [id].agent->area->is_private
	  && pls [id].agent->area == pls [i].agent->area
	  && pls [i].is_searching && pls [i].might_search_at
	  && pls [i].might_search_at->searched_by == &pls [i])
	{
	  if (msg.args.server_state.num_visibles == MAX_VISIBLES)
	    {
	      fprintf (stderr, "too many visibles to send to player %d, skipping some\n",
		       id);
	      goto send;
	    }

	  vis.type = htonl (VISIBLE_SEARCHING);
	  vis.x = htonl (pls [i].agent->place.x+12);
	  vis.y = htonl (pls [i].agent->place.y-16);
	  vis.w = htonl (16);
	  vis.h = htonl (16);

	  memcpy (&msg.args.server_state.visibles
		  [msg.args.server_state.num_visibles], &vis, sizeof (vis));
	  msg.args.server_state.num_visibles++;
	}
    }

  if (pls [id].agent->area->is_private)
    objs = pls [id].agent->private_area->objects;

  while (objs)
    {
      if (msg.args.server_state.num_visibles == MAX_VISIBLES)
	{
	  fprintf (stderr, "too many visibles to send to player %d, skipping some\n",
		   id);
	  goto send;
	}

      if (objs->area == pls [id].agent->area
	  && is_visible_by_player (pls [id].agent->place, objs->place))
	{
	  switch (objs->type)
	    {
	    case OBJECT_HEALTH:
	      vis.type = htonl (VISIBLE_HEALTH);
	      break;
	    case OBJECT_AMMO:
	      vis.type = htonl (VISIBLE_AMMO);
	      break;
	    case OBJECT_FOOD:
	      vis.type = htonl (VISIBLE_FOOD);
	      break;
	    case OBJECT_WATER:
	      vis.type = htonl (VISIBLE_WATER);
	      break;
	    case OBJECT_FLESH:
	      vis.type = htonl (VISIBLE_FLESH);
	      break;
	    default:
	      continue;
	    }

	  vis.x = htonl (objs->place.x);
	  vis.y = htonl (objs->place.y);
	  vis.w = htonl (objs->place.w);
	  vis.h = htonl (objs->place.h);

	  memcpy (&msg.args.server_state.visibles
		  [msg.args.server_state.num_visibles], &vis, sizeof (vis));
	  msg.args.server_state.num_visibles++;
	}

      objs = objs->next;
    }

  while (ss)
    {
      if (msg.args.server_state.num_visibles == MAX_VISIBLES)
	{
	  fprintf (stderr, "too many visibles to send to player %d, skipping some\n",
		   id);
	  goto send;
	}

      if (ss->areaid == pls [id].agent->area->id
	  && is_visible_by_player (pls [id].agent->place, ss->target))
	{
	  vis.type = htonl (VISIBLE_SHOT);
	  vis.x = htonl (ss->target.x);
	  vis.y = htonl (ss->target.y);
	  vis.w = htonl (ss->target.w);
	  vis.h = htonl (ss->target.h);
	  memcpy (&msg.args.server_state.visibles
		  [msg.args.server_state.num_visibles], &vis, sizeof (vis));
	  msg.args.server_state.num_visibles++;
	}

      ss = ss->next;
    }

  if (!pls [id].is_searching && pls [id].might_search_at
      && !pls [id].might_search_at->searched_by)
    {
      vis.type = htonl (VISIBLE_SEARCHABLE);
      vis.x = htonl (pls [id].might_search_at->icon.x);
      vis.y = htonl (pls [id].might_search_at->icon.y);
      vis.w = htonl (pls [id].might_search_at->icon.w);
      vis.h = htonl (pls [id].might_search_at->icon.h);
      memcpy (&msg.args.server_state.visibles
	      [msg.args.server_state.num_visibles], &vis, sizeof (vis));
      msg.args.server_state.num_visibles++;
    }

 send:
  msg.args.server_state.num_visibles = htonl (msg.args.server_state.num_visibles);

  if (pls [id].textbox)
    {
      strcpy (msg.args.server_state.textbox, pls [id].textbox);
    }
  else
    msg.args.server_state.textbox_lines_num = 0;

  if (sendto (sockfd, &msg, offsetof (struct message, args)
	      + offsetof (struct server_state_args, visibles)
	      + sizeof (struct visible) * ntohl (msg.args.server_state.num_visibles),
	      0, (struct sockaddr *)&pls [id].address, sizeof (pls [id].address)) < 0)
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
  struct message buffer;
  ssize_t recvlen;
  struct sockaddr_in local_addr, client_addr;
  socklen_t client_addr_sz = sizeof (client_addr);

  struct message *msg;

  struct interactible *in;
  struct warp *w;
  struct bag *b;

  struct server_area field = {0}, *area;
  SDL_Rect field_walkable = {0, 0, 1152, 1024},
    field_full_obs [] = {R_BY_GR (8, 0, 1, 4), R_BY_GR (8, 7, 1, 4), /* parking */
			    R_BY_GR (8, 11, 24, 1), R_BY_GR (32, 0, 1, 12),
			    R_BY_GR (12, 0, 1, 1), R_BY_GR (17, 0, 1, 1),
			    R_BY_GR (21, 0, 1, 1), R_BY_GR (27, 0, 1, 1),
			    R_BY_GR (9, 9, 1, 2), R_BY_GR (13, 9, 1, 2),
			    R_BY_GR (19, 9, 1, 2), R_BY_GR (24, 9, 1, 2),

			    /* neighborhood */
			    R_BY_GR (9, 13, 6, 8), R_BY_GR (8, 17, 1, 1),
			    R_BY_GR (8, 19, 1, 1), R_BY_GR (15, 20, 1, 1),
			    R_BY_GR (17, 16, 4, 5), R_BY_GR (22, 15, 4, 5),
			    R_BY_GR (22, 20, 2, 1), R_BY_GR (25, 20, 1, 1),
			    R_BY_GR (26, 19, 1, 1), R_BY_GR (17, 14, 9, 1),

			    R_BY_GR (22, 23, 2, 2), R_BY_GR (27, 16, 1, 2),
			    R_BY_GR (30, 20, 1, 1),

			    /* border */
			    R_BY_GR (37, 0, 1, 2), R_BY_GR (35, 2, 3, 1),
			    R_BY_GR (34, 2, 1, 20), R_BY_GR (34, 25, 1, 5),

			    /* house */
			    R_BY_GR (49, 10, 5, 3), R_BY_GR (49, 13, 2, 1),
			    R_BY_GR (52, 13, 2, 1), R_BY_GR (46, 7, 8, 1),
			    R_BY_GR (46, 9, 1, 5), R_BY_GR (52, 15, 4, 1),
			    R_BY_GR (55, 11, 1, 3), R_BY_GR (57, 10, 1, 2),
			    R_BY_GR (58, 12, 1, 2), R_BY_GR (59, 14, 1, 2),
			    R_BY_GR (61, 13, 2, 2),

			    /* top left field */
			    R_BY_GR (41, 0, 2, 2), R_BY_GR (45, 1, 2, 2),
			    R_BY_GR (46, 4, 2, 2), R_BY_GR (40, 3, 4, 4),
			    R_BY_GR (36, 7, 2, 2), R_BY_GR (37, 14, 2, 2),
			    R_BY_GR (40, 10, 3, 3), R_BY_GR (49, 0, 3, 1),
			    R_BY_GR (40, 14, 2, 1), R_BY_GR (41, 15, 1, 2),
			    R_BY_GR (42, 16, 1, 1), R_BY_GR (36, 18, 8, 1),
			    R_BY_GR (44, 14, 0, 4), R_BY_GR (46, 19, 1, 1),

			    /* top right field */
			    R_BY_GR (60, 8, 1, 2), R_BY_GR (64, 7, 1, 1),
			    R_BY_GR (66, 1, 2, 2), R_BY_GR (68, 2, 2, 2),

			    /* bottom field */
			    R_BY_GR (61, 18, 1, 1), R_BY_GR (37, 31, 8, 1),
			    R_BY_GR (49, 24, 3, 1), R_BY_GR (47, 29, 1, 2),
			    R_BY_GR (49, 28, 1, 2), R_BY_GR (50, 31, 5, 1),
			    R_BY_GR (50, 35, 3, 1), R_BY_GR (52, 37, 3, 1),
			    R_BY_GR (50, 39, 4, 1), R_BY_GR (51, 40, 3, 1),
			    R_BY_GR (50, 42, 3, 1), R_BY_GR (52, 44, 4, 1),
			    R_BY_GR (57, 44, 2, 2), R_BY_GR (52, 46, 2, 1),
			    R_BY_GR (53, 47, 2, 1), R_BY_GR (47, 49, 2, 2),
			    R_BY_GR (59, 26, 2, 12), R_BY_GR (59, 22, 1, 2),
			    R_BY_GR (52, 28, 3, 2), R_BY_GR (63, 22, 2, 2),
			    R_BY_GR (63, 25, 2, 2), R_BY_GR (63, 28, 2, 2),
			    R_BY_GR (67, 26, 2, 2), R_BY_GR (60, 17, 0, 3),
			    R_BY_GR (60, 20, 6, 1), R_BY_GR (66, 21, 0, 4),
			    R_BY_GR (66, 25, 3, 1), R_BY_GR (69, 26, 0, 4),
			    R_BY_GR (69, 30, 3, 1), R_BY_GR (65, 14, 3, 1),
			    R_BY_GR (67, 16, 3, 1), R_BY_GR (41, 27, 3, 3),
			    R_BY_GR (43, 24, 2, 2), R_BY_GR (37, 20, 4, 1),
			    R_BY_GR (37, 26, 4, 1), R_BY_GR (45, 31, 1, 13),
			    R_BY_GR (45, 46, 1, 13), R_BY_GR (45, 60, 1, 4),
			    R_BY_GR (40, 21, 1, 1), R_BY_GR (41, 25, 1, 1),
			    R_BY_GR (45, 25, 1, 1), R_BY_GR (45, 29, 1, 1),
			    R_BY_GR (63, 32, 2, 2), R_BY_GR (66, 33, 2, 2),
			    R_BY_GR (63, 35, 2, 2), R_BY_GR (66, 36, 2, 2),
			    R_BY_GR (63, 38, 2, 2), R_BY_GR (70, 33, 2, 2),
			    R_BY_GR (60, 47, 0, 10), R_BY_GR (61, 47, 0, 10),
			    R_BY_GR (60, 60, 2, 3)},

    field_half_obs [] = {/* lake */
      R_BY_GR (48, 56, 1, 8), R_BY_GR (49, 54, 1, 3),
      R_BY_GR (49, 54, 3, 1), R_BY_GR (51, 53, 1, 2),
      R_BY_GR (52, 52, 1, 2), R_BY_GR (52, 52, 3, 1),
      R_BY_GR (54, 50, 1, 3), R_BY_GR (55, 49, 1, 2),
      R_BY_GR (55, 49, 3, 1), R_BY_GR (57, 48, 1, 2),
      R_BY_GR (57, 48, 3, 1), R_BY_GR (59, 47, 1, 1),
      R_BY_GR (62, 46, 1, 2), R_BY_GR (61, 47, 1, 1),
      R_BY_GR (63, 45, 1, 2), R_BY_GR (64, 43, 1, 3),
      R_BY_GR (65, 42, 1, 2), R_BY_GR (65, 42, 3, 1),
      R_BY_GR (67, 41, 1, 2), R_BY_GR (67, 41, 3, 1),
      R_BY_GR (69, 39, 1, 3), R_BY_GR (70, 37, 1, 3),
      R_BY_GR (71, 36, 1, 2), R_BY_GR (58, 56, 1, 4),
      R_BY_GR (59, 56, 1, 1), R_BY_GR (57, 59, 1, 5),
      R_BY_GR (61, 56, 3, 1), R_BY_GR (63, 56, 1, 3),
      R_BY_GR (64, 58, 1, 2), R_BY_GR (65, 59, 1, 2),
      R_BY_GR (66, 60, 1, 4)},

    field_zombie_spawns [] = {RECT_BY_GRID (13, 31, 1, 1),
			      RECT_BY_GRID (31, 22, 1, 1),
			      RECT_BY_GRID (16, 0, 1, 1),
			      RECT_BY_GRID (0, 23, 1, 1)};

  struct server_area room = {0};
  SDL_Rect room_walkable = RECT_BY_GRID (0, 0, 12, 12),
    room_full_obs [] = {RECT_BY_GRID (1, 6, 1, 3),
    RECT_BY_GRID (7, 2, 3, 3), RECT_BY_GRID (7, 5, 1, 1),
    RECT_BY_GRID (0, 11, 5, 1), RECT_BY_GRID (7, 11, 5, 1),
    RECT_BY_GRID (7, 7, 1, 1), RECT_BY_GRID (3, 9, 1, 2),
    RECT_BY_GRID (8, 9, 1, 2), RECT_BY_GRID (10, 8, 0, 2),
    RECT_BY_GRID (10, 10, 2, 0)};
  struct object_spawn room_object_spawns [] = {{RECT_BY_GRID (1, 1, 1, 1), NULL},
					       {RECT_BY_GRID (3, 1, 1, 1), NULL}};

  struct server_area basement = {0};
  SDL_Rect basement_walkable = RECT_BY_GRID (0, 0, 12, 11),
    basement_full_obs [] = {RECT_BY_GRID (1, 0, 7, 2),
    RECT_BY_GRID (1, 4, 7, 2), RECT_BY_GRID (1, 8, 7, 2),
    RECT_BY_GRID (9, 0, 3, 3), RECT_BY_GRID (10, 7, 2, 0),
    RECT_BY_GRID (10, 7, 0, 3)};
  struct bag basement_bag = {RECT_BY_GRID (9, 3, 3, 1),
			     RECT_BY_GRID (10, 1, 1, 1)};

  struct server_area hotel_ground = {0};
  SDL_Rect hotel_ground_walkable = RECT_BY_GRID (0, 0, 12, 12),
    hotel_ground_full_obs [] = {
      RECT_BY_GRID (0, 11, 5, 1), RECT_BY_GRID (7, 11, 5, 1),
      RECT_BY_GRID (0, 3, 3, 1), RECT_BY_GRID (2, 4, 1, 5),
      RECT_BY_GRID (0, 8, 2, 1), RECT_BY_GRID (9, 3, 3, 1),
      RECT_BY_GRID (9, 4, 1, 5), RECT_BY_GRID (10, 8, 2, 1),
      RECT_BY_GRID (5, 0, 0, 3), RECT_BY_GRID (7, 0, 0, 3)};

  struct server_area hotel_room = {0};
  SDL_Rect hotel_room_walkable = RECT_BY_GRID (0, 0, 4, 8),
    hotel_room_full_obs [] = {
      RECT_BY_GRID (3, 0, 1, 3), RECT_BY_GRID (0, 4, 1, 3),
      RECT_BY_GRID (0, 7, 3, 1)};
  struct object_spawn hotel_room_object_spawns [] =
    {{RECT_BY_GRID (1, 1, 1, 1), NULL}};
  struct bag hotel_room_bag = {RECT_BY_GRID (3, 3, 1, 1),
			       RECT_BY_GRID (4, 1, 1, 1)};

  struct private_server_area *par;

  SDL_Event event;
  SDL_Rect hitrect;

  uint32_t frame_counter = 1, id;
  int char_hit, hit, quit = 0, i, j, speedx, speedy, dist,
    zombie_spawn_counter = 0, object_spawn_counter = 0;
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
  field.full_obstacles = field_full_obs;
  field.full_obstacles_num = 109;
  field.half_obstacles = field_half_obs;
  field.half_obstacles_num = 31;
  field.warps = make_warp_by_grid (51, 13, 1, 1, &room, 5, 11,
				   make_warp_by_grid (24, 20, 1, 1, &hotel_ground,
						      5, 11, NULL));
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
  room.full_obstacles = room_full_obs;
  room.full_obstacles_num = 10;
  room.warps = make_warp_by_grid (5, 11, 2, 1, &field, 51, 14,
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
  room.is_peaceful = 1;
  room.object_spawns = room_object_spawns;
  room.object_spawns_num = room.free_object_spawns_num = 2;
  room.next = &basement;

  basement.id = 2;
  basement.walkable = basement_walkable;
  basement.full_obstacles = basement_full_obs;
  basement.full_obstacles_num = 6;
  basement.warps = make_warp_by_grid (10, 7, 2, 3, &room, 10, 7, NULL);
  basement.is_peaceful = 1;
  basement.bags = &basement_bag;
  basement.next = &hotel_ground;

  hotel_ground.id = 3;
  hotel_ground.walkable = hotel_ground_walkable;
  hotel_ground.full_obstacles = hotel_ground_full_obs;
  hotel_ground.full_obstacles_num = 10;
  hotel_ground.warps = make_warp_by_grid (5, 11, 2, 1, &field, 24, 21,
					  make_warp_by_grid (5, 0, 2, 3,
							     &hotel_room, 3, 6,
							     NULL));
  hotel_ground.npcs = make_interactible_by_grid (2, 6, 1, 1,
						 "The lodgings are upstairs.    "
						 "Each person has a room.       ",
						 NULL);
  hotel_ground.is_peaceful = 1;
  hotel_ground.next = &hotel_room;

  hotel_room.id = 4;
  hotel_room.walkable = hotel_room_walkable;
  hotel_room.full_obstacles = hotel_room_full_obs;
  hotel_room.full_obstacles_num = 3;
  hotel_room.warps = make_warp_by_grid (3, 7, 1, 1, &hotel_ground, 6, 3, NULL);
  hotel_room.is_peaceful = 1;
  hotel_room.is_private = 1;
  hotel_room.object_spawns = hotel_room_object_spawns;
  hotel_room.object_spawns_num = hotel_room.free_object_spawns_num = 1;
  hotel_room.bags = &hotel_room_bag;

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
	    recvfrom (sockfd, &buffer, sizeof (buffer), 0,
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

	  msg = (struct message *) &buffer;
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

	      msg->args.login.bodytype = ntohl (msg->args.login.bodytype);

	      if (msg->args.login.bodytype < 0 || msg->args.login.bodytype > 6)
		msg->args.login.bodytype = 0;

	      id = create_player (msg->args.login.logname,
				  msg->args.login.bodytype, &client_addr,
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
	      id = ntohl (msg->args.client_char_state.id);

	      if (players [id].id == -1)
		{
		  fprintf (stderr, "got state from unknown id %d\n", id);
		}
	      else if (players [id].last_update
		       < ntohl (msg->args.client_char_state.frame_counter))
		{
		  if (!players [id].freeze)
		    {
		      if (!players [id].is_searching)
			{
			  players [id].speed_x =
			    (int32_t) ntohl (msg->args.client_char_state.char_speed_x) > 0
			    ? CHAR_SPEED
			    : (int32_t) ntohl (msg->args.client_char_state.char_speed_x) < 0
			    ? -CHAR_SPEED : 0;
			  players [id].speed_y =
			    (int32_t) ntohl (msg->args.client_char_state.char_speed_y) > 0
			    ? CHAR_SPEED
			    : (int32_t) ntohl (msg->args.client_char_state.char_speed_y) < 0
			    ? -CHAR_SPEED : 0;
			  players [id].facing
			    = ntohl (msg->args.client_char_state.char_facing);
			}

		      players [id].interact
			= msg->args.client_char_state.do_interact;

		      if (msg->args.client_char_state.do_shoot
			  && !players [id].agent->area->is_peaceful
			  && !players [id].interact && players [id].bullets
			  && !players [id].shoot_rest)
			{
			  players [id].shoot_rest = SHOOT_REST;
			}

		      if (msg->args.client_char_state.do_stab
			  && !players [id].agent->area->is_peaceful
			  && !players [id].interact && !players [id].stab_rest)
			{
			  players [id].stab_rest = STAB_REST;
			}

		      if (msg->args.client_char_state.do_search
			  && !players [id].interact)
			{
			  if (!players [id].is_searching)
			    {
			      players [id].speed_x = players [id].speed_y = 0;
			      players [id].swap1 = players [id].swap2 = -1;
			    }

			  players [id].is_searching = 1;
			}
		      else
			{
			  if (players [id].is_searching)
			    {
			      if (players [id].might_search_at
				  && players [id].might_search_at->searched_by
				  == &players [id])
				players [id].might_search_at->searched_by = NULL;
			    }

			  players [id].is_searching = 0;
			}

		      players [id].swap1 =
			(int32_t) ntohl (msg->args.client_char_state.swap [0]);
		      players [id].swap2 =
			(int32_t) ntohl (msg->args.client_char_state.swap [1]);
		    }

		  players [id].last_update
		    = ntohl (msg->args.client_char_state.frame_counter);
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
		  if (!z->next_thinking)
		    {
		      id = compute_nearest_player (z, players, &dist);

		      if (id != -1 && dist < ZOMBIE_SIGHT)
			{
			  if (z->agent->place.x != players [id].agent->place.x)
			    {
			      z->speed_x = ZOMBIE_SPEED
				* (z->agent->place.x > players [id].agent->place.x
				   ? -1 : 1);
			      z->facing = z->speed_x > 0 ? FACING_RIGHT : FACING_LEFT;
			    }
			  else
			    z->speed_x = 0;

			  if (z->agent->place.y != players [id].agent->place.y)
			    {
			      z->speed_y = ZOMBIE_SPEED
				* (z->agent->place.y > players [id].agent->place.y
				   ? -1 : 1);
			      z->facing = z->speed_y > 0 ? FACING_DOWN : FACING_UP;
			    }
			  else
			    z->speed_y = 0;
			}
		      else
			{
			  z->speed_x = (rand () % 3 - 1)*ZOMBIE_SPEED;
			  z->facing = z->speed_x > 0 ? FACING_DOWN
			    : z->speed_x < 0 ? FACING_UP : z->facing;

			  z->speed_y = (rand () % 3 - 1)*ZOMBIE_SPEED;
			  z->facing = z->speed_y > 0 ? FACING_DOWN
			    : z->speed_y < 0 ? FACING_UP : z->facing;
			}

		      z->next_thinking = 25;
		    }
		  else
		    z->next_thinking--;
		}
	      else
		{
		  z->freeze--;

		  if (!z->freeze)
		    {
		      z->speed_x = z->speed_y = 0;
		    }
		}

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

	  for (i = 0; i < MAX_PLAYERS; i++)
	    {
	      if (players [i].id == -1)
		continue;

	      par = players [i].agent->priv_areas;

	      while (par)
		{
		  if (par->free_object_spawns_num)
		    {
		      for (j = 0; j < par->object_spawns_num; j++)
			{
			  if (!par->object_spawns [j].content)
			    {
			      obj = malloc_and_check (sizeof (*obj));
			      obj->area = par->area;
			      obj->place = par->object_spawns [j].place;
			      obj->type = rand () % 4 + 1;
			      obj->spawn = &par->object_spawns [j];
			      obj->next = par->objects;
			      par->object_spawns [j].content = obj;
			      par->objects = obj;
			      break;
			    }
			}
		    }

		  par = par->next;
		}
	    }
	}

      for (i = 0; i < MAX_PLAYERS; i++)
	{
	  if (players [i].id == -1)
	    continue;

	  players [i].agent->place =
	    move_character (&players [i], players [i].agent->area->walkable,
			    players [i].agent->area->full_obstacles,
			    players [i].agent->area->full_obstacles_num,
			    players [i].agent->area->half_obstacles,
			    players [i].agent->area->half_obstacles_num,
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
	      hitrect = get_shot_rect (players [i].agent->place, players [i].facing,
				       players [i].agent->area, agents, &hit,
				       &shotag);

	      if (hit)
		{
		  s = malloc_and_check (sizeof (*s));
		  s->areaid = players [i].agent->area->id;
		  s->target = hitrect;
		  s->duration = 10;
		  s->next = shots;
		  shots = s;
		}

	      players [i].bullets--;

	      if (shotag && !shotag->immortal)
		{
		  shotag->immortal = IMMORTAL_DURATION;
		  shotag->life -= SHOOT_DAMAGE;

		  if (shotag->type == AGENT_ZOMBIE
		      && !shotag->data_ptr.zombie->freeze)
		    shotag->data_ptr.zombie->freeze = 2;
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
		      stabbed->data_ptr.zombie->freeze = 6;
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

	  if (players [i].swap_rest)
	    players [i].swap_rest--;

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

		  if (w->dest->is_private)
		    {
		      par = players [i].agent->priv_areas;

		      while (par)
			{
			  if (w->dest->id == par->id)
			    {
			      players [i].agent->private_area = par;
			      break;
			    }

			  par = par->next;
			}
		    }

		  players [i].agent->place.x = w->spawn.x;
		  players [i].agent->place.y = w->spawn.y;
		  break;
		}

	      w = w->next;
	    }

	  obj = players [i].agent->area->is_private
	    ? players [i].agent->private_area->objects
	    : objects;
	  probj = NULL;

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
		    case OBJECT_FLESH:
		      for (j = 0; j < BAG_SIZE; j++)
			{
			  if (players [i].bag [j].type == OBJECT_NONE)
			    break;
			}
		      if (j == BAG_SIZE)
			goto dont_take;
		      else
			players [i].bag [j].type = OBJECT_FLESH;
		    default:
		      break;
		    }

		  if (probj)
		    probj->next = obj->next;
		  else
		    {
		      if (players [i].agent->area->is_private)
			players [i].agent->private_area->objects = obj->next;
		      else
			objects = obj->next;
		    }

		  if (obj->spawn)
		    obj->spawn->content = NULL;

		  free (obj);

		  obj = probj ? probj->next
		    : players [i].agent->area->is_private
		    ? players [i].agent->private_area->objects : objects;
		}
	      else
		{
		dont_take:
		  probj = obj;
		  obj = obj->next;
		}
	    }

	  players [i].might_search_at = NULL;
	  b = players [i].agent->area->is_private
	    ? players [i].agent->private_area->bags
	    : players [i].agent->area->bags;

	  while (b)
	    {
	      if (IS_RECT_CONTAINED (players [i].agent->place, b->place))
		{
		  players [i].might_search_at = b;

		  if (players [i].is_searching)
		    {
		      if (!b->searched_by)
			b->searched_by = &players [i];
		    }

		  break;
		}

	      b = b->next;
	    }

	  if (players [i].is_searching && players [i].swap1 >= 0
	      && (players [i].swap1 < BAG_SIZE
		  || (players [i].might_search_at
		      && players [i].might_search_at->searched_by == &players [i]
		      && players [i].swap1 < BAG_SIZE*2))
	      && players [i].swap2 >= 0
	      && (players [i].swap2 < BAG_SIZE
		  || (players [i].might_search_at
		      && players [i].might_search_at->searched_by == &players [i]
		      && players [i].swap2 < BAG_SIZE*2))
	      && !players [i].swap_rest)
	    {
	      swap_objects (players [i].swap1 < BAG_SIZE
			    ? &players [i].bag [players [i].swap1].type
			    : &players [i].might_search_at->content
			    [players [i].swap1-BAG_SIZE].type,
			    players [i].swap2 < BAG_SIZE
			    ? &players [i].bag [players [i].swap2].type
			    : &players [i].might_search_at->content
			    [players [i].swap2-BAG_SIZE].type);
	      players [i].swap1 = players [i].swap2 = -1;
	      players [i].swap_rest = 4;
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
		  i = rand () % 20;

		  if (i && i <= 5)
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
						 area->full_obstacles,
						 area->full_obstacles_num,
						 area->half_obstacles,
						 area->half_obstacles_num,
						 players);

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

	      if (players [i].might_search_at
		  && players [i].might_search_at->searched_by == &players [i])
		players [i].might_search_at->searched_by = NULL;

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

		  if (players [i].might_search_at
		      && players [i].might_search_at->searched_by == &players [i])
		    players [i].might_search_at->searched_by = NULL;

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
