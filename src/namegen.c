#include <stdlib.h>
#include <stdio.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include "namegen.h"
#include "common.h"
#include "globals.h"
extern int randomnum (int min, int max);


char *
randomname (char *name)
{
  char nm[50];
  strcpy (nm, firstSyllable[randomnum (0, 99)]);
  strcat (nm, middleSyllable[randomnum (0, 49)]);
  strcat (nm, lastSyllable[randomnum (0, 99)]);
  sprintf (name, "%s", nm);
  return name;
}


char *
consellationName (char *name)
{
  int randIndex;
  if (randomnum (0, 1) == 0)
    {
      // 50% chance to return a constellation name
      randIndex = randomnum (1, 400);
    }
  else
    {
      // 50% chance to return "Uncharted Space"
      randIndex = 0;
    }
  // Copy the name at the random index to the buffer.
  sprintf (name, "%s", nameCollection[randIndex]);
  return name;
}


/* char * */
/* consellationName (char *name) */
/* { */
/*   char nm[50]; */
/*   int randIndex; */
/*   flip *= -1; */
/*   if (flip > 0) */
/*     { */
/*       randIndex = randomnum (0, 400); */
/*     } */
/*   else */
/*     { */
/*       randIndex = randomnum (0, nameCount); */
/*     } */
/*   if (usedNames[randIndex] == -1) */
/*     { */
/*       usedNames[randIndex] = randIndex; */
/*       nameCount++; */
/*     } */
/*   else */
/*     { */
/*       randIndex = randomnum (0, 400); */
/*       while (usedNames[randIndex] != -1) */
/*         { */
/*           randIndex = randomnum (0, 400); */
/*         } */
/*       usedNames[randIndex] = randIndex; */
/*       nameCount++; */
/*     } */
/*   sprintf (name, "%s", nameCollection[randIndex]); */
/*   return name; */
/* } */
/* void */
/* init_usedNames (void) */
/* { */
/*   int a; */
/*   usedNames = (int *) malloc (500 * sizeof (int)); */
/*   if (usedNames == NULL) { */
/*     // Handle error if malloc fails */
/*     exit(1); */
/*   } */
/*   for (a = 0; a < 500; a++) */
/*     usedNames[a] = -1; */
/* } */
