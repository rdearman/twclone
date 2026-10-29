#define TW_DB_INTERNAL 1
#include "db/db_api.h"
#include "db/db_int.h"
#include "db/repo/repo_citadel.h"
#include "db/repo/repo_combat.h"
#include "db/repo/repo_planets.h"
#include "db/sql_driver.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *db_mysql_open_internal(db_t *db, const db_config_t *cfg, db_error_t *err)
{ (void) db; (void) cfg; (void) err; return NULL; }
void server_log_printf(int priority, const char *format, ...)
{ (void) priority; (void) format; }

typedef struct {
  db_config_t config;
  pthread_barrier_t *barrier;
  int player_id, ship_id, planet_id, rc;
  int asset_id;
} worker_t;

static int query_i32(db_t *db, const char *query, const db_bind_t *params,
                     size_t n_params, int *out, db_error_t *err)
{
  char sql[512];
  db_res_t *res = NULL;
  if (sql_build(db, query, sql, sizeof sql) != 0
      || !db_query(db, sql, params, n_params, &res, err)
      || !db_res_step(res, err))
    { if (res) db_res_finalize(res); return 0; }
  *out = db_res_col_i32(res, 0, err);
  db_res_finalize(res);
  return err->code == 0;
}

static int returning_i32(db_t *db, const char *query,
                         const db_bind_t *params, size_t n_params,
                         int *out, db_error_t *err)
{
  return query_i32(db, query, params, n_params, out, err);
}

static int exec_sql(db_t *db, const char *query, const db_bind_t *params,
                    size_t n_params, db_error_t *err)
{
  char sql[1024];
  return sql_build(db, query, sql, sizeof sql) == 0
      && db_exec(db, sql, params, n_params, err);
}

static int check_fighters(db_t *db, int planet_id, int ship_id,
                          int expected_planet, int expected_ship,
                          db_error_t *err)
{
  int planet_fighters = -1, ship_fighters = -1;
  return query_i32(db, "SELECT fighters FROM planets WHERE planet_id={1};",
                   (db_bind_t[]){db_bind_i32(planet_id)}, 1,
                   &planet_fighters, err)
      && query_i32(db, "SELECT fighters FROM ships WHERE ship_id={1};",
                   (db_bind_t[]){db_bind_i32(ship_id)}, 1,
                   &ship_fighters, err)
      && planet_fighters == expected_planet
      && ship_fighters == expected_ship;
}

static int create_fixture(db_t *db, int *planet_id, int *player_id,
                          int *ship_id, int *planet_capacity,
                          int *ship_capacity, db_error_t *err)
{
  const char *planet_insert =
    "INSERT INTO planets (sector_id,name,owner_id,owner_type,class,population,type,created_by,fighters) "
    "SELECT s.sector_id,'qa_transfer_' || nextval('planets_planet_id_seq')::text,0,'player','M',0,pt.planettypes_id,0,50 "
    "FROM sectors s CROSS JOIN planettypes pt WHERE pt.code='M' ORDER BY s.sector_id LIMIT 1 RETURNING planet_id;";
  if (!returning_i32(db, planet_insert, NULL, 0, planet_id, err)) return 0;

  const char *player_insert =
    "INSERT INTO players (name,passwd,sector_id,lastplanet) "
    "SELECT 'qa_transfer_' || planet_id::text,"
    "'$argon2id$v=19$m=65536,t=3,p=1$QUJDREVGR0hJSktMTU5PUA$QUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVo',"
    "sector_id,planet_id FROM planets WHERE planet_id={1} RETURNING player_id;";
  if (!returning_i32(db, player_insert, (db_bind_t[]){db_bind_i32(*planet_id)},
                     1, player_id, err)) return 0;

  const char *ship_insert =
    "INSERT INTO ships (name,type_id,sector_id,fighters,onplanet) "
    "SELECT 'qa_transfer_ship_' || p.planet_id::text,st.shiptypes_id,p.sector_id,20,TRUE "
    "FROM planets p CROSS JOIN LATERAL (SELECT shiptypes_id FROM shiptypes WHERE COALESCE(maxfighters,0)>=100 ORDER BY maxfighters LIMIT 1) st "
    "WHERE p.planet_id={1} RETURNING ship_id;";
  if (!returning_i32(db, ship_insert, (db_bind_t[]){db_bind_i32(*planet_id)},
                     1, ship_id, err)) return 0;

  if (!exec_sql(db, "UPDATE players SET ship_id={1}, lastplanet={2} WHERE player_id={3};",
                (db_bind_t[]){db_bind_i32(*ship_id), db_bind_i32(*planet_id), db_bind_i32(*player_id)}, 3, err)
      || !exec_sql(db, "UPDATE planets SET owner_id={1} WHERE planet_id={2};",
                   (db_bind_t[]){db_bind_i32(*player_id), db_bind_i32(*planet_id)}, 2, err)
      || !exec_sql(db, "INSERT INTO citadels (planet_id,level,treasury) VALUES ({1},1,NULL);",
                   (db_bind_t[]){db_bind_i32(*planet_id)}, 1, err)) return 0;

  if (!query_i32(db, "SELECT maxfighters FROM planettypes WHERE code='M';",
                 NULL, 0, planet_capacity, err)
      || !query_i32(db,
                    "SELECT st.maxfighters FROM ships s JOIN shiptypes st ON st.shiptypes_id=s.type_id WHERE s.ship_id={1};",
                    (db_bind_t[]){db_bind_i32(*ship_id)}, 1, ship_capacity, err))
    return 0;
  return *planet_capacity >= 100 && *ship_capacity >= 20;
}

static void cleanup(db_t *db, int planet_id, int player_id, int ship_id)
{
  db_error_t err = {0};
  if (player_id > 0)
    (void) exec_sql(db, "DELETE FROM sector_assets WHERE owner_id={1};",
                    (db_bind_t[]){db_bind_i32(player_id)}, 1, &err);
  memset(&err, 0, sizeof err);
  if (player_id > 0)
    (void) exec_sql(db, "DELETE FROM players WHERE player_id={1};",
                    (db_bind_t[]){db_bind_i32(player_id)}, 1, &err);
  memset(&err, 0, sizeof err);
  if (ship_id > 0)
    (void) exec_sql(db, "DELETE FROM ships WHERE ship_id={1};",
                    (db_bind_t[]){db_bind_i32(ship_id)}, 1, &err);
  memset(&err, 0, sizeof err);
  if (planet_id > 0)
    (void) exec_sql(db, "DELETE FROM planets WHERE planet_id={1};",
                    (db_bind_t[]){db_bind_i32(planet_id)}, 1, &err);
}

static void *transfer_worker(void *arg)
{
  worker_t *worker = arg;
  db_error_t err = {0};
  db_t *db = db_open(&worker->config, &err);
  if (!db) { worker->rc = err.code ? err.code : -1; return NULL; }
  int rc = pthread_barrier_wait(worker->barrier);
  if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD)
    worker->rc = -1;
  else
    {
      if (worker->asset_id > 0)
        worker->rc = db_combat_decrement_or_delete_asset(db,
                                                          worker->asset_id, 7);
      else
        {
          int planet_after = 0, ship_after = 0;
          worker->rc = repo_planets_transfer_fighters(
            db, worker->player_id, worker->ship_id, worker->planet_id,
            "planet_to_ship", 7, &planet_after, &ship_after);
        }
    }
  db_close(db);
  return NULL;
}

typedef struct {
  db_config_t config;
  pthread_barrier_t *barrier;
  int ship_id, sector_id, asset_id, rc;
} hit_worker_t;

static void *entry_hit_worker(void *arg)
{
  hit_worker_t *worker = arg;
  db_error_t err = {0};
  db_t *db = db_open(&worker->config, &err);
  if (!db) { worker->rc = err.code ? err.code : -1; return NULL; }
  int rc = pthread_barrier_wait(worker->barrier);
  if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD)
    worker->rc = -1;
  else
    worker->rc = db_combat_apply_entry_asset_hit(
      db, worker->ship_id, worker->sector_id, worker->asset_id, 1,
      100, 50, 20, 90, 40, 15, 7);
  db_close(db);
  return NULL;
}

static int fail(const char *stage, db_error_t *err)
{
  fprintf(stderr, "FAIL: %s", stage);
  if (err && err->message[0])
    fprintf(stderr, " (backend code %d: %s)", err->backend_code, err->message);
  fputc('\n', stderr);
  return 1;
}

int main(void)
{
  const char *database = getenv("PGDATABASE");
  if (!database || (strncmp(database, "twclone_rc2_", 12) != 0
                    && strncmp(database, "twclone_qa_", 11) != 0))
    { fputs("REFUSED: PGDATABASE must name an isolated twclone_rc2_* or twclone_qa_* database\n", stderr); return 77; }

  db_config_t config = {0};
  config.backend = DB_BACKEND_POSTGRES;
  config.pg_conninfo = "";
  config.connect_timeout_ms = 5000;
  db_error_t err = {0};
  db_t *db = db_open(&config, &err);
  if (!db) return fail("connect to isolated PostgreSQL", &err);

  int planet_id = 0, player_id = 0, ship_id = 0;
  int planet_capacity = 0, ship_capacity = 0, status = 1;
  int event_id = 0;
  if (!create_fixture(db, &planet_id, &player_id, &ship_id,
                      &planet_capacity, &ship_capacity, &err))
    { status = fail("create QA transfer fixtures", &err); goto done; }

  int32_t level = 0, target = 0;
  int64_t start = -1, end = -1;
  char construction_status[32] = {0};
  if (repo_citadel_get_construction_state(db, planet_id, &level,
      construction_status, sizeof construction_status, &target, &start, &end) != 0
      || level != 1 || strcmp(construction_status, "idle") != 0
      || target != 0 || start != 0 || end != 0)
    { status = fail("citadel construction lifecycle state read", NULL); goto done; }

  int p_after = -1, s_after = -1;
  if (repo_planets_transfer_fighters(db, player_id, ship_id, planet_id,
      "ship_to_planet", 10, &p_after, &s_after) != 0
      || p_after != 60 || s_after != 10
      || repo_planets_transfer_fighters(db, player_id, ship_id, planet_id,
      "planet_to_ship", 10, &p_after, &s_after) != 0
      || p_after != 50 || s_after != 20)
    { status = fail("bidirectional atomic fighter transfer", NULL); goto done; }
  db_close(db);
  db = db_open(&config, &err);
  if (!db || !check_fighters(db, planet_id, ship_id, 50, 20, &err))
    { status = fail("fighter transfer persistence after reconnect", &err); goto done; }

  if (!exec_sql(db, "UPDATE ships SET fighters=20,mines=5,limpets=7 WHERE ship_id={1};",
      (db_bind_t[]){db_bind_i32(ship_id)}, 1, &err)
      || db_combat_consume_ship_fighters(db, ship_id, 3) != 0
      || db_combat_consume_ship_fighters(db, ship_id, 18) == 0
      || db_combat_consume_ship_mines(db, ship_id, 1, 2) != 0
      || db_combat_consume_ship_mines(db, ship_id, 4, 3) != 0
      || db_combat_consume_ship_mines(db, ship_id, 3, 1) == 0)
    { status = fail("atomic ship ammunition consumption by asset type", &err); goto done; }
  int ship_fighters = 0, ship_mines = 0, ship_limpets = 0;
  if (!query_i32(db, "SELECT fighters FROM ships WHERE ship_id={1};",
      (db_bind_t[]){db_bind_i32(ship_id)}, 1, &ship_fighters, &err)
      || !query_i32(db, "SELECT mines FROM ships WHERE ship_id={1};",
      (db_bind_t[]){db_bind_i32(ship_id)}, 1, &ship_mines, &err)
      || !query_i32(db, "SELECT limpets FROM ships WHERE ship_id={1};",
      (db_bind_t[]){db_bind_i32(ship_id)}, 1, &ship_limpets, &err)
      || ship_fighters != 17 || ship_mines != 3 || ship_limpets != 4)
    { status = fail("fighter/mine/limpet pools remain distinct", &err); goto done; }
  if (!exec_sql(db, "UPDATE ships SET fighters=20,mines=5,limpets=7 WHERE ship_id={1};",
      (db_bind_t[]){db_bind_i32(ship_id)}, 1, &err))
    { status = fail("restore ship transfer fixture", &err); goto done; }

  if (repo_planets_transfer_fighters(db, player_id, ship_id, planet_id,
      "planet_to_ship", ship_capacity, &p_after, &s_after) == 0
      || repo_planets_transfer_fighters(db, player_id, ship_id, planet_id,
      "ship_to_planet", 21, &p_after, &s_after) == 0
      || !check_fighters(db, planet_id, ship_id, 50, 20, &err))
    { status = fail("reject insufficient fighter quantities", NULL); goto done; }

  if (!exec_sql(db, "UPDATE ships SET fighters={1} WHERE ship_id={2};",
      (db_bind_t[]){db_bind_i32(ship_capacity), db_bind_i32(ship_id)}, 2, &err)
      || repo_planets_transfer_fighters(db, player_id, ship_id, planet_id,
        "planet_to_ship", 1, &p_after, &s_after) == 0
      || !check_fighters(db, planet_id, ship_id, 50, ship_capacity, &err)
      || !exec_sql(db, "UPDATE ships SET fighters=20 WHERE ship_id={1};",
      (db_bind_t[]){db_bind_i32(ship_id)}, 1, &err)
      || !exec_sql(db, "UPDATE planets SET fighters={1} WHERE planet_id={2};",
      (db_bind_t[]){db_bind_i32(planet_capacity), db_bind_i32(planet_id)}, 2, &err)
      || repo_planets_transfer_fighters(db, player_id, ship_id, planet_id,
        "ship_to_planet", 1, &p_after, &s_after) == 0
      || !check_fighters(db, planet_id, ship_id, planet_capacity, 20, &err)
      || !exec_sql(db, "UPDATE planets SET fighters=50 WHERE planet_id={1};",
      (db_bind_t[]){db_bind_i32(planet_id)}, 1, &err))
    { status = fail("reject transfers above ship and planet capacities", &err); goto done; }

  if (!exec_sql(db, "UPDATE planets SET owner_id={1} WHERE planet_id={2};",
      (db_bind_t[]){db_bind_i32(player_id + 100000), db_bind_i32(planet_id)}, 2, &err)
      || repo_planets_transfer_fighters(db, player_id, ship_id, planet_id,
        "ship_to_planet", 1, &p_after, &s_after) == 0
      || !exec_sql(db, "UPDATE planets SET owner_id={1} WHERE planet_id={2};",
      (db_bind_t[]){db_bind_i32(player_id), db_bind_i32(planet_id)}, 2, &err)
      || !exec_sql(db, "UPDATE ships SET onplanet=FALSE WHERE ship_id={1};",
      (db_bind_t[]){db_bind_i32(ship_id)}, 1, &err)
      || repo_planets_transfer_fighters(db, player_id, ship_id, planet_id,
        "ship_to_planet", 1, &p_after, &s_after) == 0
      || !exec_sql(db, "UPDATE ships SET onplanet=TRUE WHERE ship_id={1};",
      (db_bind_t[]){db_bind_i32(ship_id)}, 1, &err))
    { status = fail("reject unauthorized and unlanded transfers", &err); goto done; }
  if (!check_fighters(db, planet_id, ship_id, 50, 20, &err))
    { status = fail("rejected authorization and landing checks are atomic", &err); goto done; }

  int sector_id = 0, asset_id = 0, asset_quantity = 0;
  if (!query_i32(db, "SELECT sector_id FROM planets WHERE planet_id={1};",
      (db_bind_t[]){db_bind_i32(planet_id)}, 1, &sector_id, &err)
      || !returning_i32(db,
        "INSERT INTO sector_assets (sector_id,owner_id,asset_type,quantity) VALUES ({1},{2},1,5) RETURNING sector_assets_id;",
        (db_bind_t[]){db_bind_i32(sector_id),db_bind_i32(player_id)}, 2,
        &asset_id, &err)
      || db_combat_decrement_or_delete_asset(db, asset_id, 2) != 0
      || !query_i32(db, "SELECT quantity FROM sector_assets WHERE sector_assets_id={1};",
        (db_bind_t[]){db_bind_i32(asset_id)}, 1, &asset_quantity, &err)
      || asset_quantity != 3
      || db_combat_decrement_or_delete_asset(db, asset_id, 3) != 0
      || db_combat_decrement_or_delete_asset(db, asset_id, 1) == 0)
    { status = fail("asset depletion consumes exact quantity and deletes at zero", &err); goto done; }

  if (!returning_i32(db,
        "INSERT INTO sector_assets (sector_id,owner_id,asset_type,quantity) VALUES ({1},{2},1,10) RETURNING sector_assets_id;",
        (db_bind_t[]){db_bind_i32(sector_id),db_bind_i32(player_id)}, 2,
        &asset_id, &err))
    { status = fail("create competing hazard stack", &err); goto done; }
  pthread_barrier_t asset_barrier;
  if (pthread_barrier_init(&asset_barrier, NULL, 2) != 0)
    { status = fail("initialize concurrent asset barrier", NULL); goto done; }
  worker_t asset_workers[2] = {
    {config, &asset_barrier, player_id, ship_id, planet_id, -1, asset_id},
    {config, &asset_barrier, player_id, ship_id, planet_id, -1, asset_id}
  };
  pthread_t asset_threads[2];
  int asset_created0 = pthread_create(&asset_threads[0], NULL, transfer_worker, &asset_workers[0]) == 0;
  int asset_created1 = asset_created0 && pthread_create(&asset_threads[1], NULL, transfer_worker, &asset_workers[1]) == 0;
  if (!asset_created0 || !asset_created1)
    {
      if (asset_created0)
        { (void) pthread_barrier_wait(&asset_barrier); pthread_join(asset_threads[0], NULL); }
      pthread_barrier_destroy(&asset_barrier);
      status = fail("start competing hazard consumers", NULL);
      goto done;
    }
  pthread_join(asset_threads[0], NULL);
  pthread_join(asset_threads[1], NULL);
  pthread_barrier_destroy(&asset_barrier);
  int asset_successes = (asset_workers[0].rc == 0) + (asset_workers[1].rc == 0);
  if (asset_successes != 1
      || !query_i32(db, "SELECT quantity FROM sector_assets WHERE sector_assets_id={1};",
        (db_bind_t[]){db_bind_i32(asset_id)}, 1, &asset_quantity, &err)
      || asset_quantity != 3)
    { status = fail("competing hazards cannot consume the same asset units", &err); goto done; }

  if (!exec_sql(db, "UPDATE ships SET hull=100,shields=50,fighters=20 WHERE ship_id={1};",
      (db_bind_t[]){db_bind_i32(ship_id)}, 1, &err)
      || !returning_i32(db,
        "INSERT INTO sector_assets (sector_id,owner_id,asset_type,quantity) VALUES ({1},{2},1,10) RETURNING sector_assets_id;",
        (db_bind_t[]){db_bind_i32(sector_id),db_bind_i32(player_id)}, 2,
        &asset_id, &err))
    { status = fail("prepare atomic ship damage and mine consumption", &err); goto done; }
  pthread_barrier_t hit_barrier;
  if (pthread_barrier_init(&hit_barrier, NULL, 2) != 0)
    { status = fail("initialize concurrent entry-hit barrier", NULL); goto done; }
  hit_worker_t hit_workers[2] = {
    {config, &hit_barrier, ship_id, sector_id, asset_id, -1},
    {config, &hit_barrier, ship_id, sector_id, asset_id, -1}
  };
  pthread_t hit_threads[2];
  int hit_created0 = pthread_create(&hit_threads[0], NULL, entry_hit_worker, &hit_workers[0]) == 0;
  int hit_created1 = hit_created0 && pthread_create(&hit_threads[1], NULL, entry_hit_worker, &hit_workers[1]) == 0;
  if (!hit_created0 || !hit_created1)
    {
      if (hit_created0)
        { (void) pthread_barrier_wait(&hit_barrier); pthread_join(hit_threads[0], NULL); }
      pthread_barrier_destroy(&hit_barrier);
      status = fail("start concurrent entry-hit workers", NULL);
      goto done;
    }
  pthread_join(hit_threads[0], NULL);
  pthread_join(hit_threads[1], NULL);
  pthread_barrier_destroy(&hit_barrier);
  int hit_successes = (hit_workers[0].rc == 0) + (hit_workers[1].rc == 0);
  if (hit_successes != 1
      || !check_fighters(db, planet_id, ship_id, 50, 15, &err))
    { fprintf(stderr, "entry-hit race returned %d and %d\n", hit_workers[0].rc, hit_workers[1].rc);
      status = fail("concurrent entry damage and asset consumption commit once", &err); goto done; }
  int hull_after = 0, shields_after = 0;
  if (!query_i32(db, "SELECT hull FROM ships WHERE ship_id={1};",
      (db_bind_t[]){db_bind_i32(ship_id)}, 1, &hull_after, &err)
      || !query_i32(db, "SELECT shields FROM ships WHERE ship_id={1};",
      (db_bind_t[]){db_bind_i32(ship_id)}, 1, &shields_after, &err)
      || hull_after != 90 || shields_after != 40
      || !query_i32(db, "SELECT quantity FROM sector_assets WHERE sector_assets_id={1};",
      (db_bind_t[]){db_bind_i32(asset_id)}, 1, &asset_quantity, &err)
      || asset_quantity != 3)
    { status = fail("entry-hit snapshot and asset quantity persist atomically", &err); goto done; }

  int limpet_asset1 = 0, limpet_asset2 = 0, limpet_count = 0;
  if (!returning_i32(db,
        "INSERT INTO sector_assets (sector_id,owner_id,asset_type,quantity) VALUES ({1},{2},4,5) RETURNING sector_assets_id;",
        (db_bind_t[]){db_bind_i32(sector_id),db_bind_i32(player_id)}, 2,
        &limpet_asset1, &err)
      || !returning_i32(db,
        "INSERT INTO sector_assets (sector_id,owner_id,asset_type,quantity) VALUES ({1},{2},4,3) RETURNING sector_assets_id;",
        (db_bind_t[]){db_bind_i32(sector_id),db_bind_i32(player_id)}, 2,
        &limpet_asset2, &err)
      || db_combat_attach_limpet_from_asset(db, ship_id, player_id,
          limpet_asset1, 1700000000) != 0
      || db_combat_attach_limpet_from_asset(db, ship_id, player_id,
          limpet_asset2, 1700000001) != 1
      || !query_i32(db, "SELECT COUNT(*) FROM limpet_attached WHERE ship_id={1} AND owner_player_id={2};",
          (db_bind_t[]){db_bind_i32(ship_id),db_bind_i32(player_id)}, 2,
          &limpet_count, &err)
      || limpet_count != 1
      || !query_i32(db, "SELECT quantity FROM sector_assets WHERE sector_assets_id={1};",
          (db_bind_t[]){db_bind_i32(limpet_asset2)}, 1,
          &asset_quantity, &err)
      || asset_quantity != 3)
    { status = fail("limpet attachment consumes once and preserves duplicate stack", &err); goto done; }

  if (db_combat_remove_all_limpets_from_ship(db, ship_id) != 0
      || !query_i32(db, "SELECT COUNT(*) FROM limpet_attached WHERE ship_id={1};",
          (db_bind_t[]){db_bind_i32(ship_id)}, 1, &limpet_count, &err)
      || limpet_count != 0
      || db_combat_remove_all_limpets_from_ship(db, ship_id) != 0)
    { status = fail("limpet cleanup is complete and idempotent", &err); goto done; }

  if (!exec_sql(db, "UPDATE planets SET fighters=10 WHERE planet_id={1};",
      (db_bind_t[]){db_bind_i32(planet_id)}, 1, &err)
      || !exec_sql(db, "UPDATE ships SET fighters=0 WHERE ship_id={1};",
      (db_bind_t[]){db_bind_i32(ship_id)}, 1, &err))
    { status = fail("prepare concurrent transfer contention fixture", &err); goto done; }
  pthread_barrier_t barrier;
  if (pthread_barrier_init(&barrier, NULL, 2) != 0)
    { status = fail("initialize concurrent transfer barrier", NULL); goto done; }
  worker_t workers[2] = {
    {config, &barrier, player_id, ship_id, planet_id, -1, 0},
    {config, &barrier, player_id, ship_id, planet_id, -1, 0}
  };
  pthread_t threads[2];
  int created0 = pthread_create(&threads[0], NULL, transfer_worker, &workers[0]) == 0;
  int created1 = created0 && pthread_create(&threads[1], NULL, transfer_worker, &workers[1]) == 0;
  if (!created0 || !created1)
    {
      /* Release and join a worker already waiting at the two-party barrier. */
      if (created0)
        {
          (void) pthread_barrier_wait(&barrier);
          pthread_join(threads[0], NULL);
        }
      pthread_barrier_destroy(&barrier);
      status = fail("start concurrent transfer workers", NULL);
      goto done;
    }
  pthread_join(threads[0], NULL);
  pthread_join(threads[1], NULL);
  pthread_barrier_destroy(&barrier);
  int successes = (workers[0].rc == 0) + (workers[1].rc == 0);
  int final_planet = 0, final_ship = 0;
  if (successes != 1
      || !query_i32(db, "SELECT fighters FROM planets WHERE planet_id={1};",
         (db_bind_t[]){db_bind_i32(planet_id)}, 1, &final_planet, &err)
      || !query_i32(db, "SELECT fighters FROM ships WHERE ship_id={1};",
         (db_bind_t[]){db_bind_i32(ship_id)}, 1, &final_ship, &err)
      || final_planet != 3 || final_ship != 7)
    { status = fail("concurrent transfer consumes assets exactly once", &err); goto done; }

  if (db_planets_destroy(db, player_id + 100000, planet_id)
      == 0
      || !query_i32(db, "SELECT COUNT(*) FROM planets WHERE planet_id={1};",
        (db_bind_t[]){db_bind_i32(planet_id)}, 1, &final_planet, &err)
      || final_planet != 1)
    { status = fail("reject planet destruction by non-owner", &err); goto done; }
  if (!exec_sql(db,
        "INSERT INTO planet_goods (planet_id,commodity,quantity,max_capacity,production_rate) VALUES ({1},'ORE',2,100,0);",
        (db_bind_t[]){db_bind_i32(planet_id)}, 1, &err)
      || !exec_sql(db,
        "INSERT INTO entity_stock (entity_type,entity_id,commodity_code,quantity,price) VALUES ('planet',{1},'ORE',2,0);",
        (db_bind_t[]){db_bind_i32(planet_id)}, 1, &err)
      || !returning_i32(db,
        "INSERT INTO engine_events (ts,type,actor_player_id,sector_id,payload) VALUES ({1},'qa.planet.history',{2},{3},{4}) RETURNING engine_events_id;",
        (db_bind_t[]){db_bind_timestamp_text(1700000000),db_bind_i32(player_id),db_bind_i32(sector_id),db_bind_text("{\\\"planet_id\\\":1}")},
        4, &event_id, &err))
    { status = fail("prepare planet destruction dependents and history", &err); goto done; }

  if (db_planets_destroy(db, player_id, planet_id) != 0
      || !query_i32(db, "SELECT COUNT(*) FROM planets WHERE planet_id={1};",
        (db_bind_t[]){db_bind_i32(planet_id)}, 1, &final_planet, &err)
      || final_planet != 0
      || !query_i32(db, "SELECT COUNT(*) FROM citadels WHERE planet_id={1};",
        (db_bind_t[]){db_bind_i32(planet_id)}, 1, &final_ship, &err)
      || final_ship != 0
      || !query_i32(db, "SELECT COUNT(*) FROM planet_goods WHERE planet_id={1};",
        (db_bind_t[]){db_bind_i32(planet_id)}, 1, &final_ship, &err)
      || final_ship != 0
      || !query_i32(db, "SELECT COUNT(*) FROM entity_stock WHERE entity_type='planet' AND entity_id={1};",
        (db_bind_t[]){db_bind_i32(planet_id)}, 1, &final_ship, &err)
      || final_ship != 0
      || !query_i32(db, "SELECT COUNT(*) FROM engine_events WHERE engine_events_id={1};",
        (db_bind_t[]){db_bind_i32(event_id)}, 1, &final_ship, &err)
      || final_ship != 1
      || !query_i32(db, "SELECT COUNT(*) FROM players WHERE player_id={1} AND lastplanet IS NULL;",
        (db_bind_t[]){db_bind_i32(player_id)}, 1, &final_ship, &err)
      || final_ship != 1
      || !query_i32(db, "SELECT COUNT(*) FROM ships WHERE ship_id={1} AND onplanet=FALSE;",
        (db_bind_t[]){db_bind_i32(ship_id)}, 1, &final_ship, &err)
      || final_ship != 1)
    { status = fail("planet destruction clears live dependents and retains history", &err); goto done; }

  puts("PASS: planetary read/transfer/destruction persistence, combat asset accounting and concurrent consumption");
  status = 0;

done:
  if (db && event_id > 0)
    (void) exec_sql(db, "DELETE FROM engine_events WHERE engine_events_id={1};",
                    (db_bind_t[]){db_bind_i32(event_id)}, 1, &err);
  if (db) cleanup(db, planet_id, player_id, ship_id);
  if (db) db_close(db);
  return status;
}
