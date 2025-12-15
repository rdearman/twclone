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


