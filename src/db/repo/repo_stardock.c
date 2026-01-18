#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "db/repo/repo_stardock.h"
#include "db/sql_driver.h"
#include <stdio.h>
#include <string.h>

int repo_stardock_get_port_by_sector(db_t *db, int32_t sector_id, int32_t *out_port_id, int32_t *out_type)
{
    /* SQL_VERBATIM: Q1 */
    const char *sql = "SELECT port_id, type FROM ports WHERE sector_id = {1} AND (type = 9 OR type = 0);";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i32(sector_id) }, 1, &res, &err)) {
        if (db_res_step(res, &err)) {
            *out_port_id = (int32_t)db_res_col_i64(res, 0, &err);
            *out_type = (int32_t)db_res_col_i64(res, 1, &err);
            db_res_finalize(res);
            return 0;
        }
        db_res_finalize(res);
        return 1; // Not found
    }
    return err.code;
}

int repo_stardock_get_ship_state(db_t *db, int32_t ship_id, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q2 */
    const char *sql = "SELECT s.holds, s.fighters, s.shields, s.genesis, s.detonators, s.probes, s.cloaking_devices, s.has_transwarp, s.has_planet_scanner, s.has_long_range_scanner, st.maxholds, st.maxfighters, st.maxshields, st.maxgenesis, st.max_detonators, st.max_probes, st.can_transwarp, st.can_planet_scan, st.can_long_range_scan, st.max_cloaks FROM ships s JOIN shiptypes st ON s.type_id = st.shiptypes_id WHERE s.ship_id = {1};";
    char sql_converted[1024];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i32(ship_id) }, 1, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_get_hardware_items(db_t *db, const char *location_type, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q3 */
    const char *sql = "SELECT code, name, price, max_per_ship, category FROM hardware_items WHERE enabled = 1 AND ({1} = 'STARDOCK' OR ({2} = 'CLASS0'));";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_text(location_type), db_bind_text(location_type) }, 2, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_get_hardware_item_details(db_t *db, const char *code, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q4 */
    const char *sql = "SELECT price, requires_stardock, sold_in_class0, max_per_ship, category FROM hardware_items WHERE code = {1} AND enabled = 1;";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_text(code) }, 1, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_get_ship_limit_info(db_t *db, int32_t ship_id, const char *col_name, const char *limit_col, bool is_max_check_needed, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q5 */
    char sql[512];
    if (is_max_check_needed) {
        snprintf(sql, sizeof(sql), "SELECT s.%s, st.%s FROM ships s JOIN shiptypes st ON s.type_id = st.shiptypes_id WHERE s.ship_id = {1};", col_name, limit_col);
    } else {
        snprintf(sql, sizeof(sql), "SELECT %s, 0 FROM ships WHERE ship_id = {1};", col_name);
    }
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i32(ship_id) }, 1, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_update_ship_hardware(db_t *db, const char *col_name, int32_t quantity, int32_t ship_id)
{
    /* SQL_VERBATIM: Q6 */
    char sql[512];
    snprintf(sql, sizeof(sql), "UPDATE ships SET %s = %s + {1} WHERE ship_id = {2};", col_name, col_name);
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i32(quantity), db_bind_i32(ship_id) }, 2, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_check_shipyard_location(db_t *db, int32_t sector_id, int32_t player_id, int32_t *out_port_id)
{
    /* SQL_VERBATIM: Q7 */
    /* SQL_VERBATIM: Q11 */
    const char *sql = "SELECT p.port_id FROM ports p "
                      "JOIN ships s ON s.ported = p.port_id "
                      "JOIN players pl ON pl.ship_id = s.ship_id "
                      "WHERE p.sector_id = {1} AND pl.player_id = {2} AND (p.type = 9 OR p.type = 10);";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i32(sector_id), db_bind_i32(player_id) }, 2, &res, &err)) {
        if (db_res_step(res, &err)) {
            *out_port_id = (int32_t)db_res_col_i64(res, 0, &err);
            db_res_finalize(res);
            return 0;
        }
        db_res_finalize(res);
        return 1;
    }
    return err.code;
}

int repo_stardock_get_player_ship_info(db_t *db, int32_t player_id, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q8 */
    /* SQL_VERBATIM: Q12 */
    const char *sql = "SELECT "
                      "p.alignment, p.commission_id, p.experience, "
                      "s.ship_id, s.type_id, st.name, st.basecost, "
                      "s.fighters, s.shields, s.mines, s.limpets, s.genesis, s.detonators, s.probes, s.cloaking_devices, "
                      "s.colonists, s.ore, s.organics, s.equipment, "
                      "s.has_transwarp, s.has_planet_scanner, s.has_long_range_scanner "
                      "FROM players p JOIN ships s ON p.ship_id = s.ship_id JOIN shiptypes st ON s.type_id = st.shiptypes_id "
                      "WHERE p.player_id = {1};";
    /* Wait, Q12 also includes s.credits at index 3. Let's fix. */
    const char *sql_q12 = "SELECT "
                         "p.alignment, p.commission_id, p.experience, s.credits, "
                         "s.fighters, s.shields, s.mines, s.limpets, s.genesis, s.detonators, s.probes, s.cloaking_devices, "
                         "s.colonists, s.ore, s.organics, s.equipment, "
                         "s.has_transwarp, s.has_planet_scanner, s.has_long_range_scanner, s.ship_id, s.type_id, st.basecost "
                         "FROM players p JOIN ships s ON p.ship_id = s.ship_id JOIN shiptypes st ON s.type_id = st.shiptypes_id "
                         "WHERE p.player_id = {1};";

    char sql_converted[1024];
    sql_build(db, sql_q12, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i32(player_id) }, 1, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_get_shipyard_inventory(db_t *db, int32_t port_id, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q9 */
    const char *sql = "SELECT si.ship_type_id, st.name, st.basecost, st.required_alignment, st.required_commission, st.required_experience, st.maxholds, st.maxfighters, st.maxshields, st.maxgenesis, st.max_detonators, st.max_probes, st.max_cloaks, st.can_transwarp, st.can_planet_scan, st.can_long_range_scan, st.maxmines, st.maxlimpets "
                      "FROM shipyard_inventory si JOIN shiptypes st ON si.ship_type_id = st.shiptypes_id "
                      "WHERE si.port_id = {1} AND si.enabled = 1 AND st.enabled = 1;";
    char sql_converted[1024];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i32(port_id) }, 1, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_get_shiptype_details(db_t *db, int32_t type_id, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q13 */
    const char *sql = "SELECT basecost, required_alignment, required_commission, required_experience, maxholds, maxfighters, maxshields, maxgenesis, max_detonators, max_probes, max_cloaks, can_transwarp, can_planet_scan, can_long_range_scan, maxmines, maxlimpets, name FROM shiptypes WHERE shiptypes_id = {1} AND enabled = 1;";
    char sql_converted[1024];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i32(type_id) }, 1, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_upgrade_ship(db_t *db, int32_t new_type_id, const char *new_name, int64_t cost, int32_t ship_id)
{
    /* SQL_VERBATIM: Q14 */
    const char *sql = "UPDATE ships SET type_id = {1}, name = {2}, credits = credits - {3} WHERE ship_id = {4};";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i32(new_type_id), db_bind_text(new_name), db_bind_i64(cost), db_bind_i32(ship_id) }, 4, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_get_tavern_settings(db_t *db, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q15 */
    const char *sql = "SELECT max_bet_per_transaction, daily_max_wager, enable_dynamic_wager_limit, graffiti_max_posts, notice_expires_days, buy_round_cost, buy_round_alignment_gain, loan_shark_enabled FROM tavern_settings WHERE tavern_settings_id = 1;";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_query(db, sql_converted, NULL, 0, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_is_in_tavern(db_t *db, int32_t sector_id, bool *out_in_tavern)
{
    /* SQL_VERBATIM: Q16 */
    const char *sql = "SELECT 1 FROM taverns WHERE sector_id = {1} AND enabled = 1;";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i32(sector_id) }, 1, &res, &err)) {
        *out_in_tavern = db_res_step(res, &err);
        db_res_finalize(res);
        return 0;
    }
    return err.code;
}

int repo_stardock_get_loan(db_t *db, int32_t player_id, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q17 */
    const char *sql = "SELECT principal, interest_rate, due_date, is_defaulted FROM tavern_loans WHERE player_id = {1};";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i32(player_id) }, 1, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_get_player_credits(db_t *db, int32_t player_id, int64_t *out_credits)
{
    /* SQL_VERBATIM: Q18 */
    /* SQL_VERBATIM: Q20 */
    /* SQL_VERBATIM: Q25 */
    /* SQL_VERBATIM: Q28 */
    /* SQL_VERBATIM: Q31 */
    const char *sql = "SELECT credits FROM players WHERE player_id = {1};";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, &err)) {
        if (db_res_step(res, &err)) {
            *out_credits = db_res_col_i64(res, 0, &err);
            db_res_finalize(res);
            return 0;
        }
        db_res_finalize(res);
        return 1;
    }
    return err.code;
}

int repo_stardock_get_player_alignment(db_t *db, int32_t player_id, int64_t *out_alignment)
{
    /* SQL_VERBATIM: Q18_ALIGN */
    const char *sql = "SELECT alignment FROM players WHERE player_id = {1};";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, &err)) {
        if (db_res_step(res, &err)) {
            *out_alignment = db_res_col_i64(res, 0, &err);
            db_res_finalize(res);
            return 0;
        }
        db_res_finalize(res);
        return 1;
    }
    return err.code;
}

int repo_stardock_update_player_credits(db_t *db, int32_t player_id, int64_t amount, bool is_win)
{
    /* SQL_VERBATIM: Q19 */
    const char *sql = is_win ?
                      "UPDATE players SET credits = credits + {1} WHERE player_id = {2};"
                      : "UPDATE players SET credits = credits - {1} WHERE player_id = {2};";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i64(amount), db_bind_i32(player_id) }, 2, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_mark_loan_defaulted(db_t *db, int32_t player_id)
{
    /* SQL_VERBATIM: Q21 */
    const char *sql = "UPDATE tavern_loans SET is_defaulted = 1 WHERE player_id = {1};";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_update_loan_principal(db_t *db, int32_t player_id, int64_t new_principal)
{
    /* SQL_VERBATIM: Q22 */
    const char *sql = "UPDATE tavern_loans SET principal = {1} WHERE player_id = {2};";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i64(new_principal), db_bind_i32(player_id) }, 2, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_insert_lottery_ticket(db_t *db, const char *draw_date, int32_t player_id, int32_t number, int64_t cost, int32_t purchased_at)
{
    /* SQL_VERBATIM: Q23 */
    const char *sql = "INSERT INTO tavern_lottery_tickets (draw_date, player_id, number, cost, purchased_at) VALUES ({1}, {2}, {3}, {4}, {5});";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_text(draw_date), db_bind_i32(player_id), db_bind_i32(number), db_bind_i64(cost), db_bind_i32(purchased_at) }, 5, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_get_lottery_state(db_t *db, const char *draw_date, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q24 */
    const char *sql = "SELECT draw_date, winning_number, jackpot FROM tavern_lottery_state WHERE draw_date = {1};";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_text(draw_date) }, 1, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_get_player_lottery_tickets(db_t *db, int32_t player_id, const char *draw_date, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q26 */
    const char *sql = "SELECT number, cost, purchased_at FROM tavern_lottery_tickets WHERE player_id = {1} AND draw_date = {2};";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i32(player_id), db_bind_text(draw_date) }, 2, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_insert_deadpool_bet(db_t *db, int32_t bettor_id, int32_t target_id, int64_t amount, int32_t odds_bp, int32_t placed_at, int32_t expires_at)
{
    /* SQL_VERBATIM: Q27 */
    const char *sql = "INSERT INTO tavern_deadpool_bets (bettor_id, target_id, amount, odds_bp, placed_at, expires_at, resolved) VALUES ({1}, {2}, {3}, {4}, {5}, {6}, 0);";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i32(bettor_id), db_bind_i32(target_id), db_bind_i64(amount), db_bind_i32(odds_bp), db_bind_i32(placed_at), db_bind_i32(expires_at) }, 6, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_insert_graffiti(db_t *db, int32_t player_id, const char *text, int32_t created_at)
{
    /* SQL_VERBATIM: Q32 */
    const char *sql = "INSERT INTO tavern_graffiti (player_id, text, created_at) VALUES ({1}, {2}, {3});";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i32(player_id), db_bind_text(text), db_bind_i32(created_at) }, 3, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_get_graffiti_count(db_t *db, int64_t *out_count)
{
    /* SQL_VERBATIM: Q33 */
    const char *sql = "SELECT COUNT(*) FROM tavern_graffiti;";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql_converted, NULL, 0, &res, &err)) {
        if (db_res_step(res, &err)) {
            *out_count = db_res_col_i64(res, 0, &err);
            db_res_finalize(res);
            return 0;
        }
        db_res_finalize(res);
        return 1;
    }
    return err.code;
}

int repo_stardock_delete_oldest_graffiti(db_t *db, int32_t limit)
{
    /* SQL_VERBATIM: Q34 */
    const char *sql = "DELETE FROM tavern_graffiti WHERE tavern_graffiti_id IN (SELECT tavern_graffiti_id FROM tavern_graffiti ORDER BY created_at ASC LIMIT {1});";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i32(limit) }, 1, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_update_alignment(db_t *db, int32_t player_id, int32_t alignment_gain)
{
    /* SQL_VERBATIM: Q35 */
    const char *sql = "UPDATE players SET alignment = alignment + {1} WHERE player_id = {2};";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i32(alignment_gain), db_bind_i32(player_id) }, 2, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_insert_loan(db_t *db, int32_t player_id, int64_t principal, int32_t interest_rate_bp, int32_t due_date)
{
    /* SQL_VERBATIM: Q36 */
    const char *sql = "INSERT INTO tavern_loans (player_id, principal, interest_rate, due_date, is_defaulted) VALUES ({1}, {2}, {3}, {4}, 0);";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i32(player_id), db_bind_i64(principal), db_bind_i32(interest_rate_bp), db_bind_timestamp_text(due_date) }, 4, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_delete_loan(db_t *db, int32_t player_id)
{
    /* SQL_VERBATIM: Q37 */
    const char *sql = "DELETE FROM tavern_loans WHERE player_id = {1};";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_update_loan_repayment(db_t *db, int32_t player_id, int64_t new_principal)
{
    /* SQL_VERBATIM: Q38 */
    const char *sql = "UPDATE tavern_loans SET principal = {1}, is_defaulted = 0 WHERE player_id = {2};";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i64(new_principal), db_bind_i32(player_id) }, 2, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_get_commodity_prices(db_t *db, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q39 */
    const char *sql = "SELECT p.sector_id, c.name, pt.mode, pt.maxproduct, (c.base_price * (10000 + c.volatility * (RANDOM() % 200 - 100)) / 10000) AS price "
                      "FROM ports p JOIN port_trade pt ON p.port_id = pt.port_id JOIN commodities c ON c.code = pt.commodity "
                      "ORDER BY price DESC LIMIT 5;";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_query(db, sql_converted, NULL, 0, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_get_raffle_state(db_t *db, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q29 */
    const char *sql = "SELECT pot, last_winner_id, last_payout, last_win_ts FROM tavern_raffle_state WHERE tavern_raffle_state_id = 1;";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_query(db, sql_converted, NULL, 0, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_update_raffle_on_win(db_t *db, int32_t player_id, int64_t winnings, int32_t now)
{
    /* SQL_VERBATIM: Q30 */
    const char *sql = "UPDATE tavern_raffle_state SET pot = 0, last_winner_id = {1}, last_payout = {2}, last_win_ts = {3} WHERE tavern_raffle_state_id = 1;";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i32(player_id), db_bind_i64(winnings), db_bind_i32(now) }, 3, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_update_raffle_pot(db_t *db, int64_t current_pot)
{
    /* SQL_VERBATIM: Q31 */
    const char *sql = "UPDATE tavern_raffle_state SET pot = {1} WHERE tavern_raffle_state_id = 1;";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i64(current_pot) }, 1, &err)) {
        return 0;
    }
    return err.code;
}
