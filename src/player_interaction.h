
#ifndef PLAYER_INTERACTION_H
#define PLAYER_INTERACTION_H

#include <jansson.h>
#include <pthread.h>
#include "universe.h"


struct connectinfo
{
  int sockid;
  int msgidin;
  int msgidout;
};

extern void *handle_player (void *threadinfo);
extern void *makeplayerthreads (void *threadinfo);

json_t *list_hardware (json_t * json_data, struct player *curplayer);
json_t *buyhardware (json_t * json_data, struct player *curplayer);

// A structure to define hardware items
struct hardware
{
  int id;
  const char *name;
  int price;
  int size;
};


#endif
