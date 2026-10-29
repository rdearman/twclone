#define TW_DB_INTERNAL 1
#include "db/db_api.h"
#include "db/db_int.h"
#include "db/repo/repo_players.h"
#include "db/repo/repo_universe.h"
#include "db/sql_driver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void *db_mysql_open_internal(db_t *db, const db_config_t *cfg, db_error_t *err)
{ (void)db; (void)cfg; (void)err; return NULL; }
void server_log_printf(int priority, const char *format, ...)
{ (void)priority; (void)format; }

static int
exec_sql(db_t *db, const char *template, const db_bind_t *params,
         size_t count, db_error_t *err)
{
  char sql[1024];
  return sql_build(db, template, sql, sizeof sql) == 0
      && db_exec(db, sql, params, count, err);
}

static int
query_i32(db_t *db, const char *template, const db_bind_t *params,
          size_t count, int *value, db_error_t *err)
{
  char sql[1024];
  db_res_t *res = NULL;
  if (sql_build(db, template, sql, sizeof sql) != 0
      || !db_query(db, sql, params, count, &res, err)
      || !db_res_step(res, err))
    { if (res) db_res_finalize(res); return 0; }
  *value = db_res_col_i32(res, 0, err);
  db_res_finalize(res);
  return err->code == 0;
}

static int
check_warps(db_t *db, int player_id, int current_sector, int expect_1_2,
            int expect_2_3, int expect_3_4, db_error_t *err)
{
  db_res_t *res = repo_universe_get_player_known_warps(
      db, player_id, current_sector, err);
  if (!res) return 0;
  int seen_1_2 = 0, seen_2_3 = 0, seen_3_4 = 0;
  while (db_res_step(res, err))
    {
      int from = db_res_col_i32(res, 0, err);
      int to = db_res_col_i32(res, 1, err);
      if (from == 1 && to == 2) seen_1_2++;
      if (from == 2 && to == 3) seen_2_3++;
      if (from == 3 && to == 4) seen_3_4++;
    }
  db_res_finalize(res);
  return err->code == 0 && seen_1_2 == expect_1_2
      && seen_2_3 == expect_2_3 && seen_3_4 == expect_3_4;
}

static int
fail(const char *stage, const db_error_t *err)
{
  fprintf(stderr, "FAIL: %s", stage);
  if (err && err->message[0])
    fprintf(stderr, " (%d: %s)", err->backend_code, err->message);
  fputc('\n', stderr);
  return 1;
}

int
main(void)
{
  const char *database = getenv("PGDATABASE");
  if (!database || (strncmp(database, "twclone_rc2_", 12) != 0
                    && strncmp(database, "twclone_qa_", 11) != 0))
    { fputs("REFUSED: PGDATABASE must name an isolated QA database\n", stderr); return 77; }

  db_config_t config = {0};
  config.backend = DB_BACKEND_POSTGRES;
  config.pg_conninfo = "";
  config.connect_timeout_ms = 5000;
  db_error_t err = {0};
  db_t *db = db_open(&config, &err);
  if (!db) return fail("connect to isolated PostgreSQL", &err);

  int player_id = 0, ship_id = 0, port1 = 0, port2 = 0;
  char unique[64];
  snprintf(unique, sizeof unique, "qa_info_%ld", (long)getpid());
  db_res_t *res = NULL;
  char sql[1024];
  const char *ship_insert =
      "INSERT INTO ships(name,type_id,sector_id) "
      "SELECT {1},shiptypes_id,1 FROM shiptypes ORDER BY shiptypes_id LIMIT 1 "
      "RETURNING ship_id;";
  if (sql_build(db, ship_insert, sql, sizeof sql) != 0
      || !db_query(db, sql, (db_bind_t[]){db_bind_text(unique)}, 1, &res, &err)
      || !db_res_step(res, &err))
    { if (res) db_res_finalize(res); db_close(db); return fail("create QA ship", &err); }
  ship_id = db_res_col_i32(res, 0, &err);
  db_res_finalize(res);

  const char *player_insert =
      "INSERT INTO players(name,passwd,sector_id,ship_id) VALUES ({1},"
      "'$argon2id$v=19$m=65536,t=3,p=1$QUJDREVGR0hJSktMTU5PUA$QUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVo',1,{2}) "
      "RETURNING player_id;";
  if (sql_build(db, player_insert, sql, sizeof sql) != 0
      || !db_query(db, sql,
           (db_bind_t[]){db_bind_text(unique),db_bind_i32(ship_id)}, 2, &res, &err)
      || !db_res_step(res, &err))
    { if (res) db_res_finalize(res); db_close(db); return fail("create QA player", &err); }
  player_id = db_res_col_i32(res, 0, &err);
  db_res_finalize(res);

  if (!exec_sql(db, "INSERT INTO ports(name,sector_id) VALUES ({1},1) RETURNING port_id;",
                (db_bind_t[]){db_bind_text("qa_info_port_1")}, 1, &err))
    { db_close(db); return fail("create local port fixture", &err); }
  if (!exec_sql(db, "INSERT INTO ports(name,sector_id) VALUES ({1},2) RETURNING port_id;",
                (db_bind_t[]){db_bind_text("qa_info_port_2")}, 1, &err))
    { db_close(db); return fail("create remote port fixture", &err); }
  if (!query_i32(db, "SELECT port_id FROM ports WHERE name={1};",
                 (db_bind_t[]){db_bind_text("qa_info_port_1")}, 1, &port1, &err)
      || !query_i32(db, "SELECT port_id FROM ports WHERE name={1};",
                    (db_bind_t[]){db_bind_text("qa_info_port_2")}, 1, &port2, &err))
    { db_close(db); return fail("read port fixture IDs", &err); }

  if (!exec_sql(db,
        "INSERT INTO sector_warps(from_sector,to_sector) VALUES "
        "(1,2),(2,3),(3,4) ON CONFLICT DO NOTHING;", NULL, 0, &err))
    { db_close(db); return fail("create directed warp fixtures", &err); }

  int visit_count = -1, knowledge_count = -1;
  if (repo_players_record_visit(db, player_id, 1) != 0
      || repo_players_record_visit(db, player_id, 2) != 0
      || repo_players_record_port_knowledge(db, player_id, port1) != 0
      || repo_players_record_port_knowledge(db, player_id, port2) != 0
      || !query_i32(db, "SELECT COUNT(*) FROM player_visited_sectors WHERE player_id={1};",
                    (db_bind_t[]){db_bind_i32(player_id)}, 1, &visit_count, &err)
      || visit_count != 1
      || !query_i32(db, "SELECT COUNT(*) FROM player_known_ports WHERE player_id={1};",
                    (db_bind_t[]){db_bind_i32(player_id)}, 1, &knowledge_count, &err)
      || knowledge_count != 1
      || !check_warps(db, player_id, 1, 1, 0, 0, &err))
    { db_close(db); return fail("remote visit/port write denied and unseen warp exits withheld", &err); }

  if (!exec_sql(db, "UPDATE players SET sector_id=2 WHERE player_id={1};",
                (db_bind_t[]){db_bind_i32(player_id)}, 1, &err)
      || !exec_sql(db, "UPDATE ships SET sector_id=2 WHERE ship_id={1};",
                   (db_bind_t[]){db_bind_i32(ship_id)}, 1, &err)
      || repo_players_record_visit(db, player_id, 2) != 0
      || repo_players_record_port_knowledge(db, player_id, port2) != 0
      || !query_i32(db, "SELECT COUNT(*) FROM player_visited_sectors WHERE player_id={1};",
                    (db_bind_t[]){db_bind_i32(player_id)}, 1, &visit_count, &err)
      || visit_count != 2
      || !query_i32(db, "SELECT COUNT(*) FROM player_known_ports WHERE player_id={1};",
                    (db_bind_t[]){db_bind_i32(player_id)}, 1, &knowledge_count, &err)
      || knowledge_count != 2
      || !check_warps(db, player_id, 2, 1, 1, 0, &err))
    { db_close(db); return fail("legitimate presence records knowledge and only that sector's exits", &err); }

  (void) port1;
  (void) port2;
  (void) player_id;
  (void) ship_id;
  db_close(db);
  puts("PASS: player-scoped visit/port knowledge writes and directed warp visibility");
  return 0;
}
