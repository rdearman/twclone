/* src/server_engine.c */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <math.h>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/types.h>
#include <jansson.h>
#include <inttypes.h>

/* local includes */
#include "s2s_keyring.h"
#include "s2s_transport.h"
#include "database.h"
#include "game_db.h"
#include "server_envelope.h"
#include "engine_consumer.h"
#include "server_engine.h"
#include "server_loop.h"
#include "server_universe.h"
#include "server_log.h"
#include "server_cron.h"
#include "common.h"
#include "server_config.h"
#include "server_clusters.h"
#include "globals.h"
#include "database_cmd.h"
#include "server_stardock.h"
#include "server_players.h"
#include "db/db_api.h"

static eng_consumer_cfg_t G_CFG = { .batch_size = 200, .backlog_prio_threshold = 5000, .consumer_key = "game_engine" };

void engine_tick (db_t *db)
{
  eng_consumer_metrics_t m;
  if (engine_consume_tick (db, &G_CFG, &m) == 0) {
      LOGI ("engine processed=%d last_id=%lld", m.processed, m.last_event_id);
  }
}

static int server_commands_tick (db_t *db, int max_rows)
{
  (void)db; (void)max_rows; return 0;
}

static int engine_main_loop (int shutdown_fd)
{
  server_log_init_file ("./twclone.log", "[engine]", 0, LOG_INFO);
  db_t *db = game_db_get_handle (); if (!db) return 1;
  struct pollfd pfd = { .fd = shutdown_fd, .events = POLLIN };
  for (;;) {
      int rc = poll (&pfd, 1, 500);
      if (rc > 0) break;
      if (rc == 0) {
          engine_tick(db);
          server_commands_tick(db, 16);
      }
  }
  game_db_close (); return 0;
}

int engine_spawn (pid_t *out_pid, int *out_shutdown_fd)
{
  int pipefd[2]; if (pipe (pipefd) != 0) return -1;
  pid_t pid = fork ();
  if (pid < 0) { close (pipefd[0]); close (pipefd[1]); return -1; }
  if (pid == 0) { close (pipefd[1]); _exit (engine_main_loop (pipefd[0])); }
  close (pipefd[0]);
  if (out_pid) *out_pid = pid; if (out_shutdown_fd) *out_shutdown_fd = pipefd[1];
  return 0;
}

int engine_request_shutdown (int fd) { if (fd >= 0) close (fd); return 0; }
int engine_wait (pid_t pid, int timeout_ms) { (void)timeout_ms; waitpid(pid, NULL, 0); return 0; }
int h_player_progress_from_event_payload (json_t *ev) { (void)ev; return 0; }
json_t * engine_build_command_push (const char *t, const char *k, json_t *p, const char *c, int pr) { (void)t; (void)k; (void)p; (void)c; (void)pr; return NULL; }