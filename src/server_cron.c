#include "server_cron.h"
#include "server_log.h"
#include <string.h>

typedef struct { const char *name; cron_handler_fn fn; } entry_t;

/* ---- forward decls for handlers ---- */
static int h_fedspace_cleanup(sqlite3 *db, int64_t now_s);
static int h_autouncloak_sweeper(sqlite3 *db, int64_t now_s);
static int h_terra_replenish(sqlite3 *db, int64_t now_s);
static int h_port_reprice(sqlite3 *db, int64_t now_s);
static int h_planet_growth(sqlite3 *db, int64_t now_s);
static int h_broadcast_ttl_cleanup(sqlite3 *db, int64_t now_s);
static int h_traps_process(sqlite3 *db, int64_t now_s);
static int h_npc_step(sqlite3 *db, int64_t now_s);

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


static int try_lock(sqlite3 *db, const char *name, int64_t now_s){
  sqlite3_stmt *st=NULL; int rc;

  rc = sqlite3_prepare_v2(db,
    "INSERT INTO locks(name, owner, acquired_at) VALUES(?1, 'server', ?2) "
    "ON CONFLICT(name) DO NOTHING;", -1, &st, NULL);
  if (rc!=SQLITE_OK) return 0;
  sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
  sqlite3_bind_int64(st, 2, now_s);
  sqlite3_step(st); sqlite3_finalize(st);

  /* Check we own it now */
  rc = sqlite3_prepare_v2(db,
    "SELECT owner FROM locks WHERE name=?1;", -1, &st, NULL);
  if (rc!=SQLITE_OK) return 0;
  sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
  int ok = 0;
  if (sqlite3_step(st) == SQLITE_ROW) {
    const unsigned char *o = sqlite3_column_text(st, 0);
    ok = (o && strcmp((const char*)o, "server")==0);
  }
  sqlite3_finalize(st);
  return ok;
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

cron_handler_fn cron_find(const char *name) {
  if (!g_reg_inited || !name) return NULL;
  for (unsigned i = 0; i < sizeof(REG)/sizeof(REG[0]); ++i) {
    if (strcmp(REG[i].name, name) == 0) return REG[i].fn;
  }
  return NULL;
}

/* Helpers */
static int begin(sqlite3 *db){ return sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL); }
static int commit(sqlite3 *db){ return sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL); }
static int rollback(sqlite3 *db){ return sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL); }

static int h_fedspace_cleanup(sqlite3 *db, int64_t now_s) {
  if (!try_lock(db, "fedspace_cleanup", now_s)) return 0;
  (void)now_s;
  int rc = begin(db);
  if (rc) { LOGW("fedspace_cleanup: begin rc=%d", rc); return rc; }

  /* Example: remove abandoned mines/traps in fed sectors; clamp rogue flags.
     Adjust table/column names to your schema. */
  rc = sqlite3_exec(db,
        "DELETE FROM traps WHERE sector IN (SELECT id FROM sectors WHERE is_fed=1);",
        NULL, NULL, NULL);
  if (rc) { rollback(db); LOGE("fedspace_cleanup traps rc=%d", rc); return rc; }

  rc = sqlite3_exec(db,
        "UPDATE ships SET wanted=0 WHERE wanted=1 AND sector IN "
        "(SELECT id FROM sectors WHERE is_fed=1);",
        NULL, NULL, NULL);
  if (rc) { rollback(db); LOGE("fedspace_cleanup ships rc=%d", rc); return rc; }

  commit(db);
  LOGI("fedspace_cleanup: ok");
  unlock(db, "fedspace_cleanup");
  return 0;
}

static int h_autouncloak_sweeper(sqlite3 *db, int64_t now_s)
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

static int h_terra_replenish(sqlite3 *db, int64_t now_s)
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

static int h_port_reprice(sqlite3 *db, int64_t now_s)
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

static int h_planet_growth(sqlite3 *db, int64_t now_s) {
                                                                   
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

static int h_broadcast_ttl_cleanup(sqlite3 *db, int64_t now_s) {
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

static int h_traps_process(sqlite3 *db, int64_t now_s) {
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

static int h_npc_step(sqlite3 *db, int64_t now_s) {
  (void)db; (void)now_s;
  /* No-op for now; handled elsewhere. Consider disabling cron_tasks.enabled for npc_step. */
  // LOGD("npc_step noop");
  return 0;
}

