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
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
# GNU General Public License for more details.
#   
# You should have received a copy of the GNU General Public License 
# along with this program; if not, write to the Free Software 
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
#
# Alternatively, the GPL can be found at 
# http://www.gnu.org/copyleft/gpl.html

EXTRA_DIST = \
	config.data \
	planettypes.data \
	shiptypes.data

pkgdata_DATA = \
	config.data \
	planettypes.data \
	shiptypes.data

bin_PROGRAMS = \
	bigbang \
	client \
	server \
	test_client \
	test_server

bigbang_SOURCES = \
	bigbang.c \
	config.c \
	config.h \
	namegen.c \
	namegen.h \
	parse.c \
	parse.h \
	planet.c \
	planet.h

client_SOURCES = \
	ansi.h \
	client.c \
	client.h \
	common.c \
	common.h \
	parse.c \
	parse.h

server_SOURCES = \
	boxmuller.c \
	boxmuller.h \
	common.c \
	common.h \
	config.c \
	config.h \
	hashtable.c \
	hashtable.h \
	maint.c \
	maint.h \
	msgqueue.c \
	msgqueue.h \
	parse.c \
	parse.h \
	planet.c \
	planet.h \
	player_interaction.c \
	player_interaction.h \
	portinfo.h \
	server.c \
	serveractions.c \
	serveractions.h \
	shipinfo.c \
	shipinfo.h \
	sysop_interaction.c \
	sysop_interaction.h \
	universe.c \
	universe.h

test_client_SOURCES = \
	common.c \
	common.h \
	test_client.c

test_server_SOURCES = \
	common.c \
	common.h \
	test_server.c

