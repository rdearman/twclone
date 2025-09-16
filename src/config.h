/*
Copyright (C) 2000 Jason C. Garcowski(jcg5@po.cwru.edu), 
                   Ryan Glasnapp(rglasnap@nmt.edu)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#ifndef CONFIG_H
#define CONFIG_H

#include "common.h"

struct config
{
  int turnsperday;		//In this order in config.data
  int maxwarps;
  int startingcredits;
  int startingfighters;
  int startingholds;
  int processinterval;		//How often stuff is processed
  int autosave;			//Save every some odd minutes
  int max_players;
  int max_ships;
  int max_ports;
  int max_planets;
  int max_total_planets;
  int max_safe_planets;
  int max_citadel_level;
  int number_of_planet_types;
  int max_ship_name_length;
  int ship_type_count;
  unsigned long bangdate;
  int numnodes;
};

int init_config (char *filename);
int saveconfig(char *filename);

#endif
