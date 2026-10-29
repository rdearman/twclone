#define TW_DB_INTERNAL 1
#include "db/db_api.h"
#include "db/db_int.h"
#include "db/repo/repo_citadel.h"
#include "db/repo/repo_planets.h"
#include "db/sql_driver.h"
#include "errors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

typedef struct
{
  db_config_t config;
  pthread_barrier_t *barrier;
  int planet_id;
  int target_level;
  int rc;
} citadel_start_worker_t;

static void *
start_citadel_upgrade_worker (void *arg)
{
  citadel_start_worker_t *worker = arg;
  db_error_t err = {0};
  db_t *db = db_open (&worker->config, &err);
  bool tx_started = db && db_tx_begin (db, DB_TX_DEFAULT, &err);
  (void) pthread_barrier_wait (worker->barrier);
  if (!tx_started)
    {
      worker->rc = err.code ? err.code : ERR_DB_TX;
      if (db) db_close (db);
      return NULL;
    }
  worker->rc = repo_citadel_deduct_resources (db, 1, 1, 1, 1,
                                                worker->planet_id);
  if (worker->rc == 0)
    worker->rc = repo_citadel_start_construction (
        db, worker->planet_id, 1, 1, worker->target_level,
        100 + worker->target_level, 100000 + worker->target_level);

  if (worker->rc == 0)
    {
      if (!db_tx_commit (db, &err))
        {
          worker->rc = err.code ? err.code : ERR_DB_TX;
          (void) db_tx_rollback (db, NULL);
        }
    }
  else
    (void) db_tx_rollback (db, NULL);
  db_close (db);
  return NULL;
}

static int
race_citadel_upgrades (const db_config_t *config, int planet_id,
                       int *winning_target, db_error_t *err)
{
  db_t *setup = db_open (config, err);
  if (!setup)
    return 0;
  char sql[512];
  if (sql_build (setup,
        "UPDATE planets SET colonists_unassigned=10 WHERE planet_id={1};",
        sql, sizeof sql) != 0
      || !db_exec (setup, sql, (db_bind_t[]){db_bind_i32 (planet_id)}, 1, err))
    { db_close (setup); return 0; }

  const char *commodities[] = {"ORE", "ORG", "EQU"};
  for (size_t i = 0; i < sizeof commodities / sizeof commodities[0]; i++)
    {
      const char *seed =
        "INSERT INTO entity_stock (entity_type,entity_id,commodity_code,quantity) "
        "VALUES ('planet',{1},{2},5) ON CONFLICT (entity_type,entity_id,commodity_code) "
        "DO UPDATE SET quantity=EXCLUDED.quantity;";
      if (sql_build (setup, seed, sql, sizeof sql) != 0
          || !db_exec (setup, sql,
               (db_bind_t[]){db_bind_i32 (planet_id),
                             db_bind_text (commodities[i])}, 2, err))
        { db_close (setup); return 0; }
    }
  db_close (setup);

  pthread_barrier_t barrier;
  if (pthread_barrier_init (&barrier, NULL, 2) != 0)
    return 0;
  citadel_start_worker_t workers[2] = {
    {*config, &barrier, planet_id, 2, -1},
    {*config, &barrier, planet_id, 3, -1}
  };
  pthread_t threads[2];
  int created = 0;
  for (; created < 2; created++)
    if (pthread_create (&threads[created], NULL,
                        start_citadel_upgrade_worker, &workers[created]) != 0)
      break;
  if (created != 2)
    {
      /* Do not strand a worker at the barrier if thread creation fails. */
      if (created == 1)
        {
          (void) pthread_barrier_wait (&barrier);
          (void) pthread_join (threads[0], NULL);
        }
      pthread_barrier_destroy (&barrier);
      return 0;
    }
  (void) pthread_join (threads[0], NULL);
  (void) pthread_join (threads[1], NULL);
  pthread_barrier_destroy (&barrier);

  if ((workers[0].rc == 0) == (workers[1].rc == 0))
    {
      fprintf (stderr, "citadel race returned %d and %d\n",
               workers[0].rc, workers[1].rc);
      return 0;
    }
  *winning_target = workers[0].rc == 0 ? workers[0].target_level
                                       : workers[1].target_level;
  if (workers[0].rc != ERR_DB_BUSY && workers[1].rc != ERR_DB_BUSY)
    fprintf (stderr, "citadel race returned %d and %d (expected busy loser)\n",
             workers[0].rc, workers[1].rc);
  return workers[0].rc == ERR_DB_BUSY || workers[1].rc == ERR_DB_BUSY;
}

void *db_mysql_open_internal(db_t *db, const db_config_t *cfg, db_error_t *err)
{
  (void) db; (void) cfg; (void) err;
  return NULL;
}

void server_log_printf(int priority, const char *format, ...)
{
  (void) priority; (void) format;
}

static int fail(const char *where, db_error_t *err)
{
  fprintf(stderr, "FAIL: %s", where);
  if (err && err->message[0])
    fprintf(stderr, " (backend code %d: %s)", err->backend_code, err->message);
  fputc('\n', stderr);
  return 1;
}

static int query_i64(db_t *db, const char *query, const db_bind_t *params,
                     size_t n_params, int64_t *out, db_error_t *err)
{
  char sql[512];
  db_res_t *res = NULL;
  if (sql_build(db, query, sql, sizeof sql) != 0
      || !db_query(db, sql, params, n_params, &res, err)
      || !db_res_step(res, err))
    {
      if (res) db_res_finalize(res);
      return 0;
    }
  *out = db_res_col_i64(res, 0, err);
  db_res_finalize(res);
  return err->code == 0;
}

static int create_fixture(db_t *db, int *planet_id, int *ship_id,
                          db_error_t *err)
{
  char sql[1024];
  db_res_t *res = NULL;
  const char *planet_q =
    "INSERT INTO planets (sector_id,name,owner_id,owner_type,class,population,type,created_by,fighters) "
    "SELECT s.sector_id,'qa_planetary_repo_' || nextval('planets_planet_id_seq')::text,0,'player','M',0,"
    "(SELECT planettypes_id FROM planettypes ORDER BY planettypes_id LIMIT 1),0,50 "
    "FROM sectors s ORDER BY s.sector_id LIMIT 1 RETURNING planet_id;";
  if (sql_build(db, planet_q, sql, sizeof sql) != 0
      || !db_query(db, sql, NULL, 0, &res, err) || !db_res_step(res, err))
    {
      if (res) db_res_finalize(res);
      return 0;
    }
  *planet_id = db_res_col_i32(res, 0, err);
  db_res_finalize(res);

  const char *citadel_q =
    "INSERT INTO citadels (planet_id,level,treasury) VALUES ({1},1,NULL);";
  if (sql_build(db, citadel_q, sql, sizeof sql) != 0
      || !db_exec(db, sql, (db_bind_t[]){db_bind_i64(*planet_id)}, 1, err))
    return 0;

  const char *ship_q =
    "INSERT INTO ships (name,type_id,sector_id,fighters) "
    "SELECT 'qa_planetary_ship_' || {1}::text,"
    "(SELECT shiptypes_id FROM shiptypes ORDER BY shiptypes_id LIMIT 1),sector_id,20 "
    "FROM planets WHERE planet_id={1} RETURNING ship_id;";
  if (sql_build(db, ship_q, sql, sizeof sql) != 0
      || !db_query(db, sql, (db_bind_t[]){db_bind_i64(*planet_id)}, 1, &res, err)
      || !db_res_step(res, err))
    {
      if (res) db_res_finalize(res);
      return 0;
    }
  *ship_id = db_res_col_i32(res, 0, err);
  db_res_finalize(res);
  return err->code == 0;
}

static void cleanup_fixture(db_t *db, int planet_id, int ship_id)
{
  db_error_t err = {0};
  char sql[256];
  if (ship_id > 0 && sql_build(db, "DELETE FROM ships WHERE ship_id={1};",
                                sql, sizeof sql) == 0)
    (void) db_exec(db, sql, (db_bind_t[]){db_bind_i64(ship_id)}, 1, &err);
  if (planet_id > 0)
    {
      memset(&err, 0, sizeof err);
      if (sql_build(db, "DELETE FROM entity_stock WHERE entity_type='planet' AND entity_id={1};",
                    sql, sizeof sql) == 0)
        (void) db_exec(db, sql, (db_bind_t[]){db_bind_i64(planet_id)}, 1, &err);
      memset(&err, 0, sizeof err);
      if (sql_build(db, "DELETE FROM planets WHERE planet_id={1};",
                    sql, sizeof sql) == 0)
        (void) db_exec(db, sql, (db_bind_t[]){db_bind_i64(planet_id)}, 1, &err);
    }
}

int main(void)
{
  const char *database = getenv("PGDATABASE");
  if (!database || (strncmp(database, "twclone_rc2_", 12) != 0
                    && strncmp(database, "twclone_qa_", 11) != 0))
    {
      fputs("REFUSED: PGDATABASE must name an isolated twclone_rc2_* or twclone_qa_* database\n", stderr);
      return 77;
    }

  db_config_t config = {0};
  config.backend = DB_BACKEND_POSTGRES;
  config.pg_conninfo = "";
  config.connect_timeout_ms = 5000;
  db_error_t err = {0};
  db_t *db = db_open(&config, &err);
  if (!db) return fail("connect to isolated PostgreSQL", &err);

  int planet_id = 0, ship_id = 0;
  int planet_without_citadel = 0, unused_ship = 0;
  int status = 1;
  if (!create_fixture(db, &planet_id, &ship_id, &err))
    { status = fail("create isolated planet/citadel/ship fixtures", &err); goto done; }

  bool defenses_active = true;
  int defense_level = -1, defense_shields = -1, defense_reaction = -1;
  if (db_planets_get_citadel_defenses_ex(db, planet_id, &defenses_active,
                                         &defense_level, &defense_shields,
                                         &defense_reaction) != 0
      || defenses_active || defense_level != 1 || defense_shields != 0
      || defense_reaction != 0)
    { status = fail("distinguish an unarmed citadel from query failure", NULL); goto done; }

  char shield_sql[256];
  if (sql_build(db, "UPDATE citadels SET planetaryShields=5 WHERE planet_id={1};",
                shield_sql, sizeof shield_sql) != 0
      || !db_exec(db, shield_sql, (db_bind_t[]){db_bind_i64(planet_id)}, 1, &err)
      || db_planets_get_citadel_defenses_ex(db, planet_id, &defenses_active,
                                            &defense_level, &defense_shields,
                                            &defense_reaction) != 0
      || !defenses_active || defense_shields != 5)
    { status = fail("read active citadel defense", &err); goto done; }

  int shields_remaining = -1;
  if (db_planets_consume_citadel_shields(db, planet_id, 5, 2,
                                         &shields_remaining) != 0
      || shields_remaining != 3
      || db_planets_consume_citadel_shields(db, planet_id, 5, 2,
                                            &shields_remaining) != ERR_DB_BUSY
      || db_planets_consume_citadel_shields(db, planet_id, 3, 4,
                                            &shields_remaining) != ERR_INVALID_ARG)
    { status = fail("atomic shield consumption, stale snapshot and bounds", NULL); goto done; }

  if (!create_fixture(db, &planet_without_citadel, &unused_ship, &err))
    { status = fail("create no-citadel defense fixture", &err); goto done; }
  if (sql_build(db, "DELETE FROM citadels WHERE planet_id={1};",
                shield_sql, sizeof shield_sql) != 0
      || !db_exec(db, shield_sql,
                  (db_bind_t[]){db_bind_i64(planet_without_citadel)}, 1, &err)
      || db_planets_get_citadel_defenses_ex(db, planet_without_citadel,
                                            &defenses_active, &defense_level,
                                            &defense_shields,
                                            &defense_reaction) != 0
      || defenses_active || defense_level || defense_shields || defense_reaction)
    { status = fail("no active citadel defense is distinct from query failure", &err); goto done; }

  if (db_planets_add_treasury(db, planet_id, 100) != 0
      || db_planets_add_treasury_buy(db, planet_id, 25) != 0
      || db_planets_deduct_treasury(db, planet_id, 80) != 0)
    { status = fail("initialize, add and deduct treasury", NULL); goto done; }

  if (db_planets_deduct_treasury(db, planet_id, 46) == 0)
    { status = fail("reject treasury overdraw", NULL); goto done; }

  int64_t value = -1;
  if (!query_i64(db, "SELECT treasury FROM citadels WHERE planet_id={1};",
                 (db_bind_t[]){db_bind_i64(planet_id)}, 1, &value, &err)
      || value != 45)
    { status = fail("treasury remains 45 after rejected overdraw", &err); goto done; }

  if (!db_tx_begin(db, DB_TX_DEFAULT, &err)
      || db_planets_deduct_treasury(db, planet_id, 10) != 0
      || db_planets_lose_fighters(db, planet_id, 51) == 0
      || !db_tx_rollback(db, &err))
    { status = fail("rollback combined treasury/fighter operation", &err); goto done; }

  if (!query_i64(db, "SELECT treasury FROM citadels WHERE planet_id={1};",
                 (db_bind_t[]){db_bind_i64(planet_id)}, 1, &value, &err)
      || value != 45)
    { status = fail("rollback restores treasury", &err); goto done; }

  if (db_planets_lose_fighters(db, planet_id, 51) == 0
      || !query_i64(db, "SELECT fighters FROM planets WHERE planet_id={1};",
                    (db_bind_t[]){db_bind_i64(planet_id)}, 1, &value, &err)
      || value != 50
      || db_planets_lose_fighters(db, planet_id, 20) != 0
      || db_planets_update_ship_fighters(db, ship_id, 21) == 0
      || db_planets_update_ship_fighters(db, ship_id, 20) != 0)
    { status = fail("fighter debit bounds", &err); goto done; }

  db_close(db);
  db = db_open(&config, &err);
  if (!db) return fail("reconnect for persistence check", &err);
  if (!query_i64(db, "SELECT treasury FROM citadels WHERE planet_id={1};",
                 (db_bind_t[]){db_bind_i64(planet_id)}, 1, &value, &err)
      || value != 45
      || !query_i64(db, "SELECT fighters FROM planets WHERE planet_id={1};",
                    (db_bind_t[]){db_bind_i64(planet_id)}, 1, &value, &err)
      || value != 30
      || !query_i64(db, "SELECT fighters FROM ships WHERE ship_id={1};",
                    (db_bind_t[]){db_bind_i64(ship_id)}, 1, &value, &err)
      || value != 0)
    { status = fail("treasury and fighter persistence after reconnect", &err); goto done; }

  int winning_target = 0;
  if (!race_citadel_upgrades (&config, planet_id, &winning_target, &err))
    { status = fail("one concurrent citadel start wins and loser rolls back", &err); goto done; }

  int32_t level = 0, target_level = 0;
  int64_t started = 0, completes = 0;
  char construction_status[32] = {0};
  if (repo_citadel_get_construction_state (db, planet_id, &level,
        construction_status, sizeof construction_status, &target_level,
        &started, &completes) != 0
      || level != 1 || target_level != winning_target
      || strcmp (construction_status, "upgrading") != 0
      || started != 100 + winning_target
      || completes != 100000 + winning_target
      || repo_citadel_start_construction (db, planet_id, 1, 1,
            winning_target == 2 ? 3 : 2, 200, 200000) != ERR_DB_BUSY)
    { status = fail("citadel upgrade compare-and-set preserves winner timer", &err); goto done; }

  const char *stock_codes[] = {"ORE", "ORG", "EQU"};
  for (size_t i = 0; i < sizeof stock_codes / sizeof stock_codes[0]; i++)
    if (!query_i64 (db,
          "SELECT quantity FROM entity_stock WHERE entity_type='planet' "
          "AND entity_id={1} AND commodity_code={2};",
          (db_bind_t[]){db_bind_i32 (planet_id), db_bind_text (stock_codes[i])},
          2, &value, &err) || value != 4)
      { status = fail("losing upgrade transaction restores its resource debit", &err); goto done; }
  if (!query_i64 (db,
        "SELECT colonists_unassigned FROM planets WHERE planet_id={1};",
        (db_bind_t[]){db_bind_i32 (planet_id)}, 1, &value, &err)
      || value != 9)
    { status = fail("losing upgrade transaction restores its colonist debit", &err); goto done; }

  puts("PASS: treasury/fighter bounds, reconnect persistence, and concurrent "
       "citadel upgrade compare-and-set with loser rollback");
  status = 0;

done:
  if (db) cleanup_fixture(db, planet_id, ship_id);
  if (db) cleanup_fixture(db, planet_without_citadel, unused_ship);
  if (db) db_close(db);
  return status;
}
