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

/*
  originally by Ryan
  modified Jason Garcowski 
  7/15/00
 
  universe.c
 
  Contains all of the functions to init the universe
*/

/*
  Important note, these should be called in the order:
  universe, ship, player
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "shipinfo.h"
#include "parse.h"
#include "hashtable.h"
#include "universe.h"
#include "common.h"
#include "planet.h"
#include "config.h"

extern struct list *symbols[HASH_LENGTH];
extern struct player **players;
extern struct planet **planets;
extern struct ship **ships;
extern struct port **ports;
extern struct sector **sectors;
extern struct config *configdata;
extern struct node **nodes;

int
init_universe (char *filename, struct sector ***array)
{
  int sectorcount = 0, len, pos, sectornum, i, tempsector, sctptrcount, loop;
  FILE *univinfo;
  char buffer[BUFF_SIZE], temp[BUFF_SIZE];

  univinfo = fopen (filename, "r");
  if (univinfo == NULL)
    {
      fprintf (stderr,
	       "\ninit_universe: No sector data file. Please rerun bigbang!\n");
      exit (-1);
    }
  (*array) = NULL;

  do
    {
      buffer[0] = '\0';

      fgets (buffer, BUFF_SIZE, univinfo);

      sectornum = popint (buffer, ":");

      if (sectornum == 0)
	break;

      if (sectornum > sectorcount)
        {
	  //allocate enough pointers in the array
	  (*array) =
	    (struct sector **) realloc ((*array),
					sectornum * sizeof (struct sector *));

	  //attach the newly allocated sectors to the array
	  for (i = sectorcount; i < sectornum; i++)
	    (*array)[i] = (struct sector *) malloc (sizeof (struct sector));

	  sectorcount = sectornum;
        }
      pos = len;

      (*array)[sectornum - 1]->number = sectornum;
      //make sure all of the sector links are null
      for ( i = 0; i < MAX_WARPS_PER_SECTOR; i++)
	(*array)[sectornum - 1]->sectorptr[i] = NULL;

      sctptrcount = 0;

      popstring (buffer, temp, ":", BUFF_SIZE);

      while ((tempsector = popint (temp, ",")) != 0
	     && sctptrcount < MAX_WARPS_PER_SECTOR)
        {
	  //fprintf(stderr, "init_universe: tempsector = %d, sectornum = %d\n", tempsector, sectornum);
	  if (tempsector > sectorcount)
            {
	      //allocate enough pointers in the array
	      (*array) =
		(struct sector **) realloc ((*array), 
					    tempsector * sizeof (struct sector *));

	      //attach the newly allocated sectors to the array
	      for (i = sectorcount; i < tempsector; i++)
		(*array)[i] =
		  (struct sector *) malloc (sizeof (struct sector));

	      sectorcount = tempsector;

	      /*I set it to zero now so I can test to make sure it has its own entry */
	      (*array)[tempsector - 1]->number = 0;
            }

	  /*make the link from our current sector to where it points. */
	  (*array)[sectornum - 1]->sectorptr[sctptrcount++] =
	    (*array)[tempsector - 1];
        }

      /*set the last pointer to NULL if applicable */
      if (sctptrcount < MAX_WARPS_PER_SECTOR)
	(*array)[sectornum - 1]->sectorptr[sctptrcount] = NULL;
      /*copy the beacon info over */
      if (strncmp (buffer, ":", 1) != 0)
        {	/* This is in case there are is no nebulaes since popstring doesn't
      		 * differentiate between ":<stuff>: and <stuff>: It thinks that its 
      		 * the same thing.
      		 */
	  popstring (buffer, temp, ":", BUFF_SIZE);
	  (*array)[sectornum - 1]->beacontext =
	    (char *) malloc (strlen (temp) + 1);
	  strncpy ((*array)[sectornum - 1]->beacontext, temp,
		   strlen (temp) + 1);
	  (*array)[sectornum - 1]->beacontext[strlen (temp)] = '\0';
	  if (strlen (temp) == 0)
	    strcpy ((*array)[sectornum - 1]->beacontext, "\0");
        }
      else
        {
	  for (loop = 0; loop < strlen (buffer); loop++)
	    buffer[loop] = buffer[loop + 1];
	  (*array)[sectornum - 1]->beacontext =
	    (char *) malloc (strlen ("\0") + 1);
	  strcpy ((*array)[sectornum - 1]->beacontext, "\0");
        }

      /*copy the nebulae info over */
      popstring (buffer, temp, ":", BUFF_SIZE);
      (*array)[sectornum - 1]->nebulae = (char *) malloc (strlen (temp) + 1);
      strncpy ((*array)[sectornum - 1]->nebulae, temp, strlen (temp) + 1);
      (*array)[sectornum - 1]->nebulae[strlen (temp)] = '\0';
      if (strlen (temp) == 0)
	strcpy ((*array)[sectornum - 1]->nebulae, "\0");
      init_hash_table ((*array)[sectornum - 1]->playerlist, 1);
      init_hash_table ((*array)[sectornum - 1]->shiplist, 1);
      //init_hash_table ((*array)[sectornum - 1]->planets, 1);
      (*array)[sectornum - 1]->portptr = NULL;
      (*array)[sectornum - 1]->planets = NULL;
    }
  while (1);

  fclose (univinfo);
  return sectorcount;
}

void init_playerinfo (char *filename)
{
  FILE *playerinfo;
  char name[MAX_NAME_LENGTH], passwd[MAX_NAME_LENGTH];
  char buffer[BUFF_SIZE];
  char credits[100];
  char balance[100];
  int playernum;
  struct player *curplayer;

  players = (struct player **)
    malloc(sizeof(struct player *)*configdata->max_players);
  for (playernum = 0; playernum < configdata->max_players; playernum++)
    players[playernum] = NULL;

  playerinfo = fopen (filename, "r");
  if (playerinfo == NULL)
    {
      fprintf (stderr, "\ninit_playerinfo: No playerfile.");
      return;
    }
  while (1)
    {
      buffer[0] = '\0';
      fgets (buffer, BUFF_SIZE, playerinfo);
      if (strlen (buffer) == 0)
	break;
      playernum = popint (buffer, ":");
      popstring (buffer, name, ":", MAX_NAME_LENGTH);
      popstring (buffer, passwd, ":", MAX_NAME_LENGTH);

      //fprintf(stderr, "init_playerinfo: popped name '%s' & passwd '%s', buffer = '%s'\n",
      //    name, passwd, buffer);

      if ((curplayer =
	   (struct player *) insert (name, player, symbols,
				     HASH_LENGTH)) == NULL)
        {
	  fprintf (stderr, "init_playerinfo: duplicate player name '%s'\n",
		   name);
	  return (-1000);
        }

      curplayer->sector = popint(buffer, ":");
      curplayer->ship = popint(buffer, ":");
      curplayer->number = playernum;
      curplayer->experience = popint(buffer, ":");
      curplayer->alignment = popint(buffer, ":");
      curplayer->turns = popint(buffer, ":");
      popstring(buffer, credits, ":", 100);
      curplayer->credits = strtoul(credits,NULL,10);
      //curplayer->credits = popint(buffer, ":");
      popstring(buffer, balance, ":", 100);
      curplayer->bank_balance = strtoul(balance,NULL,10);
      curplayer->flags = popint(buffer, ":");
      //curplayer->bank_balance = popint(buffer, ":");
      curplayer->name = (char *) malloc (strlen (name) + 1);
      curplayer->passwd = (char *) malloc (strlen (passwd) + 1);
      strncpy (curplayer->name, name, strlen (name) + 1);
      strncpy (curplayer->passwd, passwd, strlen (passwd) + 1);
      curplayer->loggedin = 0;
      curplayer->lastprice = 0;
      curplayer->firstprice = 0;
      curplayer->intransit = 0;
      curplayer->beginmove = 0;
      curplayer->movingto = 0;
      curplayer->messages = NULL;
      curplayer->lastplanet = 0;

      if (players[playernum - 1] != NULL)
        {
	  fprintf (stderr,
		   "init_playinfo: duplicate player numbers, exiting...\n");
	  return (-1500);
        }
      if (curplayer->ship == 0)
	{
	  players[playernum - 1]=NULL;
	  curplayer = delete(name, player, symbols, HASH_LENGTH);
	}
      else
	{
	  players[playernum - 1] = curplayer;

	  if (ships[curplayer->ship-1]->onplanet == 0)
	    {
	      if (insertitem (curplayer, player,
			      sectors[(curplayer->sector == 0) ?
				      ships[curplayer->ship - 1]->location - 1 :
				      (curplayer->sector - 1)]->playerlist,
			      1) == NULL)
		{
		  fprintf (stderr,
			   "init_playerinfo: unable to add player '%s'to playerlist in sector %d!\n",
			   name,
			   (curplayer->sector ==
			    0) ? ships[curplayer->ship -
				       1]->location : (curplayer->sector));
		  return (-2000);
		}
	    }


	  //Here is where I need to tack this onto the playerlist

	  //printf("init_playerinfo: adding '%s' with passwd '%s', in sector '%d'\n",
	  //   name, passwd, curplayer->sector);
	}
    }

  fclose (playerinfo);
}

void init_shipinfo (char *filename)
{
  FILE *shipfile;
  char buffer[BUFF_SIZE];
  char name[MAX_NAME_LENGTH];
  int x;
  struct ship *curship;

  ships = (struct ship **)
    malloc(sizeof(struct ship *)*configdata->max_ships);
  for (x = 0; x < configdata->max_ships; x++)
    ships[x] = NULL;

  shipfile = fopen (filename, "r");
  if (shipfile == NULL)
    {
      fprintf (stderr, "\ninit_shipinfo: No ship file");
      return;
    }
  while (1)
    {
      buffer[0] = '\0';
      fgets (buffer, BUFF_SIZE, shipfile);
      if (strlen (buffer) == 0)
	break;
      x = popint (buffer, ":");
      popstring (buffer, name, ":", MAX_NAME_LENGTH);
      if ((curship =
	   (struct ship *) insert (name, ship, symbols, HASH_LENGTH)) == NULL)
        {
	  fprintf (stderr, "init_shipinfo: duplicate shipname '%s'\n", name);
	  exit (-1);
        }
      curship->number = x;
      curship->name = (char *) malloc (strlen (name) + 1);
      strcpy (curship->name, name);
      if ((curship->type = popint (buffer, ":")) > 
	  configdata->ship_type_count)
        {
	  fprintf (stderr, "init_shipinfo: bad ship type number\n");
	  exit (-1);
        }
      curship->location = popint (buffer, ":");
      curship->fighters = popint (buffer, ":");
      curship->shields = popint (buffer, ":");
      curship->holds = popint (buffer, ":");
      curship->colonists = popint (buffer, ":");
      curship->equipment = popint (buffer, ":");
      curship->organics = popint (buffer, ":");
      curship->ore = popint (buffer, ":");
      curship->owner = popint (buffer, ":");
      curship->flags = popint(buffer, ":");
      curship->onplanet = popint(buffer, ":");
      if (ships[x - 1] != NULL)
        {
	  fprintf (stderr,
		   "init_shipinfo: duplicate ship numbers, exiting...\n");
	  exit (-1);
        }
      if (curship->location == 0)
	{
	  ships[x - 1] = NULL;
	  curship = delete(name,ship,symbols,HASH_LENGTH);
	}
      else
	{
	  ships[x - 1] = curship;
	}
    }
  fclose (shipfile);

  return;
}

void init_portinfo (char *filename)
{

  FILE *portfile;
  int counter;			//Counter and other general usage
  char buffer[BUFF_SIZE];
  char name[250];
  //    char *tmpname;
  int maxStringSize = 250;


  struct port *curport;
  ports = (struct port **)
    malloc(sizeof(struct port *)*configdata->max_ports + 10);

  for (counter = 0; counter <= configdata->max_ports; counter++)
    ports[counter] = NULL;

  portfile = fopen (filename, "r");
  if (portfile == NULL)
    {
      fprintf (stderr,
	       "\ninit_portinfo: No port file! Please rerun bigbang!");
      exit (-1);
    }


  while (fgets (buffer, maxStringSize, portfile) != NULL )
    {
      //tmpname = malloc (sizeof (maxStringSize));
      counter = popint (buffer, ":");
      // I think his popstring function can't deal with long strings (more than 25 characters)
      // So I'm going to have to fix that, before I can return here.
      // popstring (buffer, tmpname, ":", maxStringSize); <<== should be this not name[25]

      popstring (buffer, name, ":", maxStringSize);
      if ((curport =
	   (struct port *) insert (name, port, symbols, HASH_LENGTH)) == NULL)
        {
	  fprintf (stderr, "init_portinfo: duplicate portname '%s'\n", name);
	  exit (-1);
        }
      curport->number = counter;
      curport->location = popint (buffer, ":");
      curport->maxproduct[0] = popint (buffer, ":");	//MaxOre
      curport->maxproduct[1] = popint (buffer, ":");	//MaxOrganics
      curport->maxproduct[2] = popint (buffer, ":");	//MaxEquipment
      curport->product[0] = popint (buffer, ":");	//Current Ore
      curport->product[1] = popint (buffer, ":");	//Current Organics
      curport->product[2] = popint (buffer, ":");	//Current Equipment
      curport->credits = popint (buffer, ":");
      curport->type = popint (buffer, ":");
      curport->invisible = popint (buffer, ":");
      curport->name = (char *) malloc (strlen (name) + 1);
      strcpy (curport->name, name);

      if (ports[counter - 1] != NULL)
        {
	  fprintf (stderr,
		   "init_portinfo: Duplicate port numbers, exiting...\n");
	  exit (-1);
        }
      ports[counter - 1] = curport;
      sectors[curport->location - 1]->portptr = curport;
    }
  fclose (portfile);

}

void init_nodes(int numsectors)
{
  int counter;
  int portcount;

  nodes = (struct node **)malloc(sizeof(struct node *)*configdata->numnodes);
  if (configdata->numnodes == 1)
    {
      nodes[0] = (struct node *)malloc(sizeof (struct node));
      nodes[0]->number = 1;
      nodes[0]->min = 1;
      nodes[0]->max = numsectors;
      return;
    }
  for (counter=0; counter < configdata->numnodes; counter++)
    {
      nodes[counter] = (struct node *)malloc(sizeof(struct node));
      nodes[counter]->number = counter+1;
      nodes[counter]->min = (int)(counter)*(float)(numsectors)/(float)configdata->numnodes + 1.0;
      nodes[counter]->max = (int)(counter + 1.0)*(float)(numsectors)/(float)configdata->numnodes;
      nodes[counter]->portptr = NULL;
      for (portcount = 0; portcount < configdata->max_ports; portcount++)
	{
	  if (ports[portcount]!=NULL)
	    {
	      if (ports[portcount]->type == 10)
		{
		  if ((ports[portcount]->location >= nodes[counter]->min) &&
		      (ports[portcount]->location <= nodes[counter]->max))
		    {
		      nodes[counter]->portptr = ports[portcount];
		      portcount = configdata->max_ports+1;
		    }
		}
	    }
	}
    }
}
//This stuff isn't used right now.
int verify_universe (struct sector **array, int sectorcount)
{
  int i;

  for (i = 0; i < sectorcount; i++)
    {
      if (array[i] == NULL)
        {
	  printf (" Sector %d does not exist!...", i + 1);
	  return -1;
        }
      else if (verify_sector_links (array[i]) == -1)
	return -1;
    }

  return 0;
}

int verify_sector_links (struct sector *test)
{
  int i, j, good;

  if (test->sectorptr[0] == NULL)
    {
      printf (" Sector %d has no links!...", test->number);
      return -1;
    }

  for (i = 0; i < MAX_WARPS_PER_SECTOR; i++)
    {
      if (test->sectorptr[i] == NULL)
	break;
      else
        {
	  good = 0;
	  for (j = 0; j < MAX_WARPS_PER_SECTOR; j++)
            {
	      if (test->sectorptr[i]->sectorptr[j] == NULL)
		break;
	      else if (test->sectorptr[i]->sectorptr[j] == test)
		good = 1;
            }
	  if (good == 0)
            {
	      printf
                ("Sector %d is linked to Sector %d, but not vice versa!...",
                 test->number, test->sectorptr[i]->number);
	      return -1;
            }
        }
    }

  return 0;
}
