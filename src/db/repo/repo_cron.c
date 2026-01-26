#define TW_DB_INTERNAL 1
#include "db_int.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <jansson.h>
#include <stdbool.h>
#include "globals.h"
#include "repo_cron.h"
#include "db/db_api.h"
#include "db/sql_driver.h"

/* ==================================================================== */
/* CRON HELPERS (Migrated from server_cron.c)                           */
/* ==================================================================== */

int
db_cron_get_ship_info (db_t *db, int ship_id, int *sector_id, int *owner_id)
{
  if (!db || !sector_id || !owner_id) return -1;
  char sql[512];
  if (sql_build(db, "SELECT T1.sector_id, T2.player_id FROM ships T1 LEFT JOIN players T2 ON T1.ship_id = T2.ship_id WHERE T1.ship_id = {1};", sql, sizeof(sql)) != 0) return -1;

  db_res_t *res = NULL;
  db_error_t err;
  db_error_clear (&err);

  if (db_query (db, sql, (db_bind_t[]){ db_bind_i32 (ship_id) }, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          *sector_id = (int) db_res_col_i32 (res, 0, &err);
          *owner_id = (int) db_res_col_i32 (res, 1, &err);
          db_res_finalize (res);
          return 0;
        }
      db_res_finalize (res);
    }
  return -1;
}

int
db_cron_tow_ship (db_t *db, int ship_id, int new_sector_id, int owner_id)
{
  if (!db) return -1;
  db_error_t err;
  db_error_clear (&err);

  char sql_ship[512];
  if (sql_build(db, "UPDATE ships SET sector_id = {1} WHERE ship_id = {2};", sql_ship, sizeof(sql_ship)) != 0) return -1;
  
  if (!db_exec (db, sql_ship, (db_bind_t[]){ db_bind_i32 (new_sector_id), db_bind_i32 (ship_id) }, 2, &err))
    return -1;

  if (owner_id > 0)
    {
      char sql_player[512];
      if (sql_build(db, "UPDATE players SET sector_id = {1} WHERE player_id = {2};", sql_player, sizeof(sql_player)) != 0) return -1;
      
      if (!db_exec (db, sql_player, (db_bind_t[]){ db_bind_i32 (new_sector_id), db_bind_i32 (owner_id) }, 2, &err))
        return -1;
    }
  return 0;
}

int
db_cron_get_max_sector_id (db_t *db, int *max_id)
{
  if (!db || !max_id) return -1;
  db_res_t *res = NULL;
  db_error_t err;
  db_error_clear (&err);
  *max_id = 0;

  char sql[256];
  sql_build(db, "SELECT MAX(sector_id) FROM sectors;", sql, sizeof(sql));
  if (db_query (db, sql, NULL, 0, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          *max_id = (int) db_res_col_i64 (res, 0, &err);
        }
      db_res_finalize (res);
      return 0;
    }
  return -1;
}

int
db_cron_get_sector_warps (db_t *db, int sector_id, int **out_neighbors, int *out_count)
{
  if (!db || !out_neighbors || !out_count) return -1;
  db_res_t *res = NULL;
  db_error_t err;
  db_error_clear (&err);
  
  char sql[512];
  if (sql_build(db, "SELECT to_sector FROM sector_warps WHERE from_sector = {1};", sql, sizeof(sql)) != 0) return -1;

  if (!db_query (db, sql, (db_bind_t[]){ db_bind_i32 (sector_id) }, 1, &res, &err))
    return -1;

  int cap = 16;
  int count = 0;
  int *arr = malloc (cap * sizeof(int));
  if (!arr) { db_res_finalize(res); return -1; }

  while (db_res_step (res, &err))
    {
      if (count >= cap)
        {
          cap *= 2;
          int *tmp = realloc (arr, cap * sizeof(int));
          if (!tmp) { free(arr); db_res_finalize(res); return -1; }
          arr = tmp;
        }
      arr[count++] = (int) db_res_col_i32 (res, 0, &err);
    }
  db_res_finalize (res);
  *out_neighbors = arr;
  *out_count = count;
  return 0;
}

int
db_cron_msl_count (db_t *db, int *count)
{
  if (!db || !count) return -1;
  db_res_t *res = NULL;
  db_error_t err;
  db_error_clear (&err);
  *count = 0;

  char sql[256];
  snprintf(sql, sizeof(sql), "SELECT COUNT(sector_id) FROM msl_sectors;");

  if (db_query (db, sql, NULL, 0, &res, &err))
    {
      if (db_res_step (res, &err))
        *count = (int) db_res_col_i32 (res, 0, &err);
      db_res_finalize (res);
      return 0;
    }
  return -1;
}

int
db_cron_create_msl_table (db_t *db)
{
  if (!db) return -1;
  db_error_t err;
  db_error_clear (&err);
  char sql[256];
  snprintf(sql, sizeof(sql), "CREATE TABLE IF NOT EXISTS msl_sectors (sector_id INTEGER PRIMARY KEY);");
  if (!db_exec (db, sql, NULL, 0, &err)) return -1;
  return 0;
}

int
db_cron_get_stardock_locations (db_t *db, int **out_sectors, int *out_count)
{
  if (!db || !out_sectors || !out_count) return -1;
  db_res_t *res = NULL;
  db_error_t err;
  db_error_clear (&err);
  
  if (!db_query (db, "SELECT sector_id FROM stardock_location;", NULL, 0, &res, &err))
    return -1;

  int cap = 8;
  int count = 0;
  int *arr = malloc (cap * sizeof(int));
  if (!arr) { db_res_finalize(res); return -1; }

  while (db_res_step (res, &err))
    {
      if (count >= cap)
        {
          cap *= 2;
          int *tmp = realloc (arr, cap * sizeof(int));
          if (!tmp) { free(arr); db_res_finalize(res); return -1; }
          arr = tmp;
        }
      arr[count++] = (int) db_res_col_i32 (res, 0, &err);
    }
  db_res_finalize (res);
  *out_sectors = arr;
  *out_count = count;
  return 0;
}

int
db_cron_msl_insert (db_t *db, int sector_id, int *added_count_ptr)
{
  if (!db) return -1;
  db_error_t err;
  db_error_clear (&err);
  
  const char *ignore = sql_insert_ignore_clause(db);
  if (!ignore) ignore = ""; // Fallback for safety, though unsupported backends will fail gracefully

  char sql_tmpl[512];
  snprintf(sql_tmpl, sizeof(sql_tmpl), "INSERT INTO msl_sectors (sector_id) VALUES ({1}) %s;", ignore);

  char sql[512];
  sql_build(db, sql_tmpl, sql, sizeof(sql));

  if (db_exec (db, sql, (db_bind_t[]){ db_bind_i32 (sector_id) }, 1, &err))
    {
      if (added_count_ptr) (*added_count_ptr)++;
      return 0;
    }
  
  /* If it already exists, that's fine (equivalent to DO NOTHING) */
  if (err.code == ERR_DB_CONSTRAINT)
    {
      return 0;
    }

  return -1;
}

int

db_cron_reset_turns_for_all_players (db_t *db, int max_turns, int64_t now_s, int *updated_count)

{

  if (!db) return -1;

  db_error_t err;

  db_error_clear (&err);

  

  /* Select players */

  db_res_t *res = NULL;

  if (!db_query (db, "SELECT player_id FROM turns;", NULL, 0, &res, &err)) return -1;

  

  int *pids = NULL;

  size_t count = 0;

  size_t cap = 64;

  pids = malloc(cap * sizeof(int));

  if (!pids) { db_res_finalize(res); return -1; }



  while (db_res_step (res, &err))

    {

      if (count >= cap) { cap *= 2; pids = realloc(pids, cap * sizeof(int)); }

      pids[count++] = (int) db_res_col_i32 (res, 0, &err);

    }

  db_res_finalize(res);



  if (!db_tx_begin (db, DB_TX_DEFAULT, &err)) { free(pids); return -1; }

  

  char sql_update[256];

  if (sql_build(db, "UPDATE turns SET turns_remaining = {1}, last_update = {2} WHERE player_id = {3}", sql_update, sizeof(sql_update)) != 0)

    {

      free(pids);

      return -1;

    }



  int success_count = 0;

  for (size_t i = 0; i < count; i++)

    {

      if (db_exec (db, sql_update, (db_bind_t[]){ db_bind_i32 (max_turns), db_bind_timestamp_text (now_s), db_bind_i32 (pids[i]) }, 3, &err))

        success_count++;

    }

  free(pids);

  

  if (!db_tx_commit (db, &err)) return -1;

  

  if (updated_count) *updated_count = success_count;

  return 0;

}



int

db_cron_try_lock (db_t *db, const char *name, int64_t now_s)

{

  if (!db || !name) return 0;

  db_error_t err;

  db_error_clear (&err);

  int64_t now_ms = now_s * 1000;

  const int LOCK_DURATION_S = 60;

  int64_t until_ms = now_ms + (LOCK_DURATION_S * 1000);



  /* 1. Cleanup expired locks */

  char sql_del[512];

  sql_build(db, "DELETE FROM locks WHERE lock_name={1} AND until_ms < {2};", sql_del, sizeof(sql_del));

  db_exec (db, sql_del, (db_bind_t[]){ db_bind_text (name), db_bind_i64 (now_ms) }, 2, &err);



  /* 2. Check if lock is currently held by someone else */

  char sql_check[512];

  sql_build(db, "SELECT owner, until_ms FROM locks WHERE lock_name={1};", sql_check, sizeof(sql_check));

  db_res_t *res = NULL;

  bool held_by_other = false;

  if (db_query (db, sql_check, (db_bind_t[]){ db_bind_text (name) }, 1, &res, &err)) {

      if (db_res_step (res, &err)) {

          const char *o = db_res_col_text (res, 0, &err);

          int64_t u = db_res_col_i64 (res, 1, &err);

          if (o && strcmp(o, g_engine_uuid) != 0 && u >= now_ms) {

              held_by_other = true;

          }

      }

      db_res_finalize (res);

  }

  if (held_by_other) return 0;



  /* 3. Attempt to acquire/refresh lock */

  char sql_ins[512];

  sql_build(db, "INSERT INTO locks(lock_name, owner, until_ms) VALUES({1}, {2}, {3});", sql_ins, sizeof(sql_ins));

  db_bind_t ins_params[] = { db_bind_text (name), db_bind_text(g_engine_uuid), db_bind_i64 (until_ms) };

  if (!db_exec (db, sql_ins, ins_params, 3, &err)) {

      if (err.code == ERR_DB_CONSTRAINT) {

          /* Double check if we own it (concurrent write) */

          sql_build(db, "UPDATE locks SET until_ms = {3} WHERE lock_name={1} AND owner={2};", sql_ins, sizeof(sql_ins));

          int64_t rows = 0;

          if (db_exec_rows_affected(db, sql_ins, ins_params, 3, &rows, &err) && rows > 0) return 1;

          return 0;

      }

      return 0;

  }



  return 1;

}



int64_t

db_cron_get_lock_until (db_t *db, const char *name)

{

  if (!db || !name) return 0;

  char SQL[512];

  sql_build(db, "SELECT until_ms FROM locks WHERE lock_name = {1};", SQL, sizeof(SQL));

  db_res_t *res = NULL;

  db_error_t err;

  db_error_clear (&err);

  int64_t until_ms = 0;

  if (db_query (db, SQL, (db_bind_t[]){ db_bind_text (name) }, 1, &res, &err))

    {

      if (db_res_step (res, &err)) until_ms = db_res_col_i64 (res, 0, &err);

      db_res_finalize (res);

    }

  return until_ms;

}



int

db_cron_unlock (db_t *db, const char *name)
{
  if (!db || !name) return -1;
  db_error_t err;
  db_error_clear (&err);
  char SQL[512];
  sql_build(db, "DELETE FROM locks WHERE lock_name={1} AND owner={2};", SQL, sizeof(SQL));
  db_exec (db, SQL, (db_bind_t[]){ db_bind_text (name), db_bind_text(g_engine_uuid) }, 2, &err);
  return 0;
}



int

db_cron_uncloak_fedspace_ships (db_t *db)

{

  if (!db) return -1;

  db_error_t err;

  db_error_clear (&err);

  const char *sql = "UPDATE ships SET cloaked=NULL WHERE cloaked IS NOT NULL AND (sector_id IN (SELECT sector_id FROM stardock_location) OR sector_id IN (1, 2, 3, 4, 5, 6, 7, 8, 9, 10));";

  if (!db_exec (db, sql, NULL, 0, &err)) return -1;

  return 0;

}



int

db_cron_get_illegal_assets_json (db_t *db, json_t **out_array)

{

  if (!db || !out_array) return -1;

  *out_array = json_array();

  db_res_t *res = NULL;

  db_error_t err;

  db_error_clear (&err);

  const char *sql = "SELECT owner_id, asset_type, sector_id, quantity FROM sector_assets WHERE sector_id IN (SELECT sector_id FROM msl_sectors) AND owner_id != 0;";

  

  if (db_query (db, sql, NULL, 0, &res, &err))

    {

      while (db_res_step (res, &err))

        {

          json_t *obj = json_object();

          json_object_set_new(obj, "player_id", json_integer(db_res_col_i32(res, 0, &err)));

          json_object_set_new(obj, "asset_type", json_integer(db_res_col_i32(res, 1, &err)));

          json_object_set_new(obj, "sector_id", json_integer(db_res_col_i32(res, 2, &err)));

          json_object_set_new(obj, "quantity", json_integer(db_res_col_i32(res, 3, &err)));

          json_array_append_new(*out_array, obj);

        }

      db_res_finalize (res);

      return 0;

    }

  return -1;

}



int

db_cron_delete_sector_asset (db_t *db, int player_id, int asset_type, int sector_id, int quantity)

{

  if (!db) return -1;

  db_error_t err;

  db_error_clear (&err);

  char sql[512];

  if (sql_build(db, "DELETE FROM sector_assets WHERE owner_id = {1} AND asset_type = {2} AND sector_id = {3} AND quantity = {4};", sql, sizeof(sql)) != 0) return -1;

  if (!db_exec (db, sql, (db_bind_t[]){ db_bind_i32 (player_id), db_bind_i32 (asset_type), db_bind_i32 (sector_id), db_bind_i32 (quantity) }, 4, &err)) return -1;

  return 0;

}



int

db_cron_logout_inactive_players (db_t *db, int64_t cutoff_s)

{

  if (!db) return -1;

  db_error_t err;

  db_error_clear (&err);

  

  char sql[320];

  if (sql_build(db, "UPDATE players SET loggedin = {1} WHERE loggedin != {2} AND last_update < {3};", sql, sizeof(sql)) != 0) return -1;

  

  if (!db_exec (db, sql, (db_bind_t[]){ db_bind_timestamp_text (0), db_bind_timestamp_text (0), db_bind_timestamp_text (cutoff_s) }, 3, &err)) return -1;

  return 0;

}



int

db_cron_init_eligible_tows (db_t *db, int start_sec, int end_sec, int64_t stale_cutoff)

{

  if (!db) return -1;

  db_error_t err;

  db_error_clear (&err);

  

  db_exec (db, "CREATE TABLE IF NOT EXISTS eligible_tows (ship_id INTEGER PRIMARY KEY, sector_id INTEGER, owner_id INTEGER, fighters INTEGER, alignment INTEGER, experience INTEGER);", NULL, 0, &err);

  db_exec (db, "DELETE FROM eligible_tows", NULL, 0, &err);



  char sql[512];

  if (sql_build(db, "INSERT INTO eligible_tows (ship_id, sector_id, owner_id, fighters, alignment, experience) "

    "SELECT T1.ship_id, T1.sector_id, T2.player_id, T1.fighters, COALESCE(T2.alignment, 0), COALESCE(T2.experience, 0) "

    "FROM ships T1 LEFT JOIN players T2 ON T1.ship_id = T2.ship_id "

    "WHERE T1.sector_id BETWEEN {1} AND {2} AND (T2.player_id IS NULL OR COALESCE(T2.login_time, {3}) < {4}) "

    "ORDER BY T1.ship_id ASC", sql, sizeof(sql)) != 0) return -1;



  if (!db_exec (db, sql, (db_bind_t[]){ db_bind_i32 (start_sec), db_bind_i32 (end_sec), db_bind_timestamp_text (0), db_bind_timestamp_text (stale_cutoff) }, 4, &err)) return -1;

  return 0;

}



int

db_cron_get_eligible_tows_json (db_t *db, const char *reason, int limit, json_t **out_array)

{

  if (!db || !out_array) return -1;

  *out_array = json_array();

  db_res_t *res = NULL;

  db_error_t err;

  db_error_clear (&err);

  

  char sql[512];

  db_bind_t params[2];

  int param_count = 1;



    if (strcmp(reason, "evil") == 0) {



      sql_build(db, "SELECT ship_id FROM eligible_tows WHERE owner_id IS NOT NULL AND alignment < 0 LIMIT {1};", sql, sizeof(sql));



      params[0] = db_bind_i32(limit);



    } else if (strcmp(reason, "fighters") == 0) {



      sql_build(db, "SELECT ship_id FROM eligible_tows WHERE fighters > {1} LIMIT {2};", sql, sizeof(sql));



      params[0] = db_bind_i32(49);



      params[1] = db_bind_i32(limit);



      param_count = 2;



    } else if (strcmp(reason, "exp") == 0) {



      sql_build(db, "SELECT ship_id FROM eligible_tows WHERE owner_id IS NOT NULL AND experience >= 1000 LIMIT {1};", sql, sizeof(sql));



      params[0] = db_bind_i32(limit);



    } else if (strcmp(reason, "no_owner") == 0) {



      sql_build(db, "SELECT ship_id FROM eligible_tows WHERE owner_id IS NULL LIMIT {1};", sql, sizeof(sql));



      params[0] = db_bind_i32(limit);



    } else {

    return -1;

  }



  if (db_query (db, sql, params, param_count, &res, &err))

    {

      while (db_res_step (res, &err))

        {

          json_array_append_new(*out_array, json_integer(db_res_col_i32(res, 0, &err)));

        }

      db_res_finalize (res);

      return 0;

    }

  return -1;

}



int

db_cron_clear_eligible_tows (db_t *db)

{

  if (!db) return -1;

  db_error_t err;

  db_error_clear (&err);

  if (!db_exec (db, "DELETE FROM eligible_tows", NULL, 0, &err)) return -1;

  return 0;

}



int

db_cron_robbery_decay_suspicion (db_t *db)

{

  if (!db) return -1;

  db_error_t err;

  db_error_clear (&err);

  if (!db_exec (db, "UPDATE cluster_player_status SET suspicion = CAST(suspicion * 0.9 AS INTEGER) WHERE suspicion > 0;", NULL, 0, &err)) return -1;

  return 0;

}



int

db_cron_robbery_clear_busts (db_t *db, int64_t now_s)

{

  if (!db) return -1;

  db_error_t err;

  db_error_clear (&err);



  int ttl_days = 0;

  db_res_t *res = NULL;

  if (db_query(db, "SELECT robbery_real_bust_ttl_days FROM law_enforcement WHERE law_enforcement_id=1", NULL, 0, &res, &err)) {

      if (db_res_step(res, &err)) {

          ttl_days = db_res_col_i32(res, 0, &err);

      }

      db_res_finalize(res);

  } else {

      return -1;

  }



  int64_t cutoff_s = now_s - (ttl_days * 86400);



    char sql[512];



    if (sql_build(db, "UPDATE port_busts SET active = FALSE WHERE active = TRUE AND ( (bust_type = 'fake') OR (bust_type = 'real' AND last_bust_at < {1}) );", sql, sizeof(sql)) != 0) return -1;



    if (!db_exec (db, sql, (db_bind_t[]){ db_bind_timestamp_text (cutoff_s) }, 1, &err)) return -1;

  return 0;

}



int

db_cron_reset_daily_turns (db_t *db, int turns)





{

  (void) turns;





  if (!db) return -1;



  db_error_t err;

  db_error_clear (&err);

  // Using explicit value instead of subselect for safety/simplicity

  char sql[256];

  if (sql_build(db, "UPDATE turns SET turns_remaining = CAST((SELECT value FROM config WHERE key = 'turnsperday') AS INTEGER);", sql, sizeof(sql)) != 0) return -1;

  if (!db_exec (db, sql, NULL, 0, &err)) return -1;

  return 0;

}



int

db_cron_autouncloak_ships (db_t *db, int64_t uncloak_threshold_s)

{

  if (!db) return -1;

  db_error_t err;

  db_error_clear (&err);

  char cloaked_epoch[256];

  if (sql_ts_to_epoch_expr(db, "cloaked", cloaked_epoch, sizeof(cloaked_epoch)) != 0) return -1;

  char sql_tmpl[512], sql[512];

  snprintf(sql_tmpl, sizeof(sql_tmpl), "UPDATE ships SET cloaked = NULL WHERE cloaked IS NOT NULL AND %s < {1};", cloaked_epoch);

  if (sql_build(db, sql_tmpl, sql, sizeof(sql)) != 0) return -1;

  if (!db_exec (db, sql, (db_bind_t[]){ db_bind_i64 (uncloak_threshold_s) }, 1, &err)) return -1;

  return 0;

}



int

db_cron_terra_replenish (db_t *db)

{

  if (!db) return -1;

  db_error_t err;

  db_error_clear (&err);

  if (!db_exec (db, "UPDATE planet_goods SET quantity = max_capacity WHERE planet_id = 1;", NULL, 0, &err)) return -1;

  if (!db_exec (db, "UPDATE planets SET terraform_turns_left = 1 WHERE owner_id > 0;", NULL, 0, &err)) return -1;

  return 0;

}



int

db_cron_planet_pop_growth_tick (db_t *db, double growth_rate)

{

  if (!db) return -1;

  db_error_t err;

  db_error_clear (&err);

  db_res_t *res = NULL;

  

  const char *sql =

    "SELECT p.planet_id, p.population, "

    "       COALESCE(pt.maxColonist_ore, 0) + COALESCE(pt.maxColonist_organics, 0) + COALESCE(pt.maxColonist_equipment, 0) AS max_pop "

    "FROM planets p "

    "JOIN planettypes pt ON p.type = pt.planettypes_id "

    "WHERE p.owner_id > 0 AND p.population > 0;";



  if (!db_query (db, sql, NULL, 0, &res, &err)) return -1;



  char sql_update[512];

  sql_build(db, "UPDATE planets SET population = {1} WHERE planet_id = {2};", sql_update, sizeof(sql_update));



  while (db_res_step (res, &err))

    {

      int planet_id = (int) db_res_col_i32 (res, 0, &err);

      int current_pop = (int) db_res_col_i32 (res, 1, &err);

      int max_pop = (int) db_res_col_i32 (res, 2, &err);

      if (max_pop <= 0) max_pop = 10000;

      

      if (current_pop < max_pop)

        {

          double delta = (double)current_pop * growth_rate * (1.0 - (double)current_pop / (double)max_pop);

          int delta_int = (int)delta;

          if (delta_int < 1 && current_pop < max_pop) delta_int = 1;

          int new_pop = current_pop + delta_int;

          if (new_pop > max_pop) new_pop = max_pop;



          db_exec (db, sql_update, (db_bind_t[]){ db_bind_i64 (new_pop), db_bind_i64 (planet_id) }, 2, &err);

        }

    }

  db_res_finalize (res);

  return 0;

}



int

db_cron_citadel_treasury_tick (db_t *db, int rate_bps)

{

  if (!db) return -1;

  db_error_t err;

  db_error_clear (&err);

  db_res_t *res = NULL;



  const char *sql = "SELECT citadel_id, treasury FROM citadels WHERE level >= 1 AND treasury > 0;";

  if (!db_query (db, sql, NULL, 0, &res, &err)) return -1;



  char sql_update[512];

  sql_build(db, "UPDATE citadels SET treasury = treasury + ( (treasury * {1}) / 10000 ) WHERE citadel_id = {2};", sql_update, sizeof(sql_update));



  while (db_res_step (res, &err))

    {

      int citadel_id = (int) db_res_col_i32 (res, 0, &err);

      long long current_treasury = db_res_col_i64 (res, 1, &err);

      long long delta = (current_treasury * rate_bps) / 10000;



      if (delta > 0)

        {

          db_exec (db, sql_update, (db_bind_t[]){ db_bind_i64 (rate_bps), db_bind_i64 (citadel_id) }, 2, &err);

        }

    }

  db_res_finalize (res);

  return 0;

}



int

db_cron_planet_update_production_stock (db_t *db, int64_t now_s)

{

  if (!db) return -1;

  db_error_t err;

  db_error_clear (&err);

  char sql_update_commodities[2048];

  

  const char *sql_template = 

    "INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price, last_updated_ts) "

    "SELECT 'planet', p.planet_id, pp.commodity_code, "

    "GREATEST(0, LEAST(CASE pp.commodity_code "

    "  WHEN 'ORE' THEN pltype.maxore "

    "  WHEN 'ORG' THEN pltype.maxorganics "

    "  WHEN 'EQU' THEN pltype.maxequipment "

    "  ELSE 999999 END, "

    "COALESCE(es.quantity, 0) + pp.base_prod_rate + "

    "(CASE pp.commodity_code "

    "  WHEN 'ORE' THEN p.colonists_ore * 1 "

    "  WHEN 'ORG' THEN p.colonists_org * pltype.organicsProduction "

    "  WHEN 'EQU' THEN p.colonists_eq * pltype.equipmentProduction "

    "  WHEN 'FUE' THEN p.colonists_unassigned * pltype.fuelProduction "

    "  ELSE p.colonists_unassigned * 1 END) - pp.base_cons_rate)) AS new_quantity, 0, {1} "

    "FROM planets p "

    "JOIN planet_production pp ON p.type = pp.planet_type_id "

    "LEFT JOIN entity_stock es ON es.entity_type = 'planet' AND es.entity_id = p.planet_id AND es.commodity_code = pp.commodity_code "

    "LEFT JOIN planettypes pltype ON p.type = pltype.planettypes_id "

    "WHERE (pp.base_prod_rate > 0 OR pp.base_cons_rate > 0 OR p.colonists_ore > 0 OR p.colonists_org > 0 OR p.colonists_eq > 0 OR p.colonists_unassigned > 0)";

  

  if (sql_build(db, sql_template, sql_update_commodities, sizeof(sql_update_commodities)) != 0) return -1;

  if (!db_exec (db, sql_update_commodities, (db_bind_t[]){ db_bind_i64 (now_s) }, 1, &err)) return -1;

  return 0;

}



int

db_cron_planet_get_market_data_json (db_t *db, json_t **out_array)

{

  if (!db || !out_array) return -1;

  *out_array = json_array();

  db_res_t *res = NULL;

  db_error_t err;

  db_error_clear (&err);



  const char *sql =

    "SELECT p.planet_id, "

    "       pt.maxore, pt.maxorganics, pt.maxequipment, "

    "       pp.commodity_code, "

    "       c.commodities_id, "

    "       es.quantity AS current_quantity, "

    "       pp.base_prod_rate, pp.base_cons_rate, "

    "       c.base_price, "

    "       c.illegal, "

    "       p.owner_id, "

    "       p.owner_type "

    "FROM planets p "

    "JOIN planettypes pt ON p.type = pt.planettypes_id "

    "JOIN planet_production pp ON p.type = pp.planet_type_id "

    "LEFT JOIN entity_stock es ON es.entity_type = 'planet' AND es.entity_id = p.planet_id AND es.commodity_code = pp.commodity_code "

        "JOIN commodities c ON pp.commodity_code = c.code "

        "WHERE (pp.base_prod_rate > 0 OR pp.base_cons_rate > 0) AND c.illegal = FALSE;";



  if (db_query (db, sql, NULL, 0, &res, &err))

    {

      while (db_res_step (res, &err))

        {

          json_t *obj = json_object();

          json_object_set_new(obj, "planet_id", json_integer(db_res_col_i32(res, 0, &err)));

          json_object_set_new(obj, "maxore", json_integer(db_res_col_i32(res, 1, &err)));

          json_object_set_new(obj, "maxorganics", json_integer(db_res_col_i32(res, 2, &err)));

          json_object_set_new(obj, "maxequipment", json_integer(db_res_col_i32(res, 3, &err)));

          json_object_set_new(obj, "commodity_code", json_string(db_res_col_text(res, 4, &err)));

          json_object_set_new(obj, "commodity_id", json_integer(db_res_col_i32(res, 5, &err)));

          json_object_set_new(obj, "current_quantity", json_integer(db_res_col_i32(res, 6, &err)));

          json_object_set_new(obj, "base_prod_rate", json_integer(db_res_col_i32(res, 7, &err)));

          json_object_set_new(obj, "base_cons_rate", json_integer(db_res_col_i32(res, 8, &err)));

          json_object_set_new(obj, "base_price", json_integer(db_res_col_i32(res, 9, &err)));

          json_object_set_new(obj, "owner_id", json_integer(db_res_col_i32(res, 11, &err)));

          const char *ot = db_res_col_text(res, 12, &err);

          if (ot) json_object_set_new(obj, "owner_type", json_string(ot));

          

          json_array_append_new(*out_array, obj);

        }

      db_res_finalize (res);

      return 0;

    }

  return -1;

}



int



db_cron_broadcast_cleanup (db_t *db, int64_t now_s)



{



  if (!db) return -1;



  db_error_t err;



  db_error_clear (&err);







  const char *ts_fmt = sql_epoch_param_to_timestamptz(db);



  if (!ts_fmt) return -1;







  char ts_expr[64];



  snprintf(ts_expr, sizeof(ts_expr), ts_fmt, "{1}");







  char sql_tmpl[512];



  snprintf(sql_tmpl, sizeof(sql_tmpl), "DELETE FROM system_notice WHERE expires_at IS NOT NULL AND expires_at <= %s;", ts_expr);







  char sql[512];



  if (sql_build(db, sql_tmpl, sql, sizeof(sql)) != 0) return -1;



  



  if (!db_exec (db, sql, (db_bind_t[]){ db_bind_i64 (now_s) }, 1, &err)) return -1;



  return 0;



}



int

db_cron_traps_process (db_t *db, int64_t now_s)

{

  if (!db) return -1;

  db_error_t err;

  db_error_clear (&err);

  

  const char *now_expr = sql_now_expr(db);

  if (!now_expr) return -1;

  const char *json_obj_fn = sql_json_object_fn(db);

  if (!json_obj_fn) return -1;



  char trigger_at_epoch[256];

  if (sql_ts_to_epoch_expr(db, "trigger_at", trigger_at_epoch, sizeof(trigger_at_epoch)) != 0) return -1;



  char sql_insert_tmpl[512], sql_insert[512];

    snprintf(sql_insert_tmpl, sizeof(sql_insert_tmpl),

      "INSERT INTO engine_commands(type, payload, created_at, due_at) "

      "SELECT 'trap.trigger', %s('trap_id',id), %s, %s "

      "FROM traps WHERE armed=TRUE AND trigger_at IS NOT NULL AND %s <= {1};",

      json_obj_fn, now_expr, now_expr, trigger_at_epoch);

  if (sql_build(db, sql_insert_tmpl, sql_insert, sizeof(sql_insert)) != 0) return -1;

  

  if (!db_exec (db, sql_insert, (db_bind_t[]){ db_bind_i64 (now_s) }, 1, &err)) return -1;



  char sql_delete_tmpl[512], sql_delete[512];

  snprintf(sql_delete_tmpl, sizeof(sql_delete_tmpl),

    "DELETE FROM traps WHERE armed=TRUE AND trigger_at IS NOT NULL AND %s <= {1};",

    trigger_at_epoch);

  if (sql_build(db, sql_delete_tmpl, sql_delete, sizeof(sql_delete)) != 0) return -1;



  if (!db_exec (db, sql_delete, (db_bind_t[]){ db_bind_i64 (now_s) }, 1, &err)) return -1;

  return 0;

}



int

db_cron_port_get_economy_data_json (db_t *db, json_t **out_array)

{

  if (!db || !out_array) return -1;

  *out_array = json_array();

  db_res_t *res = NULL;

  db_error_t err;

  db_error_clear (&err);



  const char *sql =

    "SELECT p.port_id AS port_id, "

    "       p.size AS port_size, "

    "       p.type AS port_type, "

    "       es.commodity_code, "

    "       es.quantity AS current_quantity, "

    "       ec.base_restock_rate, "

    "       ec.target_stock, "

    "       c.commodities_id AS commodity_id "

    "FROM ports p "

    "JOIN entity_stock es ON p.port_id = es.entity_id AND es.entity_type = 'port' "

    "JOIN economy_curve ec ON p.economy_curve_id = ec.economy_curve_id "

    "JOIN commodities c ON es.commodity_code = c.code;";



  if (db_query (db, sql, NULL, 0, &res, &err))

    {

      while (db_res_step (res, &err))

        {

          json_t *obj = json_object();

          json_object_set_new(obj, "port_id", json_integer(db_res_col_i32(res, 0, &err)));

          json_object_set_new(obj, "port_size", json_integer(db_res_col_i32(res, 1, &err)));

          json_object_set_new(obj, "port_type", json_integer(db_res_col_i32(res, 2, &err)));

          json_object_set_new(obj, "commodity_code", json_string(db_res_col_text(res, 3, &err)));

          json_object_set_new(obj, "current_quantity", json_integer(db_res_col_i32(res, 4, &err)));

                    json_object_set_new(obj, "base_restock_rate", json_real(db_res_col_double(res, 5, &err)));

                    json_object_set_new(obj, "target_stock", json_integer(db_res_col_i32(res, 6, &err)));

                    json_object_set_new(obj, "commodity_id", json_integer(db_res_col_i32(res, 7, &err)));

          json_array_append_new(*out_array, obj);

        }

      db_res_finalize (res);

      return 0;

    }

  return -1;

}



int

db_cron_get_all_commodities_json (db_t *db, json_t **out_array)

{

  if (!db || !out_array) return -1;

  *out_array = json_array();

  db_res_t *res = NULL;

  db_error_t err;

  db_error_clear (&err);



  if (db_query (db, "SELECT commodities_id, code FROM commodities;", NULL, 0, &res, &err))

    {

      while (db_res_step (res, &err))

        {

          json_t *obj = json_object();

          json_object_set_new(obj, "id", json_integer(db_res_col_i32(res, 0, &err)));

          json_object_set_new(obj, "code", json_string(db_res_col_text(res, 1, &err)));

          json_array_append_new(*out_array, obj);

        }

      db_res_finalize (res);

      return 0;

    }

  return -1;

}



int

db_cron_expire_market_orders (db_t *db, int64_t now_s)

{

  if (!db) return -1;

  db_error_t err;

  db_error_clear (&err);

  char sql[320];

  if (sql_build(db, "UPDATE commodity_orders SET status='expired' WHERE status='open' AND expires_at IS NOT NULL AND expires_at < {1};", sql, sizeof(sql)) != 0) return -1;

  if (!db_exec (db, sql, (db_bind_t[]){ db_bind_timestamp_text (now_s) }, 1, &err)) return -1;

  return 0;

}



int

db_cron_news_get_events_json (db_t *db, int64_t start_s, int64_t end_s, json_t **out_array)

{

  if (!db || !out_array) return -1;

  *out_array = json_array();

  db_res_t *res = NULL;

  db_error_t err;

  db_error_clear (&err);

  

  char sql[512];

  if (sql_build(db, "SELECT engine_events_id, ts, type, actor_player_id, sector_id, payload FROM engine_events WHERE ts >= {1} AND ts < {2} ORDER BY ts ASC;", sql, sizeof(sql)) != 0) return -1;



  if (db_query (db, sql, (db_bind_t[]){ db_bind_timestamp_text (start_s), db_bind_timestamp_text (end_s) }, 2, &res, &err))

    {

      while (db_res_step (res, &err))

        {

          json_t *obj = json_object();

          // json_object_set_new(obj, "id", json_integer(db_res_col_i64(res, 0, &err)));

          json_object_set_new(obj, "ts", json_integer(db_res_col_i64(res, 1, &err)));

          json_object_set_new(obj, "type", json_string(db_res_col_text(res, 2, &err)));

          json_object_set_new(obj, "actor_player_id", json_integer(db_res_col_i32(res, 3, &err)));

          json_object_set_new(obj, "sector_id", json_integer(db_res_col_i32(res, 4, &err)));

          json_object_set_new(obj, "payload", json_string(db_res_col_text(res, 5, &err)));

          json_array_append_new(*out_array, obj);

        }

      db_res_finalize (res);

      return 0;

    }

  return -1;

}



int

db_cron_get_corp_details_json (db_t *db, int corp_id, json_t **out_json)

{

  if (!db || !out_json) return -1;

  *out_json = NULL;

  db_res_t *res = NULL;

  db_error_t err;

  db_error_clear (&err);

  

  char sql[512];

  if (sql_build(db, "SELECT name, tag, tax_arrears, credit_rating FROM corporations WHERE corporation_id = {1};", sql, sizeof(sql)) != 0) return -1;



  if (db_query (db, sql, (db_bind_t[]){ db_bind_i64 (corp_id) }, 1, &res, &err))

    {

      if (db_res_step (res, &err))

        {

          *out_json = json_object();

          json_object_set_new(*out_json, "name", json_string(db_res_col_text(res, 0, &err)));

          const char *tag = db_res_col_text(res, 1, &err);

          if (tag) json_object_set_new(*out_json, "tag", json_string(tag));

          json_object_set_new(*out_json, "tax_arrears", json_integer(db_res_col_i64(res, 2, &err)));

          json_object_set_new(*out_json, "credit_rating", json_integer(db_res_col_i32(res, 3, &err)));

        }

      db_res_finalize (res);

      return 0;

    }

  return -1;

}



int

db_cron_get_stock_details_json (db_t *db, int corp_id, int stock_id, json_t **out_json)

{

  if (!db || !out_json) return -1;

  *out_json = NULL;

  db_res_t *res = NULL;

  db_error_t err;

  db_error_clear (&err);



  char sql[512];

  if (sql_build(db, "SELECT c.name, s.ticker FROM corporations c JOIN stocks s ON c.corporation_id = s.corp_id WHERE c.corporation_id = {1} AND s.id = {2}", sql, sizeof(sql)) != 0) return -1;



  if (db_query (db, sql, (db_bind_t[]){ db_bind_i64 (corp_id), db_bind_i64 (stock_id) }, 2, &res, &err))

    {

      if (db_res_step (res, &err))

        {

          *out_json = json_object();

          json_object_set_new(*out_json, "name", json_string(db_res_col_text(res, 0, &err)));

          json_object_set_new(*out_json, "ticker", json_string(db_res_col_text(res, 1, &err)));

        }

      db_res_finalize (res);

      return 0;

    }

  return -1;

}



/* ==================================================================== */

/* ADDITIONAL CRON HELPERS                                              */

/* ==================================================================== */



int

db_cron_cleanup_old_news (db_t *db, int64_t cutoff_s)

{

  if (!db) return -1;

  db_error_t err;

  db_error_clear (&err);

  char sql[512];

  if (sql_build(db, "DELETE FROM news_feed WHERE published_ts < {1};", sql, sizeof(sql)) != 0) return -1;

  if (!db_exec (db, sql, (db_bind_t[]){ db_bind_i64 (cutoff_s) }, 1, &err)) return -1;

  return 0;

}



int

db_cron_lottery_check_processed (db_t *db, const char *date_str)

{

  if (!db || !date_str) return -1;

  db_res_t *res = NULL;

  db_error_t err;

  db_error_clear (&err);

  int processed = 0;

  char sql[512];

  if (sql_build(db, "SELECT winning_number FROM tavern_lottery_state WHERE draw_date = {1}", sql, sizeof(sql)) != 0) return -1;



  if (db_query (db, sql, (db_bind_t[]){ db_bind_text (date_str) }, 1, &res, &err))

    {

      if (db_res_step (res, &err))

        {

          if (!db_res_col_is_null (res, 0)) processed = 1;

        }

      db_res_finalize (res);

    }

  return processed;

}



int

db_cron_lottery_get_yesterday (db_t *db, const char *date_str, long long *carried_over)

{

  if (!db || !date_str || !carried_over) return -1;

  *carried_over = 0;

  db_res_t *res = NULL;

  db_error_t err;

  db_error_clear (&err);

  char sql[512];

  if (sql_build(db, "SELECT jackpot, carried_over FROM tavern_lottery_state WHERE draw_date = {1}", sql, sizeof(sql)) != 0) return -1;



  if (db_query (db, sql, (db_bind_t[]){ db_bind_text (date_str) }, 1, &res, &err))

    {

      if (db_res_step (res, &err))

        {

          *carried_over = db_res_col_i64 (res, 1, &err);

        }

      db_res_finalize (res);

      return 0;

    }

  return -1;

}



int

db_cron_lottery_get_stats (db_t *db, const char *date_str, long long *total_pot)

{

  if (!db || !date_str || !total_pot) return -1;

  *total_pot = 0;

  db_res_t *res = NULL;

  db_error_t err;

  db_error_clear (&err);

  char sql[512];

  if (sql_build(db, "SELECT COUNT(*), SUM(cost) FROM tavern_lottery_tickets WHERE draw_date = {1}", sql, sizeof(sql)) != 0) return -1;



  if (db_query (db, sql, (db_bind_t[]){ db_bind_text (date_str) }, 1, &res, &err))

    {

      if (db_res_step (res, &err))

        {

          *total_pot = db_res_col_i64 (res, 1, &err);

        }

      db_res_finalize (res);

      return 0;

    }

  return -1;

}



int

db_cron_lottery_get_winners_json (db_t *db, const char *date_str, int winning_number, json_t **out_array)

{

  if (!db || !date_str || !out_array) return -1;

  *out_array = json_array();

  db_res_t *res = NULL;

  db_error_t err;

  db_error_clear (&err);

  char sql[512];

  if (sql_build(db, "SELECT player_id FROM tavern_lottery_tickets WHERE draw_date = {1} AND number = {2}", sql, sizeof(sql)) != 0) return -1;



  if (db_query (db, sql, (db_bind_t[]){ db_bind_text (date_str), db_bind_i64 (winning_number) }, 2, &res, &err))

    {

      while (db_res_step (res, &err))

        {

          json_t *obj = json_object();

          json_object_set_new(obj, "player_id", json_integer(db_res_col_i32(res, 0, &err)));

          json_array_append_new(*out_array, obj);

        }

      db_res_finalize (res);

      return 0;

    }

  return -1;

}



int
db_cron_lottery_update_state (db_t *db, const char *date_str, int winning_number, long long jackpot, long long carried_over)
{
  if (!db || !date_str) return -1;
  db_error_t err;
  db_error_clear (&err);

  db_bind_t params[4];
  params[0] = db_bind_text (date_str);
  params[1] = (winning_number > 0) ? db_bind_i64 (winning_number) : db_bind_null ();
  params[2] = db_bind_i64 (jackpot);
  params[3] = db_bind_i64 (carried_over);

  /* 1. Try Update first */
  const char *sql_upd = "UPDATE tavern_lottery_state SET winning_number = {2}, jackpot = {3}, carried_over = {4} WHERE draw_date = {1};";
  char sql[512]; sql_build(db, sql_upd, sql, sizeof(sql));
  int64_t rows = 0;
  if (db_exec_rows_affected(db, sql, params, 4, &rows, &err) && rows > 0) return 0;

  /* 2. Try Insert if update affected 0 rows */
  const char *sql_ins = "INSERT INTO tavern_lottery_state (draw_date, winning_number, jackpot, carried_over) VALUES ({1}, {2}, {3}, {4});";
  sql_build(db, sql_ins, sql, sizeof(sql));
  if (!db_exec(db, sql, params, 4, &err)) {
      /* 3. If Insert failed due to constraint (concurrent write), retry Update once */
      if (err.code == ERR_DB_CONSTRAINT) {
          sql_build(db, sql_upd, sql, sizeof(sql));
          if (db_exec(db, sql, params, 4, &err)) return 0;
      }
      return -1;
  }

  return 0;
}



int

db_cron_deadpool_expire_bets (db_t *db, int64_t now_s)

{

  if (!db) return -1;

  db_error_t err;

  db_error_clear (&err);

  

    char sql[256];

  

    if (sql_build(db, "UPDATE tavern_deadpool_bets SET resolved = TRUE, result = 'expired', resolved_at = {1} WHERE resolved = FALSE AND expires_at <= {2}", sql, sizeof(sql)) != 0) return -1;



  if (!db_exec (db, sql, (db_bind_t[]){ db_bind_timestamp_text (now_s), db_bind_timestamp_text (now_s) }, 2, &err)) return -1;

  return 0;

}



int

db_cron_deadpool_get_events_json (db_t *db, json_t **out_array)

{

  if (!db || !out_array) return -1;

  *out_array = json_array();

  db_res_t *res = NULL;

  db_error_t err;

  db_error_clear (&err);

      int64_t one_day_ago = (int64_t)time(NULL) - 86400;

      const char *sql = "SELECT payload FROM engine_events WHERE type = 'ship.destroyed' AND ts > (SELECT COALESCE(MAX(resolved_at), {1}) FROM tavern_deadpool_bets WHERE result IS NOT NULL AND result != 'expired');";

      db_query (db, sql, (db_bind_t[]){ db_bind_timestamp_text(one_day_ago) }, 1, &res, &err);

    {

      while (db_res_step (res, &err))

        {

          const char *payload_str = db_res_col_text (res, 0, &err);

          if (payload_str)

            {

              json_error_t jerr;

              json_t *payload_obj = json_loads (payload_str, 0, &jerr);

              if (payload_obj) json_array_append_new(*out_array, payload_obj);

            }

        }

      db_res_finalize (res);

      return 0;

    }

  return -1;

}



int

db_cron_deadpool_get_bets_json (db_t *db, int target_id, json_t **out_array)

{

  if (!db || !out_array) return -1;

  *out_array = json_array();

  db_res_t *res = NULL;

  db_error_t err;

  db_error_clear (&err);

  char sql[512];

  if (sql_build(db, "SELECT tavern_deadpool_bets_id AS id, bettor_id, amount, odds_bp FROM tavern_deadpool_bets WHERE target_id = {1} AND resolved = FALSE", sql, sizeof(sql)) != 0) return -1;



  if (db_query (db, sql, (db_bind_t[]){ db_bind_i64 (target_id) }, 1, &res, &err))

    {

      while (db_res_step (res, &err))

        {

          json_t *obj = json_object();

          json_object_set_new(obj, "id", json_integer(db_res_col_i32(res, 0, &err)));

          json_object_set_new(obj, "bettor_id", json_integer(db_res_col_i32(res, 1, &err)));

          json_object_set_new(obj, "amount", json_integer(db_res_col_i64(res, 2, &err)));

          json_object_set_new(obj, "odds_bp", json_integer(db_res_col_i32(res, 3, &err)));

          json_array_append_new(*out_array, obj);

        }

      db_res_finalize (res);

      return 0;

    }

  return -1;

}



int

db_cron_deadpool_update_bet (db_t *db, int bet_id, const char *result, int64_t resolved_at)

{

  if (!db || !result) return -1;

  db_error_t err;

  db_error_clear (&err);

  char sql[512];

  if (sql_build(db, "UPDATE tavern_deadpool_bets SET resolved = TRUE, result = {1}, resolved_at = {2} WHERE tavern_deadpool_bets_id = {3}", sql, sizeof(sql)) != 0) return -1;

  if (!db_exec (db, sql, (db_bind_t[]){ db_bind_text (result), db_bind_timestamp_text (resolved_at), db_bind_i64 (bet_id) }, 3, &err)) return -1;

  return 0;

}



int

db_cron_deadpool_update_lost_bets (db_t *db, int target_id, int64_t resolved_at)

{

  if (!db) return -1;

  db_error_t err;

  db_error_clear (&err);

  char sql[512];

  if (sql_build(db, "UPDATE tavern_deadpool_bets SET resolved = TRUE, result = 'lost', resolved_at = {1} WHERE target_id = {2} AND resolved = FALSE", sql, sizeof(sql)) != 0) return -1;

  if (!db_exec (db, sql, (db_bind_t[]){ db_bind_timestamp_text (resolved_at), db_bind_i64 (target_id) }, 2, &err)) return -1;

  return 0;

}



int

db_cron_tavern_cleanup (db_t *db, int64_t now_s)

{

  if (!db) return -1;

  db_error_t err;

  db_error_clear (&err);



  char sql1[256];

  if (sql_build(db, "DELETE FROM tavern_notices WHERE expires_at <= {1};", sql1, sizeof(sql1)) != 0) return -1;

  if (!db_exec (db, sql1, (db_bind_t[]){ db_bind_timestamp_text (now_s) }, 1, &err)) return -1;



  char sql2[256];

  if (sql_build(db, "DELETE FROM corp_recruiting WHERE expires_at <= {1};", sql2, sizeof(sql2)) != 0) return -1;

  if (!db_exec (db, sql2, (db_bind_t[]){ db_bind_timestamp_text (now_s) }, 1, &err)) return -1;

  return 0;

}



int

db_cron_get_loans_json (db_t *db, json_t **out_array)

{

  if (!db || !out_array) return -1;

  *out_array = json_array();

  db_res_t *res = NULL;

  db_error_t err;

  db_error_clear (&err);

  const char *sql = "SELECT player_id, principal, interest_rate, due_date, is_defaulted FROM tavern_loans WHERE principal > 0;";



  if (db_query (db, sql, NULL, 0, &res, &err))

    {

      while (db_res_step (res, &err))

        {

          json_t *obj = json_object();

          json_object_set_new(obj, "player_id", json_integer(db_res_col_i32(res, 0, &err)));

          json_object_set_new(obj, "principal", json_integer(db_res_col_i64(res, 1, &err)));

          json_object_set_new(obj, "interest_rate", json_integer(db_res_col_i32(res, 2, &err)));

          json_array_append_new(*out_array, obj);

        }

      db_res_finalize (res);

      return 0;

    }

  return -1;

}



int

db_cron_get_stocks_json (db_t *db, json_t **out_array)

{

  if (!db || !out_array) return -1;

  *out_array = json_array();

  db_res_t *res = NULL;

  db_error_t err;

  db_error_clear (&err);

  const char *sql = "SELECT s.id, s.corp_id, s.total_shares FROM stocks s WHERE s.corp_id > 0;";



  if (db_query (db, sql, NULL, 0, &res, &err))

    {

      while (db_res_step (res, &err))

        {

          json_t *obj = json_object();

          json_object_set_new(obj, "id", json_integer(db_res_col_i32(res, 0, &err)));

          json_object_set_new(obj, "corp_id", json_integer(db_res_col_i32(res, 1, &err)));

          json_object_set_new(obj, "total_shares", json_integer(db_res_col_i64(res, 2, &err)));

          json_array_append_new(*out_array, obj);

        }

      db_res_finalize (res);

      return 0;

    }

  return -1;

}



int

db_cron_get_corp_planet_assets (db_t *db, int corp_id, long long *net_value)

{

  if (!db || !net_value) return -1;

  *net_value = 0;

  db_res_t *res = NULL;

  db_error_t err;

  db_error_clear (&err);

  char sql[512];

  if (sql_build(db, "SELECT ore_on_hand, organics_on_hand, equipment_on_hand FROM planets WHERE owner_id = {1} AND owner_type = 'corp'", sql, sizeof(sql)) != 0) return -1;



  if (db_query (db, sql, (db_bind_t[]){ db_bind_i64 (corp_id) }, 1, &res, &err))

    {

      while (db_res_step (res, &err))

        {

          *net_value += db_res_col_i64 (res, 0, &err) * 100;

          *net_value += db_res_col_i64 (res, 1, &err) * 150;

          *net_value += db_res_col_i64 (res, 2, &err) * 200;

        }

      db_res_finalize (res);

      return 0;

    }

  return -1;

}



int

db_cron_update_stock_price (db_t *db, int stock_id, long long new_price)

{

  if (!db) return -1;

  db_error_t err;

  db_error_clear (&err);

  char sql[512];

  if (sql_build(db, "UPDATE stocks SET current_price = {1} WHERE id = {2}", sql, sizeof(sql)) != 0) return -1;

  if (!db_exec (db, sql, (db_bind_t[]){ db_bind_i64 (new_price), db_bind_i64 (stock_id) }, 2, &err)) return -1;

  return 0;

}



int

db_cron_shield_regen (db_t *db, int percent)

{

  if (!db) return -1;

  db_error_t err;

  db_error_clear (&err);

  char sql[512];

  if (sql_build(db, "UPDATE ships SET shields = LEAST(installed_shields, shields + ((installed_shields * {1}) / 100)) WHERE destroyed = FALSE AND installed_shields > 0 AND shields < installed_shields;", sql, sizeof(sql)) != 0) return -1;

  if (!db_exec (db, sql, (db_bind_t[]){ db_bind_i64 (percent) }, 1, &err)) return -1;

  return 0;

}
