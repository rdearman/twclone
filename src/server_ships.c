#include <stdatomic.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#include <jansson.h>		/* -ljansson */
#include <stdbool.h>
#include <sqlite3.h>
/* local includes */
#include "database.h"
#include "schemas.h"
#include "errors.h"
#include "config.h"
#include "server_cmds.h"
#include "server_loop.h"
#include "common.h"
#include "server_ships.h"   


void handle_move_pathfind (client_ctx_t *ctx, json_t *root);

/* -------- move.pathfind: BFS path A->B with avoid list -------- */
 void
handle_move_pathfind (client_ctx_t *ctx, json_t *root)
{
  if (!ctx) return;

  /* Parse request data */
  json_t *data = root ? json_object_get(root, "data") : NULL;

  /* from: null -> current sector */
  int from = (ctx->sector_id > 0) ? ctx->sector_id : 1;
  if (data) {
    json_t *jfrom = json_object_get(data, "from");
    if (jfrom && json_is_integer(jfrom)) from = (int)json_integer_value(jfrom);
    /* if null or missing, keep default */
  }

  /* to: required */
  int to = -1;
  if (data) {
    json_t *jto = json_object_get(data, "to");
    if (jto && json_is_integer(jto)) to = (int)json_integer_value(jto);
  }
  if (to <= 0) {
    send_enveloped_error(ctx->fd, root, 1401, "Target sector not specified");
    return;
  }

  /* Build avoid set (optional) */
  /* We’ll size bitsets by max sector id */
  sqlite3 *db = db_get_handle();
  int max_id = 0;
  sqlite3_stmt *st = NULL;

  pthread_mutex_lock(&db_mutex);
  if (sqlite3_prepare_v2(db, "SELECT MAX(id) FROM sectors", -1, &st, NULL) == SQLITE_OK &&
      sqlite3_step(st) == SQLITE_ROW) {
    max_id = sqlite3_column_int(st, 0);
  }
  if (st) { sqlite3_finalize(st); st = NULL; }
  pthread_mutex_unlock(&db_mutex);

  if (max_id <= 0) {
    send_enveloped_error(ctx->fd, root, 1401, "No sectors");
    return;
  }

  /* Clamp from/to to valid range quickly */
  if (from <= 0 || from > max_id || to > max_id) {
    send_enveloped_error(ctx->fd, root, 1401, "Sector not found");
    return;
  }

  /* allocate simple arrays sized max_id+1 */
  size_t N = (size_t)max_id + 1;
  unsigned char *avoid = (unsigned char*)calloc(N, 1);
  int *prev = (int*)malloc(N * sizeof(int));
  unsigned char *seen = (unsigned char*)calloc(N, 1);
  int *queue = (int*)malloc(N * sizeof(int));

  if (!avoid || !prev || !seen || !queue) {
    free(avoid); free(prev); free(seen); free(queue);
    send_enveloped_error(ctx->fd, root, 1500, "Out of memory");
    return;
  }

  /* Fill avoid */
  if (data) {
    json_t *javoid = json_object_get(data, "avoid");
    if (javoid && json_is_array(javoid)) {
      size_t i, len = json_array_size(javoid);
      for (i = 0; i < len; ++i) {
        json_t *v = json_array_get(javoid, i);
        if (json_is_integer(v)) {
          int sid = (int)json_integer_value(v);
          if (sid > 0 && sid <= max_id) avoid[sid] = 1;
        }
      }
    }
  }

  /* If target or source is avoided, unreachable */
  if (avoid[to] || avoid[from]) {
    free(avoid); free(prev); free(seen); free(queue);
    send_enveloped_error(ctx->fd, root, 1406, "Path not found");
    return;
  }

  /* Trivial path */
  if (from == to) {
    json_t *steps = json_array();
    json_array_append_new(steps, json_integer(from));
    json_t *out = json_object();
    json_object_set_new(out, "steps", steps);
    json_object_set_new(out, "total_cost", json_integer(0));
    send_enveloped_ok(ctx->fd, root, "move.path_v1", out);
    free(avoid); free(prev); free(seen); free(queue);
    return;
  }

  /* Prepare neighbor query once */
  pthread_mutex_lock(&db_mutex);
  int rc = sqlite3_prepare_v2(db,
      "SELECT to_sector FROM sector_warps WHERE from_sector = ?1",
      -1, &st, NULL);
  pthread_mutex_unlock(&db_mutex);

  if (rc != SQLITE_OK || !st) {
    free(avoid); free(prev); free(seen); free(queue);
    send_enveloped_error(ctx->fd, root, 1500, "Pathfind init failed");
    return;
  }

  /* BFS */
  for (int i = 0; i <= max_id; ++i) prev[i] = -1;
  int qh = 0, qt = 0;
  queue[qt++] = from;
  seen[from] = 1;

  int found = 0;

  while (qh < qt) {
    int u = queue[qh++];

    /* fetch neighbors of u */
    pthread_mutex_lock(&db_mutex);
    sqlite3_reset(st);
    sqlite3_clear_bindings(st);
    sqlite3_bind_int(st, 1, u);
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
      int v = sqlite3_column_int(st, 0);
      if (v <= 0 || v > max_id) continue;
      if (avoid[v] || seen[v]) continue;
      seen[v] = 1;
      prev[v] = u;
      queue[qt++] = v;
      if (v == to) {
        found = 1;
        /* still finish stepping rows to keep stmt sane */
        /* break after unlock */
      }
    }
    pthread_mutex_unlock(&db_mutex);
    if (found) break;
  }

  /* finalize stmt */
  pthread_mutex_lock(&db_mutex);
  sqlite3_finalize(st);
  pthread_mutex_unlock(&db_mutex);

  if (!found) {
    free(avoid); free(prev); free(seen); free(queue);
    send_enveloped_error(ctx->fd, root, 1406, "Path not found");
    return;
  }

  /* Reconstruct path */
  json_t *steps = json_array();
  int cur = to;
  int hops = 0;
  /* backtrack into a simple stack (we can append to a temp C array then JSON) */
  int *stack = (int*)malloc(N * sizeof(int));
  if (!stack) {
    free(avoid); free(prev); free(seen); free(queue);
    send_enveloped_error(ctx->fd, root, 1500, "Out of memory");
    return;
  }

  int sp = 0;
  while (cur != -1) {
    stack[sp++] = cur;
    if (cur == from) break;
    cur = prev[cur];
  }
  /* If we didn’t reach 'from', something’s off */
  if (stack[sp-1] != from) {
    free(stack); free(avoid); free(prev); free(seen); free(queue);
    send_enveloped_error(ctx->fd, root, 1406, "Path not found");
    return;
  }
  /* reverse into JSON steps: from .. to */
  for (int i = sp - 1; i >= 0; --i) {
    json_array_append_new(steps, json_integer(stack[i]));
  }
  hops = sp - 1;
  free(stack);

  /* Build response */
  json_t *out = json_object();
  json_object_set_new(out, "steps", steps);
  json_object_set_new(out, "total_cost", json_integer(hops));

  send_enveloped_ok(ctx->fd, root, "move.path_v1", out);

  free(avoid); free(prev); free(seen); free(queue);
}

