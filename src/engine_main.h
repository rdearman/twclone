// server_engine.h
#ifndef ENGINE_MAIN_H
#define ENGINE_MAIN_H

#include <pthread.h>
#include <sys/types.h> // pid_t

// (kept for now; harmless if unused)
void *game_engine_thread(void *arg);

// Forked-process API
int  engine_spawn(pid_t *out_pid, int *out_shutdown_fd);
int  engine_request_shutdown(int shutdown_fd);  // signal the child to exit
int  engine_wait(pid_t pid, int timeout_ms);    // reap with timeout

#endif // SERVER_ENGINE_H
