/* src/server_loop.c */
#include <stdatomic.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <jansson.h>
#include <stdbool.h>

/* local includes */
#include "database.h"
#include "game_db.h"
#include "schemas.h"
#include "errors.h"
#include "config.h"
#include "server_cmds.h"
#include "server_ships.h"
#include "common.h"
#include "server_envelope.h"
#include "server_players.h"
#include "server_ports.h"
#include "server_auth.h"
#include "server_s2s.h"
#include "server_universe.h"
#include "server_autopilot.h"
#include "server_config.h"
#include "server_communication.h"
#include "server_planets.h"
#include "server_citadel.h"
#include "server_combat.h"
#include "server_bulk.h"
#include "server_news.h"
#include "server_log.h"
#include "server_stardock.h"
#include "server_corporation.h"
#include "server_bank.h"
#include "server_cron.h"
#include "database_cmd.h"
#include "db/db_api.h"

client_node_t *g_clients = NULL;
pthread_mutex_t g_clients_mu = PTHREAD_MUTEX_INITIALIZER;

void server_register_client (client_ctx_t *ctx)
{
  pthread_mutex_lock (&g_clients_mu);
  client_node_t *n = calloc (1, sizeof (*n));
  if (n) { n->ctx = ctx; n->next = g_clients; g_clients = n; }
  pthread_mutex_unlock (&g_clients_mu);
}

void server_unregister_client (client_ctx_t *ctx)
{
  pthread_mutex_lock (&g_clients_mu);
  client_node_t **pp = &g_clients;
  while (*pp) {
      if ((*pp)->ctx == ctx) { client_node_t *dead = *pp; *pp = (*pp)->next; free (dead); break; }
      pp = &((*pp)->next);
  }
  pthread_mutex_unlock (&g_clients_mu);
}

int server_deliver_to_player (int pid, const char *type, json_t *data)
{
  int sent = 0; pthread_mutex_lock (&g_clients_mu);
  for (client_node_t *n = g_clients; n; n = n->next) {
      if (n->ctx && n->ctx->player_id == pid) {
          json_t *tmp = json_incref(data); send_response_ok_take(n->ctx, NULL, type, &tmp); sent++;
      }
  }
  pthread_mutex_unlock (&g_clients_mu); return sent ? 0 : -1;
}

static int broadcast_sweep_once (db_t *db, int max)
{
  (void)db; (void)max; return 0;
}

static void process_message (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle(); if (!db) { send_response_error(ctx, root, ERR_DB, "No DB"); return; }
  json_t *cmd = json_object_get(root, "command");
  if (!cmd || !json_is_string(cmd)) { send_response_error(ctx, root, ERR_INVALID_SCHEMA, "Bad schema"); return; }
  const char *c = json_string_value(cmd);
  if (strcasecmp(c, "auth.login") == 0) cmd_auth_login(ctx, root);
  else if (strcasecmp(c, "auth.register") == 0) cmd_auth_register(ctx, root);
  else if (strcasecmp(c, "move.warp") == 0) cmd_move_warp(ctx, root);
  else if (strcasecmp(c, "sector.scan") == 0) cmd_sector_scan(ctx, root);
  else if (strcasecmp(c, "ship.status") == 0) cmd_ship_status(ctx, root);
  else send_response_error(ctx, root, ERR_INVALID_SCHEMA, "Unknown cmd");
}

static void * connection_thread (void *arg)
{
  client_ctx_t *ctx = (client_ctx_t *)arg; char buf[8192];
  while (*ctx->running) {
      ssize_t n = recv(ctx->fd, buf, sizeof(buf), 0);
      if (n > 0) {
          json_error_t jerr; json_t *root = json_loadb(buf, n, 0, &jerr);
          if (root) { process_message(ctx, root); json_decref(root); }
      } else if (n == 0) break;
  }
  close(ctx->fd); server_unregister_client(ctx); free(ctx); return NULL;
}

int server_loop (volatile sig_atomic_t *running)
{
  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons(g_cfg.server_port), .sin_addr.s_addr = INADDR_ANY };
  bind(listen_fd, (struct sockaddr *)&sa, sizeof(sa)); listen(listen_fd, 128);
  while (*running) {
      struct pollfd pfd = { .fd = listen_fd, .events = POLLIN };
      if (poll(&pfd, 1, 100) > 0) {
          client_ctx_t *ctx = calloc(1, sizeof(*ctx));
          ctx->fd = accept(listen_fd, NULL, NULL); ctx->running = running;
          server_register_client(ctx); pthread_t th;
          pthread_create(&th, NULL, connection_thread, ctx); pthread_detach(th);
      }
  }
  close(listen_fd); return 0;
}