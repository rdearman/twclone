#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "db/repo/repo_ships.h"
#include "db/sql_driver.h"
#include <stdio.h>
#include <string.h>

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
    /* SQL_VERBATIM: Q1 */
    const char *sql =
        "SELECT result_code "
        "FROM public.handle_ship_destruction("
        "{1},{2},{3},{4},{5},"
        "{6},{7},{8},{9},{10},{11}"
        ");";

    db_bind_t params[] = {
        db_bind_i64(victim_pid),
        db_bind_i64(victim_sid),
        db_bind_i64(killer_pid),
        db_bind_text(cause),
        db_bind_i64(sector_id),
        db_bind_i64(xp_loss_flat),
        db_bind_i64(xp_loss_percent),
        db_bind_i64(max_per_day),
        db_bind_i64(big_sleep),
        db_bind_i64(now_ts),
        db_bind_i64(0)
    };

    db_res_t *res = NULL;
    db_error_t err;
    char sql_converted[1024];
    if (sql_build(db, sql, sql_converted, sizeof(sql_converted)) != 0)
        return ERR_DB_QUERY_FAILED;

    if (!db_query(db, sql_converted, params, 11, &res, &err)) {
        return err.code;
    }

    if (db_res_step(res, &err)) {
        if (result_code_out) *result_code_out = db_res_col_i32(res, 0, &err);
    }
    db_res_finalize(res);
    return 0;
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
