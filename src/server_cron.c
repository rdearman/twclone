#include "server_cron.h"
#include "server_log.h"
#include <string.h>

typedef struct { const char *name; cron_handler_fn fn; } entry_t;

/* ---- forward decls for handlers ---- */
/* static int h_fedspace_cleanup(sqlite3 *db, int64_t now_s); */
/* static int h_autouncloak_sweeper(sqlite3 *db, int64_t now_s); */
/* static int h_terra_replenish(sqlite3 *db, int64_t now_s); */
/* static int h_port_reprice(sqlite3 *db, int64_t now_s); */
/* static int h_planet_growth(sqlite3 *db, int64_t now_s); */
/* static int h_broadcast_ttl_cleanup(sqlite3 *db, int64_t now_s); */
/* static int h_traps_process(sqlite3 *db, int64_t now_s); */
/* static int h_npc_step(sqlite3 *db, int64_t now_s); */

static entry_t REG[] = {
  { "fedspace_cleanup",       h_fedspace_cleanup },
  { "autouncloak_sweeper",    h_autouncloak_sweeper },
  { "terra_replenish",        h_terra_replenish },
  { "port_reprice",           h_port_reprice },
  { "planet_growth",          h_planet_growth },
  { "broadcast_ttl_cleanup",  h_broadcast_ttl_cleanup },
  { "traps_process",          h_traps_process },
  { "npc_step",               h_npc_step },
};

static int g_reg_inited = 0;


static int try_lock(sqlite3 *db, const char *name, int64_t now_s) {
  sqlite3_stmt *st = NULL;
  int rc;
  
  /* Lock duration: 60 seconds (in seconds, matching now_s) */
  const int LOCK_DURATION_S = 60; 
  int64_t until_s = now_s + LOCK_DURATION_S;

  // 1. --- STALE LOCK CLEANUP ---
  // Delete any lock that has ALREADY EXPIRED (until_ms <= now_s).
  // Note: Since your schema uses 'until_ms' (INTEGER) but you pass 'now_s' (seconds),
  // we will assume the INTEGER column stores SECONDS for now, and adjust the column names.
  
  rc = sqlite3_prepare_v2(db, 
    "DELETE FROM locks WHERE lock_name=?1 AND until_ms < ?2;", -1, &st, NULL);
  if (rc == SQLITE_OK) {
      sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
      sqlite3_bind_int64(st, 2, now_s); // Use current time as the limit
      sqlite3_step(st); // Execute the deletion of the stale lock
  }
  sqlite3_finalize(st);
  if (rc != SQLITE_OK) return 0; // Abort on SQL error during cleanup

  // 2. --- LOCK ACQUISITION ---
  rc = sqlite3_prepare_v2(db,
    "INSERT INTO locks(lock_name, owner, until_ms) VALUES(?1, 'server', ?2) "
    "ON CONFLICT(lock_name) DO NOTHING;", -1, &st, NULL);
  if (rc != SQLITE_OK) return 0;

  sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
  // Bind the calculated expiration time (now_s + 60s)
  sqlite3_bind_int64(st, 2, until_s); 
  
  sqlite3_step(st); 
  sqlite3_finalize(st);

  /* 3. Check we own it now (The original logic for checking ownership is still valid) */
  rc = sqlite3_prepare_v2(db,
    "SELECT owner FROM locks WHERE lock_name=?1;", -1, &st, NULL);
  if (rc != SQLITE_OK) return 0;
  
  sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
  int ok = 0;
  if (sqlite3_step(st) == SQLITE_ROW) {
    const unsigned char *o = sqlite3_column_text(st, 0);
    ok = (o && strcmp((const char*)o, "server") == 0);
  }
  sqlite3_finalize(st);
  return ok;
}



/// Returns 0 if lock is free, or the 'until_ms' timestamp if locked.
int64_t db_lock_status(sqlite3 *db, const char *name) {
    const char *SQL = "SELECT until_ms FROM locks WHERE lock_name = ?1;";
    sqlite3_stmt *st = NULL;
    int64_t until_ms = 0;

    if (sqlite3_prepare_v2(db, SQL, -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW) {
            // Note: assuming until_ms is stored in milliseconds (int64)
            until_ms = sqlite3_column_int64(st, 0);
        }
        sqlite3_finalize(st);
    }
    return until_ms;
}

static void unlock(sqlite3 *db, const char *name){
  sqlite3_stmt *st=NULL;
  if (sqlite3_prepare_v2(db, "DELETE FROM locks WHERE name=?1 AND owner='server';", -1, &st, NULL)==SQLITE_OK){
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    sqlite3_step(st);
    sqlite3_finalize(st);
  }
}


void cron_register_builtins(void) {
  g_reg_inited = 1;
}

/* cron_handler_fn cron_find(const char *name) { */
/*   if (!g_reg_inited || !name) return NULL; */
/*   for (unsigned i = 0; i < sizeof(REG)/sizeof(REG[0]); ++i) { */
/*     if (strcmp(REG[i].name, name) == 0) return REG[i].fn; */
/*   } */
/*   return NULL; */
/* } */

/* Helpers */
static int begin(sqlite3 *db){ return sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL); }
static int commit(sqlite3 *db){ return sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL); }
static int rollback(sqlite3 *db){ return sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL); }

/* FedSpace cleanup:
   - Remove traps in FedSpace
   - Clear wanted flags for ships in FedSpace
   - Tow excess ships if > MAX_SHIPS_PER_FED_SECTOR in any Fed sector (keep the lowest IDs in place)
   - Tow ships carrying >= MAX_FED_FIGHTERS in FedSpace
   - At most 50 total tows per pass
*/
 int h_fedspace_cleanup(sqlite3 *db, int64_t now_s)
 {
  // Use milliseconds for the lock check (now_s is seconds, convert to ms)
  int64_t now_ms = now_s * 1000; 

  if (!try_lock(db, "fedspace_cleanup", now_ms)) {
    // try_lock failed. Check the status to see if the lock is stale.
    int64_t until_ms = db_lock_status(db, "fedspace_cleanup");
    int64_t time_left_s = (until_ms - now_ms) / 1000;

    if (until_ms > now_ms) {
      // The lock exists and is NOT stale. This is an ACTIVE conflict.
      LOGW("fedspace_cleanup: FAILED to acquire lock. Still held for %lld more seconds.", (long long)time_left_s);
    } else {
      // The lock exists but should have been cleaned up by try_lock (Stale/Expired).
      // This indicates a problem in the try_lock's cleanup step, but logs the state.
      LOGW("fedspace_cleanup: FAILED to acquire lock. Lock is stale (Expires at %lld).", (long long)until_ms);
    }
    return 0;
  } else {
    // try_lock succeeded.
    LOGI("fedspace_cleanup: Lock acquired, starting cleanup operations.");
  }
  
   
  enum { MAX_TOWS_PER_PASS = 50, MAX_SHIPS_PER_FED_SECTOR = 5, MAX_FED_FIGHTERS = 98 };
  int rc = begin(db);
  LOGI("Starting Fedspace Cleanup");
  if (rc) { LOGW("fedspace_cleanup: begin rc=%d", rc); return rc; }

    LOGI("Looking for mines in fedspace");
  /* 1) Remove sector traps/mines in FedSpace */
  rc = sqlite3_exec(db,
        "DELETE FROM traps WHERE sector IN (SELECT id FROM sectors WHERE is_fed=1);",
        NULL, NULL, NULL);
  if (rc) { rollback(db); LOGE("fedspace_cleanup traps rc=%d", rc); unlock(db, "fedspace_cleanup"); return rc; }

  /* 2) Clear wanted flags in FedSpace (engine policy) */
  rc = sqlite3_exec(db,
        "UPDATE ships SET wanted=0 "
        "WHERE wanted=1 AND sector IN (SELECT id FROM sectors WHERE is_fed=1);",
        NULL, NULL, NULL);
  if (rc) { rollback(db); LOGE("fedspace_cleanup ships rc=%d", rc); unlock(db, "fedspace_cleanup"); return rc; }

  /* Helper: find a non-fed tow target for a given Fed sector (first non-fed neighbour; fallback = 1) */
  sqlite3_stmt *q_tow = NULL;
  rc = sqlite3_prepare_v2(db,
        "WITH nf AS ( "
        "  SELECT w.to_sector AS cand "
        "  FROM warps w JOIN sectors s ON s.id=w.to_sector "
        "  WHERE w.from_sector=?1 AND s.is_fed=0 "
        "  ORDER BY cand "
        ") SELECT COALESCE((SELECT cand FROM nf LIMIT 1), 1);",
        -1, &q_tow, NULL);
  if (rc != SQLITE_OK)
    {
    rollback(db); LOGE("fedspace_cleanup prep tow-target rc=%d", rc);
    unlock(db, "fedspace_cleanup");
    return rc;
    }
  else
    LOGI("Tow Target: %s", (char *)&q_tow);

  // OK, need to figure out what to do with player who has multiple ships? If the ship is towed and they are in it, they move too. 
  LOGI("Set ships sectors for twoed ships");
  /* Update ship sector */
  sqlite3_stmt *u_ship = NULL;
  rc = sqlite3_prepare_v2(db,
        "UPDATE ships SET sector=?1 WHERE id=?2;", -1, &u_ship, NULL);
  if (rc != SQLITE_OK) { sqlite3_finalize(q_tow); rollback(db); LOGE("fedspace_cleanup prep upd-ship rc=%d", rc); unlock(db, "fedspace_cleanup"); return rc; }

  
  /* Notice to owner (adjust to your notice table/columns) */
  sqlite3_stmt *i_notice = NULL;
  rc = sqlite3_prepare_v2(db,
        "INSERT INTO system_notice(scope,scope_id,message,created_at,expires_at) "
        "VALUES('player',?1,?2,?3,NULL);",
        -1, &i_notice, NULL);
  if (rc != SQLITE_OK)
    {
      sqlite3_finalize(u_ship);
      sqlite3_finalize(q_tow);
      rollback(db);
      LOGE("fedspace_cleanup prep notice rc=%d", rc);
      unlock(db, "fedspace_cleanup");
      return rc;
    }
  else
    LOGI("%s",(char *) &i_notice);

  int tows = 0;

  /* 3) Overcrowding: tow ships beyond MAX_SHIPS_PER_FED_SECTOR in each Fed sector (keep the lowest IDs) */
  sqlite3_stmt *q_over = NULL;
  rc = sqlite3_prepare_v2(db,
        "WITH fed AS IN (1,2,3,4,5,6,7,8,9,10), "
        "crowded AS ( "
        "  SELECT s.id AS ship_id, s.sector, s.owner_player_id, "
        "         ROW_NUMBER() OVER (PARTITION BY s.sector ORDER BY s.id) AS rn "
        "  FROM ships s WHERE s.sector IN (SELECT id FROM fed) "
        "), offenders AS ( "
        "  SELECT ship_id, sector, owner_player_id "
        "  FROM crowded WHERE rn > ?1 "
        "  LIMIT ?2 "
        ") "
        "SELECT ship_id, sector, owner_player_id FROM offenders;",
        -1, &q_over, NULL);
  if (rc != SQLITE_OK)
    {
    sqlite3_finalize(i_notice);
    sqlite3_finalize(u_ship);
    sqlite3_finalize(q_tow);
    rollback(db);
    LOGE("fedspace_cleanup prep overcrowded rc=%d", rc);
    unlock(db, "fedspace_cleanup");
    return rc;
    }
  else
    LOGI("Cleared ship from fed from fedspace to %s",(char *) &q_over);
  
  sqlite3_bind_int(q_over, 1, MAX_SHIPS_PER_FED_SECTOR);
  sqlite3_bind_int(q_over, 2, MAX_TOWS_PER_PASS);

  while (tows < MAX_TOWS_PER_PASS && sqlite3_step(q_over) == SQLITE_ROW) {
    int ship_id = sqlite3_column_int(q_over, 0);
    int sec     = sqlite3_column_int(q_over, 1);
    int owner   = sqlite3_column_int(q_over, 2);

    /* compute tow target */
    int tow_to = 1; // this should probably just be a random number between 11-500
    LOGI("Q_TOW=%s", (char *) q_tow);
    sqlite3_reset(q_tow);
    sqlite3_clear_bindings(q_tow);
    sqlite3_bind_int(q_tow, 1, sec);
    if (sqlite3_step(q_tow) == SQLITE_ROW) {
      tow_to = sqlite3_column_int(q_tow, 0);
    }
    sqlite3_reset(q_tow);

    /* move ship */
    sqlite3_reset(u_ship);
    sqlite3_clear_bindings(u_ship);
    sqlite3_bind_int(u_ship, 1, tow_to);
    sqlite3_bind_int(u_ship, 2, ship_id);
    if (sqlite3_step(u_ship) != SQLITE_DONE) { rc = SQLITE_ERROR; break; }

    /* owner notice */
    sqlite3_reset(i_notice);
    sqlite3_clear_bindings(i_notice);
    sqlite3_bind_int(i_notice, 1, owner);
    sqlite3_bind_text(i_notice, 2,
      "Federation tow: sector overcrowded; your ship was relocated to a nearby non-Fed sector.",
      -1, SQLITE_STATIC);
    sqlite3_bind_int64(i_notice, 3, now_s);
    if (sqlite3_step(i_notice) != SQLITE_DONE) { rc = SQLITE_ERROR; break; }

    tows++;
  }

  sqlite3_finalize(q_over);

  if (rc == SQLITE_OK && tows < MAX_TOWS_PER_PASS) {
    /* 4) Fighter cap: tow ships carrying >= 99 fighters while in FedSpace (bounded by remaining budget) */
    int remaining = MAX_TOWS_PER_PASS - tows;
    sqlite3_stmt *q_fcap = NULL;
    rc = sqlite3_prepare_v2(db,
          "SELECT s.id AS ship_id, s.sector, s.owner_player_id "
          "FROM ships s "
          "WHERE s.sector IN (SELECT id FROM sectors WHERE in (1,2,3,4,5,6,7,8,9,10)) "
          "  AND s.fighters >= ?1 "
          "LIMIT ?2;",
          -1, &q_fcap, NULL);
    if (rc == SQLITE_OK) {
      sqlite3_bind_int(q_fcap, 1, MAX_FED_FIGHTERS + 1); /* >= 99 */
      sqlite3_bind_int(q_fcap, 2, remaining);

      while (tows < MAX_TOWS_PER_PASS && sqlite3_step(q_fcap) == SQLITE_ROW) {
        int ship_id = sqlite3_column_int(q_fcap, 0);
        int sec     = sqlite3_column_int(q_fcap, 1);
        int owner   = sqlite3_column_int(q_fcap, 2);

        int tow_to = 1;
        sqlite3_reset(q_tow);
        sqlite3_clear_bindings(q_tow);
        sqlite3_bind_int(q_tow, 1, sec);
        if (sqlite3_step(q_tow) == SQLITE_ROW) {
          tow_to = sqlite3_column_int(q_tow, 0);
        }
        sqlite3_reset(q_tow);

        sqlite3_reset(u_ship);
        sqlite3_clear_bindings(u_ship);
        sqlite3_bind_int(u_ship, 1, tow_to);
        sqlite3_bind_int(u_ship, 2, ship_id);
        if (sqlite3_step(u_ship) != SQLITE_DONE) { rc = SQLITE_ERROR; break; }

        sqlite3_reset(i_notice);
        sqlite3_clear_bindings(i_notice);
        sqlite3_bind_int(i_notice, 1, owner);
        sqlite3_bind_text(i_notice, 2,
          "Federation tow: carrying too many fighters in FedSpace; your ship was relocated.",
          -1, SQLITE_STATIC);
        sqlite3_bind_int64(i_notice, 3, now_s);
        if (sqlite3_step(i_notice) != SQLITE_DONE) { rc = SQLITE_ERROR; break; }

        tows++;
      }
      sqlite3_finalize(q_fcap);
    }
  }

  sqlite3_finalize(i_notice);
  sqlite3_finalize(u_ship);
  sqlite3_finalize(q_tow);

  if (rc != SQLITE_OK) {
    rollback(db);
    LOGE("fedspace_cleanup tow rc=%d (towed=%d)", rc, tows);
    unlock(db, "fedspace_cleanup");
    return rc;
  }

  commit(db);
  LOGI("fedspace_cleanup: ok (towed=%d)", tows);
  unlock(db, "fedspace_cleanup");
  return 0;
}


// In ../src/server_cron.c (or a dedicated cron job file)

int h_daily_turn_reset(sqlite3 *db, int64_t now_s) {
  if (!try_lock(db, "daily_turn_reset", now_s)) return 0;
  
  LOGI("daily_turn_reset: starting daily turn reset.");

  int rc = begin(db); 
  if (rc) {
    unlock(db, "daily_turn_reset");
    return rc;
  }
  
  // 1. Reset all player's available turns to the daily maximum.
  rc = sqlite3_exec(db, 
    "UPDATE players SET turns = (SELECT turnsperday FROM config WHERE id=1);", 
    NULL, NULL, NULL);
  
  if (rc != SQLITE_OK) {
    LOGE("daily_turn_reset: player turn update failed: %s", sqlite3_errmsg(db));
    rollback(db);
    unlock(db, "daily_turn_reset");
    return rc;
  }

  // 2. Perform any other daily cleanup/reset logic here (e.g., daily events, stats reset).

  rc = commit(db);
  if (rc != SQLITE_OK) {
    LOGE("daily_turn_reset: commit failed: %s", sqlite3_errmsg(db));
  }
  
  LOGI("daily_turn_reset: ok");
  unlock(db, "daily_turn_reset");
  return rc;
}


 int h_autouncloak_sweeper(sqlite3 *db, int64_t now_s)
{
  if (!try_lock(db, "autouncloak_sweeper", now_s)) return 0;
  sqlite3_stmt *st=NULL;
  int rc = sqlite3_prepare_v2(db,
    "UPDATE ships SET cloaked=0, cloak_expires_at=NULL "
    "WHERE cloaked=1 AND cloak_expires_at IS NOT NULL AND cloak_expires_at <= ?1;",
    -1, &st, NULL);
  if (rc==SQLITE_OK) { sqlite3_bind_int64(st, 1, now_s); sqlite3_step(st); }
  sqlite3_finalize(st);
  rc = commit(db); 
  LOGI("autouncloak_sweeper: ok");
  unlock(db, "autouncloak_sweeper");
  return 0;
}

 int h_terra_replenish(sqlite3 *db, int64_t now_s)
{
if (!try_lock(db, "terra_replenish", now_s)) return 0;
  (void)now_s;
  int rc = begin(db); if (rc) return rc;

  rc = sqlite3_exec(db,
    "UPDATE ports SET credits=MAX(credits, 500000000), fuel_stock=1000000, organics_stock=1000000 "
    "WHERE is_terra=1;",
    NULL, NULL, NULL);

  if (rc) { rollback(db); LOGE("terra_replenish rc=%d", rc); return rc; }
  commit(db);
  LOGI("terra_replenish: ok");
  unlock(db, "terra_replenish");
  return 0;
}

 int h_port_reprice(sqlite3 *db, int64_t now_s)
{
if (!try_lock(db, "port_reprice", now_s)) return 0; 
  (void)now_s;
  int rc = begin(db); if (rc) return rc;

  /* Placeholder: gentle mean reversion toward baseline prices. */
  rc = sqlite3_exec(db,
    "UPDATE ports SET "
    " fuel_price = (fuel_price*9 + baseline_fuel_price)/10, "
    " org_price  = (org_price*9  + baseline_org_price)/10, "
    " equip_price= (equip_price*9+ baseline_equip_price)/10;",
    NULL, NULL, NULL);

  if (rc) { rollback(db); LOGE("port_reprice rc=%d", rc); return rc; }
  commit(db);
  LOGI("port_reprice: ok");
  unlock(db, "port_reprice");
  return 0;
}

 int h_planet_growth(sqlite3 *db, int64_t now_s) {
                                                                   
if (!try_lock(db, "planet_growth", now_s)) return 0;
  (void)now_s;
  int rc = begin(db); if (rc) return rc;

  rc = sqlite3_exec(db,
    "UPDATE planets SET "
    " population = population + CAST(population*0.001 AS INT), "  /* +0.1% */
    " ore_stock  = MIN(ore_cap,  ore_stock  + 50), "
    " org_stock  = MIN(org_cap,  org_stock  + 50), "
    " equip_stock= MIN(equip_cap,equip_stock+ 50);",
    NULL, NULL, NULL);

  if (rc) { rollback(db); LOGE("planet_growth rc=%d", rc); return rc; }
  commit(db);
  LOGI("planet_growth: ok");
  unlock(db, "planet_growth");
  return 0;
}

 int h_broadcast_ttl_cleanup(sqlite3 *db, int64_t now_s) {
  if (!try_lock(db, "broadcast_ttl_cleanup", now_s)) return 0; 
  sqlite3_stmt *st=NULL;
  int rc = sqlite3_prepare_v2(db,
    "DELETE FROM broadcasts WHERE ttl_expires_at IS NOT NULL AND ttl_expires_at <= ?1;",
    -1, &st, NULL);
  if (rc==SQLITE_OK) { sqlite3_bind_int64(st, 1, now_s); sqlite3_step(st); }
  sqlite3_finalize(st);
  rc = commit(db); 
  LOGI("broadcast_ttl_cleanup: ok");
  unlock(db, "broadcast_ttl_cleanup");
  return 0;
}

 int h_traps_process(sqlite3 *db, int64_t now_s) {
  if (!try_lock(db, "traps_process", now_s)) return 0;
  int rc = begin(db); if (rc) return rc;

  /* Example: move due traps to a jobs table for the engine/worker to consume. */
  rc = sqlite3_exec(db,
    "INSERT INTO jobs(type, payload, created_at) "
    "SELECT 'trap.trigger', json_object('trap_id',id), ?1 "
    "FROM traps WHERE armed=1 AND trigger_at IS NOT NULL AND trigger_at <= ?1;",
    NULL, NULL, NULL);
  if (rc) { rollback(db); LOGE("traps_process insert rc=%d", rc); return rc; }

  rc = sqlite3_exec(db,
    "DELETE FROM traps WHERE armed=1 AND trigger_at IS NOT NULL AND trigger_at <= ?1;",
    NULL, NULL, NULL);
  if (rc) { rollback(db); LOGE("traps_process delete rc=%d", rc); return rc; }

  commit(db);
  LOGI("traps_process: ok");
  unlock(db, "traps_process");
  return 0;
}

 int h_npc_step(sqlite3 *db, int64_t now_s) {
  (void)db; (void)now_s;
  /* No-op for now; handled elsewhere. Consider disabling cron_tasks.enabled for npc_step. */
  // LOGD("npc_step noop");
  return 0;
}

