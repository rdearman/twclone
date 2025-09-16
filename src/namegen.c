/* Copyright (c) 2002 Rick Dearman <rick@ricken.demon.co.uk>
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
* LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
* OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
* WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <stdlib.h>
#include <stdio.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include "namegen.h"
#include "common.h"

// The single, global definition.
int *usedNames;

extern int randomnum (int min, int max);
int flip = 1;

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
  char nm[50];
  int randIndex;
  flip *= -1;
  if (flip > 0)
    {
      randIndex = randomnum (0, 400);
    }
  else
    {
      randIndex = 0;
    }
  strcpy (nm, nameCollection[randIndex]);
  nameCount++;
  usedNames[nameCount] = randIndex;
  sprintf (name, "%s", nm);
  return name;
}

void
init_usedNames ()
{
  int a;
  for (a = 0; a < 500; a++)
    usedNames[a] = -1;
}
