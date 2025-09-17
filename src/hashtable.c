#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hashtable.h"
#include "universe.h"

int
hash (const char *symbol, int hash_length)
{
  int hashval;
  int a = 31415, b = 27183;

  for (hashval = 0; *symbol != '\0'; symbol++)
    {
      hashval = (a * hashval + *symbol) % hash_length;
      a = a * b % (hash_length - 1);
    }

  return hashval;
}

void
init_hash_table (struct list *hash_table[], int hash_length)
{
  int i;
  for (i = 0; i < hash_length; i++)
    {
      hash_table[i] = NULL;
    }
}

void *
find (const char *symbol, enum listtype type, struct list *hash_table[],
      int hash_length)
{
  struct list *curlist;
  curlist = hash_table[hash (symbol, hash_length)];
  while (curlist != NULL)
    {
      if (curlist->type == type)
	{
	  switch (type)
	    {
	    case player:
	      if (strcmp (((struct player *) curlist->item)->name,
			  symbol) == 0)
		return curlist->item;
	      break;
	    case planet:
	      if (strcmp (((struct planet *) curlist->item)->name,
			  symbol) == 0)
		return curlist->item;
	      break;
	    case port:
	      if (strcmp (((struct port *) curlist->item)->name, symbol) == 0)
		return curlist->item;
	      break;
	    case ship:
	      if (strcmp (((struct ship *) curlist->item)->name, symbol) == 0)
		return curlist->item;
	      break;
	    }
	}
      curlist = curlist->listptr;
    }
  return NULL;
}


void *
insert (const char *symbol, enum listtype type, struct list *hash_table[],
	int hash_length)
{
  struct list *newlist;
  int hashval;

  newlist = (struct list *) malloc (sizeof (struct list));

  newlist->type = type;

  switch (type)
    {
    case player:
      newlist->item = malloc (sizeof (struct player));
      ((struct player *) newlist->item)->name = strdup (symbol);
      break;
    case planet:
      newlist->item = malloc (sizeof (struct planet));
      ((struct planet *) newlist->item)->name = strdup (symbol);
      break;
    case port:
      newlist->item = malloc (sizeof (struct port));
      ((struct port *) newlist->item)->name = strdup (symbol);
      break;
    case ship:
      newlist->item = malloc (sizeof (struct ship));
      ((struct ship *) newlist->item)->name = strdup (symbol);
      break;
    }

  hashval = hash (symbol, hash_length);
  newlist->listptr = hash_table[hashval];
  hash_table[hashval] = newlist;
  return newlist->item;
}


void *
delete (const char *symbol, enum listtype type, struct list *hash_table[],
	int hash_length)
{
  struct list *curlist, *prevlist;
  void *delitem;
  int hashval;

  hashval = hash (symbol, hash_length);
  curlist = hash_table[hashval];
  prevlist = curlist;
  while (curlist != NULL)
    {
      if (curlist->type == type)
	{
	  switch (type)
	    {
	    case player:
	      if (strcmp (((struct player *) curlist->item)->name,
			  symbol) == 0)
		goto found;
	      break;
	    case planet:
	      if (strcmp (((struct planet *) curlist->item)->name,
			  symbol) == 0)
		goto found;
	      break;
	    case port:
	      if (strcmp (((struct port *) curlist->item)->name, symbol) == 0)
		goto found;
	      break;
	    case ship:
	      if (strcmp (((struct ship *) curlist->item)->name, symbol) == 0)
		goto found;
	      break;
	    }
	}
      prevlist = curlist;
      curlist = curlist->listptr;
    }
  return NULL;

found:
  if (curlist == prevlist)
    {
      hash_table[hashval] = curlist->listptr;
    }
  else
    {
      prevlist->listptr = curlist->listptr;
    }
  delitem = curlist->item;
  free (curlist->item);
  free (curlist);
  return delitem;
}


void *
insertitem (void *item, enum listtype type, struct list *hash_table[],
	    int hash_length)
{
  struct list *newlist;
  int hashval;
  char *name;

  newlist = (struct list *) malloc (sizeof (struct list));

  newlist->type = type;
  newlist->item = item;

  switch (type)
    {
    case player:
      name = ((struct player *) item)->name;
      break;
    case planet:
      name = ((struct planet *) item)->name;
      break;
    case port:
      name = ((struct port *) item)->name;
      break;
    case ship:
      name = ((struct ship *) item)->name;
      break;
    }

  hashval = hash (name, hash_length);
  newlist->listptr = hash_table[hashval];
  hash_table[hashval] = newlist;

  return newlist->item;
}


void *
deleteitem (void *item, enum listtype type, struct list *hash_table[],
	    int hash_length)
{
  struct list *curlist, *prevlist;
  void *delitem;
  int hashval;
  char *name;

  switch (type)
    {
    case player:
      name = ((struct player *) item)->name;
      break;
    case planet:
      name = ((struct planet *) item)->name;
      break;
    case port:
      name = ((struct port *) item)->name;
      break;
    case ship:
      name = ((struct ship *) item)->name;
      break;
    }

  hashval = hash (name, hash_length);
  curlist = hash_table[hashval];
  prevlist = curlist;
  while (curlist != NULL)
    {
      if (curlist->type == type && curlist->item == item)
	goto found;
      prevlist = curlist;
      curlist = curlist->listptr;
    }
  return NULL;

found:
  if (curlist == prevlist)
    {
      hash_table[hashval] = curlist->listptr;
    }
  else
    {
      prevlist->listptr = curlist->listptr;
    }
  delitem = curlist->item;
  free (curlist);
  return delitem;
}
