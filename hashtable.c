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
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include "universe.h"
#include "hashtable.h"

/*********************************************\
 * init_hash_table                            |
 *                                            |
 * description: sets the pointer at every     |
 * location in the hash_table to NULL         |
\*********************************************/

void init_hash_table (struct list *hash_table[], int hash_length)
{
  int x;
  for (x = 0; x < hash_length; x++)
    hash_table[x] = NULL;

  return;
}


/*********************************************\
 * find                                       |
 *                                            | 
 * desription: returns pointer to the         | 
 * location of the attributes of the input    |
 * symbol in the hash_table                   |
\*********************************************/

void *
find (const char *symbol, enum listtype type, struct list *hash_table[],
      int hash_length)
{
  struct list *temp_element = hash_table[hash (symbol, hash_length)];
  while (temp_element != NULL)
    {
      if (temp_element->type == type)
	{
	  switch (type)
	    {
	    case player:
	      if (strcmp
		  (((struct player *) (temp_element->item))->name,
		   symbol) == 0)
		return temp_element->item;
	      break;
	    case planet:
	      if (strcmp
		  (((struct planet *) (temp_element->item))->name,
		   symbol) == 0)
		return temp_element->item;
	      break;
	    case port:
	      if (strcmp
		  (((struct port *) (temp_element->item))->name, symbol) == 0)
		return temp_element->item;
	      break;
	    case ship:
	      if (strcmp
		  (((struct ship *) (temp_element->item))->name, symbol) == 0)
		return temp_element->item;
	      break;
	    }
	}
      else
	temp_element = temp_element->listptr;
    }
  return NULL;
}

/*********************************************\
 * insert                                     |
 *                                            |
 * description: adds an entry to the          |
 * hash_table for the input symbol and        |
 * return a pointer to the location of the    |
 * attributes of said symbol, if symbol is    |
 * already there returns a NULL               |
\*********************************************/

void *
insert (const char *symbol, enum listtype type, struct list *hash_table[],
	int hash_length)
{
  void *item = NULL;

  switch (type)
    {
    case player:
      item = malloc (sizeof (struct player));
      ((struct player *) (item))->name =
	(char *) malloc (strlen (symbol) + 1);
      strcpy (((struct player *) (item))->name, symbol);
      break;
    case planet:
      item = malloc (sizeof (struct planet));
      ((struct planet *) (item))->name =
	(char *) malloc (strlen (symbol) + 1);
      strcpy (((struct planet *) (item))->name, symbol);
      break;
    case port:
      item = malloc (sizeof (struct port));
      ((struct port *) (item))->name = (char *) malloc (strlen (symbol) + 1);
      strcpy (((struct port *) (item))->name, symbol);
      break;
    case ship:
      item = malloc (sizeof (struct ship));
      ((struct ship *) (item))->name = (char *) malloc (strlen (symbol) + 1);
      strcpy (((struct ship *) (item))->name, symbol);
      break;
    }

  return insertitem (item, type, hash_table, hash_length);
}


//This does not free the item, it just removes it from the list.
void *
delete (const char *symbol, enum listtype type, struct list *hash_table[],
	int hash_length)
{
  struct list *temp_element =
    hash_table[hash (symbol, hash_length)], *tobedeleted;
  void *temp;

  if (temp_element == NULL)
    return NULL;

  if (temp_element->type == type)
    {
      switch (type)
	{
	case player:
	  if (strcmp (((struct player *) (temp_element->item))->name, symbol)
	      == 0)
	    {
	      temp = temp_element->item;
	      tobedeleted = temp_element;
	      hash_table[hash (symbol, hash_length)] = temp_element->listptr;
	      free (tobedeleted);
	      return temp;
	    }
	  break;
	case planet:
	  if (strcmp (((struct planet *) (temp_element->item))->name, symbol)
	      == 0)
	    {
	      temp = temp_element->item;
	      tobedeleted = temp_element;
	      hash_table[hash (symbol, hash_length)] = temp_element->listptr;
	      free (tobedeleted);
	      return temp;
	    }
	  break;
	case port:
	  if (strcmp (((struct port *) (temp_element->item))->name, symbol) ==
	      0)
	    {
	      temp = temp_element->item;
	      tobedeleted = temp_element;
	      hash_table[hash (symbol, hash_length)] = temp_element->listptr;
	      free (tobedeleted);
	      return temp;
	    }
	  break;
	case ship:
	  if (strcmp (((struct ship *) (temp_element->item))->name, symbol) ==
	      0)
	    {
	      temp = temp_element->item;
	      tobedeleted = temp_element;
	      hash_table[hash (symbol, hash_length)] = temp_element->listptr;
	      free (tobedeleted);
	      return temp;
	    }
	  break;
	}
    }

  while (temp_element->listptr != NULL)
    {
      if (temp_element->listptr->type == type)
	{
	  switch (type)
	    {
	    case player:
	      if (strcmp
		  (((struct player *) (temp_element->listptr->item))->name,
		   symbol) == 0)
		{
		  temp = temp_element->listptr->item;
		  tobedeleted = temp_element->listptr;
		  temp_element->listptr = temp_element->listptr->listptr;
		  free (tobedeleted);
		  return temp;
		}
	      break;
	    case planet:
	      if (strcmp
		  (((struct planet *) (temp_element->listptr->item))->name,
		   symbol) == 0)
		{
		  temp = temp_element->listptr->item;
		  tobedeleted = temp_element->listptr;
		  temp_element->listptr = temp_element->listptr->listptr;
		  free (tobedeleted);
		  return temp;
		}
	      break;
	    case port:
	      if (strcmp
		  (((struct port *) (temp_element->listptr->item))->name,
		   symbol) == 0)
		{
		  temp = temp_element->listptr->item;
		  tobedeleted = temp_element->listptr;
		  temp_element->listptr = temp_element->listptr->listptr;
		  free (tobedeleted);
		  return temp;
		}
	      break;
	    case ship:
	      if (strcmp
		  (((struct ship *) (temp_element->listptr->item))->name,
		   symbol) == 0)
		{
		  temp = temp_element->listptr->item;
		  tobedeleted = temp_element->listptr;
		  temp_element->listptr = temp_element->listptr->listptr;
		  free (tobedeleted);
		  return temp;
		}
	      break;
	    }
	  if (temp_element->listptr != NULL)
	    temp_element = temp_element->listptr;
	}
      else
	temp_element = temp_element->listptr;
    }
  return NULL;
}

//Returns NULL if the addition is not made, and item if it is
void * insertitem (void *item, enum listtype type, struct list *hash_table[],
		   int hash_length)
{
  int key;
  struct list *new_element = (struct list *) malloc (sizeof (struct list *));
  struct list *e_pointer;
  char *symbol;

  new_element->item = item;
  new_element->type = type;
  new_element->listptr = NULL;


  //since all objects have name members, no need for mult. casts
  symbol = ((struct player *) item)->name;
  key = hash (symbol, hash_length);

  e_pointer = hash_table[key];
  while (1)
    {
      if (e_pointer == NULL)
	{
	  hash_table[key] = new_element;
	  return new_element->item;
	}
      else
	{
	  if (((struct player *) (e_pointer->item))->name != NULL
	      && symbol != NULL)
	    {
	      if (strcmp (((struct player *) (e_pointer->item))->name, symbol) ==
		  0)
		{
		  free (new_element);
		  return NULL;
		}
	      if (e_pointer->listptr != NULL)
		e_pointer = e_pointer->listptr;
	      else
		{
		  e_pointer->listptr = new_element;
		  return new_element->item;
		}
	    }
	}
    }
  perror ("this is a chaining HT, this should never be reached\n");
  return NULL;
}


/*********************************************\
 * hash                                       |
 *                                            |
 * description: returns the location in the   |
 * hash table based on the input, adds up the |
 * value of all the characters in string, and |
 * outputs it mod hash_length                 |
\*********************************************/

int
hash (const char *symbol, int hash_length)
{
  int x = 0;
  int temp_int = 0;

  while (symbol[x] != '\0')
    temp_int += (int) symbol[x++] * pow (2, x);

  return (temp_int % hash_length);
}
