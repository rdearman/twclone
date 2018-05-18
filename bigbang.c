/*
  Copyright (C) 2002 Scott Long (link@kansastubacrew.com)
 
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

/* Modification History **
**************************
** 
** LAST MODIFICATION DATE: 10 June 2002
** Author: Rick Dearman
** 1) Modified all defined items to allow them to be user defined instead. 
**    With one exception which was the MAXJUMPPERCENT which was caused problems
**    with other defined items in the universe.h file. 
**
** 2) Modified all comments from // to C comments in case a users complier isn't C99 
**    complilant. (like some older Sun or HP compilers)
**
** 3) Added random name generation for the ports.
** 
** 4) Added consellation names for sectors.
**
** 5) Added randomly placed Ferringhi sector.
**
** 6) Now creates the planet.data file with terra and ferringhi planets
** included by default.
**
** 7) Creates any number of random planets input by the user.
**
*/

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include "config.h"
#include "universe.h"
// #include "namegen.h"


/*  This is the max length of tunnels and dead ends. */
#define MAXTUNLEN		6
int maxTunnel = MAXTUNLEN;

/*  Variables that will eventually be inputted by the user when this program */
/*  is initally run. */
#define NUMSECTORS		500
int numSectors = NUMSECTORS;

/*  This is the number of ports  */
#define NUMPORTS		140
int numPorts = NUMPORTS;

/*  Percentage of sectors that will have the maximum number of warps in them  */
#define MAXJUMPPERCENT		3
/* Still hard-coded */
int percentJump = MAXJUMPPERCENT;

/*  Percentage chance that a final jump will be a one-way */
#define ONEWAYJUMPPERCENT	3
int percentDeadend = ONEWAYJUMPPERCENT;

/*  Percentage chance that a tunnel will be a dead end */
#define DEADENDPERCENT		30
int percentOneway = DEADENDPERCENT;

/*  Number of Nodes(how many seperate sector groups) in universe */
#define NUMNODES 1
int numNodes = NUMNODES;

/* Length of strings for names, memory is an issue */
int strNameLength = 25;

int numRandomPlanets = 0;
/* This is because of random memory corruption in the configdata */

int maxWarps = 0;

/*  THESE ARE THE SET-IN-STONE FEDSPACE LINKS */
/*  DON'T EVEN THINK ABOUT TOUCHING THESE... */
const int fedspace[10][6] =
  {
    {
      2, 3, 4, 5, 6, 7
    },
    {1, 3, 7, 8, 9, 10},
    {1, 2, 4, 0, 0, 0},
    {1, 3, 5, 0, 0, 0},
    {1, 4, 6, 0, 0, 0},
    {1, 5, 7, 0, 0, 0},
    {1, 2, 6, 8, 0, 0},
    {2, 7, 0, 0, 0, 0},
    {2, 10, 0, 0, 0, 0},
    {2, 9, 0, 0, 0, 0}
  };

struct sector **sectorlist;
struct sector **bigsectorlist;
struct sector **sectors;
struct config *configdata;
/*  struct port *portlist[NUMPORTS]; */
struct port **portlist;
/*  int randsectornum[NUMSECTORS-10]; */
int *randsectornum;
// int *usedNames;
int compsec (const void *cmp1, const void *cmp2);
int randjump (int maxjumplen);
int randomnum (int min, int max);
void secjump (int from, int to);
int freewarp (int sector);
int warpsfull (int sector);
int numwarps (int sector);
int innode(int sector);
void makeports ();
void sectorsort (struct sector *base[maxWarps], int elements);
extern char *randomname (char *name);
extern char *consellationName (char *name);
extern void init_usedNames ();
extern int insert_planet (struct planet *p, struct sector *s);



int
main (int argc, char **argv)
{
  int c;
  int loop;
  char *tmpname;
  int x, y, z, tempint, randint, tosector, fromsector, startsec, secptrcpy,
    jumpsize;
  int maxjumpsize;
  int usedsecptr;
  int len;
  char *fileline, *tempstr;
  FILE *file;
  FILE *planetfile;
  struct sector *secptr;
  char *terraInfo = malloc(sizeof(char)*400);
  "1:1:terra:1:-1:Unknown:3000:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:"
    ;
  char *ferrinfo = malloc(sizeof(char)*400);
  char *plout = malloc(sizeof(char)*400);
  char *randomPlanetInfo;
  int ferringhiSector;
  time_t datenow;
  int totalsectors = 0;
  int totalsectorsdone = 0;
  int counter;


  char *usageinfo =
    "Usage: bigbang [options]\n    Options:-t < integer >\n    indicate the max length of tunnels and dead ends.(default /minimum 6)\n    -s < integer >\n    indicate the max number of sectors.(default /minimum 500)\n    -p < integer >\n    indicate the max number of ports which MUST be at least 10 LESS than the\n    number of sectors.(default /minimum 190)\n    -o < integer >\n    indicate the percentage chance that a final jump will be a one -\n    way.(default /minimum 3)\n    -d < integer >\n    indicate the percentage chance that a tunnel will be a dead end.\n    (default /minimum 30)\n    -g < integer > generate a number of random planets.(default 0) \n     -n < integer > indicate number of Nodes in universe(default 1).\n       Nodes are partially disconnected subuniverses that are only connected \n       from certain ports.\n";
  /* This has to be taken out because of the knockon affect it was having with the rest of the program.
     -j <integer>  indicate the percentage of sectors that will have the maximum number of warps in them. (must be between 3 and 7) 
  */
  opterr = 0;

  strcpy(terraInfo,"1:1:terra:1:-1:Unknown:3000:0:0:0:0:0:0:0:0:0:0:0:0:0:0:");

  /* Setup the defaults before finding the arguments. */
  maxTunnel = MAXTUNLEN; 
  numSectors = NUMSECTORS;
  numPorts = NUMPORTS; 
  percentJump = MAXJUMPPERCENT;
  percentDeadend = DEADENDPERCENT; 
  percentOneway = ONEWAYJUMPPERCENT ;
  numRandomPlanets = numRandomPlanets ; 
  numNodes =  NUMNODES; 
    
  /*    while ((c = getopt (argc, argv, "t:s:p:j:d:o:")) != -1) */
  while ((c = getopt (argc, argv, "t:s:p:d:o:g:n:")) != -1)
    {
      switch (c)
        {
        case 't':
	  maxTunnel = (MAXTUNLEN > atoi (optarg)) ? MAXTUNLEN : atoi (optarg);
	  break;
        case 's':
	  numSectors =
	    (NUMSECTORS > atoi (optarg)) ? NUMSECTORS : atoi (optarg);
	  break;
        case 'p':
	  numPorts = (NUMPORTS > atoi (optarg)) ? NUMPORTS : atoi (optarg);
	  break;
	  /*        case 'j': */
	  /*          percentJump = (MAXJUMPPERCENT > atoi(optarg)) ? MAXJUMPPERCENT : atoi(optarg); */
	  /*          percentJump = (7 > percentJump) ? 7 : percentJump; */
	  /*          break; */
        case 'd':
	  percentDeadend =
	    (DEADENDPERCENT > atoi (optarg)) ? DEADENDPERCENT : atoi (optarg);
	  break;
        case 'o':
	  percentOneway =
	    (ONEWAYJUMPPERCENT >
	     atoi (optarg)) ? ONEWAYJUMPPERCENT : atoi (optarg);
	  break;
        case 'g':
	  numRandomPlanets =
	    (numRandomPlanets >
	     atoi (optarg)) ? numRandomPlanets : atoi (optarg);
        case 'n':
	  numNodes =
	    (NUMNODES >
	     atoi (optarg)) ? NUMNODES : atoi( optarg);
	  break;
        case '?':
	  if (isprint (optopt))
	    fprintf (stderr, "Unknown option `-%c'.\n\n%s", optopt,
		     usageinfo);
	  else
	    fprintf (stderr,
		     "Unknown option character `\\x%x'.\n\n%s",
		     optopt, usageinfo);
	  return 1;
	  //default:

        }
    }


  if (numPorts > (numSectors - 10))
    {
      fprintf (stderr,
	       "The max number of sectors MUST be at least 10 sectors greater than the number of ports. Program aborted.");
      exit (0);
    }

  usedNames = (int *) malloc( numSectors * sizeof(int) );
  tmpname = malloc (sizeof (strNameLength));

  /*  Seed our randomizer */
  srand ((unsigned int) time (NULL));

  init_usedNames ();

  /*  Reading config.data file for config data (Duh...) */
  printf ("\nReading in config.data...");
  (void) init_config ("config.data");
  maxWarps = configdata->maxwarps;
  printf ("done.\n");

  printf ("Creating sector array...");

  sectorlist = (struct sector **)
    malloc (numSectors * sizeof (struct sector *));
  bigsectorlist = (struct sector **)
    malloc(numSectors * sizeof(struct sector *));
  for (x = 0; x < numSectors; x++)
    {
      sectorlist[x] = NULL;
      bigsectorlist[x] = NULL;
    }
	 
  sectors = sectorlist;

  printf ("Creating port array...");
  portlist = malloc (numPorts * sizeof (struct port *));
  for (x = 0; x < numPorts; x++)
    {
      portlist[x] = NULL;
    }
  printf ("done.\n\n");

  totalsectors = numSectors;
  for (counter = 1; counter <= numNodes; counter++)
    {
      numSectors = totalsectors/numNodes;
      if ((counter == numNodes) && ((totalsectorsdone + numSectors) < totalsectors))
	{
	  numSectors = totalsectors - totalsectorsdone;
	}
      if (numNodes != 1)
	fprintf(stderr, "Now creating a node with %d sectors in it!\n", numSectors);
      if (counter != 1)
	{
	  numSectors = numSectors + 10;
	}
		  
      maxjumpsize = (int) (numSectors * ((double) percentJump / 100));
      randsectornum = (int *) malloc ((numSectors - 11) * sizeof (int));

      for (x = 0; x < numSectors; x++)
	{
	  sectorlist[x] = malloc (sizeof (struct sector));
	  for (y=0; y <= maxWarps; y++)
	    {
	      sectorlist[x]->sectorptr[y] = NULL;
	    }
	}	
      /*  Fills in the randsectornum array with numbers 10 to (numsectors - 1) */
      for (x = 0; x < numSectors - 10; x++)
	randsectornum[x] = x + 10;

        
      usedsecptr = numSectors - 11;
      printf ("Randomly picking sector numbers...");
      /*  Randomly creates sector numbers to use: */
      for (x = numSectors - 11; x > 0; x--)
        {
	  randint = randomnum (0, x);
	  tempint = randsectornum[randint];
	  randsectornum[randint] = randsectornum[x];
	  randsectornum[x] = tempint;
        }
      printf ("done.\n");

      /*  Initalize sectorlist with data */
      for (x = 0; x < numSectors; x++)
	sectorlist[x]->number = x + 1;

      printf ("Creating Fedspace...");
      /*  Sets up Fed Space */
      for (x = 0; x < 10; x++)
        {
	  for (y = 0; y < 6; y++)
	    if (fedspace[x][y] != 0)
	      sectorlist[x]->sectorptr[y] = sectorlist[(fedspace[x][y]) - 1];
	  sectorlist[x]->beacontext = "The Federation -- Do Not Dump!";
	  sectorlist[x]->nebulae = "The Federation";
        }
      printf ("done.\n");

      printf ("Setting up links from FedSpace out to other sectors...");
      /*  Sets up 13 jumps from Fed Space to 6jump sectors  */
      for (x = 0; x <= 13; x++)
        {
	  randint = randomnum (1, 5);
	  do
            {
	      fromsector = randomnum (2, 9);
            }
	  while (warpsfull (fromsector));
	  jumpsize = randjump (3);

	  for (y = 0; y < jumpsize; y++)
            {
	      tempint = randomnum (maxjumpsize, usedsecptr);
	      tosector = randsectornum[tempint];
	      randsectornum[tempint] = randsectornum[usedsecptr];
	      randsectornum[usedsecptr] = tosector;
	      usedsecptr--;
	      secjump (fromsector, tosector);
	      secjump (tosector, fromsector);
	      fromsector = tosector;
            }
	  if (randint < 4)
            {
	      secjump (fromsector, x);
	      secjump (x, fromsector);
            }
        }
      printf ("done.\n");

      printf ("Setting up the max warp sectors...");
      fflush(stdout);
      /*  Sets up rest of links for the maxwarp sectors (the meat and potatoes) */
      for (x = 0; x < maxjumpsize; x++)
        {
	  for (y = freewarp (x); y < maxWarps; y++)
            {
	      randint = randomnum (1, 100);
	      jumpsize = randjump (maxTunnel);
	      startsec = randsectornum[x];
	      fromsector = startsec;
	      for (z = 0; z < jumpsize; z++)
                {
		  tempint = randomnum (maxjumpsize, usedsecptr);
		  tosector = randsectornum[tempint];
		  randsectornum[tempint] = randsectornum[usedsecptr];
		  randsectornum[usedsecptr] = tosector;
		  usedsecptr--;

		  secjump (fromsector, tosector);
		  secjump (tosector, fromsector);
		  fromsector = tosector;
                }
	      if (randint <= (100 - percentDeadend))
                {
		  do
                    {
		      tosector = randsectornum[randomnum (0, maxjumpsize - 1)];
                    }
		  while (tosector == startsec);
		  secjump (fromsector, tosector);
		  if (randomnum (1, 100) <= (100 - percentOneway)
		      && !warpsfull (tosector))
		    secjump (tosector, fromsector);
                }
            }
        }
      printf ("done.\n");
      fflush(stdout);

      for (x = 0; x < maxjumpsize; x++)
        {
	  tempint = randsectornum[x];
	  randsectornum[x] = randsectornum[usedsecptr];
	  randsectornum[usedsecptr] = tempint;
	  usedsecptr--;
        }

      printf ("Using up leftover sector numbers...");
      fflush(stdout);
      while (usedsecptr >= 0)	/*  finishes up creating other sector links... */
        {
	  randint = randomnum (1, 100);
	  tempint = maxTunnel;
	  if (usedsecptr + 1 < maxTunnel)
	    tempint = usedsecptr + 1;
	  jumpsize = randjump (tempint);
	  startsec = randsectornum[randomnum (usedsecptr + 1, numSectors - 11)];
	  if (freewarp (startsec))
            {
	      fromsector = startsec;
	      secptrcpy = usedsecptr;
	      for (z = 0; z < jumpsize; z++)
                {
		  tempint = randomnum (0, usedsecptr);
		  tosector = randsectornum[tempint];

		  randsectornum[tempint] = randsectornum[usedsecptr];
		  randsectornum[usedsecptr] = tosector;
		  usedsecptr--;

		  secjump (fromsector, tosector);
		  secjump (tosector, fromsector);
		  fromsector = tosector;
                }
	      if (randint <= (100 - percentDeadend))
                {
		  do
                    {
		      tosector = randsectornum[randomnum (0, secptrcpy)];
                    }
		  while (tosector == startsec || warpsfull (tosector));
		  secjump (fromsector, tosector);
		  if (randomnum (1, 100) <= (100 - percentOneway))
		    secjump (tosector, fromsector);
                }
            }
        }
      printf ("done.\n");
      fflush(stdout);
      if (counter == 1)
	{
	  for (x=0; x < numSectors ; x++)
	    {
	      bigsectorlist[x + totalsectorsdone] = sectorlist[x];
	      //fprintf(stderr, "Linking sector %d to sector %d\n", x+totalsectorsdone+1, x+1);
	      bigsectorlist[x + totalsectorsdone]->number = 
		bigsectorlist[x + totalsectorsdone]->number + totalsectorsdone;
	    }
	}
      else
	{
	  for (x=0; x < (numSectors - 10); x++)
	    {
	      if (x + totalsectorsdone >= totalsectors)
		{
		  x = numSectors+12;
		}
	      else
		{
		  if (x < 10) //Make sure links to fedspace are dealt with correctly
		    sectorlist[x]->number = sectorlist[x]->number + totalsectorsdone;
		  //fprintf(stderr,"Linking sector %d to sector %d numSectors is %d x is %d\n", x+11, x+totalsectorsdone+1, numSectors, x);
		  sectorlist[x+10]->number = sectorlist[x+10]->number - 10;
		  bigsectorlist[x + totalsectorsdone] = sectorlist[x+10];
		  bigsectorlist[x + totalsectorsdone]->number = 
		    bigsectorlist[x + totalsectorsdone]->number + totalsectorsdone;
		}
	    }
	}
      if (counter == 1)
	totalsectorsdone = totalsectorsdone + numSectors;
      else
	totalsectorsdone = totalsectorsdone + numSectors - 10;
      if (numNodes != 1)
	{
	  printf("Now finishing Node %d. With %d sectors complete.\n\n", counter,
		 totalsectorsdone);
	}
      fflush(stdout);
		  
    }
  sectorlist = bigsectorlist;
  numSectors = totalsectors;
  for (counter=0; counter < numSectors; counter++)
    {
      if (sectorlist[counter] == NULL)
	{
	  //fprintf(stderr, "Sector %d is null!\n", counter+1);
	  sectorlist[counter] = (struct sector *)malloc(sizeof(struct sector));
	  sectorlist[counter]->portptr = NULL;
	  for (x=0; x< maxWarps; x++)
	    sectorlist[counter]->sectorptr[x] = NULL;
	  sectorlist[counter]->sectorptr[0] = sectorlist[0];
	}
      else 
	{
	  if (sectorlist[counter]->portptr != NULL)
	    sectorlist[counter]->portptr = NULL;
	  if (sectorlist[counter]->sectorptr[0] == NULL)
	    {
	      //fprintf(stderr, "\nSector %d has no warps!\n", counter+1);
	      fflush(stderr);
	      sectorlist[counter]->sectorptr[0] = sectorlist[0];
	    }
	}
    }

  printf ("Creating %d ports...", numPorts);
  fflush(stdout);
  makeports ();
  printf ("done.\n");
  fflush(stdout);


  printf ("Creating Ferringhi home sector...");
  ferringhiSector = randomnum (21, (numSectors - 1));
  sectorlist[ferringhiSector]->beacontext = "Ferringhi";
  sectorlist[ferringhiSector]->nebulae = "Ferringhi";
  printf ("done.\n");

  printf ("Creating planets...");
  planetfile = fopen ("./planets.data", "w");
  for (loop = 1; loop <= 299 - strlen(terraInfo); loop++)
    strcat(terraInfo, " ");
  strcat(terraInfo, "\n");
  fprintf (planetfile, "%s", terraInfo);
  sprintf (ferrinfo, "%d:%d:Ferringhi:1:-2:Unknown:1000:1000:1000:0:0:0:0:3:100000:20:30:10:0:0:0:0:", 2, ferringhiSector);
  for (loop = 1; loop <= 299 - strlen(ferrinfo); loop++)
    strcat(ferrinfo, " ");
  strcat(ferrinfo, "\n");
  fprintf(planetfile,"%s", ferrinfo);
  randomPlanetInfo = malloc (sizeof (strNameLength));
  if (numRandomPlanets > 0)
    {
      c = 3;
      for (x = 0; x < numRandomPlanets; x++)
        {
	  tempint = randomnum (21, (numSectors - 1));
	  while (tempint == ferringhiSector || tempint < 10)
            {
	      tempint = randomnum (21, (numSectors - 1));
            }
	  sprintf (ferrinfo, "%d:%d:%s:%d:0:Unknown:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:"
		   , c, tempint, randomname (randomPlanetInfo), randomnum(1,5));
	  for (loop = 1; loop <= 299 - strlen(ferrinfo); loop++)
	    strcat(ferrinfo, " ");
	  strcat(ferrinfo, "\n");
	  fprintf(planetfile,"%s", ferrinfo);

	  c++;
        }
    }

  printf ("done.\n");

  printf ("Saving planets to file...");
  (void) fclose (planetfile);
  printf ("done.\n");


  if (numPorts > configdata->max_ports)
    {
      if (numPorts <= numSectors)
	configdata->max_ports = (numSectors + numPorts)/2;
    }
  printf("Saving config data to file...");
  datenow = time(NULL);
  configdata->bangdate = (unsigned long)datenow;
  configdata->numnodes = numNodes;
  saveconfig("config.data");


  /*  Sorts each sector's warps into numeric order */
  for (x = 0; x < numSectors; x++)
    {
      sectorsort (sectorlist[x]->sectorptr, numwarps (x));
    }

  /*  Writing data to universe.data file */
  printf ("Saving universe to file...");
  file = fopen ("./universe.data", "w");

  fileline = malloc (1024 * sizeof (char));
  tempstr = malloc (10 * sizeof (char));

  for (x = 0; x < numSectors; x++)
    {
      sprintf (fileline, "%d", (x + 1));
      fileline = strcat (fileline, ":");
      for (y = 0; y < numwarps(x); y++)
        {
	  secptr = sectorlist[x]->sectorptr[y];
	  sprintf (tempstr, "%d", secptr->number);
	  fileline = strcat (fileline, tempstr);
	  if ((y+1) < numwarps(x))
	    fileline = strcat (fileline, ",");
        }
      fileline = strcat (fileline, ":");
      /* Adds in names for sectors */
      if (sectorlist[x]->nebulae == NULL)
        {
	  sectorlist[x]->nebulae = malloc (sizeof (strNameLength));
	  tmpname = consellationName (tmpname);
	  sectorlist[x]->nebulae = tmpname;
        }
      if (sectorlist[x]->beacontext != NULL)
	fileline = strcat (fileline, sectorlist[x]->beacontext);
      fileline = strcat (fileline, ":");
      if (sectorlist[x]->nebulae != NULL)
	fileline = strcat (fileline, sectorlist[x]->nebulae);
      fileline = strcat (fileline, ":\n");
      /*  Later put in whitespace buffer for saving */
      /*  Not needed until user created beacons put in */
      fprintf (file, "%s", fileline);
    }
  fclose (file);
  free (fileline);
  free (tempstr);
  printf ("done.\n");

  /*  Writing data to ports.data file */
  printf ("Saving ports to file...");
  file = fopen ("./ports.data", "w");
  fileline = malloc (1024 * sizeof (char));

  tempstr = malloc (10 * sizeof (char));

  for (x = 0; x < numPorts; x++)
    {
      sprintf (fileline, "%d:%s:%d:%d:%d:%d:%d:%d:%d:%ld:%d:%d", (x + 1),
	       portlist[x]->name, portlist[x]->location,
	       portlist[x]->maxproduct[0], portlist[x]->maxproduct[1],
	       portlist[x]->maxproduct[2], portlist[x]->product[0],
	       portlist[x]->product[1], portlist[x]->product[2],
	       (long int) portlist[x]->credits, portlist[x]->type,
	       (int) portlist[x]->invisible);
      fileline = strcat (fileline, ":");
      len = (int) strlen (fileline);
      for (y = 0; y <= 99 - len; y++)
	strcat (fileline, " ");
      strcat (fileline, "\n");
      fprintf (file, "%s",fileline);
    }
  fclose (file);

  printf ("done.\nUniverse sucessfully created!\n\n");

  return 0;
}

int
compsec (const void *cmp1, const void *cmp2)
{
  const struct sector *a = *(struct sector **) cmp1;
  const struct sector *b = *(struct sector **) cmp2;

  if (a->number > b->number)
    return 1;
  if (a->number < b->number)
    return -1;
  return 0;
}

int randjump (int maxjumplen)
{
  if (maxjumplen > 2)
    {
      int temprandnum = randomnum (0, 2 + 2 + 1 + maxjumplen);
      /*  int temprandnum = 1 + ((int)((double)rand() / ((double) RAND_MAX + 1) * (2+2+1+maxjumplen))); */

      if (temprandnum == 0)
	return 0;
      if (temprandnum >= 1 && temprandnum <= 3)
	return 1;
      if (temprandnum >= 4 && temprandnum <= 6)
	return 2;
      if (temprandnum == 7 || temprandnum == 8)
	return 3;
      if (temprandnum == 9)
	return 4;
      if (temprandnum == 10)
	return 5;
      if (temprandnum == 11)
	return 6;
      if (temprandnum == 12)
	return 7;
      if (temprandnum == 13)
	return 8;
      if (temprandnum == 14)
	return 9;
      if (temprandnum == 15)
	return 10;
      return 1;
    }
  return maxjumplen;
}

int
randomnum (int min, int max)
{
  return (min +
	  ((int)
	   ((double) rand () / ((double) RAND_MAX + 1) * (1 + max - min))));
}

void
secjump (int from, int to)
{
  int y = freewarp (from);
  if (y != -1)
    sectorlist[from]->sectorptr[y] = sectorlist[to];
}

int freewarp (int sector)
{
  int x;
  for (x = 0; x < maxWarps; x++)
    {
      if (sectorlist[sector]->sectorptr[x] == NULL)
	return x;
    }
  return -1;
}

int
warpsfull (int sector)
{
  if (freewarp (sector) == -1)
    return 1;
  return 0;
}

int innode(int sector)
{
  int counter;
  int nodemin;
  int nodemax;

  if (numNodes == 1)
    {
      return 1;
    }
  for (counter=1; counter <= numNodes; counter++)
    {
      nodemin = (int)(counter - 1.0)*(float)(numSectors)/(float)numNodes + 1.0;
      nodemax = (int)counter*(float)numSectors/(float)numNodes;
      if (sector >= nodemin && sector <= nodemax)
        {
	  return(counter);
        }
    }
}

int numwarps (int sector)
{
  int x = freewarp (sector);
  if (x == -1)
    return maxWarps;
  return x;
}

void sectorsort (struct sector *base[maxWarps], int elements)
{
  struct sector *holdersector;
  int x = 0;
  int done = 0, alldone = 1;	/* This allows for exiting the sort */
  /*This could be done better, but for now it works */
  if (elements == 1)
    return;
  if (elements == 2)
    {
      if (base[0]->number > base[1]->number)
        {
	  holdersector = base[0];
	  base[0] = base[1];
	  base[1] = holdersector;
        }
      return;

    }
  while (1 != 0)		/* instead of while (1) : gets rid of splint warning */
    {
      alldone = 1;
      for (x = 0; x < (elements / 2 - 1 + elements % 2); x++)
        {
	  if (base[2 * x]->number > base[2 * x + 1]->number)
            {
	      holdersector = base[2 * x];
	      base[2 * x] = base[2 * x + 1];
	      base[2 * x + 1] = holdersector;
            }
        }
      for (x = 1; x <= (elements / 2 - 1 + elements % 2); x++)
        {
	  if (base[2 * x - 1]->number > base[2 * x]->number)
            {
	      alldone = 0;
	      done = 0;
	      holdersector = base[2 * x - 1];
	      base[2 * x - 1] = base[2 * x];
	      base[2 * x] = holdersector;
            }
	  else if (alldone != 0)
	    done = 1;
        }
      if (done != 0)
	break;
    }
}

void
makeports ()
{
  struct port *curport;
  struct port *ckPort;
  int type = 0;
  int loop = 0;
  int sector = 0;
  char name[25];
  char *tmpname;
  char *expandedtmpname;
  int curnode;
  //  char expandedPN[] =  "[A Vadie Guild Port]";

  tmpname = malloc (sizeof (strNameLength));

  for (loop = 0; loop < numPorts; loop++)
    {
      tmpname = randomname (tmpname);
      // Sanity Check Port Names and make sure there are no duplicates.
      for (int sanity_ck =0; sanity_ck < loop; sanity_ck++)
	{
	  ckPort = portlist[sanity_ck];
	  if (strcmp(tmpname,ckPort->name ) == 0)
	    {
	      expandedtmpname = malloc (sizeof (tmpname) + 50);
	      strcpy(expandedtmpname,  "\0");
	      sprintf(expandedtmpname, "%s%s", tmpname,  "I");
	      tmpname = realloc (tmpname, strlen(expandedtmpname) + 1);
	      sprintf(tmpname, "%s", expandedtmpname);
	      //fprintf(stdout, "PortName:%s\n",expandedtmpname);
	      //break;
	    }
	}

      curport = (struct port *) malloc (sizeof (struct port));
      curport->number = loop + 1;
      curport->name = (char *) malloc (sizeof (char) * (strlen(tmpname) + 1));
      strcpy (name, "\0");

      if (loop == 0)
        {
	  strcpy (curport->name, "Sol");
	  type = 0;
        }
      else if (loop == 1)
        {
	  strcpy (curport->name, "Alpha Centauri");
	  type = 0;
        }
      else if (loop == 2)
        {
	  strcpy (curport->name, "Rylos");
	  type = 0;
        }
      else if (loop == 3)
        {
	  strcpy (curport->name, "Stargate Alpha I");
	  type = 9;
	  curport->maxproduct[0] = randomnum (2800, 3000);
	  curport->maxproduct[1] = randomnum (2800, 3000);
	  curport->maxproduct[2] = randomnum (2800, 3000);
        }
      else if ((loop <= numNodes+3) && numNodes > 1)
	{
	  if (loop == 4)
	    {
	      tmpname = realloc (tmpname, strlen("Terra Node") + 1);
	      sprintf(tmpname, "Terra Node");
	    }
	    else
	    {
	      tmpname = realloc (tmpname, strlen(tmpname) + 7);
	      strcat(tmpname, " Node");
	    }
	  type = 10;
	  strcpy(curport->name, tmpname);
	  curport->maxproduct[0] = randomnum (2800, 3000);
	  curport->maxproduct[1] = randomnum (2800, 3000);
	  curport->maxproduct[2] = randomnum (2800, 3000);
	}
      else 
        {
	  strcpy (curport->name, tmpname);
	  curport->maxproduct[0] = randomnum (2800, 3000);
	  curport->maxproduct[1] = randomnum (2800, 3000);
	  curport->maxproduct[2] = randomnum (2800, 3000);
	  type = randomnum (1, 8);
        }
      curport->type = type;
      curport->product[0] = 0;
      curport->product[1] = 0;
      curport->product[2] = 0;
      curport->credits = 50000;
      curport->invisible = 0;	/*  Only *special* ports are invisible; */


      switch (type)
        {
        case 1:
	  curport->product[2] = curport->maxproduct[2];
	  break;
        case 2:
	  curport->product[1] = curport->maxproduct[1];
	  break;
        case 3:
	  curport->product[1] = curport->maxproduct[1];
	  curport->product[2] = curport->maxproduct[2];
	  break;
        case 4:
	  curport->product[0] = curport->maxproduct[0];
	  break;
        case 5:
	  curport->product[0] = curport->maxproduct[0];
	  curport->product[2] = curport->maxproduct[2];
	  break;
        case 6:
	  curport->product[0] = curport->maxproduct[0];
	  curport->product[1] = curport->maxproduct[1];
	  break;
        case 7:
	  curport->product[0] = curport->maxproduct[0];
	  curport->product[1] = curport->maxproduct[1];
	  curport->product[2] = curport->maxproduct[2];
	  break;
        default:
	  break;
        }
      portlist[loop] = curport;
      curport = NULL;

      /*  Now for assigning the port to a sector */
      if (loop != 0)
        {
	  if (numNodes == 1)
	    {
	      sector = randomnum (0, numSectors - 1);
	      while (sectorlist[sector]->portptr != NULL)
		sector = randomnum (0, numSectors - 1);
	      portlist[loop]->location = sector + 1;
	    }
	  else if ((loop > 3) && (loop <= numNodes+3))
	    {
	      //fprintf(stderr, "Inserting type 10 port into node %d\n", loop-3);
	      fflush(stdout);
	      sector = randomnum(0, numSectors - 1);
	      curnode = innode(sector+1);
	      while ((curnode != (loop-3)))
		{
		  do
		    {
		      sector = randomnum(0, numSectors - 1);
		      curnode = innode(sector+1);
		    }while(sectorlist[sector]->portptr!=NULL);
		}
	      portlist[loop]->location = sector + 1;
	    }
	  else
	    {
	      //fprintf(stderr, "looking to place port %d in a sector\n", loop+1);
	      sector = randomnum (0, numSectors - 1);
	      while (sectorlist[sector]->portptr != NULL)
		sector = randomnum (0, NUMSECTORS - 1);
	      portlist[loop]->location = sector + 1;
	    }
        }
      else
	portlist[loop]->location = 1;
      sectorlist[sector]->portptr = portlist[loop];
    }
}
