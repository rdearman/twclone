#ifndef GLOBALS_H
#define GLOBALS_H

#include <time.h>		// You need this for the time_t type
#include "common.h"		// Include common.h for armid_mine_config_t

extern int threadid;
extern int threadcount;
extern int msgidin;
extern int msgidout;
extern int shutdown_flag;
extern time_t next_process;
extern armid_mine_config_t g_armid_config;

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
