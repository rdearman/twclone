// server_engine.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include "engine_main.h"
#include "database.h"

// Keep the legacy thread entry (unused after forking, but preserved)
#define GAME_TICK_INTERVAL_SEC 60
void *game_engine_thread(void *arg) {
  (void)arg;
  printf("[engine] (thread) started; legacy placeholder.\n");
  while (true) {
    printf("[engine] (thread) tick…\n");
    sleep(GAME_TICK_INTERVAL_SEC);
  }
  return NULL;
}

/* ----- Forked engine process implementation ----- */

static int engine_main_loop(int shutdown_fd) {
  // Child process: initialise its own DB handle
  if (db_init() != 0) {
    fprintf(stderr, "[engine] db_init failed in child.\n");
    return 1;
  }

  printf("[engine] child up. pid=%d\n", getpid());

  const int tick_ms = 500; // quick tick; we can fetch from DB config later
  struct pollfd pfd = { .fd = shutdown_fd, .events = POLLIN };

  // Watermarks/config bootstrap would go here (per ENGINE.md)
  // …

  for (;;) {
    // Sleep until next tick or until shutdown pipe changes
    int rc = poll(&pfd, 1, tick_ms);
    if (rc > 0) {
      // either data or EOF on the pipe means: time to exit
      char buf[8];
      ssize_t n = read(shutdown_fd, buf, sizeof(buf));
      (void)n; // We don't care what's read; any activity/EOF = shutdown
      printf("[engine] shutdown signal received.\n");
      break;
    }
    if (rc == 0) {
      // timeout -> do one bounded tick of work
      // (placeholders now; wire real sweepers per ENGINE.md)
      // - consume events in ASC batches
      // - run due cron_tasks
      // - NPC step, TTL sweepers, etc.
      // - update engine_offset watermark
      // Keep each unit short and idempotent.
      // …
      // For now, just a heartbeat:
      static time_t last = 0;
      time_t now = time(NULL);
      if (now != last) { printf("[engine] tick @ %ld\n", (long)now); last = now; }
    } else if (rc < 0 && errno != EINTR) {
      fprintf(stderr, "[engine] poll error: %s\n", strerror(errno));
      break;
    }
  }

  db_close();
  printf("[engine] child exiting cleanly.\n");
  return 0;
}

int engine_spawn(pid_t *out_pid, int *out_shutdown_fd) {
  int pipefd[2];
  if (pipe(pipefd) != 0) {
    perror("pipe");
    return -1;
  }
  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    close(pipefd[0]); close(pipefd[1]);
    return -1;
  }
  if (pid == 0) {
    // Child: read-end open; close write-end
    close(pipefd[1]);
    int ec = engine_main_loop(pipefd[0]);
    // ensure child exits here regardless of parent state
    _exit(ec);
  }
  // Parent: keep write-end to signal shutdown; close read-end
  close(pipefd[0]);
  if (out_pid) *out_pid = pid;
  if (out_shutdown_fd) *out_shutdown_fd = pipefd[1];
  printf("[server] engine forked. pid=%d\n", (int)pid);
  return 0;
}

int engine_request_shutdown(int shutdown_fd) {
  // Closing the parent's write-end causes EOF in the child -> graceful exit
  if (shutdown_fd >= 0) close(shutdown_fd);
  return 0;
}

int engine_wait(pid_t pid, int timeout_ms) {
  // Simple timed waitpid loop
  const int step_ms = 50;
  int elapsed = 0;
  for (;;) {
    int status = 0;
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) return 0;          // reaped
    if (r < 0) return -1;            // error
    if (timeout_ms >= 0 && elapsed >= timeout_ms) return 1; // still running
    usleep(step_ms * 1000);
    elapsed += step_ms;
  }
}
