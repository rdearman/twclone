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

#ifdef HAVE_CONFIG_H
#  include <autoconf.h>
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_ERRNO_H
#  include <errno.h>
#endif /* HAVE_STDIO_H */

#ifdef HAVE_STDIO_H
#  include <stdio.h>
#endif /* HAVE_STDIO_H */

#ifdef HAVE_STDLIB_H
#  include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include "config.h"
#include "parse.h"

extern struct config *configdata;

#define init_config_cleanup()			\
  {						\
    if (configfile != NULL)			\
      {						\
        fclose (configfile);			\
        configfile = NULL;			\
      }						\
  }

#define init_config_error(a)			\
  {						\
    perror (a);					\
    init_config_cleanup ();			\
    return (errno);				\
  }

int init_config (char *filename)
{
  FILE *configfile = NULL;
  char buffer[BUFF_SIZE];
  char bangdate[BUFF_SIZE];

  if ((configdata =
       (struct config *) malloc (sizeof (struct config))) == NULL)
    {
      fprintf (stderr,
	       "Unable to allocate enough memory for configuration data\n");
      exit (-1);
    }

  configfile = fopen (filename, "rb");
  if (configfile == NULL)
    {
      init_config_error (filename);
      /* 
	 I don't like the way this works. I think that if it can't find a config
	 then it generates a config.data.default file, tells the user and closes.
	 This allows the user to rename the config.data.default file to config.data
	 and re-run the program.
      */
      /*
	fprintf(stdout, "Can't find config.data file. Generating config.data.default file, rename to config.data and re-run.\n");
	fclose(configfile);
	configdata->turnsperday = TURNS_PER_DAY;
	configdata->maxwarps = MAX_WARPS;
	configdata->startingcredits = START_CREDITS;
	configdata->startingfighters = START_FIGHTERS ;
	configdata->startingholds = START_HOLDS ;
	configdata->processinterval = PROCESS_INTERVAL; 
	configdata->autosave = AUTOSAVE;
	configdata->max_players = MAX_PLAYERS ;
	configdata->max_ships = MAX_SHIPS;
	configdata->max_ports = MAX_PORTS;
	configdata->max_planets = MAX_TOTAL_PLANETS / 2 ;
	configdata->max_total_planets = MAX_TOTAL_PLANETS ;
	configdata->max_safe_planets = MAX_TOTAL_PLANETS / 2 ; 
	configdata->max_citadel_level = MAX_CITADEL_LEVEL;
	configdata->number_of_planet_types = NUMBER_OF_PLANET_TYPES;
	configdata->max_ship_name_length = MAX_SHIP_NAME;
	configdata->ship_type_count = SHIP_TYPES;
	time_t datenow;
	datenow = time(NULL);
	configdata->bangdate = (unsigned long)datenow;
	configdata->numnodes = DEFAULT_NODES; 
	saveconfig("config.data.default");
	exit (1);
      */
    }

  if (fgets (buffer, BUFF_SIZE, configfile))
    {
      configdata->turnsperday = popint (buffer, ":");
      configdata->maxwarps = popint(buffer, ":");
      configdata->startingcredits = popint (buffer, ":");
      configdata->startingfighters = popint (buffer, ":");
      configdata->startingholds = popint (buffer, ":");
      configdata->processinterval = popint (buffer, ":");
      configdata->autosave = popint (buffer, ":");
      configdata->max_players = popint(buffer, ":");
      configdata->max_ships = popint(buffer, ":");
      configdata->max_ports = popint(buffer, ":");
      configdata->max_planets = popint(buffer, ":");
      configdata->max_total_planets = popint(buffer, ":");
      configdata->max_safe_planets = popint(buffer, ":");
      configdata->max_citadel_level = popint(buffer, ":");
      configdata->number_of_planet_types = popint(buffer, ":");
      configdata->max_ship_name_length = popint(buffer, ":");
      configdata->ship_type_count = popint(buffer, ":");
      popstring(buffer, bangdate, ":", BUFF_SIZE);
      configdata->bangdate = strtoul(bangdate,NULL,10); 
      configdata->numnodes = popint(buffer, ":");

      fclose(configfile);
    }
  return (1);
}

int saveconfig(char *filename)
{
  FILE *configfile = NULL;
  char *buffer = (char *)malloc(sizeof(char)*BUFF_SIZE);
  char *bdate = (char *)malloc(sizeof(char)*BUFF_SIZE);
  int loop=0;
  
  configfile = fopen (filename, "w");

  strcpy(buffer, "\0");
  strcpy(bdate, "\0");

  addint(buffer, configdata->turnsperday, ':', BUFF_SIZE);
  addint(buffer, configdata->maxwarps, ':', BUFF_SIZE);
  addint(buffer, configdata->startingcredits, ':', BUFF_SIZE);
  addint(buffer, configdata->startingfighters, ':', BUFF_SIZE);
  addint(buffer, configdata->startingholds, ':', BUFF_SIZE);
  addint(buffer, configdata->processinterval, ':', BUFF_SIZE);
  addint(buffer, configdata->autosave, ':', BUFF_SIZE);
  addint(buffer, configdata->max_players, ':', BUFF_SIZE);
  addint(buffer, configdata->max_ships, ':', BUFF_SIZE);
  addint(buffer, configdata->max_ports, ':', BUFF_SIZE);
  addint(buffer, configdata->max_planets, ':', BUFF_SIZE);
  addint(buffer, configdata->max_total_planets, ':', BUFF_SIZE);
  addint(buffer, configdata->max_safe_planets, ':', BUFF_SIZE);
  addint(buffer, configdata->max_citadel_level, ':', BUFF_SIZE);
  addint(buffer, configdata->number_of_planet_types, ':', BUFF_SIZE);
  addint(buffer, configdata->max_ship_name_length, ':', BUFF_SIZE);
  addint(buffer, configdata->ship_type_count, ':', BUFF_SIZE);
  sprintf(bdate, "%ld", configdata->bangdate);
  addstring(buffer, bdate, ':', BUFF_SIZE);
  addint(buffer, configdata->numnodes, ':', BUFF_SIZE);

  for (loop=0; loop < 199 - strlen(buffer); loop++)
    strcat(buffer, " ");
  strcat(buffer, "\n");

  fprintf(configfile, "%s",  buffer);

  fclose(configfile);
  free(buffer);
  free(bdate);
  return (1);
}
