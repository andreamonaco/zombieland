# Copyright (C) 2025 Andrea Monaco
#
# This file is part of zombieland, an MMO game.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#



AC_PREREQ([2.69])
AC_INIT([zombieland], [0.1], [monacoandrea94@gmail.com])

AM_INIT_AUTOMAKE([-Wall -Werror foreign])


AC_CONFIG_SRCDIR([zombieland.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])


AC_PROG_CC


AC_CHECK_HEADERS([stdio.h])


AC_CHECK_LIB([SDL2], [SDL_Init], [true], [AC_MSG_ERROR([SDL2 not found])])

AC_CHECK_LIB([SDL2_image], [IMG_Init], [true], [AC_MSG_ERROR([SDL2_image not found])])

AC_CHECK_LIB([SDL2_ttf], [TTF_Init], [true], [AC_MSG_ERROR([SDL2_ttf not found])])



AC_OUTPUT
