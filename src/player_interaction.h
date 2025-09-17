
#ifndef PLAYER_INTERACTION_H
#define PLAYER_INTERACTION_H

#include <jansson.h>
#include <pthread.h>

struct connectinfo
{
  int sockid;
  int msgidin;
  int msgidout;
};

void *handle_player (void *threadinfo);
void *makeplayerthreads (void *threadinfo);

#endif
