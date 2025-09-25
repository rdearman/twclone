#ifndef SERVER_ENGINE_H
#define SERVER_ENGINE_H

#include <pthread.h>

// Forward declaration of the game engine function
// This is the main entry point for the game loop thread.
void *game_engine_thread(void *arg);

#endif // SERVER_ENGINE_H
