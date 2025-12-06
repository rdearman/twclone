#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <time.h>
#include <jansson.h>
#include <sqlite3.h>
#include "database.h"
#include "server_communication.h"       // (optional if you want to also emit immediately)
#include "engine_consumer.h"
#include "server_engine.h" // For h_player_progress_from_event_payload
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
load_watermark (sqlite3 *db, const char *key, long long *last_id,
                long long *last_ts)
{
  const char *sql =
    "SELECT last_event_id, last_event_ts FROM engine_offset WHERE key=?1;";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc)
    {
      return rc;
    }
  sqlite3_bind_text (st, 1, key, -1, SQLITE_STATIC);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      *last_id = sqlite3_column_int64 (st, 0);
      *last_ts = sqlite3_column_int64 (st, 1);
      rc = SQLITE_OK;
    }
  else
    {
      *last_id = 0;
      *last_ts = 0;
      rc = SQLITE_OK;
    }
  sqlite3_finalize (st);
  return rc;
}


static int
save_watermark (sqlite3 *db, const char *key, long long last_id,
                long long last_ts)
{
  const char *up =
    "INSERT INTO engine_offset(key,last_event_id,last_event_ts) "
    "VALUES(?1,?2,?3) "
    "ON CONFLICT(key) DO UPDATE SET last_event_id=excluded.last_event_id, last_event_ts=excluded.last_event_ts;";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, up, -1, &st, NULL);
  if (rc)
    {
      return rc;
    }
  sqlite3_bind_text (st, 1, key, -1, SQLITE_STATIC);
  sqlite3_bind_int64 (st, 2, last_id);
  sqlite3_bind_int64 (st, 3, last_ts);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? SQLITE_OK : rc;
}


static int
fetch_max_event_id (sqlite3 *db, long long *max_id)
{
  const char *sql = "SELECT COALESCE(MAX(id),0) FROM engine_events;";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc)
    {
      return rc;
    }
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      *max_id = sqlite3_column_int64 (st, 0);
      rc = SQLITE_OK;
    }
  sqlite3_finalize (st);
  return rc;
}


static int
quarantine (sqlite3 *db, sqlite3_stmt *row, const char *err)
{
  /* row columns: id, ts, type, payload */
  const char *sql =
    "INSERT INTO engine_events_deadletter(id,ts,type,payload,error,moved_at) "
    "VALUES(?1,?2,?3,?4,?5,?6) "
    "ON CONFLICT(id) DO UPDATE SET error=excluded.error, moved_at=excluded.moved_at;";
  sqlite3_stmt *ins = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &ins, NULL);
  if (rc)
    {
      return rc;
    }
  sqlite3_bind_int64 (ins, 1, sqlite3_column_int64 (row, 0));
  sqlite3_bind_int64 (ins, 2, sqlite3_column_int64 (row, 1));
  sqlite3_bind_text (ins, 3, (const char *) sqlite3_column_text (row, 2), -1,
                     SQLITE_TRANSIENT);
  sqlite3_bind_text (ins, 4, (const char *) sqlite3_column_text (row, 3), -1,
                     SQLITE_TRANSIENT);
  sqlite3_bind_text (ins, 5, err ? err : "unknown error", -1,
                     SQLITE_TRANSIENT);
  sqlite3_bind_int (ins, 6, get_now_epoch ());
  rc = sqlite3_step (ins);
  sqlite3_finalize (ins);
  return (rc == SQLITE_DONE) ? SQLITE_OK : rc;
}


/* Build the SELECT used this tick. Two-phase:
   1) If backlog >= threshold, first pass fetches only priority types, id ASC.
   2) Second pass (if room left) fetches the remaining types, id ASC.
   Each pass never breaks id ordering within itself; overall ordering is still strict
   per-pass; cross-pass “prioritisation” is documented. */
static const char *BASE_SELECT =
  "SELECT id, ts, type, payload "
  "FROM engine_events "
  "WHERE id > ?1 "
  "  AND (?2 = 0 OR type IN (SELECT trim(value) FROM json_each(?3))) "
  "ORDER BY id ASC " "LIMIT ?4;";


static int
handle_ship_self_destruct_initiated (sqlite3 *db, sqlite3_stmt *ev_row)
{
  int rc = SQLITE_ERROR;
  int player_id = sqlite3_column_int (ev_row, 3);       // actor_player_id
  int sector_id = sqlite3_column_int (ev_row, 4);       // sector_id
  const char *payload_str = (const char *) sqlite3_column_text (ev_row, 5);     // payload
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
    sqlite3_stmt *st_ship = NULL;
    const char *sql_get_ship_id = "SELECT ship FROM players WHERE id = ?;";


    if (sqlite3_prepare_v2 (db, sql_get_ship_id, -1, &st_ship, NULL) ==
        SQLITE_OK)
      {
        sqlite3_bind_int (st_ship, 1, player_id);
        if (sqlite3_step (st_ship) == SQLITE_ROW)
          {
            ship_id = sqlite3_column_int (st_ship, 0);
          }
        sqlite3_finalize (st_ship);
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
    sqlite3_stmt *st_name = NULL;
    const char *sql_get_ship_name = "SELECT name FROM ships WHERE id = ?;";


    if (sqlite3_prepare_v2 (db, sql_get_ship_name, -1, &st_name, NULL) ==
        SQLITE_OK)
      {
        sqlite3_bind_int (st_name, 1, ship_id);
        if (sqlite3_step (st_name) == SQLITE_ROW)
          {
            strncpy (ship_name,
                     (const char *) sqlite3_column_text (st_name, 0),
                     sizeof (ship_name) - 1);
          }
        sqlite3_finalize (st_name);
      }
  }


  // Perform ship destruction
  rc = db_destroy_ship (db, player_id, ship_id);
  if (rc != SQLITE_OK)
    {
      LOGE ("Error destroying ship %d for player %d: %s", ship_id, player_id,
            sqlite3_errmsg (db));
      json_decref (payload);
      return 1;                 // Quarantine
    }
  // Log ship.destroyed event
  json_t *destroyed_payload = json_pack ("{s:i, s:i, s:s}",
                                         "player_id", player_id,
                                         "ship_id", ship_id,
                                         "ship_name", ship_name);


  if (!destroyed_payload)
    {
      LOGE ("Error creating ship.destroyed payload.");
      json_decref (payload);
      return 1;                 // Quarantine
    }
  db_log_engine_event (get_now_epoch (), "ship.destroyed", "player",
                       player_id, sector_id, destroyed_payload, NULL);
  json_decref (destroyed_payload);
  json_decref (payload);
  return 0;                     // Success
}


static int
engine_event_handler_player_trade_v1 (sqlite3 *db, sqlite3_stmt *ev_row)
{
  int rc = SQLITE_ERROR;
  const char *payload_str = (const char *) sqlite3_column_text (ev_row, 3); // Payload is the 4th column (index 3)
  json_error_t jerr;
  json_t *payload = json_loads (payload_str, 0, &jerr);
  if (!payload)
    {
      LOGE (
        "engine_event_handler_player_trade_v1: Error parsing JSON payload: %s",
        jerr.text);
      return 1; // Quarantine
    }
  rc = h_player_progress_from_event_payload (payload);
  json_decref (payload);
  return rc;
}


int
handle_event (const char *type, sqlite3 *db, sqlite3_stmt *ev_row)
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
engine_consume_tick (sqlite3 *db,
                     const eng_consumer_cfg_t *cfg,
                     eng_consumer_metrics_t *out)
{
  memset (out, 0, sizeof (*out));
  long long last_id = 0, last_ts = 0, max_id = 0;
  int rc;


  /* Load watermark and lag */
  rc = load_watermark (db, cfg->consumer_key, &last_id, &last_ts);
  if (rc)
    {
      return rc;
    }
  rc = fetch_max_event_id (db, &max_id);
  if (rc)
    {
      return rc;
    }
  out->last_event_id = last_id;
  out->lag = (max_id > last_id) ? (max_id - last_id) : 0;
  int remaining = (cfg->batch_size > 0 ? cfg->batch_size : 100);
  int prio_phase = (cfg->priority_types_csv && *cfg->priority_types_csv &&
                    out->lag >= (cfg->backlog_prio_threshold >
                                 0 ? cfg->backlog_prio_threshold : 0));


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
          sqlite3_stmt *st = NULL;


          rc = sqlite3_prepare_v2 (db, BASE_SELECT, -1, &st, NULL);
          if (rc)
            {
              return rc;
            }
          sqlite3_bind_int64 (st, 1, last_id);
          sqlite3_bind_int (st, 2, priority_only ? 1 : 0);
          sqlite3_bind_text (st, 3, prio_json, -1, SQLITE_TRANSIENT);
          sqlite3_bind_int (st, 4, remaining);
          /* Wrap one pass in a transaction so the watermark is advanced atomically per pass. */
          sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
          long long batch_max_id = last_id;
          long long batch_max_ts = last_ts;
          int processed_this_stmt = 0;


          while ((rc = sqlite3_step (st)) == SQLITE_ROW)
            {
              long long ev_id = sqlite3_column_int64 (st, 0);
              long long ev_ts = sqlite3_column_int64 (st, 1);
              const char *ev_type =
                (const char *) sqlite3_column_text (st, 2);


              /* Prioritisation second pass: skip priority types to avoid reprocessing. */
              if (!priority_only && cfg->priority_types_csv &&
                  csv_contains (cfg->priority_types_csv, ev_type))
                {
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
              if (remaining == 0)
                {
                  break;
                }
            }
          sqlite3_finalize (st);
          /* If we fetched none, end this pass */
          if (processed_this_stmt == 0)
            {
              sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
              break;
            }
          /* Persist watermark AFTER the pass */
          rc =
            save_watermark (db, cfg->consumer_key, batch_max_id,
                            batch_max_ts);
          if (rc)
            {
              sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
              return rc;
            }
          sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);
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
  if (rc)
    {
      return rc;
    }
  out->lag =
    (max_id > out->last_event_id) ? (max_id - out->last_event_id) : 0;
  return SQLITE_OK;
}

