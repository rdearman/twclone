#define TW_DB_INTERNAL 1
#include "db/db_api.h"
#include "db/db_int.h"
#include "db/repo/repo_stardock.h"
#include "db/sql_driver.h"
#include "errors.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *db_mysql_open_internal(db_t *db, const db_config_t *cfg, db_error_t *err)
{ (void)db; (void)cfg; (void)err; return NULL; }
void server_log_printf(int priority, const char *format, ...)
{ (void)priority; (void)format; }

typedef struct { db_config_t config; pthread_barrier_t *barrier; int ship_id; int rc; } worker_t;

static int query_pair(db_t *db, const char *query, const db_bind_t *params,
                      size_t n_params, int *first, int *second, db_error_t *err)
{
  char sql[512]; db_res_t *res = NULL;
  if (sql_build(db, query, sql, sizeof sql) != 0
      || !db_query(db, sql, params, n_params, &res, err)
      || !db_res_step(res, err))
    { if (res) db_res_finalize(res); return 0; }
  *first = db_res_col_i32(res, 0, err);
  *second = db_res_col_i32(res, 1, err);
  db_res_finalize(res);
  return err->code == 0;
}

static void *buy_one_fighter(void *arg)
{
  worker_t *w = arg; db_error_t err = {0};
  db_t *db = db_open(&w->config, &err);
  if (!db) { w->rc = -999; return NULL; }
  pthread_barrier_wait(w->barrier);
  w->rc = repo_stardock_update_ship_hardware(db, "fighters", 1, w->ship_id);
  db_close(db);
  return NULL;
}

static int fail(const char *what, const db_error_t *err)
{
  fprintf(stderr, "FAIL: %s", what);
  if (err && err->message[0]) fprintf(stderr, " (%d: %s)", err->backend_code, err->message);
  fputc('\n', stderr); return 1;
}

int main(void)
{
  const char *database = getenv("PGDATABASE");
  if (!database || (strncmp(database, "twclone_rc2_", 12) != 0
                    && strncmp(database, "twclone_qa_", 11) != 0))
    { fputs("REFUSED: PGDATABASE must name an isolated twclone_rc2_* or twclone_qa_* database\n", stderr); return 77; }
  db_config_t config = {0}; config.backend = DB_BACKEND_POSTGRES; config.pg_conninfo = ""; config.connect_timeout_ms = 5000;
  db_error_t err = {0}; db_t *db = db_open(&config, &err);
  if (!db) return fail("connect to isolated PostgreSQL", &err);
  int type_id = 0, max_fighters = 0, ship_id = 0, sector_id = 0;
  if (!query_pair(db, "SELECT shiptypes_id,maxfighters FROM shiptypes WHERE enabled=TRUE AND maxfighters>1 ORDER BY maxfighters LIMIT 1;",
                  NULL, 0, &type_id, &max_fighters, &err))
    { db_close(db); return fail("find a seed ship type with fighter capacity", &err); }
  char sql[768]; db_res_t *res = NULL;
  const char *insert = "INSERT INTO ships(name,type_id,sector_id,fighters,credits) SELECT 'qa_stardock_atomic_' || {1}::text,{2},sector_id,{3},10 FROM sectors ORDER BY sector_id LIMIT 1 RETURNING ship_id,sector_id;";
  if (sql_build(db, insert, sql, sizeof sql) != 0
      || !db_query(db, sql, (db_bind_t[]){db_bind_i64(max_fighters),db_bind_i64(type_id),db_bind_i64(max_fighters-1)}, 3, &res, &err)
      || !db_res_step(res, &err))
    { if (res) db_res_finalize(res); db_close(db); return fail("create ship fixture", &err); }
  ship_id = db_res_col_i32(res, 0, &err); sector_id = db_res_col_i32(res, 1, &err); db_res_finalize(res);

  pthread_barrier_t barrier;
  int barrier_live = 0, threads_started = 0;
  if (pthread_barrier_init(&barrier, NULL, 2) != 0)
    { fprintf(stderr,"FAIL: initialize concurrent purchase barrier\n"); goto cleanup; }
  barrier_live = 1;
  worker_t workers[2] = {{config,&barrier,ship_id,-1},{config,&barrier,ship_id,-1}};
  pthread_t threads[2];
  if (pthread_create(&threads[0],NULL,buy_one_fighter,&workers[0]) != 0)
    { fprintf(stderr,"FAIL: start first concurrent hardware purchase\n"); pthread_barrier_destroy(&barrier); barrier_live=0; goto cleanup; }
  threads_started = 1;
  if (pthread_create(&threads[1],NULL,buy_one_fighter,&workers[1]) != 0)
    {
      fprintf(stderr,"FAIL: start second concurrent hardware purchase\n");
      pthread_barrier_wait(&barrier);
      pthread_join(threads[0],NULL);
      threads_started = 0;
      pthread_barrier_destroy(&barrier); barrier_live=0; goto cleanup;
    }
  pthread_join(threads[0],NULL); pthread_join(threads[1],NULL); pthread_barrier_destroy(&barrier);
  barrier_live = 0; threads_started = 0;
  if (!((workers[0].rc == 0 && workers[1].rc == ERR_DB_BUSY)
        || (workers[1].rc == 0 && workers[0].rc == ERR_DB_BUSY)))
    { fprintf(stderr,"FAIL: concurrent capacity purchases returned %d and %d\n",workers[0].rc,workers[1].rc); goto cleanup; }
  if (repo_stardock_update_ship_hardware(db,"has_transwarp",1,ship_id) != 0
      || repo_stardock_update_ship_hardware(db,"has_transwarp",1,ship_id) != ERR_DB_BUSY
      || repo_stardock_update_ship_hardware(db,"name",1,ship_id) != ERR_INVALID_ARG
      || repo_stardock_upgrade_ship(db,type_id,"qa_upgrade",11,ship_id) != ERR_DB_BUSY
      || repo_stardock_upgrade_ship(db,type_id,"qa_upgrade",10,ship_id) != 0)
    { fprintf(stderr,"FAIL: boolean hardware, identifier validation or conditional ship upgrade\n"); goto cleanup; }
  int actual = 0, ignored = 0;
  if (!query_pair(db,"SELECT fighters,credits FROM ships WHERE ship_id={1};",(db_bind_t[]){db_bind_i64(ship_id)},1,&actual,&ignored,&err)
      || actual != max_fighters || ignored != 0)
    { fail("capacity and ship upgrade accounting", &err); goto cleanup; }
  puts("PASS: atomic capacity enforcement, concurrent fighter purchases, boolean hardware and bounded upgrade debit");
  if (sql_build(db,"DELETE FROM ships WHERE ship_id={1};",sql,sizeof sql)==0)
    (void)db_exec(db,sql,(db_bind_t[]){db_bind_i64(ship_id)},1,&err);
  db_close(db); return 0;
cleanup:
  if (threads_started) pthread_join(threads[0],NULL);
  if (barrier_live) pthread_barrier_destroy(&barrier);
  if (ship_id > 0 && sql_build(db,"DELETE FROM ships WHERE ship_id={1};",sql,sizeof sql)==0)
    (void)db_exec(db,sql,(db_bind_t[]){db_bind_i64(ship_id)},1,&err);
  (void)sector_id; db_close(db); return 1;
}
