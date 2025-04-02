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


#define SHOOT_REST 10


uint32_t next_id = 0;

struct
player
{
  uint32_t id;
  struct sockaddr_in address;
  uint16_t portoffset;
  uint32_t last_update;

  char name [MAX_LOGNAME_LEN+1];
  struct server_area *area;
  SDL_Rect place;
  int32_t speed_x, speed_y;
  enum facing facing;
  int life, shoot_rest;

  int timeout;
  struct player *next;
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
  struct interactible *next;
};


struct zombie
{
  SDL_Rect place;
  enum facing facing;
  int speed_x, speed_y;
  int life;
  struct zombie *next;
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
};



void
set_rect (SDL_Rect *rect, int x, int y, int w, int h)
{
  rect->x = x;
  rect->y = y;
  rect->w = w;
  rect->h = h;
}


struct player *
create_player (char name[], struct sockaddr_in *addr, uint16_t portoff,
	       struct server_area *area, struct player *next)
{
  struct player *ret = malloc_and_check (sizeof (*ret));

  ret->id = next_id++;
  memcpy (&ret->address, addr, sizeof (*addr));
  ret->address.sin_port = htons (ZOMBIELAND_PORT+portoff);
  ret->portoffset = portoff;
  ret->last_update = 0;
  strcpy (ret->name, name);
  ret->area = area;
  set_rect (&ret->place, 96, 16, 16, 16);
  ret->speed_x = ret->speed_y = ret->facing = 0;
  ret->life = 10;
  ret->shoot_rest = 0;
  ret->timeout = CLIENT_TIMEOUT;
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


SDL_Rect
check_and_resolve_collision (SDL_Rect charbox, int *speed_x, int *speed_y,
			     SDL_Rect unwalkable, int *did_collide)
{
  int new;

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
					     unwalkables [i], &collided);

      if (collided)
	*did_collide = 1;
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

  charbox = check_and_resolve_collisions (charbox, &speed_x, &speed_y, unwalkables,
					  unwalkables_num, &collided);

  while (z)
    {
      charbox = check_and_resolve_collision (charbox, &speed_x, &speed_y, z->place,
					     &collided);

      if (collided)
	*character_hit = 1;

      z = z->next;
    }

  return check_boundary (charbox, speed_x, speed_y, walkable);
}


int
is_target_hit (SDL_Rect charbox, enum facing facing, SDL_Rect target,
	       SDL_Rect *hitpart)
{
  switch (facing)
    {
    case FACING_DOWN:
      if (charbox.y+charbox.h<=target.y
	  && target.x<=charbox.x+charbox.w/2
	  && charbox.x+charbox.w/2 <= target.x+target.w)
	{
	  hitpart->x = charbox.x;
	  hitpart->y = target.y;
	  return 1;
	}
      break;
    case FACING_UP:
      if (charbox.y>=target.y+target.h
	  && target.x<=charbox.x+charbox.w/2
	  && charbox.x+charbox.w/2 <= target.x+target.w)
	{
	  hitpart->x = charbox.x;
	  hitpart->y = target.y+target.h-GRID_CELL_H;
	  return 1;
	}
      break;
    case FACING_RIGHT:
      if (charbox.x+charbox.w<=target.x
	  && target.y<=charbox.y+charbox.h/2
	  && charbox.y+charbox.h/2 <= target.y+target.h)
	{
	  hitpart->x = target.x;
	  hitpart->y = charbox.y;
	  return 1;
	}
      break;
    case FACING_LEFT:
      if (charbox.x>=target.x+target.w
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
get_shot_rect (SDL_Rect charbox, enum facing facing, SDL_Rect walkable,
	       SDL_Rect unwalkables[], int unwalkables_num, struct zombie *zs,
	       struct zombie **shotz, struct zombie **prevshotz)
{
  int i, found = 0;
  SDL_Rect ret, hitpart;
  struct zombie *pr = NULL;

  *shotz = NULL;

  for (i = 0; i < unwalkables_num; i++)
    {
      if (is_target_hit (charbox, facing, unwalkables [i], &hitpart))
	{
	  if (!found || is_closer (facing, hitpart, ret))
	    {
	      found = 1;
	      ret = hitpart;
	    }
	}
    }

  *prevshotz = NULL;

  while (zs)
    {
      if (is_target_hit (charbox, facing, zs->place, &hitpart))
	{
	  if (!found || is_closer (facing, hitpart, ret))
	    {
	      found = 1;
	      *shotz = zs;
	      *prevshotz = pr;
	      ret = hitpart;
	    }
	}

      pr = zs;
      zs = zs->next;
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
      ret.y = walkable.h;
      break;
    case FACING_UP:
      ret.x = charbox.x;
      ret.y = -GRID_CELL_H;
      break;
    case FACING_RIGHT:
      ret.x = walkable.w;
      ret.y = charbox.y;
      break;
    case FACING_LEFT:
      ret.x = -GRID_CELL_W;
      ret.y = charbox.y;
      break;
    }

  return ret;
}


void
send_server_state (int sockfd, uint32_t frame_counter, struct player *p,
		   struct player *pls, struct shot *ss)
{
  char buf [MAXMSGSIZE];
  struct message *msg = (struct message *) &buf;
  struct other_player opl;

  msg->type = htonl (MSG_SERVER_STATE);
  msg->args.server_state.frame_counter = frame_counter;
  msg->args.server_state.areaid = p->area->id;
  msg->args.server_state.x = p->place.x;
  msg->args.server_state.y = p->place.y;
  msg->args.server_state.w = p->place.w;
  msg->args.server_state.h = p->place.h;
  msg->args.server_state.char_facing = p->facing;
  msg->args.server_state.num_entities = 0;
  msg->args.server_state.num_shots = 0;

  while (pls)
    {
      if (sizeof (msg)+(msg->args.server_state.num_entities+1)
	  *sizeof (struct other_player) > MAXMSGSIZE)
	break;

      if (pls != p && pls->area == p->area)
	{
	  opl.x = pls->place.x;
	  opl.y = pls->place.y;
	  opl.w = pls->place.w;
	  opl.h = pls->place.h;
	  opl.facing = pls->facing;
	  opl.speed_x = pls->speed_x;
	  opl.speed_y = pls->speed_y;
	  memcpy (&buf [sizeof (*msg)+msg->args.server_state.num_entities
			*sizeof (opl)], &opl, sizeof (opl));
	  msg->args.server_state.num_entities++;
	}

      pls = pls->next;
    }

  while (ss)
    {
      if (sizeof (msg)+msg->args.server_state.num_entities
	  *sizeof (struct other_player)+(msg->args.server_state.num_entities+1)
	  *sizeof (struct SDL_Rect) > MAXMSGSIZE)
	break;

      if (ss->areaid == p->area->id)
	{
	  memcpy (&buf [sizeof (*msg)+msg->args.server_state.num_entities
			*sizeof (opl)+msg->args.server_state.num_shots
			*sizeof (struct SDL_Rect)], &ss->target, sizeof (ss->target));
	  msg->args.server_state.num_shots++;
	}

      ss = ss->next;
    }

  if (sendto (sockfd, buf,
	      sizeof (*msg)+msg->args.server_state.num_entities*sizeof (opl)
	      +msg->args.server_state.num_shots*sizeof (struct SDL_Rect),
	      0, (struct sockaddr *)&p->address, sizeof (p->address)) < 0)
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
  struct player *players = NULL, *p, *pl, *pr;
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

  struct warp *w;

  struct server_area field = {0};
  SDL_Rect field_walkable = {0, 0, 256, 256},
    field_unwalkables [] = {RECT_BY_GRID (1, 3, 4, 4),
    RECT_BY_GRID (1, 10, 3, 3), RECT_BY_GRID (10, 9, 2, 5),
    RECT_BY_GRID (13, 9, 2, 5), RECT_BY_GRID (12, 9, 1, 3)};

  struct server_area room = {0};
  SDL_Rect room_walkable = RECT_BY_GRID (0, 0, 12, 12),
    room_unwalkables [] = {RECT_BY_GRID (1, 6, 1, 3),
    RECT_BY_GRID (7, 2, 3, 3), RECT_BY_GRID (7, 5, 1, 2),
    RECT_BY_GRID (0, 11, 5, 1), RECT_BY_GRID (7, 11, 5, 1)};

  SDL_Event event;

  uint32_t frame_counter = 1;
  int char_hit, quit = 0;
  Uint32 t1, t2;
  double delay;


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

  field.id = 0;
  field.walkable = field_walkable;
  field.unwalkables = field_unwalkables;
  field.unwalkables_num = 5;
  field.warps = make_warp_by_grid (12, 13, 1, 1, &room, 5, 11, NULL);
  field.interactibles = NULL;
  field.zombies = NULL;
  field.zombies_num = 0;

  room.id = 1;
  room.walkable = room_walkable;
  room.unwalkables = room_unwalkables;
  room.unwalkables_num = 5;
  room.warps = make_warp_by_grid (5, 11, 2, 1, &field, 12, 14, NULL);
  room.interactibles = NULL;
  room.zombies = NULL;
  room.zombies_num = 0;

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
	      p = players;

	      while (p)
		{
		  if (!strcmp (p->name, msg->args.login.logname))
		    {
		      fprintf (stderr, "username %s already log in\n",
			       msg->args.login.logname);
		      send_message (sockfd, &client_addr,
				    ntohs (msg->args.login.portoff),
				    MSG_LOGINFAIL);
		      goto get_new_message;
		    }

		  p = p->next;
		}

	      players = create_player (msg->args.login.logname, &client_addr,
				       ntohs (msg->args.login.portoff), &field,
				       players);
	      printf ("created player %s with port offset %d\n",
		      msg->args.login.logname, players->portoffset);

	      msg->type = htonl (MSG_LOGINOK);
	      msg->args.loginok.id = htonl (players->id);

	      client_addr.sin_port = htons (ZOMBIELAND_PORT+players->portoffset);

	      if (sendto (sockfd, (char *)msg, sizeof (*msg), 0,
			  (struct sockaddr *) &client_addr, client_addr_sz) < 0)
		{
		  fprintf (stderr, "could not send data\n");
		  return 1;
		}
	      break;
	    case MSG_CLIENT_CHAR_STATE:
	      p = players, pl = NULL;

	      while (p)
		{
		  if (p->id == msg->args.client_char_state.id)
		    {
		      pl = p;
		      break;
		    }

		  p = p->next;
		}

	      if (!pl)
		{
		  fprintf (stderr, "got state from unknown id %d\n",
			   msg->args.client_char_state.id);
		}
	      else if (pl->last_update < msg->args.client_char_state.frame_counter)
		{
		  pl->speed_x = msg->args.client_char_state.char_speed_x;
		  pl->speed_y = msg->args.client_char_state.char_speed_y;
		  pl->facing = msg->args.client_char_state.char_facing;

		  if (!pl->shoot_rest && msg->args.client_char_state.do_shoot)
		    pl->shoot_rest = SHOOT_REST;

		  pl->last_update = msg->args.client_char_state.frame_counter;
		  pl->timeout = CLIENT_TIMEOUT;
		}
	      break;
	    default:
	      fprintf (stderr, "message type not known\n");
	      return 1;
	    }
	}

      p = players;

      while (p)
	{
	  p->place = move_character (p->place, p->speed_x, p->speed_y,
				     p->area->walkable, p->area->unwalkables,
				     p->area->unwalkables_num, NULL, &char_hit);

	  if (p->shoot_rest == SHOOT_REST)
	    {
	      s = malloc_and_check (sizeof (*s));
	      s->areaid = p->area->id;
	      s->target = get_shot_rect (p->place, p->facing, p->area->walkable,
					 p->area->unwalkables,
					 p->area->unwalkables_num, NULL, &z,
					 &prz);
	      s->duration = 10;
	      s->next = shots;
	      shots = s;
	    }

	  if (p->shoot_rest)
	    p->shoot_rest--;

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

	  w = p->area->warps;

	  while (w)
	    {
	      if (IS_RECT_CONTAINED (p->place, w->place))
		{
		  p->area = w->dest;
		  p->place.x = w->spawn.x;
		  p->place.y = w->spawn.y;
		  break;
		}

	      w = w->next;
	    }

	  p = p->next;
	}

      p = players;

      while (p)
	{
	  send_server_state (sockfd, frame_counter, p, players, shots);

	  p = p->next;
	}

      p = players, pr = NULL;

      while (p)
	{
	  p->timeout--;

	  if (!p->timeout)
	    {
	      printf ("player %s disconnected due to timeout\n", p->name);

	      if (pr)
		pr->next = p->next;
	      else
		players = p->next;

	      free (p);
	      p = pr ? pr->next : players;
	    }
	  else
	    {
	      pr = p;
	      p = p->next;
	    }
	}

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
