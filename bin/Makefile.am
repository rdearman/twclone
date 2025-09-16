# -*- makefile -*-
# $Id: Makefile.am,v 1.5 2004/01/21 04:00:53 eryndil Exp $
# Copyright (C) 2002, Nathan Paul Simons (npsimons@hardcorehackers.com)
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
# Alternatively, the GPL can be found at
# http://www.gnu.org/copyleft/gpl.html

AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = \
        bigbang \
        client \
        server \
        test_client \
        test_server

bigbang_SOURCES = \
        ../src/bigbang.c \
        ../src/config.c \
        ../src/config.h \
        ../src/namegen.c \
        ../src/namegen.h \
        ../src/parse.c \
        ../src/parse.h \
        ../src/planet.c \
        ../src/planet.h

client_SOURCES = \
        ../src/ansi.h \
        ../src/client.c \
        ../src/client.h \
        ../src/common.c \
        ../src/common.h \
        ../src/parse.c \
        ../src/parse.h

server_SOURCES = \
        ../src/boxmuller.c \
        ../src/boxmuller.h \
        ../src/common.c \
        ../src/common.h \
        ../src/config.c \
        ../src/config.h \
        ../src/hashtable.c \
        ../src/hashtable.h \
        ../src/maint.c \
        ../src/maint.h \
        ../src/msgqueue.c \
        ../src/msgqueue.h \
        ../src/parse.c \
        ../src/parse.h \
        ../src/planet.c \
        ../src/planet.h \
        ../src/player_interaction.c \
        ../src/player_interaction.h \
        ../src/portinfo.h \
        ../src/server.c \
        ../src/serveractions.c \
        ../src/serveractions.h \
        ../src/shipinfo.c \
        ../src/shipinfo.h \
        ../src/sysop_interaction.c \
        ../src/sysop_interaction.h \
        ../src/universe.c \
        ../src/universe.h

test_client_SOURCES = \
        ../src/common.c \
        ../src/common.h \
        ../src/test_client.c

test_server_SOURCES = \
        ../src/common.c \
        ../src/common.h \
        ../src/test_server.c
