#ifndef SERVER_LOOP_H
#define SERVER_LOOP_H
#pragma once
#include <signal.h>
#include "database.h"		/* for db_handle, etc. */
// #include "universe.h"                /* for sector/planet structures if needed */
//#include "player_interaction.h"

/* Single, canonical declaration */
int server_loop (volatile sig_atomic_t * running);

#endif /* SERVER_LOOP_H */
