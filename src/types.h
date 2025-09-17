#ifndef TYPES_H
#define TYPES_H

#define HASH_LENGTH 500

enum listtype
{
  player,
  planet,
  port,
  ship
};

struct list
{
  void *item;
  enum listtype type;
  struct list *listptr;
};

#endif
