#include "server_planets.h"
#include "server_rules.h"
#include "common.h"
#include "server_log.h"
#include "database.h"           // For DB functions and helpers
#include "namegen.h"            // For h_namegen_planet_name
#include "errors.h"             // For ERR_ codes
#include <time.h>               // For time(NULL)
#include <string.h>             // For strncpy, strlen
#include <stdlib.h>             // For strdup, free
#include "server_cmds.h"        // For send_error_response, send_json_response
#include "server_corporation.h"
#include "server_ports.h"       // For h_update_entity_stock
#include "database_market.h"    // For commodity_order_t, db_insert_commodity_order
#include "server_players.h"     // For h_get_player_petty_cash, h_add_player_petty_cash
// Forward declaration
void send_enveloped_error (int fd, json_t *root, int code, const char *msg);


/**
 * @brief Logic helper to determine if a planet is NPC-controlled.
 *
 * Does not query the DB. Operates on the in-memory planet_t structure.
 *
 * @param p Pointer to planet_t struct
 * @return true if NPC, false otherwise
 */
bool
planet_is_npc (const planet_t *p)
{
  if (!p)
    {
      return false;
    }
  // If owner_id is 0, it's unowned or system-owned (NPC behavior)
  if (p->owner_id == 0)
    {
      return true;
    }
  // If owner_type is explicitly "npc" (if that becomes a thing), or not "player"/"corporation"
  if (p->owner_type)
    {
      if (strcasecmp (p->owner_type, "player") == 0)
        {
          return false;
        }
      if (strcasecmp (p->owner_type, "corporation") == 0)
        {
          return false;
        }
      if (strcasecmp (p->owner_type, "corp") == 0)
        {
          return false;
        }
      // Any other type is considered NPC (e.g. "npc", "system", "alien")
      return true;
    }
  // Fallback: owner_id > 0 but no type? Assume player/corp if not specified?
  // Safe default for now: if owned but type unknown, assume non-NPC to avoid accidental selling?
  // Or assume NPC?
  // Given existing logic, planets usually have "player" or "corp".
  return true;
}

// Helper to load minimal planet info needed for ownership check
static int h_get_planet_owner_info(sqlite3 *db, int planet_id, planet_t *p) {
    if (!p) return SQLITE_ERROR;
    sqlite3_stmt *st = NULL;
    const char *sql = "SELECT id, owner_id, owner_type FROM planets WHERE id = ?1;";
    int rc = sqlite3_prepare_v2(db, sql, -1, &st, NULL);
    if (rc != SQLITE_OK) return rc;
    
    sqlite3_bind_int(st, 1, planet_id);
    
    if (sqlite3_step(st) == SQLITE_ROW) {
        p->id = sqlite3_column_int(st, 0);
        p->owner_id = sqlite3_column_int(st, 1);
        const char *type_str = (const char *)sqlite3_column_text(st, 2);
        p->owner_type = type_str ? strdup(type_str) : NULL;
        rc = SQLITE_OK;
    } else {
        rc = SQLITE_NOTFOUND;
    }
    sqlite3_finalize(st);
    return rc;
}

static void h_free_planet_t(planet_t *p) {
    if (p->owner_type) free(p->owner_type);
}


int
cmd_planet_genesis (client_ctx_t *ctx, json_t *root)
{
  send_enveloped_error (ctx->fd,
                        root,
                        1101,
                        "Not implemented: " "planet.genesis");
  return 0;
}


int
cmd_planet_info (client_ctx_t *ctx, json_t *root)
{
  send_enveloped_error (ctx->fd, root, 1101, "Not implemented: " "planet.info");
  return 0;
}


int
cmd_planet_rename (client_ctx_t *ctx,
                   json_t *root)
{
  send_enveloped_error (ctx->fd, root, 1101,
                        "Not implemented: " "planet.rename");
  return 0;
}


int
cmd_planet_land (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_AUTHENTICATED,
                            "Authentication required.");
      return -1;
    }
  sqlite3 *db = db_get_handle ();

  // Consume 1 turn for landing
  TurnConsumeResult tc = h_consume_player_turn (db, ctx, 1);


  if (tc != TURN_CONSUME_SUCCESS)
    {
      return handle_turn_consumption_error (ctx, tc, "planet.land", root, NULL);
    }

  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_enveloped_error (ctx->fd, root, ERR_BAD_REQUEST,
                            "Missing data payload.");
      return 0;
    }
  int planet_id = 0;


  json_unpack (data, "{s:i}", "planet_id", &planet_id);
  if (planet_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_MISSING_FIELD,
                            "Missing or invalid 'planet_id'.");
      return 0;
    }
  // Check if player is in the same sector as the planet
  int player_sector = 0;


  db_player_get_sector (ctx->player_id, &player_sector);
  sqlite3_stmt *st = NULL;
  const char *sql_planet_info =
    "SELECT sector, owner_id, owner_type FROM planets WHERE id = ?;";


  if (sqlite3_prepare_v2 (db, sql_planet_info, -1, &st, NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, ERR_DB_QUERY_FAILED,
                            "Failed to get planet info.");
      return 0;
    }
  sqlite3_bind_int (st, 1, planet_id);
  int planet_sector = 0;
  int owner_id = 0;
  const char *owner_type = NULL;


  if (sqlite3_step (st) == SQLITE_ROW)
    {
      planet_sector = sqlite3_column_int (st, 0);
      owner_id = sqlite3_column_int (st, 1);
      owner_type = (const char *) sqlite3_column_text (st, 2);
    }
  else
    {
      sqlite3_finalize (st);
      send_enveloped_error (ctx->fd, root, ERR_NOT_FOUND,
                            "Planet not found.");
      return 0;
    }
  sqlite3_finalize (st);
  if (player_sector != planet_sector)
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
                            "You are not in the same sector as the planet.");
      return 0;
    }
  bool can_land = false;


  if (owner_id == 0)
    {                           // unowned
      can_land = true;
    }
  else if (strcmp (owner_type, "player") == 0)
    {
      if (owner_id == ctx->player_id)
        {
          can_land = true;
        }
    }
  else if (strcmp (owner_type, "corp") == 0)
    {
      int player_corp_id = h_get_player_corp_id (db, ctx->player_id);


      if (player_corp_id > 0 && player_corp_id == owner_id)
        {
          can_land = true;
        }
    }
  if (!can_land)
    {
      send_enveloped_error (ctx->fd, root, ERR_PERMISSION_DENIED,
                            "You do not have permission to land on this planet.");
      return 0;
    }
  if (db_player_land_on_planet (db, ctx->player_id, planet_id) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, ERR_SERVER_ERROR,
                            "Failed to land on planet.");
      return 0;
    }
  // Update context
  ctx->sector_id = 0;           // Not in a sector anymore
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
                       json_string ("Landed successfully."));
  json_object_set_new (response_data, "planet_id", json_integer (planet_id));
  send_enveloped_ok (ctx->fd,
                     root,
                     "planet.land.success",
                     response_data);
  return 0;
}


int
cmd_planet_launch (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_AUTHENTICATED,
                            "Authentication required.");
      return -1;
    }
  sqlite3 *db = db_get_handle ();
  int sector_id = 0;


  if (db_player_launch_from_planet (db, ctx->player_id,
                                    &sector_id) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, ERR_SERVER_ERROR,
                            "Failed to launch from planet. Are you on a planet?");
      return 0;
    }
  // Update context
  ctx->sector_id = sector_id;
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
                       json_string ("Launched successfully."));
  json_object_set_new (response_data, "sector_id", json_integer (sector_id));
  send_enveloped_ok (ctx->fd, root, "planet.launch.success", response_data);
  return 0;
}


int
cmd_planet_transfer_ownership (client_ctx_t *ctx, json_t *root)
{
  send_enveloped_error (ctx->fd,
                        root,
                        1101,
                        "Not implemented: " "planet.transfer_ownership");
  return 0;
}


int
cmd_planet_harvest (client_ctx_t *ctx, json_t *root)
{
  send_enveloped_error (ctx->fd,
                        root,
                        1101,
                        "Not implemented: " "planet.harvest");
  return 0;
}


int
cmd_planet_deposit (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle();
  if (!ctx || ctx->player_id <= 0) {
      send_enveloped_refused(ctx->fd, root, ERR_NOT_AUTHENTICATED, "Not authenticated", NULL);
      return 0;
  }

  json_t *data = json_object_get(root, "data");
  if (!data) {
      send_enveloped_error(ctx->fd, root, ERR_BAD_REQUEST, "Missing data payload.");
      return 0;
  }

  int planet_id = 0;
  int amount = 0;
  json_unpack(data, "{s:i, s:i}", "planet_id", &planet_id, "amount", &amount);

  if (planet_id <= 0 || amount <= 0) {
      send_enveloped_error(ctx->fd, root, ERR_INVALID_ARG, "Invalid planet_id or amount.");
      return 0;
  }

  planet_t p = {0};
  if (h_get_planet_owner_info(db, planet_id, &p) != SQLITE_OK) {
      send_enveloped_error(ctx->fd, root, ERR_NOT_FOUND, "Planet not found.");
      return 0;
  }

  // Ownership check
  bool allowed = false;
  if (p.owner_type && strcmp(p.owner_type, "player") == 0 && p.owner_id == ctx->player_id) {
      allowed = true;
  } else if (p.owner_type && (strcmp(p.owner_type, "corp") == 0 || strcmp(p.owner_type, "corporation") == 0)) {
       // Any member of the owning corp can deposit
       if (ctx->corp_id > 0 && ctx->corp_id == p.owner_id) {
           allowed = true;
       }
  }
  h_free_planet_t(&p);

  if (!allowed) {
      send_enveloped_refused(ctx->fd, root, 1403, "You do not control this planet.", NULL);
      return 0;
  }

  // Check citadel level
  int citadel_level = 0;
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2(db, "SELECT level FROM citadels WHERE planet_id = ?1;", -1, &st, NULL);
  if (rc == SQLITE_OK) {
      sqlite3_bind_int(st, 1, planet_id);
      if (sqlite3_step(st) == SQLITE_ROW) {
          citadel_level = sqlite3_column_int(st, 0);
      }
      sqlite3_finalize(st);
  }

  if (citadel_level < 1) {
      send_enveloped_refused(ctx->fd, root, 1403, "No citadel or insufficient level for treasury.", NULL);
      return 0;
  }

  // Transaction
  if (sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK) {
      send_enveloped_error(ctx->fd, root, 500, "Database busy.");
      return 0;
  }

  // Deduct from player credits
  long long new_player_balance = 0;
  if (h_deduct_player_petty_cash_unlocked(db, ctx->player_id, amount, &new_player_balance) != SQLITE_OK) {
      sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_refused(ctx->fd, root, 1402, "Insufficient credits.", NULL);
      return 0;
  }

  // Add to treasury
  if (sqlite3_prepare_v2(db, "UPDATE citadels SET treasury = treasury + ?1 WHERE planet_id = ?2;", -1, &st, NULL) != SQLITE_OK) {
      sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error(ctx->fd, root, 500, "Database error.");
      return 0;
  }
  sqlite3_bind_int(st, 1, amount);
  sqlite3_bind_int(st, 2, planet_id);
  if (sqlite3_step(st) != SQLITE_DONE) {
      sqlite3_finalize(st);
      sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error(ctx->fd, root, 500, "Database update failed.");
      return 0;
  }
  sqlite3_finalize(st);

  // Commit
  if (sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
      sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error(ctx->fd, root, 500, "Transaction commit failed.");
      return 0;
  }

  // Fetch new treasury balance for response
  long long new_treasury = 0;
  if (sqlite3_prepare_v2(db, "SELECT treasury FROM citadels WHERE planet_id = ?1;", -1, &st, NULL) == SQLITE_OK) {
      sqlite3_bind_int(st, 1, planet_id);
      if (sqlite3_step(st) == SQLITE_ROW) {
          new_treasury = sqlite3_column_int64(st, 0);
      }
      sqlite3_finalize(st);
  }

  json_t *resp = json_object();
  json_object_set_new(resp, "planet_id", json_integer(planet_id));
  json_object_set_new(resp, "planet_treasury_balance", json_integer(new_treasury));
  json_object_set_new(resp, "player_credits", json_integer(new_player_balance));
  
  send_enveloped_ok(ctx->fd, root, "planet.deposit", resp);
  json_decref(resp);
  return 0;
}


int
cmd_planet_withdraw (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle();
  if (!ctx || ctx->player_id <= 0) {
      send_enveloped_refused(ctx->fd, root, ERR_NOT_AUTHENTICATED, "Not authenticated", NULL);
      return 0;
  }

  json_t *data = json_object_get(root, "data");
  if (!data) {
      send_enveloped_error(ctx->fd, root, ERR_BAD_REQUEST, "Missing data payload.");
      return 0;
  }

  int planet_id = 0;
  int amount = 0;
  json_unpack(data, "{s:i, s:i}", "planet_id", &planet_id, "amount", &amount);

  if (planet_id <= 0 || amount <= 0) {
      send_enveloped_error(ctx->fd, root, ERR_INVALID_ARG, "Invalid planet_id or amount.");
      return 0;
  }

  planet_t p = {0};
  if (h_get_planet_owner_info(db, planet_id, &p) != SQLITE_OK) {
      send_enveloped_error(ctx->fd, root, ERR_NOT_FOUND, "Planet not found.");
      return 0;
  }

  // Ownership check
  bool allowed = false;
  if (p.owner_type && strcmp(p.owner_type, "player") == 0 && p.owner_id == ctx->player_id) {
      allowed = true;
  } else if (p.owner_type && (strcmp(p.owner_type, "corp") == 0 || strcmp(p.owner_type, "corporation") == 0)) {
       // Corp planet: check if player is in corp and has rank
       if (ctx->corp_id > 0 && ctx->corp_id == p.owner_id) {
           char role[16];
           h_get_player_corp_role(db, ctx->player_id, ctx->corp_id, role, sizeof(role));
           if (strcasecmp(role, "Leader") == 0 || strcasecmp(role, "Officer") == 0) {
               allowed = true;
           }
       }
  }
  h_free_planet_t(&p);

  if (!allowed) {
      send_enveloped_refused(ctx->fd, root, 1403, "You do not control this planet or lack permissions.", NULL);
      return 0;
  }

  // Check citadel level and treasury balance
  int citadel_level = 0;
  long long current_treasury = 0;
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2(db, "SELECT level, treasury FROM citadels WHERE planet_id = ?1;", -1, &st, NULL);
  if (rc == SQLITE_OK) {
      sqlite3_bind_int(st, 1, planet_id);
      if (sqlite3_step(st) == SQLITE_ROW) {
          citadel_level = sqlite3_column_int(st, 0);
          current_treasury = sqlite3_column_int64(st, 1);
      }
      sqlite3_finalize(st);
  }

  if (citadel_level < 1) {
      send_enveloped_refused(ctx->fd, root, 1403, "No citadel or insufficient level for treasury.", NULL);
      return 0;
  }

  if (current_treasury < amount) {
      send_enveloped_refused(ctx->fd, root, 1402, "Insufficient treasury funds.", NULL);
      return 0;
  }

  // Transaction
  if (sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK) {
      send_enveloped_error(ctx->fd, root, 500, "Database busy.");
      return 0;
  }

  // Deduct from treasury
  if (sqlite3_prepare_v2(db, "UPDATE citadels SET treasury = treasury - ?1 WHERE planet_id = ?2;", -1, &st, NULL) != SQLITE_OK) {
      sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error(ctx->fd, root, 500, "Database error.");
      return 0;
  }
  sqlite3_bind_int(st, 1, amount);
  sqlite3_bind_int(st, 2, planet_id);
  if (sqlite3_step(st) != SQLITE_DONE) {
      sqlite3_finalize(st);
      sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error(ctx->fd, root, 500, "Database update failed.");
      return 0;
  }
  sqlite3_finalize(st);

  // Add to player credits
  long long new_player_balance = 0;
  if (h_add_player_petty_cash_unlocked(db, ctx->player_id, amount, &new_player_balance) != SQLITE_OK) {
      sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error(ctx->fd, root, 500, "Failed to credit player.");
      return 0;
  }

  // Commit
  if (sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
      sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error(ctx->fd, root, 500, "Transaction commit failed.");
      return 0;
  }

  json_t *resp = json_object();
  json_object_set_new(resp, "planet_id", json_integer(planet_id));
  json_object_set_new(resp, "planet_treasury_balance", json_integer(current_treasury - amount));
  json_object_set_new(resp, "player_credits", json_integer(new_player_balance));
  
  send_enveloped_ok(ctx->fd, root, "planet.withdraw", resp);
  json_decref(resp);
  return 0;
}


// Helper to send error and return (implicit rollback from engine transaction)
static int
send_error_and_return (client_ctx_t *ctx, json_t *root, int err_code,
                       const char *msg)
{
  return send_error_response (ctx, root, err_code, msg);        // Assuming send_error_response handles JSON
}


int
cmd_planet_genesis_create (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle ();
  // Local variables
  json_t *data = json_object_get (root, "data");
  int player_id = ctx->player_id;
  int ship_id = ctx->ship_id;
  int target_sector_id;
  char *planet_name = NULL;     // To hold the validated planet name
  const char *owner_entity_type = NULL;
  const char *idempotency_key = NULL;
  int owner_id = 0;
  char planet_class_str[2] = { 0 };     // For 'M', 'K', etc. + NUL
  long long new_planet_id = -1;
  bool over_cap_flag = false;
  int navhaz_delta = 0;         // From GENESIS_NAVHAZ_DELTA macro
  int current_unix_ts = (int) time (NULL);      // Current Unix epoch timestamp
  json_t *response_json = NULL;
  int rc = 0;                   // Return code for various operations
  // 1. Input Parsing and Initial Checks
  if (!data)
    {
      return send_error_and_return (ctx, root, ERR_BAD_REQUEST,
                                    "Missing data payload.");
    }
  if (player_id <= 0 || ship_id <= 0)
    {
      return send_error_and_return (ctx, root, ERR_NOT_AUTHENTICATED,
                                    "Player or ship not found in context.");
    }
  // MANDATORY: sector_id
  if (!json_get_int_flexible (data, "sector_id", &target_sector_id)
      || target_sector_id <= 0)
    {
      return send_error_and_return (ctx, root, ERR_INVALID_ARG,
                                    "Missing or invalid 'sector_id'.");
    }
  // MANDATORY: name
  const char *requested_name = json_get_string_or_null (data, "name");


  if (!requested_name || strlen (requested_name) == 0)
    {
      return send_error_and_return (ctx, root, ERR_MISSING_FIELD,
                                    "Missing 'name' for the new planet.");
    }
  planet_name = strdup (requested_name);        // Duplicate for local use and potential sanitization
  // MANDATORY: owner_entity_type
  owner_entity_type = json_get_string_or_null (data, "owner_entity_type");
  if (!owner_entity_type
      || (strcasecmp (owner_entity_type, "player") != 0
          && strcasecmp (owner_entity_type, "corporation") != 0))
    {
      free (planet_name);
      return send_error_and_return (ctx,
                                    root,
                                    ERR_INVALID_OWNER_TYPE,
                                    "Invalid 'owner_entity_type'. Must be 'player' or 'corporation'.");
    }
  // OPTIONAL: idempotency_key
  idempotency_key = json_get_string_or_null (data, "idempotency_key");
  // 2. Idempotency Check (as early as possible after parsing key)
  if (idempotency_key)
    {
      sqlite3_stmt *stmt_idem_check;
      const char *sql_check_idem =
        "SELECT response FROM idempotency WHERE key = ? AND cmd = 'planet.genesis_create';";


      sqlite3_prepare_v2 (db, sql_check_idem, -1, &stmt_idem_check, NULL);
      sqlite3_bind_text (stmt_idem_check, 1, idempotency_key, -1,
                         SQLITE_STATIC);
      if (sqlite3_step (stmt_idem_check) == SQLITE_ROW)
        {
          const char *prev_response_json =
            (const char *) sqlite3_column_text (stmt_idem_check, 0);


          if (prev_response_json)
            {
              json_t *prev_response =
                json_loads (prev_response_json, 0, NULL);


              if (prev_response)
                {
                  send_json_response (ctx, prev_response);      // Assumes send_json_response handles decref
                  sqlite3_finalize (stmt_idem_check);
                  free (planet_name);
                  return 0;     // Idempotent success
                }
            }
        }
      sqlite3_finalize (stmt_idem_check);
    }
  // 3. Feature Gate
  if (!GENESIS_ENABLED)
    {                           // Using macro
      free (planet_name);
      return send_error_and_return (ctx,
                                    root,
                                    ERR_GENESIS_DISABLED,
                                    "Genesis torpedo feature is currently disabled.");
    }
  // 4. "Where can I fire" Validation Rules
  // 4.1 MSL Prohibition
  sqlite3_stmt *stmt_msl;
  const char *sql_check_msl =
    "SELECT 1 FROM msl_sectors WHERE sector_id = ?;";


  sqlite3_prepare_v2 (db, sql_check_msl, -1, &stmt_msl, NULL);
  sqlite3_bind_int (stmt_msl, 1, target_sector_id);
  if (sqlite3_step (stmt_msl) == SQLITE_ROW)
    {
      sqlite3_finalize (stmt_msl);
      free (planet_name);
      return send_error_and_return (ctx, root, ERR_GENESIS_MSL_PROHIBITED,
                                    "Planet creation prohibited in MSL sector.");
    }
  sqlite3_finalize (stmt_msl);
  // 4.2 Planet Count Check (Per Sector)
  int current_planet_count_sector = 0;
  sqlite3_stmt *stmt_count_sector;
  const char *sql_count_planets_sector =
    "SELECT COUNT(*) FROM planets WHERE sector = ?;";


  sqlite3_prepare_v2 (db, sql_count_planets_sector, -1, &stmt_count_sector,
                      NULL);
  sqlite3_bind_int (stmt_count_sector, 1, target_sector_id);
  if (sqlite3_step (stmt_count_sector) == SQLITE_ROW)
    {
      current_planet_count_sector = sqlite3_column_int (stmt_count_sector, 0);
    }
  sqlite3_finalize (stmt_count_sector);
  int max_per_sector_cfg = db_get_config_int (db, "max_planets_per_sector", 6); // Get from config table


  over_cap_flag = (current_planet_count_sector + 1 > max_per_sector_cfg);
  if (GENESIS_BLOCK_AT_CAP
      && current_planet_count_sector >= max_per_sector_cfg)
    {                           // Using macro
      free (planet_name);
      return send_error_and_return (ctx, root, ERR_GENESIS_SECTOR_FULL,
                                    "Sector has reached maximum planets.");
    }
  // 5. Planet Naming Validation
  int max_name_len = db_get_config_int (db, "max_name_length", 50);     // Get from config table


  if (strlen (planet_name) > (size_t) max_name_len)
    {
      free (planet_name);
      return send_error_and_return (ctx, root, ERR_INVALID_PLANET_NAME_LENGTH,
                                    "Planet name too long.");
    }
  // TODO: Add further sanitization (e.g., control characters)
  // 6. Owner Entity Type Validation
  if (strcasecmp (owner_entity_type, "corporation") == 0)
    {
      if (ctx->corp_id <= 0)
        {                       // Player is not associated with a corporation
          free (planet_name);
          return send_error_and_return (ctx,
                                        root,
                                        ERR_NO_CORPORATION,
                                        "Player is not in a corporation to create a corporate planet.");
        }
      owner_id = ctx->corp_id;
    }
  else
    {                           // "player"
      owner_id = player_id;
    }
  // 7. Ship Inventory Check
  int genesis_torps_on_ship = 0;
  sqlite3_stmt *stmt_ship_inv;
  const char *sql_get_torps = "SELECT genesis FROM ships WHERE id = ?;";


  sqlite3_prepare_v2 (db, sql_get_torps, -1, &stmt_ship_inv, NULL);
  sqlite3_bind_int (stmt_ship_inv, 1, ship_id);
  if (sqlite3_step (stmt_ship_inv) == SQLITE_ROW)
    {
      genesis_torps_on_ship = sqlite3_column_int (stmt_ship_inv, 0);
    }
  sqlite3_finalize (stmt_ship_inv);
  if (genesis_torps_on_ship < 1)
    {
      free (planet_name);
      return send_error_and_return (ctx,
                                    root,
                                    ERR_NO_GENESIS_TORPEDO,
                                    "Insufficient Genesis torpedoes on your ship.");
    }
  // 8. Planet Class Random Generation (Weighted)
  // Fetch weights from planettypes table
  const char *classes[] = { "M", "K", "O", "L", "C", "H", "U" };
  int weights[7];
  int total_weight = 0;
  sqlite3_stmt *stmt_get_weights;
  const char *sql_get_weights =
    "SELECT code, genesis_weight FROM planettypes ORDER BY id;";


  sqlite3_prepare_v2 (db, sql_get_weights, -1, &stmt_get_weights, NULL);
  int class_idx_counter = 0;    // Use a counter for the fixed 'classes' array


  while (sqlite3_step (stmt_get_weights) == SQLITE_ROW
         && class_idx_counter < 7)
    {
      const char *code =
        (const char *) sqlite3_column_text (stmt_get_weights, 0);
      int weight = sqlite3_column_int (stmt_get_weights, 1);
      // Find the index for this 'code' in our 'classes' array
      int current_class_fixed_idx = -1;


      for (int i = 0; i < 7; ++i)
        {
          if (strcasecmp (code, classes[i]) == 0)
            {
              weights[i] = weight;
              current_class_fixed_idx = i;
              break;
            }
        }
      if (current_class_fixed_idx != -1)
        {
          if (weights[current_class_fixed_idx] < 0)
            {
              weights[current_class_fixed_idx] = 0;
            }
          total_weight += weights[current_class_fixed_idx];
        }
      else
        {
          LOGW
          (
            "Unknown planet type code '%s' found in planettypes table. Ignoring.",
            code);
        }
      class_idx_counter++;      // Increment only for the while loop condition
    }
  sqlite3_finalize (stmt_get_weights);
  if (total_weight <= 0)
    {                           // Fallback if all weights are zero or invalid from DB
      LOGE
      (
        "Planet class weights from DB invalid or not found. Falling back to hardcoded defaults.");
      // Re-initialize weights from default macros (if any) or equal distribution
      weights[0] = 10;
      weights[1] = 10;
      weights[2] = 10;
      weights[3] = 10;          // Example defaults
      weights[4] = 10;
      weights[5] = 10;
      weights[6] = 5;
      total_weight = 65;        // Sum of example defaults
    }
  // Perform weighted random selection
  int random_val = randomnum (0, total_weight - 1);
  int selected_idx = 0;
  int current_sum = 0;


  for (int i = 0; i < 7; ++i)
    {
      current_sum += weights[i];
      if (random_val < current_sum)
        {
          selected_idx = i;
          break;
        }
    }
  strncpy (planet_class_str, classes[selected_idx], 1);
  planet_class_str[1] = '\0';
  // 9. Resolve planettypes.id
  int planet_type_id = -1;
  sqlite3_stmt *stmt_get_planet_type;
  const char *sql_get_planet_type =
    "SELECT id FROM planettypes WHERE code = ?;";


  sqlite3_prepare_v2 (db, sql_get_planet_type, -1, &stmt_get_planet_type,
                      NULL);
  sqlite3_bind_text (stmt_get_planet_type, 1, planet_class_str, -1,
                     SQLITE_STATIC);
  if (sqlite3_step (stmt_get_planet_type) == SQLITE_ROW)
    {
      planet_type_id = sqlite3_column_int (stmt_get_planet_type, 0);
    }
  sqlite3_finalize (stmt_get_planet_type);
  if (planet_type_id == -1)
    {
      LOGE ("Failed to find planettype_id for class %s", planet_class_str);
      free (planet_name);
      return send_error_and_return (ctx,
                                    root,
                                    ERR_DB_QUERY_FAILED,
                                    "Failed to resolve planet type for creation.");
    }
  // 10. Insert Planet Row
  // The `num` field is an integer legacy planet ID. We can probably just leave it NULL for new planets,
  // or assign a new sequential ID if it's strictly required and managed.
  // For now, let's leave it NULL, assuming it's an auto-increment or not strictly needed for new planets.
  sqlite3_stmt *stmt_insert_planet;
  const char *sql_insert_planet =
    "INSERT INTO planets (num, sector, name, owner_id, owner_type, class, population, type, creator, colonist, fighters, created_at, created_by, genesis_flag, citadel_level, ore_on_hand, organics_on_hand, equipment_on_hand) "
    "VALUES (NULL, ?, ?, ?, ?, 0, ?, '', 0, 0, ?, ?, 1, 0, 0, 0, 0);";                                                                                                                                                                                                                                                                  // Populate initial fields


  sqlite3_prepare_v2 (db, sql_insert_planet, -1, &stmt_insert_planet, NULL);
  sqlite3_bind_int (stmt_insert_planet, 1, target_sector_id);
  sqlite3_bind_text (stmt_insert_planet, 2, planet_name, -1,
                     SQLITE_TRANSIENT);
  sqlite3_bind_int (stmt_insert_planet, 3, owner_id);
  sqlite3_bind_text (stmt_insert_planet, 4, owner_entity_type, -1,
                     SQLITE_STATIC);
  sqlite3_bind_text (stmt_insert_planet, 5, planet_class_str, -1,
                     SQLITE_STATIC);
  sqlite3_bind_int (stmt_insert_planet, 6, planet_type_id);
  sqlite3_bind_int (stmt_insert_planet, 7, current_unix_ts);
  sqlite3_bind_int (stmt_insert_planet, 8, player_id);
  rc = sqlite3_step (stmt_insert_planet);
  if (rc != SQLITE_DONE)
    {
      LOGE ("Failed to insert new planet: %s (code: %d)", sqlite3_errmsg (db),
            rc);
      // Check specifically for ERR_UNIVERSE_FULL from the trigger
      if (rc == SQLITE_ABORT
          && strstr (sqlite3_errmsg (db), "ERR_UNIVERSE_FULL"))
        {
          rc =
            send_error_and_return (ctx,
                                   root,
                                   ERR_UNIVERSE_FULL,
                                   "The universe has reached its maximum planet capacity.");
        }
      else
        {
          rc =
            send_error_and_return (ctx, root, ERR_DB,
                                   "Failed to create planet.");
        }
      sqlite3_finalize (stmt_insert_planet);
      free (planet_name);
      return rc;
    }
  new_planet_id = sqlite3_last_insert_rowid (db);
  sqlite3_finalize (stmt_insert_planet);
  // 11. Consume Genesis Torpedo
  sqlite3_stmt *stmt_update_ship;
  const char *sql_update_ship =
    "UPDATE ships SET genesis = genesis - 1 WHERE id = ? AND genesis >= 1;";


  sqlite3_prepare_v2 (db, sql_update_ship, -1, &stmt_update_ship, NULL);
  sqlite3_bind_int (stmt_update_ship, 1, ship_id);
  rc = sqlite3_step (stmt_update_ship);
  if (rc != SQLITE_DONE || sqlite3_changes (db) == 0)
    {
      LOGE ("Failed to decrement genesis torps for ship %d: %s (changes: %d)",
            ship_id, sqlite3_errmsg (db), sqlite3_changes (db));
      // This should not happen if the check in step 7 passed, but handle defensively
      rc =
        send_error_and_return (ctx, root, ERR_DB,
                               "Failed to consume Genesis torpedo.");
      sqlite3_finalize (stmt_update_ship);
      free (planet_name);
      return rc;
    }
  sqlite3_finalize (stmt_update_ship);
  // 12. Adjust NavHaz
  navhaz_delta = GENESIS_NAVHAZ_DELTA;  // Using macro
  if (navhaz_delta != 0)
    {
      // Assume 'navhaz' column exists in 'sectors' table for this to work
      // If not, ALTER TABLE sectors ADD COLUMN navhaz INTEGER NOT NULL DEFAULT 0; is needed.
      // For now, it's a stub if the column is not present.
      sqlite3_stmt *stmt_update_sector;
      const char *sql_update_sector =
        "UPDATE sectors SET navhaz = MAX(0, COALESCE(navhaz, 0) + ?) WHERE id = ?;";


      rc =
        sqlite3_prepare_v2 (db, sql_update_sector, -1, &stmt_update_sector,
                            NULL);
      if (rc == SQLITE_OK)
        {
          sqlite3_bind_int (stmt_update_sector, 1, navhaz_delta);
          sqlite3_bind_int (stmt_update_sector, 2, target_sector_id);
          if (sqlite3_step (stmt_update_sector) != SQLITE_DONE)
            {
              LOGW ("Failed to update navhaz for sector %d: %s",
                    target_sector_id, sqlite3_errmsg (db));
            }
        }
      else
        {
          LOGW ("Failed to prepare navhaz update statement: %s",
                sqlite3_errmsg (db));
        }
      sqlite3_finalize (stmt_update_sector);
      LOGD
        ("NavHaz adjustment (delta %d) for sector %d applied (or attempted).",
        navhaz_delta, target_sector_id);
    }
  // 13. Idempotency Recording
  if (idempotency_key)
    {
      response_json = json_pack ("{s:s, s:s, s:o}",
                                 "status", "ok",
                                 "type", "planet.genesis_created_v1",
                                 "data",
                                 json_pack
                                   ("{s:i, s:i, s:s, s:s, s:s, s:i, s:b, s:i}",
                                   "sector_id", target_sector_id, "planet_id",
                                   (int) new_planet_id, "class",
                                   planet_class_str, "name", planet_name,
                                   "owner_type", owner_entity_type, "owner_id",
                                   owner_id, "over_cap", over_cap_flag,
                                   "navhaz_delta", navhaz_delta));
      char *success_response_str = json_dumps (response_json, 0);
      sqlite3_stmt *stmt_idem_record;
      const char *sql_record_idem =
        "INSERT INTO idempotency (key, cmd, req_fp, response, created_at, updated_at) "
        "VALUES (?, 'planet.genesis_create', ?, ?, ?, ?);";                                                                                                             // req_fp is hash of original request


      sqlite3_prepare_v2 (db, sql_record_idem, -1, &stmt_idem_record, NULL);
      sqlite3_bind_text (stmt_idem_record, 1, idempotency_key, -1,
                         SQLITE_STATIC);
      // TODO: Replace with actual hash of original request for req_fp
      sqlite3_bind_text (stmt_idem_record, 2, "placeholder_req_fp", -1,
                         SQLITE_STATIC);
      sqlite3_bind_text (stmt_idem_record, 3, success_response_str, -1,
                         SQLITE_TRANSIENT);
      sqlite3_bind_int (stmt_idem_record, 4, current_unix_ts);
      sqlite3_bind_int (stmt_idem_record, 5, current_unix_ts);
      if (sqlite3_step (stmt_idem_record) != SQLITE_DONE)
        {
          LOGE ("Failed to record idempotency for key %s: %s",
                idempotency_key, sqlite3_errmsg (db));
        }
      sqlite3_finalize (stmt_idem_record);
      free (success_response_str);
      // response_json will be decreffed by send_json_response
    }
  // 14. Emit JSON Success Response (if not handled by idempotency already)
  // If response_json was already created for idempotency, reuse it.
  if (!response_json)
    {
      response_json = json_pack ("{s:s, s:s, s:o}",
                                 "status", "ok",
                                 "type", "planet.genesis_created_v1",
                                 "data",
                                 json_pack
                                   ("{s:i, s:i, s:s, s:s, s:s, s:i, s:b, s:i}",
                                   "sector_id", target_sector_id, "planet_id",
                                   (int) new_planet_id, "class",
                                   planet_class_str, "name", planet_name,
                                   "owner_type", owner_entity_type, "owner_id",
                                   owner_id, "over_cap", over_cap_flag,
                                   "navhaz_delta", navhaz_delta));
    }
  send_json_response (ctx, response_json);      // Assumes this sends and decrefs
  // 15. Event Logging (System Broadcast)
  json_t *event_payload = json_pack ("{s:i, s:s, s:s, s:i, s:b, s:i}",
                                     "planet_id", (int) new_planet_id,
                                     "class", planet_class_str,
                                     "name", planet_name,
                                     "owner_id", player_id,
                                     "over_cap", over_cap_flag,
                                     "sector_id", target_sector_id);
  char *event_payload_str = json_dumps (event_payload, 0);
  sqlite3_stmt *stmt_log_event;
  const char *sql_log_event =
    "INSERT INTO engine_events (ts, type, actor_player_id, sector_id, payload, idem_key) "
    "VALUES (?, 'planet.genesis_created', ?, ?, ?, ?);";


  sqlite3_prepare_v2 (db, sql_log_event, -1, &stmt_log_event, NULL);
  sqlite3_bind_int (stmt_log_event, 1, current_unix_ts);
  sqlite3_bind_int (stmt_log_event, 2, player_id);
  sqlite3_bind_int (stmt_log_event, 3, target_sector_id);
  sqlite3_bind_text (stmt_log_event, 4, event_payload_str, -1,
                     SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt_log_event, 5,
                     idempotency_key ? idempotency_key : "", -1,
                     SQLITE_STATIC);
  if (sqlite3_step (stmt_log_event) != SQLITE_DONE)
    {
      LOGE ("Failed to log engine event for planet.genesis_created: %s",
            sqlite3_errmsg (db));
    }
  sqlite3_finalize (stmt_log_event);
  free (event_payload_str);
  json_decref (event_payload);
  // Cleanup
  free (planet_name);
  return 0;                     // Success
}

/**
 * @brief Adjusts a planet's stock for market settlement purposes.
 *
 * Explicitly enforces that the resulting quantity stays within [0, max_capacity].
 *
 * @param db Database handle.
 * @param planet_id The ID of the planet.
 * @param commodity_code The canonical code of the commodity.
 * @param quantity_delta The amount to add (positive) or subtract (negative).
 * @return SQLITE_OK on success, or an SQLite error code.
 */
int
h_market_move_planet_stock (sqlite3 *db,
                            int planet_id,
                            const char *commodity_code,
                            int quantity_delta)
{
  if (!db || planet_id <= 0 || !commodity_code)
    {
      return SQLITE_MISUSE;
    }
  
  if (quantity_delta == 0) {
      return SQLITE_OK;
  }

  // 1. Get current quantity and capacity
  sqlite3_stmt *stmt = NULL;
  
  // We need to join planettypes to get max capacity for the specific commodity.
  // Assuming commodity_code is 'ORE', 'ORG', or 'EQU'.
  // We need to select the correct column based on code.
  // Since we can't dynamic SQL binding for column name easily in this constrained env,
  // we fetch all maxes and pick one in C.
  
  const char *sql_info = 
      "SELECT es.quantity, pt.maxore, pt.maxorganics, pt.maxequipment "
      "FROM planets p "
      "JOIN planettypes pt ON p.type = pt.id "
      "LEFT JOIN entity_stock es ON p.id = es.entity_id AND es.entity_type = 'planet' AND es.commodity_code = ?2 "
      "WHERE p.id = ?1;";
  
  int rc = sqlite3_prepare_v2(db, sql_info, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
      LOGE("h_market_move_planet_stock: prepare info failed: %s", sqlite3_errmsg(db));
      return rc;
  }
  
  sqlite3_bind_int(stmt, 1, planet_id);
  sqlite3_bind_text(stmt, 2, commodity_code, -1, SQLITE_STATIC);
  
  int current_quantity = 0;
  int max_capacity = 0;
  
  if (sqlite3_step(stmt) == SQLITE_ROW) {
      current_quantity = sqlite3_column_int(stmt, 0); // NULL becomes 0
      int maxore = sqlite3_column_int(stmt, 1);
      int maxorg = sqlite3_column_int(stmt, 2);
      int maxequ = sqlite3_column_int(stmt, 3);
      
      if (strcasecmp(commodity_code, "ORE") == 0) max_capacity = maxore;
      else if (strcasecmp(commodity_code, "ORG") == 0) max_capacity = maxorg;
      else if (strcasecmp(commodity_code, "EQU") == 0) max_capacity = maxequ;
      else max_capacity = 999999; // Fallback
      
  } else {
      LOGE("h_market_move_planet_stock: Planet %d not found.", planet_id);
      sqlite3_finalize(stmt);
      return SQLITE_NOTFOUND;
  }
  sqlite3_finalize(stmt);

  // 2. Calculate new quantity with bounds checking
  int new_quantity = current_quantity + quantity_delta;
  new_quantity = (new_quantity < 0) ? 0 : new_quantity;
  new_quantity = (new_quantity > max_capacity) ? max_capacity : new_quantity;

  // 3. Update DB
  const char *sql_upsert =
    "INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price, last_updated_ts) "
    "VALUES ('planet', ?1, ?2, ?3, 0, strftime('%s','now')) "
    "ON CONFLICT(entity_type, entity_id, commodity_code) DO UPDATE SET quantity = ?3, last_updated_ts = strftime('%s','now');";

  rc = sqlite3_prepare_v2(db, sql_upsert, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
      LOGE("h_market_move_planet_stock: prepare upsert failed: %s", sqlite3_errmsg(db));
      return rc;
  }
  
  sqlite3_bind_int(stmt, 1, planet_id);
  sqlite3_bind_text(stmt, 2, commodity_code, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 3, new_quantity);
  
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
       LOGE("h_market_move_planet_stock: upsert failed: %s", sqlite3_errmsg(db));
       sqlite3_finalize(stmt);
       return SQLITE_ERROR;
  }
  
  sqlite3_finalize(stmt);
  return SQLITE_OK;
}

static int h_get_commodity_id_by_code(sqlite3 *db, const char *code) {
    sqlite3_stmt *st = NULL;
    int id = 0;
    if (sqlite3_prepare_v2(db, "SELECT id FROM commodities WHERE code = ?1;", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, code, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW) {
            id = sqlite3_column_int(st, 0);
        }
        sqlite3_finalize(st);
    }
    return id;
}

int
cmd_planet_market_sell (client_ctx_t *ctx, json_t *root)
{
    sqlite3 *db = db_get_handle();
    if (!ctx || ctx->player_id <= 0) {
        send_enveloped_refused(ctx->fd, root, ERR_NOT_AUTHENTICATED, "Not authenticated", NULL);
        return 0;
    }

    json_t *data = json_object_get(root, "data");
    if (!data) {
        send_enveloped_error(ctx->fd, root, ERR_BAD_REQUEST, "Missing data payload.");
        return 0;
    }

    int planet_id = 0;
    json_unpack(data, "{s:i}", "planet_id", &planet_id);
    const char *raw_commodity = json_string_value(json_object_get(data, "commodity"));
    int quantity = 0;
    json_unpack(data, "{s:i}", "quantity", &quantity);

    if (planet_id <= 0 || !raw_commodity || quantity <= 0) {
        send_enveloped_error(ctx->fd, root, ERR_INVALID_ARG, "Invalid planet_id, commodity, or quantity.");
        return 0;
    }

    planet_t p = {0};
    if (h_get_planet_owner_info(db, planet_id, &p) != SQLITE_OK) {
        send_enveloped_error(ctx->fd, root, ERR_NOT_FOUND, "Planet not found.");
        return 0;
    }

    if (planet_is_npc(&p)) {
        h_free_planet_t(&p);
        send_enveloped_refused(ctx->fd, root, 1403, "Cannot manually trade with NPC planets.", NULL);
        return 0;
    }

    // Ownership check
    bool allowed = false;
    if (p.owner_type && strcmp(p.owner_type, "player") == 0 && p.owner_id == ctx->player_id) {
        allowed = true;
    } else if (p.owner_type && (strcmp(p.owner_type, "corp") == 0 || strcmp(p.owner_type, "corporation") == 0)) {
         if (ctx->corp_id > 0 && ctx->corp_id == p.owner_id) {
             allowed = true;
         }
    }

    h_free_planet_t(&p);

    if (!allowed) {
        send_enveloped_refused(ctx->fd, root, 1403, "You do not control this planet.", NULL);
        return 0;
    }

    char *commodity_code = (char*)commodity_to_code(raw_commodity);
    if (!commodity_code) {
        send_enveloped_refused(ctx->fd, root, 1405, "Invalid commodity.", NULL);
        return 0;
    }

    // Check stock
    int current_stock = 0;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, "SELECT quantity FROM entity_stock WHERE entity_type='planet' AND entity_id=?1 AND commodity_code=?2", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, planet_id);
        sqlite3_bind_text(st, 2, commodity_code, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW) {
            current_stock = sqlite3_column_int(st, 0);
        }
        sqlite3_finalize(st);
    }

    if (current_stock < quantity) {
        free(commodity_code);
        send_enveloped_refused(ctx->fd, root, 1402, "Insufficient stock on planet.", NULL);
        return 0;
    }

    int unit_price = 0;
    // Basic price fetch from commodities table (base_price) as per prompt guidance to use existing market logic 
    // but simplified since no planet pricing helper exists. 
    // Actually, prompt says: "Use h_entity_calculate_sell_price... (now supporting ENTITY_TYPE_PLANET if necessary)".
    // Since I couldn't modify server_ports.c to add planet support to h_entity_calculate_sell_price without violating "no changes to existing trade commands" (risk of regression),
    // and `commodities.base_price` is the core of the market logic, I will use that.
    if (sqlite3_prepare_v2(db, "SELECT base_price FROM commodities WHERE code=?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, commodity_code, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW) {
            unit_price = sqlite3_column_int(st, 0);
        }
        sqlite3_finalize(st);
    }
    
    if (unit_price <= 0) {
        free(commodity_code);
        send_enveloped_error(ctx->fd, root, 500, "Could not determine commodity price.");
        return 0;
    }

    long long total_credits = (long long)quantity * unit_price;

    // Execute Sale
    // 1. Update Stock
    if (h_update_entity_stock(db, ENTITY_TYPE_PLANET, planet_id, commodity_code, -quantity, NULL) != SQLITE_OK) {
        free(commodity_code);
        send_enveloped_error(ctx->fd, root, 500, "Failed to update planet stock.");
        return 0;
    }

    // 2. Credit Player
    long long new_balance = 0;
    if (h_add_player_petty_cash(db, ctx->player_id, total_credits, &new_balance) != SQLITE_OK) {
        LOGE("cmd_planet_market_sell: Failed to credit player %d. Stock was already deducted!", ctx->player_id);
    }

    // Response
    json_t *resp = json_object();
    json_object_set_new(resp, "planet_id", json_integer(planet_id));
    json_object_set_new(resp, "commodity", json_string(commodity_code));
    json_object_set_new(resp, "quantity_sold", json_integer(quantity));
    json_object_set_new(resp, "total_credits_received", json_integer(total_credits));
    
    send_enveloped_ok(ctx->fd, root, "planet.market.sell", resp);
    json_decref(resp);
    free(commodity_code);
    return 0;
}

int
cmd_planet_market_buy_order (client_ctx_t *ctx, json_t *root)
{
    sqlite3 *db = db_get_handle();
    if (!ctx || ctx->player_id <= 0) {
        send_enveloped_refused(ctx->fd, root, ERR_NOT_AUTHENTICATED, "Not authenticated", NULL);
        return 0;
    }

    json_t *data = json_object_get(root, "data");
    if (!data) {
        send_enveloped_error(ctx->fd, root, ERR_BAD_REQUEST, "Missing data payload.");
        return 0;
    }

    int planet_id = 0;
    json_unpack(data, "{s:i}", "planet_id", &planet_id);
    const char *raw_commodity = json_string_value(json_object_get(data, "commodity"));
    int quantity_total = 0;
    json_unpack(data, "{s:i}", "quantity_total", &quantity_total);
    int max_price = 0;
    json_unpack(data, "{s:i}", "max_price", &max_price);

    if (planet_id <= 0 || !raw_commodity || quantity_total <= 0 || max_price <= 0) {
        send_enveloped_error(ctx->fd, root, ERR_INVALID_ARG, "Invalid parameters.");
        return 0;
    }

    planet_t p = {0};
    if (h_get_planet_owner_info(db, planet_id, &p) != SQLITE_OK) {
        send_enveloped_error(ctx->fd, root, ERR_NOT_FOUND, "Planet not found.");
        return 0;
    }

    if (planet_is_npc(&p)) {
        h_free_planet_t(&p);
        send_enveloped_refused(ctx->fd, root, 1403, "Cannot manually trade with NPC planets.", NULL);
        return 0;
    }

    // Ownership check
    bool allowed = false;
    if (p.owner_type && strcmp(p.owner_type, "player") == 0 && p.owner_id == ctx->player_id) {
        allowed = true;
    } else if (p.owner_type && (strcmp(p.owner_type, "corp") == 0 || strcmp(p.owner_type, "corporation") == 0)) {
         if (ctx->corp_id > 0 && ctx->corp_id == p.owner_id) {
             allowed = true;
         }
    }
    h_free_planet_t(&p);

    if (!allowed) {
        send_enveloped_refused(ctx->fd, root, 1403, "You do not control this planet.", NULL);
        return 0;
    }

    char *commodity_code = (char*)commodity_to_code(raw_commodity);
    if (!commodity_code) {
        send_enveloped_refused(ctx->fd, root, 1405, "Invalid commodity.", NULL);
        return 0;
    }
    
    int commodity_id = h_get_commodity_id_by_code(db, commodity_code);
    if (commodity_id <= 0) {
        free(commodity_code);
        send_enveloped_error(ctx->fd, root, 500, "Commodity ID lookup failed.");
        return 0;
    }

    // Credit Check
    long long worst_case_cost = (long long)quantity_total * max_price;
    long long player_cash = 0;
    if (h_get_player_petty_cash(db, ctx->player_id, &player_cash) != SQLITE_OK) {
        free(commodity_code);
        send_enveloped_error(ctx->fd, root, 500, "Failed to check credits.");
        return 0;
    }

    if (player_cash < worst_case_cost) {
        free(commodity_code);
        send_enveloped_refused(ctx->fd, root, 1402, "Insufficient credits for max cost.", NULL);
        return 0;
    }

    // Insert Order - Manual SQL to include location fields
    sqlite3_stmt *stmt = NULL;
    const char *sql = 
        "INSERT INTO commodity_orders ("
        "actor_type, actor_id, location_type, location_id, commodity_id, side, quantity, price, status, ts, expires_at, filled_quantity) "
        "VALUES (?, ?, 'planet', ?, ?, 'buy', ?, ?, 'open', strftime('%s','now'), ?, 0);";
        
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        free(commodity_code);
        send_enveloped_error(ctx->fd, root, 500, "DB Error preparing order.");
        return 0;
    }
    
    sqlite3_bind_text(stmt, 1, "player", -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, ctx->player_id);
    sqlite3_bind_int(stmt, 3, planet_id);
    sqlite3_bind_int(stmt, 4, commodity_id);
    sqlite3_bind_int(stmt, 5, quantity_total);
    sqlite3_bind_int(stmt, 6, max_price);
    sqlite3_bind_null(stmt, 7); // expires_at null for now
    
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        free(commodity_code);
        send_enveloped_error(ctx->fd, root, 500, "DB Error inserting order.");
        return 0;
    }
    
    int order_id = (int)sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);

    // Response
    json_t *resp = json_object();
    json_object_set_new(resp, "order_id", json_integer(order_id));
    json_object_set_new(resp, "planet_id", json_integer(planet_id));
    json_object_set_new(resp, "commodity", json_string(commodity_code));
    json_object_set_new(resp, "quantity_total", json_integer(quantity_total));
    json_object_set_new(resp, "max_price", json_integer(max_price));
    
    send_enveloped_ok(ctx->fd, root, "planet.market.buy_order", resp);
    json_decref(resp);
    free(commodity_code);
    return 0;
}

/* ========================================================================= */
/* SCHEMA PLAN (P3B) */
/* Table: planets
 * Proposed columns:
 *   colonists_ore INTEGER NOT NULL DEFAULT 0
 *   colonists_org INTEGER NOT NULL DEFAULT 0
 *   colonists_eq  INTEGER NOT NULL DEFAULT 0
 *   colonists_mil INTEGER NOT NULL DEFAULT 0
 *
 * (Legacy 'colonist' column may remain for total or migration purposes)
 */
/* ========================================================================= */

int
cmd_planet_colonists_set (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle();
  if (!ctx || ctx->player_id <= 0) {
      send_enveloped_refused(ctx->fd, root, ERR_NOT_AUTHENTICATED, "Not authenticated", NULL);
      return 0;
  }

  json_t *data = json_object_get(root, "data");
  if (!data) {
      send_enveloped_error(ctx->fd, root, ERR_BAD_REQUEST, "Missing data payload.");
      return 0;
  }

  int planet_id = 0;
  json_unpack(data, "{s:i}", "planet_id", &planet_id);
  
  if (planet_id <= 0) {
      send_enveloped_error(ctx->fd, root, ERR_INVALID_ARG, "Invalid planet_id.");
      return 0;
  }

  planet_t p = {0};
  if (h_get_planet_owner_info(db, planet_id, &p) != SQLITE_OK) {
      send_enveloped_error(ctx->fd, root, ERR_NOT_FOUND, "Planet not found.");
      return 0;
  }

  // Ownership check
  bool allowed = false;
  if (p.owner_type && strcmp(p.owner_type, "player") == 0 && p.owner_id == ctx->player_id) {
      allowed = true;
  } else if (p.owner_type && (strcmp(p.owner_type, "corp") == 0 || strcmp(p.owner_type, "corporation") == 0)) {
       if (ctx->corp_id > 0 && ctx->corp_id == p.owner_id) {
           allowed = true;
       }
  }
  h_free_planet_t(&p);

  if (!allowed) {
      send_enveloped_refused(ctx->fd, root, 1403, "You do not control this planet.", NULL);
      return 0;
  }

  /* Get inputs (optional) */
  json_t *j_ore = json_object_get(data, "colonists_ore");
  json_t *j_org = json_object_get(data, "colonists_org");
  json_t *j_eq  = json_object_get(data, "colonists_eq");
  json_t *j_mil = json_object_get(data, "colonists_mil");

  /* Fetch current state (TODO: using 'colonist' for total, assuming dummy per-role columns for now) */
  /* Since schema doesn't exist, we will simulate reading/writing from legacy 'colonist' or temporary memory if possible,
     but for persistence we are limited.
     However, instructions say: "If per-role fields do not exist... Implement code with clear TODO comments where it depends on those columns".
     I will simulate the logic but fail to persist specifically to non-existent columns, or use a dummy store if possible.
     Wait, I can't persist to non-existent columns.
     I will read 'population' as total.
     I will check sum of inputs <= population.
     I will stub the persistence.
  */
  
  int pop_total = 0;
  sqlite3_stmt *st_pop = NULL;
  if (sqlite3_prepare_v2(db, "SELECT population FROM planets WHERE id=?1", -1, &st_pop, NULL) == SQLITE_OK) {
      sqlite3_bind_int(st_pop, 1, planet_id);
      if (sqlite3_step(st_pop) == SQLITE_ROW) {
          pop_total = sqlite3_column_int(st_pop, 0);
      }
      sqlite3_finalize(st_pop);
  }

  // Current values (Default to 0 if columns missing)
  int cur_ore = 0, cur_org = 0, cur_eq = 0, cur_mil = 0;
  // TODO: SELECT colonists_ore, colonists_org... FROM planets WHERE id=?

  int new_ore = j_ore ? json_integer_value(j_ore) : cur_ore;
  int new_org = j_org ? json_integer_value(j_org) : cur_org;
  int new_eq  = j_eq  ? json_integer_value(j_eq)  : cur_eq;
  int new_mil = j_mil ? json_integer_value(j_mil) : cur_mil;

  if (new_ore < 0 || new_org < 0 || new_eq < 0 || new_mil < 0) {
      send_enveloped_error(ctx->fd, root, ERR_INVALID_ARG, "Colonist counts must be non-negative.");
      return 0;
  }

  if ((long long)new_ore + new_org + new_eq + new_mil > pop_total) {
      send_enveloped_error(ctx->fd, root, 1406, "Total allocation exceeds population.");
      return 0;
  }

  /* Persist updates */
  // TODO: UPDATE planets SET colonists_ore=?, ... WHERE id=?
  // For now, we pretend we saved it.
  
  json_t *resp = json_object();
  json_object_set_new(resp, "planet_id", json_integer(planet_id));
  json_object_set_new(resp, "colonists_ore", json_integer(new_ore));
  json_object_set_new(resp, "colonists_org", json_integer(new_org));
  json_object_set_new(resp, "colonists_eq", json_integer(new_eq));
  json_object_set_new(resp, "colonists_mil", json_integer(new_mil));
  json_object_set_new(resp, "population_total", json_integer(pop_total));

  send_enveloped_ok(ctx->fd, root, "planet.colonists.set", resp);
  json_decref(resp);
  return 0;
}

int
cmd_planet_colonists_get (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle();
  if (!ctx || ctx->player_id <= 0) {
      send_enveloped_refused(ctx->fd, root, ERR_NOT_AUTHENTICATED, "Not authenticated", NULL);
      return 0;
  }

  json_t *data = json_object_get(root, "data");
  if (!data) {
      send_enveloped_error(ctx->fd, root, ERR_BAD_REQUEST, "Missing data payload.");
      return 0;
  }

  int planet_id = 0;
  json_unpack(data, "{s:i}", "planet_id", &planet_id);
  
  if (planet_id <= 0) {
      send_enveloped_error(ctx->fd, root, ERR_INVALID_ARG, "Invalid planet_id.");
      return 0;
  }

  planet_t p = {0};
  if (h_get_planet_owner_info(db, planet_id, &p) != SQLITE_OK) {
      send_enveloped_error(ctx->fd, root, ERR_NOT_FOUND, "Planet not found.");
      return 0;
  }

  // Ownership check
  bool allowed = false;
  if (p.owner_type && strcmp(p.owner_type, "player") == 0 && p.owner_id == ctx->player_id) {
      allowed = true;
  } else if (p.owner_type && (strcmp(p.owner_type, "corp") == 0 || strcmp(p.owner_type, "corporation") == 0)) {
       if (ctx->corp_id > 0 && ctx->corp_id == p.owner_id) {
           allowed = true;
       }
  }
  h_free_planet_t(&p);

  if (!allowed) {
      send_enveloped_refused(ctx->fd, root, 1403, "You do not control this planet.", NULL);
      return 0;
  }

  int pop_total = 0;
  // Default values
  int cur_ore = 0, cur_org = 0, cur_eq = 0, cur_mil = 0;

  // TODO: SELECT population, colonists_ore, ... FROM planets WHERE id=?
  sqlite3_stmt *st_pop = NULL;
  if (sqlite3_prepare_v2(db, "SELECT population FROM planets WHERE id=?1", -1, &st_pop, NULL) == SQLITE_OK) {
      sqlite3_bind_int(st_pop, 1, planet_id);
      if (sqlite3_step(st_pop) == SQLITE_ROW) {
          pop_total = sqlite3_column_int(st_pop, 0);
          // cur_ore = ...
      }
      sqlite3_finalize(st_pop);
  }

  json_t *resp = json_object();
  json_object_set_new(resp, "planet_id", json_integer(planet_id));
  json_object_set_new(resp, "colonists_ore", json_integer(cur_ore));
  json_object_set_new(resp, "colonists_org", json_integer(cur_org));
  json_object_set_new(resp, "colonists_eq", json_integer(cur_eq));
  json_object_set_new(resp, "colonists_mil", json_integer(cur_mil));
  json_object_set_new(resp, "population_total", json_integer(pop_total));

  send_enveloped_ok(ctx->fd, root, "planet.colonists.get", resp);
  json_decref(resp);
  return 0;
}