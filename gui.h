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



#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>



#define HUD_FONT_SIZE 12



SDL_Texture *render_string (const char *string, TTF_Font *font, SDL_Color col,
			    SDL_Renderer *rend, int *w, int *h);
void display_string (const char *string, SDL_Rect rect, TTF_Font *font, SDL_Color col,
		     SDL_Renderer *rend);
void display_strings_centrally (TTF_Font *font, int scaling, SDL_Color col,
				SDL_Renderer *rend, int cursor, ...);
