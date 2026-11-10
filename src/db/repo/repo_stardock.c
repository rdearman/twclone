#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "db/repo/repo_stardock.h"
#include "db/repo/repo_clusters.h"
#include "db/sql_driver.h"
#include "errors.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int
ship_sale_abort (db_t *db, int code, db_error_t *err)
{
    (void) db_tx_rollback (db, err);
    return code;
}

static int
ship_sale_lock_ship (db_t *db, int32_t ship_id, db_error_t *err)
{
    char sql[256];
    db_res_t *res = NULL;
    if (sql_build (db, "SELECT ship_id FROM ships WHERE ship_id = {1} FOR UPDATE",
                   sql, sizeof sql) != 0)
        return ERR_DB_QUERY_FAILED;
    if (!db_query (db, sql, (db_bind_t[]){db_bind_i32 (ship_id)}, 1, &res, err))
        return err->code ? err->code : ERR_DB_QUERY_FAILED;
    bool found = db_res_step (res, err);
    db_res_finalize (res);
    if (!found && err->code)
        return err->code;
    return found ? 0 : ERR_SHIP_NOT_FOUND;
}

static int
ship_sale_require_exclusive_owner (db_t *db, int32_t ship_id,
                                   int32_t player_id, db_error_t *err)
{
    char sql[512];
    db_res_t *res = NULL;
    if (sql_build (db,
                   "SELECT player_id, role_id, is_primary FROM ship_ownership "
                   "WHERE ship_id = {1} ORDER BY player_id, role_id FOR UPDATE",
                   sql, sizeof sql) != 0)
        return ERR_DB_QUERY_FAILED;
    if (!db_query (db, sql, (db_bind_t[]){db_bind_i32 (ship_id)}, 1, &res, err))
        return err->code ? err->code : ERR_DB_QUERY_FAILED;
    int row_count = 0;
    bool valid_owner = false;
    while (db_res_step (res, err))
      {
        row_count++;
        valid_owner = row_count == 1
          && db_res_col_i32 (res, 0, err) == player_id
          && db_res_col_i32 (res, 1, err) == 1
          && db_res_col_bool (res, 2, err);
      }
    db_res_finalize (res);
    if (err->code)
        return err->code;
    return row_count == 1 && valid_owner
      ? 0 : ERR_PERMISSION_DENIED;
}

typedef struct
{
    int32_t ship_id;
    int32_t sector_id;
    int32_t ported;
    int32_t type_id;
    bool destroyed;
    bool onplanet;
    bool cloaked;
    int32_t towing_ship_id;
    int32_t towed_by_ship_id;
    int64_t hull;
    int64_t holds;
    int64_t installed_shields;
    int64_t mines;
    int64_t limpets;
    int64_t fighters;
    int64_t genesis;
    int64_t detonators;
    int64_t probes;
    int64_t photons;
    int64_t beacons;
    int64_t cloaks;
    bool has_transwarp;
    bool has_planet_scanner;
    bool has_long_range_scanner;
    int64_t base_price;
    int64_t initial_holds;
} ship_sale_state_t;

static bool
ship_sale_add_equipment_value (int64_t quantity, int64_t unit_price,
                               int64_t *total)
{
    int64_t value = 0;
    return quantity >= 0 && unit_price >= 0
      && !__builtin_mul_overflow (quantity, unit_price, &value)
      && !__builtin_add_overflow (*total, value, total);
}

int
repo_stardock_sell_ship (db_t *db, int32_t player_id, int32_t ship_id,
                         int32_t replacement_ship_id, int sale_factor_bp,
                         repo_ship_sale_result_t *out_result)
{
    if (!db || player_id <= 0 || ship_id <= 0 || replacement_ship_id <= 0
        || ship_id == replacement_ship_id || !out_result
        || sale_factor_bp < 0 || sale_factor_bp > 10000)
        return ERR_INVALID_ARG;
    memset (out_result, 0, sizeof (*out_result));

    db_error_t err = {0};
    if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
        return err.code ? err.code : ERR_DB_TX;

    char sql[2048];
    db_res_t *res = NULL;
    int32_t active_ship_id = 0;
    int32_t player_sector_id = 0;
    int64_t credits = 0;
    if (sql_build (db,
                   "SELECT ship_id, sector_id, credits FROM players "
                   "WHERE player_id = {1} FOR UPDATE",
                   sql, sizeof sql) != 0
        || !db_query (db, sql, (db_bind_t[]){db_bind_i32 (player_id)}, 1,
                      &res, &err))
        return ship_sale_abort (db, err.code ? err.code : ERR_DB_QUERY_FAILED,
                                &err);
    if (!db_res_step (res, &err))
      {
        db_res_finalize (res);
        return ship_sale_abort (db, err.code ? err.code : ERR_NOT_FOUND, &err);
      }
    active_ship_id = db_res_col_i32 (res, 0, &err);
    player_sector_id = db_res_col_i32 (res, 1, &err);
    credits = db_res_col_i64 (res, 2, &err);
    db_res_finalize (res);
    if (active_ship_id <= 0 || player_sector_id <= 0)
        return ship_sale_abort (db, ERR_SHIP_NOT_FOUND, &err);

    /* Use one global ship lock order (ascending IDs) after locking the player. */
    int32_t lock_ids[3] = {active_ship_id, ship_id, replacement_ship_id};
    for (int i = 0; i < 3; i++)
      for (int j = i + 1; j < 3; j++)
        if (lock_ids[j] < lock_ids[i])
          {
            int32_t tmp = lock_ids[i];
            lock_ids[i] = lock_ids[j];
            lock_ids[j] = tmp;
          }
    int32_t previous_lock = 0;
    for (int i = 0; i < 3; i++)
      if (lock_ids[i] != previous_lock)
        {
          int rc = ship_sale_lock_ship (db, lock_ids[i], &err);
          if (rc != 0)
              return ship_sale_abort (db, rc, &err);
          previous_lock = lock_ids[i];
        }

    int owned_ship_count = 0;
    if (sql_build (db,
                   "SELECT COUNT(*) FROM ship_ownership WHERE player_id = {1} "
                   "AND role_id = 1",
                   sql, sizeof sql) != 0
        || !db_query (db, sql,
                      (db_bind_t[]){db_bind_i32 (player_id)}, 1, &res, &err))
        return ship_sale_abort (db, err.code ? err.code : ERR_DB_QUERY_FAILED,
                                &err);
    if (db_res_step (res, &err))
        owned_ship_count = db_res_col_i32 (res, 0, &err);
    else
      {
        db_res_finalize (res);
        return ship_sale_abort (db, ERR_DB_QUERY_FAILED, &err);
      }
    db_res_finalize (res);
    if (owned_ship_count <= 1)
        return ship_sale_abort (db, ERR_PERMISSION_DENIED, &err);

    int32_t port_id = 0;
    int location_rc = repo_stardock_check_shipyard_location
      (db, player_sector_id, player_id, &port_id);
    if (location_rc != 0)
        return ship_sale_abort (db, location_rc == 1
                                ? ERR_NOT_AT_SHIPYARD : location_rc, &err);
    if (sql_build (db,
                   "SELECT p.port_id FROM ports p JOIN porttypes pt "
                   "ON pt.porttype_id = p.porttype_id WHERE p.port_id = {1} "
                   "AND p.sector_id = {2} AND (pt.is_stardock = {3} "
                   "OR pt.is_black_market = {3} OR EXISTS (SELECT 1 "
                   "FROM shipyard_inventory si WHERE si.port_id = p.port_id "
                   "AND si.enabled = {3})) FOR UPDATE",
                   sql, sizeof sql) != 0
        || !db_query (db, sql,
                      (db_bind_t[]){db_bind_i32 (port_id),
                                    db_bind_i32 (player_sector_id),
                                    db_bind_bool (true)}, 3, &res, &err))
        return ship_sale_abort (db, err.code ? err.code : ERR_DB_QUERY_FAILED,
                                &err);
    bool authorized_shipyard = db_res_step (res, &err);
    db_res_finalize (res);
    if (!authorized_shipyard)
        return ship_sale_abort (db, ERR_NOT_AT_SHIPYARD, &err);

    int32_t cluster_id = 0;
    int banned = 0;
    if (repo_clusters_get_cluster_for_sector (db, player_sector_id,
                                               &cluster_id) != 0)
        return ship_sale_abort (db, ERR_DB_QUERY_FAILED, &err);
    if (cluster_id > 0)
      {
        if (sql_build (db,
                       "SELECT banned FROM cluster_player_status "
                       "WHERE cluster_id = {1} AND player_id = {2} FOR UPDATE",
                       sql, sizeof sql) != 0
            || !db_query (db, sql,
                          (db_bind_t[]){db_bind_i32 (cluster_id),
                                        db_bind_i32 (player_id)}, 2, &res, &err))
            return ship_sale_abort (db, err.code ? err.code : ERR_DB_QUERY_FAILED,
                                    &err);
        if (db_res_step (res, &err))
            banned = db_res_col_bool (res, 0, &err) ? 1 : 0;
        db_res_finalize (res);
      }
    if (banned)
        return ship_sale_abort (db, ERR_BANNED_FROM_JURISDICTION, &err);

    int owner_rc = ship_sale_require_exclusive_owner (db, ship_id, player_id,
                                                      &err);
    if (owner_rc == 0)
        owner_rc = ship_sale_require_exclusive_owner (db, replacement_ship_id,
                                                      player_id, &err);
    if (owner_rc != 0)
        return ship_sale_abort (db, owner_rc, &err);

    if (sql_build (db,
                   "SELECT s.ship_id, s.sector_id, s.ported, s.type_id, "
                   "s.destroyed, s.onplanet, (s.cloaked IS NOT NULL), "
                   "s.towing_ship_id, s.is_being_towed_by, s.hull, s.holds, "
                   "s.fighters, s.installed_shields, s.mines, s.limpets, "
                   "s.genesis, s.detonators, s.probes, s.photons, s.beacons, "
                   "s.cloaking_devices, s.has_transwarp, s.has_planet_scanner, "
                   "s.has_long_range_scanner, st.basecost, st.initialholds "
                   "FROM ships s JOIN shiptypes st ON st.shiptypes_id = s.type_id "
                   "WHERE s.ship_id IN ({1}, {2}) ORDER BY s.ship_id",
                   sql, sizeof sql) != 0
        || !db_query (db, sql,
                      (db_bind_t[]){db_bind_i32 (ship_id),
                                    db_bind_i32 (replacement_ship_id)},
                      2, &res, &err))
        return ship_sale_abort (db, err.code ? err.code : ERR_DB_QUERY_FAILED,
                                &err);
    ship_sale_state_t sold = {0}, replacement = {0};
    int state_count = 0;
    while (db_res_step (res, &err))
      {
        ship_sale_state_t state = {0};
        state.ship_id = db_res_col_i32 (res, 0, &err);
        state.sector_id = db_res_col_i32 (res, 1, &err);
        state.ported = db_res_col_i32 (res, 2, &err);
        state.type_id = db_res_col_i32 (res, 3, &err);
        state.destroyed = db_res_col_bool (res, 4, &err);
        state.onplanet = db_res_col_bool (res, 5, &err);
        state.cloaked = db_res_col_bool (res, 6, &err);
        state.towing_ship_id = db_res_col_i32 (res, 7, &err);
        state.towed_by_ship_id = db_res_col_i32 (res, 8, &err);
        state.hull = db_res_col_i64 (res, 9, &err);
        state.holds = db_res_col_i64 (res, 10, &err);
        state.fighters = db_res_col_i64 (res, 11, &err);
        state.installed_shields = db_res_col_i64 (res, 12, &err);
        state.mines = db_res_col_i64 (res, 13, &err);
        state.limpets = db_res_col_i64 (res, 14, &err);
        state.genesis = db_res_col_i64 (res, 15, &err);
        state.detonators = db_res_col_i64 (res, 16, &err);
        state.probes = db_res_col_i64 (res, 17, &err);
        state.photons = db_res_col_i64 (res, 18, &err);
        state.beacons = db_res_col_i64 (res, 19, &err);
        state.cloaks = db_res_col_i64 (res, 20, &err);
        state.has_transwarp = db_res_col_bool (res, 21, &err);
        state.has_planet_scanner = db_res_col_bool (res, 22, &err);
        state.has_long_range_scanner = db_res_col_bool (res, 23, &err);
        state.base_price = db_res_col_i64 (res, 24, &err);
        state.initial_holds = db_res_col_i64 (res, 25, &err);
        if (state.ship_id == ship_id)
            sold = state;
        else if (state.ship_id == replacement_ship_id)
            replacement = state;
        state_count++;
      }
    db_res_finalize (res);
    if (err.code)
        return ship_sale_abort (db, err.code, &err);
    if (state_count != 2 || sold.ship_id != ship_id
        || replacement.ship_id != replacement_ship_id)
        return ship_sale_abort (db, ERR_SHIP_NOT_FOUND, &err);

    if (sold.destroyed || sold.hull <= 0 || sold.onplanet || sold.cloaked
        || sold.towing_ship_id > 0 || sold.towed_by_ship_id > 0
        || sold.sector_id != player_sector_id || sold.ported != port_id)
        return ship_sale_abort (db, ERR_PERMISSION_DENIED, &err);
    if (replacement.destroyed || replacement.hull <= 0 || replacement.onplanet
        || replacement.cloaked || replacement.towing_ship_id > 0
        || replacement.towed_by_ship_id > 0
        || (active_ship_id == ship_id
            && replacement.sector_id != player_sector_id))
        return ship_sale_abort (db, ERR_TARGET_INVALID, &err);
    if (sold.base_price < 0 || sold.initial_holds < 0)
        return ship_sale_abort (db, ERR_DB_QUERY_FAILED, &err);

    const char *price_sql =
      "SELECT (SELECT price FROM hardware_items WHERE code = 'FIGHTERS'), "
      "(SELECT price FROM hardware_items WHERE code = 'SHIELDS'), "
      "(SELECT price FROM hardware_items WHERE code = 'HOLDS'), "
      "(SELECT price FROM hardware_items WHERE code = 'GENESIS'), "
      "(SELECT price FROM hardware_items WHERE code = 'DETONATOR'), "
      "(SELECT price FROM hardware_items WHERE code = 'PROBE'), "
      "(SELECT price FROM hardware_items WHERE code = 'MINES'), "
      "(SELECT price FROM hardware_items WHERE code = 'LIMPETS'), "
      "(SELECT price FROM hardware_items WHERE code = 'PHOTONS'), "
      "(SELECT price FROM hardware_items WHERE code = 'BEACONS'), "
      "(SELECT price FROM hardware_items WHERE code = 'CLOAK'), "
      "(SELECT price FROM hardware_items WHERE code = 'LSCANNER'), "
      "(SELECT price FROM hardware_items WHERE code = 'PSCANNER'), "
      "(SELECT price FROM hardware_items WHERE code = 'TWARP')";
    if (!db_query (db, price_sql, NULL, 0, &res, &err)
        || !db_res_step (res, &err))
      {
        if (res) db_res_finalize (res);
        return ship_sale_abort (db, err.code ? err.code : ERR_DB_QUERY_FAILED,
                                &err);
      }
    int64_t quantities[14] = {
      sold.fighters, sold.installed_shields,
      sold.holds > sold.initial_holds ? sold.holds - sold.initial_holds : 0,
      sold.genesis, sold.detonators, sold.probes, sold.mines, sold.limpets,
      sold.photons, sold.beacons, sold.cloaks,
      sold.has_long_range_scanner ? 1 : 0,
      sold.has_planet_scanner ? 1 : 0,
      sold.has_transwarp ? 1 : 0
    };
    int64_t equipment_value = 0;
    bool value_ok = true;
    for (int i = 0; i < 14; i++)
      {
        if (db_res_col_is_null (res, i))
          {
            if (quantities[i] != 0) value_ok = false;
            continue;
          }
        int64_t unit_price = db_res_col_i64 (res, i, &err);
        if (!ship_sale_add_equipment_value (quantities[i], unit_price,
                                             &equipment_value))
            value_ok = false;
      }
    db_res_finalize (res);
    if (!value_ok)
        return ship_sale_abort (db, ERR_DB_QUERY_FAILED, &err);

    int64_t subtotal = 0, scaled = 0;
    if (__builtin_add_overflow (sold.base_price, equipment_value, &subtotal)
        || __builtin_mul_overflow (subtotal, (int64_t) sale_factor_bp, &scaled))
        return ship_sale_abort (db, ERR_INVALID_ARG, &err);
    int64_t proceeds = scaled / 10000;
    if (credits < 0 || proceeds > INT64_MAX - credits)
        return ship_sale_abort (db, ERR_INVALID_ARG, &err);

    int rc = 0;
    int64_t rows = 0;
    const char *credit_sql =
      "UPDATE players SET credits = credits + {1}, "
      "ship_id = CASE WHEN ship_id = {2} THEN {3} ELSE ship_id END "
      "WHERE player_id = {4} AND ship_id = {5} AND credits <= {6}";
    if (sql_build (db, credit_sql, sql, sizeof sql) != 0
        || !db_exec_rows_affected
          (db, sql,
           (db_bind_t[]){db_bind_i64 (proceeds), db_bind_i32 (ship_id),
                         db_bind_i32 (replacement_ship_id), db_bind_i32 (player_id),
                         db_bind_i32 (active_ship_id),
                         db_bind_i64 (INT64_MAX - proceeds)},
           6, &rows, &err))
        rc = err.code ? err.code : ERR_DB_QUERY_FAILED;
    else if (rows != 1)
        rc = ERR_DB_BUSY;
    if (rc != 0)
        return ship_sale_abort (db, rc, &err);

    const char *clear_cargo = "DELETE FROM ship_cargo WHERE ship_id = {1}";
    if (sql_build (db, clear_cargo, sql, sizeof sql) != 0
        || !db_exec (db, sql, (db_bind_t[]){db_bind_i32 (ship_id)}, 1, &err))
        return ship_sale_abort (db, err.code ? err.code : ERR_DB_QUERY_FAILED,
                                &err);
    const char *clear_legacy_cargo =
      "UPDATE ships SET colonists = 0, ore = 0, organics = 0, equipment = 0, "
      "slaves = 0, weapons = 0, drugs = 0 WHERE ship_id = {1}";
    if (sql_build (db, clear_legacy_cargo, sql, sizeof sql) != 0
        || !db_exec_rows_affected
          (db, sql, (db_bind_t[]){db_bind_i32 (ship_id)}, 1, &rows, &err)
        || rows != 1)
        return ship_sale_abort (db, err.code ? err.code : ERR_DB_BUSY, &err);

    const char *insurance_sql =
      "UPDATE insurance_policies SET active = {1} WHERE subject_type = {2} "
      "AND subject_id = {3} AND active = {4}";
    if (sql_build (db, insurance_sql, sql, sizeof sql) != 0
        || !db_exec (db, sql,
                     (db_bind_t[]){db_bind_bool (false), db_bind_text ("ship"),
                                   db_bind_i32 (ship_id), db_bind_bool (true)},
                     4, &err))
        return ship_sale_abort (db, err.code ? err.code : ERR_DB_QUERY_FAILED,
                                &err);

    const char *delete_owner = "DELETE FROM ship_ownership WHERE ship_id = {1}";
    if (sql_build (db, delete_owner, sql, sizeof sql) != 0
        || !db_exec_rows_affected
          (db, sql, (db_bind_t[]){db_bind_i32 (ship_id)}, 1, &rows, &err)
        || rows < 1)
        return ship_sale_abort (db, err.code ? err.code : ERR_DB_BUSY, &err);

    int64_t sale_id = 0;
    const char *audit_sql =
      "INSERT INTO ship_sale_audit (ship_id, seller_player_id, "
      "replacement_ship_id, port_id, hull_valuation, equipment_valuation, "
      "sale_factor_bp, proceeds) VALUES ({1},{2},{3},{4},{5},{6},{7},{8})";
    if (sql_build (db, audit_sql, sql, sizeof sql) != 0
        || !db_exec_insert_id
          (db, sql,
           (db_bind_t[]){db_bind_i32 (ship_id), db_bind_i32 (player_id),
                         db_bind_i32 (replacement_ship_id), db_bind_i32 (port_id),
                         db_bind_i64 (sold.base_price),
                         db_bind_i64 (equipment_value), db_bind_i32 (sale_factor_bp),
                         db_bind_i64 (proceeds)},
           8, "sale_id", &sale_id, &err))
        return ship_sale_abort (db, err.code ? err.code : ERR_DB_QUERY_FAILED,
                                &err);
    int64_t inventory_id = 0;
    const char *inventory_sql =
      "INSERT INTO shipyard_used_inventory (sale_id, ship_id, port_id) "
      "VALUES ({1},{2},{3})";
    if (sql_build (db, inventory_sql, sql, sizeof sql) != 0
        || !db_exec_insert_id
          (db, sql,
           (db_bind_t[]){db_bind_i64 (sale_id), db_bind_i32 (ship_id),
                         db_bind_i32 (port_id)},
           3, "used_inventory_id", &inventory_id, &err))
        return ship_sale_abort (db, err.code ? err.code : ERR_DB_QUERY_FAILED,
                                &err);

    if (!db_tx_commit (db, &err))
      {
        (void) db_tx_rollback (db, &err);
        return err.code ? err.code : ERR_DB_TX;
      }
    out_result->sale_id = sale_id;
    out_result->used_inventory_id = inventory_id;
    out_result->hull_valuation = sold.base_price;
    out_result->equipment_valuation = equipment_value;
    out_result->proceeds = proceeds;
    out_result->updated_credits = credits + proceeds;
    out_result->port_id = port_id;
    out_result->sold_active_ship = active_ship_id == ship_id;
    return 0;
}

int repo_stardock_get_port_by_sector(db_t *db, int32_t sector_id, int32_t *out_port_id, int32_t *out_porttype_id)
{
    /* SQL_VERBATIM: Q1 */
    /* Get a hardware-service port. Preserve legacy Stardock/Class-0
     * precedence while allowing explicitly flagged black markets. */
    const char *sql = "SELECT p.port_id, COALESCE(p.porttype_id, 0) FROM ports p "
                      "LEFT JOIN porttypes pt ON p.porttype_id = pt.porttype_id "
                      "WHERE p.sector_id = {1} AND (pt.is_stardock = true OR pt.code = 'CLASS0' OR pt.is_black_market = true) "
                      "ORDER BY pt.is_stardock DESC, (pt.code = 'CLASS0') DESC LIMIT 1;";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i64(sector_id) }, 1, &res, &err)) {
        if (db_res_step(res, &err)) {
            *out_port_id = (int32_t)db_res_col_i64(res, 0, &err);
            *out_porttype_id = (int32_t)db_res_col_i64(res, 1, &err);
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
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i64(ship_id) }, 1, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_get_hardware_items(db_t *db, const char *location_type, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q3 */
    const char *sql = "SELECT code, name, price, max_per_ship, category FROM hardware_items WHERE enabled = {3} AND ({1} = 'STARDOCK' OR ({2} = 'CLASS0'));";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_text(location_type), db_bind_text(location_type), db_bind_bool(true) }, 3, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_get_black_market_hardware_items(db_t *db, int32_t port_id,
                                                   int32_t porttype_id,
                                                   db_res_t **out_res)
{
    if (!db || port_id <= 0 || porttype_id <= 0 || !out_res)
        return -1;
    /* Special-port catalogue entries must be explicitly mapped and have a
     * configured stock row. Keep empty entries visible so clients can show
     * their effective price and unavailable state. */
    const char *sql =
        "SELECT hi.code, hi.name, hi.price, hi.max_per_ship, hi.category, "
        "phs.stock_quantity, phs.max_stock "
        "FROM hardware_items hi JOIN porttype_items pti "
        "ON pti.hardware_items_id = hi.hardware_items_id "
        "JOIN port_hardware_stock phs ON phs.hardware_items_id = hi.hardware_items_id "
        "WHERE hi.enabled = {1} AND pti.porttype_id = {2} AND pti.can_buy = {3} "
        "AND phs.port_id = {4} ORDER BY hi.code;";
    char sql_converted[768];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_query(db, sql_converted,
                 (db_bind_t[]){ db_bind_bool(true), db_bind_i64(porttype_id),
                                db_bind_bool(true), db_bind_i64(port_id) },
                 4, out_res, &err))
        return 0;
    return err.code;
}

int
repo_stardock_get_black_market_hardware_stock(db_t *db, int32_t port_id,
                                               const char *code,
                                               int32_t *out_stock,
                                               int32_t *out_max_stock)
{
    if (!db || port_id <= 0 || !code || !out_stock || !out_max_stock)
        return -1;
    const char *sql =
        "SELECT phs.stock_quantity, phs.max_stock "
        "FROM port_hardware_stock phs JOIN hardware_items hi "
        "ON hi.hardware_items_id = phs.hardware_items_id "
        "WHERE phs.port_id = {1} AND hi.code = {2} AND hi.enabled = {3};";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_res_t *res = NULL;
    db_error_t err;
    if (!db_query(db, sql_converted,
                  (db_bind_t[]){ db_bind_i64(port_id), db_bind_text(code),
                                 db_bind_bool(true) }, 3, &res, &err))
        return err.code;
    if (!db_res_step(res, &err))
      {
        db_res_finalize(res);
        return 1;
      }
    *out_stock = (int32_t) db_res_col_i64(res, 0, &err);
    *out_max_stock = (int32_t) db_res_col_i64(res, 1, &err);
    db_res_finalize(res);
    return 0;
}

int
repo_stardock_consume_black_market_hardware_stock(db_t *db, int32_t port_id,
                                                   const char *code,
                                                   int32_t quantity,
                                                   int64_t *out_remaining)
{
    if (!db || port_id <= 0 || !code || quantity <= 0)
        return -1;
    const char *sql =
        "UPDATE port_hardware_stock phs SET stock_quantity = phs.stock_quantity - {1}, "
        "updated_at = CURRENT_TIMESTAMP FROM hardware_items hi "
        "WHERE phs.hardware_items_id = hi.hardware_items_id AND phs.port_id = {2} "
        "AND hi.code = {3} AND phs.stock_quantity >= {1} "
        "RETURNING phs.stock_quantity;";
    char sql_converted[768];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_res_t *res = NULL;
    db_error_t err;
    if (!db_exec_returning(db, sql_converted,
                           (db_bind_t[]){ db_bind_i64(quantity), db_bind_i64(port_id),
                                          db_bind_text(code) }, 3, &res, &err))
        return err.code;
    if (!db_res_step(res, &err))
      {
        db_res_finalize(res);
        return 1;
      }
    if (out_remaining)
      *out_remaining = db_res_col_i64(res, 0, &err);
    db_res_finalize(res);
    return 0;
}

int
repo_stardock_restock_hardware(db_t *db, int64_t now_s)
{
    if (!db)
        return -1;
    const char *sql =
        "UPDATE port_hardware_stock SET "
        "stock_quantity = LEAST(max_stock, stock_quantity + restock_quantity), "
        "next_restock_at = CAST({1} AS timestamptz) + "
        "(restock_interval_seconds * INTERVAL '1 second'), "
        "updated_at = CAST({1} AS timestamptz) "
        "WHERE restock_quantity > 0 AND next_restock_at <= CAST({1} AS timestamptz);";
    char sql_converted[768];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (!db_exec(db, sql_converted,
                 (db_bind_t[]){ db_bind_timestamp_text(now_s) }, 1, &err))
        return err.code ? err.code : -1;
    return 0;
}

int repo_stardock_get_hardware_item_details(db_t *db, const char *code, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q4 */
    const char *sql = "SELECT price, requires_stardock, sold_in_class0, max_per_ship, category FROM hardware_items WHERE code = {1} AND enabled = {2};";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_text(code), db_bind_bool(true) }, 2, out_res, &err)) {
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
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i64(ship_id) }, 1, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_update_ship_hardware(db_t *db, const char *col_name, int32_t quantity, int32_t ship_id)
{
    if (!db || !col_name || ship_id <= 0 || quantity <= 0)
        return ERR_INVALID_ARG;

    /* Only allow the inventory columns exposed by the hardware command.
     * Numeric items are bounded in the same statement as the increment so
     * concurrent purchases cannot exceed the ship type's capacity. */
    const char *column = NULL, *limit = NULL;
    bool boolean_item = false;
    struct hardware_column { const char *name, *limit; bool boolean_item; };
    static const struct hardware_column allowed[] = {
        {"fighters", "maxfighters", false},
        {"shields", "maxshields", false},
        {"holds", "maxholds", false},
        {"genesis", "maxgenesis", false},
        {"mines", "maxmines", false},
        {"beacons", "maxbeacons", false},
        {"cloaking_devices", "max_cloaks", false},
        {"detonators", "max_detonators", false},
        {"probes", "max_probes", false},
        {"has_transwarp", NULL, true},
        {"has_planet_scanner", NULL, true},
        {"has_long_range_scanner", NULL, true}
    };
    for (size_t i = 0; i < sizeof allowed / sizeof allowed[0]; ++i)
        if (strcmp(col_name, allowed[i].name) == 0) {
            column = allowed[i].name;
            limit = allowed[i].limit;
            boolean_item = allowed[i].boolean_item;
            break;
        }
    if (!column || (boolean_item && quantity != 1))
        return ERR_INVALID_ARG;

    char query[768];
    int n;
    if (boolean_item)
        n = snprintf(query, sizeof query,
                     "UPDATE ships SET %s=TRUE WHERE ship_id={1} AND %s=FALSE;",
                     column, column);
    else
        n = snprintf(query, sizeof query,
                     "UPDATE ships SET %s=COALESCE(%s,0)+{1} "
                     "WHERE ship_id={2} "
                     "AND COALESCE(%s,0)+{1} <= "
                     "(SELECT COALESCE(%s,0) FROM shiptypes "
                     "WHERE shiptypes_id=ships.type_id);",
                     column, column, column, limit);
    if (n < 0 || (size_t)n >= sizeof query)
        return ERR_DB_QUERY_FAILED;
    char sql[768];
    if (sql_build(db, query, sql, sizeof sql) != 0)
        return ERR_DB_QUERY_FAILED;
    db_error_t err = {0};
    int64_t rows = 0;
    db_bind_t params[2];
    size_t count;
    if (boolean_item) {
        params[0] = db_bind_i64(ship_id);
        count = 1;
    } else {
        params[0] = db_bind_i64(quantity);
        params[1] = db_bind_i64(ship_id);
        count = 2;
    }
    if (!db_exec_rows_affected(db, sql, params, count, &rows, &err))
        return err.code ? err.code : ERR_DB_QUERY_FAILED;
    return rows == 1 ? 0 : ERR_DB_BUSY;
}

int repo_stardock_check_shipyard_location(db_t *db, int32_t sector_id, int32_t player_id, int32_t *out_port_id)
{
    /* SQL_VERBATIM: Q7 */
    /* SQL_VERBATIM: Q11 */
    /* Check if player is docked at stardock or black market port */
    const char *sql = "SELECT p.port_id FROM ports p "
                      "JOIN ships s ON s.ported = p.port_id "
                      "JOIN players pl ON pl.ship_id = s.ship_id "
                      "JOIN porttypes pt ON p.porttype_id = pt.porttype_id "
                      "WHERE p.sector_id = {1} AND pl.player_id = {2} "
                      "AND (pt.is_stardock = true OR pt.is_black_market = true "
                      "OR EXISTS (SELECT 1 FROM shipyard_inventory si "
                      "WHERE si.port_id = p.port_id AND si.enabled = true));";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i64(sector_id), db_bind_i64(player_id) }, 2, &res, &err)) {
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
    /* Wait, Q12 also includes s.credits at index 3. Let's fix. */
    const char *sql_q12 = "SELECT "
                         "p.alignment, p.commission_id, p.experience, p.credits, "
                         "s.fighters, s.shields, s.mines, s.limpets, s.genesis, s.detonators, s.probes, s.cloaking_devices, "
                         "s.colonists, s.ore, s.organics, s.equipment, "
                         "s.has_transwarp, s.has_planet_scanner, s.has_long_range_scanner, s.ship_id, s.type_id, st.basecost "
                         "FROM players p JOIN ships s ON p.ship_id = s.ship_id JOIN shiptypes st ON s.type_id = st.shiptypes_id "
                         "WHERE p.player_id = {1};";

    char sql_converted[1024];
    sql_build(db, sql_q12, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i64(player_id) }, 1, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_get_shipyard_inventory(db_t *db, int32_t port_id, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q9 */
    const char *sql = "SELECT si.ship_type_id, st.name, st.basecost, st.required_alignment, st.required_commission, st.required_experience, st.maxholds, st.maxfighters, st.maxshields, st.maxgenesis, st.max_detonators, st.max_probes, st.max_cloaks, st.can_transwarp, st.can_planet_scan, st.can_long_range_scan, st.maxmines, st.maxlimpets "
                      "FROM shipyard_inventory si JOIN shiptypes st ON si.ship_type_id = st.shiptypes_id "
                      "WHERE si.port_id = {1} AND si.enabled = {2} AND st.enabled = {3};";
    char sql_converted[1024];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i64(port_id), db_bind_bool(true), db_bind_bool(true) }, 3, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_get_shiptype_details(db_t *db, int32_t type_id, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q13 */
    const char *sql = "SELECT basecost, required_alignment, required_commission, required_experience, maxholds, maxfighters, maxshields, maxgenesis, max_detonators, max_probes, max_cloaks, can_transwarp, can_planet_scan, can_long_range_scan, maxmines, maxlimpets, name FROM shiptypes WHERE shiptypes_id = {1} AND enabled = {2};";
    char sql_converted[1024];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i64(type_id), db_bind_bool(true) }, 2, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_upgrade_ship(db_t *db, int32_t new_type_id, const char *new_name, int64_t cost, int32_t ship_id)
{
    if (!db || new_type_id <= 0 || !new_name || !new_name[0]
        || cost < 0 || ship_id <= 0)
        return ERR_INVALID_ARG;
    /* Credits belong to the player. Resolve the player through the ship link,
     * require exactly one owner, and conditionally debit before changing the
     * ship. The nested transaction makes the two writes atomic even when a
     * later ship update (for example its type FK) fails. */
    db_error_t err = {0};
    if (!db_tx_begin(db, DB_TX_DEFAULT, &err))
        return err.code ? err.code : ERR_DB_TX;

    const char *debit_sql = "UPDATE players SET credits = credits - {1} "
                            "WHERE ship_id = {2} AND credits >= {1} "
                            "AND (SELECT COUNT(*) FROM players p2 WHERE p2.ship_id = {2}) = 1;";
    char sql_converted[512];
    if (sql_build(db, debit_sql, sql_converted, sizeof(sql_converted)) != 0) {
        (void)db_tx_rollback(db, &err);
        return ERR_DB_QUERY_FAILED;
    }
    int64_t rows = 0;
    if (!db_exec_rows_affected(db, sql_converted,
                               (db_bind_t[]){db_bind_i64(cost),
                                             db_bind_i64(ship_id)},
                               2, &rows, &err)) {
        int rc = err.code ? err.code : ERR_DB_QUERY_FAILED;
        (void)db_tx_rollback(db, &err);
        return rc;
    }
    if (rows != 1) {
        (void)db_tx_rollback(db, &err);
        return ERR_DB_BUSY;
    }

    const char *ship_sql = "UPDATE ships SET type_id = {1}, name = {2} WHERE ship_id = {3};";
    if (sql_build(db, ship_sql, sql_converted, sizeof(sql_converted)) != 0) {
        (void)db_tx_rollback(db, &err);
        return ERR_DB_QUERY_FAILED;
    }
    rows = 0;
    if (!db_exec_rows_affected(db, sql_converted,
                               (db_bind_t[]){db_bind_i64(new_type_id),
                                             db_bind_text(new_name),
                                             db_bind_i64(ship_id)},
                               3, &rows, &err)) {
        int rc = err.code ? err.code : ERR_DB_QUERY_FAILED;
        (void)db_tx_rollback(db, &err);
        return rc;
    }
    if (rows != 1) {
        (void)db_tx_rollback(db, &err);
        return ERR_DB_BUSY;
    }
    if (!db_tx_commit(db, &err)) {
        int rc = err.code ? err.code : ERR_DB_TX;
        (void)db_tx_rollback(db, &err);
        return rc;
    }
    return 0;
}

int
repo_stardock_purchase_used_ship (db_t *db, int32_t player_id,
                                  int32_t port_id, int64_t used_inventory_id,
                                  int resale_factor_bp,
                                  repo_used_ship_purchase_t *out)
{
    if (!db || player_id <= 0 || port_id <= 0 || used_inventory_id <= 0
        || resale_factor_bp < 0 || resale_factor_bp > 10000 || !out)
        return ERR_INVALID_ARG;
    memset (out, 0, sizeof *out);
    db_error_t err = {0};
    if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
        return err.code ? err.code : ERR_DB_TX;
    char sql[1024];
    db_res_t *res = NULL;
    if (sql_build (db,
        "SELECT COALESCE(ship_id,0),COALESCE(credits,0),COALESCE(sector_id,0) "
        "FROM players WHERE player_id={1} FOR UPDATE", sql, sizeof sql) != 0
        || !db_query (db, sql, (db_bind_t[]){db_bind_i32 (player_id)}, 1,
                      &res, &err))
        goto used_fail;
    if (!db_res_step (res, &err))
      { db_res_finalize (res); res = NULL; err.code = ERR_DB_NOT_FOUND; goto used_fail; }
    int32_t old_ship_id = db_res_col_i32 (res, 0, &err);
    int64_t credits = db_res_col_i64 (res, 1, &err);
    int32_t sector_id = db_res_col_i32 (res, 2, &err);
    db_res_finalize (res); res = NULL;
    if (err.code || old_ship_id <= 0 || sector_id <= 0)
      { if (!err.code) err.code = ERR_DB_CONSTRAINT; goto used_fail; }
    int32_t actual_port = 0;
    if (repo_stardock_check_shipyard_location (db, sector_id, player_id,
                                                &actual_port) != 0
        || actual_port != port_id)
      { err.code = ERR_NOT_AT_SHIPYARD; goto used_fail; }

    const char *used_tmpl =
      "SELECT u.ship_id,u.port_id,a.hull_valuation,a.equipment_valuation,"
      "s.type_id,s.name,COALESCE(s.destroyed,FALSE) "
      "FROM shipyard_used_inventory u JOIN ship_sale_audit a ON a.sale_id=u.sale_id "
      "JOIN ships s ON s.ship_id=u.ship_id WHERE u.used_inventory_id={1} FOR UPDATE";
    if (sql_build (db, used_tmpl, sql, sizeof sql) != 0
        || !db_query (db, sql,
             (db_bind_t[]){db_bind_i64 (used_inventory_id)}, 1, &res, &err))
      goto used_fail;
    if (!db_res_step (res, &err))
      { db_res_finalize (res); res = NULL; err.code = ERR_DB_NOT_FOUND; goto used_fail; }
    int32_t used_ship_id = db_res_col_i32 (res, 0, &err);
    int32_t inventory_port_id = db_res_col_i32 (res, 1, &err);
    int64_t hull_value = db_res_col_i64 (res, 2, &err);
    int64_t equipment_value = db_res_col_i64 (res, 3, &err);
    int32_t type_id = db_res_col_i32 (res, 4, &err);
    const char *name = db_res_col_text (res, 5, &err);
    snprintf (out->ship_name, sizeof out->ship_name, "%s", name ? name : "Used vessel");
    bool destroyed = db_res_col_bool (res, 6, &err);
    db_res_finalize (res); res = NULL;
    if (err.code) goto used_fail;
    if (inventory_port_id != port_id || used_ship_id <= 0 || type_id <= 0
        || destroyed || used_ship_id == old_ship_id)
      { err.code = ERR_DB_CONSTRAINT; goto used_fail; }
    __int128 value = (__int128) hull_value + equipment_value;
    __int128 quoted = value * resale_factor_bp / 10000;
    if (hull_value < 0 || equipment_value < 0 || quoted > INT64_MAX)
      { err.code = ERR_INVALID_ARG; goto used_fail; }
    int64_t price = (int64_t) quoted;
    if (credits < price) { err.code = ERR_INSUFFICIENT_FUNDS; goto used_fail; }
    int64_t affected = 0;
    if (price > 0)
      {
        if (sql_build (db, "UPDATE players SET credits=credits-{1} WHERE player_id={2} AND credits>={1}",
                       sql, sizeof sql) != 0)
          { err.code = ERR_DB_QUERY_FAILED; goto used_fail; }
        if (!db_exec_rows_affected (db, sql,
              (db_bind_t[]){db_bind_i64 (price),db_bind_i32 (player_id)}, 2,
              &affected, &err) || affected != 1)
          { if (!err.code) err.code = ERR_DB_CONSTRAINT; goto used_fail; }
      }

    if (sql_build (db,
        "UPDATE ships SET sector_id={1} WHERE ship_id={2} AND destroyed=FALSE",
        sql, sizeof sql) != 0
        || !db_exec (db, sql,
             (db_bind_t[]){db_bind_i32 (sector_id),db_bind_i32 (used_ship_id)},
             2, &err))
      goto used_fail;
    if (sql_build (db,
        "UPDATE ship_ownership SET is_primary=FALSE WHERE player_id={1} "
        "AND ship_id={2} AND is_primary=TRUE", sql, sizeof sql) != 0)
      { err.code = ERR_DB_QUERY_FAILED; goto used_fail; }
    affected = 0;
    if (!db_exec_rows_affected (db, sql,
          (db_bind_t[]){db_bind_i32 (player_id),db_bind_i32 (old_ship_id)}, 2,
          &affected, &err) || affected != 1)
      { if (!err.code) err.code = ERR_DB_CONSTRAINT; goto used_fail; }
    if (sql_build (db,
        "INSERT INTO ship_ownership(ship_id,player_id,role_id,is_primary) "
        "VALUES({1},{2},1,TRUE)", sql, sizeof sql) != 0
        || !db_exec (db, sql,
             (db_bind_t[]){db_bind_i32 (used_ship_id),db_bind_i32 (player_id)},
             2, &err))
      goto used_fail;
    if (sql_build (db,
        "DELETE FROM shipyard_used_inventory WHERE used_inventory_id={1} "
        "AND ship_id={2} AND port_id={3}", sql, sizeof sql) != 0)
      { err.code = ERR_DB_QUERY_FAILED; goto used_fail; }
    affected = 0;
    if (!db_exec_rows_affected (db, sql,
          (db_bind_t[]){db_bind_i64 (used_inventory_id),
            db_bind_i32 (used_ship_id),db_bind_i32 (port_id)}, 3,
          &affected, &err) || affected != 1)
      { if (!err.code) err.code = ERR_DB_CONSTRAINT; goto used_fail; }
    if (!db_tx_commit (db, &err)) goto used_fail;
    out->ship_id = used_ship_id;
    out->ship_type_id = type_id;
    out->credits_spent = price;
    return 0;

used_fail:
    if (res) db_res_finalize (res);
    (void) db_tx_rollback (db, &err);
    return err.code ? err.code : ERR_DB_EXEC_FAILED;
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
    const char *sql = "SELECT 1 FROM taverns WHERE sector_id = {1} AND enabled = {2};";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i64(sector_id), db_bind_bool(true) }, 2, &res, &err)) {
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
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i64(player_id) }, 1, out_res, &err)) {
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
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, &err)) {
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
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, &err)) {
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
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i64(amount), db_bind_i64(player_id) }, 2, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_mark_loan_defaulted(db_t *db, int32_t player_id)
{
    /* SQL_VERBATIM: Q21 */
    const char *sql = "UPDATE tavern_loans SET is_defaulted = TRUE WHERE player_id = {1};";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &err)) {
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
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i64(new_principal), db_bind_i64(player_id) }, 2, &err)) {
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
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_text(draw_date), db_bind_i64(player_id), db_bind_i64(number), db_bind_i64(cost), db_bind_timestamp_text(purchased_at) }, 5, &err)) {
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
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i64(player_id), db_bind_text(draw_date) }, 2, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_insert_deadpool_bet(db_t *db, int32_t bettor_id, int32_t target_id, int64_t amount, int32_t odds_bp, int32_t placed_at, int32_t expires_at)
{
    /* SQL_VERBATIM: Q27 */
    const char *sql = "INSERT INTO tavern_deadpool_bets (bettor_id, target_id, amount, odds_bp, placed_at, expires_at, resolved) VALUES ({1}, {2}, {3}, {4}, {5}, {6}, FALSE);";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i64(bettor_id), db_bind_i64(target_id), db_bind_i64(amount), db_bind_i64(odds_bp), db_bind_timestamp_text(placed_at), db_bind_timestamp_text(expires_at) }, 6, &err)) {
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
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i64(player_id), db_bind_text(text), db_bind_timestamp_text(created_at) }, 3, &err)) {
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
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i64(limit) }, 1, &err)) {
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
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i64(alignment_gain), db_bind_i64(player_id) }, 2, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_insert_loan(db_t *db, int32_t player_id, int64_t principal, int32_t interest_rate_bp, int32_t due_date)
{
    /* SQL_VERBATIM: Q36 */
    const char *sql = "INSERT INTO tavern_loans (player_id, principal, interest_rate, due_date, is_defaulted) VALUES ({1}, {2}, {3}, {4}, FALSE);";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i64(player_id), db_bind_i64(principal), db_bind_i64(interest_rate_bp), db_bind_timestamp_text(due_date) }, 4, &err)) {
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
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &err)) {
        return 0;
    }
    return err.code;
}

int repo_stardock_update_loan_repayment(db_t *db, int32_t player_id, int64_t new_principal)
{
    /* SQL_VERBATIM: Q38 */
    const char *sql = "UPDATE tavern_loans SET principal = {1}, is_defaulted = FALSE WHERE player_id = {2};";
    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    db_error_t err;
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i64(new_principal), db_bind_i64(player_id) }, 2, &err)) {
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
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i64(player_id), db_bind_i64(winnings), db_bind_i64(now) }, 3, &err)) {
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
