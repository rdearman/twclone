#include "server_citadel.h"
#include "server_envelope.h"
#include "errors.h"
#include "config.h"
#include "server_players.h"
#include "server_log.h"
#include <jansson.h>
#include <sqlite3.h>
#include <time.h>
#include <string.h>
#include <strings.h>

// Helper to get the player's current planet_id from their active ship.
// Returns planet_id > 0 on success, 0 if not on a planet or error.
static int get_player_planet(sqlite3 *db, int player_id) {
    int ship_id = h_get_active_ship_id(db, player_id);
    if (ship_id <= 0) {
        return 0;
    }

    sqlite3_stmt *st = NULL;
    const char *sql = "SELECT onplanet FROM ships WHERE id = ?1;";
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_int(st, 1, ship_id);
    int planet_id = 0;
    if (sqlite3_step(st) == SQLITE_ROW) {
        planet_id = sqlite3_column_int(st, 0);
    }
    sqlite3_finalize(st);
    return planet_id;
}

/* Require auth before touching citadel features */
static inline int
require_auth (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id > 0)
    return 1;
  send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
  return 0;
}

int cmd_citadel_build(client_ctx_t *ctx, json_t *root) {
    // This command is an alias for upgrading from level 0.
    return cmd_citadel_upgrade(ctx, root);
}

int cmd_citadel_upgrade(client_ctx_t *ctx, json_t *root) {
    if (!require_auth(ctx, root)) {
        return 0;
    }

    sqlite3 *db = db_get_handle();
    if (!db) {
        send_enveloped_error(ctx->fd, root, 500, "Database unavailable.");
        return 0;
    }

    // 1. Get Player Location & Planet Info
    int planet_id = get_player_planet(db, ctx->player_id);
    if (planet_id <= 0) {
        send_enveloped_refused(ctx->fd, root, 1405, "You must be landed on a planet to build or upgrade a citadel.", NULL);
        return 0;
    }

    sqlite3_stmt *planet_st = NULL;
    const char *sql_planet = "SELECT type, owner_player_id, colonist, ore_on_hand, organics_on_hand, equipment_on_hand FROM planets WHERE id = ?1;";
    if (sqlite3_prepare_v2(db, sql_planet, -1, &planet_st, NULL) != SQLITE_OK) {
        send_enveloped_error(ctx->fd, root, 500, "Failed to query planet data.");
        return 0;
    }
    sqlite3_bind_int(planet_st, 1, planet_id);

    int planet_type = 0, owner_player_id = 0;
    long long p_colonists = 0, p_ore = 0, p_org = 0, p_equip = 0;

    if (sqlite3_step(planet_st) == SQLITE_ROW) {
        planet_type = sqlite3_column_int(planet_st, 0);
        owner_player_id = sqlite3_column_int(planet_st, 1);
        p_colonists = sqlite3_column_int64(planet_st, 2);
        p_ore = sqlite3_column_int64(planet_st, 3);
        p_org = sqlite3_column_int64(planet_st, 4);
        p_equip = sqlite3_column_int64(planet_st, 5);
    }
    sqlite3_finalize(planet_st);

    if (owner_player_id != ctx->player_id) {
        send_enveloped_refused(ctx->fd, root, 1403, "You do not own this planet.", NULL);
        return 0;
    }

    // 2. Get Citadel State
    sqlite3_stmt *citadel_st = NULL;
    const char *sql_citadel = "SELECT level, construction_status FROM citadels WHERE planet_id = ?1;";
    int current_level = 0;
    const char *construction_status = "idle";
    if (sqlite3_prepare_v2(db, sql_citadel, -1, &citadel_st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(citadel_st, 1, planet_id);
        if (sqlite3_step(citadel_st) == SQLITE_ROW) {
            current_level = sqlite3_column_int(citadel_st, 0);
            construction_status = (const char*)sqlite3_column_text(citadel_st, 1);
        }
        sqlite3_finalize(citadel_st);
    }

    if (strcasecmp(construction_status, "idle") != 0) {
        send_enveloped_refused(ctx->fd, root, 1106, "An upgrade is already in progress.", NULL);
        return 0;
    }

    if (current_level >= 6) {
        send_enveloped_refused(ctx->fd, root, 1107, "Citadel is already at maximum level.", NULL);
        return 0;
    }

    int target_level = current_level + 1;

    // 3. Get Upgrade Requirements
    char *sql_req_query = sqlite3_mprintf(
        "SELECT citadelUpgradeColonist_lvl%d, citadelUpgradeOre_lvl%d, citadelUpgradeOrganics_lvl%d, citadelUpgradeEquipment_lvl%d, citadelUpgradeTime_lvl%d FROM planettypes WHERE id = %d;",
        target_level, target_level, target_level, target_level, target_level, planet_type
    );

    sqlite3_stmt *req_st = NULL;
    long long r_colonists = 0, r_ore = 0, r_org = 0, r_equip = 0;
    int r_days = 0;

    if (sqlite3_prepare_v2(db, sql_req_query, -1, &req_st, NULL) == SQLITE_OK) {
        if (sqlite3_step(req_st) == SQLITE_ROW) {
            r_colonists = sqlite3_column_int64(req_st, 0);
            r_ore = sqlite3_column_int64(req_st, 1);
            r_org = sqlite3_column_int64(req_st, 2);
            r_equip = sqlite3_column_int64(req_st, 3);
            r_days = sqlite3_column_int(req_st, 4);
        }
        sqlite3_finalize(req_st);
    }
    sqlite3_free(sql_req_query);

    if (r_days <= 0) {
        send_enveloped_error(ctx->fd, root, 500, "Could not retrieve upgrade requirements for this planet type.");
        return 0;
    }

    // 4. Check Resources
    if (p_colonists < r_colonists || p_ore < r_ore || p_org < r_org || p_equip < r_equip) {
        json_t *missing = json_object();
        if (p_colonists < r_colonists) json_object_set_new(missing, "colonists", json_integer(r_colonists - p_colonists));
        if (p_ore < r_ore) json_object_set_new(missing, "ore", json_integer(r_ore - p_ore));
        if (p_org < r_org) json_object_set_new(missing, "organics", json_integer(r_org - p_org));
        if (p_equip < r_equip) json_object_set_new(missing, "equipment", json_integer(r_equip - p_equip));
        
        json_t *meta = json_object();
        json_object_set_new(meta, "missing", missing);
        send_enveloped_refused(ctx->fd, root, 1402, "Insufficient resources on planet to begin upgrade.", meta);
        return 0;
    }

    // 5. Execute Upgrade
    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    // Deduct resources
    sqlite3_stmt *update_planet_st = NULL;
    const char *sql_update_planet = "UPDATE planets SET ore_on_hand = ore_on_hand - ?1, organics_on_hand = organics_on_hand - ?2, equipment_on_hand = equipment_on_hand - ?3 WHERE id = ?4;";
    if (sqlite3_prepare_v2(db, sql_update_planet, -1, &update_planet_st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(update_planet_st, 1, r_ore);
        sqlite3_bind_int64(update_planet_st, 2, r_org);
        sqlite3_bind_int64(update_planet_st, 3, r_equip);
        sqlite3_bind_int(update_planet_st, 4, planet_id);
        if (sqlite3_step(update_planet_st) != SQLITE_DONE) {
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            send_enveloped_error(ctx->fd, root, 500, "Failed to deduct resources.");
            sqlite3_finalize(update_planet_st);
            return 0;
        }
        sqlite3_finalize(update_planet_st);
    } else {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        send_enveloped_error(ctx->fd, root, 500, "Database error during resource deduction.");
        return 0;
    }

    // Start construction
    sqlite3_stmt *update_citadel_st = NULL;
    const char *sql_update_citadel = "INSERT INTO citadels (planet_id, level, owner, construction_status, target_level, construction_start_time, construction_end_time) VALUES (?1, ?2, ?3, 'upgrading', ?4, ?5, ?6) ON CONFLICT(planet_id) DO UPDATE SET construction_status='upgrading', target_level=?4, construction_start_time=?5, construction_end_time=?6;";
    
    long long start_time = time(NULL);
    long long end_time = start_time + (r_days * 86400); // Assuming 1 day = 86400 seconds

    if (sqlite3_prepare_v2(db, sql_update_citadel, -1, &update_citadel_st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(update_citadel_st, 1, planet_id);
        sqlite3_bind_int(update_citadel_st, 2, current_level);
        sqlite3_bind_int(update_citadel_st, 3, ctx->player_id);
        sqlite3_bind_int(update_citadel_st, 4, target_level);
        sqlite3_bind_int64(update_citadel_st, 5, start_time);
        sqlite3_bind_int64(update_citadel_st, 6, end_time);

        if (sqlite3_step(update_citadel_st) != SQLITE_DONE) {
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            send_enveloped_error(ctx->fd, root, 500, "Failed to start citadel construction.");
            sqlite3_finalize(update_citadel_st);
            return 0;
        }
        sqlite3_finalize(update_citadel_st);
    } else {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        send_enveloped_error(ctx->fd, root, 500, "Database error during construction start.");
        return 0;
    }

    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);

    // Log the event for news generation
    json_t *event_payload = json_object();
    json_object_set_new(event_payload, "planet_id", json_integer(planet_id));
    json_object_set_new(event_payload, "current_level", json_integer(current_level));
    json_object_set_new(event_payload, "target_level", json_integer(target_level));
    json_object_set_new(event_payload, "days_to_complete", json_integer(r_days));
    db_log_engine_event((long long)time(NULL), "citadel.upgrade_started", "player", ctx->player_id, 0, event_payload, NULL);

    // 6. Send Response
    json_t *payload = json_object();
    json_object_set_new(payload, "planet_id", json_integer(planet_id));
    json_object_set_new(payload, "target_level", json_integer(target_level));
    json_object_set_new(payload, "completion_time", json_integer(end_time));
    json_object_set_new(payload, "days_to_complete", json_integer(r_days));
    send_enveloped_ok(ctx->fd, root, "citadel.upgrade_started", payload);
    json_decref(payload);

    return 0;
}
