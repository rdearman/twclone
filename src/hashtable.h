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

#ifndef HASH_LENGTH
#define HASH_LENGTH 200
#endif

#ifndef HASH_TABLE_H
#define HASH_TABLE_H

#include "universe.h"

void init_hash_table (struct list *hash_table[], int hash_length);
int hash (const char *symbol, int hash_length);
void *find (const char *symbol, enum listtype type, struct list *hash_table[],
	    int hash_length);
void *insert (const char *symbol, enum listtype type,
	      struct list *hash_table[], int hash_length);
void *delete (const char *symbol, enum listtype type,
	      struct list *hash_table[], int hash_length);
void *insertitem (void *item, enum listtype type, struct list *hash_table[],
		  int hash_length);

#endif
