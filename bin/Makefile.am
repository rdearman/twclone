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

server_LDADD = -ljansson
AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = \
	bigbang \
	client \
	server \
	test_client \
	test_server

bigbang_SOURCES = \
	../src/globals.c \
	../src/bigbang.c \
	../src/config.c \
	../src/namegen.c \
	../src/parse.c \
	../src/planet.c

client_SOURCES = \
	../src/globals.c \
	../src/client.c \
	../src/common.c \
	../src/parse.c

server_SOURCES = \
	../src/globals.c \
	../src/boxmuller.c \
	../src/common.c \
	../src/config.c \
	../src/hashtable.c \
	../src/maint.c \
	../src/msgqueue.c \
	../src/parse.c \
	../src/planet.c \
	../src/player_interaction.c \
	../src/server.c \
	../src/serveractions.c \
	../src/shipinfo.c \
	../src/sysop_interaction.c \
	../src/universe.c

test_client_SOURCES = \
	../src/globals.c \
	../src/common.c \
	../src/test_client.c

test_server_SOURCES = \
	../src/globals.c \
	../src/common.c \
	../src/test_server.c
