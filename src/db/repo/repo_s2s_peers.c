#include "repo_s2s_peers.h"
#include "db/sql_driver.h"
#include "server_log.h"
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int
repo_s2s_peer_list (db_t *db, s2s_peer_t **out_peers, int *out_count)
{
  if (!db || !out_peers || !out_count)
    return -1;

  const char *sql = "SELECT peer_id, host, port, enabled, shared_key_id, "
                    "EXTRACT(EPOCH FROM last_seen_at)::bigint, notes, "
                    "EXTRACT(EPOCH FROM created_at)::bigint "
                    "FROM s2s_peers ORDER BY peer_id";

  db_error_t err;
  db_error_clear (&err);
  db_res_t *res = NULL;

  if (!db_query (db, sql, NULL, 0, &res, &err))
    {
      LOGE ("repo_s2s_peer_list: query failed: %s", err.message);
      return -1;
    }

  int count = 0;
  s2s_peer_t *peers = NULL;
  int capacity = 10;
  peers = malloc (capacity * sizeof (s2s_peer_t));
  if (!peers)
    {
      db_res_finalize (res);
      return -1;
    }

  while (db_res_step (res, &err))
    {
      if (count >= capacity)
        {
          capacity *= 2;
          s2s_peer_t *tmp = realloc (peers, capacity * sizeof (s2s_peer_t));
          if (!tmp)
            {
              free (peers);
              db_res_finalize (res);
              return -1;
            }
          peers = tmp;
        }

      const char *peer_id = db_res_col_text (res, 0, &err);
      const char *host = db_res_col_text (res, 1, &err);
      int port = db_res_col_int (res, 2, &err);
      int enabled = db_res_col_bool (res, 3, &err);
      const char *shared_key_id = db_res_col_text (res, 4, &err);
      long last_seen = db_res_col_i64 (res, 5, &err);
      const char *notes = db_res_col_text (res, 6, &err);
      long created_at = db_res_col_i64 (res, 7, &err);

      if (peer_id && host && shared_key_id)
        {
          s2s_peer_t *p = &peers[count];
          snprintf (p->peer_id, sizeof (p->peer_id), "%s", peer_id);
          snprintf (p->host, sizeof (p->host), "%s", host);
          p->port = port;
          p->enabled = enabled;
          snprintf (p->shared_key_id, sizeof (p->shared_key_id), "%s",
                    shared_key_id);
          p->last_seen_at = (time_t) last_seen;
          if (notes)
            snprintf (p->notes, sizeof (p->notes), "%s", notes);
          else
            p->notes[0] = '\0';
          p->created_at = (time_t) created_at;
          count++;
        }
    }

  db_res_finalize (res);
  *out_peers = peers;
  *out_count = count;
  return 0;
}

int
repo_s2s_peer_get (db_t *db, const char *peer_id, s2s_peer_t *out_peer)
{
  if (!db || !peer_id || !out_peer)
    return -1;

  const char *sql = "SELECT peer_id, host, port, enabled, shared_key_id, "
                    "EXTRACT(EPOCH FROM last_seen_at)::bigint, notes, "
                    "EXTRACT(EPOCH FROM created_at)::bigint "
                    "FROM s2s_peers WHERE peer_id = {1}";

  db_error_t err;
  db_error_clear (&err);
  db_res_t *res = NULL;
  db_bind_t params[] = {db_bind_text (peer_id)};

  if (!db_query (db, sql, params, 1, &res, &err))
    {
      LOGE ("repo_s2s_peer_get: query failed: %s", err.message);
      return -1;
    }

  int found = 0;
  if (db_res_step (res, &err))
    {
      const char *p_id = db_res_col_text (res, 0, &err);
      const char *host = db_res_col_text (res, 1, &err);
      int port = db_res_col_int (res, 2, &err);
      int enabled = db_res_col_bool (res, 3, &err);
      const char *shared_key_id = db_res_col_text (res, 4, &err);
      long last_seen = db_res_col_i64 (res, 5, &err);
      const char *notes = db_res_col_text (res, 6, &err);
      long created_at = db_res_col_i64 (res, 7, &err);

      if (p_id && host && shared_key_id)
        {
          snprintf (out_peer->peer_id, sizeof (out_peer->peer_id), "%s",
                    p_id);
          snprintf (out_peer->host, sizeof (out_peer->host), "%s", host);
          out_peer->port = port;
          out_peer->enabled = enabled;
          snprintf (out_peer->shared_key_id,
                    sizeof (out_peer->shared_key_id), "%s", shared_key_id);
          out_peer->last_seen_at = (time_t) last_seen;
          if (notes)
            snprintf (out_peer->notes, sizeof (out_peer->notes), "%s", notes);
          else
            out_peer->notes[0] = '\0';
          out_peer->created_at = (time_t) created_at;
          found = 1;
        }
    }

  db_res_finalize (res);
  return found ? 0 : -1;
}

int
repo_s2s_peer_upsert (db_t *db, const s2s_peer_t *peer)
{
  if (!db || !peer || !peer->shared_key_id)
    return -1;
  if (peer->peer_id[0] == '\0' || peer->host[0] == '\0' || peer->port <= 0)
    return -1;

  const char *now_expr = sql_now_expr (db);
  if (!now_expr)
    {
      LOGE ("repo_s2s_peer_upsert: Unsupported database backend");
      return -1;
    }

  /* Try insert; if conflict, update */
  char sql_insert[512];
  snprintf (sql_insert, sizeof (sql_insert),
            "INSERT INTO s2s_peers (peer_id, host, port, enabled, "
            "shared_key_id, notes, created_at) VALUES ({1}, {2}, {3}, {4}, "
            "{5}, {6}, %s) "
            "ON CONFLICT(peer_id) DO UPDATE SET "
            "host={2}, port={3}, enabled={4}, shared_key_id={5}, notes={6}",
            now_expr);

  db_error_t err;
  db_error_clear (&err);

  db_bind_t params[] = {db_bind_text (peer->peer_id),
                        db_bind_text (peer->host),
                        db_bind_i32 ((int32_t) peer->port),
                        db_bind_bool ((bool) peer->enabled),
                        db_bind_text (peer->shared_key_id),
                        db_bind_text (peer->notes[0] ? peer->notes : "")};

  if (!db_exec (db, sql_insert, params, 6, &err))
    {
      LOGE ("repo_s2s_peer_upsert: exec failed: %s", err.message);
      return -1;
    }

  return 0;
}

int
repo_s2s_peer_set_enabled (db_t *db, const char *peer_id, int enabled)
{
  if (!db || !peer_id)
    return -1;

  const char *sql = "UPDATE s2s_peers SET enabled = {1} WHERE peer_id = {2}";

  db_error_t err;
  db_error_clear (&err);
  db_bind_t params[] = {db_bind_bool (enabled), db_bind_text (peer_id)};

  if (!db_exec (db, sql, params, 2, &err))
    {
      LOGE ("repo_s2s_peer_set_enabled: exec failed: %s", err.message);
      return -1;
    }

  return 0;
}

int
repo_s2s_peer_touch_last_seen (db_t *db, const char *peer_id)
{
  if (!db || !peer_id)
    return -1;

  const char *now_expr = sql_now_expr (db);
  if (!now_expr)
    {
      LOGE ("repo_s2s_peer_touch_last_seen: Unsupported database backend");
      return -1;
    }

  char sql[256];
  snprintf (sql, sizeof (sql),
            "UPDATE s2s_peers SET last_seen_at = %s WHERE peer_id = {1}",
            now_expr);

  db_error_t err;
  db_error_clear (&err);
  db_bind_t params[] = {db_bind_text (peer_id)};

  if (!db_exec (db, sql, params, 1, &err))
    {
      LOGE ("repo_s2s_peer_touch_last_seen: exec failed: %s", err.message);
      return -1;
    }

  return 0;
}

int
repo_s2s_nonce_check_and_insert (db_t *db, const char *peer_id,
                                  const char *nonce, time_t msg_ts)
{
  if (!db || !peer_id || !nonce)
    return -1;

  const char *now_expr = sql_now_expr (db);
  if (!now_expr)
    {
      LOGE ("repo_s2s_nonce_check_and_insert: Unsupported database backend");
      return -1;
    }

  /* Insert; if conflict (duplicate key), nonce was already seen */
  char sql_insert[512];
  snprintf (sql_insert, sizeof (sql_insert),
            "INSERT INTO s2s_nonce_seen (peer_id, nonce, msg_ts, seen_at) "
            "VALUES ({1}, {2}, to_timestamp({3}), %s)",
            now_expr);

  db_error_t err;
  db_error_clear (&err);

  db_bind_t params[] = {db_bind_text (peer_id), db_bind_text (nonce),
                        db_bind_i64 ((long long) msg_ts)};

  if (!db_exec (db, sql_insert, params, 3, &err))
    {
      if (err.code == 1006 || strstr (err.message, "UNIQUE") != NULL
          || strstr (err.message, "duplicate") != NULL)
        {
          /* Replay detected */
          LOGD ("Replay: peer %s, nonce %s already seen", peer_id, nonce);
          return -1;
        }
      LOGE ("repo_s2s_nonce_check_and_insert: exec failed: %s",
            err.message);
      return -1;
    }

  return 0;
}

int
repo_s2s_nonce_cleanup (db_t *db, int age_seconds)
{
  if (!db || age_seconds <= 0)
    return -1;

  char sql[256];
  snprintf (sql, sizeof (sql),
            "DELETE FROM s2s_nonce_seen WHERE seen_at < "
            "CURRENT_TIMESTAMP - INTERVAL '%d seconds'",
            age_seconds);

  db_error_t err;
  db_error_clear (&err);

  if (!db_exec (db, sql, NULL, 0, &err))
    {
      LOGE ("repo_s2s_nonce_cleanup: exec failed: %s", err.message);
      return -1;
    }

  return 0;
}
