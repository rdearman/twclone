/*
Copyright (C) 2002 Ryan Glasnapp(rglasnap@nmt.edu)

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

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "universe.h"
#include "shipinfo.h"
#include "hashtable.h"
#include "maint.h"
#include "config.h"
#include "msgqueue.h"
#include "common.h"
#include "serveractions.h"
#include "planet.h"

extern struct sector **sectors;
extern struct list *symbols[HASH_LENGTH];
extern struct player **players;
extern struct sp_shipinfo **shiptypes;
extern struct ship **ships;
extern struct port **ports;
extern struct planet **planets;
extern struct config *configdata;
extern time_t starttime;
extern int sectorcount;

time_t *timeptr;

void *
background_maint (void *threadinfo)
{
  time_t curtime;
  struct tm *timenow;
  int lastregen = -1;
  int lastday = -1;
  int loop = 0;
  int temp=0;

  free (threadinfo);

  while (1)
    {
      curtime = time (timeptr);
      timenow = localtime (&curtime);
      if (lastregen == -1)
			lastregen = timenow->tm_hour;
      if (lastday == -1)
			lastday = timenow->tm_yday;
      if ((curtime - starttime) % configdata->autosave * 60)	//Autosave
			;//saveall();
      if ((timenow->tm_hour == lastregen + 1)
	  || ((timenow->tm_hour == 0) && (lastregen == 23)))
		{
	  		lastregen = timenow->tm_hour;
	  		fprintf (stderr, "\nRegen turns by the hour!");
	  		loop = 0;
	  		for (loop=0; loop<configdata->max_players;loop++)
	    	{
				if (players[loop] != NULL)
				{
	      		players[loop]->turns =
						players[loop]->turns + configdata->turnsperday / 24;
	      		if (players[loop]->turns > configdata->turnsperday)
						players[loop]->turns = configdata->turnsperday;
	      		loop++;
				}
	    	}
			for (loop=0; loop<configdata->max_planets;loop++)
			{
				if (planets[loop]!=NULL)
				{
					if (planets[loop]->citdl->upgradestart != 0)
					{
						if ((curtime - planets[loop]->citdl->upgradestart) > 
								(planets[loop]->pClass->citadelUpgradeTime[planets[loop]->citdl->level]*(24*3600)))
						{
							planets[loop]->citdl->upgradestart = 0;
							planets[loop]->citdl->level = planets[loop]->citdl->level + 1;
						}
					}
					planets[loop]->fuel = planets[loop]->fuel +
				(planets[loop]->fuelColonist/planets[loop]->pClass->fuelProduction)/24;
					
					planets[loop]->organics = planets[loop]->organics +
			(planets[loop]->organicsColonist/planets[loop]->pClass->organicsProduction)/24;
					
					planets[loop]->equipment = planets[loop]->equipment +
		(planets[loop]->equipmentColonist/planets[loop]->pClass->equipmentProduction)/24;
					
					planets[loop]->fighters = planets[loop]->fighters +
			((planets[loop]->fuelColonist/planets[loop]->pClass->fuelProduction +
			 planets[loop]->organicsColonist/planets[loop]->pClass->organicsProduction +
			 planets[loop]->equipmentColonist/planets[loop]->pClass->equipmentProduction)
			 /planets[loop]->pClass->fighterProduction)/24;
					
					planets[loop]->fuelColonist = planets[loop]->fuelColonist +
					planets[loop]->fuelColonist*planets[loop]->pClass->breeding/24;
					
					planets[loop]->organicsColonist = 
						planets[loop]->organicsColonist +
				planets[loop]->organicsColonist*planets[loop]->pClass->breeding/24;
					
					planets[loop]->equipmentColonist = 
						planets[loop]->equipmentColonist +
				planets[loop]->equipmentColonist*planets[loop]->pClass->breeding/24;

					planets[loop]->citdl->treasury = planets[loop]->citdl->treasury
					 + planets[loop]->citdl->treasury*0.10;
				if (planets[loop]->fuel > planets[loop]->pClass->maxore)
					planets[loop]->fuel = planets[loop]->pClass->maxore;
				if (planets[loop]->organics > planets[loop]->pClass->maxorganics)
					planets[loop]->organics = planets[loop]->pClass->maxorganics;
				if (planets[loop]->equipment > planets[loop]->pClass->maxequipment)
					planets[loop]->equipment = planets[loop]->pClass->maxequipment;
				if (planets[loop]->fighters > planets[loop]->pClass->maxfighters)
					planets[loop]->fighters = planets[loop]->pClass->maxfighters;

				if (planets[loop]->fuelColonist > planets[loop]->pClass->maxColonist[0])
					planets[loop]->fuelColonist = planets[loop]->pClass->maxColonist[0];
				if (planets[loop]->organicsColonist > planets[loop]->pClass->maxColonist[1])
					planets[loop]->organicsColonist = planets[loop]->pClass->maxColonist[1];
				if (planets[loop]->equipmentColonist > planets[loop]->pClass->maxColonist[2])
					planets[loop]->equipmentColonist = planets[loop]->pClass->maxColonist[2];
				}
			}
		}
      if ((timenow->tm_yday == lastday + 1)
	  		|| ((timenow->tm_yday == 0) && (lastday == 365)))
		{
	  		lastday = timenow->tm_yday;
	  		fprintf (stderr, "\nRegen leftover turns!");
	  		for (loop=0;loop<configdata->max_players;loop++)
	    	{
				if (players[loop]!=NULL)
				{
	      		players[loop]->turns =
						players[loop]->turns + configdata->turnsperday % 24;
	      		if (players[loop]->turns > configdata->turnsperday)
						players[loop]->turns = configdata->turnsperday;
	      		loop++;
				}
	    	}
			for (loop=0; loop<configdata->max_planets;loop++)
			{
				if (planets[loop]!=NULL)
				{
					if (planets[loop]->citdl->upgradestart != 0)
					{
						if ((curtime - planets[loop]->citdl->upgradestart) > 
								(planets[loop]->pClass->citadelUpgradeTime[planets[loop]->citdl->level]*(24*3600)))
						{
							planets[loop]->citdl->upgradestart = 0;
							planets[loop]->citdl->level = planets[loop]->citdl->level + 1;
						}
					}
					planets[loop]->fuel = planets[loop]->fuel +
				(planets[loop]->fuelColonist/planets[loop]->pClass->fuelProduction) % 24;
					
					planets[loop]->organics = planets[loop]->organics +
			(planets[loop]->organicsColonist/planets[loop]->pClass->organicsProduction) % 24;
					
					planets[loop]->equipment = planets[loop]->equipment +
		(planets[loop]->equipmentColonist/planets[loop]->pClass->equipmentProduction) % 24;
					
					planets[loop]->fighters = planets[loop]->fighters +
			((planets[loop]->fuelColonist/planets[loop]->pClass->fuelProduction +
			 planets[loop]->organicsColonist/planets[loop]->pClass->organicsProduction +
			 planets[loop]->equipmentColonist/planets[loop]->pClass->equipmentProduction)
			 /planets[loop]->pClass->fighterProduction) % 24;
				
			temp = (int)planets[loop]->fuelColonist*planets[loop]->pClass->breeding;
					planets[loop]->fuelColonist = planets[loop]->fuelColonist +
							  temp % 24;
					
				temp = (int)planets[loop]->fuelColonist*planets[loop]->pClass->breeding;
				planets[loop]->organicsColonist = 
						planets[loop]->organicsColonist + temp % 24;
					
				temp = (int)planets[loop]->fuelColonist*planets[loop]->pClass->breeding;
				planets[loop]->equipmentColonist = 
						planets[loop]->equipmentColonist + temp % 24;

					planets[loop]->citdl->treasury = planets[loop]->citdl->treasury
					 + planets[loop]->citdl->treasury*0.10;
				if (planets[loop]->fuel > planets[loop]->pClass->maxore)
					planets[loop]->fuel = planets[loop]->pClass->maxore;
				if (planets[loop]->organics > planets[loop]->pClass->maxorganics)
					planets[loop]->organics = planets[loop]->pClass->maxorganics;
				if (planets[loop]->equipment > planets[loop]->pClass->maxequipment)
					planets[loop]->equipment = planets[loop]->pClass->maxequipment;
				if (planets[loop]->fighters > planets[loop]->pClass->maxfighters)
					planets[loop]->fighters = planets[loop]->pClass->maxfighters;

				if (planets[loop]->fuelColonist > planets[loop]->pClass->maxColonist[0])
					planets[loop]->fuelColonist = planets[loop]->pClass->maxColonist[0];
				if (planets[loop]->organicsColonist > planets[loop]->pClass->maxColonist[1])
					planets[loop]->organicsColonist = planets[loop]->pClass->maxColonist[1];
				if (planets[loop]->equipmentColonist > planets[loop]->pClass->maxColonist[2])
					planets[loop]->equipmentColonist = planets[loop]->pClass->maxColonist[2];
				}
			}
		}
      if ((curtime - starttime) % configdata->processinterval == 0)
		{
	  	//Process real time stuff?
		}
      if ((curtime - starttime) % 3 * configdata->processinterval == 0)
		{
	  //Alien movement here.
		}
	 }
}
