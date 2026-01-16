#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "repo_combat.h"
#include "repo_players.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "db/db_api.h"
#include "db/sql_driver.h"

/* MSL */
bool db_combat_is_msl_sector(db_t *db, int sector_id) {
    if (!db) return false;
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql[256];
    sql_build(db, "SELECT 1 FROM msl_sectors WHERE sector_id = {1} LIMIT 1;", sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(sector_id) }, 1, &res, &err)) return false;
    bool is_msl = db_res_step(res, &err);
    db_res_finalize(res);
    return is_msl && (err.code == 0);
}

/* Fighters on Entry */
int db_combat_get_ship_stats(db_t *db, int ship_id, repo_combat_ship_t *out) {
    if (!db || !out) return -1;
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql[256];
    sql_build(db, "SELECT ship_id, hull, fighters, shields FROM ships WHERE ship_id = {1};", sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(ship_id) }, 1, &res, &err)) return -1;
    if (!db_res_step(res, &err)) { db_res_finalize(res); return 0; }
    out->id = (int)db_res_col_i32(res, 0, &err);
    out->hull = (int)db_res_col_i32(res, 1, &err);
    out->fighters = (int)db_res_col_i32(res, 2, &err);
    out->shields = (int)db_res_col_i32(res, 3, &err);
    db_res_finalize(res);
    return (err.code == 0) ? 1 : -1;
}

int db_combat_update_ship_stats(db_t *db, int ship_id, int hull, int fighters, int shields) {
    if (!db) return -1;
    db_error_t err = {0};
    char sql[256];
    sql_build(db, "UPDATE ships SET hull = {1}, fighters = {2}, shields = {3} WHERE ship_id = {4};", sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(hull), db_bind_i32(fighters), db_bind_i32(shields), db_bind_i32(ship_id) }, 4, &err)) return -1;
    return 0;
}

int db_combat_get_hostile_fighters(db_t *db, int sector_id, int **out_ids, int **out_quantities, int **out_owners, int **out_corps, int **out_modes, int *out_count) {
    if (!db || !out_ids || !out_quantities || !out_owners || !out_corps || !out_modes || !out_count) return -1;
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql[256];
    sql_build(db, "SELECT sector_assets_id, quantity, owner_id, corporation_id, offensive_setting FROM sector_assets WHERE sector_id = {1} AND asset_type = 2 AND quantity > 0;", sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(sector_id) }, 1, &res, &err)) return -1;
    int cap = 16, count = 0;
    *out_ids = malloc(cap * sizeof(int)); *out_quantities = malloc(cap * sizeof(int));
    *out_owners = malloc(cap * sizeof(int)); *out_corps = malloc(cap * sizeof(int));
    *out_modes = malloc(cap * sizeof(int));
    while (db_res_step(res, &err)) {
        if (count >= cap) {
            cap *= 2;
            *out_ids = realloc(*out_ids, cap * sizeof(int)); *out_quantities = realloc(*out_quantities, cap * sizeof(int));
            *out_owners = realloc(*out_owners, cap * sizeof(int)); *out_corps = realloc(*out_corps, cap * sizeof(int));
            *out_modes = realloc(*out_modes, cap * sizeof(int));
        }
        (*out_ids)[count] = (int)db_res_col_i32(res, 0, &err);
        (*out_quantities)[count] = (int)db_res_col_i32(res, 1, &err);
        (*out_owners)[count] = (int)db_res_col_i32(res, 2, &err);
        (*out_corps)[count] = (int)db_res_col_i32(res, 3, &err);
        (*out_modes)[count] = (int)db_res_col_i32(res, 4, &err);
        count++;
    }
    db_res_finalize(res);
    *out_count = count;
    return (err.code == 0) ? 0 : -1;
}

int db_combat_delete_sector_asset(db_t *db, int asset_id) {
    if (!db) return -1;
    db_error_t err = {0};
    char sql[256];
    sql_build(db, "DELETE FROM sector_assets WHERE sector_assets_id = {1};", sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(asset_id) }, 1, &err)) return -1;
    return 0;
}

/* Flee */
int db_combat_get_flee_info(db_t *db, int ship_id, int *maxholds, int *sector_id) {
    if (!db || !maxholds || !sector_id) return -1;
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql[256];
    sql_build(db, "SELECT t.maxholds, s.sector_id FROM ships s JOIN shiptypes t ON s.type_id = t.shiptypes_id WHERE s.ship_id = {1};", sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(ship_id) }, 1, &res, &err)) return -1;
    if (db_res_step(res, &err)) {
        *maxholds = (int)db_res_col_i32(res, 0, &err);
        *sector_id = (int)db_res_col_i32(res, 1, &err);
    } else { db_res_finalize(res); return -1; }
    db_res_finalize(res);
    return (err.code == 0) ? 0 : -1;
}

int db_combat_get_first_adjacent_sector(db_t *db, int sector_id, int *dest_sector) {
    if (!db || !dest_sector) return -1;
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql[256];
    sql_build(db, "SELECT to_sector FROM sector_warps WHERE from_sector = {1} ORDER BY to_sector ASC LIMIT 1;", sql, sizeof(sql));
    *dest_sector = 0;
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(sector_id) }, 1, &res, &err)) return -1;
    if (db_res_step(res, &err)) *dest_sector = (int)db_res_col_i32(res, 0, &err);
    db_res_finalize(res);
    return (err.code == 0) ? 0 : -1;
}

int db_combat_move_ship_and_player(db_t *db, int ship_id, int player_id, int dest_sector) {
    if (!db) return -1;
    db_error_t err = {0};
    char sql_ship[256];
    sql_build(db, "UPDATE ships SET sector_id = {1} WHERE ship_id = {2};", sql_ship, sizeof(sql_ship));
    if (!db_exec(db, sql_ship, (db_bind_t[]){ db_bind_i32(dest_sector), db_bind_i32(ship_id) }, 2, &err)) return -1;
    char sql_player[256];
    sql_build(db, "UPDATE players SET sector_id = {1} WHERE player_id = {2};", sql_player, sizeof(sql_player));
    if (!db_exec(db, sql_player, (db_bind_t[]){ db_bind_i32(dest_sector), db_bind_i32(player_id) }, 2, &err)) return -1;
    return 0;
}

/* Deploy Assets List */
static int db_combat_list_assets_internal(db_t *db, int player_id, const char *sql_query, json_t **out_array) {
    if (!db || !out_array) return -1;
    *out_array = json_array();
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql_built[2048];
    sql_build(db, sql_query, sql_built, sizeof(sql_built));
    if (!db_query(db, sql_built, (db_bind_t[]){ db_bind_i32(player_id), db_bind_i32(player_id) }, 2, &res, &err)) return -1;
    while (db_res_step(res, &err)) {
        json_t *entry = json_object();
        json_object_set_new(entry, "asset_id", json_integer(db_res_col_i32(res, 0, &err)));
        json_object_set_new(entry, "sector_id", json_integer(db_res_col_i32(res, 1, &err)));
        json_object_set_new(entry, "count", json_integer(db_res_col_i32(res, 2, &err)));
        json_object_set_new(entry, "offense_mode", json_integer(db_res_col_i32(res, 3, &err)));
        json_object_set_new(entry, "player_id", json_integer(db_res_col_i32(res, 4, &err)));
        const char *pname = db_res_col_text(res, 5, &err);
        json_object_set_new(entry, "player_name", json_string(pname ? pname : "Unknown"));
        json_object_set_new(entry, "corp_id", json_integer(db_res_col_i32(res, 6, &err)));
        const char *ctag = db_res_col_text(res, 7, &err);
        json_object_set_new(entry, "corp_tag", json_string(ctag ? ctag : ""));
        json_object_set_new(entry, "type", json_integer(db_res_col_i32(res, 8, &err)));
        json_array_append_new(*out_array, entry);
    }
    db_res_finalize(res);
    return (err.code == 0) ? 0 : -1;
}

int db_combat_list_fighters(db_t *db, int player_id, json_t **out_array) {
    const char *sql = "SELECT sa.sector_assets_id, sa.sector_id, sa.quantity, sa.offensive_setting, sa.owner_id, p.name, c.corporation_id, c.tag, sa.asset_type FROM sector_assets sa JOIN players p ON sa.owner_id = p.player_id LEFT JOIN corp_members cm_player ON cm_player.player_id = sa.owner_id LEFT JOIN corporations c ON c.corporation_id = cm_player.corporation_id WHERE sa.asset_type = 2 AND (sa.owner_id = {1} OR sa.owner_id IN (SELECT cm_member.player_id FROM corp_members cm_member WHERE cm_member.corporation_id = (SELECT cm_self.corporation_id FROM corp_members cm_self WHERE cm_self.player_id = {2}))) ORDER BY sa.sector_id ASC;";
    return db_combat_list_assets_internal(db, player_id, sql, out_array);
}

int db_combat_list_mines(db_t *db, int player_id, json_t **out_array) {
    const char *sql = "SELECT sa.sector_assets_id, sa.sector_id, sa.quantity, sa.offensive_setting, sa.owner_id, p.name, c.corporation_id, c.tag, sa.asset_type FROM sector_assets sa JOIN players p ON sa.owner_id = p.player_id LEFT JOIN corp_members cm_player ON cm_player.player_id = sa.owner_id LEFT JOIN corporations c ON c.corporation_id = cm_player.corporation_id WHERE sa.asset_type IN (1, 4) AND (sa.owner_id = {1} OR sa.owner_id IN (SELECT cm_member.player_id FROM corp_members cm_member WHERE cm_member.corporation_id = (SELECT cm_self.corporation_id FROM corp_members cm_self WHERE cm_self.player_id = {2}))) ORDER BY sa.sector_id ASC, sa.asset_type ASC;";
    return db_combat_list_assets_internal(db, player_id, sql, out_array);
}

/* Deploy Fighters */
int db_combat_get_ship_sector_locked(db_t *db, int ship_id, int *sector_id) {
    if (!db || !sector_id) return -1;
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql_tmpl[256], sql[256];
    snprintf(sql_tmpl, sizeof(sql_tmpl), "SELECT sector_id FROM ships WHERE ship_id = {1}%s;", sql_for_update_skip_locked(db));
    sql_build(db, sql_tmpl, sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(ship_id) }, 1, &res, &err)) return -1;
    if (db_res_step(res, &err)) *sector_id = (int)db_res_col_i32(res, 0, &err); else *sector_id = -1;
    db_res_finalize(res);
    return (err.code == 0) ? 0 : -1;
}

int db_combat_lock_sector(db_t *db, int sector_id) {
    if (!db) return -1;
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql_tmpl[256], sql[256];
    snprintf(sql_tmpl, sizeof(sql_tmpl), "SELECT sector_id FROM sectors WHERE sector_id = {1}%s;", sql_for_update_skip_locked(db));
    sql_build(db, sql_tmpl, sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(sector_id) }, 1, &res, &err)) return -1;
    int found = 0; if (db_res_step(res, &err)) found = 1;
    db_res_finalize(res);
    return (found && err.code == 0) ? 0 : -1;
}

int db_combat_sum_sector_fighters(db_t *db, int sector_id, int *total) {
    if (!db || !total) return -1;
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql[256];
    sql_build(db, "SELECT COALESCE(SUM(quantity), 0) FROM sector_assets WHERE sector_id = {1} AND asset_type = 2;", sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(sector_id) }, 1, &res, &err)) return -1;
    if (db_res_step(res, &err)) *total = (int)db_res_col_i32(res, 0, &err);
    db_res_finalize(res);
    return (err.code == 0) ? 0 : -1;
}

int db_combat_consume_ship_fighters(db_t *db, int ship_id, int amount) {
    if (!db) return -1;
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql_check[256];
    sql_build(db, "SELECT fighters FROM ships WHERE ship_id = {1};", sql_check, sizeof(sql_check));
    if (!db_query(db, sql_check, (db_bind_t[]){ db_bind_i32(ship_id) }, 1, &res, &err)) return -1;
    int current = 0; if (db_res_step(res, &err)) current = (int)db_res_col_i32(res, 0, &err);
    db_res_finalize(res);
    if (current < amount) return 2007;
    char sql_upd[256];
    sql_build(db, "UPDATE ships SET fighters = fighters - {1} WHERE ship_id = {2};", sql_upd, sizeof(sql_upd));
    if (!db_exec(db, sql_upd, (db_bind_t[]){ db_bind_i32(amount), db_bind_i32(ship_id) }, 2, &err)) return -1;
    return 0;
}

int db_combat_insert_sector_fighters(db_t *db, int sector_id, int owner_id, int corp_id, int mode, int amount) {
    if (!db) return -1;
    db_error_t err = {0};
    char sql[512];
    sql_build(db, "INSERT INTO sector_assets (sector_id, owner_id, corporation_id, asset_type, quantity, offensive_setting, deployed_at) VALUES ({1}, {2}, {3}, 2, {4}, {5}, CURRENT_TIMESTAMP);", sql, sizeof(sql));
    db_bind_t params[5] = { db_bind_i32(sector_id), db_bind_i32(owner_id), db_bind_i32(corp_id), db_bind_i32(amount), db_bind_i32(mode) };
    if (!db_exec(db, sql, params, 5, &err)) return -1;
    return 0;
}

int db_combat_get_max_asset_id(db_t *db, int sector_id, int owner_id, int asset_type, int64_t *out_id) {
    if (!db || !out_id) return -1;
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql[256];
    sql_build(db, "SELECT MAX(sector_assets_id) FROM sector_assets WHERE sector_id={1} AND owner_id={2} AND asset_type={3};", sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(sector_id), db_bind_i32(owner_id), db_bind_i32(asset_type) }, 3, &res, &err)) return -1;
    if (db_res_step(res, &err)) *out_id = db_res_col_i64(res, 0, &err);
    db_res_finalize(res);
    return (err.code == 0) ? 0 : -1;
}

/* Deploy Mines */
int db_combat_sum_sector_mines(db_t *db, int sector_id, int *total) {
    if (!db || !total) return -1;
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql[256];
    sql_build(db, "SELECT COALESCE(SUM(quantity), 0) FROM sector_assets WHERE sector_id = {1} AND asset_type IN (1, 4);", sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(sector_id) }, 1, &res, &err)) return -1;
    if (db_res_step(res, &err)) *total = (int)db_res_col_i32(res, 0, &err);
    db_res_finalize(res);
    return (err.code == 0) ? 0 : -1;
}

int db_combat_consume_ship_mines(db_t *db, int ship_id, int asset_type, int amount) {
    if (!db) return -1;
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql_check[256];
    (void)asset_type;
    sql_build(db, "SELECT mines FROM ships WHERE ship_id = {1};", sql_check, sizeof(sql_check));
    if (!db_query(db, sql_check, (db_bind_t[]){ db_bind_i32(ship_id) }, 1, &res, &err)) return -1;
    int current = 0; if (db_res_step(res, &err)) current = (int)db_res_col_i32(res, 0, &err);
    db_res_finalize(res);
    if (current < amount) return 2007;
    char sql_upd[256];
    sql_build(db, "UPDATE ships SET mines = mines - {1} WHERE ship_id = {2};", sql_upd, sizeof(sql_upd));
    if (!db_exec(db, sql_upd, (db_bind_t[]){ db_bind_i32(amount), db_bind_i32(ship_id) }, 2, &err)) return -1;
    return 0;
}

int db_combat_insert_sector_mines(db_t *db, int sector_id, int owner_id, int corp_id, int asset_type, int mode, int amount) {
    if (!db) return -1;
    db_error_t err = {0};
    char sql[512];
    sql_build(db, "INSERT INTO sector_assets (sector_id, owner_id, corporation_id, asset_type, quantity, offensive_setting, deployed_at, ttl) VALUES ({1}, {2}, {3}, {4}, {5}, {6}, CURRENT_TIMESTAMP, {7});", sql, sizeof(sql));
    db_bind_t params[7] = { db_bind_i32(sector_id), db_bind_i32(owner_id), db_bind_i32(corp_id), db_bind_i32(asset_type), db_bind_i32(amount), db_bind_i32(mode), db_bind_i64(0) };
    if (!db_exec(db, sql, params, 7, &err)) return -1;
    return 0;
}

/* Load Ship Combat Stats */
int db_combat_load_ship_full_locked(db_t *db, int ship_id, repo_combat_ship_full_t *out, bool skip_locked) {
    if (!db || !out) return -1;
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql_tmpl[1024], sql[1024];
    snprintf(sql_tmpl, sizeof(sql_tmpl), "SELECT s.ship_id, s.hull, s.shields, s.fighters, s.sector_id, s.name, st.offense, st.defense, st.maxattack, op.player_id, cm.corporation_id FROM ships s JOIN shiptypes st ON s.type_id = st.shiptypes_id JOIN ship_ownership op ON op.ship_id = s.ship_id AND op.is_primary = TRUE LEFT JOIN corp_members cm ON cm.player_id = op.player_id WHERE s.ship_id = {1} %s", skip_locked ? "FOR UPDATE SKIP LOCKED" : "FOR UPDATE");
    sql_build(db, sql_tmpl, sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(ship_id) }, 1, &res, &err)) return -1;
    if (!db_res_step(res, &err)) { db_res_finalize(res); return -1; }
    out->id = (int)db_res_col_i32(res, 0, &err); out->hull = (int)db_res_col_i32(res, 1, &err);
    out->shields = (int)db_res_col_i32(res, 2, &err); out->fighters = (int)db_res_col_i32(res, 3, &err);
    out->sector = (int)db_res_col_i32(res, 4, &err); const char *nm = db_res_col_text(res, 5, &err);
    strncpy(out->name, nm ? nm : "", sizeof(out->name)-1);
    out->attack_power = (int)db_res_col_i32(res, 6, &err); out->defense_power = (int)db_res_col_i32(res, 7, &err);
    out->max_attack = (int)db_res_col_i32(res, 8, &err); out->player_id = (int)db_res_col_i32(res, 9, &err);
    out->corp_id = (int)db_res_col_i32(res, 10, &err);
    db_res_finalize(res);
    return (err.code == 0) ? 0 : -1;
}

int db_combat_load_ship_full_unlocked(db_t *db, int ship_id, repo_combat_ship_full_t *out) {
    if (!db || !out) return -1;
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql[1024];
    sql_build(db, "SELECT s.ship_id, s.hull, s.shields, s.fighters, s.sector_id, s.name, st.offense, st.defense, st.maxattack, op.player_id, cm.corporation_id FROM ships s JOIN shiptypes st ON s.type_id = st.shiptypes_id JOIN ship_ownership op ON op.ship_id = s.ship_id AND op.is_primary = TRUE LEFT JOIN corp_members cm ON cm.player_id = op.player_id WHERE s.ship_id = {1}", sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(ship_id) }, 1, &res, &err)) return -1;
    if (!db_res_step(res, &err)) { db_res_finalize(res); return -1; }
    out->id = (int)db_res_col_i32(res, 0, &err); out->hull = (int)db_res_col_i32(res, 1, &err);
    out->shields = (int)db_res_col_i32(res, 2, &err); out->fighters = (int)db_res_col_i32(res, 3, &err);
    out->sector = (int)db_res_col_i32(res, 4, &err); const char *nm = db_res_col_text(res, 5, &err);
    strncpy(out->name, nm ? nm : "", sizeof(out->name)-1);
    out->attack_power = (int)db_res_col_i32(res, 6, &err); out->defense_power = (int)db_res_col_i32(res, 7, &err);
    out->max_attack = (int)db_res_col_i32(res, 8, &err); out->player_id = (int)db_res_col_i32(res, 9, &err);
    out->corp_id = (int)db_res_col_i32(res, 10, &err);
    db_res_finalize(res);
    return (err.code == 0) ? 0 : -1;
}

int db_combat_persist_ship_damage(db_t *db, int ship_id, int hull, int shields, int fighters_lost) {
    if (!db) return -1;
    db_error_t err = {0};
    char sql[256];
    sql_build(db, "UPDATE ships SET hull = {1}, shields = {2}, fighters = GREATEST(0, fighters - {3}) WHERE ship_id = {4}", sql, sizeof(sql));
    int64_t rows = 0;
    if (!db_exec_rows_affected(db, sql, (db_bind_t[]){ db_bind_i32(hull), db_bind_i32(shields), db_bind_i32(fighters_lost), db_bind_i32(ship_id) }, 4, &rows, &err)) return -1;
    return (rows == 1) ? 0 : -1;
}

/* Armid/Limpet/Quasar */
int db_combat_select_armid_mines_locked(db_t *db, int sector_id, json_t **out_array) {
    if (!db || !out_array) return -1;
    *out_array = json_array();
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql_tmpl[512], sql[512];
    snprintf(sql_tmpl, sizeof(sql_tmpl), "SELECT sector_assets_id, quantity, offensive_setting, owner_id, corporation_id, ttl FROM sector_assets WHERE sector_id = {1} AND asset_type = 1%s;", sql_for_update_skip_locked(db));
    sql_build(db, sql_tmpl, sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(sector_id) }, 1, &res, &err)) return -1;
    while(db_res_step(res, &err)) {
        json_t *obj = json_object();
        json_object_set_new(obj, "id", json_integer(db_res_col_i32(res, 0, &err)));
        json_object_set_new(obj, "quantity", json_integer(db_res_col_i32(res, 1, &err)));
        json_object_set_new(obj, "offense", json_integer(db_res_col_i32(res, 2, &err)));
        json_object_set_new(obj, "owner_id", json_integer(db_res_col_i32(res, 3, &err)));
        json_object_set_new(obj, "corp_id", json_integer(db_res_col_i32(res, 4, &err)));
        json_object_set_new(obj, "ttl", json_integer(db_res_col_i64(res, 5, &err)));
        json_array_append_new(*out_array, obj);
    }
    db_res_finalize(res);
    return (err.code == 0) ? 0 : -1;
}

int db_combat_select_limpets_locked(db_t *db, int sector_id, json_t **out_array) {
    if (!db || !out_array) return -1;
    *out_array = json_array();
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql_tmpl[512], sql[512];
    snprintf(sql_tmpl, sizeof(sql_tmpl), "SELECT sector_assets_id, quantity, owner_id, corporation_id, ttl FROM sector_assets WHERE sector_id = {1} AND asset_type = 4 AND quantity > 0%s;", sql_for_update_skip_locked(db));
    sql_build(db, sql_tmpl, sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(sector_id) }, 1, &res, &err)) return -1;
    while(db_res_step(res, &err)) {
        json_t *obj = json_object();
        json_object_set_new(obj, "id", json_integer(db_res_col_i32(res, 0, &err)));
        json_object_set_new(obj, "quantity", json_integer(db_res_col_i32(res, 1, &err)));
        json_object_set_new(obj, "owner_id", json_integer(db_res_col_i32(res, 2, &err)));
        json_object_set_new(obj, "corp_id", json_integer(db_res_col_i32(res, 3, &err)));
        json_object_set_new(obj, "ttl", json_integer(db_res_col_i64(res, 4, &err)));
        json_array_append_new(*out_array, obj);
    }
    db_res_finalize(res);
    return (err.code == 0) ? 0 : -1;
}

int db_combat_check_limpet_attached(db_t *db, int ship_id, int owner_id, bool *attached) {
    if (!db || !attached) return -1;
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql[256];
    sql_build(db, "SELECT 1 FROM limpet_attached WHERE ship_id = {1} AND owner_player_id = {2} LIMIT 1;", sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(ship_id), db_bind_i32(owner_id) }, 2, &res, &err)) return -1;
    *attached = db_res_step(res, &err);
    db_res_finalize(res);
    return (err.code == 0) ? 0 : -1;
}

int db_combat_decrement_or_delete_asset(db_t *db, int asset_id, int quantity) {
    if (!db) return -1;
    db_error_t err = {0};
    char sql[256];
    if (quantity > 1) sql_build(db, "UPDATE sector_assets SET quantity = quantity - 1 WHERE sector_assets_id = {1};", sql, sizeof(sql));
    else sql_build(db, "DELETE FROM sector_assets WHERE sector_assets_id = {1};", sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(asset_id) }, 1, &err)) return -1;
    return 0;
}

int db_combat_attach_limpet(db_t *db, int ship_id, int owner_id, int64_t created_ts) {
    if (!db) return -1;
    db_error_t err = {0};
    char sql[256];
    sql_build(db, "INSERT INTO limpet_attached (ship_id, owner_player_id, created_ts) VALUES ({1}, {2}, {3});", sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(ship_id), db_bind_i32(owner_id), db_bind_timestamp_text(created_ts) }, 3, &err)) {
        if (err.code == ERR_DB_CONSTRAINT) {
            // Treat duplicate key as success (DO NOTHING semantics)
            return 0;
        }
        return err.code;
    }
    return 0;
}

int db_combat_get_planet_quasar_info(db_t *db, int sector_id, json_t **out_array) {
    if (!db || !out_array) return -1;
    *out_array = json_array();
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql_tmpl[512], sql[512];
    snprintf(sql_tmpl, sizeof(sql_tmpl), "SELECT p.planet_id, p.owner_id, p.owner_type, c.level, c.qCannonSector, c.militaryReactionLevel FROM planets p LEFT JOIN citadels c ON p.planet_id = c.planet_id WHERE p.sector_id = {1} AND c.level >= 3 AND c.qCannonSector > 0%s;", sql_for_update_skip_locked(db));
    sql_build(db, sql_tmpl, sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(sector_id) }, 1, &res, &err)) return -1;
    while(db_res_step(res, &err)) {
        json_t *obj = json_object();
        json_object_set_new(obj, "planet_id", json_integer(db_res_col_i32(res, 0, &err)));
        json_object_set_new(obj, "owner_id", json_integer(db_res_col_i32(res, 1, &err)));
        const char *ot = db_res_col_text(res, 2, &err);
        json_object_set_new(obj, "owner_type", json_string(ot ? ot : ""));
        json_object_set_new(obj, "base_strength", json_integer(db_res_col_i32(res, 4, &err)));
        json_object_set_new(obj, "reaction", json_integer(db_res_col_i32(res, 5, &err)));
        json_array_append_new(*out_array, obj);
    }
    db_res_finalize(res);
    return (err.code == 0) ? 0 : -1;
}

int db_combat_get_planet_atmosphere_quasar(db_t *db, int planet_id, int *owner_id, char *owner_type_buf, int *base_str, int *reaction) {
    if (!db || !owner_id || !owner_type_buf || !base_str || !reaction) return -1;
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql[512];
    sql_build(db, "SELECT p.owner_id, p.owner_type, c.level, c.qCannonAtmosphere, c.militaryReactionLevel FROM planets p LEFT JOIN citadels c ON p.planet_id = c.planet_id WHERE p.planet_id = {1} AND c.level >= 3 AND c.qCannonAtmosphere > 0;", sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(planet_id) }, 1, &res, &err)) return -1;
    int found = 0;
    if (db_res_step(res, &err)) {
        *owner_id = (int)db_res_col_i32(res, 0, &err);
        const char *ot = db_res_col_text(res, 1, &err);
        if (ot) strcpy(owner_type_buf, ot); else *owner_type_buf = 0;
        *base_str = (int)db_res_col_i32(res, 3, &err);
        *reaction = (int)db_res_col_i32(res, 4, &err);
        found = 1;
    }
    db_res_finalize(res);
    return (found && err.code == 0) ? 0 : -1;
}

/* Mines Recall */
int db_combat_get_ship_mine_capacity(db_t *db, int ship_id, int *sector_id, int *current, int *max) {
    if (!db || !sector_id || !current || !max) return -1;
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql[256];
    sql_build(db, "SELECT s.sector_id, s.mines, st.maxmines FROM ships s JOIN shiptypes st ON s.type_id = st.shiptypes_id WHERE s.ship_id = {1};", sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(ship_id) }, 1, &res, &err)) return -1;
    if (db_res_step(res, &err)) {
        *sector_id = (int)db_res_col_i32(res, 0, &err);
        *current = (int)db_res_col_i32(res, 1, &err);
        *max = (int)db_res_col_i32(res, 2, &err);
    }
    db_res_finalize(res);
    return (err.code == 0) ? 0 : -1;
}

int db_combat_get_asset_info(db_t *db, int asset_id, int owner_id, int sector_id, int *quantity, int *type) {
    if (!db || !quantity || !type) return -1;
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql[256];
    sql_build(db, "SELECT quantity, asset_type FROM sector_assets WHERE sector_assets_id = {1} AND owner_id = {2} AND sector_id = {3} AND asset_type IN (1, 4);", sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(asset_id), db_bind_i32(owner_id), db_bind_i32(sector_id) }, 3, &res, &err)) return -1;
    if (db_res_step(res, &err)) { *quantity = (int)db_res_col_i32(res, 0, &err); *type = (int)db_res_col_i32(res, 1, &err); }
    else { *quantity = 0; }
    db_res_finalize(res);
    return (err.code == 0) ? 0 : -1;
}

int db_combat_recall_mines(db_t *db, int asset_id, int ship_id, int quantity) {
    if (!db) return -1;
    db_error_t err = {0};
    char sql_del[256], sql_upd[256];
    sql_build(db, "DELETE FROM sector_assets WHERE sector_assets_id = {1};", sql_del, sizeof(sql_del));
    if (!db_exec(db, sql_del, (db_bind_t[]){ db_bind_i32(asset_id) }, 1, &err)) return -1;
    sql_build(db, "UPDATE ships SET mines = mines + {1} WHERE ship_id = {2};", sql_upd, sizeof(sql_upd));
    if (!db_exec(db, sql_upd, (db_bind_t[]){ db_bind_i32(quantity), db_bind_i32(ship_id) }, 2, &err)) return -1;
    return 0;
}

int db_combat_get_sector_fighters_locked(db_t *db, int sector_id, json_t **out_array) {
    if (!db || !out_array) return -1;
    *out_array = json_array();
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql_tmpl[512], sql[512];
    snprintf(sql_tmpl, sizeof(sql_tmpl), "SELECT sector_assets_id, quantity, offensive_setting, owner_id, corporation_id FROM sector_assets WHERE sector_id = {1} AND asset_type = 2 AND quantity > 0%s;", sql_for_update_skip_locked(db));
    sql_build(db, sql_tmpl, sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(sector_id) }, 1, &res, &err)) return -1;
    while(db_res_step(res, &err)) {
        json_t *obj = json_object();
        json_object_set_new(obj, "id", json_integer(db_res_col_i32(res, 0, &err)));
        json_object_set_new(obj, "quantity", json_integer(db_res_col_i32(res, 1, &err)));
        json_object_set_new(obj, "mode", json_integer(db_res_col_i32(res, 2, &err)));
        json_object_set_new(obj, "owner_id", json_integer(db_res_col_i32(res, 3, &err)));
        json_object_set_new(obj, "corp_id", json_integer(db_res_col_i32(res, 4, &err)));
        json_array_append_new(*out_array, obj);
    }
    db_res_finalize(res);
    return (err.code == 0) ? 0 : -1;
}

int db_combat_get_ship_fighter_capacity(db_t *db, int ship_id, int *sector_id, int *current, int *max, int *corp_id) {
    if (!db || !sector_id || !current || !max || !corp_id) return -1;
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql[512];
    sql_build(db, "SELECT s.sector_id, s.fighters, st.maxfighters, COALESCE(cm.corporation_id,0) FROM ships s JOIN shiptypes st ON s.type_id = st.shiptypes_id LEFT JOIN corp_members cm ON cm.player_id = (SELECT player_id FROM ship_ownership WHERE ship_id = {1} AND is_primary = TRUE) WHERE s.ship_id = {1};", sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(ship_id) }, 1, &res, &err)) return -1;
    if (db_res_step(res, &err)) {
        *sector_id = (int)db_res_col_i32(res, 0, &err);
        *current = (int)db_res_col_i32(res, 1, &err);
        *max = (int)db_res_col_i32(res, 2, &err);
        *corp_id = (int)db_res_col_i32(res, 3, &err);
    } else { db_res_finalize(res); return -1; }
    db_res_finalize(res);
    return (err.code == 0) ? 0 : -1;
}

int db_combat_get_asset_info_locked(db_t *db, int asset_id, int sector_id, int asset_type, int *quantity, int *owner_id, int *corp_id, int *mode) {
    if (!db || !quantity || !owner_id || !corp_id || !mode) return -1;
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql_tmpl[512], sql[512];
    snprintf(sql_tmpl, sizeof(sql_tmpl), "SELECT quantity, owner_id as player, corporation_id as corporation, offensive_setting FROM sector_assets WHERE sector_assets_id = {1} AND sector_id = {2} AND asset_type = {3} %s;", sql_for_update_skip_locked(db));
    sql_build(db, sql_tmpl, sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(asset_id), db_bind_i32(sector_id), db_bind_i32(asset_type) }, 3, &res, &err)) return -1;
    if (db_res_step(res, &err)) {
        *quantity = (int)db_res_col_i32(res, 0, &err);
        *owner_id = (int)db_res_col_i32(res, 1, &err);
        *corp_id = (int)db_res_col_i32(res, 2, &err);
        *mode = (int)db_res_col_i32(res, 3, &err);
    } else { db_res_finalize(res); return -1; }
    db_res_finalize(res);
    return (err.code == 0) ? 0 : -1;
}

int db_combat_add_ship_fighters(db_t *db, int ship_id, int amount) {
    if (!db) return -1;
    db_error_t err = {0};
    char sql[256];
    sql_build(db, "UPDATE ships SET fighters = fighters + {1} WHERE ship_id = {2};", sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(amount), db_bind_i32(ship_id) }, 2, &err)) return -1;
    return 0;
}

int db_combat_select_mines_locked(db_t *db, int sector_id, int asset_type, json_t **out_array) {
    if (!db || !out_array) return -1;
    *out_array = json_array();
    db_res_t *res = NULL;
    db_error_t err = {0};
    char sql_tmpl[512], sql[512];
    snprintf(sql_tmpl, sizeof(sql_tmpl), "SELECT sector_assets_id as id, quantity, owner_id as player, corporation_id as corporation, ttl FROM sector_assets WHERE sector_id = {1} AND asset_type = {2} AND quantity > 0 %s ORDER BY sector_assets_id ASC;", sql_for_update_skip_locked(db));
    sql_build(db, sql_tmpl, sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(sector_id), db_bind_i32(asset_type) }, 2, &res, &err)) return -1;
    while(db_res_step(res, &err)) {
        json_t *obj = json_object();
        json_object_set_new(obj, "id", json_integer(db_res_col_i32(res, 0, &err)));
        json_object_set_new(obj, "quantity", json_integer(db_res_col_i32(res, 1, &err)));
        json_object_set_new(obj, "player", json_integer(db_res_col_i32(res, 2, &err)));
        json_object_set_new(obj, "corporation", json_integer(db_res_col_i32(res, 3, &err)));
        json_object_set_new(obj, "ttl", json_integer(db_res_col_i64(res, 4, &err)));
        json_array_append_new(*out_array, obj);
    }
    db_res_finalize(res);
    return (err.code == 0) ? 0 : -1;
}

int db_combat_debit_credits(db_t *db, int player_id, int amount) {
    if (!db) return -1;
    db_error_t err = {0};
    int64_t rows = 0;
    char sql[512];
    sql_build(db, "UPDATE players SET credits = credits - {1} WHERE player_id = {2} AND credits >= {1};", sql, sizeof(sql));
    if (!db_exec_rows_affected(db, sql, (db_bind_t[]){ db_bind_i32(amount), db_bind_i32(player_id) }, 2, &rows, &err)) return -1;
    
    if (rows > 0) {
        long long dummy;
        return repo_players_get_credits(db, player_id, &dummy);
    }
    return -1;
}

int db_combat_update_asset_quantity(db_t *db, int asset_id, int new_quantity) {
    if (!db) return -1;
    db_error_t err = {0};
    char sql[256];
    sql_build(db, "UPDATE sector_assets SET quantity = {1} WHERE sector_assets_id = {2};", sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(new_quantity), db_bind_i32(asset_id) }, 2, &err)) return -1;
    return 0;
}

int db_combat_get_stardock_locations(db_t *db, int **out_sectors, int *out_count) {
    if (!db || !out_sectors || !out_count) return -1;
    db_res_t *res = NULL;
    db_error_t err = {0};
    if (!db_query (db, "SELECT sector_id FROM stardock_location;", NULL, 0, &res, &err)) return -1;
    int cap = 8, count = 0;
    int *arr = malloc (cap * sizeof(int));
    while (db_res_step (res, &err)) {
        if (count >= cap) { cap *= 2; arr = realloc (arr, cap * sizeof(int)); }
        arr[count++] = (int) db_res_col_i32 (res, 0, &err);
    }
    db_res_finalize (res);
    *out_sectors = arr; *out_count = count;
    return (err.code == 0) ? 0 : -1;
}