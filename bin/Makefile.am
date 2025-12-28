# -*- makefile -*-
# $Id: Makefile.am,v 1.5 2004/01/21 04:00:53 eryndil Exp $
# Copyright (C) 2002, Nathan Paul Simons (npsimons@hardcorehackers.com)
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License for published by
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

# AM_CFLAGS = -Wall -Wextra -Werror -I../src

# AM_CFLAGS = -Wall -Wextra
AM_CFLAGS = -Wall -Wextra -D_GNU_SOURCE -I../src -DDB_BACKEND_PG -ljansson -lpq

server_LDADD   	= -ljansson -lcrypto -lpthread -lm -lpq
AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = \
	server \
	bigbang


# BigBang sources (PostgreSQL Only)
bigbang_SOURCES = \
        ../src/bigbang_pg_main.c \
        ../src/db/db_api.c \
        ../src/db/pg/db_pg.c \
        ../src/common.c \
        ../src/server_log.c
bigbang_LDFLAGS = -ljansson -lpq
bigbang_LDADD = -ljansson -lpq -lcrypto -lpthread -lm $(server_LDADD)


# Server sources (now with DB abstraction)
server_SOURCES = \
	../src/common.c \
	../src/db/db_api.c \
	../src/db/pg/db_pg.c \
	../src/game_db.c \
	../src/database_cmd.c \
	../src/database_market.c \
	../src/db_player_settings.c \
	../src/engine_consumer.c \
	../src/globals.c \
	../src/s2s_keyring.c \
	../src/s2s_transport.c \
	../src/schemas.c \
	../src/server_auth.c \
	../src/server_autopilot.c \
	../src/server_bank.c \
	../src/server_bulk.c \
	../src/server_citadel.c \
	../src/server_clusters.c \
	../src/server_cmds.c \
	../src/server_combat.c  \
	../src/server_communication.c \
	../src/server_config.c \
	../src/server_corporation.c \
	../src/server_cron.c \
	../src/server_engine.c \
	../src/server_envelope.c \
	../src/server_log.c \
	../src/server_loop.c \
	../src/server_main.c \
	../src/server_news.c \
	../src/server_planets.c \
	../src/server_players.c \
	../src/server_ports.c \
	../src/server_s2s.c \
	../src/server_ships.c \
	../src/server_stardock.c \
	../src/server_universe.c \
	../src/server_warp_post_processing.c \
	../src/sysop_interaction.c
