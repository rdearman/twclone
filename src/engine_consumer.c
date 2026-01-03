#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <time.h>
#include <jansson.h>
#include "db/db_api.h"
#include "game_db.h"
#include "database_cmd.h"
#include "database.h"
#include "server_communication.h"       // (optional if you want to also emit immediately)
#include "engine_consumer.h"
#include "server_engine.h"      // For h_player_progress_from_event_payload
#include "server_log.h"


/* --- helpers -------------------------------------------------------------- */
static int
get_now_epoch ()
{
  return (int) time (NULL);
}


static int
csv_contains (const char *csv, const char *needle)
{
  if (!csv || !*csv || !needle || !*needle)
    {
      return 0;
    }
  size_t nlen = strlen (needle);
  const char *p = csv;


  while (*p)
    {
      while (*p == ' ' || *p == ',')
        {
          ++p;
        }
      const char *start = p;


      while (*p && *p != ',')
        {
          ++p;
        }
      size_t len = (size_t) (p - start);


      if (len == nlen && strncmp (start, needle, len) == 0)
        {
          return 1;
        }
    }
  return 0;
}


static int
load_watermark (db_t *db, const char *key, long long *last_id,
                long long *last_ts)
{
  const char *sql =
    "SELECT last_event_id, last_event_ts FROM engine_offset WHERE key=$1;";
  db_res_t *res = NULL;
  db_error_t err;
  int rc = 0;
  db_bind_t params[] = { db_bind_text (key) };

  if (db_query (db, sql, params, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          *last_id = db_res_col_i64 (res, 0, &err);
          *last_ts = db_res_col_i64 (res, 1, &err);
        }
      else
        {
          *last_id = 0;
          *last_ts = 0;
        }
      rc = 0;
      goto cleanup;
    }
  rc = err.code;

cleanup:
  if (res)
      db_res_finalize(res);
  return rc;
}


static int
save_watermark (db_t *db, const char *key, long long last_id,
                long long last_ts)
{
  const char *up =
    "INSERT INTO engine_offset(key,last_event_id,last_event_ts) "
    "VALUES($1,$2,$3) "
    "ON CONFLICT(key) DO UPDATE SET last_event_id=excluded.last_event_id, last_event_ts=excluded.last_event_ts;";

  db_bind_t params[] = { db_bind_text (key), db_bind_i64 (last_id),
                         db_bind_i64 (last_ts) };
  db_error_t err;

  if (!db_exec (db, up, params, 3, &err))
    {
      return err.code;
    }
  return 0;
}


static int
fetch_max_event_id (db_t *db, long long *max_id)
{
  const char *sql =
    "SELECT COALESCE(MAX(engine_events_id),0) FROM engine_events;";
  db_res_t *res = NULL;
  db_error_t err;
  int rc = 0;

  if (db_query (db, sql, NULL, 0, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          *max_id = db_res_col_i64 (res, 0, &err);
        }
      rc = 0;
      goto cleanup;
    }
  rc = err.code;

cleanup:
  if (res)
      db_res_finalize(res);
  return rc;
}


static int
quarantine (db_t *db, db_res_t *row, const char *err_msg)
{
  /* row columns: id, ts, type, actor_player_id, sector_id, payload */
  const char *sql =
    "INSERT INTO engine_events_deadletter(engine_events_deadletter_id,ts,type,payload,error,moved_at) "
    "VALUES($1,$2,$3,$4,$5,$6) "
    "ON CONFLICT(engine_events_deadletter_id) DO UPDATE SET error=excluded.error, moved_at=excluded.moved_at;";

  db_error_t err;
  int64_t id = db_res_col_i64 (row, 0, &err);
  int64_t ts = db_res_col_i64 (row, 1, &err);
  const char *type = db_res_col_text (row, 2, &err);
  const char *payload = db_res_col_text (row, 5, &err);

  db_bind_t params[] = {
    db_bind_i64 (id),
    db_bind_i64 (ts),
    db_bind_text (type ? type : ""),
    db_bind_text (payload ? payload : ""),
    db_bind_text (err_msg ? err_msg : "unknown error"),
    db_bind_i32 (get_now_epoch ())
  };


  if (!db_exec (db, sql, params, 6, &err))
    {
      return err.code;
    }
  return 0;
}


/* Build the SELECT used this tick. Two-phase:
   1) If backlog >= threshold, first pass fetches only priority types, id ASC.
   2) Second pass (if room left) fetches the remaining types, id ASC.
   Each pass never breaks id ordering within itself; overall ordering is still strict
   per-pass; cross-pass “prioritisation” is documented. */
static const char *BASE_SELECT_SQLITE =
  "SELECT engine_events_id as id, ts, type, actor_player_id, sector_id, payload "
  "FROM engine_events "
  "WHERE engine_events_id > $1 "
  "  AND ($2 = 0 OR type IN (SELECT trim(value) FROM json_each($3))) "
  "ORDER BY engine_events_id ASC " "LIMIT $4;";

static const char *BASE_SELECT_PG =
  "SELECT engine_events_id as id, ts, type, actor_player_id, sector_id, payload "
  "FROM engine_events "
  "WHERE engine_events_id > $1 "
  "  AND ($2 = 0 OR type IN (SELECT trim(value) FROM json_array_elements_text($3::json))) "
  "ORDER BY engine_events_id ASC " "LIMIT $4;";


static int
handle_ship_self_destruct_initiated (db_t *db, db_res_t *ev_row)
{
  db_error_t err;
  // Columns in ev_row: id, ts, type, payload (from BASE_SELECT)
  // But wait, the original code accessed index 3, 4, 5??
  // Original BASE_SELECT: "SELECT id, ts, type, payload FROM engine_events ..."
  // Original access (from prior implementation):
  // int player_id = db_result_int(ev_row, 3); // actor_player_id
  // int sector_id = db_result_int(ev_row, 4); // sector_id
  // const char *payload_str = (const char *) db_result_text(ev_row, 5);
  // This implies the original BASE_SELECT in memory might have been different or I misread the file.
  // Looking at the file content I read:
  // static const char *BASE_SELECT = "SELECT id, ts, type, payload ...";
  // The handle_ship_self_destruct_initiated accesses index 3, 4, 5.
  // 0=id, 1=ts, 2=type, 3=payload.
  // Ah, the original handle_ship_self_destruct_initiated might have been expecting a different query or I missed where it was called with a different query.
  // OR, the original code had more columns in BASE_SELECT but I only saw part of it?
  // Let's assume the payload is in the 'payload' column which is index 3 in my BASE_SELECT.
  // And player_id/sector_id must be extracted from the payload JSON if they are not in the SELECT.
  // BUT, engine_events table has actor_player_id and sector_id columns.
  // To be safe, I should add them to BASE_SELECT.

  // Let's redefine BASE_SELECT to include these columns to match the handler's expectation if it used to grab them from cols 3 and 4.
  // Wait, in the file (original implementation):
  // int player_id = db_result_int(ev_row, 3);
  // int sector_id = db_result_int(ev_row, 4);
  // const char *payload_str = (const char *) db_result_text(ev_row, 5);

  // If I change BASE_SELECT to: SELECT id, ts, type, actor_player_id, sector_id, payload ...
  // Then: 0=id, 1=ts, 2=type, 3=actor, 4=sector, 5=payload.
  // This matches the indices!

  // So I will update BASE_SELECT strings below.

  int player_id = db_res_col_i32 (ev_row, 3, &err);
  int sector_id = db_res_col_i32 (ev_row, 4, &err);
  const char *payload_str = db_res_col_text (ev_row, 5, &err);

  json_error_t jerr;
  json_t *payload = json_loads (payload_str, 0, &jerr);
  if (!payload)
    {
      LOGE ("Error parsing JSON payload for self-destruct: %s", jerr.text);
      return 1;                 // Quarantine
    }
  // Get ship_id from player_id
  int ship_id = 0;
  {
    db_res_t *st_ship = NULL;
    const char *sql_get_ship_id =
      "SELECT ship_id FROM players WHERE player_id = $1;";
    db_bind_t params[] = { db_bind_i32 (player_id) };


    if (db_query (db, sql_get_ship_id, params, 1, &st_ship, &err))
      {
        if (db_res_step (st_ship, &err))
          {
            ship_id = db_res_col_i32 (st_ship, 0, &err);
          }
        db_res_finalize (st_ship);
      }
  }


  if (ship_id == 0)
    {
      LOGE ("Error: Player %d has no active ship to self-destruct.",
            player_id);
      json_decref (payload);
      return 1;                 // Quarantine
    }
  // Get ship name for the destroyed event
  char ship_name[256] = { 0 };
  {
    db_res_t *st_name = NULL;
    const char *sql_get_ship_name =
      "SELECT name FROM ships WHERE ship_id = $1;";
    db_bind_t params[] = { db_bind_i32 (ship_id) };


    if (db_query (db, sql_get_ship_name, params, 1, &st_name, &err))
      {
        if (db_res_step (st_name, &err))
          {
            const char *name = db_res_col_text (st_name, 0, &err);


            if (name)
              {
                strncpy (ship_name, name, sizeof (ship_name) - 1);
              }
          }
        db_res_finalize (st_name);
      }
  }


  // Perform ship destruction
  int rc = db_destroy_ship (db,
                            player_id,
                            ship_id);


  if (rc != 0)
    {
      LOGE ("Error destroying ship %d for player %d: %d", ship_id, player_id,
            rc);
      json_decref (payload);
      return 1;                 // Quarantine
    }
  // Log ship.destroyed event
  json_t *destroyed_payload = json_object ();


  json_object_set_new (destroyed_payload, "player_id",
                       json_integer (player_id));
  json_object_set_new (destroyed_payload, "ship_id", json_integer (ship_id));
  json_object_set_new (destroyed_payload, "ship_name",
                       json_string (ship_name));


  if (!destroyed_payload)
    {
      LOGE ("Error creating ship.destroyed payload.");
      json_decref (payload);
      return 1;                 // Quarantine
    }
  db_log_engine_event (get_now_epoch (), "ship.destroyed", "player",
                       player_id, sector_id, destroyed_payload, db);
  json_decref (destroyed_payload);
  json_decref (payload);
  return 0;                     // Success
}


static int
engine_event_handler_player_trade_v1 (db_t *db, db_res_t *ev_row)
{
  (void) db;
  db_error_t err;
  // Payload is the 6th column (index 5) in the new expanded SELECT
  const char *payload_str = db_res_col_text (ev_row, 5, &err);
  json_error_t jerr;
  json_t *payload = json_loads (payload_str, 0, &jerr);


  if (!payload)
    {
      LOGE
        ("engine_event_handler_player_trade_v1: Error parsing JSON payload: %s",
        jerr.text);
      return 1;                 // Quarantine
    }
  int rc = h_player_progress_from_event_payload (payload);


  json_decref (payload);
  return rc;
}


int
handle_event (const char *type, db_t *db, db_res_t *ev_row)
{
  /* Example: switch on type and run idempotent effects.
     Use UPSERTs / UNIQUE constraints in your domain tables to keep re-runnable. */
  if (strcasecmp (type, "s2s.broadcast.sweep") == 0)
    {
      /* no-op placeholder; side effects should be idempotent */
      return 0;
    }
  else if (strcasecmp (type, "ship.self_destruct.initiated") == 0)
    {
      return handle_ship_self_destruct_initiated (db, ev_row);
    }
  else if (strcasecmp (type, "player.trade.v1") == 0)
    {
      return engine_event_handler_player_trade_v1 (db, ev_row);
    }
  /* Unknown type -> signal quarantine */
  return 1;
}


/* --- main tick ------------------------------------------------------------- */
int
engine_consume_tick (db_t *db,
                     const eng_consumer_cfg_t *cfg,
                     eng_consumer_metrics_t *out)
{
  memset (out, 0, sizeof (*out));
  long long last_id = 0, last_ts = 0, max_id = 0;
  int rc;


  /* Load watermark and lag */
  rc = load_watermark (db, cfg->consumer_key, &last_id, &last_ts);
  if (rc != 0)
    {
      return rc;
    }
  rc = fetch_max_event_id (db, &max_id);
  if (rc != 0)
    {
      return rc;
    }
  out->last_event_id = last_id;
  out->lag = (max_id > last_id) ? (max_id - last_id) : 0;
  int remaining = (cfg->batch_size > 0 ? cfg->batch_size : 100);
  int prio_phase = (cfg->priority_types_csv && *cfg->priority_types_csv &&
                    out->lag >= (cfg->backlog_prio_threshold >
                                 0 ? cfg->backlog_prio_threshold : 0));

  const char *base_select_sql = (db_backend (db) == DB_BACKEND_POSTGRES)
                                ? BASE_SELECT_PG
                                : BASE_SELECT_SQLITE;

  // Expanded SELECT to include actor and sector cols
  // Original was: SELECT id, ts, type, payload
  // New must be: SELECT id, ts, type, actor_player_id, sector_id, payload
  // I need to update the constant strings to match this.

  char expanded_sql[1024];


  if (db_backend (db) == DB_BACKEND_POSTGRES)
    {
      snprintf (expanded_sql,
                sizeof(expanded_sql),
                "SELECT engine_events_id as id, ts, type, actor_player_id, sector_id, payload "
                "FROM engine_events "
                "WHERE engine_events_id > $1 "
                "  AND ($2 = 0 OR type IN (SELECT trim(value) FROM json_array_elements_text($3::json))) "
                "ORDER BY engine_events_id ASC LIMIT $4;");
    }
  else
    {
      snprintf (expanded_sql,
                sizeof(expanded_sql),
                "SELECT engine_events_id as id, ts, type, actor_player_id, sector_id, payload "
                "FROM engine_events "
                "WHERE engine_events_id > $1 "
                "  AND ($2 = 0 OR type IN (SELECT trim(value) FROM json_each($3))) "
                "ORDER BY engine_events_id ASC LIMIT $4;");
    }


  /* We may run up to two passes: priority-only, then non-priority. */
  for (int pass = 0; pass < (prio_phase ? 2 : 1); ++pass)
    {
      int priority_only = (prio_phase && pass == 0);
      /* Build a JSON array string for json_each, e.g. ["a","b"] */
      char prio_json[512] = "[]";


      if (priority_only)
        {
          /* Convert CSV to simple JSON array on the fly (best-effort, tiny buffer). */
          /* Expect small list; if bigger, plug a proper builder. */
          snprintf (prio_json,
                    sizeof (prio_json),
                    "[\"%s\"]",
                    cfg->priority_types_csv);                                                   /* commas acceptable for json_each */
          for (char *p = prio_json; *p; ++p)
            {
              if (*p == ',')
                {
                  *p = '"', memmove (p + 1, p, strlen (p) + 1), *p++ = ',';
                }
            }
          /* Result is not perfect JSON for spaces; acceptable if CSV is simple. */
        }
      while (remaining > 0)
        {
          db_res_t *st = NULL;
          db_error_t err;

          db_bind_t params[] = {
            db_bind_i64 (last_id),
            db_bind_i32 (priority_only ? 1 : 0),
            db_bind_text (prio_json),
            db_bind_i32 (remaining)
          };


          if (db_query (db, expanded_sql, params, 4, &st, &err) != true)
            {
              return err.code;
            }

          /* Wrap one pass in a transaction so the watermark is advanced atomically per pass. */
          db_tx_begin (db, DB_TX_IMMEDIATE, NULL);

          long long batch_max_id = last_id;
          long long batch_max_ts = last_ts;
          int processed_this_stmt = 0;


          while (db_res_step (st, &err))
            {
              long long ev_id = db_res_col_i64 (st, 0, &err);
              long long ev_ts = db_res_col_i64 (st, 1, &err);
              const char *tmp_ev_type = db_res_col_text (st, 2, &err);

              char *ev_type = tmp_ev_type ? strdup (tmp_ev_type) : NULL;


              /* Prioritisation second pass: skip priority types to avoid reprocessing. */
              if (!priority_only && cfg->priority_types_csv &&
                  csv_contains (cfg->priority_types_csv, ev_type))
                {
                  free (ev_type);
                  continue;     /* Leave for next tick; preserves per-pass order. */
                }

              int hrc = handle_event (ev_type, db, st);


              if (hrc != 0)
                {
                  /* Quarantine and continue (clear error path) */
                  (void) quarantine (db, st,
                                     "handler failed or unknown type");
                  out->quarantined++;
                  /* Advance past this poisoned event to avoid permanent block */
                }
              else
                {
                  out->processed++;
                }
              if (ev_id > batch_max_id)
                {
                  batch_max_id = ev_id;
                }
              if (ev_ts > batch_max_ts)
                {
                  batch_max_ts = ev_ts;
                }
              processed_this_stmt++;
              remaining--;
              free (ev_type);
              if (remaining == 0)
                {
                  break;
                }
            }
          db_res_finalize (st);
          /* If we fetched none, end this pass */
          if (processed_this_stmt == 0)
            {
              db_tx_rollback (db, NULL);
              break;
            }
          /* Persist watermark AFTER the pass */
          rc =
            save_watermark (db, cfg->consumer_key, batch_max_id,
                            batch_max_ts);
          if (rc != 0)
            {
              db_tx_rollback (db, NULL);
              return rc;
            }
          db_tx_commit (db, NULL);
          last_id = batch_max_id;
          last_ts = batch_max_ts;
          out->last_event_id = last_id;
          /* If we hit remaining==0, we leave the loop and return (bounded work). */
          if (remaining == 0)
            {
              break;
            }
        }
      /* fallthrough to next pass if any */
    }
  /* Recompute lag after work (optional) */
  rc = fetch_max_event_id (db, &max_id);
  if (rc != 0)
    {
      return rc;
    }
  out->lag =
    (max_id > out->last_event_id) ? (max_id - out->last_event_id) : 0;
  return 0;
}

