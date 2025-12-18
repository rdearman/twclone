#ifndef SERVER_ENGINE_H
#define SERVER_ENGINE_H
#include <pthread.h>
#include <sys/types.h>		// pid_t
#include <jansson.h>		// for json_t
// Forward declaration of the game engine function
// This is the main entry point for the game loop thread.
void *game_engine_thread (void *arg);
// Forked-process API
int engine_spawn (pid_t * out_pid, int *out_shutdown_fd);
int engine_request_shutdown (int shutdown_fd);	// signal the child to exit
int engine_wait (pid_t pid, int timeout_ms);	// reap with timeout
// Function to process engine event payloads for player progress updates
int h_player_progress_from_event_payload (json_t * ev_payload);
#endif // SERVER_ENGINE_H
