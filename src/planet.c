#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "planet.h"
#include "parse.h"
#include "config.h"
#include "universe.h"

// Provide the single definition for the variables.
planetClass **planetTypes = NULL;
struct planet **planets = NULL;


extern struct config *configdata;
extern struct sector **sectors;
/*
 *	init_planets(filename, secarray)
 *	loads planet info from file.  returns number
 *	of planets in the universe when done
 */

void
saveplanets (char *filename)
{
  FILE *planetfile;
  int loop;
  int index = 0;
  char *treasury = (char *) malloc (sizeof (char) * BUFF_SIZE);
  char *stufftosave = (char *) malloc (sizeof (char) * BUFF_SIZE);

  planetfile = fopen (filename, "w");

  for (index = 0; index < configdata->max_total_planets; index++)
    {
      if (planets[index] != NULL)
	{
	  strcpy (stufftosave, "\0");
	  strcpy (treasury, "\0");
	  addint (stufftosave, planets[index]->num, ':', BUFF_SIZE);
	  addint (stufftosave, planets[index]->sector, ':', BUFF_SIZE);
	  addstring (stufftosave, planets[index]->name, ':', BUFF_SIZE);
	  addint (stufftosave, planets[index]->type, ':', BUFF_SIZE);
	  addint (stufftosave, planets[index]->owner, ':', BUFF_SIZE);
	  addstring (stufftosave, planets[index]->creator, ':', BUFF_SIZE);
	  addint (stufftosave, planets[index]->fuelColonist, ':', BUFF_SIZE);
	  addint (stufftosave, planets[index]->organicsColonist, ':',
		  BUFF_SIZE);
	  addint (stufftosave, planets[index]->equipmentColonist, ':',
		  BUFF_SIZE);
	  addint (stufftosave, planets[index]->fuel, ':', BUFF_SIZE);
	  addint (stufftosave, planets[index]->organics, ':', BUFF_SIZE);
	  addint (stufftosave, planets[index]->equipment, ':', BUFF_SIZE);
	  addint (stufftosave, planets[index]->fighters, ':', BUFF_SIZE);
	  addint (stufftosave, planets[index]->citdl->level, ':', BUFF_SIZE);
	  sprintf (treasury, "%ld", planets[index]->citdl->treasury);
	  addstring (stufftosave, treasury, ':', BUFF_SIZE);
	  addint (stufftosave, planets[index]->citdl->militaryReactionLevel,
		  ':', BUFF_SIZE);
	  addint (stufftosave, planets[index]->citdl->qCannonAtmosphere, ':',
		  BUFF_SIZE);
	  addint (stufftosave, planets[index]->citdl->qCannonSector, ':',
		  BUFF_SIZE);
	  addint (stufftosave, planets[index]->citdl->planetaryShields, ':',
		  BUFF_SIZE);
	  addint (stufftosave, planets[index]->citdl->transporterlvl, ':',
		  BUFF_SIZE);
	  addint (stufftosave, planets[index]->citdl->interdictor, ':',
		  BUFF_SIZE);
	  addint (stufftosave, planets[index]->citdl->upgradestart, ':',
		  BUFF_SIZE);
	  for (loop = 0; loop < 399 - strlen (stufftosave); loop++)
	    strcat (stufftosave, " ");
	  strcat (stufftosave, "\n");
	  fprintf (planetfile, "%s", stufftosave);
	}
    }
  fclose (planetfile);
  // free(stufftosave);
  // free(treasury);
}

int
init_planets (char *filename)
{
  FILE *planetfile;
  int i, p_num, p_sec, p_type, p_owner;
  char dummy[3], buffer[BUFF_SIZE], *temp;
  int count = 0;
  int done = 0;

  temp = (char *) malloc (sizeof (char) * 30);

  planets = (struct planet **)
    malloc (sizeof (struct planet *) * configdata->max_total_planets);
  for (i = 0; i < configdata->max_total_planets; i++)
    planets[i] = NULL;

  planetfile = fopen (filename, "r");
  if (planetfile == NULL)
    {
      fprintf (stderr, "init_planets: No %s file!", filename);
      return (0);
    }
  done = 0;


  /* while (!done) */
  /*   { */
  /*     buffer[0] = '\0'; */
  /*     if (fgets (buffer, BUFF_SIZE, planetfile)) */

  while (fgets (buffer, BUFF_SIZE, planetfile) != NULL)
    {
      p_num = popint (buffer, ":");
      planets[p_num - 1] = (struct planet *) malloc (sizeof (struct planet));
      planets[p_num - 1]->name =
	(char *) malloc ((MAX_NAME_LENGTH + 1) * sizeof (char));
      planets[p_num - 1]->creator =
	(char *) malloc (sizeof (char) * MAX_NAME_LENGTH);
      planets[p_num - 1]->citdl =
	(struct citadel *) malloc (sizeof (struct citadel));

      p_sec = popint (buffer, ":");
      popstring (buffer, planets[p_num - 1]->name, ":", MAX_NAME_LENGTH);
      p_type = popint (buffer, ":");
      p_owner = popint (buffer, ":");	//Owner should be a number!

      planets[p_num - 1]->num = p_num;
      planets[p_num - 1]->pClass = planetTypes[p_type];
      planets[p_num - 1]->sector = p_sec;
      planets[p_num - 1]->owner = p_owner;
      planets[p_num - 1]->type = p_type;

      popstring (buffer, planets[p_num - 1]->creator, ":", MAX_NAME_LENGTH);
      planets[p_num - 1]->fuelColonist = popint (buffer, ":");
      planets[p_num - 1]->organicsColonist = popint (buffer, ":");
      planets[p_num - 1]->equipmentColonist = popint (buffer, ":");
      planets[p_num - 1]->fuel = popint (buffer, ":");
      planets[p_num - 1]->organics = popint (buffer, ":");
      planets[p_num - 1]->equipment = popint (buffer, ":");
      planets[p_num - 1]->fighters = popint (buffer, ":");
      planets[p_num - 1]->citdl->level = popint (buffer, ":");
      popstring (buffer, temp, ":", BUFF_SIZE);
      planets[p_num - 1]->citdl->treasury = strtoul (temp, NULL, 10);
      planets[p_num - 1]->citdl->militaryReactionLevel = popint (buffer, ":");
      planets[p_num - 1]->citdl->qCannonAtmosphere = popint (buffer, ":");
      planets[p_num - 1]->citdl->qCannonSector = popint (buffer, ":");
      planets[p_num - 1]->citdl->planetaryShields = popint (buffer, ":");
      planets[p_num - 1]->citdl->transporterlvl = popint (buffer, ":");
      planets[p_num - 1]->citdl->interdictor = popint (buffer, ":");
      planets[p_num - 1]->citdl->upgradestart = popint (buffer, ":");

      insert_planet (planets[p_num - 1], sectors[p_sec - 1], 0);
    }
  // free(temp);
  return (0);
}


/*
 *	insert_planet(p, s)
 *	returns the sector number it was inserted in, and
 *	-1 if called with a NULL sector
 */
int
insert_planet (struct planet *p, struct sector *s, int playernumber)
{
  struct list *p_list, *newp_list;
  if (s == NULL)
    {
      fprintf (stderr, "insert_planet on NULL sector, yo\n");
      return -1;
    }

  newp_list = (struct list *) malloc (sizeof (struct list *));
  newp_list->item = p;
  newp_list->type = planet;
  newp_list->listptr = NULL;

  p_list = s->planets;
  if (p_list != NULL)
    {
      while (p_list->listptr != NULL)
	{
	  p_list = p_list->listptr;
	}
      p_list->listptr = newp_list;
    }
  else
    s->planets = newp_list;
  fprintf (stdout, "\t-planet number %d (%s) inserted in sector %d\n", p->num,
	   p->name, s->number);
  return s->number;

}

/*
** Create all of the planet types which will be used later.
*/
void
save_planetinfo (char *filename)
{
  FILE *planetinfo;
  char *stufftosave = (char *) malloc (sizeof (char) * BUFF_SIZE);
  int done = 0;
  int index = 0;
  int loop = 0;

  planetinfo = fopen (filename, "w");

  for (index = 0; index < configdata->number_of_planet_types; index++)
    {
      strcpy (stufftosave, "\0");
      addstring (stufftosave, planetTypes[index]->typeClass, ':', BUFF_SIZE);
      addstring (stufftosave, planetTypes[index]->typeName, ':', BUFF_SIZE);
      for (loop = 0; loop < MAX_CITADEL_LEVEL - 1; loop++)
	{
	  addint (stufftosave, planetTypes[index]->citadelUpgradeTime[loop],
		  ',', BUFF_SIZE);
	}
      addint (stufftosave,
	      planetTypes[index]->citadelUpgradeTime[MAX_CITADEL_LEVEL - 1],
	      ':', BUFF_SIZE);
      for (loop = 0; loop < MAX_CITADEL_LEVEL - 1; loop++)
	{
	  addint (stufftosave, planetTypes[index]->citadelUpgradeOre[loop],
		  ',', BUFF_SIZE);
	}
      addint (stufftosave,
	      planetTypes[index]->citadelUpgradeOre[MAX_CITADEL_LEVEL - 1],
	      ':', BUFF_SIZE);
      for (loop = 0; loop < MAX_CITADEL_LEVEL - 1; loop++)
	{
	  addint (stufftosave,
		  planetTypes[index]->citadelUpgradeOrganics[loop], ',',
		  BUFF_SIZE);
	}
      addint (stufftosave,
	      planetTypes[index]->citadelUpgradeOrganics[MAX_CITADEL_LEVEL -
							 1], ':', BUFF_SIZE);
      for (loop = 0; loop < MAX_CITADEL_LEVEL - 1; loop++)
	{
	  addint (stufftosave,
		  planetTypes[index]->citadelUpgradeEquipment[loop], ',',
		  BUFF_SIZE);
	}
      addint (stufftosave,
	      planetTypes[index]->citadelUpgradeEquipment[MAX_CITADEL_LEVEL -
							  1], ':', BUFF_SIZE);
      for (loop = 0; loop < MAX_CITADEL_LEVEL - 1; loop++)
	{
	  addint (stufftosave,
		  planetTypes[index]->citadelUpgradeColonist[loop], ',',
		  BUFF_SIZE);
	}
      addint (stufftosave,
	      planetTypes[index]->citadelUpgradeColonist[MAX_CITADEL_LEVEL -
							 1], ':', BUFF_SIZE);
      for (loop = 0; loop < 3; loop++)
	{
	  addint (stufftosave, planetTypes[index]->maxColonist[loop],
		  ',', BUFF_SIZE);
	}
      addint (stufftosave, planetTypes[index]->citadelUpgradeTime[2], ':',
	      BUFF_SIZE);
      addint (stufftosave, planetTypes[index]->fuelProduction, ':',
	      BUFF_SIZE);
      addint (stufftosave, planetTypes[index]->organicsProduction, ':',
	      BUFF_SIZE);
      addint (stufftosave, planetTypes[index]->equipmentProduction, ':',
	      BUFF_SIZE);
      addint (stufftosave, planetTypes[index]->fighterProduction, ':',
	      BUFF_SIZE);
      addint (stufftosave, planetTypes[index]->maxore, ':', BUFF_SIZE);
      addint (stufftosave, planetTypes[index]->maxorganics, ':', BUFF_SIZE);
      addint (stufftosave, planetTypes[index]->maxequipment, ':', BUFF_SIZE);
      addint (stufftosave, planetTypes[index]->maxfighters, ':', BUFF_SIZE);
      addint (stufftosave, (int) (planetTypes[index]->breeding * 1000), ':',
	      BUFF_SIZE);

      for (loop = 1; loop < 399 - strlen (stufftosave); loop++)
	strcat (stufftosave, " ");
      strcat (stufftosave, "\n");
      fprintf (planetinfo, "%s", stufftosave);
    }
  fclose (planetinfo);
  //  free(stufftosave);
}

void
init_planetinfo (char *filename)
{
  FILE *planetinfo;
  char *buffer = (char *) malloc (sizeof (char) * BUFF_SIZE);
  char *temp = (char *) malloc (sizeof (char) * BUFF_SIZE);
  int done = 0;
  int index = 0;
  int loop = 0;

  planetTypes = (planetClass **)
    malloc (sizeof (planetClass *) * configdata->number_of_planet_types);

  planetinfo = fopen (filename, "r");
  if (planetinfo == NULL)
    {
      fprintf (stderr, "init_planetinfo: No %s file!", filename);
      exit (-1);
    }

  while (fgets (buffer, BUFF_SIZE, planetinfo) != NULL)
    {
      planetTypes[index] = (planetClass *) malloc (sizeof (planetClass));
      planetTypes[index]->typeClass =
	(char *) malloc (sizeof (char) * MAX_NAME_LENGTH * 2);
      planetTypes[index]->typeName =
	(char *) malloc (sizeof (char) * MAX_NAME_LENGTH * 2);

      popstring (buffer, planetTypes[index]->typeClass, ":",
		 MAX_NAME_LENGTH * 2);
      popstring (buffer, planetTypes[index]->typeName, ":",
		 MAX_NAME_LENGTH * 2);
      temp[0] = '\0';
      popstring (buffer, temp, ":", BUFF_SIZE);
      for (loop = 0; loop < MAX_CITADEL_LEVEL; loop++)
	{
	  planetTypes[index]->citadelUpgradeTime[loop] = popint (temp, ",");
	}
      temp[0] = '\0';
      popstring (buffer, temp, ":", BUFF_SIZE);
      for (loop = 0; loop < MAX_CITADEL_LEVEL; loop++)
	{
	  planetTypes[index]->citadelUpgradeOre[loop] = popint (temp, ",");
	}
      temp[0] = '\0';
      popstring (buffer, temp, ":", BUFF_SIZE);
      for (loop = 0; loop < MAX_CITADEL_LEVEL; loop++)
	{
	  planetTypes[index]->citadelUpgradeOrganics[loop] =
	    popint (temp, ",");
	}
      temp[0] = '\0';
      popstring (buffer, temp, ":", BUFF_SIZE);
      for (loop = 0; loop < MAX_CITADEL_LEVEL; loop++)
	{
	  planetTypes[index]->citadelUpgradeEquipment[loop] =
	    popint (temp, ",");
	}
      temp[0] = '\0';
      popstring (buffer, temp, ":", BUFF_SIZE);
      for (loop = 0; loop < MAX_CITADEL_LEVEL; loop++)
	{
	  planetTypes[index]->citadelUpgradeColonist[loop] =
	    popint (temp, ",");
	}
      temp[0] = '\0';
      popstring (buffer, temp, ":", BUFF_SIZE);
      for (loop = 0; loop < 3; loop++)
	{
	  planetTypes[index]->maxColonist[loop] = popint (temp, ",");
	}

      planetTypes[index]->fuelProduction = popint (buffer, ":");
      planetTypes[index]->organicsProduction = popint (buffer, ":");
      planetTypes[index]->equipmentProduction = popint (buffer, ":");
      planetTypes[index]->fighterProduction = popint (buffer, ":");
      planetTypes[index]->maxore = popint (buffer, ":");
      planetTypes[index]->maxorganics = popint (buffer, ":");
      planetTypes[index]->maxequipment = popint (buffer, ":");
      planetTypes[index]->maxfighters = popint (buffer, ":");

      planetTypes[index]->breeding = (float) popint (buffer, ":") / 1000.0;
      index++;
    }
  fclose (planetinfo);
  // free(buffer);
  // free(temp);
}
