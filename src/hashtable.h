#ifndef HASH_TABLE_H
#define HASH_TABLE_H

#include "types.h"

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
void *deleteitem (void *item, enum listtype type, struct list *hash_table[],
		  int hash_length);

#endif
