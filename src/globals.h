#ifndef GLOBALS_H
#define GLOBALS_H

#include <time.h>		// You need this for the time_t type

extern int threadid;
extern int threadcount;
extern int msgidin;
extern int msgidout;
extern int shutdown_flag;
extern time_t next_process;

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


#endif // GLOBALS_H
