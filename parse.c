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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "parse.h"

void
addstring (char *list, char *item, char delimiter, int maxlistsize)
{
  char *temp = (char *) malloc (maxlistsize);

  if (strlen (list) == 0)
    {
      sprintf (list, "%c%s%c", delimiter, item, delimiter);
      free (temp);
      return;
    }

  sprintf (temp, "%s%c", item, delimiter);
  temp[maxlistsize - strlen (list) - 1] = '\0';
  strcat (list, temp);
  free (temp);

  return;
}

void
addint (char *list, int num, char delimiter, int maxlistsize)
{
  char *temp = (char *) malloc (maxlistsize);

  if (strlen (list) == 0)
    {
      sprintf (list, "%c%d%c", delimiter, num, delimiter);
      free (temp);
      return;
    }

  sprintf (temp, "%d%c", num, delimiter);
  temp[maxlistsize - strlen (list) - 1] = '\0';
  strcat (list, temp);
  free (temp);

  return;
}

void
popstring (char *list, char *item, char *delimiters, int maxitemsize)
{
  int pos = 0, len = 0, x;

  //fprintf(stderr, "popstring: list='%s', item='%s', delimiters='%s', maxitemsize='%d'\n",
  // list, item, delimiters, maxitemsize);

  if (strcspn (list, delimiters) == 0 && strlen (list) > 0)
    pos++;

  len = strcspn (list + pos, delimiters);

  len = (len < maxitemsize) ? len : maxitemsize - 1;

  strncpy (item, list + pos, len);

  item[len] = '\0';

  //fprintf(stderr, "popstring: item='%s', pos='%d', len='%d'\n", item, pos, len);

  for (x = pos + len + 1; x <= strlen (list); x++)
    list[x - pos - len - 1] = list[x];

  //fprintf(stderr, "popstring: After shifting, list='%s'\n", list);

  return;
}


int
popint (char *list, char *delimiters)
{
  int pos = 0, len = 0, num, x;
  char *endptr[1];

  //  fprintf(stderr, "popint: list = '%s', delimiters = '%s'\n", list, delimiters);

  if (strcspn (list, delimiters) == 0 && strlen (list) > 0)
    pos++;

  len = strcspn (list + pos, delimiters);

  num = strtol (list + pos, endptr, 10);

  //fprintf(stderr, "popint: pos = %d, len = %d, list +... = %d\n", pos, len, (*endptr) - list - pos);

  len = (len > ((*endptr) - list - pos)) ? len : (*endptr) - list - pos;

  if (strlen (list) < pos + len + 1)
    list[0] = list[strlen (list)];
  else
    for (x = pos + len + 1; x <= strlen (list); x++)
      list[x - pos - len - 1] = list[x];

  //fprintf(stderr, "popint: list = '%s', num = %d\n", list, num);

  return num;
}
