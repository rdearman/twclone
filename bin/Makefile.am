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

## AM_CFLAGS = -Wall -Wextra -Werror


server_LDADD   	= -ljansson -lsqlite3 -lcrypto -lpthread -lm
# bigbang_LDADD  = -ljansson -lsqlite3 -lpthread -lm
# client_LDADD   = -ljansson -lsqlite3 -lpthread -lm
bigbang_LDADD 	= -ljansson -lsqlite3 -lcrypto -lpthread -lm

AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = \
	server \
	bigbang

server_SOURCES = \
        ../src/server_communication.c \
        ../src/server_bulk.c \
        ../src/server_combat.c  \
        ../src/server_bigbang.c \
        ../src/server_cmds.c \
        ../src/server_citadel.c \
        ../src/server_envelope.c \
        ../src/server_auth.c \
        ../src/server_config.c \
        ../src/server_loop.c \
        ../src/server_main.c \
        ../src/server_news.c \
        ../src/server_planets.c \
        ../src/server_players.c \
        ../src/server_ports.c \
        ../src/server_ships.c \
        ../src/server_universe.c \
        ../src/server_s2s.c \
        ../src/server_warp_post_processing.c \
        ../src/server_log.c \
        ../src/server_cron.c \
        ../src/namegen.c \
        ../src/globals.c \
        ../src/database.c \
        ../src/schemas.c \
        ../src/common.c \
        ../src/server_engine.c \
        ../src/engine_consumer.c \
        ../src/db_player_settings.c \
        ../src/s2s_transport.c \
	../src/sysop_interaction.c \
	../src/s2s_keyring.c \
        ../src/server_stardock.c


bigbang_SOURCES = \
	../src/database.c \
	../src/schemas.c \
	../src/namegen.c \
	../src/server_bigbang.c \
	../src/server_warp_post_processing.c \
	../src/globals.c \
	../src/common.c \
	../src/test_bang.c \
	../src/server_log.c





