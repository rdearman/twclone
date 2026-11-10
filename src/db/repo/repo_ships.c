#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "db/repo/repo_ships.h"
#include "db/sql_driver.h"
#include "errors.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>

typedef struct
{
  int64_t policy_id;
  int64_t payout;
  int64_t fund_id;
} destruction_policy_t;

/* Settle every currently eligible player-held hull policy as part of the
 * destruction transaction. The policy's configured payout is capped at hull
 * replacement value. A funded policy debits its insurer; otherwise the
 * guaranteed shortfall is explicitly identified as INSURANCE_GUARANTEE. */
static int
settle_destruction_insurance (db_t *db, int64_t player_id, int64_t ship_id,
                              int64_t hull_value, int64_t now_ts,
                              const char *event_key, db_error_t *err)
{
  if (hull_value <= 0) return 0;
  const char *policy_query_pg =
    "SELECT insurance_policies_id,payout,COALESCE(fund_id,0) "
    "FROM insurance_policies WHERE holder_type='player' AND holder_id={1} "
    "AND subject_type='ship' AND subject_id={2} AND active=TRUE "
    "AND COALESCE(NULLIF(start_ts,'')::bigint,0)<={3} "
    "AND (expiry_ts IS NULL OR expiry_ts='' OR NULLIF(expiry_ts,'')::bigint>{3}) "
    "ORDER BY insurance_policies_id FOR UPDATE";
  const char *policy_query_my =
    "SELECT insurance_policies_id,payout,COALESCE(fund_id,0) "
    "FROM insurance_policies WHERE holder_type='player' AND holder_id={1} "
    "AND subject_type='ship' AND subject_id={2} AND active=TRUE "
    "AND COALESCE(CAST(NULLIF(start_ts,'') AS SIGNED),0)<={3} "
    "AND (expiry_ts IS NULL OR expiry_ts='' OR "
    "CAST(NULLIF(expiry_ts,'') AS SIGNED)>{3}) "
    "ORDER BY insurance_policies_id FOR UPDATE";
  char sql[1024];
  const char *policy_query = db_backend(db) == DB_BACKEND_POSTGRES
    ? policy_query_pg : policy_query_my;
  if (sql_build(db, policy_query, sql, sizeof sql) != 0)
    return ERR_DB_QUERY_FAILED;
  db_res_t *res = NULL;
  if (!db_query(db, sql,
                (db_bind_t[]){db_bind_i64(player_id), db_bind_i64(ship_id),
                              db_bind_i64(now_ts)}, 3, &res, err))
    return err->code ? err->code : ERR_DB_QUERY_FAILED;

  destruction_policy_t *policies = NULL;
  size_t count = 0;
  while (db_res_step(res, err))
    {
      destruction_policy_t *next = realloc(policies,
        (count + 1) * sizeof(*policies));
      if (!next)
        {
          db_res_finalize(res);
          free(policies);
          return ERR_DB_QUERY_FAILED;
        }
      policies = next;
      policies[count].policy_id = db_res_col_i64(res, 0, err);
      policies[count].payout = db_res_col_i64(res, 1, err);
      policies[count].fund_id = db_res_col_i64(res, 2, err);
      if (err->code)
        {
          db_res_finalize(res);
          free(policies);
          return err->code;
        }
      count++;
    }
  db_res_finalize(res);
  if (err->code)
    { free(policies); return err->code; }
  if (count == 0)
    { free(policies); return 0; }

  int64_t account_id = 0;
  int64_t account_balance = 0;
  const char *account_query =
    "SELECT bank_accounts_id,balance FROM bank_accounts WHERE "
    "owner_type='player' AND owner_id={1} AND currency='CRD' "
    "AND is_active=TRUE ORDER BY bank_accounts_id LIMIT 1 FOR UPDATE";
  if (sql_build(db, account_query, sql, sizeof sql) != 0)
    { free(policies); return ERR_DB_QUERY_FAILED; }
  res = NULL;
  if (!db_query(db, sql, (db_bind_t[]){db_bind_i64(player_id)}, 1, &res, err))
    { free(policies); return err->code ? err->code : ERR_DB_QUERY_FAILED; }
  int has_account = db_res_step(res, err);
  if (has_account)
    {
      account_id = db_res_col_i64(res, 0, err);
      account_balance = db_res_col_i64(res, 1, err);
    }
  db_res_finalize(res);
  if (err->code)
    { free(policies); return err->code; }

  int64_t remaining = hull_value;
  for (size_t i = 0; i < count && remaining > 0; i++)
    {
      int64_t amount = policies[i].payout;
      if (amount <= 0) continue;
      if (amount > remaining) amount = remaining;
      remaining -= amount;

      char claim_key[96];
      snprintf(claim_key, sizeof claim_key, "%s", event_key);
      const char *claim_sql =
        "INSERT INTO insurance_claims(policy_id,event_id,amount,status) "
        "VALUES({1},{2},{3},'open')";
      if (sql_build(db, claim_sql, sql, sizeof sql) != 0)
        { free(policies); return ERR_DB_QUERY_FAILED; }
      int64_t claim_id = 0;
      if (!db_exec_insert_id(db, sql,
            (db_bind_t[]){db_bind_i64(policies[i].policy_id),
                          db_bind_text(claim_key), db_bind_i64(amount)},
            3, "insurance_claims_id", &claim_id, err))
        { free(policies); return err->code ? err->code : ERR_DB_EXEC_FAILED; }
      if (claim_id <= 0)
        { free(policies); return ERR_DB_EXEC_FAILED; }

      if (!has_account || account_balance > INT64_MAX - amount)
        { free(policies); return ERR_DB_CONSTRAINT; }
      bool insurer_funded = false;
      int64_t rows = 0;
      if (policies[i].fund_id > 0)
        {
          const char *fund_lock =
            "SELECT balance FROM insurance_funds WHERE fund_id={1} FOR UPDATE";
          if (sql_build(db, fund_lock, sql, sizeof sql) != 0)
            { free(policies); return ERR_DB_QUERY_FAILED; }
          res = NULL;
          if (!db_query(db, sql, (db_bind_t[]){db_bind_i64(policies[i].fund_id)},
                        1, &res, err))
            { free(policies); return err->code ? err->code : ERR_DB_QUERY_FAILED; }
          int has_fund = db_res_step(res, err);
          int64_t fund_balance = has_fund ? db_res_col_i64(res, 0, err) : 0;
          db_res_finalize(res);
          if (err->code)
            { free(policies); return err->code; }
          if (has_fund && fund_balance >= amount)
            {
              const char *fund_debit =
                "UPDATE insurance_funds SET balance=balance-{1} "
                "WHERE fund_id={2} AND balance>={1}";
              rows = 0;
              if (sql_build(db, fund_debit, sql, sizeof sql) != 0
                  || !db_exec_rows_affected(db, sql,
                        (db_bind_t[]){db_bind_i64(amount),
                                      db_bind_i64(policies[i].fund_id)}, 2,
                        &rows, err))
                { free(policies); return err->code ? err->code : ERR_DB_EXEC_FAILED; }
              if (rows != 1)
                { free(policies); return ERR_DB_CONSTRAINT; }
              insurer_funded = true;
            }
        }

      const char *account_credit =
        "UPDATE bank_accounts SET balance=balance+{1} "
        "WHERE bank_accounts_id={2} AND is_active=TRUE";
      rows = 0;
      if (sql_build(db, account_credit, sql, sizeof sql) != 0
          || !db_exec_rows_affected(db, sql,
                (db_bind_t[]){db_bind_i64(amount), db_bind_i64(account_id)},
                2, &rows, err))
        { free(policies); return err->code ? err->code : ERR_DB_EXEC_FAILED; }
      if (rows != 1)
        { free(policies); return ERR_DB_CONSTRAINT; }
      account_balance += amount;

      char tx_group[96], tx_idem[128];
      snprintf(tx_group, sizeof tx_group, "insurance-claim-%lld",
               (long long)claim_id);
      snprintf(tx_idem, sizeof tx_idem, "insurance-auto-%lld-%lld",
               (long long)ship_id, (long long)policies[i].policy_id);
      const char *ledger =
        "INSERT INTO bank_transactions(account_id,tx_type,direction,amount,"
        "currency,tx_group_id,description,ts,balance_after,idempotency_key) "
        "VALUES({1},{7},'CREDIT',{2},'CRD',{3},{8},"
        "{4},{5},{6})";
      int64_t tx_id = 0;
      if (sql_build(db, ledger, sql, sizeof sql) != 0
          || !db_exec_insert_id(db, sql,
                (db_bind_t[]){db_bind_i64(account_id), db_bind_i64(amount),
                              db_bind_text(tx_group), db_bind_i64(now_ts),
                              db_bind_i64(account_balance), db_bind_text(tx_idem),
                              db_bind_text(insurer_funded ? "ADJUSTMENT" : "INSURANCE_GUARANTEE"),
                              db_bind_text(insurer_funded ? "automatic hull insurance" : "system-guaranteed hull insurance")},
                8, "bank_transactions_id", &tx_id, err))
        { free(policies); return err->code ? err->code : ERR_DB_EXEC_FAILED; }
      if (tx_id <= 0)
        { free(policies); return ERR_DB_EXEC_FAILED; }
      const char *paid_update =
        "UPDATE insurance_claims SET status='paid',paid_bank_tx={1} "
        "WHERE insurance_claims_id={2} AND status='open'";
      if (sql_build(db, paid_update, sql, sizeof sql) != 0
          || !db_exec(db, sql,
                (db_bind_t[]){db_bind_i64(tx_id), db_bind_i64(claim_id)}, 2, err))
        { free(policies); return err->code ? err->code : ERR_DB_EXEC_FAILED; }
    }
  free(policies);
  return 0;
}

int repo_ships_handle_destruction(db_t *db,
                                  int64_t victim_pid,
                                  int64_t victim_sid,
                                  int64_t killer_pid,
                                  const char *cause,
                                  int64_t sector_id,
                                  int64_t xp_loss_flat,
                                  int64_t xp_loss_percent,
                                  int64_t max_per_day,
                                  int64_t big_sleep,
                                  int64_t now_ts,
                                  int32_t *result_code_out)
{
    (void) killer_pid;
    (void) max_per_day; /* V3 destruction always pods; no daily pod cap. */
    (void) big_sleep;   /* Big Sleep is superseded by universal pod respawn. */
    if (!db || victim_pid <= 0 || victim_sid <= 0 || !cause)
        return ERR_INVALID_ARG;

    db_error_t err = {0};
    if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err))
        return err.code ? err.code : ERR_DB_TX;
    char sql[1024];
    db_res_t *res = NULL;
    int rc = ERR_DB_QUERY_FAILED;
    int active_ship = 0;
    long long experience = 0;
    int player_sector = 0;
    const char *player_lock =
      "SELECT COALESCE(ship_id,0),COALESCE(experience,0),COALESCE(sector_id,1) "
      "FROM players WHERE player_id={1} FOR UPDATE";
    if (sql_build(db, player_lock, sql, sizeof sql) != 0
        || !db_query(db, sql, (db_bind_t[]){db_bind_i64(victim_pid)}, 1,
                     &res, &err))
        goto fail;
    if (!db_res_step(res, &err))
      { db_res_finalize(res); res = NULL; rc = ERR_DB_NOT_FOUND; goto fail; }
    active_ship = db_res_col_i32(res, 0, &err);
    experience = db_res_col_i64(res, 1, &err);
    player_sector = db_res_col_i32(res, 2, &err);
    db_res_finalize(res); res = NULL;
    if (err.code) { rc = err.code; goto fail; }
    if (active_ship != victim_sid)
      { rc = ERR_DB_CONSTRAINT; goto fail; }

    const char *ship_lock =
      "SELECT type_id,COALESCE(sector_id,0),destroyed,"
      "COALESCE((SELECT basecost FROM shiptypes st WHERE "
      "st.shiptypes_id=ships.type_id),0) FROM ships "
      "WHERE ship_id={1} FOR UPDATE";
    if (sql_build(db, ship_lock, sql, sizeof sql) != 0
        || !db_query(db, sql, (db_bind_t[]){db_bind_i64(victim_sid)}, 1,
                     &res, &err))
        goto fail;
    if (!db_res_step(res, &err))
      { db_res_finalize(res); res = NULL; rc = ERR_DB_NOT_FOUND; goto fail; }
    int ship_type = db_res_col_i32(res, 0, &err);
    int ship_sector = db_res_col_i32(res, 1, &err);
    int destroyed = db_res_col_bool(res, 2, &err);
    int64_t hull_value = db_res_col_i64(res, 3, &err);
    db_res_finalize(res); res = NULL;
    if (err.code) { rc = err.code; goto fail; }
    if (destroyed || ship_type <= 0)
      { rc = ERR_DB_CONSTRAINT; goto fail; }
    if (sector_id <= 0) sector_id = ship_sector > 0 ? ship_sector : player_sector;

    int64_t xp_loss = xp_loss_flat;
    if (xp_loss_percent > 0 && experience > 0)
      {
        __int128 pct = (__int128) experience * xp_loss_percent / 100;
        if (pct > INT64_MAX) pct = INT64_MAX;
        xp_loss += (int64_t) pct;
      }
    if (xp_loss < 0) xp_loss = 0;
    if (xp_loss > experience) xp_loss = experience;
    char destruction_key[64];
    snprintf(destruction_key, sizeof destruction_key,
             "ship-destruction-%lld", (long long)victim_sid);
    if (settle_destruction_insurance(db, victim_pid, victim_sid, hull_value,
                                     now_ts, destruction_key, &err) != 0)
      goto fail;
    const char *lose_ship =
      "UPDATE ships SET destroyed=TRUE,hull=0,fighters=0,shields=0,"
      "installed_shields=0,mines=0,limpets=0,genesis=0,detonators=0,"
      "probes=0,photons=0,beacons=0,cloaking_devices=0,has_transwarp=FALSE,"
      "has_planet_scanner=FALSE,has_long_range_scanner=FALSE,cloaked=NULL,"
      "ore=0,organics=0,equipment=0,colonists=0,slaves=0,weapons=0,drugs=0 "
      "WHERE ship_id={1} AND destroyed=FALSE";
    int64_t affected = 0;
    if (sql_build(db, lose_ship, sql, sizeof sql) != 0
        || !db_exec_rows_affected(db, sql, (db_bind_t[]){db_bind_i64(victim_sid)},
                                  1, &affected, &err))
      goto fail;
    if (affected != 1) { rc = ERR_DB_CONSTRAINT; goto fail; }
    const char *delete_ownership = "DELETE FROM ship_ownership WHERE ship_id={1}";
    if (sql_build(db, delete_ownership, sql, sizeof sql) != 0
        || !db_exec(db, sql, (db_bind_t[]){db_bind_i64(victim_sid)}, 1, &err))
      goto fail;

    const char *progress =
      "UPDATE players SET times_blown_up=COALESCE(times_blown_up,0)+1,"
      "experience={1},ship_id=NULL,lastplanet=NULL WHERE player_id={2}";
    if (sql_build(db, progress, sql, sizeof sql) != 0
        || !db_exec(db, sql, (db_bind_t[]){db_bind_i64(experience-xp_loss),
                                          db_bind_i64(victim_pid)}, 2, &err))
      goto fail;

    /* Prefer the configured canonical type, but recover the existing canonical
       Escape Pod definition if an old seed left the setting at its placeholder 0. */
    int pod_type = 0;
    /* Use a scalar subquery so the canonical ship type remains a fallback if
       an older installation does not yet contain the configuration row. */
    const char *pod_type_query = db_backend(db) == DB_BACKEND_POSTGRES
      ? "SELECT CASE WHEN COALESCE(NULLIF((SELECT value FROM config WHERE key="
        "'death.escape_pod_shiptype_id' AND type='int'),'')::bigint,0)>0 "
        "THEN (SELECT value::bigint FROM config WHERE key="
        "'death.escape_pod_shiptype_id' AND type='int') ELSE "
        "(SELECT shiptypes_id FROM shiptypes WHERE name='Escape Pod' "
        "AND enabled=TRUE ORDER BY shiptypes_id LIMIT 1) END"
      : "SELECT CASE WHEN COALESCE(CAST(NULLIF((SELECT value FROM config WHERE `key`="
        "'death.escape_pod_shiptype_id' AND type='int'),'') AS SIGNED),0)>0 "
        "THEN (SELECT CAST(value AS SIGNED) FROM config WHERE `key`="
        "'death.escape_pod_shiptype_id' AND type='int') ELSE "
        "(SELECT shiptypes_id FROM shiptypes WHERE name='Escape Pod' "
        "AND enabled=TRUE ORDER BY shiptypes_id LIMIT 1) END";
    if (sql_build(db, pod_type_query, sql, sizeof sql) != 0
        || !db_query(db, sql, NULL, 0, &res, &err))
      goto fail;
    if (db_res_step(res, &err)) pod_type = db_res_col_i32(res, 0, &err);
    db_res_finalize(res); res = NULL;
    if (err.code || pod_type <= 0)
      { rc = err.code ? err.code : ERR_DB_NOT_FOUND; goto fail; }

    const char *sector_check = "SELECT sector_id FROM sectors WHERE sector_id={1} FOR UPDATE";
    if (sql_build(db, sector_check, sql, sizeof sql) != 0
        || !db_query(db, sql, (db_bind_t[]){db_bind_i32(1)}, 1, &res, &err))
      goto fail;
    if (!db_res_step(res, &err))
      { db_res_finalize(res); res=NULL; rc=ERR_DB_NOT_FOUND; goto fail; }
    db_res_finalize(res); res = NULL;

    int64_t pod_id = 0;
    const char *insert_pod =
      "INSERT INTO ships (name,type_id,sector_id,holds,fighters,shields,"
      "installed_shields,hull,destroyed) VALUES ('Escape Pod',{1},1,0,0,0,0,100,FALSE)";
    if (sql_build(db, insert_pod, sql, sizeof sql) != 0
        || !db_exec_insert_id(db, sql, (db_bind_t[]){db_bind_i32(pod_type)},
                              1, "ship_id", &pod_id, &err))
      goto fail;
    if (pod_id <= 0) { rc = ERR_DB_EXEC_FAILED; goto fail; }

    int owner_role = 0;
    const char *role_query = "SELECT role_id FROM ship_roles WHERE role='owner' ORDER BY role_id LIMIT 1";
    if (sql_build(db, role_query, sql, sizeof sql) != 0
        || !db_query(db, sql, NULL, 0, &res, &err))
      goto fail;
    if (db_res_step(res, &err)) owner_role = db_res_col_i32(res, 0, &err);
    db_res_finalize(res); res = NULL;
    if (owner_role <= 0 || err.code)
      { rc = err.code ? err.code : ERR_DB_NOT_FOUND; goto fail; }
    const char *own_pod =
      "INSERT INTO ship_ownership(ship_id,player_id,role_id,is_primary) "
      "VALUES({1},{2},{3},TRUE)";
    if (sql_build(db, own_pod, sql, sizeof sql) != 0
        || !db_exec(db, sql, (db_bind_t[]){db_bind_i64(pod_id),
                                           db_bind_i64(victim_pid),
                                           db_bind_i32(owner_role)}, 3, &err))
      goto fail;
    const char *assign_pod =
      "UPDATE players SET ship_id={1},sector_id=1,lastplanet=NULL WHERE player_id={2}";
    if (sql_build(db, assign_pod, sql, sizeof sql) != 0
        || !db_exec(db, sql, (db_bind_t[]){db_bind_i64(pod_id),
                                           db_bind_i64(victim_pid)}, 2, &err))
      goto fail;

    const char *status_lock =
      "SELECT podded_count_today,podded_last_reset FROM podded_status "
      "WHERE player_id={1} FOR UPDATE";
    if (sql_build(db, status_lock, sql, sizeof sql) != 0
        || !db_query(db, sql, (db_bind_t[]){db_bind_i64(victim_pid)}, 1,
                     &res, &err))
      goto fail;
    int pod_count = 0;
    int64_t last_reset = 0;
    int has_status = db_res_step(res, &err);
    if (has_status)
      {
        pod_count = db_res_col_i32(res, 0, &err);
        last_reset = db_res_col_i64(res, 1, &err);
      }
    db_res_finalize(res); res = NULL;
    if (err.code) goto fail;
    if (has_status && (last_reset <= 0 || now_ts-last_reset >= 86400))
      { pod_count = 0; last_reset = now_ts; }
    if (!has_status) last_reset = now_ts;
    const char *status_upsert = db_backend(db) == DB_BACKEND_POSTGRES
      ? "INSERT INTO podded_status(player_id,status,podded_count_today,podded_last_reset) "
        "VALUES({1},'podded',{2},{3}) ON CONFLICT(player_id) DO UPDATE SET "
        "status='podded',big_sleep_until=NULL,reason=NULL,"
        "podded_count_today=EXCLUDED.podded_count_today,podded_last_reset=EXCLUDED.podded_last_reset"
      : "INSERT INTO podded_status(player_id,status,podded_count_today,podded_last_reset) "
        "VALUES({1},'podded',{2},{3}) ON DUPLICATE KEY UPDATE status='podded',"
        "big_sleep_until=NULL,reason=NULL,podded_count_today=VALUES(podded_count_today),"
        "podded_last_reset=VALUES(podded_last_reset)";
    if (sql_build(db, status_upsert, sql, sizeof sql) != 0
        || !db_exec(db, sql, (db_bind_t[]){db_bind_i64(victim_pid),
                                          db_bind_i32(pod_count+1),
                                          db_bind_i64(last_reset)}, 3, &err))
      goto fail;

    char payload[512];
    snprintf(payload, sizeof payload,
      "{\"victim_player_id\":%lld,\"victim_ship_id\":%lld,\"killer_player_id\":%lld,\"cause\":\"%s\",\"sector_id\":%d,\"decision\":\"podded\",\"escape_pod_ship_id\":%lld,\"xp_lost\":%lld}",
      (long long)victim_pid, (long long)victim_sid, (long long)killer_pid,
      cause, (int)sector_id, (long long)pod_id, (long long)xp_loss);
    const char *event_sql =
      "INSERT INTO engine_events(ts,type,actor_player_id,sector_id,payload,idem_key) "
      "VALUES({1},'ship.destroyed',{2},{3},{4},{5})";
    if (sql_build(db, event_sql, sql, sizeof sql) != 0
        || !db_exec(db, sql, (db_bind_t[]){db_bind_timestamp_text(now_ts),
                                           db_bind_i64(victim_pid),
                                           db_bind_i64(sector_id),
                                           db_bind_text(payload),
                                           db_bind_text(destruction_key)}, 5, &err))
      goto fail;

    if (!db_tx_commit(db, &err))
      { db_tx_rollback(db, NULL); return err.code ? err.code : ERR_DB_TX; }
    if (result_code_out) *result_code_out = 0;
    return 0;

fail:
    rc = err.code ? err.code : rc;
    if (res) db_res_finalize(res);
    db_tx_rollback(db, NULL);
    return rc;
}

int repo_ships_rename_active(db_t *db, int32_t player_id, int32_t ship_id,
                             const char *name, int64_t *rows_out)
{
    if (!db || !name || !rows_out) return ERR_DB_MISUSE;
    const char *template = "UPDATE ships SET name = {1} WHERE ship_id = {2} "
        "AND EXISTS (SELECT 1 FROM players WHERE player_id = {3} AND ship_id = {2}) "
        "AND EXISTS (SELECT 1 FROM ship_ownership WHERE ship_id = {2} "
        "AND player_id = {3} AND is_primary = {4});";
    char sql[512];
    if (sql_build(db, template, sql, sizeof sql) != 0) return ERR_DB_QUERY_FAILED;
    db_error_t err = {0};
    if (!db_exec_rows_affected(db, sql, (db_bind_t[]){db_bind_text(name),
            db_bind_i64(ship_id), db_bind_i64(player_id), db_bind_bool(true)},
            4, rows_out, &err)) return err.code ? err.code : ERR_DB_QUERY_FAILED;
    /* MySQL may report zero changed rows when the requested name is already
     * set. Only accept that case if the active ownership still matches. */
    if (*rows_out == 0) {
        const char *check = "SELECT 1 FROM ships s JOIN players p ON p.ship_id = s.ship_id "
            "JOIN ship_ownership o ON o.ship_id = s.ship_id AND o.player_id = p.player_id "
            "WHERE s.ship_id = {1} AND p.player_id = {2} AND o.is_primary = {3} AND s.name = {4};";
        if (sql_build(db, check, sql, sizeof sql) != 0) return ERR_DB_QUERY_FAILED;
        db_res_t *res = NULL;
        if (!db_query(db, sql, (db_bind_t[]){db_bind_i64(ship_id), db_bind_i64(player_id),
                db_bind_bool(true), db_bind_text(name)}, 4, &res, &err))
            return err.code ? err.code : ERR_DB_QUERY_FAILED;
        if (db_res_step(res, &err)) *rows_out = 1;
        db_res_finalize(res);
    }
    return 0;
}

int repo_ships_transfer_primary(db_t *db, int32_t ship_id,
                                int32_t source_player_id,
                                int32_t target_player_id,
                                int64_t *rows_out)
{
    if (!db || ship_id <= 0 || source_player_id <= 0 || target_player_id <= 0
        || source_player_id == target_player_id || !rows_out)
        return ERR_INVALID_ARG;

    /* Serialize the player references before checking ownership.  The server
     * starts the transaction, while this repository owns the lifecycle
     * mutation and its concurrency contract.  Always acquire the two player
     * locks in ID order: opposing transfers (A -> B and B -> A) must not
     * deadlock while closing the race with active-ship changes or a second
     * transfer. */
    const char *lock_player =
        "SELECT player_id FROM players WHERE player_id = {1} FOR UPDATE;";
    char sql[1024];
    db_error_t err = {0};
    db_res_t *res = NULL;
    int32_t player_id = 0;
    int32_t player_ids[2] = {
        source_player_id < target_player_id ? source_player_id : target_player_id,
        source_player_id < target_player_id ? target_player_id : source_player_id
    };
    if (sql_build(db, lock_player, sql, sizeof sql) != 0)
        return ERR_DB_QUERY_FAILED;
    for (size_t i = 0; i < 2; ++i)
      {
        res = NULL;
        if (!db_query(db, sql, (db_bind_t[]){db_bind_i64(player_ids[i])},
                      1, &res, &err))
          return err.code ? err.code : ERR_DB_QUERY_FAILED;
        if (!db_res_step(res, &err))
          {
            db_res_finalize(res);
            return err.code ? err.code : ERR_TARGET_INVALID;
          }
        player_id = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        if (player_id != player_ids[i])
          return ERR_TARGET_INVALID;
      }

    /* A player-to-player handoff is an in-world exchange: both players and
     * the transferred hull must currently occupy the same sector.  Resolve
     * these facts after locking both player rows so the ownership mutation
     * cannot race a movement update. */
    const char *lock_ship =
        "SELECT sector_id FROM ships WHERE ship_id = {1} FOR UPDATE;";
    if (sql_build(db, lock_ship, sql, sizeof sql) != 0)
        return ERR_DB_QUERY_FAILED;
    res = NULL;
    if (!db_query(db, sql, (db_bind_t[]){db_bind_i64(ship_id)}, 1,
                  &res, &err))
        return err.code ? err.code : ERR_DB_QUERY_FAILED;
    if (!db_res_step(res, &err))
      {
        db_res_finalize(res);
        return err.code ? err.code : ERR_TARGET_INVALID;
      }
    int32_t ship_sector = db_res_col_i32(res, 0, &err);
    db_res_finalize(res);

    const char *player_sector =
        "SELECT sector_id FROM players WHERE player_id = {1} FOR UPDATE;";
    if (sql_build(db, player_sector, sql, sizeof sql) != 0)
        return ERR_DB_QUERY_FAILED;
    int32_t source_sector = 0;
    int32_t target_sector = 0;
    for (size_t i = 0; i < 2; ++i)
      {
        res = NULL;
        if (!db_query(db, sql,
                      (db_bind_t[]){db_bind_i64(player_ids[i])}, 1,
                      &res, &err))
          return err.code ? err.code : ERR_DB_QUERY_FAILED;
        if (!db_res_step(res, &err))
          {
            db_res_finalize(res);
            return err.code ? err.code : ERR_TARGET_INVALID;
          }
        int32_t sector = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        if (player_ids[i] == source_player_id)
            source_sector = sector;
        else
            target_sector = sector;
      }
    if (ship_sector <= 0 || source_sector != ship_sector
        || target_sector != ship_sector)
        return ERR_TARGET_INVALID;

    /* Lock the primary ownership row before applying the guarded update. */
    const char *lock_owner =
        "SELECT ship_id FROM ship_ownership "
        "WHERE ship_id = {1} AND player_id = {2} AND is_primary = {3} "
        "FOR UPDATE;";
    if (sql_build(db, lock_owner, sql, sizeof sql) != 0)
        return ERR_DB_QUERY_FAILED;
    res = NULL;
    if (!db_query(db, sql,
                  (db_bind_t[]){db_bind_i64(ship_id),
                                db_bind_i64(source_player_id),
                                db_bind_bool(true)},
                  3, &res, &err))
        return err.code ? err.code : ERR_DB_QUERY_FAILED;
    if (!db_res_step(res, &err)) {
        db_res_finalize(res);
        return err.code ? err.code : ERR_TARGET_INVALID;
    }
    db_res_finalize(res);

    /* Keep lifecycle mutation in the ship repository.  In particular, do not
     * update players.ship_id here: an active ship is deliberately not
     * transferable and the deferred ownership invariants must remain true. */
    const char *template =
        "UPDATE ship_ownership SET player_id = {1} "
        "WHERE ship_id = {2} AND player_id = {3} AND is_primary = {4} "
        "AND EXISTS (SELECT 1 FROM players WHERE player_id = {1}) "
        "AND NOT EXISTS (SELECT 1 FROM players WHERE player_id = {3} "
        "                       AND ship_id = {2}) "
        "AND NOT EXISTS (SELECT 1 FROM ship_ownership "
        "                 WHERE ship_id = {2} AND player_id = {1});";
    if (sql_build(db, template, sql, sizeof sql) != 0)
        return ERR_DB_QUERY_FAILED;

    if (!db_exec_rows_affected(db, sql,
                               (db_bind_t[]){ db_bind_i64(target_player_id),
                                              db_bind_i64(ship_id),
                                              db_bind_i64(source_player_id),
                                              db_bind_bool(true) },
                               4, rows_out, &err))
        return err.code ? err.code : ERR_DB_QUERY_FAILED;
    return 0;
}

int repo_ships_get_active_id(db_t *db, int32_t player_id, int32_t *ship_id_out)
{
    /* SQL_VERBATIM: Q2 */
    const char *sql_template = "SELECT ship_id FROM players WHERE player_id = {1};";
    char sql[256];
    if (sql_build(db, sql_template, sql, sizeof sql) != 0) return ERR_DB_QUERY_FAILED;

    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, &err)) {
        if (db_res_step(res, &err)) {
            if (ship_id_out) *ship_id_out = db_res_col_i32(res, 0, &err);
        }
        db_res_finalize(res);
        return 0;
    }
    return err.code;
}

int repo_ships_get_active_id_for_update(db_t *db, int32_t player_id,
                                        int32_t *ship_id_out)
{
    if (!db || player_id <= 0 || !ship_id_out || db->tx_nest_level <= 0)
        return ERR_INVALID_ARG;
    const char *query = "SELECT ship_id FROM players WHERE player_id = {1} FOR UPDATE;";
    char sql[256];
    if (sql_build(db, query, sql, sizeof sql) != 0)
        return ERR_DB_QUERY_FAILED;
    db_res_t *res = NULL;
    db_error_t err = {0};
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i64(player_id) }, 1,
                  &res, &err))
        return err.code ? err.code : ERR_DB_QUERY_FAILED;
    if (!db_res_step(res, &err)) {
        db_res_finalize(res);
        return err.code ? err.code : ERR_DB_NOT_FOUND;
    }
    *ship_id_out = db_res_col_i32(res, 0, &err);
    db_res_finalize(res);
    return err.code;
}

int repo_ships_get_towing_id(db_t *db, int32_t ship_id, int32_t *towing_id_out)
{
    /* SQL_VERBATIM: Q3 */
    const char *sql_template = "SELECT towing_ship_id FROM ships WHERE ship_id = {1};";
    char sql[256];
    if (sql_build(db, sql_template, sql, sizeof sql) != 0) return ERR_DB_QUERY_FAILED;

    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(ship_id) }, 1, &res, &err)) {
        if (db_res_step(res, &err)) {
            if (towing_id_out) *towing_id_out = db_res_col_i32(res, 0, &err);
        }
        db_res_finalize(res);
        return 0;
    }
    return err.code;
}

int repo_ships_clear_towing_id(db_t *db, int32_t ship_id)
{
    /* SQL_VERBATIM: Q4 */
    const char *sql_template = "UPDATE ships SET towing_ship_id = 0 WHERE ship_id = {1};";
    char sql[256];
    if (sql_build(db, sql_template, sql, sizeof sql) != 0) return ERR_DB_QUERY_FAILED;

    db_error_t err;
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(ship_id) }, 1, &err)) {
        return err.code;
    }
    return 0;
}

int repo_ships_clear_is_being_towed_by(db_t *db, int32_t ship_id)
{
    /* SQL_VERBATIM: Q5 */
    const char *sql_template = "UPDATE ships SET is_being_towed_by = 0 WHERE ship_id = {1};";
    char sql[256];
    if (sql_build(db, sql_template, sql, sizeof sql) != 0) return ERR_DB_QUERY_FAILED;

    db_error_t err;
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(ship_id) }, 1, &err)) {
        return err.code;
    }
    return 0;
}

int repo_ships_get_is_being_towed_by(db_t *db, int32_t ship_id, int32_t *towed_by_id_out)
{
    /* SQL_VERBATIM: Q6 */
    const char *sql_template = "SELECT is_being_towed_by FROM ships WHERE ship_id = {1};";
    char sql[256];
    if (sql_build(db, sql_template, sql, sizeof sql) != 0) return ERR_DB_QUERY_FAILED;

    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(ship_id) }, 1, &res, &err)) {
        if (db_res_step(res, &err)) {
            if (towed_by_id_out) *towed_by_id_out = db_res_col_i32(res, 0, &err);
        }
        db_res_finalize(res);
        return 0;
    }
    return err.code;
}

int repo_ships_get_towed_ship_info(db_t *db, int32_t tower_ship_id, int32_t *towed_ship_id_out, int32_t *towed_player_id_out, int32_t *towed_corp_id_out)
{
    /* SQL_VERBATIM: Q20 */
    const char *sql_template = "SELECT s.ship_id, p.player_id, cm.corporation_id FROM ships s "
                               "JOIN players p ON s.ship_id = p.ship_id "
                               "LEFT JOIN corp_members cm ON p.player_id = cm.player_id "
                               "WHERE s.is_being_towed_by = {1} LIMIT 1;";
    char sql[512];
    if (sql_build(db, sql_template, sql, sizeof sql) != 0) return ERR_DB_QUERY_FAILED;

    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(tower_ship_id) }, 1, &res, &err)) {
        int found = 0;
        if (db_res_step(res, &err)) {
            if (towed_ship_id_out) *towed_ship_id_out = db_res_col_i32(res, 0, &err);
            if (towed_player_id_out) *towed_player_id_out = db_res_col_i32(res, 1, &err);
            if (towed_corp_id_out) *towed_corp_id_out = db_res_col_i32(res, 2, &err);
            found = 1;
        }
        db_res_finalize(res);
        return found ? 0 : -1;
    }
    return err.code;
}

int repo_ships_set_towing_id(db_t *db, int32_t ship_id, int32_t target_ship_id)
{
    /* SQL_VERBATIM: Q7 */
    const char *sql_template = "UPDATE ships SET towing_ship_id = {1} WHERE ship_id = {2};";
    char sql[256];
    if (sql_build(db, sql_template, sql, sizeof sql) != 0) return ERR_DB_QUERY_FAILED;

    db_error_t err;
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(target_ship_id), db_bind_i64(ship_id) }, 2, &err)) {
        return err.code;
    }
    return 0;
}

int repo_ships_set_is_being_towed_by(db_t *db, int32_t target_ship_id, int32_t player_ship_id)
{
    /* SQL_VERBATIM: Q8 */
    const char *sql_template = "UPDATE ships SET is_being_towed_by = {1} WHERE ship_id = {2};";
    char sql[256];
    if (sql_build(db, sql_template, sql, sizeof sql) != 0) return ERR_DB_QUERY_FAILED;

    db_error_t err;
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(player_ship_id), db_bind_i64(target_ship_id) }, 2, &err)) {
        return err.code;
    }
    return 0;
}

int repo_ships_get_cargo_and_holds(db_t *db,
                                   int32_t ship_id,
                                   int *ore,
                                   int *organics,
                                   int *equipment,
                                   int *colonists,
                                   int *slaves,
                                   int *weapons,
                                   int *drugs,
                                   int *holds)
{
    /* SQL_VERBATIM: Q9 */
    /* SQL_VERBATIM: Q11 */
    const char *sql =
        "SELECT ore, organics, equipment, colonists, slaves, weapons, drugs, holds FROM ships WHERE ship_id = {1};";

    char sql_final[512];
    if (sql_build(db, sql, sql_final, sizeof(sql_final)) != 0)
        return ERR_DB_QUERY_FAILED;

    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql_final, (db_bind_t[]){ db_bind_i64(ship_id) }, 1, &res, &err)) {
        if (db_res_step(res, &err)) {
            if (ore) *ore = db_res_col_i32(res, 0, &err);
            if (organics) *organics = db_res_col_i32(res, 1, &err);
            if (equipment) *equipment = db_res_col_i32(res, 2, &err);
            if (colonists) *colonists = db_res_col_i32(res, 3, &err);
            if (slaves) *slaves = db_res_col_i32(res, 4, &err);
            if (weapons) *weapons = db_res_col_i32(res, 5, &err);
            if (drugs) *drugs = db_res_col_i32(res, 6, &err);
            if (holds) *holds = db_res_col_i32(res, 7, &err);
            db_res_finalize(res);
            return 0;
        }
        db_res_finalize(res);
        return 1; // Not found
    }
    return err.code;
}

int repo_ships_update_cargo_column(db_t *db, int32_t ship_id, const char *col_name, int32_t new_qty)
{
    if (!db || ship_id <= 0 || !col_name || new_qty < 0)
        return ERR_INVALID_ARG;
    static const char *const allowed_columns[] = {
        "ore", "organics", "equipment", "colonists", "slaves",
        "weapons", "drugs"
    };
    bool allowed = false;
    for (size_t i = 0; i < sizeof allowed_columns / sizeof allowed_columns[0]; ++i)
        if (strcmp(col_name, allowed_columns[i]) == 0) { allowed = true; break; }
    if (!allowed) return ERR_INVALID_ARG;
    /* SQL_VERBATIM: Q10 */
    char sql_up[512];
    snprintf(sql_up,
             sizeof(sql_up),
             "UPDATE ships SET %s = {1} WHERE ship_id = {2};",
             col_name);

    char sql_up_converted[512];
    if (sql_build(db, sql_up, sql_up_converted, sizeof(sql_up_converted)) != 0)
        return ERR_DB_QUERY_FAILED;

    db_error_t err = {0};
    if (!db_exec(db, sql_up_converted,
                 (db_bind_t[]){ db_bind_i64(new_qty), db_bind_i64(ship_id) },
                 2, &err))
        return err.code ? err.code : ERR_DB_EXEC_FAILED;
    /* Keep the historical success-on-executed-update behavior: MySQL's
     * changed-row count is zero when the value already equals new_qty. */
    return 0;
}

int
repo_ships_decloak (db_t *db, int32_t ship_id)
{
    if (!db || ship_id <= 0)
        return ERR_DB_MISUSE;

    const char *sql_template =
        "UPDATE ships SET cloaked = NULL WHERE ship_id = {1} "
        "AND cloaked IS NOT NULL;";
    char sql[256];
    if (sql_build (db, sql_template, sql, sizeof sql) != 0)
        return ERR_DB_QUERY_FAILED;

    db_error_t err = {0};
    if (!db_exec (db, sql, (db_bind_t[]){db_bind_i64 (ship_id)}, 1, &err))
        return err.code ? err.code : ERR_DB_EXEC_FAILED;
    return 0;
}

static int
insurance_lock_player_account (db_t *db, int32_t player_id,
                               int64_t *account_id, int64_t *balance,
                               db_error_t *err)
{
    const char *id_col = db_backend (db) == DB_BACKEND_POSTGRES
        ? "bank_accounts_id" : "id";
    char query[512], sql[768];
    snprintf (query, sizeof query,
        "SELECT %s,balance FROM bank_accounts WHERE owner_type='player' "
        "AND owner_id={1} AND currency='CRD' AND is_active=TRUE FOR UPDATE",
        id_col);
    if (sql_build (db, query, sql, sizeof sql) != 0)
        return ERR_DB_QUERY_FAILED;
    db_res_t *res = NULL;
    if (!db_query (db, sql, (db_bind_t[]){db_bind_i32 (player_id)}, 1,
                   &res, err))
        return err->code ? err->code : ERR_DB_QUERY_FAILED;
    if (!db_res_step (res, err))
      { db_res_finalize (res); return err->code ? err->code : ERR_NOT_FOUND; }
    *account_id = db_res_col_i64 (res, 0, err);
    *balance = db_res_col_i64 (res, 1, err);
    db_res_finalize (res);
    return err->code ? err->code : 0;
}

static int
insurance_update_player_balance (db_t *db, int64_t account_id,
                                 int64_t amount, bool debit,
                                 db_error_t *err)
{
    const char *id_col = db_backend (db) == DB_BACKEND_POSTGRES
        ? "bank_accounts_id" : "id";
    char query[384], sql[512];
    if (debit)
        snprintf (query, sizeof query,
            "UPDATE bank_accounts SET balance=balance-{1} WHERE %s={2} "
            "AND is_active=TRUE AND balance>={1}", id_col);
    else
        snprintf (query, sizeof query,
            "UPDATE bank_accounts SET balance=balance+{1} WHERE %s={2} "
            "AND is_active=TRUE", id_col);
    int64_t rows = 0;
    if (sql_build (db, query, sql, sizeof sql) != 0
        || !db_exec_rows_affected (db, sql,
             (db_bind_t[]){db_bind_i64 (amount),db_bind_i64 (account_id)},
             2, &rows, err))
        return err->code ? err->code : ERR_DB_EXEC_FAILED;
    return rows == 1 ? 0 : (debit ? ERR_INSUFFICIENT_FUNDS : ERR_DB_BUSY);
}

int
repo_ships_insurance_buy_policy (db_t *db, int32_t player_id,
                                 const char *subject_type,
                                 int64_t subject_id, int64_t premium,
                                 int64_t payout, int64_t fund_id,
                                 const char *start_ts, const char *expiry_ts,
                                 int64_t *policy_id_out)
{
    if (!db || player_id <= 0 || !subject_type || subject_id <= 0
        || premium < 0 || payout < 0 || fund_id <= 0 || !start_ts
        || !*start_ts || !expiry_ts || !*expiry_ts || !policy_id_out
        || (strcmp (subject_type, "ship") != 0
            && strcmp (subject_type, "cargo") != 0
            && strcmp (subject_type, "planet") != 0))
        return ERR_INVALID_ARG;
    *policy_id_out = 0;
    db_error_t err = {0};
    if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
        return err.code ? err.code : ERR_DB_TX;
    char sql[1024];
    db_res_t *res = NULL;
    if (sql_build (db, "SELECT player_id FROM players WHERE player_id={1} FOR UPDATE",
                   sql, sizeof sql) != 0
        || !db_query (db, sql, (db_bind_t[]){db_bind_i32 (player_id)}, 1,
                      &res, &err))
        goto policy_fail;
    if (!db_res_step (res, &err))
      { db_res_finalize (res); res = NULL; err.code = ERR_NOT_FOUND; goto policy_fail; }
    db_res_finalize (res); res = NULL;

    const char *owned_query =
      (strcmp (subject_type, "ship") == 0 || strcmp (subject_type, "cargo") == 0)
      ? "SELECT o.ship_id FROM ship_ownership o JOIN ships s ON s.ship_id=o.ship_id "
        "WHERE o.ship_id={1} AND o.player_id={2} AND o.is_primary=TRUE FOR UPDATE"
      : "SELECT planet_id FROM planets WHERE planet_id={1} AND owner_type='player' "
        "AND owner_id={2} FOR UPDATE";
    if (sql_build (db, owned_query, sql, sizeof sql) != 0
        || !db_query (db, sql,
             (db_bind_t[]){db_bind_i64 (subject_id),db_bind_i32 (player_id)},
             2, &res, &err))
        goto policy_fail;
    if (!db_res_step (res, &err))
      { db_res_finalize (res); res = NULL; err.code = ERR_PERMISSION_DENIED; goto policy_fail; }
    db_res_finalize (res); res = NULL;

    if (sql_build (db, "SELECT fund_id FROM insurance_funds WHERE fund_id={1} FOR UPDATE",
                   sql, sizeof sql) != 0
        || !db_query (db, sql, (db_bind_t[]){db_bind_i64 (fund_id)}, 1,
                      &res, &err))
        goto policy_fail;
    if (!db_res_step (res, &err))
      { db_res_finalize (res); res = NULL; err.code = ERR_NOT_FOUND; goto policy_fail; }
    db_res_finalize (res); res = NULL;

    int64_t account_id = 0, balance = 0;
    int rc = insurance_lock_player_account (db, player_id, &account_id,
                                             &balance, &err);
    if (rc != 0) { err.code = rc; goto policy_fail; }
    if (balance < premium) { err.code = ERR_INSUFFICIENT_FUNDS; goto policy_fail; }
    const char *policy_insert =
      "INSERT INTO insurance_policies(holder_type,holder_id,subject_type,subject_id,"
      "premium,payout,fund_id,start_ts,expiry_ts,active) "
      "VALUES('player',{1},{2},{3},{4},{5},{6},{7},{8},TRUE)";
    if (sql_build (db, policy_insert, sql, sizeof sql) != 0
        || !db_exec_insert_id (db, sql,
             (db_bind_t[]){db_bind_i32 (player_id),db_bind_text (subject_type),
               db_bind_i64 (subject_id),db_bind_i64 (premium),db_bind_i64 (payout),
               db_bind_i64 (fund_id),db_bind_text (start_ts),
               db_bind_text (expiry_ts)}, 8, "insurance_policies_id",
             policy_id_out, &err))
        goto policy_fail;
    if (premium > 0)
      {
        if (insurance_update_player_balance (db, account_id, premium,
                                                true, &err) != 0)
          { if (!err.code) err.code = ERR_DB_CONSTRAINT; goto policy_fail; }
        int64_t rows = 0;
        if (sql_build (db, "UPDATE insurance_funds SET balance=balance+{1} "
                       "WHERE fund_id={2} AND balance<={3}",
                       sql, sizeof sql) != 0
            || !db_exec_rows_affected (db, sql,
                 (db_bind_t[]){db_bind_i64 (premium),db_bind_i64 (fund_id),
                   db_bind_i64 (INT64_MAX - premium)}, 3, &rows, &err)
            || rows != 1)
          { if (!err.code) err.code = ERR_DB_CONSTRAINT; goto policy_fail; }
        char group[64], idem[96];
        snprintf (group, sizeof group, "insurance-policy-%lld",
                  (long long) *policy_id_out);
        snprintf (idem, sizeof idem, "insurance-policy-premium-%lld",
                  (long long) *policy_id_out);
        const char *ledger =
          "INSERT INTO bank_transactions(account_id,tx_type,direction,amount,currency,"
          "tx_group_id,description,ts,balance_after,idempotency_key) "
          "VALUES({1},'FEE','DEBIT',{2},'CRD',{3},'insurance premium',{4},{5},{6})";
        if (sql_build (db, ledger, sql, sizeof sql) != 0
            || !db_exec (db, sql,
                 (db_bind_t[]){db_bind_i64 (account_id),db_bind_i64 (premium),
                   db_bind_text (group),db_bind_i64 ((int64_t) time (NULL)),
                   db_bind_i64 (balance - premium),db_bind_text (idem)}, 6,
                 &err))
            goto policy_fail;
      }
    if (!db_tx_commit (db, &err)) goto policy_fail;
    return 0;

policy_fail:
    if (res) db_res_finalize (res);
    db_tx_rollback (db, &err);
    return err.code ? err.code : ERR_DB_EXEC_FAILED;
}

int
repo_ships_insurance_settle_claim (db_t *db, int32_t player_id,
                                   int64_t claim_id, const char *event_id,
                                   int64_t amount, int64_t *claim_id_out)
{
    if (!db || player_id <= 0 || claim_id <= 0 || !event_id || !*event_id
        || amount <= 0 || !claim_id_out)
        return ERR_INVALID_ARG;
    *claim_id_out = 0;
    db_error_t err = {0};
    if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
        return err.code ? err.code : ERR_DB_TX;
    char sql[1024];
    db_res_t *res = NULL;
    if (sql_build (db, "SELECT policy_id,amount,status FROM insurance_claims "
                   "WHERE insurance_claims_id={1} FOR UPDATE", sql, sizeof sql) != 0
        || !db_query (db, sql, (db_bind_t[]){db_bind_i64 (claim_id)}, 1,
                      &res, &err))
        goto claim_fail;
    if (!db_res_step (res, &err))
      { db_res_finalize (res); res = NULL; err.code = ERR_NOT_FOUND; goto claim_fail; }
    int64_t policy_id = db_res_col_i64 (res, 0, &err);
    int64_t claim_amount = db_res_col_i64 (res, 1, &err);
    const char *status_value = db_res_col_text (res, 2, &err);
    char status[16] = {0};
    snprintf (status, sizeof status, "%s", status_value ? status_value : "");
    db_res_finalize (res); res = NULL;
    if (err.code) goto claim_fail;
    const char *policy_query =
      "SELECT holder_type,holder_id,payout,COALESCE(fund_id,0),active "
      "FROM insurance_policies WHERE insurance_policies_id={1} FOR UPDATE";
    if (sql_build (db, policy_query, sql, sizeof sql) != 0
        || !db_query (db, sql, (db_bind_t[]){db_bind_i64 (policy_id)}, 1,
                      &res, &err))
        goto claim_fail;
    if (!db_res_step (res, &err))
      { db_res_finalize (res); res = NULL; err.code = ERR_NOT_FOUND; goto claim_fail; }
    const char *holder_value = db_res_col_text (res, 0, &err);
    char holder_type[16] = {0};
    snprintf (holder_type, sizeof holder_type, "%s", holder_value ? holder_value : "");
    int64_t holder_id = db_res_col_i64 (res, 1, &err);
    int64_t payout = db_res_col_i64 (res, 2, &err);
    int64_t fund_id = db_res_col_i64 (res, 3, &err);
    bool active = db_res_col_bool (res, 4, &err);
    db_res_finalize (res); res = NULL;
    if (err.code) goto claim_fail;
    if (strcmp (holder_type, "player") != 0 || holder_id != player_id)
      { err.code = ERR_PERMISSION_DENIED; goto claim_fail; }
    if (strcmp (status, "paid") == 0)
      {
        *claim_id_out = claim_id;
        if (!db_tx_commit (db, &err)) goto claim_fail;
        return 0;
      }
    if (strcmp (status, "open") != 0 || !active)
      { err.code = ERR_PERMISSION_DENIED; goto claim_fail; }
    if (amount > payout || amount > claim_amount)
      { err.code = ERR_INVALID_ARG; goto claim_fail; }

    int64_t account_id = 0, balance = 0;
    int rc = insurance_lock_player_account (db, player_id, &account_id,
                                             &balance, &err);
    if (rc != 0) { err.code = rc; goto claim_fail; }
    if (balance > INT64_MAX - amount)
      { err.code = ERR_DB_CONSTRAINT; goto claim_fail; }
    bool funded = false;
    if (fund_id > 0)
      {
        if (sql_build (db, "SELECT balance FROM insurance_funds WHERE fund_id={1} FOR UPDATE",
                       sql, sizeof sql) != 0
            || !db_query (db, sql, (db_bind_t[]){db_bind_i64 (fund_id)}, 1,
                          &res, &err))
            goto claim_fail;
        if (db_res_step (res, &err))
          funded = db_res_col_i64 (res, 0, &err) >= amount;
        db_res_finalize (res); res = NULL;
        if (err.code) goto claim_fail;
      }
    int64_t rows = 0;
    if (funded
        && (sql_build (db, "UPDATE insurance_funds SET balance=balance-{1} "
                       "WHERE fund_id={2} AND balance>={1}", sql, sizeof sql) != 0
            || !db_exec_rows_affected (db, sql,
                 (db_bind_t[]){db_bind_i64 (amount),db_bind_i64 (fund_id)},
                 2, &rows, &err) || rows != 1))
      { if (!err.code) err.code = ERR_DB_BUSY; goto claim_fail; }
    if (insurance_update_player_balance (db, account_id, amount, false,
                                        &err) != 0)
      goto claim_fail;

    char group[64], idem[96];
    snprintf (group, sizeof group, "insurance-claim-%lld", (long long) claim_id);
    snprintf (idem, sizeof idem, "insurance-claim-settlement-%lld",
              (long long) claim_id);
    const char *ledger =
      "INSERT INTO bank_transactions(account_id,tx_type,direction,amount,currency,"
      "tx_group_id,description,ts,balance_after,idempotency_key) "
      "VALUES({1},{2},'CREDIT',{3},'CRD',{4},{5},{6},{7},{8})";
    int64_t tx_id = 0;
    if (sql_build (db, ledger, sql, sizeof sql) != 0
        || !db_exec_insert_id (db, sql,
             (db_bind_t[]){db_bind_i64 (account_id),
               db_bind_text (funded ? "ADJUSTMENT" : "INSURANCE_GUARANTEE"),
               db_bind_i64 (amount),db_bind_text (group),
               db_bind_text (funded ? "automatic hull insurance" :
                                      "system-guaranteed hull insurance"),
               db_bind_i64 ((int64_t) time (NULL)),db_bind_i64 (balance + amount),
               db_bind_text (idem)}, 8,
             db_backend (db) == DB_BACKEND_POSTGRES
               ? "bank_transactions_id" : "id", &tx_id, &err))
        goto claim_fail;
    if (sql_build (db, "UPDATE insurance_claims SET status='paid',event_id={1},"
                   "paid_bank_tx={2} WHERE insurance_claims_id={3} AND status='open'",
                   sql, sizeof sql) != 0
        || !db_exec_rows_affected (db, sql,
             (db_bind_t[]){db_bind_text (event_id),db_bind_i64 (tx_id),
               db_bind_i64 (claim_id)}, 3, &rows, &err) || rows != 1)
      { if (!err.code) err.code = ERR_DB_BUSY; goto claim_fail; }
    if (!db_tx_commit (db, &err)) goto claim_fail;
    *claim_id_out = claim_id;
    return 0;

claim_fail:
    if (res) db_res_finalize (res);
    db_tx_rollback (db, &err);
    return err.code ? err.code : ERR_DB_EXEC_FAILED;
}
