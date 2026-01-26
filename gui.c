/*  Copyright (C) 2026 Andrea Monaco
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



#include <netinet/in.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "zombieland.h"
#include "gui.h"



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


int
move_in_menu (int menu_cursor, SDL_Keycode direction, int num_elements,
	      int num_displayed_elements, int *display_cursor)
{
  menu_cursor += direction == SDLK_UP ? -1 : 1;
  *display_cursor += direction == SDLK_UP ? -1 : 1;

  if (menu_cursor < 0)
    {
      menu_cursor = num_elements-1;
      *display_cursor = num_elements < num_displayed_elements ? num_elements-1
	: num_displayed_elements-1;
    }
  else if (menu_cursor >= num_elements)
    {
      menu_cursor = *display_cursor = 0;
    }
  else
    {
      if (*display_cursor < 0)
	*display_cursor = 0;
      else if (*display_cursor >= num_displayed_elements)
	*display_cursor = num_displayed_elements-1;
    }

  return menu_cursor;
}


void
display_menu (TTF_Font *font, int scaling, SDL_Color col, SDL_Renderer *rend,
	      struct menu *menu, int menu_cursor, int submenu_cursor,
	      int display_cursor)
{
  SDL_Texture *str;
  SDL_Rect rect, cursorrect;
  int i, xcenter = WINDOW_WIDTH*scaling/2, ystep;

  ystep = WINDOW_HEIGHT*scaling/5;
  rect.y = ystep/2-HUD_FONT_SIZE*scaling/2;

  str = render_string (menu->title, font, col, rend, &rect.w, &rect.h);
  rect.x = xcenter-rect.w/2;
  SDL_RenderCopy (rend, str, NULL, &rect);
  SDL_DestroyTexture (str);

  rect.y += ystep*2;

  for (i = 0; i < 3; i++)
    {
      if (i+menu_cursor-display_cursor >= menu->num_elements)
	break;

      str = render_string (menu->elements [i+menu_cursor-display_cursor].caption,
			   font, col, rend, &rect.w, &rect.h);

      if (menu->elements [i+menu_cursor-display_cursor].right_caption)
	{
	  rect.x = 30*scaling;
	  SDL_RenderCopy (rend, str, NULL, &rect);
	  SDL_DestroyTexture (str);

	  if (i == display_cursor && !submenu_cursor)
	    {
	      cursorrect.x = rect.x-20*scaling;
	      cursorrect.y = rect.y-10*scaling;
	      cursorrect.w = rect.w+40*scaling;
	      cursorrect.h = rect.h+20*scaling;

	      SDL_SetRenderDrawColor (rend, 0, 0, 0, 255);
	      SDL_RenderDrawRect (rend, &cursorrect);
	    }

	  str = render_string (menu->elements [i+menu_cursor-display_cursor].
			       right_caption,
			       font, col, rend, &rect.w, &rect.h);
	  rect.x = WINDOW_WIDTH*scaling-30*scaling-rect.w;

	  if (i == display_cursor && submenu_cursor)
	    {
	      cursorrect.x = rect.x-20*scaling;
	      cursorrect.y = rect.y-10*scaling;
	      cursorrect.w = rect.w+40*scaling;
	      cursorrect.h = rect.h+20*scaling;

	      SDL_SetRenderDrawColor (rend, 0, 0, 0, 255);
	      SDL_RenderDrawRect (rend, &cursorrect);
	    }

	  SDL_RenderCopy (rend, str, NULL, &rect);
	  SDL_DestroyTexture (str);
	}
      else
	{
	  rect.x = xcenter-rect.w/2;
	  SDL_RenderCopy (rend, str, NULL, &rect);

	  if (i == display_cursor)
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

      rect.y += ystep;
    }
}
