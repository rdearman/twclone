#include <string.h>
#include <stdio.h>
#include <stdlib.h> // For malloc, free, calloc, realloc
#include <string.h> // For memcpy, strlen, strncpy, strncat
#include <sqlite3.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <jansson.h>
#include <stdbool.h>
#include <pthread.h>
#include <stddef.h> // For size_t

// local includes
#include  "common.h"
#include "server_config.h"
#include "database.h"
#include "server_log.h"
#include "server_cron.h"


/* Define and initialize the mutex for the database handle */
// pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t db_mutex;




// Helper flag to ensure initialization runs only once
static bool db_mutex_initialized = false;

static sqlite3 *db_handle = NULL;
extern sqlite3 *g_db;

/* Forward declaration so we can call it before the definition */
static json_t *parse_neighbors_csv (const unsigned char *txt);
/* Unlocked helpers (call only when db_mutex is already held) */
static int db_ensure_auth_schema_unlocked (void);
static int db_ensure_idempotency_schema_unlocked (void);
static int db_create_tables_unlocked (bool schema_exists);
static int db_insert_defaults_unlocked (void);
static int db_seed_ai_qa_bot_bank_account_unlocked (void);
static int db_ensure_ship_perms_column_unlocked (void);
int db_seed_cron_tasks (sqlite3 * db);
void db_handle_close_and_reset (void);

// New function to add
void
db_handle_close_and_reset (void)
{
  // Only proceed if the handle is open
  if (db_handle != NULL)
    {
      sqlite3_close (db_handle);
      db_handle = NULL;
    }
}

static void
db_init_recursive_mutex_once (void)
{
  if (db_mutex_initialized)
    {
      return;
    }

  // Set up recursive attributes
  pthread_mutexattr_t attr;
  if (pthread_mutexattr_init (&attr) == 0)
    {
      pthread_mutexattr_settype (&attr, PTHREAD_MUTEX_RECURSIVE);
      pthread_mutex_init (&db_mutex, &attr);
      pthread_mutexattr_destroy (&attr);
      db_mutex_initialized = true;
    }
  else
    {
      LOGE ("FATAL: Failed to initialize mutex attributes for db_mutex.");
    }
}



// database.c

sqlite3 *
db_get_handle (void)
{
  // Flag to ensure the mutex is initialized only once
  static bool mutex_initialized = false;	// Static ensures it's only checked once

  // 1. Check if the handle is already open. If so, return it immediately.
  if (db_handle)
    {
      return db_handle;
    }

  // =================================================================
  // CRITICAL FIX: Mutex initialization runs ONCE before DB open
  // =================================================================
  if (!mutex_initialized)
    {
      pthread_mutexattr_t attr;
      if (pthread_mutexattr_init (&attr) != 0)
	{
	  LOGE ("FATAL: Failed to initialize mutex attributes.");
	  return NULL;
	}

      // 1b. Set the mutex type to RECURSIVE (the core fix)
      if (pthread_mutexattr_settype (&attr, PTHREAD_MUTEX_RECURSIVE) != 0)
	{
	  LOGE ("FATAL: Failed to set mutex type to recursive.");
	  pthread_mutexattr_destroy (&attr);
	  return NULL;
	}

      // 1c. Initialize the global db_mutex with recursive attributes
      if (pthread_mutex_init (&db_mutex, &attr) != 0)
	{
	  LOGE ("FATAL: Failed to initialize recursive db_mutex.");
	  pthread_mutexattr_destroy (&attr);
	  return NULL;
	}

      pthread_mutexattr_destroy (&attr);

      mutex_initialized = true;
    }
  // =================================================================

  // 2. The handle is NULL (due to a close_and_reset or initial load).
  //    Open a new connection using the full, robust V2 flags.
  // LOGI("db_get_handle: Attempting to open new database connection to '%s'", DEFAULT_DB_NAME);
  int rc = sqlite3_open_v2 (DEFAULT_DB_NAME,
			    &db_handle,
			    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
			    SQLITE_OPEN_URI,
			    NULL);

  if (rc != SQLITE_OK)
    {
      LOGE ("db_get_handle: Can't open database '%s': %s (rc=%d)",
	    DEFAULT_DB_NAME, sqlite3_errmsg (db_handle), rc);
      if (db_handle)
	{
	  sqlite3_close (db_handle);
	  db_handle = NULL;
	}
      return NULL;		// Return NULL on failure
    }
  // LOGI("db_get_handle: Successfully opened database connection to '%s'", DEFAULT_DB_NAME);

  // 3. Apply critical settings for concurrency (WAL and busy timeout)
  if (sqlite3_exec (db_handle, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL)
      != SQLITE_OK)
    {
      LOGW ("Failed to set WAL mode on fresh handle.");
    }
  sqlite3_busy_timeout (db_handle, 5000);	// 5 seconds

  // 4. Return the newly opened handle.
  return db_handle;
}



int
db_commands_accept (const char *cmd_type,
		    const char *idem_key,
		    json_t *payload,
		    int *out_cmd_id, int *out_duplicate, int *out_due_at)
{
  if (!cmd_type || !idem_key || !payload)
    return -1;

  // Ensure idempotency index exists once (no-op if already there)
  sqlite3_exec (db_handle,
		"CREATE UNIQUE INDEX IF NOT EXISTS idx_engine_cmds_idem "
		"ON engine_commands(idem_key);", NULL, NULL, NULL);

  const char *SQL_INS =
    "INSERT INTO engine_commands("
    "  type, payload, status, priority, attempts, created_at, due_at, idem_key"
    ") VALUES ("
    "  ?,    json(?), 'ready', 100,      0,        strftime('%s','now'), strftime('%s','now'), ?"
    ");";

  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db_handle, SQL_INS, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return -2;

  char *payload_str = json_dumps (payload, JSON_COMPACT);
  sqlite3_bind_text (st, 1, cmd_type, -1, SQLITE_STATIC);
  sqlite3_bind_text (st, 2, payload_str, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 3, idem_key, -1, SQLITE_STATIC);

  int dup = 0, cmd_id = 0, due_at = 0;

  rc = sqlite3_step (st);
  if (rc == SQLITE_DONE)
    {
      cmd_id = (int) sqlite3_last_insert_rowid (db_handle);
      dup = 0;
    }
  else if (rc == SQLITE_CONSTRAINT)
    {
      // Duplicate: fetch existing id + due_at
      const char *SQL_GET =
	"SELECT id, COALESCE(due_at, strftime('%s','now')) "
	"FROM engine_commands WHERE idem_key = ?;";
      sqlite3_stmt *gt = NULL;
      sqlite3_prepare_v2 (db_handle, SQL_GET, -1, &gt, NULL);
      sqlite3_bind_text (gt, 1, idem_key, -1, SQLITE_STATIC);
      if (sqlite3_step (gt) == SQLITE_ROW)
	{
	  cmd_id = sqlite3_column_int (gt, 0);
	  due_at = sqlite3_column_int (gt, 1);
	}
      sqlite3_finalize (gt);
      dup = 1;
    }
  else
    {
      sqlite3_finalize (st);
      free (payload_str);
      return -3;
    }

  sqlite3_finalize (st);
  free (payload_str);

  // For new rows, read due_at
  if (!dup)
    {
      const char *SQL_DUE =
	"SELECT COALESCE(due_at, strftime('%s','now')) "
	"FROM engine_commands WHERE id = ?;";
      sqlite3_stmt *sd = NULL;
      sqlite3_prepare_v2 (db_handle, SQL_DUE, -1, &sd, NULL);
      sqlite3_bind_int (sd, 1, cmd_id);
      if (sqlite3_step (sd) == SQLITE_ROW)
	{
	  due_at = sqlite3_column_int (sd, 0);
	}
      sqlite3_finalize (sd);
    }

  if (out_cmd_id)
    *out_cmd_id = cmd_id;
  if (out_duplicate)
    *out_duplicate = dup;
  if (out_due_at)
    *out_due_at = due_at;
  return 0;
}


////////////////////

/**
 * @brief Thread-safe wrapper for a simple column update.
 *
 * This function acquires a lock before executing the query and releases
 * the lock afterwards.
 *
 * @param table The name of the table to update.
 * @param id The ID of the row to update.
 * @param column The name of the column to update.
 * @param value A JSON value containing the data to set.
 * @return SQLITE_OK on success, or an SQLite error code.
 */
int
db_thread_safe_update_single_column (const char *table, int id,
				     const char *column, json_t *value)
{
  int rc = -1;
  char *sql = NULL;
  sqlite3_stmt *stmt = NULL;

  // 1. Acquire the lock before accessing the shared database handle
  pthread_mutex_lock (&db_mutex);

  // Build the SQL query string
  const char *template = " UPDATE %s SET %s = ? WHERE id = ?; ";
  sql = sqlite3_mprintf (template, table, column);
  if (!sql)
    {
      pthread_mutex_unlock (&db_mutex);
      return SQLITE_NOMEM;
    }

  // 2. Prepare the SQL statement
  rc = sqlite3_prepare_v2 (db_get_handle (), sql, -1, &stmt, NULL);
  sqlite3_free (sql);

  if (rc != SQLITE_OK)
    {
      goto cleanup;
    }

  // 3. Bind the value based on its type
  if (json_is_integer (value))
    {
      sqlite3_bind_int (stmt, 1, (int) json_integer_value (value));
    }
  else if (json_is_string (value))
    {
      sqlite3_bind_text (stmt, 1, json_string_value (value), -1,
			 SQLITE_STATIC);
    }
  else if (json_is_real (value))
    {
      sqlite3_bind_double (stmt, 1, json_real_value (value));
    }
  else
    {
      rc = SQLITE_ERROR;
      goto cleanup;
    }

  sqlite3_bind_int (stmt, 2, id);

  // 4. Execute the statement
  rc = sqlite3_step (stmt);
  if (rc != SQLITE_DONE)
    {
      goto cleanup;
    }

  rc = SQLITE_OK;

cleanup:
  if (stmt)
    {
      sqlite3_finalize (stmt);
    }

  // 5. Release the lock before the function returns
  pthread_mutex_unlock (&db_mutex);

  return rc;
}


const char *create_table_sql[] = {

/* Advisory locks */
  "CREATE TABLE IF NOT EXISTS locks ("
    "  lock_name TEXT PRIMARY KEY," "  owner TEXT," "  until_ms INTEGER" ");",
  "CREATE INDEX IF NOT EXISTS idx_locks_until ON locks(until_ms);",

/* Engine KV */
  "CREATE TABLE IF NOT EXISTS engine_state ("
    "  state_key TEXT PRIMARY KEY," "  state_val TEXT NOT NULL" ");",

/* ----------------------- original ------------------- */
  " CREATE TABLE IF NOT EXISTS config ( "
    "  id INTEGER PRIMARY KEY CHECK (id = 1), "
    "  turnsperday INTEGER, "
    "  maxwarps_per_sector INTEGER, "
    "  startingcredits INTEGER, "
    "  startingfighters INTEGER, "
    "  startingholds INTEGER, "
    "  processinterval INTEGER, "
    "  autosave INTEGER, "
    "  max_ports INTEGER, "
    "  max_planets_per_sector INTEGER, "
    "  max_total_planets INTEGER, "
    "  max_citadel_level INTEGER, "
    "  number_of_planet_types INTEGER, "
    "  max_ship_name_length INTEGER, "
    "  ship_type_count INTEGER, "
    "  hash_length INTEGER, "
    "  default_nodes INTEGER, "
    "  buff_size INTEGER, "
    "  max_name_length INTEGER, "
    "  max_cloak_duration INTEGER DEFAULT 24, "
    "  planet_type_count INTEGER, "
    "  server_port INTEGER, "
    "  s2s_port INTEGER, "
    "  bank_alert_threshold_player INTEGER DEFAULT 1000000, "
    "  bank_alert_threshold_corp INTEGER DEFAULT 5000000, "
    "  genesis_enabled INTEGER NOT NULL DEFAULT 1, "
    "  genesis_block_at_cap INTEGER NOT NULL DEFAULT 0, "
    "  genesis_navhaz_delta INTEGER NOT NULL DEFAULT 0, "
    "  genesis_class_weight_M INTEGER NOT NULL DEFAULT 10, "
    "  genesis_class_weight_K INTEGER NOT NULL DEFAULT 10, "
    "  genesis_class_weight_O INTEGER NOT NULL DEFAULT 10, "
    "  genesis_class_weight_L INTEGER NOT NULL DEFAULT 10, "
    "  genesis_class_weight_C INTEGER NOT NULL DEFAULT 10, "
    "  genesis_class_weight_H INTEGER NOT NULL DEFAULT 10, "
    "  genesis_class_weight_U INTEGER NOT NULL DEFAULT 5, "
    "  shipyard_enabled INTEGER NOT NULL DEFAULT 1, "
    "  shipyard_trade_in_factor_bp INTEGER NOT NULL DEFAULT 5000, "
    "  shipyard_require_cargo_fit INTEGER NOT NULL DEFAULT 1, "
    "  shipyard_require_fighters_fit INTEGER NOT NULL DEFAULT 1, "
    "  shipyard_require_shields_fit INTEGER NOT NULL DEFAULT 1, "
    "  shipyard_require_hardware_compat INTEGER NOT NULL DEFAULT 1, "
    "  shipyard_tax_bp INTEGER NOT NULL DEFAULT 1000 " " ); ",

  " CREATE TABLE IF NOT EXISTS trade_idempotency ( "
    " key          TEXT PRIMARY KEY, "
    " player_id    INTEGER NOT NULL, "
    " sector_id    INTEGER NOT NULL, "
    " request_json TEXT NOT NULL, "
    " response_json TEXT NOT NULL, " " created_at   INTEGER NOT NULL ); ",

  " CREATE TABLE IF NOT EXISTS used_sectors (used INTEGER); ",

  " CREATE TABLE IF NOT EXISTS npc_shipnames (id INTEGER, name TEXT); ",

  " CREATE TABLE IF NOT EXISTS tavern_names ( "
    "   id        INTEGER PRIMARY KEY AUTOINCREMENT, "
    "   name      TEXT NOT NULL UNIQUE, "
    "   enabled   INTEGER NOT NULL DEFAULT 1, "
    "   weight    INTEGER NOT NULL DEFAULT 1 " " ); ",

  " CREATE TABLE IF NOT EXISTS taverns ( "
    "   sector_id   INTEGER PRIMARY KEY REFERENCES sectors(id), "
    "   name_id     INTEGER NOT NULL REFERENCES tavern_names(id), "
    "   enabled     INTEGER NOT NULL DEFAULT 1 " " ); ",

  " CREATE TABLE IF NOT EXISTS tavern_settings ( "
    "   id                          INTEGER PRIMARY KEY CHECK (id = 1), "
    "   max_bet_per_transaction     INTEGER NOT NULL DEFAULT 5000, "
    "   daily_max_wager             INTEGER NOT NULL DEFAULT 50000, "
    "   enable_dynamic_wager_limit  INTEGER NOT NULL DEFAULT 0, "
    "   graffiti_max_posts          INTEGER NOT NULL DEFAULT 100, "
    "   notice_expires_days         INTEGER NOT NULL DEFAULT 7, "
    "   buy_round_cost              INTEGER NOT NULL DEFAULT 1000, "
    "   buy_round_alignment_gain    INTEGER NOT NULL DEFAULT 5, "
    "   loan_shark_enabled          INTEGER NOT NULL DEFAULT 1 " " ); ",

  " CREATE TABLE IF NOT EXISTS tavern_lottery_state ( "
    "   draw_date      TEXT PRIMARY KEY, "
    "   winning_number INTEGER, "
    "   jackpot        INTEGER NOT NULL, "
    "   carried_over   INTEGER NOT NULL DEFAULT 0 " " ); ",
  " CREATE TABLE IF NOT EXISTS tavern_lottery_tickets ( "
    "   id             INTEGER PRIMARY KEY, "
    "   draw_date      TEXT NOT NULL, "
    "   player_id      INTEGER NOT NULL REFERENCES players(id), "
    "   number         INTEGER NOT NULL, "
    "   cost           INTEGER NOT NULL, "
    "   purchased_at   INTEGER NOT NULL " " ); ",

  " CREATE TABLE IF NOT EXISTS tavern_deadpool_bets ( "
    "   id             INTEGER PRIMARY KEY, "
    "   bettor_id      INTEGER NOT NULL REFERENCES players(id), "
    "   target_id      INTEGER NOT NULL REFERENCES players(id), "
    "   amount         INTEGER NOT NULL, "
    "   odds_bp        INTEGER NOT NULL, "
    "   placed_at      INTEGER NOT NULL, "
    "   expires_at     INTEGER NOT NULL, "
    "   resolved       INTEGER NOT NULL DEFAULT 0, "
    "   resolved_at    INTEGER, " "   result         TEXT " " ); ",

  " CREATE TABLE IF NOT EXISTS tavern_raffle_state ( "
    "   id             INTEGER PRIMARY KEY CHECK (id = 1), "
    "   pot            INTEGER NOT NULL, "
    "   last_winner_id INTEGER, "
    "   last_payout    INTEGER, " "   last_win_ts    INTEGER " " ); ",

  " CREATE TABLE IF NOT EXISTS tavern_graffiti ( "
    "   id          INTEGER PRIMARY KEY, "
    "   player_id   INTEGER NOT NULL REFERENCES players(id), "
    "   text        TEXT NOT NULL, "
    "   created_at  INTEGER NOT NULL " " ); ",

  " CREATE TABLE IF NOT EXISTS tavern_notices ( "
    "   id          INTEGER PRIMARY KEY, "
    "   author_id   INTEGER NOT NULL REFERENCES players(id), "
    "   corp_id     INTEGER, "
    "   text        TEXT NOT NULL, "
    "   created_at  INTEGER NOT NULL, "
    "   expires_at  INTEGER NOT NULL " " ); ",

  " CREATE TABLE IF NOT EXISTS corp_recruiting ( "
    "   corp_id       INTEGER PRIMARY KEY REFERENCES corporations(id), "
    "   tagline       TEXT NOT NULL, "
    "   min_alignment INTEGER, "
    "   play_style    TEXT, "
    "   created_at    INTEGER NOT NULL, "
    "   expires_at    INTEGER NOT NULL " " ); ",

  " CREATE TABLE IF NOT EXISTS corp_invites ( " "   id INTEGER PRIMARY KEY AUTOINCREMENT, " "   corp_id INTEGER NOT NULL, " "   player_id INTEGER NOT NULL, " "   invited_at INTEGER NOT NULL, "	/* unix epoch seconds */
    "   expires_at INTEGER NOT NULL, "	/* unix epoch seconds */
    "   UNIQUE(corp_id, player_id), "
    "   FOREIGN KEY(corp_id) REFERENCES corporations(id) ON DELETE CASCADE, "
    "   FOREIGN KEY(player_id) REFERENCES players(id) ON DELETE CASCADE "
    " ); ",

  " CREATE INDEX IF NOT EXISTS idx_corp_invites_corp ON corp_invites(corp_id); ",
  " CREATE INDEX IF NOT EXISTS idx_corp_invites_player ON corp_invites(player_id); ",



  " CREATE TABLE IF NOT EXISTS tavern_loans ( "
    "   player_id      INTEGER PRIMARY KEY REFERENCES players(id), "
    "   principal      INTEGER NOT NULL, "
    "   interest_rate  INTEGER NOT NULL, "
    "   due_date       INTEGER NOT NULL, "
    "   is_defaulted   INTEGER NOT NULL DEFAULT 0 " " ); ",



  " CREATE TABLE IF NOT EXISTS planettypes (id INTEGER PRIMARY KEY AUTOINCREMENT, code TEXT UNIQUE, typeDescription TEXT, typeName TEXT, citadelUpgradeTime_lvl1 INTEGER, citadelUpgradeTime_lvl2 INTEGER, citadelUpgradeTime_lvl3 INTEGER, citadelUpgradeTime_lvl4 INTEGER, citadelUpgradeTime_lvl5 INTEGER, citadelUpgradeTime_lvl6 INTEGER, citadelUpgradeOre_lvl1 INTEGER, citadelUpgradeOre_lvl2 INTEGER, citadelUpgradeOre_lvl3 INTEGER, citadelUpgradeOre_lvl4 INTEGER, citadelUpgradeOre_lvl5 INTEGER, citadelUpgradeOre_lvl6 INTEGER, citadelUpgradeOrganics_lvl1 INTEGER, citadelUpgradeOrganics_lvl2 INTEGER, citadelUpgradeOrganics_lvl3 INTEGER, citadelUpgradeOrganics_lvl4 INTEGER, citadelUpgradeOrganics_lvl5 INTEGER, citadelUpgradeOrganics_lvl6 INTEGER, citadelUpgradeEquipment_lvl1 INTEGER, citadelUpgradeEquipment_lvl2 INTEGER, citadelUpgradeEquipment_lvl3 INTEGER, citadelUpgradeEquipment_lvl4 INTEGER, citadelUpgradeEquipment_lvl5 INTEGER, citadelUpgradeEquipment_lvl6 INTEGER, citadelUpgradeColonist_lvl1 INTEGER, citadelUpgradeColonist_lvl2 INTEGER, citadelUpgradeColonist_lvl3 INTEGER, citadelUpgradeColonist_lvl4 INTEGER, citadelUpgradeColonist_lvl5 INTEGER, citadelUpgradeColonist_lvl6 INTEGER, maxColonist_ore INTEGER, maxColonist_organics INTEGER, maxColonist_equipment INTEGER, fighters INTEGER, fuelProduction INTEGER, organicsProduction INTEGER, equipmentProduction INTEGER, fighterProduction INTEGER, maxore INTEGER, maxorganics INTEGER, maxequipment INTEGER, maxfighters INTEGER, breeding REAL, genesis_weight INTEGER NOT NULL DEFAULT 10); ",

    " CREATE TABLE IF NOT EXISTS ports ( " " id INTEGER PRIMARY KEY AUTOINCREMENT,  " " number INTEGER, " " name TEXT NOT NULL, " " sector INTEGER NOT NULL, "	/* FK to sectors.id */

      " size INTEGER, "

      " techlevel INTEGER, "

      " ore_on_hand INTEGER NOT NULL DEFAULT 0, "

      " organics_on_hand INTEGER NOT NULL DEFAULT 0, "

      " equipment_on_hand INTEGER NOT NULL DEFAULT 0, "

      " slaves_on_hand INTEGER NOT NULL DEFAULT 0, "

      " weapons_on_hand INTEGER NOT NULL DEFAULT 0, "

      " drugs_on_hand INTEGER NOT NULL DEFAULT 0, "

      " petty_cash INTEGER NOT NULL DEFAULT 0, "

      " invisible INTEGER DEFAULT 0, "

      " type INTEGER DEFAULT 1, "
    " FOREIGN KEY (sector) REFERENCES sectors(id)); ",


  " CREATE TABLE IF NOT EXISTS port_trade ( "
    " id INTEGER PRIMARY KEY AUTOINCREMENT,  "
    " port_id INTEGER NOT NULL,  "
    " maxproduct INTEGER,  "
    " commodity TEXT CHECK(commodity IN ('ore','organics','equipment')),  "
    " mode TEXT CHECK(mode IN ('buy','sell')),  "
    " FOREIGN KEY (port_id) REFERENCES ports(id)); ",


  " CREATE TABLE IF NOT EXISTS players ( "
    " id INTEGER PRIMARY KEY AUTOINCREMENT,  "
    " type INTEGER DEFAULT 2,  "
    " number INTEGER,  "
    " name TEXT NOT NULL,  "
    " passwd TEXT NOT NULL,  "
    " sector INTEGER,  "
    " ship INTEGER,  "
    " experience INTEGER,  "
    " alignment INTEGER,  "
    " commission INTEGER DEFAULT 0,  "
    " credits INTEGER,  "
    " flags INTEGER,  "
    " login_time INTEGER,  "
    " last_update INTEGER,  "
    " intransit INTEGER,  "
    " beginmove INTEGER,  "
    " movingto INTEGER,  "
    " loggedin INTEGER,  "
    " lastplanet INTEGER,  "
    " score INTEGER,  " " last_news_read_timestamp INTEGER DEFAULT 0);  ",


  " CREATE TABLE IF NOT EXISTS player_types (type INTEGER PRIMARY KEY AUTOINCREMENT, description TEXT); ",

  " CREATE TABLE IF NOT EXISTS sectors (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, beacon TEXT, nebulae TEXT); ",

  " CREATE TABLE IF NOT EXISTS sector_warps (from_sector INTEGER, to_sector INTEGER, PRIMARY KEY (from_sector, to_sector), FOREIGN KEY (from_sector) REFERENCES sectors(id) ON DELETE CASCADE, FOREIGN KEY (to_sector) REFERENCES sectors(id) ON DELETE CASCADE); ",


  " CREATE TABLE IF NOT EXISTS shiptypes (  "
    "   id INTEGER PRIMARY KEY AUTOINCREMENT,  "
    "   name TEXT NOT NULL UNIQUE,  "
    "   basecost INTEGER,  "
    "   required_alignment INTEGER,  "
    "   required_commission INTEGER,  "
    "   required_experience INTEGER,  "
    "   maxattack INTEGER,  "
    "   initialholds INTEGER,  "
    "   maxholds INTEGER,  "
    "   maxfighters INTEGER,  "
    "   turns INTEGER,  "
    "   maxmines INTEGER,  "
    "   maxlimpets INTEGER,  "
    "   maxgenesis INTEGER,  "
    "   max_detonators INTEGER NOT NULL DEFAULT 0, "
    "   max_probes INTEGER NOT NULL DEFAULT 0, "
    "   can_transwarp INTEGER NOT NULL DEFAULT 0,  "
    "   transportrange INTEGER,  "
    "   maxshields INTEGER,  "
    "   offense INTEGER,  "
    "   defense INTEGER,  "
    "   maxbeacons INTEGER,  "
    "   can_long_range_scan INTEGER NOT NULL DEFAULT 0,  "
    "   can_planet_scan INTEGER NOT NULL DEFAULT 0,  "
    "   maxphotons INTEGER, /* Photon torpedo count */  "
    "   max_cloaks INTEGER NOT NULL DEFAULT 0, "
    "   can_purchase INTEGER, /* Can be bought at a port (0/1) */  "
    "   enabled INTEGER NOT NULL DEFAULT 1 " " );  ",

  " CREATE TABLE IF NOT EXISTS ships (  "
    "   id INTEGER PRIMARY KEY AUTOINCREMENT,  "
    "   name TEXT NOT NULL,  "
    "   type_id INTEGER, /* Foreign Key to shiptypes.id */  "
    "   attack INTEGER,  "
    "   holds INTEGER,  "
    "   mines INTEGER, /* Current quantity carried */  "
    "   limpets INTEGER, /* Current quantity carried */  "
    "   fighters INTEGER, /* Current quantity carried */  "
    "   genesis INTEGER, /* Current quantity carried */  "
    "   detonators INTEGER NOT NULL DEFAULT 0, "
    "   probes INTEGER NOT NULL DEFAULT 0, "
    "   photons INTEGER, /* Current quantity carried */  "
    "   sector INTEGER, /* Foreign Key to sectors.id */  "
    "   shields INTEGER,  "
    "   beacons INTEGER, /* Current quantity carried */  "
    "   colonists INTEGER,  "
    "   equipment INTEGER,  "
    "   organics INTEGER,  "
    "   ore INTEGER,  "
    "   slaves INTEGER DEFAULT 0,  "
    "   weapons INTEGER DEFAULT 0,  "
    "   drugs INTEGER DEFAULT 0,  "
    "   flags INTEGER,  "
    "   cloaking_devices INTEGER,  "
    "   has_transwarp INTEGER NOT NULL DEFAULT 0, "
    "   has_planet_scanner INTEGER NOT NULL DEFAULT 0, "
    "   has_long_range_scanner INTEGER NOT NULL DEFAULT 0, "
    "   cloaked TIMESTAMP,  "
    "   ported INTEGER,  "
    "   onplanet INTEGER,  "
    "   destroyed INTEGER DEFAULT 0,  "
    "   hull INTEGER NOT NULL DEFAULT 100, "
    "   perms INTEGER NOT NULL DEFAULT 731, "
    "   CONSTRAINT check_current_cargo_limit CHECK ( (colonists + equipment + organics + ore) <= holds ), "
    "   FOREIGN KEY(type_id) REFERENCES shiptypes(id),  "
    "   FOREIGN KEY(sector) REFERENCES sectors(id)  " " );  ",



  " CREATE TABLE IF NOT EXISTS ship_markers ( "
    " ship_id        INTEGER NOT NULL REFERENCES ships(id), "
    " owner_player   INTEGER NOT NULL, "
    " owner_corp     INTEGER NOT NULL DEFAULT 0, "
    " marker_type    TEXT NOT NULL, "
    " PRIMARY KEY (ship_id, owner_player, marker_type) " " ); ",



  " CREATE TABLE IF NOT EXISTS player_ships ( player_id INTEGER DEFAULT 0, ship_id INTEGER DEFAULT 0, role INTEGER DEFAULT 1, is_active INTEGER DEFAULT 1); ",
  " CREATE TABLE IF NOT EXISTS ship_roles ( role_id INTEGER PRIMARY KEY, role INTEGER DEFAULT 1, role_description TEXT DEFAULT 1); ",


  " CREATE TABLE IF NOT EXISTS ship_ownership ( "
    " ship_id     INTEGER NOT NULL, "
    " player_id   INTEGER NOT NULL, "
    " role_id     INTEGER NOT NULL, "
    " is_primary  INTEGER NOT NULL DEFAULT 0, "
    " acquired_at INTEGER NOT NULL DEFAULT (strftime('%s','now')), "
    " PRIMARY KEY (ship_id, player_id, role_id), "
    " FOREIGN KEY(ship_id)  REFERENCES ships(id), "
    " FOREIGN KEY(player_id) REFERENCES players(id)); ",



  " CREATE TABLE IF NOT EXISTS planets ( " " id INTEGER PRIMARY KEY AUTOINCREMENT,  " " num INTEGER,  "	/* legacy planet ID */
    " sector INTEGER NOT NULL,  "	/* FK to sectors.id */
    " name TEXT NOT NULL,  " " owner_id INTEGER NOT NULL, "	/* Generic owner ID */
    " owner_type TEXT NOT NULL DEFAULT 'player', "	/* 'player' or 'corporation' */
    " class TEXT NOT NULL DEFAULT 'M', "	/* Canonical class string */
    " population INTEGER,  " " type INTEGER,  "	/* FK to planettypes.id */
    " creator TEXT,  "
    " colonist INTEGER,  "
    " fighters INTEGER,  "
    " created_at INTEGER NOT NULL, "
    " created_by INTEGER NOT NULL, "
    " genesis_flag INTEGER NOT NULL DEFAULT 1, "
    " citadel_level INTEGER DEFAULT 0,  "
    " ore_on_hand INTEGER NOT NULL DEFAULT 0, "
    " organics_on_hand INTEGER NOT NULL DEFAULT 0, "
    " equipment_on_hand INTEGER NOT NULL DEFAULT 0, "
    " FOREIGN KEY (sector) REFERENCES sectors(id),  "
    /* No direct FK for owner_id/owner_type due to polymorphic nature */
    " FOREIGN KEY (type) REFERENCES planettypes(id) " " ); ",
  " CREATE TRIGGER IF NOT EXISTS trg_planets_total_cap_before_insert "
    " BEFORE INSERT ON planets "
    " FOR EACH ROW "
    " BEGIN "
    "   SELECT CASE "
    "     WHEN (SELECT COUNT(*) FROM planets) >= (SELECT max_total_planets FROM config WHERE id = 1) "
    "     THEN RAISE(ABORT, 'ERR_UNIVERSE_FULL') "
    "     ELSE 1 " "   END; " " END; ",

  " CREATE TABLE IF NOT EXISTS citadel_requirements ( "
    "   planet_type_id INTEGER NOT NULL REFERENCES planettypes(id) ON DELETE CASCADE, "
    "   citadel_level INTEGER NOT NULL, "
    "   ore_cost INTEGER NOT NULL DEFAULT 0, "
    "   organics_cost INTEGER NOT NULL DEFAULT 0, "
    "   equipment_cost INTEGER NOT NULL DEFAULT 0, "
    "   colonist_cost INTEGER NOT NULL DEFAULT 0, "
    "   time_cost_days INTEGER NOT NULL DEFAULT 0, "
    "   PRIMARY KEY (planet_type_id, citadel_level) " " ); ",
  " CREATE TABLE IF NOT EXISTS planet_goods ( "
    "    planet_id INTEGER NOT NULL, "
    "    commodity TEXT NOT NULL CHECK(commodity IN ('ore', 'organics', 'equipment')), "
    "    quantity INTEGER NOT NULL DEFAULT 0, "
    "    max_capacity INTEGER NOT NULL, "
    "    production_rate INTEGER NOT NULL, "
    "    PRIMARY KEY (planet_id, commodity), "
    "    FOREIGN KEY (planet_id) REFERENCES planets(id) " " ); ",

  " CREATE TABLE IF NOT EXISTS hardware_items ( "
    " id INTEGER PRIMARY KEY, "
    " code TEXT UNIQUE NOT NULL, "
    " name TEXT NOT NULL, "
    " price INTEGER NOT NULL, "
    " requires_stardock INTEGER NOT NULL DEFAULT 1, "
    " sold_in_class0 INTEGER NOT NULL DEFAULT 0, "
    " max_per_ship INTEGER, "
    " category TEXT NOT NULL, " " enabled INTEGER NOT NULL DEFAULT 1 " " ); ",

  /* --- citadels table (fixed, closed properly) --- */
  " CREATE TABLE IF NOT EXISTS citadels ( " " id INTEGER PRIMARY KEY AUTOINCREMENT,  " " planet_id INTEGER UNIQUE NOT NULL,  "	/* 1:1 link to planets.id */
    " level INTEGER,  " " treasury INTEGER,  " " militaryReactionLevel INTEGER,  " " qCannonAtmosphere INTEGER,  " " qCannonSector INTEGER,  " " planetaryShields INTEGER,  " " transporterlvl INTEGER,  " " interdictor INTEGER,  " " upgradePercent REAL,  " " upgradestart INTEGER,  " " owner INTEGER,  "	/* FK to players.id */
    " shields INTEGER,  "
    " torps INTEGER,  "
    " fighters INTEGER,  "
    " qtorps INTEGER,  "
    " qcannon INTEGER,  "
    " qcannontype INTEGER,  "
    " qtorpstype INTEGER,  "
    " military INTEGER,  "
    " construction_start_time INTEGER DEFAULT 0,  "
    " construction_end_time INTEGER DEFAULT 0,  "
    " target_level INTEGER DEFAULT 0,  "
    " construction_status TEXT DEFAULT 'idle',  "
    " FOREIGN KEY (planet_id) REFERENCES planets(id) ON DELETE CASCADE,  "
    " FOREIGN KEY (owner) REFERENCES players(id) " " ); ",

  " CREATE TABLE IF NOT EXISTS sessions ( " "   token       TEXT PRIMARY KEY, "	/* 64-hex opaque */
    "   player_id   INTEGER NOT NULL, " "   expires     INTEGER NOT NULL, "	/* epoch seconds (UTC) */
    "   created_at  INTEGER NOT NULL "	/* epoch seconds */
    " ); "
    " CREATE INDEX IF NOT EXISTS idx_sessions_player ON sessions(player_id); "
    " CREATE INDEX IF NOT EXISTS idx_sessions_expires ON sessions(expires); ",

  " CREATE TABLE IF NOT EXISTS turns( "
    "   player INTEGER NOT NULL, "
    "   turns_remaining INTEGER NOT NULL, "
    "   last_update TIMESTAMP NOT NULL, "
    "   PRIMARY KEY (player), "
    "   FOREIGN KEY (player) REFERENCES players(id) ON DELETE CASCADE ); ",

  " CREATE TABLE IF NOT EXISTS mail ( "
    "   id INTEGER PRIMARY KEY AUTOINCREMENT,  "
    "   thread_id INTEGER,  "
    "   sender_id INTEGER NOT NULL,  "
    "   recipient_id INTEGER NOT NULL,  "
    "   subject TEXT,  "
    "   body TEXT NOT NULL,  "
    "   sent_at DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),  "
    "   read_at DATETIME,  "
    "   archived INTEGER NOT NULL DEFAULT 0,  "
    "   deleted INTEGER NOT NULL DEFAULT 0,  "
    "   idempotency_key TEXT,  "
    "   FOREIGN KEY(sender_id) REFERENCES players(id) ON DELETE CASCADE,  "
    "   FOREIGN KEY(recipient_id) REFERENCES players(id) ON DELETE CASCADE "
    " ); ",

  /* SUBSPACE (global chat + cursor) */
  " CREATE TABLE IF NOT EXISTS subspace ( "
    "   id INTEGER PRIMARY KEY AUTOINCREMENT,  "
    "   sender_id INTEGER,  "
    "   message TEXT NOT NULL,  "
    "   kind TEXT NOT NULL DEFAULT 'chat',  "
    "   posted_at DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),  "
    "   FOREIGN KEY(sender_id) REFERENCES players(id) ON DELETE SET NULL "
    " ); ",

  " CREATE TABLE IF NOT EXISTS subspace_cursors ( "
    "   player_id INTEGER PRIMARY KEY,  "
    "   last_seen_id INTEGER NOT NULL DEFAULT 0,  "
    "   FOREIGN KEY(player_id) REFERENCES players(id) ON DELETE CASCADE "
    " ); ",

  /* CORPORATIONS + MEMBERS */
  " CREATE TABLE IF NOT EXISTS corporations ( "
    "   id INTEGER PRIMARY KEY,  "
    "   name TEXT NOT NULL COLLATE NOCASE,  "
    "   owner_id INTEGER,  "
    "   tag TEXT COLLATE NOCASE,  "
    "   description TEXT,  "
    "   tax_arrears INTEGER NOT NULL DEFAULT 0, "
    "   credit_rating INTEGER NOT NULL DEFAULT 0, "
    "   created_at DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),  "
    "   updated_at DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),  "
    "   FOREIGN KEY(owner_id) REFERENCES players(id) ON DELETE SET NULL ON UPDATE CASCADE,  "
    "   CHECK (tag IS NULL OR (length(tag) BETWEEN 2 AND 5 AND tag GLOB '[A-Za-z0-9]*')) "
    " ); ",


  " CREATE TABLE IF NOT EXISTS corp_members ( "
    "   corp_id INTEGER NOT NULL,  "
    "   player_id INTEGER NOT NULL,  "
    "   role TEXT NOT NULL DEFAULT 'Member',  "
    "   join_date DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),  "
    "   PRIMARY KEY (corp_id, player_id),  "
    "   FOREIGN KEY(corp_id) REFERENCES corporations(id) ON DELETE CASCADE ON UPDATE CASCADE,  "
    "   FOREIGN KEY(player_id) REFERENCES players(id) ON DELETE CASCADE ON UPDATE CASCADE,  "
    "   CHECK (role IN ('Leader','Officer','Member')) " " ); ",


  /* CORP MAIL + LOGS */
  " CREATE TABLE IF NOT EXISTS corp_mail ( "
    "   id INTEGER PRIMARY KEY AUTOINCREMENT,  "
    "   corp_id INTEGER NOT NULL,  "
    "   sender_id INTEGER,  "
    "   subject TEXT,  "
    "   body TEXT NOT NULL,  "
    "   posted_at DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),  "
    "   FOREIGN KEY(corp_id) REFERENCES corporations(id) ON DELETE CASCADE,  "
    "   FOREIGN KEY(sender_id) REFERENCES players(id) ON DELETE SET NULL "
    " ); ",


  " CREATE TABLE IF NOT EXISTS corp_mail_cursors ( "
    "   corp_id INTEGER NOT NULL,  "
    "   player_id INTEGER NOT NULL,  "
    "   last_seen_id INTEGER NOT NULL DEFAULT 0,  "
    "   PRIMARY KEY (corp_id, player_id),  "
    "   FOREIGN KEY(corp_id) REFERENCES corporations(id) ON DELETE CASCADE,  "
    "   FOREIGN KEY(player_id) REFERENCES players(id) ON DELETE CASCADE "
    " ); ",

  " CREATE TABLE IF NOT EXISTS corp_log ( "
    "   id INTEGER PRIMARY KEY AUTOINCREMENT,  "
    "   corp_id INTEGER NOT NULL,  "
    "   actor_id INTEGER,  "
    "   event_type TEXT NOT NULL,  "
    "   payload TEXT NOT NULL,  "
    "   created_at DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),  "
    "   FOREIGN KEY(corp_id) REFERENCES corporations(id) ON DELETE CASCADE,  "
    "   FOREIGN KEY(actor_id) REFERENCES players(id) ON DELETE SET NULL "
    " ); ",


  /* SYSTEM EVENTS + SUBSCRIPTIONS */
  " CREATE TABLE IF NOT EXISTS system_events ( "
    "   id INTEGER PRIMARY KEY AUTOINCREMENT,  "
    "   scope TEXT NOT NULL,  "
    "   event_type TEXT NOT NULL,  "
    "   payload TEXT NOT NULL,  "
    "   created_at DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')) "
    " ); ",


  " CREATE TABLE IF NOT EXISTS subscriptions ( "
    "   id INTEGER PRIMARY KEY AUTOINCREMENT,  "
    "   player_id INTEGER NOT NULL,  "
    "   event_type TEXT NOT NULL,  "
    "   delivery TEXT NOT NULL,  "
    "   filter_json TEXT,  "
    "   ephemeral INTEGER NOT NULL DEFAULT 0,  "
    "   locked INTEGER NOT NULL DEFAULT 0, "
    "   enabled INTEGER NOT NULL DEFAULT 1,  "
    "   UNIQUE(player_id, event_type),  "
    "   FOREIGN KEY(player_id) REFERENCES players(id) ON DELETE CASCADE "
    " ); ",

  "CREATE TABLE IF NOT EXISTS player_block ("
    "  blocker_id   INTEGER NOT NULL,"
    "  blocked_id   INTEGER NOT NULL,"
    "  created_at   INTEGER NOT NULL,"
    "  PRIMARY KEY (blocker_id, blocked_id)" ");",


  "CREATE TABLE IF NOT EXISTS notice_seen ("
    "  notice_id  INTEGER NOT NULL,"
    "  player_id  INTEGER NOT NULL,"
    "  seen_at    INTEGER NOT NULL,"
    "  PRIMARY KEY (notice_id, player_id)" ");",


  "CREATE TABLE IF NOT EXISTS system_notice ("
    "  id         INTEGER PRIMARY KEY,"
    "  created_at INTEGER NOT NULL,"
    "  title      TEXT NOT NULL,"
    "  body       TEXT NOT NULL,"
    "  severity   TEXT NOT NULL CHECK(severity IN ('info','warn','error')),"
    "  expires_at INTEGER" ");",

  "CREATE TABLE  IF NOT EXISTS player_prefs   (  "
    "  player_id  INTEGER NOT NULL  ,  "
    "  key        TEXT    NOT NULL,       "
    "  type       TEXT    NOT NULL CHECK (type IN ('bool','int','string','json'))  ,  "
    "  value      TEXT    NOT NULL,       "
    "  updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))  ,  "
    "  PRIMARY KEY (player_id, key)  ,  "
    "  FOREIGN KEY (player_id) REFERENCES players(id) ON DELETE CASCADE )  ;  ",

  "CREATE TABLE IF NOT EXISTS player_bookmarks   (  "
    "  id         INTEGER PRIMARY KEY AUTOINCREMENT  ,  "
    "  player_id  INTEGER NOT NULL  ,  "
    "  name       TEXT    NOT NULL,      "
    "  sector_id  INTEGER NOT NULL  ,  "
    "  updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))  ,  "
    "  UNIQUE(player_id, name)  ,  "
    "  FOREIGN KEY (player_id) REFERENCES players(id) ON DELETE CASCADE  ,  "
    "  FOREIGN KEY (sector_id) REFERENCES sectors(id) ON DELETE CASCADE )  ;  ",

  "CREATE TABLE  IF NOT EXISTS player_avoid  (  "
    "  player_id  INTEGER NOT NULL  ,  "
    "  sector_id  INTEGER NOT NULL  ,  "
    "  updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))  ,  "
    "  PRIMARY KEY (player_id, sector_id)  ,  "
    "  FOREIGN KEY (player_id) REFERENCES players(id) ON DELETE CASCADE  ,  "
    "  FOREIGN KEY (sector_id) REFERENCES sectors(id) ON DELETE CASCADE )  ;  ",

  "CREATE TABLE IF NOT EXISTS player_notes   (  "
    "  id         INTEGER PRIMARY KEY AUTOINCREMENT  ,  "
    "  player_id  INTEGER NOT NULL  ,  "
    "  scope      TEXT    NOT NULL,    "
    "  key        TEXT    NOT NULL,    "
    "  note       TEXT    NOT NULL  ,  "
    "  updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))  ,  "
    "  UNIQUE(player_id, scope, key)  ,  "
    "  FOREIGN KEY (player_id) REFERENCES players(id) ON DELETE CASCADE )  ;  ",

  "CREATE TABLE IF NOT EXISTS sector_assets  ( "
    "    id INTEGER PRIMARY KEY,  "
    "    sector INTEGER NOT NULL REFERENCES sectors(id), "
    "    player INTEGER REFERENCES players(id),  "
    "    corporation INTEGER NOT NULL DEFAULT 0,  "
    "    asset_type INTEGER NOT NULL,  "
    "    offensive_setting INTEGER DEFAULT 0,  "
    "    quantity INTEGER, "
    "    ttl INTEGER,  " "    deployed_at INTEGER NOT NULL  " "); ",

  "CREATE TABLE IF NOT EXISTS msl_sectors ("
    "  sector_id INTEGER PRIMARY KEY REFERENCES sectors(id)" ");",

  "CREATE TABLE IF NOT EXISTS trade_log ("
    "    id            INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    player_id     INTEGER NOT NULL,"
    "    port_id       INTEGER NOT NULL,"
    "    sector_id     INTEGER NOT NULL,"
    "    commodity     TEXT NOT NULL,"
    "    units         INTEGER NOT NULL,"
    "    price_per_unit REAL NOT NULL,"
    "    action        TEXT CHECK(action IN ('buy', 'sell')) NOT NULL,"
    "    timestamp     INTEGER NOT NULL,"
    "    FOREIGN KEY (player_id) REFERENCES players(id),"
    "    FOREIGN KEY (port_id) REFERENCES ports(id),"
    "    FOREIGN KEY (sector_id) REFERENCES sectors(id)" ");",

  "CREATE INDEX IF NOT EXISTS ix_trade_log_ts ON trade_log(timestamp);",



  "CREATE TABLE IF NOT EXISTS stardock_assets ("
    "    sector_id      INTEGER PRIMARY KEY,"
    "    owner_id       INTEGER NOT NULL,"
    "    fighters       INTEGER NOT NULL DEFAULT 0,"
    "    defenses       INTEGER NOT NULL DEFAULT 0,"
    "    ship_capacity  INTEGER NOT NULL DEFAULT 1,"
    "    created_at     INTEGER NOT NULL,"
    "    FOREIGN KEY (sector_id) REFERENCES sectors(id),"
    "    FOREIGN KEY (owner_id) REFERENCES players(id)" ");",

  "CREATE INDEX IF NOT EXISTS ix_stardock_owner ON stardock_assets(owner_id);",

  "CREATE TABLE IF NOT EXISTS shipyard_inventory ("
    "    port_id         INTEGER NOT NULL REFERENCES ports(id),"
    "    ship_type_id    INTEGER NOT NULL REFERENCES shiptypes(id),"
    "    enabled         INTEGER NOT NULL DEFAULT 1,"
    "    PRIMARY KEY (port_id, ship_type_id)" ");",

  "CREATE TABLE IF NOT EXISTS shipyard_inventory ("
    "    port_id         INTEGER NOT NULL REFERENCES ports(id),"
    "    ship_type_id    INTEGER NOT NULL REFERENCES shiptypes(id),"
    "    enabled         INTEGER NOT NULL DEFAULT 1,"
    "    PRIMARY KEY (port_id, ship_type_id)" ");",



  " CREATE TABLE IF NOT EXISTS podded_status ( "
    "   player_id INTEGER PRIMARY KEY REFERENCES players(id), "
    "   status TEXT NOT NULL DEFAULT 'active', "
    "   big_sleep_until INTEGER, "
    "   reason TEXT, "
    "   podded_count_today INTEGER NOT NULL DEFAULT 0, "
    "   podded_last_reset INTEGER " " ); ",



  "CREATE TABLE IF NOT EXISTS planet_goods ("
    "    planet_id      INTEGER NOT NULL,"
    "    commodity      TEXT NOT NULL CHECK(commodity IN ('ore', 'organics', 'equipment', 'food', 'fuel')),"
    "    quantity       INTEGER NOT NULL DEFAULT 0,"
    "    max_capacity   INTEGER NOT NULL,"
    "    production_rate INTEGER NOT NULL,"
    "    PRIMARY KEY (planet_id, commodity),"
    "    FOREIGN KEY (planet_id) REFERENCES planets(id)" ");",

  /* --- Clusters --- */
  "CREATE TABLE IF NOT EXISTS clusters ("
    "    id           INTEGER PRIMARY KEY,"
    "    name         TEXT NOT NULL,"
    "    role         TEXT NOT NULL,"
    "    kind         TEXT NOT NULL,"
    "    center_sector INTEGER,"
    "    law_severity INTEGER NOT NULL DEFAULT 1,"
    "    alignment    INTEGER NOT NULL DEFAULT 0,"
    "    created_at   TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
    ");",

  "CREATE TABLE IF NOT EXISTS cluster_sectors ("
    "    cluster_id   INTEGER NOT NULL,"
    "    sector_id    INTEGER NOT NULL,"
    "    PRIMARY KEY (cluster_id, sector_id),"
    "    FOREIGN KEY (cluster_id) REFERENCES clusters(id),"
    "    FOREIGN KEY (sector_id)  REFERENCES sectors(id)"
    ");",
  "CREATE INDEX IF NOT EXISTS idx_cluster_sectors_sector ON cluster_sectors(sector_id);",

  "CREATE TABLE IF NOT EXISTS cluster_commodity_index ("
    "    cluster_id     INTEGER NOT NULL,"
    "    commodity_code TEXT    NOT NULL,"
    "    mid_price      INTEGER NOT NULL,"
    "    last_updated   TEXT    NOT NULL DEFAULT CURRENT_TIMESTAMP,"
    "    PRIMARY KEY (cluster_id, commodity_code),"
    "    FOREIGN KEY (cluster_id) REFERENCES clusters(id)"
    ");",

  "CREATE TABLE IF NOT EXISTS cluster_player_status ("
    "    cluster_id   INTEGER NOT NULL,"
    "    player_id    INTEGER NOT NULL,"
    "    suspicion    INTEGER NOT NULL DEFAULT 0,"
    "    bust_count   INTEGER NOT NULL DEFAULT 0,"
    "    last_bust_at TEXT,"
    "    wanted_level INTEGER NOT NULL DEFAULT 0,"
    "    banned       INTEGER NOT NULL DEFAULT 0,"
    "    PRIMARY KEY (cluster_id, player_id),"
    "    FOREIGN KEY (cluster_id) REFERENCES clusters(id),"
    "    FOREIGN KEY (player_id)  REFERENCES players(id)"
    ");",


  //////////////////////////////////////////////////////////////////////
  /// CREATE VIEWS 
  //////////////////////////////////////////////////////////////////////
  /* --- longest_tunnels view (array item ends with a comma, not semicolon) --- */
  " CREATE VIEW IF NOT EXISTS longest_tunnels AS\n "
    " WITH\n "
    " all_sectors AS (\n "
    "   SELECT from_sector AS id FROM sector_warps\n "
    "   UNION\n "
    "   SELECT to_sector   AS id FROM sector_warps\n "
    " ),\n "
    " outdeg AS (\n "
    "   SELECT a.id, COALESCE(COUNT(w.to_sector),0) AS deg\n "
    "   FROM all_sectors a\n "
    "   LEFT JOIN sector_warps w ON w.from_sector = a.id\n "
    "   GROUP BY a.id\n "
    " ),\n "
    " edges AS (\n "
    "   SELECT from_sector, to_sector FROM sector_warps\n "
    " ),\n "
    " entry AS (\n "
    "   SELECT e.from_sector AS entry, e.to_sector AS next\n "
    "  FROM edges e\n"
    "  JOIN outdeg df ON df.id = e.from_sector AND df.deg > 1\n"
    "  JOIN outdeg dn ON dn.id = e.to_sector  AND dn.deg = 1\n"
    "),\n"
    "rec(entry, curr, path, steps) AS (\n"
    "  SELECT entry, next, printf('%d->%d', entry, next), 1\n"
    "  FROM entry\n"
    "  UNION ALL\n"
    "  SELECT r.entry, e.to_sector,\n"
    "         r.path || '->' || printf('%d', e.to_sector),\n"
    "         r.steps + 1\n"
    "  FROM rec r\n"
    "  JOIN edges e  ON e.from_sector = r.curr\n"
    "  JOIN outdeg d ON d.id = r.curr AND d.deg = 1\n"
    "  WHERE instr(r.path, '->' || printf('%d', e.to_sector) || '->') = 0\n"
    ")\n"
    "SELECT\n"
    "  r.entry                 AS entry_sector,\n"
    "  r.curr                  AS exit_sector,\n"
    "  r.path                  AS tunnel_path,\n"
    "  r.steps                 AS tunnel_length_edges\n"
    "FROM rec r\n"
    "JOIN outdeg d_exit ON d_exit.id = r.curr\n"
    "WHERE d_exit.deg <> 1 AND r.steps >= 2\n"
    "ORDER BY r.steps DESC, r.entry, r.curr;"
    /* ===================== GRAPH / TOPOLOGY ===================== */
/* 1) Degrees per sector (base for several views) */
    "CREATE VIEW IF NOT EXISTS sector_degrees AS\n"
    "WITH outdeg AS (\n"
    "  SELECT s.id, COUNT(w.to_sector) AS outdeg\n"
    "  FROM sectors s\n"
    "  LEFT JOIN sector_warps w ON w.from_sector = s.id\n"
    "  GROUP BY s.id\n"
    "), indeg AS (\n"
    "  SELECT s.id, COUNT(w.from_sector) AS indeg\n"
    "  FROM sectors s\n"
    "  LEFT JOIN sector_warps w ON w.to_sector = s.id\n"
    "  GROUP BY s.id\n"
    ")\n"
    "SELECT o.id AS sector_id, o.outdeg, i.indeg\n"
    "FROM outdeg o JOIN indeg i USING(id);",

/* 2) Dead-out (no outgoing) */
  "CREATE VIEW IF NOT EXISTS sectors_dead_out AS\n"
    "SELECT sector_id FROM sector_degrees WHERE outdeg = 0;",

/* 3) Dead-in (no incoming) */
  "CREATE VIEW IF NOT EXISTS sectors_dead_in AS\n"
    "SELECT sector_id FROM sector_degrees WHERE indeg = 0;",

/* 4) Isolated (no in, no out) */
  "CREATE VIEW IF NOT EXISTS sectors_isolated AS\n"
    "SELECT sector_id FROM sector_degrees WHERE outdeg = 0 AND indeg = 0;",

/* 5) One-way edges (no reverse) */
  "CREATE VIEW IF NOT EXISTS one_way_edges AS\n"
    "SELECT s.from_sector, s.to_sector\n"
    "FROM sector_warps s\n"
    "LEFT JOIN sector_warps r\n"
    "  ON r.from_sector = s.to_sector AND r.to_sector = s.from_sector\n"
    "WHERE r.from_sector IS NULL;",

/* 6) Bidirectional edges (dedup pairs) */
  "CREATE VIEW IF NOT EXISTS bidirectional_edges AS\n"
    "SELECT s.from_sector, s.to_sector\n"
    "FROM sector_warps s\n"
    "JOIN sector_warps r\n"
    "  ON r.from_sector = s.to_sector AND r.to_sector = s.from_sector\n"
    "WHERE s.from_sector < s.to_sector;",

/* 7) Sector adjacency list (CSV) */
  "CREATE VIEW IF NOT EXISTS sector_adjacency AS\n"
    "SELECT s.id AS sector_id,\n"
    "       COALESCE(GROUP_CONCAT(w.to_sector, ','), '') AS neighbors\n"
    "FROM sectors s\n"
    "LEFT JOIN sector_warps w ON w.from_sector = s.id\n" "GROUP BY s.id;",

/* 8) Sector summary (depends on sector_degrees) */
  "CREATE VIEW IF NOT EXISTS sector_summary AS\n"
    "WITH pc AS (\n"
    "  SELECT sector AS sector_id, COUNT(*) AS planet_count\n"
    "  FROM planets GROUP BY sector\n"
    "), prt AS (\n"
    "  SELECT sector AS sector_id, COUNT(*) AS port_count\n"
    "  FROM ports GROUP BY sector\n"
    ")\n"
    "SELECT s.id AS sector_id,\n"
    "       COALESCE(d.outdeg,0) AS outdeg,\n"
    "       COALESCE(d.indeg,0) AS indeg,\n"
    "       COALESCE(prt.port_count,0) AS ports,\n"
    "       COALESCE(pc.planet_count,0) AS planets\n"
    "FROM sectors s\n"
    "LEFT JOIN sector_degrees d ON d.sector_id = s.id\n"
    "LEFT JOIN prt ON prt.sector_id = s.id\n"
    "LEFT JOIN pc  ON pc.sector_id  = s.id;",

/* ===================== PORTS & TRADE ===================== */

/* 9) Compact trade code per port (B/S over ore|organics|equipment) */
  "CREATE VIEW IF NOT EXISTS port_trade_code AS\n"
    "WITH m AS (\n"
    "  SELECT p.id AS port_id,\n"
    "         MAX(CASE WHEN t.commodity='ore'       THEN (CASE t.mode WHEN 'buy' THEN 'B' ELSE 'S' END) END) AS ore,\n"
    "         MAX(CASE WHEN t.commodity='organics'  THEN (CASE t.mode WHEN 'buy' THEN 'B' ELSE 'S' END) END) AS org,\n"
    "         MAX(CASE WHEN t.commodity='equipment' THEN (CASE t.mode WHEN 'buy' THEN 'B' ELSE 'S' END) END) AS eqp\n"
    "  FROM ports p\n"
    "  LEFT JOIN port_trade t ON t.port_id = p.id\n"
    "  GROUP BY p.id\n"
    ")\n"
    "SELECT p.id, p.number, p.name, p.sector AS sector_id, p.size, p.techlevel, p.petty_cash,\n"
    "       COALESCE(m.ore,'-') || COALESCE(m.org,'-') || COALESCE(m.eqp,'-') AS trade_code\n"
    "FROM ports p\n" "LEFT JOIN m ON m.port_id = p.id;",

/* 10) Ports grouped by sector (depends on port_trade_code) */
  "CREATE VIEW IF NOT EXISTS sector_ports AS\n"
    "SELECT s.id AS sector_id,\n"
    "       COUNT(p.id) AS port_count,\n"
    "       COALESCE(GROUP_CONCAT(p.name || ':' || pt.trade_code, ' | '), '') AS ports\n"
    "FROM sectors s\n"
    "LEFT JOIN port_trade_code pt ON pt.sector_id = s.id\n"
    "LEFT JOIN ports p ON p.id = pt.id\n" "GROUP BY s.id;",

/* 11) Stardock location (by type=9 or name) */
  "CREATE VIEW IF NOT EXISTS stardock_location AS\n"
    "SELECT id AS port_id, number, name, sector AS sector_id\n"
    "FROM ports\n" "WHERE type = 9 OR name LIKE '%Stardock%';",

/* ===================== PLANETS / CITADELS ===================== */

/* 12) Planet + citadel (with optional owner) */
  "CREATE VIEW IF NOT EXISTS planet_citadels AS\n"
    "SELECT c.id AS citadel_id,\n"
    "       c.level AS citadel_level,\n"
    "       p.id AS planet_id,\n"
    "       p.name AS planet_name,\n"
    "       p.sector AS sector_id,\n"
    "       c.owner AS owner_id,\n"
    "       pl.name AS owner_name\n"
    "FROM citadels c\n"
    "JOIN planets  p  ON p.id = c.planet_id\n"
    "LEFT JOIN players pl ON pl.id = c.owner;",

/* 13) Planets grouped by sector */
  "CREATE VIEW IF NOT EXISTS sector_planets AS\n"
    "SELECT s.id AS sector_id,\n"
    "       COUNT(p.id) AS planet_count,\n"
    "       COALESCE(GROUP_CONCAT(p.name, ', '), '') AS planets\n"
    "FROM sectors s\n"
    "LEFT JOIN planets p ON p.sector = s.id\n" "GROUP BY s.id;",

/* ===================== RUNTIME SNAPSHOTS ===================== */

/* 14) Player locations */
  "CREATE VIEW IF NOT EXISTS player_locations AS\n"
    "SELECT\n"
    "  p.id AS player_id,\n"
    "  p.name AS player_name,\n"
    "  sh.sector AS sector_id,\n"
    "  sh.id AS ship_id,\n"
    "  CASE\n"
    "    WHEN sh.ported = 1 THEN 'docked_at_port'\n"
    "    WHEN sh.onplanet = 1 THEN 'landed_on_planet'\n"
    "    WHEN sh.sector IS NOT NULL THEN 'in_space'\n"
    "    ELSE 'unknown'\n"
    "  END AS location_kind,\n"
    "  sh.ported AS is_ported,\n"
    "  sh.onplanet AS is_onplanet\n"
    "FROM players p\n" "LEFT JOIN ships sh ON sh.id = p.ship;",

/* 15) Ships by sector */
  "CREATE VIEW IF NOT EXISTS ships_by_sector AS\n"
    "SELECT s.id AS sector_id,\n"
    "       COUNT(sh.id) AS ship_count\n"
    "FROM sectors s\n"
    "LEFT JOIN ships sh ON sh.sector = s.id\n" "GROUP BY s.id;",

/* ===================== OPS DASHBOARDS ===================== */

/* 16) Sector ops (depends on sector_summary, sector_ports, sector_planets, ships_by_sector) */
  " CREATE VIEW IF NOT EXISTS sector_ops AS  "
    "  WITH weighted_assets AS (  "
    "     SELECT  "
    "         sector AS sector_id,  "
    "         COALESCE(SUM(  "
    "             quantity * CASE asset_type  "
    "                 WHEN 1 THEN 10  "
    "                 WHEN 2 THEN 5  "
    "                 WHEN 3 THEN 1  "
    "                 WHEN 4 THEN 10  "
    "                 ELSE 0  "
    "             END  "
    "         ), 0) AS asset_score  "
    "     FROM sector_assets  "
    "     GROUP BY sector  "
    "  )  "
    "  SELECT  "
    "     ss.sector_id,  "
    "     ss.outdeg,  "
    "     ss.indeg,  "
    "     sp.port_count,  "
    "     spp.planet_count,  "
    "     sbs.ship_count,  "
    "     (  "
    "         (COALESCE(spp.planet_count, 0) * 500)  "
    "       + (COALESCE(sp.port_count, 0) * 100)  "
    "       + (COALESCE(sbs.ship_count, 0) * 40)  "
    "       + (COALESCE(wa.asset_score, 0))  "
    "     ) AS total_density_score,  "
    "     wa.asset_score AS weighted_asset_score  "
    "  FROM sector_summary ss  "
    "  LEFT JOIN sector_ports    sp  ON sp.sector_id  = ss.sector_id  "
    "  LEFT JOIN sector_planets  spp ON spp.sector_id = ss.sector_id  "
    "  LEFT JOIN ships_by_sector sbs ON sbs.sector_id = ss.sector_id  "
    "  LEFT JOIN weighted_assets wa ON wa.sector_id = ss.sector_id;  ",


/* 17) World summary (one row) */
  "CREATE VIEW IF NOT EXISTS world_summary AS\n"
    "WITH a AS (SELECT COUNT(*) AS sectors FROM sectors),\n"
    "     b AS (SELECT COUNT(*) AS warps   FROM sector_warps),\n"
    "     c AS (SELECT COUNT(*) AS ports   FROM ports),\n"
    "     d AS (SELECT COUNT(*) AS planets FROM planets),\n"
    "     e AS (SELECT COUNT(*) AS players FROM players),\n"
    "     f AS (SELECT COUNT(*) AS ships   FROM ships)\n"
    "SELECT a.sectors, b.warps, c.ports, d.planets, e.players, f.ships\n"
    "FROM a,b,c,d,e,f;",

  "CREATE VIEW IF NOT EXISTS v_bidirectional_warps AS\n"
    "SELECT\n"
    "  CASE WHEN w1.from_sector < w1.to_sector THEN w1.from_sector ELSE w1.to_sector END AS a,\n"
    "  CASE WHEN w1.from_sector < w1.to_sector THEN w1.to_sector ELSE w1.from_sector END AS b\n"
    "FROM sector_warps AS w1\n"
    "JOIN sector_warps AS w2\n"
    "  ON w1.from_sector = w2.to_sector\n"
    " AND w1.to_sector   = w2.from_sector\n" "GROUP BY a, b;",


  /* --- player_info_v1 view and indexes --- */

  " CREATE VIEW IF NOT EXISTS player_info_v1  AS   "
    " SELECT   "
    "   p.id         AS player_id,   "
    "   p.name       AS player_name,   "
    "   p.number     AS player_number,   "
    "   sh.sector    AS sector_id,    "
    "   sctr.name    AS sector_name,   "
    "   p.credits    AS petty_cash,   "
    "   p.alignment  AS alignment,   "
    "   p.experience AS experience,   "
    "   p.ship       AS ship_number,   "
    "   sh.id        AS ship_id,   "
    "   sh.name      AS ship_name,   "
    "   sh.type_id   AS ship_type_id,   "
    "   st.name      AS ship_type_name,   "
    "   st.maxholds  AS ship_holds_capacity,    "
    "   sh.holds     AS ship_holds_current,    "
    "   sh.fighters  AS ship_fighters,   "
    "   sh.mines     AS ship_mines,            "
    "   sh.limpets   AS ship_limpets,          "
    "   sh.genesis   AS ship_genesis,          "
    "   sh.photons   AS ship_photons,          "
    "   sh.beacons   AS ship_beacons,          "
    "   sh.colonists AS ship_colonists,        "
    "   sh.equipment AS ship_equipment,        "
    "   sh.organics  AS ship_organics,         "
    "   sh.ore       AS ship_ore,              "
    "   sh.ported    AS ship_ported,           "
    "   sh.onplanet  AS ship_onplanet,         "
    "   (COALESCE(p.credits,0) + COALESCE(sh.fighters,0)*2) AS approx_worth   "
    " FROM players p   "
    " LEFT JOIN ships      sh   ON sh.id = p.ship   "
    " LEFT JOIN shiptypes  st   ON st.id = sh.type_id   "
    " LEFT JOIN sectors    sctr ON sctr.id = sh.sector; ",


  " CREATE VIEW IF NOT EXISTS sector_search_index  AS  "
    " SELECT   "
    "     'sector' AS kind,  "
    "     s.id AS id,  "
    "     s.name AS name,  "
    "     s.id AS sector_id,  "
    "     s.name AS sector_name,  "
    "     s.name AS search_term_1  "
    " FROM sectors s  "
    " UNION ALL  "
    " SELECT   "
    "     'port' AS kind,  "
    "     p.id AS id,  "
    "     p.name AS name,  "
    "     p.sector AS sector_id,  "
    "     s.name AS sector_name,  "
    "     p.name AS search_term_1  "
    " FROM ports p  " " JOIN sectors s ON s.id = p.sector;  "
//////////////////////////////////////////////////////////////////////
/// CREATE INDEX
//////////////////////////////////////////////////////////////////////
/* ===================== INDEXES ===================== */
    "CREATE INDEX IF NOT EXISTS idx_player_block_blocked "
    "ON player_block (blocked_id);",

  "CREATE INDEX IF NOT EXISTS idx_notice_seen_player "
    "ON notice_seen (player_id, seen_at DESC);",

  "CREATE INDEX IF NOT EXISTS idx_system_notice_active "
    "ON system_notice (expires_at, created_at DESC);",



  "CREATE INDEX IF NOT EXISTS idx_warps_from ON sector_warps(from_sector);",
  "CREATE INDEX IF NOT EXISTS idx_warps_to   ON sector_warps(to_sector);",
  "CREATE INDEX IF NOT EXISTS idx_ports_loc  ON ports(sector);",
  "CREATE INDEX IF NOT EXISTS idx_planets_sector ON planets(sector);",
  "CREATE INDEX IF NOT EXISTS idx_citadels_planet ON citadels(planet_id);",
  "CREATE INDEX IF NOT EXISTS ix_warps_from_to ON sector_warps(from_sector, to_sector);",
  "CREATE INDEX IF NOT EXISTS idx_players_name     ON players(name);",
  "CREATE INDEX IF NOT EXISTS idx_players_sector   ON players(sector);",
  "CREATE INDEX IF NOT EXISTS idx_players_ship     ON players(ship);",
  "CREATE INDEX IF NOT EXISTS idx_ships_id         ON ships(id);",
  "CREATE INDEX IF NOT EXISTS idx_sectors_id       ON sectors(id);",
  "DROP INDEX IF EXISTS uniq_ship_owner;",
  "DROP TABLE IF EXISTS player_ships;",

  "DROP INDEX IF EXISTS idx_ships_id;",
  "DROP INDEX IF EXISTS idx_sectors_id;",

  //-- Make player names unique if that’s a rule:
  "DROP INDEX IF EXISTS idx_players_name;",
  //  "CREATE UNIQUE INDEX idx_players_name ON players(name);",

  // -- `ports.number` probably unique (if that’s your design):
  "CREATE UNIQUE INDEX IF NOT EXISTS idx_ports_number ON ports(number);",

  //-- Ship ownership lookups by player or ship:
  "CREATE INDEX IF NOT EXISTS idx_ship_own_ship   ON ship_ownership(ship_id);",

  "DROP INDEX IF EXISTS idx_ports_number;",
  "CREATE UNIQUE INDEX IF NOT EXISTS idx_ports_loc_number ON ports(sector, number);",


  " CREATE UNIQUE INDEX IF NOT EXISTS idx_mail_idem_recipient  "
    " ON mail(idempotency_key, recipient_id)  "
    " WHERE idempotency_key IS NOT NULL; ",

  " CREATE INDEX IF NOT EXISTS idx_mail_inbox   "
    " ON mail(recipient_id, deleted, archived, sent_at DESC); ",

  " CREATE INDEX IF NOT EXISTS idx_mail_unread  "
    " ON mail(recipient_id, read_at); ",

  " CREATE INDEX IF NOT EXISTS idx_mail_sender  "
    " ON mail(sender_id, sent_at DESC); ",
  " CREATE INDEX IF NOT EXISTS idx_subspace_time  "
    " ON subspace(posted_at DESC); ",

  " CREATE UNIQUE INDEX IF NOT EXISTS ux_corp_name_uc  "
    " ON corporations(upper(name)); ",

  " CREATE UNIQUE INDEX IF NOT EXISTS ux_corp_tag_uc  "
    " ON corporations(upper(tag)) WHERE tag IS NOT NULL; ",

  " CREATE INDEX IF NOT EXISTS ix_corporations_owner  "
    " ON corporations(owner_id); ",


  " CREATE INDEX IF NOT EXISTS idx_ship_own_player ON ship_ownership(player_id); ",

  " CREATE INDEX IF NOT EXISTS ix_corp_members_player  "
    " ON corp_members(player_id); ",

  " CREATE INDEX IF NOT EXISTS ix_corp_members_role  "
    " ON corp_members(corp_id, role); ",

  " CREATE INDEX IF NOT EXISTS idx_corp_mail_corp  "
    " ON corp_mail(corp_id, posted_at DESC); ",

  " CREATE INDEX IF NOT EXISTS idx_corp_log_corp_time  "
    " ON corp_log(corp_id, created_at DESC); ",

  " CREATE INDEX IF NOT EXISTS idx_corp_log_type  "
    " ON corp_log(event_type, created_at DESC); ",

  " CREATE INDEX IF NOT EXISTS idx_sys_events_time  "
    " ON system_events(created_at DESC); ",

  " CREATE INDEX IF NOT EXISTS idx_sys_events_scope  "
    " ON system_events(scope); ",

  " CREATE INDEX IF NOT EXISTS idx_subscriptions_player  "
    " ON subscriptions(player_id, enabled); ",

  " CREATE INDEX IF NOT EXISTS idx_subs_enabled  "
    " ON subscriptions(enabled); ",

  " CREATE INDEX IF NOT EXISTS idx_subs_event  "
    " ON subscriptions(event_type); ",


  "CREATE INDEX IF NOT EXISTS idx_player_prefs_player ON player_prefs(player_id)  ;  "
    "CREATE INDEX IF NOT EXISTS idx_bookmarks_player ON player_bookmarks(player_id)  ;  "
    "CREATE INDEX IF NOT EXISTS idx_avoid_player ON player_avoid(player_id)  ;  "
    "CREATE INDEX IF NOT EXISTS idx_notes_player ON player_notes(player_id)  ;  "
    ///////////////// TRIGGERS /////////////////////
    " CREATE TRIGGER IF NOT EXISTS corporations_touch_updated  "
    " AFTER UPDATE ON corporations  "
    " FOR EACH ROW  "
    " BEGIN  "
    "   UPDATE corporations  "
    "     SET updated_at = strftime('%Y-%m-%dT%H:%M:%SZ','now')  "
    "   WHERE id = NEW.id; " " END; ",

  " CREATE TRIGGER IF NOT EXISTS corp_owner_must_be_member_insert  "
    " AFTER INSERT ON corporations  "
    " FOR EACH ROW  "
    " WHEN NEW.owner_id IS NOT NULL  "
    " AND NOT EXISTS (SELECT 1 FROM corp_members WHERE corp_id=NEW.id AND player_id=NEW.owner_id)  "
    " BEGIN  "
    "   INSERT INTO corp_members(corp_id, player_id, role) VALUES(NEW.id, NEW.owner_id, 'Leader'); "
    " END; ",

  " CREATE TRIGGER IF NOT EXISTS corp_one_leader_guard  "
    " BEFORE INSERT ON corp_members  "
    " FOR EACH ROW  "
    " WHEN NEW.role='Leader'  "
    " AND EXISTS (SELECT 1 FROM corp_members WHERE corp_id=NEW.corp_id AND role='Leader')  "
    " BEGIN  "
    "   SELECT RAISE(ABORT, 'corp may have only one Leader'); " " END; ",

  " CREATE TRIGGER IF NOT EXISTS corp_owner_leader_sync  "
    " AFTER UPDATE OF owner_id ON corporations  "
    " FOR EACH ROW  "
    " BEGIN  "
    "   UPDATE corp_members SET role='Officer'  "
    "    WHERE corp_id=NEW.id AND role='Leader' AND player_id<>NEW.owner_id;  "
    "   INSERT INTO corp_members(corp_id, player_id, role)  "
    "   VALUES(NEW.id, NEW.owner_id, 'Leader')  "
    "   ON CONFLICT(corp_id, player_id) DO UPDATE SET role='Leader'; "
    " END; ",

};


const char *insert_default_sql[] = {
  /* Config defaults */
  "INSERT OR IGNORE INTO config (id, turnsperday, maxwarps_per_sector, startingcredits, startingfighters, startingholds, processinterval, autosave, max_ports, max_planets_per_sector, max_total_planets, max_citadel_level, number_of_planet_types, max_ship_name_length, ship_type_count, hash_length, default_nodes, buff_size, max_name_length, planet_type_count, server_port, s2s_port, bank_alert_threshold_player, bank_alert_threshold_corp, genesis_enabled, genesis_block_at_cap, genesis_navhaz_delta, genesis_class_weight_M, genesis_class_weight_K, genesis_class_weight_O, genesis_class_weight_L, genesis_class_weight_C, genesis_class_weight_H, genesis_class_weight_U, shipyard_enabled, shipyard_trade_in_factor_bp, shipyard_require_cargo_fit, shipyard_require_fighters_fit, shipyard_require_shields_fit, shipyard_require_hardware_compat, shipyard_tax_bp) "
    "VALUES (1, 120, 6, 1000, 10, 20, 1, 5, 200, 6, 300, 6, 8, 50, 8, 128, 500, 1024, 50, 8, 1234, 4321, 1000000, 5000000, 1, 0, 0, 10, 10, 10, 10, 10, 10, 5, 1, 5000, 1, 1, 1, 1, 1000);",


/* Shiptypes: name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled */

/* Initial Ship Types (First Block) */
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('Merchant Cruiser', 41300, NULL, NULL, NULL, 750, 20, 75, 2500, 3, 50, 0, 5, 0, 0, 0, 5, 400, 10, 10, 0, 1, 1, 0, 0, 1, 1); ",
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('Scout Marauder', 15950, NULL, NULL, NULL, 250, 10, 25, 250, 2, 0, 0, 0, 0, 0, 0, 0, 100, 20, 20, 0, 1, 1, 0, 0, 1, 1); ",
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('Missile Frigate', 100000, NULL, NULL, NULL, 2000, 12, 60, 5000, 3, 5, 0, 0, 0, 0, 0, 2, 400, 13, 13, 5, 0, 0, 1, 0, 1, 1); ",
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('Battleship', 88500, NULL, NULL, NULL, 3000, 16, 80, 10000, 4, 25, 0, 1, 0, 0, 0, 8, 750, 16, 16, 50, 1, 1, 0, 0, 1, 1); ",
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('Corporate Flagship', 163500, NULL, NULL, NULL, 6000, 20, 85, 20000, 3, 100, 0, 10, 0, 0, 1, 10, 1500, 12, 12, 100, 1, 1, 1, 0, 1, 1); ",
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('Colonial Transport', 63600, NULL, NULL, NULL, 100, 50, 250, 200, 6, 0, 0, 5, 0, 0, 0, 7, 500, 6, 6, 10, 0, 1, 0, 0, 1, 1); ",
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('Cargo Transport', 51950, NULL, NULL, NULL, 125, 50, 125, 400, 4, 1, 0, 2, 0, 0, 0, 5, 1000, 8, 8, 20, 1, 1, 0, 0, 1, 1); ",
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('Merchant Freighter', 33400, NULL, NULL, NULL, 100, 30, 65, 300, 2, 2, 0, 2, 0, 0, 0, 5, 500, 8, 8, 20, 1, 1, 0, 0, 1, 1); ",
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('Imperial Starship', 329000, NULL, NULL, NULL, 10000, 40, 150, 50000, 4, 125, 0, 10, 0, 0, 1, 15, 2000, 15, 15, 150, 1, 1, 1, 0, 1, 1); ",
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('Havoc Gunstar', 79000, NULL, NULL, NULL, 1000, 12, 50, 10000, 3, 5, 0, 1, 0, 0, 1, 6, 3000, 13, 13, 5, 1, 0, 0, 0, 1, 1); ",
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('Constellation', 72500, NULL, NULL, NULL, 2000, 20, 80, 5000, 3, 25, 0, 2, 0, 0, 0, 6, 750, 14, 14, 50, 1, 1, 0, 0, 1, 1); ",
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('T''khasi Orion', 42500, NULL, NULL, NULL, 250, 30, 60, 750, 2, 5, 0, 1, 0, 0, 0, 3, 750, 11, 11, 20, 1, 1, 0, 0, 1, 1); ",
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('Tholian Sentinel', 47500, NULL, NULL, NULL, 800, 10, 50, 2500, 4, 50, 0, 1, 0, 0, 0, 3, 4000, 1, 1, 10, 1, 0, 0, 0, 1, 1); ",
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('Taurean Mule', 63600, NULL, NULL, NULL, 150, 50, 150, 300, 4, 0, 0, 1, 0, 0, 0, 5, 600, 5, 5, 20, 1, 1, 0, 0, 1, 1); ",
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('Interdictor Cruiser', 539000, NULL, NULL, NULL, 15000, 10, 40, 100000, 15, 200, 0, 20, 0, 0, 0, 20, 4000, 12, 12, 100, 1, 1, 0, 0, 1, 1); ",
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('Ferrengi Warship', 150000, NULL, NULL, NULL, 5000, 20, 100, 15000, 5, 20, 0, 5, 0, 0, 0, 10, 5000, 15, 15, 50, 1, 1, 1, 0, 0, 1); ",
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('Imperial Starship (NPC)', 329000, NULL, NULL, NULL, 10000, 40, 150, 50000, 4, 125, 0, 10, 0, 0, 1, 15, 2000, 15, 15, 150, 1, 1, 1, 0, 0, 1); ",
/* Orion Syndicate Ship Types (Second Block) */
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('Orion Heavy Fighter Patrol', 150000, NULL, NULL, NULL, 5000, 20, 50, 20000, 5, 10, 0, 5, 0, 0, 0, 10, 5000, 20, 10, 25, 1, 1, 1, 0, 0, 1); ",
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('Orion Scout/Looter', 80000, NULL, NULL, NULL, 4000, 10, 150, 5000, 5, 10, 0, 5, 0, 0, 0, 10, 3000, 8, 8, 25, 1, 1, 1, 0, 0, 1); ",
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('Orion Contraband Runner', 120000, NULL, NULL, NULL, 3000, 10, 200, 3000, 5, 10, 0, 5, 0, 0, 0, 10, 4000, 10, 5, 25, 1, 1, 1, 0, 0, 1); ",
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('Orion Smuggler''s Kiss', 130000, NULL, NULL, NULL, 5000, 15, 100, 10000, 5, 10, 0, 5, 0, 0, 0, 10, 5000, 15, 15, 25, 1, 1, 1, 0, 0, 1); ",
  " INSERT OR IGNORE INTO shiptypes (name, basecost, required_alignment, required_commission, required_experience, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, max_detonators, max_probes, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled) VALUES ('Orion Black Market Guard', 180000, NULL, NULL, NULL, 6000, 20, 60, 8000, 5, 10, 0, 5, 0, 0, 0, 10, 8000, 12, 25, 25, 1, 1, 1, 0, 0, 1); "
    "INSERT OR IGNORE INTO ship_roles (role_id, role, role_description) VALUES (1, 'owner',   'Legal owner; can sell/rename, set availability, assign others');",
  "INSERT OR IGNORE INTO ship_roles (role_id, role, role_description) VALUES (2, 'pilot',   'Currently flying the ship; usually the active ship for the player');",
  "INSERT OR IGNORE INTO ship_roles (role_id, role, role_description) VALUES (3, 'crew',    'Can board and use limited functions (e.g., scan, fire fighters)');",
  "INSERT OR IGNORE INTO ship_roles (role_id, role, role_description) VALUES (4, 'leasee',  'Temporary control with limits; can pilot but not sell/rename');",
  "INSERT OR IGNORE INTO ship_roles (role_id, role, role_description) VALUES (5, 'lender',  'Party that lent/leased the ship; can revoke lease');",
  "INSERT OR IGNORE INTO ship_roles (role_id, role, role_description) VALUES (6, 'corp',    'Corporate ownership/control (for future org/corp features)');",
  "INSERT OR IGNORE INTO ship_roles (role_id, role, role_description) VALUES (7, 'manager', 'Delegated admin; can assign crew/pilot but not sell');",



  "INSERT OR IGNORE INTO planettypes (code, typeDescription, typeName, "
    "citadelUpgradeTime_lvl1, citadelUpgradeTime_lvl2, citadelUpgradeTime_lvl3, citadelUpgradeTime_lvl4, citadelUpgradeTime_lvl5, citadelUpgradeTime_lvl6, "
    "citadelUpgradeOre_lvl1, citadelUpgradeOre_lvl2, citadelUpgradeOre_lvl3, citadelUpgradeOre_lvl4, citadelUpgradeOre_lvl5, citadelUpgradeOre_lvl6, "
    "citadelUpgradeOrganics_lvl1, citadelUpgradeOrganics_lvl2, citadelUpgradeOrganics_lvl3, citadelUpgradeOrganics_lvl4, citadelUpgradeOrganics_lvl5, citadelUpgradeOrganics_lvl6, "
    "citadelUpgradeEquipment_lvl1, citadelUpgradeEquipment_lvl2, citadelUpgradeEquipment_lvl3, citadelUpgradeEquipment_lvl4, citadelUpgradeEquipment_lvl5, citadelUpgradeEquipment_lvl6, "
    "citadelUpgradeColonist_lvl1, citadelUpgradeColonist_lvl2, citadelUpgradeColonist_lvl3, citadelUpgradeColonist_lvl4, citadelUpgradeColonist_lvl5, citadelUpgradeColonist_lvl6, "
    "maxColonist_ore, maxColonist_organics, maxColonist_equipment, "
    "fighters, fuelProduction, organicsProduction, equipmentProduction, fighterProduction, "
    "maxore, maxorganics, maxequipment, maxfighters, breeding) "
    "VALUES ('M','Earth type','Earth', "
    "4,4,5,10,5,15, "
    "300,200,500,1000,300,1000, "
    "200,50,250,1200,400,1200, "
    "250,250,500,1000,1000,2000, "
    "1000000,2000000,4000000,6000000,6000000,6000000, "
    "100000,100000,100000, "
    "0,0,0,0,0, " "10000000,100000,100000,1000000,0.75);",

  /* Mountainous (L) */
  "INSERT OR IGNORE INTO planettypes (code, typeDescription, typeName, "
    "citadelUpgradeTime_lvl1, citadelUpgradeTime_lvl2, citadelUpgradeTime_lvl3, citadelUpgradeTime_lvl4, citadelUpgradeTime_lvl5, citadelUpgradeTime_lvl6, "
    "citadelUpgradeOre_lvl1, citadelUpgradeOre_lvl2, citadelUpgradeOre_lvl3, citadelUpgradeOre_lvl4, citadelUpgradeOre_lvl5, citadelUpgradeOre_lvl6, "
    "citadelUpgradeOrganics_lvl1, citadelUpgradeOrganics_lvl2, citadelUpgradeOrganics_lvl3, citadelUpgradeOrganics_lvl4, citadelUpgradeOrganics_lvl5, citadelUpgradeOrganics_lvl6, "
    "citadelUpgradeEquipment_lvl1, citadelUpgradeEquipment_lvl2, citadelUpgradeEquipment_lvl3, citadelUpgradeEquipment_lvl4, citadelUpgradeEquipment_lvl5, citadelUpgradeEquipment_lvl6, "
    "citadelUpgradeColonist_lvl1, citadelUpgradeColonist_lvl2, citadelUpgradeColonist_lvl3, citadelUpgradeColonist_lvl4, citadelUpgradeColonist_lvl5, citadelUpgradeColonist_lvl6, "
    "maxColonist_ore, maxColonist_organics, maxColonist_equipment, "
    "fighters, fuelProduction, organicsProduction, equipmentProduction, fighterProduction, "
    "maxore, maxorganics, maxequipment, maxfighters, breeding) "
    "VALUES ('L','Mountainous','Mountain', "
    "2,5,5,8,5,12, "
    "150,200,600,1000,300,1000, "
    "100,50,250,1200,400,1200, "
    "150,250,700,1000,1000,2000, "
    "400000,1400000,3600000,5600000,7000000,5600000, "
    "200000,200000,200000, "
    "0,0,0,0,0, " "200000,200000,100000,1000000,0.24);",

  /* Oceanic (O) */
  "INSERT OR IGNORE INTO planettypes (code, typeDescription, typeName, "
    "citadelUpgradeTime_lvl1, citadelUpgradeTime_lvl2, citadelUpgradeTime_lvl3, citadelUpgradeTime_lvl4, citadelUpgradeTime_lvl5, citadelUpgradeTime_lvl6, "
    "citadelUpgradeOre_lvl1, citadelUpgradeOre_lvl2, citadelUpgradeOre_lvl3, citadelUpgradeOre_lvl4, citadelUpgradeOre_lvl5, citadelUpgradeOre_lvl6, "
    "citadelUpgradeOrganics_lvl1, citadelUpgradeOrganics_lvl2, citadelUpgradeOrganics_lvl3, citadelUpgradeOrganics_lvl4, citadelUpgradeOrganics_lvl5, citadelUpgradeOrganics_lvl6, "
    "citadelUpgradeEquipment_lvl1, citadelUpgradeEquipment_lvl2, citadelUpgradeEquipment_lvl3, citadelUpgradeEquipment_lvl4, citadelUpgradeEquipment_lvl5, citadelUpgradeEquipment_lvl6, "
    "citadelUpgradeColonist_lvl1, citadelUpgradeColonist_lvl2, citadelUpgradeColonist_lvl3, citadelUpgradeColonist_lvl4, citadelUpgradeColonist_lvl5, citadelUpgradeColonist_lvl6, "
    "maxColonist_ore, maxColonist_organics, maxColonist_equipment, "
    "fighters, fuelProduction, organicsProduction, equipmentProduction, fighterProduction, "
    "maxore, maxorganics, maxequipment, maxfighters, breeding) "
    "VALUES ('O','Oceanic','Ocean', "
    "6,5,8,5,4,8, "
    "500,200,600,700,300,700, "
    "200,50,400,900,400,900, "
    "400,300,650,800,1000,1600, "
    "1400000,2400000,4400000,7000000,8000000,7000000, "
    "100000,1000000,1000000, "
    "0,0,0,0,0, " "50000,1000000,50000,1000000,0.30);",

  /* Desert Wasteland (K) */
  "INSERT OR IGNORE INTO planettypes (code, typeDescription, typeName, "
    "citadelUpgradeTime_lvl1, citadelUpgradeTime_lvl2, citadelUpgradeTime_lvl3, citadelUpgradeTime_lvl4, citadelUpgradeTime_lvl5, citadelUpgradeTime_lvl6, "
    "citadelUpgradeOre_lvl1, citadelUpgradeOre_lvl2, citadelUpgradeOre_lvl3, citadelUpgradeOre_lvl4, citadelUpgradeOre_lvl5, citadelUpgradeOre_lvl6, "
    "citadelUpgradeOrganics_lvl1, citadelUpgradeOrganics_lvl2, citadelUpgradeOrganics_lvl3, citadelUpgradeOrganics_lvl4, citadelUpgradeOrganics_lvl5, citadelUpgradeOrganics_lvl6, "
    "citadelUpgradeEquipment_lvl1, citadelUpgradeEquipment_lvl2, citadelUpgradeEquipment_lvl3, citadelUpgradeEquipment_lvl4, citadelUpgradeEquipment_lvl5, citadelUpgradeEquipment_lvl6, "
    "citadelUpgradeColonist_lvl1, citadelUpgradeColonist_lvl2, citadelUpgradeColonist_lvl3, citadelUpgradeColonist_lvl4, citadelUpgradeColonist_lvl5, citadelUpgradeColonist_lvl6, "
    "maxColonist_ore, maxColonist_organics, maxColonist_equipment, "
    "fighters, fuelProduction, organicsProduction, equipmentProduction, fighterProduction, "
    "maxore, maxorganics, maxequipment, maxfighters, breeding) "
    "VALUES ('K','Desert Wasteland','Desert', "
    "6,5,8,5,4,8, "
    "400,300,700,700,300,700, "
    "300,80,900,900,400,900, "
    "600,400,800,800,1000,1600, "
    "1000000,2400000,4000000,7000000,8000000,7000000, "
    "200000,50000,50000, " "0,0,0,0,0, " "200000,50000,10000,1000000,0.50);",

  /* Volcanic (H) */
  "INSERT OR IGNORE INTO planettypes (code, typeDescription, typeName, "
    "citadelUpgradeTime_lvl1, citadelUpgradeTime_lvl2, citadelUpgradeTime_lvl3, citadelUpgradeTime_lvl4, citadelUpgradeTime_lvl5, citadelUpgradeTime_lvl6, "
    "citadelUpgradeOre_lvl1, citadelUpgradeOre_lvl2, citadelUpgradeOre_lvl3, citadelUpgradeOre_lvl4, citadelUpgradeOre_lvl5, citadelUpgradeOre_lvl6, "
    "citadelUpgradeOrganics_lvl1, citadelUpgradeOrganics_lvl2, citadelUpgradeOrganics_lvl3, citadelUpgradeOrganics_lvl4, citadelUpgradeOrganics_lvl5, citadelUpgradeOrganics_lvl6, "
    "citadelUpgradeEquipment_lvl1, citadelUpgradeEquipment_lvl2, citadelUpgradeEquipment_lvl3, citadelUpgradeEquipment_lvl4, citadelUpgradeEquipment_lvl5, citadelUpgradeEquipment_lvl6, "
    "citadelUpgradeColonist_lvl1, citadelUpgradeColonist_lvl2, citadelUpgradeColonist_lvl3, citadelUpgradeColonist_lvl4, citadelUpgradeColonist_lvl5, citadelUpgradeColonist_lvl6, "
    "maxColonist_ore, maxColonist_organics, maxColonist_equipment, "
    "fighters, fuelProduction, organicsProduction, equipmentProduction, fighterProduction, "
    "maxore, maxorganics, maxequipment, maxfighters, breeding) "
    "VALUES ('H','Volcanic','Volcano', "
    "4,5,8,12,18,8, "
    "500,300,1200,2000,3000,2000, "
    "300,100,400,2000,1200,2000, "
    "600,400,1500,2500,2000,5000, "
    "800000,1600000,4400000,7000000,10000000,7000000, "
    "1000000,10000,10000, " "0,0,0,0,0, "
    "1000000,10000,100000,1000000,0.30);",

  /* Gaseous (U) */
  "INSERT OR IGNORE INTO planettypes (code, typeDescription, typeName, "
    "citadelUpgradeTime_lvl1, citadelUpgradeTime_lvl2, citadelUpgradeTime_lvl3, citadelUpgradeTime_lvl4, citadelUpgradeTime_lvl5, citadelUpgradeTime_lvl6, "
    "citadelUpgradeOre_lvl1, citadelUpgradeOre_lvl2, citadelUpgradeOre_lvl3, citadelUpgradeOre_lvl4, citadelUpgradeOre_lvl5, citadelUpgradeOre_lvl6, "
    "citadelUpgradeOrganics_lvl1, citadelUpgradeOrganics_lvl2, citadelUpgradeOrganics_lvl3, citadelUpgradeOrganics_lvl4, citadelUpgradeOrganics_lvl5, citadelUpgradeOrganics_lvl6, "
    "citadelUpgradeEquipment_lvl1, citadelUpgradeEquipment_lvl2, citadelUpgradeEquipment_lvl3, citadelUpgradeEquipment_lvl4, citadelUpgradeEquipment_lvl5, citadelUpgradeEquipment_lvl6, "
    "citadelUpgradeColonist_lvl1, citadelUpgradeColonist_lvl2, citadelUpgradeColonist_lvl3, citadelUpgradeColonist_lvl4, citadelUpgradeColonist_lvl5, citadelUpgradeColonist_lvl6, "
    "maxColonist_ore, maxColonist_organics, maxColonist_equipment, "
    "fighters, fuelProduction, organicsProduction, equipmentProduction, fighterProduction, "
    "maxore, maxorganics, maxequipment, maxfighters, breeding) "
    "VALUES ('U','Gaseous','Gas Giant', "
    "8,4,5,5,4,8, "
    "1200,300,500,500,200,500, "
    "400,100,500,200,200,200, "
    "2500,400,2000,600,600,1200, "
    "3000000,3000000,8000000,6000000,8000000,6000000, "
    "10000,10000,10000, " "0,0,0,0,0, " "10000,10000,10000,1000000,-0.10);",

  /* Glacial/Ice (C) */
  "INSERT OR IGNORE INTO planettypes (code, typeDescription, typeName, "
    "citadelUpgradeTime_lvl1, citadelUpgradeTime_lvl2, citadelUpgradeTime_lvl3, citadelUpgradeTime_lvl4, citadelUpgradeTime_lvl5, citadelUpgradeTime_lvl6, "
    "citadelUpgradeOre_lvl1, citadelUpgradeOre_lvl2, citadelUpgradeOre_lvl3, citadelUpgradeOre_lvl4, citadelUpgradeOre_lvl5, citadelUpgradeOre_lvl6, "
    "citadelUpgradeOrganics_lvl1, citadelUpgradeOrganics_lvl2, citadelUpgradeOrganics_lvl3, citadelUpgradeOrganics_lvl4, citadelUpgradeOrganics_lvl5, citadelUpgradeOrganics_lvl6, "
    "citadelUpgradeEquipment_lvl1, citadelUpgradeEquipment_lvl2, citadelUpgradeEquipment_lvl3, citadelUpgradeEquipment_lvl4, citadelUpgradeEquipment_lvl5, citadelUpgradeEquipment_lvl6, "
    "citadelUpgradeColonist_lvl1, citadelUpgradeColonist_lvl2, citadelUpgradeColonist_lvl3, citadelUpgradeColonist_lvl4, citadelUpgradeColonist_lvl5, citadelUpgradeColonist_lvl6, "
    "maxColonist_ore, maxColonist_organics, maxColonist_equipment, "
    "fighters, fuelProduction, organicsProduction, equipmentProduction, fighterProduction, "
    "maxore, maxorganics, maxequipment, maxfighters, breeding) "
    "VALUES ('C','Glacial/Ice','Ice World', "
    "5,5,7,5,4,8, "
    "400,300,600,700,300,700, "
    "300,80,400,900,400,900, "
    "600,400,650,700,1000,1400, "
    "1000000,24000000,4400000,6600000,9000000,6600000, "
    "20000,50000,20000, " "0,0,0,0,0, " "20000,50000,10000,1000000,-0.10);",

  /* Earth planet in sector 1 */
  " INSERT OR IGNORE INTO planets (num, sector, name, owner_id, owner_type, population, type, creator, colonist, fighters) "
    " VALUES (1, 1, 'Earth', 0, 'player', 8000000000, 1, 'System', 18000000, 1000000); ",

  " INSERT OR IGNORE INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate) VALUES "
    " ((SELECT id FROM planets WHERE name='Earth'), 'ore', 10000000, 10000000, 0); ",
  " INSERT OR IGNORE INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate) VALUES "
    " ((SELECT id FROM planets WHERE name='Earth'), 'organics', 10000000, 10000000, 0); ",
  " INSERT OR IGNORE INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate) VALUES "
    " ((SELECT id FROM planets WHERE name='Earth'), 'equipment', 10000000, 10000000, 0); ",

  /* Ferringhi planet in sector 0 (change in bigbang) */
  " INSERT OR IGNORE INTO planets (num, sector, name, owner_id, owner_type, population, type, creator, colonist, fighters) "
    " VALUES (2, 0, 'Ferringhi Homeworld', 0, 'npc_faction', 8000000000, 1, 'System', 18000000, 1000000); ",

  " INSERT OR IGNORE INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate) VALUES "
    " ((SELECT id FROM planets WHERE name='Earth'), 'ore', 10000000, 10000000, 0); ",
  " INSERT OR IGNORE INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate) VALUES "
    " ((SELECT id FROM planets WHERE name='Earth'), 'organics', 10000000, 10000000, 0); ",
  " INSERT OR IGNORE INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate) VALUES "
    " ((SELECT id FROM planets WHERE name='Earth'), 'equipment', 10000000, 10000000, 0); ",
  " INSERT OR IGNORE INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate) VALUES "
    " ((SELECT id FROM planets WHERE name='Earth'), 'fuel', 100000, 100000, 0); ",

  /* NPC Planet: Orion Hideout (Contraband Outpost) */
  " INSERT OR IGNORE INTO planets (num, sector, name, owner_id, owner_type, population, type, creator, colonist, fighters) "
    " VALUES (3, 0, 'Orion Hideout', 0, 'npc_faction', 20000000, 1, 'Syndicate', 20010000, 200000); ",

  " /* Orion Hideout Commodity Stock and Capacity */ "
    " INSERT OR IGNORE INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate) VALUES "
    " ((SELECT id FROM planets WHERE name='Orion Hideout'), 'ore', 50000000, 50000000, 10); ",
  " INSERT OR IGNORE INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate) VALUES " " ((SELECT id FROM planets WHERE name='Orion Hideout'), 'organics', 100, 100, 0); ",	/* Near Zero Capacity for Organics */
  " INSERT OR IGNORE INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate) VALUES "
    " ((SELECT id FROM planets WHERE name='Orion Hideout'), 'equipment', 30000000, 30000000, 10); ",


  /* Fedspace sectors 1–10 */
  "INSERT OR IGNORE INTO sectors (id, name, beacon, nebulae) VALUES (1, 'Fedspace 1', 'The Federation -- Do Not Dump!', 'The Federation');",
  "INSERT OR IGNORE INTO sectors (id, name, beacon, nebulae) VALUES (2, 'Fedspace 2', 'The Federation -- Do Not Dump!', 'The Federation');",
  "INSERT OR IGNORE INTO sectors (id, name, beacon, nebulae) VALUES (3, 'Fedspace 3', 'The Federation -- Do Not Dump!', 'The Federation');",
  "INSERT OR IGNORE INTO sectors (id, name, beacon, nebulae) VALUES (4, 'Fedspace 4', 'The Federation -- Do Not Dump!', 'The Federation');",
  "INSERT OR IGNORE INTO sectors (id, name, beacon, nebulae) VALUES (5, 'Fedspace 5', 'The Federation -- Do Not Dump!', 'The Federation');",
  "INSERT OR IGNORE INTO sectors (id, name, beacon, nebulae) VALUES (6, 'Fedspace 6', 'The Federation -- Do Not Dump!', 'The Federation');",
  "INSERT OR IGNORE INTO sectors (id, name, beacon, nebulae) VALUES (7, 'Fedspace 7', 'The Federation -- Do Not Dump!', 'The Federation');",
  "INSERT OR IGNORE INTO sectors (id, name, beacon, nebulae) VALUES (8, 'Fedspace 8', 'The Federation -- Do Not Dump!', 'The Federation');",
  "INSERT OR IGNORE INTO sectors (id, name, beacon, nebulae) VALUES (9, 'Fedspace 9', 'The Federation -- Do Not Dump!', 'The Federation');",
  "INSERT OR IGNORE INTO sectors (id, name, beacon, nebulae) VALUES (10, 'Fedspace 10','The Federation -- Do Not Dump!', 'The Federation');",

  " INSERT OR IGNORE INTO ports (id, number, name, sector, size, techlevel, ore_on_hand, organics_on_hand, equipment_on_hand, petty_cash, type) "
    " VALUES (1, 1, 'Earth Port', 1, 10, 10, 10000, 10000, 10000, 0, 1); ",


  /* Fedspace warps (hard-coded) */
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (1,2);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (1,3);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (1,4);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (1,5);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (1,6);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (1,7);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (2,1);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (2,3);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (2,7);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (2,8);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (2,9);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (2,10);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (3,1);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (3,2);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (3,4);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (4,1);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (4,3);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (4,5);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (5,1);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (5,4);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (5,6);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (6,1);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (6,5);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (6,7);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (7,1);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (7,2);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (7,6);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (7,8);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (8,2);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (8,7);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (9,2);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (9,10);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (10,2);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (10,9);",


  "INSERT INTO shiptypes\n"
    "(name, basecost, maxattack, initialholds, maxholds, maxfighters, "
    " turns, maxmines, maxlimpets, maxgenesis, can_transwarp, transportrange, maxshields, "
    " offense, defense, maxbeacons, maxphotons, can_purchase)\n"
    " SELECT"
    " 'Mary Celeste Class', basecost, maxattack, initialholds, maxholds, maxfighters, "
    " turns, maxmines, maxlimpets, maxgenesis, can_transwarp, transportrange, maxshields, "
    " offense, defense, maxbeacons, maxphotons, 0\n"
    " FROM shiptypes "
    "WHERE name='Corporate Flagship'"
    " AND NOT EXISTS (SELECT 1 FROM shiptypes WHERE name='Mary Celeste Class');",


  "INSERT INTO npc_shipnames (id, name) VALUES\n"
    "(1, 'Starlight Voyager'),\n"
    "(2, 'Iron Sentinel'),\n"
    "(3, 'Crimson Horizon'),\n"
    "(4, 'The Unrelenting'),\n"
    "(5, 'Vanguard of Sol'),\n"
    "(6, 'Aether''s Echo'),\n"
    "(7, 'Voiddrifter'),\n"
    "(8, 'Celestia'),\n"
    "(9, 'The Final Word'),\n"
    "(10, 'Sovereign''s Might'),\n"
    "(11, 'The Silence'),\n"
    "(12, 'Ghost of Proxima'),\n"
    "(13, 'Harbinger of Ruin'),\n"
    "(14, 'Blackstar'),\n"
    "(15, 'Fallen Angel'),\n"
    "(16, 'Grave Digger'),\n"
    "(17, 'The Empty Sky'),\n"
    "(18, 'Cinderclaw'),\n"
    "(19, 'Whisper of the Abyss'),\n"
    "(20, 'The Nameless Dread'),\n"
    "(21, 'Not My Fault'),\n"
    "(22, 'Totally Not a Trap'),\n"
    "(23, 'The Gravitational Pull'),\n"
    "(24, 'Unlicensed & Uninsured'),\n"
    "(25, 'Ship Happens'),\n"
    "(26, 'The Loan Shark''s Repossession'),\n"
    "(27, 'Where Are We Going?'),\n"
    "(28, 'Taxes Included'),\n"
    "(29, 'Error 404: Ship Not Found'),\n"
    "(30, 'The Padded Cell'),\n"
    "(31, 'Quantum Leap'),\n"
    "(32, 'The Data Stream'),\n"
    "(33, 'Sub-Light Cruiser'),\n"
    "(34, 'Temporal Paradox'),\n"
    "(35, 'Neon Genesis'),\n"
    "(36, 'The Warp Core'),\n"
    "(37, 'The Nanite Swarm'),\n"
    "(38, 'Synthetic Dream'),\n"
    "(39, 'The Singularity'),\n"
    "(40, 'Blink Drive'),\n"
    "(41, 'The Last Endeavor'),\n"
    "(42, 'Odyssey''s End'),\n"
    "(43, 'The Magellan'),\n"
    "(44, 'Star''s Fury'),\n"
    "(45, 'Cosmic Drifter'),\n"
    "(46, 'The Old Dog'),\n"
    "(47, 'The Wayfinder'),\n"
    "(48, 'The Horizon Breaker'),\n"
    "(49, 'Stormchaser'),\n" "(50, 'Beyond the Veil');\n",

  "INSERT OR IGNORE INTO tavern_names (name, enabled, weight) VALUES ('The Rusty Flange', 1, 10);",
  "INSERT OR IGNORE INTO tavern_names (name, enabled, weight) VALUES ('The Starfall Inn', 1, 10);",
  "INSERT OR IGNORE INTO tavern_names (name, enabled, weight) VALUES ('Orions Den', 1, 5);",
  "INSERT OR IGNORE INTO tavern_names (name, enabled, weight) VALUES ('FedSpace Cantina', 1, 8);",

  "INSERT OR IGNORE INTO tavern_settings (id, max_bet_per_transaction, daily_max_wager, enable_dynamic_wager_limit, graffiti_max_posts, notice_expires_days, buy_round_cost, buy_round_alignment_gain, loan_shark_enabled)\n"
    "VALUES (1, 5000, 50000, 0, 100, 7, 1000, 5, 1);",

  "INSERT OR IGNORE INTO hardware_items (code, name, price, requires_stardock, sold_in_class0, max_per_ship, category, enabled) VALUES ('FIGHTERS', 'Fighters', 100, 0, 1, NULL, 'FIGHTER', 1);",
  "INSERT OR IGNORE INTO hardware_items (code, name, price, requires_stardock, sold_in_class0, max_per_ship, category, enabled) VALUES ('SHIELDS', 'Shields', 200, 0, 1, NULL, 'SHIELD', 1);",
  "INSERT OR IGNORE INTO hardware_items (code, name, price, requires_stardock, sold_in_class0, max_per_ship, category, enabled) VALUES ('HOLDS', 'Holds', 50, 0, 1, NULL, 'HOLD', 1);",
  "INSERT OR IGNORE INTO hardware_items (code, name, price, requires_stardock, sold_in_class0, max_per_ship, category, enabled) VALUES ('GENESIS', 'Genesis Torpedo', 25000, 1, 0, NULL, 'SPECIAL', 1);",
  "INSERT OR IGNORE INTO hardware_items (code, name, price, requires_stardock, sold_in_class0, max_per_ship, category, enabled) VALUES ('DETONATOR', 'Atomic Detonator', 10000, 1, 0, NULL, 'SPECIAL', 1);",
  "INSERT OR IGNORE INTO hardware_items (code, name, price, requires_stardock, sold_in_class0, max_per_ship, category, enabled) VALUES ('CLOAK', 'Cloaking Device', 50000, 1, 0, 1, 'MODULE', 1);",
  "INSERT OR IGNORE INTO hardware_items (code, name, price, requires_stardock, sold_in_class0, max_per_ship, category, enabled) VALUES ('LSCANNER', 'Long-Range Scanner', 30000, 1, 0, 1, 'MODULE', 1);",
  "INSERT OR IGNORE INTO hardware_items (code, name, price, requires_stardock, sold_in_class0, max_per_ship, category, enabled) VALUES ('PSCANNER', 'Planet Scanner', 15000, 1, 0, 1, 'MODULE', 1);",
  "INSERT OR IGNORE INTO hardware_items (code, name, price, requires_stardock, sold_in_class0, max_per_ship, category, enabled) VALUES ('TWARP', 'TransWarp Drive', 100000, 1, 0, 1, 'MODULE', 1);",

  /* fix a problem with the terraforming cron */
  "ALTER TABLE planets ADD COLUMN terraform_turns_left INTEGER NOT NULL DEFAULT 1;",

  "INSERT INTO ships (name, type_id, attack, holds, mines, limpets, fighters, genesis, photons, sector, shields, beacons, colonists, equipment, organics, ore, flags, cloaking_devices, cloaked, ported, onplanet) "
    "VALUES ('Bit Banger', 1, 110, 20, 25, 5, 2300, 5, 1, 1, 400, 10, 5, 5, 5, 5, 0, 5, NULL, 1, 1);",

  "INSERT INTO players (number, name, passwd, sector, ship, type) VALUES (1, 'System', 'BOT',1,1,1);",
  "INSERT INTO players (number, name, passwd, sector, ship, type) VALUES (1, 'Federation Administrator', 'BOT',1,1,1);",
  "INSERT INTO players (number, name, passwd, sector, ship, type) VALUES (7, 'newguy', 'pass123',1,1,2);",

  /* ---- AI QA Bot Test Player ---- */
  "INSERT INTO ships (name, type_id, attack, holds, fighters, shields, sector, onplanet, ported) SELECT 'QA Bot 1', T.id, T.maxattack, T.maxholds, T.maxfighters, T.maxshields, 1, 0, 0 FROM shiptypes T WHERE T.name = 'Scout Marauder';",
  "INSERT INTO players (number, name, passwd, sector, credits, type) VALUES (8, 'ai_qa_bot', 'quality', 1, 10000, 2);",
  "UPDATE players SET ship = (SELECT id FROM ships WHERE name = 'QA Bot 1') WHERE name = 'ai_qa_bot';",
  "INSERT INTO ship_ownership (player_id, ship_id, is_primary, role_id) VALUES ((SELECT id FROM players WHERE name = 'ai_qa_bot'), (SELECT id FROM ships WHERE name = 'QA Bot 1'), 1, 1);",
  /* ----------------------------- */

  "INSERT INTO ship_ownership (player_id, ship_id, is_primary, role_id) VALUES (1,1,1,0);",
  "INSERT INTO player_types (description) VALUES ('NPC');"
    "INSERT INTO player_types (description) VALUES ('Human Player');"
    /* ------------------------------------------------------------------------------------- */
    /* ------------------------------------------------------------------------------------- */
    /* 1. Insert Orion Syndicate Ship Types (Disabled for Purchase) - NEW COLUMNS APPLIED */
    /* ------------------------------------------------------------------------------------- */
    //// see above
/* ------------------------------------------------------------------------------------- */
/* 2. Insert five maxed-out Orion Syndicate ships (FULL SCHEMA APPLIED) */
/* ------------------------------------------------------------------------------------- */
    " INSERT INTO ships ( "
    "  name, type_id, attack, holds, mines, limpets, fighters, genesis, photons, sector, shields, beacons, colonists, equipment, organics, ore, flags, cloaking_devices, cloaked, ported, onplanet "
    " ) "
    " SELECT "
    "  'Orion Heavy Fighter Alpha', T.id, 0, T.maxholds, T.maxmines, 0, T.maxfighters, T.maxgenesis, T.maxphotons, P.sector, T.maxshields, T.maxbeacons, 0, 0, 0, 0, 0, 0, NULL, 0, 0 "
    " FROM shiptypes T, planets P "
    " WHERE P.num=3 AND T.name='Orion Heavy Fighter Patrol' "
    " UNION ALL "
    " SELECT "
    "  'Orion Scout Gamma', T.id, 0, T.maxholds, T.maxmines, 0, T.maxfighters, T.maxgenesis, T.maxphotons, P.sector, T.maxshields, T.maxbeacons, 0, 0, 0, 0, 0, 0, NULL, 0, 0 "
    " FROM shiptypes T, planets P "
    " WHERE P.num=3 AND T.name='Orion Scout/Looter' "
    " UNION ALL "
    " SELECT "
    "  'Orion Contraband Delta', T.id, 0, T.maxholds, T.maxmines, 0, T.maxfighters, T.maxgenesis, T.maxphotons, P.sector, T.maxshields, T.maxbeacons, 0, 0, 0, 0, 0, 0, NULL, 0, 0 "
    " FROM shiptypes T, planets P "
    " WHERE P.num=3 AND T.name='Orion Contraband Runner' "
    " UNION ALL "
    " SELECT "
    "  'Orion Smuggler Beta', T.id, 0, T.maxholds, T.maxmines, 0, T.maxfighters, T.maxgenesis, T.maxphotons, P.sector, T.maxshields, T.maxbeacons, 0, 0, 0, 0, 0, 0, NULL, 0, 0 "
    " FROM shiptypes T, planets P "
    " WHERE P.num=3 AND T.name='Orion Smuggler''s Kiss' "
    " UNION ALL "
    " SELECT "
    "  'Orion Guard Epsilon', T.id, 0, T.maxholds, T.maxmines, 0, T.maxfighters, T.maxgenesis, T.maxphotons, P.sector, T.maxshields, T.maxbeacons, 0, 0, 0, 0, 0, 0, NULL, 0, 0 "
    " FROM shiptypes T, planets P "
    " WHERE P.num=3 AND T.name='Orion Black Market Guard';"
    /* ------------------------------------------------------------------------------------- */
    /* 3. Insert 5 Orion Captains and assign ships (FIXED: players columns and ship ownership) */
    /* ------------------------------------------------------------------------------------- */
    /* Insert 5 Orion Captains (Players) - Removed non-schema columns (faction_id, empire, turns) */
    "INSERT INTO players (type, name, passwd, sector, experience, alignment, credits) "
    "SELECT 1, 'Zydras, Heavy Fighter Captain', '', P.sector, 100, -100, 1000 FROM planets P WHERE P.num=3 UNION ALL "
    "SELECT 1, 'Krell, Scout Captain', '', P.sector, 100, -100, 1000 FROM planets P WHERE P.num=3 UNION ALL "
    "SELECT 1, 'Vex, Contraband Captain', '', P.sector, 100, -100, 1000 FROM planets P WHERE P.num=3 UNION ALL "
    "SELECT 1, 'Jaxx, Smuggler Captain', '', P.sector, 100, -100, 1000 FROM planets P WHERE P.num=3 UNION ALL "
    "SELECT 1, 'Sira, Market Guard Captain', '', P.sector, 100, -100, 1000 FROM planets P WHERE P.num=3;"
    /* Link Players to Ships using the players.ship column (ships table has no owner_id) */
    "UPDATE players SET ship = (SELECT id FROM ships WHERE name='Orion Heavy Fighter Alpha') WHERE name='Zydras, Heavy Fighter Captain';"
    "UPDATE players SET ship = (SELECT id FROM ships WHERE name='Orion Scout Gamma') WHERE name='Krell, Scout Captain';"
    "UPDATE players SET ship = (SELECT id FROM ships WHERE name='Orion Contraband Delta') WHERE name='Vex, Contraband Captain';"
    "UPDATE players SET ship = (SELECT id FROM ships WHERE name='Orion Smuggler Beta') WHERE name='Jaxx, Smuggler Captain';"
    "UPDATE players SET ship = (SELECT id FROM ships WHERE name='Orion Guard Epsilon') WHERE name='Sira, Market Guard Captain';"
    /* ------------------------------------------------------------------------------------- */
    /*  -- 1. Create the Orion Syndicate Corporation */
    "INSERT INTO corporations (name, owner_id, tag) VALUES "
    "('Orion Syndicate',4, 'ORION');",

  /* -- 2. Create the Ferrengi Alliance Corporation */
  "INSERT INTO corporations (name, owner_id, tag) VALUES "
    "('Ferrengi Alliance', 9, 'FENG');"
    /* Ferringhi Homeworld (Planet ID 2) Commodities */
    /* -- Ore (High Capacity) */
    "INSERT INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate) VALUES "
    " (2, 'ore', 10000000, 10000000, 0);",

  /* -- Organics (Very High Capacity/Focus) */
  "INSERT INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate) VALUES "
    " (2, 'organics', 50000000, 50000000, 10);",

  /* -- Equipment (High Capacity) */
  " INSERT INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate) VALUES "
    " (2, 'equipment', 10000000, 10000000, 0);",

  "INSERT INTO turns (player, turns_remaining, last_update)"
    "SELECT "
    "    id, "
    "    100, "
    "    strftime('%s', 'now') "
    "FROM " "    players " "WHERE " "    type = 2; "
    ///////////////////////
    " CREATE TABLE IF NOT EXISTS currencies (  "
    "   code TEXT PRIMARY KEY,  "
    "   name TEXT NOT NULL,  "
    "   minor_unit INTEGER NOT NULL DEFAULT 1 CHECK (minor_unit > 0),  "
    "   is_default INTEGER NOT NULL DEFAULT 0 CHECK (is_default IN (0,1))  "
    " );  "
    " INSERT OR IGNORE INTO currencies(code, name, minor_unit, is_default)  "
    " VALUES ('CRD','Galactic Credits',1,1);  "
    " CREATE TABLE IF NOT EXISTS commodities (  "
    "   id INTEGER PRIMARY KEY,  "
    "   code TEXT UNIQUE NOT NULL,  "
    "   name TEXT NOT NULL,  "
    "   illegal INTEGER NOT NULL DEFAULT 0,  "
    "   base_price INTEGER NOT NULL DEFAULT 0 CHECK (base_price >= 0),  "
    "   volatility INTEGER NOT NULL DEFAULT 0 CHECK (volatility >= 0)  "
    " );  "
    " INSERT OR IGNORE INTO commodities (code, name, base_price, volatility)  "
    " VALUES  "
    "   ('ORE', 'Ore', 100, 20),  "
    "   ('ORG', 'Organics', 150, 30),  "
    "   ('EQU', 'Equipment', 200, 25);  "
    " INSERT OR IGNORE INTO commodities (code, name, base_price, volatility, illegal)  "
    " VALUES  "
    "   ('SLV', 'Slaves', 1000, 50, 1),  "
    "   ('WPN', 'Weapons', 750, 40, 1),  "
    "   ('DRG', 'Drugs', 500, 60, 1);  "
    " CREATE TABLE IF NOT EXISTS commodity_orders (  "
    "   id INTEGER PRIMARY KEY,  "
    "   actor_type TEXT NOT NULL CHECK (actor_type IN ('player','corp','npc_planet','port')),  "
    "   actor_id INTEGER NOT NULL,  "
    "   location_type TEXT NOT NULL CHECK (location_type IN ('planet','port')),  "
    "   location_id INTEGER NOT NULL,  "
    "   commodity_id INTEGER NOT NULL REFERENCES commodities(id) ON DELETE CASCADE,  "
    "   side TEXT NOT NULL CHECK (side IN ('buy','sell')),  "
    "   quantity INTEGER NOT NULL CHECK (quantity > 0),  "
    "   price INTEGER NOT NULL CHECK (price >= 0),  "
    "   status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','filled','cancelled','expired')),  "
    "   ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))  "
    " );  "
    " CREATE INDEX IF NOT EXISTS idx_commodity_orders_comm ON commodity_orders(commodity_id, status);  "
    " CREATE TABLE IF NOT EXISTS commodity_trades (  "
    "   id INTEGER PRIMARY KEY,  "
    "   commodity_id INTEGER NOT NULL REFERENCES commodities(id) ON DELETE CASCADE,  "
    "   buyer_actor_type TEXT NOT NULL CHECK (buyer_actor_type IN ('player','corp','npc_planet','port')),  "
    "   buyer_actor_id INTEGER NOT NULL,  "
    "   buyer_location_type TEXT NOT NULL CHECK (buyer_location_type IN ('planet','port')),  "
    "   buyer_location_id INTEGER NOT NULL,  "
    "   seller_actor_type TEXT NOT NULL CHECK (seller_actor_type IN ('player','corp','npc_planet','port')),  "
    "   seller_actor_id INTEGER NOT NULL,  "
    "   seller_location_type TEXT NOT NULL CHECK (seller_location_type IN ('planet','port')),  "
    "   seller_location_id INTEGER NOT NULL,  "
    "   quantity INTEGER NOT NULL CHECK (quantity > 0),  "
    "   price INTEGER NOT NULL CHECK (price >= 0),  "
    "   ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  "
    "   settlement_tx_buy INTEGER,  "
    "   settlement_tx_sell INTEGER  "
    " );  "
    " CREATE TABLE IF NOT EXISTS bank_accounts (  "
    "   id INTEGER PRIMARY KEY,  "
    "   owner_type TEXT NOT NULL,  "
    "   owner_id INTEGER NOT NULL,  "
    "   currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code),  "
    "   balance INTEGER NOT NULL DEFAULT 0 CHECK (balance >= 0),  "
    "   interest_rate_bp INTEGER NOT NULL DEFAULT 0,  "
    "   last_interest_tick INTEGER,  "
    "   tx_alert_threshold INTEGER DEFAULT 0,  "
    "   is_active INTEGER NOT NULL DEFAULT 1  "
    " );  "
    " CREATE UNIQUE INDEX idx_bank_accounts_owner ON bank_accounts(owner_type, owner_id, currency);  "
    " CREATE TABLE IF NOT EXISTS bank_transactions (  "
    "   id INTEGER PRIMARY KEY,  "
    "   account_id INTEGER NOT NULL REFERENCES bank_accounts(id),  "
    "   tx_type TEXT NOT NULL CHECK (tx_type IN (  "
    "     'DEPOSIT',  "
    "     'WITHDRAWAL',  "
    "     'TRANSFER',  "
    "     'INTEREST',  "
    "     'FEE',  "
    "     'WIRE',  "
    "     'TAX',  "
    "     'TRADE_BUY',  "
    "     'TRADE_SELL',  "
    "     'TRADE_BUY_FEE',  "
    "     'TRADE_SELL_FEE',  "
    "     'WITHDRAWAL_FEE',  "
    "     'ADJUSTMENT'  "
    "   )),  "
    "   direction TEXT NOT NULL CHECK(direction IN ('CREDIT','DEBIT')),  "
    "   amount INTEGER NOT NULL CHECK (amount > 0),  "
    "   currency TEXT NOT NULL,  "
    "   tx_group_id TEXT,  "
    "   related_account_id INTEGER,  "
    "   description TEXT,  "
    "   ts INTEGER NOT NULL,  "
    "   balance_after INTEGER DEFAULT 0,  "
    "   idempotency_key TEXT,  "
    "   engine_event_id INTEGER  "
    " );  "
    " CREATE INDEX IF NOT EXISTS idx_bank_transactions_account_ts ON bank_transactions(account_id, ts DESC);  "
    " CREATE INDEX IF NOT EXISTS idx_bank_transactions_tx_group ON bank_transactions(tx_group_id);  "
    " CREATE UNIQUE INDEX IF NOT EXISTS idx_bank_transactions_idem ON bank_transactions(account_id, idempotency_key) WHERE idempotency_key IS NOT NULL;  "
    " CREATE TRIGGER IF NOT EXISTS trg_bank_transactions_balance_after  "
    " AFTER INSERT ON bank_transactions  "
    " FOR EACH ROW  "
    " BEGIN  "
    "   UPDATE bank_transactions  "
    "   SET balance_after = (SELECT balance FROM bank_accounts WHERE id = NEW.account_id)  "
    "   WHERE id = NEW.id;  " " END; ",

  " CREATE TABLE IF NOT EXISTS bank_fee_schedules (  "
    "   id INTEGER PRIMARY KEY,  "
    "   tx_type TEXT NOT NULL,  "
    "   fee_code TEXT NOT NULL,  "
    "   owner_type TEXT,  "
    "   currency TEXT NOT NULL DEFAULT 'CRD',  "
    "   value INTEGER NOT NULL,  "
    "   is_percentage INTEGER NOT NULL DEFAULT 0 CHECK (is_percentage IN (0,1)),  "
    "   min_tx_amount INTEGER DEFAULT 0,  "
    "   max_tx_amount INTEGER,  "
    "   effective_from INTEGER NOT NULL,  "
    "   effective_to INTEGER  "
    " );  "
    " CREATE INDEX IF NOT EXISTS idx_bank_fee_active ON bank_fee_schedules(tx_type, owner_type, currency, effective_from, effective_to);  "
    " CREATE TABLE IF NOT EXISTS bank_interest_policy (  "
    "   id INTEGER PRIMARY KEY CHECK (id = 1),  "
    "   apr_bps INTEGER NOT NULL DEFAULT 0 CHECK (apr_bps >= 0),  "
    "   min_balance INTEGER NOT NULL DEFAULT 0 CHECK (min_balance >= 0),  "
    "   max_balance INTEGER NOT NULL DEFAULT 9223372036854775807,  "
    "   last_run_at TEXT,  "
    "   currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code)  "
    " );  "
    " INSERT OR IGNORE INTO bank_interest_policy (id, apr_bps, min_balance, max_balance, last_run_at, currency)  "
    " VALUES (1, 0, 0, 9223372036854775807, NULL, 'CRD');  "
    " CREATE TABLE IF NOT EXISTS bank_orders (  "
    "   id INTEGER PRIMARY KEY,  "
    "   player_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE,  "
    "   kind TEXT NOT NULL CHECK (kind IN ('recurring','once')),  "
    "   schedule TEXT NOT NULL,  "
    "   next_run_at TEXT,  "
    "   enabled INTEGER NOT NULL DEFAULT 1 CHECK (enabled IN (0,1)),  "
    "   amount INTEGER NOT NULL CHECK (amount > 0),  "
    "   currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code),  "
    "   to_entity TEXT NOT NULL CHECK (to_entity IN ('player','corp','gov','npc')),  "
    "   to_id INTEGER NOT NULL,  "
    "   memo TEXT  "
    " );  "
    " CREATE TABLE IF NOT EXISTS bank_flags (  "
    "   player_id INTEGER PRIMARY KEY REFERENCES players(id) ON DELETE CASCADE,  "
    "   is_frozen INTEGER NOT NULL DEFAULT 0 CHECK (is_frozen IN (0,1)),  "
    "   risk_tier TEXT NOT NULL DEFAULT 'normal' CHECK (risk_tier IN ('normal','elevated','high','blocked'))  "
    " );  "
    " CREATE TRIGGER IF NOT EXISTS trg_bank_transactions_before_insert  "
    " BEFORE INSERT ON bank_transactions  "
    " FOR EACH ROW  "
    " BEGIN  "
    "   SELECT CASE  "
    "     WHEN NEW.direction = 'DEBIT'  "
    "       AND (SELECT balance FROM bank_accounts WHERE id = NEW.account_id) - NEW.amount < 0  "
    "     THEN RAISE(ABORT, 'BANK_INSUFFICIENT_FUNDS')  "
    "     ELSE 1  "
    "   END;  "
    " END;  "
    " CREATE TRIGGER IF NOT EXISTS trg_bank_transactions_after_insert  "
    " AFTER INSERT ON bank_transactions  "
    " FOR EACH ROW  "
    " BEGIN  "
    "   UPDATE bank_accounts  "
    "   SET balance = CASE NEW.direction  "
    "                   WHEN 'DEBIT'  THEN balance - NEW.amount  "
    "                   WHEN 'CREDIT' THEN balance + NEW.amount  "
    "                   ELSE balance  "
    "                 END  "
    "   WHERE id = NEW.account_id;  "
    "   UPDATE bank_transactions  "
    "   SET balance_after = (SELECT balance FROM bank_accounts WHERE id = NEW.account_id)  "
    "   WHERE id = NEW.id;  "
    " END;  "
    " CREATE TRIGGER IF NOT EXISTS trg_bank_transactions_before_delete  "
    " BEFORE DELETE ON bank_transactions  "
    " FOR EACH ROW  "
    " BEGIN  "
    "   SELECT RAISE(ABORT, 'BANK_LEDGER_APPEND_ONLY');  "
    " END;  "
    " CREATE TABLE IF NOT EXISTS corp_accounts (  "
    "   corp_id INTEGER PRIMARY KEY REFERENCES corps(id) ON DELETE CASCADE,  "
    "   currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code),  "
    "   balance INTEGER NOT NULL DEFAULT 0 CHECK (balance >= 0),  "
    "   last_interest_at TEXT  "
    " );  "
    " CREATE TABLE IF NOT EXISTS corp_tx (  "
    "   id INTEGER PRIMARY KEY,  "
    "   corp_id INTEGER NOT NULL REFERENCES corps(id) ON DELETE CASCADE,  "
    "   ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  "
    "   kind TEXT NOT NULL CHECK (kind IN (  "
    "     'deposit',  "
    "     'withdraw',  "
    "     'transfer_in',  "
    "     'transfer_out',  "
    "     'interest',  "
    "     'dividend',  "
    "     'salary',  "
    "     'adjustment'  "
    "   )),  "
    "   amount INTEGER NOT NULL CHECK (amount > 0),  "
    "   balance_after INTEGER,  "
    "   currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code),  "
    "   memo TEXT,  "
    "   idempotency_key TEXT UNIQUE  "
    " );  "
    " CREATE INDEX IF NOT EXISTS idx_corp_tx_corp_ts ON corp_tx(corp_id, ts);  "
    " CREATE TABLE IF NOT EXISTS corp_interest_policy (  "
    "   id INTEGER PRIMARY KEY CHECK (id = 1),  "
    "   apr_bps INTEGER NOT NULL DEFAULT 0 CHECK (apr_bps >= 0),  "
    "   compounding TEXT NOT NULL DEFAULT 'none' CHECK (compounding IN ('none','daily','weekly','monthly')),  "
    "   last_run_at TEXT,  "
    "   currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code)  "
    " );  "
    " INSERT OR IGNORE INTO corp_interest_policy (id, apr_bps, compounding, last_run_at, currency)  "
    " VALUES (1, 0, 'none', NULL, 'CRD');  "
    " CREATE TRIGGER IF NOT EXISTS trg_corp_tx_before_insert  "
    " BEFORE INSERT ON corp_tx  "
    " FOR EACH ROW  "
    " BEGIN  "
    "   INSERT OR IGNORE INTO corp_accounts(corp_id, currency, balance, last_interest_at)  "
    "   VALUES (NEW.corp_id, COALESCE(NEW.currency,'CRD'), 0, NULL);  "
    "   SELECT CASE  "
    "     WHEN NEW.kind IN ('withdraw','transfer_out','dividend','salary')  "
    "       AND (SELECT balance FROM corp_accounts WHERE corp_id = NEW.corp_id) - NEW.amount < 0  "
    "     THEN RAISE(ABORT, 'CORP_INSUFFICIENT_FUNDS')  "
    "     ELSE 1  "
    "   END;  "
    " END;  "
    " CREATE TRIGGER IF NOT EXISTS trg_corp_tx_after_insert  "
    " AFTER INSERT ON corp_tx  "
    " FOR EACH ROW  "
    " BEGIN  "
    "   UPDATE corp_accounts  "
    "   SET balance = CASE NEW.kind  "
    "                   WHEN 'withdraw'     THEN balance - NEW.amount  "
    "                   WHEN 'transfer_out' THEN balance - NEW.amount  "
    "                   WHEN 'dividend'     THEN balance - NEW.amount  "
    "                   WHEN 'salary'       THEN balance - NEW.amount  "
    "                   ELSE balance + NEW.amount  "
    "                 END  "
    "   WHERE corp_id = NEW.corp_id;  "
    "   UPDATE corp_tx  "
    "   SET balance_after = (SELECT balance FROM corp_accounts WHERE corp_id = NEW.corp_id)  "
    "   WHERE id = NEW.id;  "
    " END;  "
    " CREATE TABLE IF NOT EXISTS stocks (  "
    "   id INTEGER PRIMARY KEY,  "
    "   corp_id INTEGER NOT NULL REFERENCES corporations(id) ON DELETE CASCADE,  "
    "   ticker TEXT NOT NULL UNIQUE,  "
    "   total_shares INTEGER NOT NULL CHECK (total_shares > 0),  "
    "   par_value INTEGER NOT NULL DEFAULT 0 CHECK (par_value >= 0),  "
    "   current_price INTEGER NOT NULL DEFAULT 0 CHECK (current_price >= 0),  "
    "   last_dividend_ts TEXT  "
    " );  "
    " CREATE TABLE IF NOT EXISTS corp_shareholders ( "
    "   corp_id INTEGER NOT NULL REFERENCES corporations(id) ON DELETE CASCADE, "
    "   player_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE, "
    "   shares INTEGER NOT NULL CHECK (shares >= 0), "
    "   PRIMARY KEY (corp_id, player_id) "
    " );  "
    " CREATE TABLE IF NOT EXISTS stock_orders (  "
    "   id INTEGER PRIMARY KEY,  "
    "   player_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE,  "
    "   stock_id INTEGER NOT NULL REFERENCES stocks(id) ON DELETE CASCADE,  "
    "   type TEXT NOT NULL CHECK (type IN ('buy','sell')),  "
    "   quantity INTEGER NOT NULL CHECK (quantity > 0),  "
    "   price INTEGER NOT NULL CHECK (price >= 0),  "
    "   status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','filled','cancelled','expired')),  "
    "   ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))  "
    " );  "
    " CREATE INDEX IF NOT EXISTS idx_stock_orders_stock ON stock_orders(stock_id, status);  "
    " CREATE TABLE IF NOT EXISTS stock_trades (  "
    "   id INTEGER PRIMARY KEY,  "
    "   stock_id INTEGER NOT NULL REFERENCES stocks(id) ON DELETE CASCADE,  "
    "   buyer_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE,  "
    "   seller_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE,  "
    "   quantity INTEGER NOT NULL CHECK (quantity > 0),  "
    "   price INTEGER NOT NULL CHECK (price >= 0),  "
    "   ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  "
    "   settlement_tx_buy INTEGER,  "
    "   settlement_tx_sell INTEGER  "
    " );  "
    " CREATE TABLE IF NOT EXISTS stock_dividends (  "
    "   id INTEGER PRIMARY KEY,  "
    "   stock_id INTEGER NOT NULL REFERENCES stocks(id) ON DELETE CASCADE,  "
    "   amount_per_share INTEGER NOT NULL CHECK (amount_per_share >= 0),  "
    "   declared_ts TEXT NOT NULL,  "
    "   paid_ts TEXT  "
    " );  "
    " CREATE TABLE IF NOT EXISTS stock_indices (  "
    "   id INTEGER PRIMARY KEY,  "
    "   name TEXT UNIQUE NOT NULL  "
    " );  "
    " CREATE TABLE IF NOT EXISTS stock_index_members (  "
    "   index_id INTEGER NOT NULL REFERENCES stock_indices(id) ON DELETE CASCADE,  "
    "   stock_id INTEGER NOT NULL REFERENCES stocks(id) ON DELETE CASCADE,  "
    "   weight REAL NOT NULL DEFAULT 1.0,  "
    "   PRIMARY KEY (index_id, stock_id)  "
    " );  "
    " CREATE TABLE IF NOT EXISTS insurance_funds (  "
    "   id INTEGER PRIMARY KEY,  "
    "   owner_type TEXT NOT NULL CHECK (owner_type IN ('system','corp','player')),  "
    "   owner_id INTEGER,  "
    "   balance INTEGER NOT NULL DEFAULT 0 CHECK (balance >= 0)  "
    " );  "
    " CREATE TABLE IF NOT EXISTS insurance_policies (  "
    "   id INTEGER PRIMARY KEY,  "
    "   holder_type TEXT NOT NULL CHECK (holder_type IN ('player','corp')),  "
    "   holder_id INTEGER NOT NULL,  "
    "   subject_type TEXT NOT NULL CHECK (subject_type IN ('ship','cargo','planet')),  "
    "   subject_id INTEGER NOT NULL,  "
    "   premium INTEGER NOT NULL CHECK (premium >= 0),  "
    "   payout INTEGER NOT NULL CHECK (payout >= 0),  "
    "   fund_id INTEGER REFERENCES insurance_funds(id) ON DELETE SET NULL,  "
    "   start_ts TEXT NOT NULL,  "
    "   expiry_ts TEXT,  "
    "   active INTEGER NOT NULL DEFAULT 1 CHECK (active IN (0,1))  "
    " );  "
    " CREATE INDEX IF NOT EXISTS idx_policies_holder ON insurance_policies(holder_type, holder_id);  "
    " CREATE TABLE IF NOT EXISTS insurance_claims (  "
    "   id INTEGER PRIMARY KEY,  "
    "   policy_id INTEGER NOT NULL REFERENCES insurance_policies(id) ON DELETE CASCADE,  "
    "   event_id TEXT,  "
    "   amount INTEGER NOT NULL CHECK (amount >= 0),  "
    "   status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','paid','denied')),  "
    "   ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  "
    "   paid_bank_tx INTEGER  "
    " );  "
    " CREATE TABLE IF NOT EXISTS risk_profiles (  "
    "   id INTEGER PRIMARY KEY,  "
    "   entity_type TEXT NOT NULL CHECK (entity_type IN ('player','corp')),  "
    "   entity_id INTEGER NOT NULL,  "
    "   risk_score INTEGER NOT NULL DEFAULT 0  "
    " );  "
    " CREATE TABLE IF NOT EXISTS loans (  "
    "   id INTEGER PRIMARY KEY,  "
    "   lender_type TEXT NOT NULL CHECK (lender_type IN ('player','corp','bank')),  "
    "   lender_id INTEGER,  "
    "   borrower_type TEXT NOT NULL CHECK (borrower_type IN ('player','corp')),  "
    "   borrower_id INTEGER NOT NULL,  "
    "   principal INTEGER NOT NULL CHECK (principal > 0),  "
    "   rate_bps INTEGER NOT NULL DEFAULT 0 CHECK (rate_bps >= 0),  "
    "   term_days INTEGER NOT NULL CHECK (term_days > 0),  "
    "   next_due TEXT,  "
    "   status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active','paid','defaulted','written_off')),  "
    "   created_ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))  "
    " );  "
    " CREATE TABLE IF NOT EXISTS loan_payments (  "
    "   id INTEGER PRIMARY KEY,  "
    "   loan_id INTEGER NOT NULL REFERENCES loans(id) ON DELETE CASCADE,  "
    "   ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  "
    "   amount INTEGER NOT NULL CHECK (amount > 0),  "
    "   status TEXT NOT NULL DEFAULT 'posted' CHECK (status IN ('posted','reversed')),  "
    "   bank_tx_id INTEGER  "
    " );  "
    " CREATE TABLE IF NOT EXISTS collateral (  "
    "   id INTEGER PRIMARY KEY,  "
    "   loan_id INTEGER NOT NULL REFERENCES loans(id) ON DELETE CASCADE,  "
    "   asset_type TEXT NOT NULL CHECK (asset_type IN ('ship','planet','cargo','stock','other')),  "
    "   asset_id INTEGER NOT NULL,  "
    "   appraised_value INTEGER NOT NULL DEFAULT 0 CHECK (appraised_value >= 0)  "
    " );  "
    " CREATE TABLE IF NOT EXISTS credit_ratings (  "
    "   entity_type TEXT NOT NULL CHECK (entity_type IN ('player','corp')),  "
    "   entity_id INTEGER NOT NULL,  "
    "   score INTEGER NOT NULL DEFAULT 600 CHECK (score BETWEEN 300 AND 900),  "
    "   last_update TEXT,  "
    "   PRIMARY KEY (entity_type, entity_id)  "
    " );  "
    " CREATE TABLE IF NOT EXISTS charters (  "
    "   id INTEGER PRIMARY KEY,  "
    "   name TEXT NOT NULL UNIQUE,  "
    "   granted_by TEXT NOT NULL DEFAULT 'federation',  "
    "   monopoly_scope TEXT,  "
    "   start_ts TEXT NOT NULL,  "
    "   expiry_ts TEXT  "
    " );  "
    " CREATE TABLE IF NOT EXISTS expeditions (  "
    "   id INTEGER PRIMARY KEY,  "
    "   leader_player_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE,  "
    "   charter_id INTEGER REFERENCES charters(id) ON DELETE SET NULL,  "
    "   goal TEXT NOT NULL,  "
    "   target_region TEXT,  "
    "   pledged_total INTEGER NOT NULL DEFAULT 0 CHECK (pledged_total >= 0),  "
    "   duration_days INTEGER NOT NULL DEFAULT 7 CHECK (duration_days > 0),  "
    "   status TEXT NOT NULL DEFAULT 'planning' CHECK (status IN ('planning','launched','complete','failed','aborted')),  "
    "   created_ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))  "
    " );  "
    " CREATE TABLE IF NOT EXISTS expedition_backers (  "
    "   expedition_id INTEGER NOT NULL REFERENCES expeditions(id) ON DELETE CASCADE,  "
    "   backer_type TEXT NOT NULL CHECK (backer_type IN ('player','corp')),  "
    "   backer_id INTEGER NOT NULL,  "
    "   pledged_amount INTEGER NOT NULL CHECK (pledged_amount >= 0),  "
    "   share_pct REAL NOT NULL CHECK (share_pct >= 0),  "
    "   PRIMARY KEY (expedition_id, backer_type, backer_id)  "
    " );  "
    " CREATE TABLE IF NOT EXISTS expedition_returns (  "
    "   id INTEGER PRIMARY KEY,  "
    "   expedition_id INTEGER NOT NULL REFERENCES expeditions(id) ON DELETE CASCADE,  "
    "   ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  "
    "   amount INTEGER NOT NULL CHECK (amount >= 0),  "
    "   bank_tx_id INTEGER  "
    " );  "
    " CREATE TABLE IF NOT EXISTS commodity_orders (  "
    "   id INTEGER PRIMARY KEY,  "
    "   actor_type TEXT NOT NULL CHECK (actor_type IN ('player','corp','npc','port')),  "
    "   actor_id INTEGER NOT NULL,  "
    "   commodity_id INTEGER NOT NULL REFERENCES commodities(id) ON DELETE CASCADE,  "
    "   side TEXT NOT NULL CHECK (side IN ('buy','sell')),  "
    "   quantity INTEGER NOT NULL CHECK (quantity > 0),  "
    "   price INTEGER NOT NULL CHECK (price >= 0),  "
    "   status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','filled','cancelled','expired')),  "
    "   ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))  "
    " );  "
    " CREATE INDEX IF NOT EXISTS idx_commodity_orders_comm ON commodity_orders(commodity_id, status);  "
    " CREATE TABLE IF NOT EXISTS commodity_trades (  "
    "   id INTEGER PRIMARY KEY,  "
    "   commodity_id INTEGER NOT NULL REFERENCES commodities(id) ON DELETE CASCADE,  "
    "   buyer_type TEXT NOT NULL CHECK (buyer_type IN ('player','corp','npc','port')),  "
    "   buyer_id INTEGER NOT NULL,  "
    "   seller_type TEXT NOT NULL CHECK (seller_type IN ('player','corp','npc','port')),  "
    "   seller_id INTEGER NOT NULL,  "
    "   quantity INTEGER NOT NULL CHECK (quantity > 0),  "
    "   price INTEGER NOT NULL CHECK (price >= 0),  "
    "   ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  "
    "   settlement_tx_buy INTEGER,  "
    "   settlement_tx_sell INTEGER  "
    " );  "
    " CREATE TABLE IF NOT EXISTS futures_contracts (  "
    "   id INTEGER PRIMARY KEY,  "
    "   commodity_id INTEGER NOT NULL REFERENCES commodities(id) ON DELETE CASCADE,  "
    "   buyer_type TEXT NOT NULL CHECK (buyer_type IN ('player','corp')),  "
    "   buyer_id INTEGER NOT NULL,  "
    "   seller_type TEXT NOT NULL CHECK (seller_type IN ('player','corp')),  "
    "   seller_id INTEGER NOT NULL,  "
    "   strike_price INTEGER NOT NULL CHECK (strike_price >= 0),  "
    "   expiry_ts TEXT NOT NULL,  "
    "   status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','settled','defaulted','cancelled'))  "
    " );  "
    " CREATE TABLE IF NOT EXISTS warehouses (  "
    "   id INTEGER PRIMARY KEY,  "
    "   location_type TEXT NOT NULL CHECK (location_type IN ('sector','planet','port')),  "
    "   location_id INTEGER NOT NULL,  "
    "   owner_type TEXT NOT NULL CHECK (owner_type IN ('player','corp')),  "
    "   owner_id INTEGER NOT NULL  "
    " );  "
    " CREATE TABLE IF NOT EXISTS gov_accounts (  "
    "   id INTEGER PRIMARY KEY,  "
    "   name TEXT NOT NULL UNIQUE,  "
    "   balance INTEGER NOT NULL DEFAULT 0 CHECK (balance >= 0)  "
    " );  "
    " CREATE TABLE IF NOT EXISTS tax_policies (  "
    "   id INTEGER PRIMARY KEY,  "
    "   name TEXT NOT NULL,  "
    "   tax_type TEXT NOT NULL CHECK (tax_type IN ('trade','income','corp','wealth','transfer')),  "
    "   rate_bps INTEGER NOT NULL DEFAULT 0 CHECK (rate_bps >= 0),  "
    "   active INTEGER NOT NULL DEFAULT 1 CHECK (active IN (0,1))  "
    " );  "
    " CREATE TABLE IF NOT EXISTS tax_ledgers (  "
    "   id INTEGER PRIMARY KEY,  "
    "   policy_id INTEGER NOT NULL REFERENCES tax_policies(id) ON DELETE CASCADE,  "
    "   payer_type TEXT NOT NULL CHECK (payer_type IN ('player','corp')),  "
    "   payer_id INTEGER NOT NULL,  "
    "   amount INTEGER NOT NULL CHECK (amount >= 0),  "
    "   ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  "
    "   bank_tx_id INTEGER  "
    " );  "
    " CREATE TABLE IF NOT EXISTS fines (  "
    "   id INTEGER PRIMARY KEY,  "
    "   issued_by TEXT NOT NULL DEFAULT 'federation',  "
    "   recipient_type TEXT NOT NULL CHECK (recipient_type IN ('player','corp')),  "
    "   recipient_id INTEGER NOT NULL,  "
    "   reason TEXT,  "
    "   amount INTEGER NOT NULL CHECK (amount >= 0),  "
    "   status TEXT NOT NULL DEFAULT 'unpaid' CHECK (status IN ('unpaid','paid','void')),  "
    "   issued_ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  "
    "   paid_bank_tx INTEGER  "
    " );  "
    " CREATE TABLE IF NOT EXISTS bounties (  "
    "   id INTEGER PRIMARY KEY,  "
    "   posted_by_type TEXT NOT NULL CHECK (posted_by_type IN ('player','corp','gov','npc')),  "
    "   posted_by_id INTEGER,  "
    "   target_type TEXT NOT NULL CHECK (target_type IN ('player','corp','npc')),  "
    "   target_id INTEGER NOT NULL,  "
    "   reward INTEGER NOT NULL CHECK (reward >= 0),  "
    "   escrow_bank_tx INTEGER,  "
    "   status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','claimed','cancelled','expired')),  "
    "   posted_ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  "
    "   claimed_by INTEGER,  "
    "   paid_bank_tx INTEGER  "
    " );  "
    " CREATE TABLE IF NOT EXISTS grants (  "
    "   id INTEGER PRIMARY KEY,  "
    "   name TEXT NOT NULL,  "
    "   recipient_type TEXT NOT NULL CHECK (recipient_type IN ('player','corp')),  "
    "   recipient_id INTEGER NOT NULL,  "
    "   amount INTEGER NOT NULL CHECK (amount >= 0),  "
    "   awarded_ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  "
    "   bank_tx_id INTEGER  "
    " );  "
    " CREATE TABLE IF NOT EXISTS research_projects (  "
    "   id INTEGER PRIMARY KEY,  "
    "   sponsor_type TEXT NOT NULL CHECK (sponsor_type IN ('player','corp','gov')),  "
    "   sponsor_id INTEGER,  "
    "   title TEXT NOT NULL,  "
    "   field TEXT NOT NULL,  "
    "   cost INTEGER NOT NULL CHECK (cost >= 0),  "
    "   progress INTEGER NOT NULL DEFAULT 0 CHECK (progress BETWEEN 0 AND 100),  "
    "   status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active','paused','complete','failed')),  "
    "   created_ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))  "
    " );  "
    " CREATE TABLE IF NOT EXISTS research_contributors (  "
    "   project_id INTEGER NOT NULL REFERENCES research_projects(id) ON DELETE CASCADE,  "
    "   actor_type TEXT NOT NULL CHECK (actor_type IN ('player','corp')),  "
    "   actor_id INTEGER NOT NULL,  "
    "   amount INTEGER NOT NULL CHECK (amount >= 0),  "
    "   PRIMARY KEY (project_id, actor_type, actor_id)  "
    " );  "
    " CREATE TABLE IF NOT EXISTS research_results (  "
    "   id INTEGER PRIMARY KEY,  "
    "   project_id INTEGER NOT NULL REFERENCES research_projects(id) ON DELETE CASCADE,  "
    "   blueprint_code TEXT NOT NULL,  "
    "   unlocked_ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))  "
    " );  ",

  "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance, is_active) VALUES ('port', 1, 'CRD', 500000, 1); ",

  "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, balance) "
    "VALUES ('npc_planet', (SELECT id FROM planets WHERE name='Earth'), 1000000);",
  "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, balance) "
    "VALUES ('npc_planet', (SELECT id FROM planets WHERE name='Ferringhi Homeworld'), 1000000);",
  "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, balance) "
    "VALUES ('npc_planet', (SELECT id FROM planets WHERE name='Orion Hideout'), 1000000);"
    " CREATE TABLE IF NOT EXISTS black_accounts (  "
    "   id INTEGER PRIMARY KEY,  "
    "   owner_type TEXT NOT NULL CHECK (owner_type IN ('player','corp','npc')),  "
    "   owner_id INTEGER NOT NULL,  "
    "   balance INTEGER NOT NULL DEFAULT 0 CHECK (balance >= 0)  "
    " );  "
    " CREATE TABLE IF NOT EXISTS laundering_ops (  "
    "   id INTEGER PRIMARY KEY,  "
    "   from_black_id INTEGER REFERENCES black_accounts(id) ON DELETE SET NULL,  "
    "   to_player_id INTEGER REFERENCES players(id) ON DELETE SET NULL,  "
    "   amount INTEGER NOT NULL CHECK (amount > 0),  "
    "   risk_pct INTEGER NOT NULL DEFAULT 25 CHECK (risk_pct BETWEEN 0 AND 100),  "
    "   status TEXT NOT NULL DEFAULT 'pending' CHECK (status IN ('pending','cleaned','seized','failed')),  "
    "   ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))  "
    " );  "
    " CREATE TABLE IF NOT EXISTS contracts_illicit (  "
    "   id INTEGER PRIMARY KEY,  "
    "   contractor_type TEXT NOT NULL CHECK (contractor_type IN ('player','corp','npc')),  "
    "   contractor_id INTEGER NOT NULL,  "
    "   target_type TEXT NOT NULL CHECK (target_type IN ('player','corp','npc')),  "
    "   target_id INTEGER NOT NULL,  "
    "   reward INTEGER NOT NULL CHECK (reward >= 0),  "
    "   escrow_black_id INTEGER REFERENCES black_accounts(id) ON DELETE SET NULL,  "
    "   status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','fulfilled','failed','cancelled')),  "
    "   ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))  "
    " );  "
    " CREATE TABLE IF NOT EXISTS fences (  "
    "   id INTEGER PRIMARY KEY,  "
    "   npc_id INTEGER,  "
    "   sector_id INTEGER,  "
    "   reputation INTEGER NOT NULL DEFAULT 0  "
    " );  "
    " CREATE TABLE IF NOT EXISTS economic_indicators (  "
    "   id INTEGER PRIMARY KEY,  "
    "   ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  "
    "   inflation_bps INTEGER NOT NULL DEFAULT 0,  "
    "   liquidity INTEGER NOT NULL DEFAULT 0,  "
    "   credit_velocity REAL NOT NULL DEFAULT 0.0  "
    " );  "
    " CREATE TABLE IF NOT EXISTS sector_gdp (  "
    "   sector_id INTEGER PRIMARY KEY,  "
    "   gdp INTEGER NOT NULL DEFAULT 0,  "
    "   last_update TEXT  "
    " );  "
    " CREATE TABLE IF NOT EXISTS event_triggers (  "
    "   id INTEGER PRIMARY KEY,  "
    "   name TEXT NOT NULL,  "
    "   condition_json TEXT NOT NULL,  "
    "   action_json TEXT NOT NULL  "
    " );  "
    " CREATE TABLE IF NOT EXISTS charities (  "
    "   id INTEGER PRIMARY KEY,  "
    "   name TEXT NOT NULL UNIQUE,  "
    "   description TEXT  "
    " );  "
    " CREATE TABLE IF NOT EXISTS donations (  "
    "   id INTEGER PRIMARY KEY,  "
    "   charity_id INTEGER NOT NULL REFERENCES charities(id) ON DELETE CASCADE,  "
    "   donor_type TEXT NOT NULL CHECK (donor_type IN ('player','corp')),  "
    "   donor_id INTEGER NOT NULL,  "
    "   amount INTEGER NOT NULL CHECK (amount >= 0),  "
    "   ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  "
    "   bank_tx_id INTEGER  "
    " );  "
    " CREATE TABLE IF NOT EXISTS temples (  "
    "   id INTEGER PRIMARY KEY,  "
    "   name TEXT NOT NULL UNIQUE,  "
    "   sector_id INTEGER,  "
    "   favour INTEGER NOT NULL DEFAULT 0  "
    " );  "
    " CREATE TABLE IF NOT EXISTS guilds (  "
    "   id INTEGER PRIMARY KEY,  "
    "   name TEXT NOT NULL UNIQUE,  "
    "   description TEXT  "
    " );  "
    " CREATE TABLE IF NOT EXISTS guild_memberships (  "
    "   guild_id INTEGER NOT NULL REFERENCES guilds(id) ON DELETE CASCADE,  "
    "   member_type TEXT NOT NULL CHECK (member_type IN ('player','corp')),  "
    "   member_id INTEGER NOT NULL,  "
    "   role TEXT NOT NULL DEFAULT 'member',  "
    "   PRIMARY KEY (guild_id, member_type, member_id)  "
    " );  "
    " CREATE TABLE IF NOT EXISTS guild_dues (  "
    "   id INTEGER PRIMARY KEY,  "
    "   guild_id INTEGER NOT NULL REFERENCES guilds(id) ON DELETE CASCADE,  "
    "   amount INTEGER NOT NULL CHECK (amount >= 0),  "
    "   period TEXT NOT NULL DEFAULT 'monthly' CHECK (period IN ('weekly','monthly','quarterly','yearly'))  "
    " );  "
    " CREATE TABLE IF NOT EXISTS economy_snapshots (  "
    "   id INTEGER PRIMARY KEY,  "
    "   ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  "
    "   money_supply INTEGER NOT NULL DEFAULT 0,  "
    "   total_deposits INTEGER NOT NULL DEFAULT 0,  "
    "   total_loans INTEGER NOT NULL DEFAULT 0,  "
    "   total_insured INTEGER NOT NULL DEFAULT 0,  "
    "   notes TEXT  "
    " );  "
    " CREATE TABLE IF NOT EXISTS ai_economy_agents (  "
    "   id INTEGER PRIMARY KEY,  "
    "   name TEXT NOT NULL,  "
    "   role TEXT NOT NULL,  "
    "   config_json TEXT NOT NULL  "
    " );  "
    " CREATE TABLE IF NOT EXISTS anomaly_reports (  "
    "   id INTEGER PRIMARY KEY,  "
    "   ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),  "
    "   severity TEXT NOT NULL CHECK (severity IN ('low','medium','high','critical')),  "
    "   subject TEXT NOT NULL,  "
    "   details TEXT NOT NULL,  "
    "   resolved INTEGER NOT NULL DEFAULT 0 CHECK (resolved IN (0,1))  "
    " );  "
    " CREATE TABLE IF NOT EXISTS economy_policies (  "
    "   id INTEGER PRIMARY KEY,  "
    "   name TEXT NOT NULL UNIQUE,  "
    "   config_json TEXT NOT NULL,  "
    "   active INTEGER NOT NULL DEFAULT 1 CHECK (active IN (0,1))  "
    " );  "
    " CREATE VIEW IF NOT EXISTS v_player_networth AS  "
    " SELECT  "
    "   p.id AS player_id,  "
    "   p.name AS player_name,  "
    "   COALESCE(ba.balance,0) AS bank_balance  "
    " FROM players p  "
    " LEFT JOIN bank_accounts ba ON ba.owner_type = 'player' AND ba.owner_id = p.id;  "
    " CREATE VIEW IF NOT EXISTS v_corp_treasury AS  "
    " SELECT  "
    "   c.id AS corp_id,  "
    "   c.name AS corp_name,  "
    "   COALESCE(ca.balance,0) AS bank_balance  "
    " FROM corps c  "
    " LEFT JOIN corp_accounts ca ON ca.corp_id = c.id;  "
    " CREATE VIEW IF NOT EXISTS v_bounty_board AS  "
    " SELECT  "
    "   b.id,  "
    "   b.target_type,  "
    "   b.target_id,  "
    "   p_target.name AS target_name,  "
    "   b.reward,  "
    "   b.status,  "
    "   b.posted_by_type,  "
    "   b.posted_by_id,  "
    "   CASE b.posted_by_type  "
    "     WHEN 'player' THEN p_poster.name  "
    "     WHEN 'corp' THEN c_poster.name  "
    "     ELSE b.posted_by_type  "
    "   END AS poster_name,  "
    "   b.posted_ts  "
    " FROM bounties b  "
    " LEFT JOIN players p_target ON b.target_type = 'player' AND b.target_id = p_target.id  "
    " LEFT JOIN players p_poster ON b.posted_by_type = 'player' AND b.posted_by_id = p_poster.id  "
    " LEFT JOIN corps c_poster ON b.posted_by_type = 'corp' AND b.posted_by_id = c_poster.id  "
    " WHERE b.status = 'open';  "
    " CREATE VIEW IF NOT EXISTS v_bank_leaderboard AS  "
    " SELECT  "
    "   ba.owner_id AS player_id,  "
    "   p.name,  "
    "   ba.balance  "
    " FROM bank_accounts ba  "
    " JOIN players p ON ba.owner_type = 'player' AND ba.owner_id = p.id  "
    " LEFT JOIN player_prefs pp ON ba.owner_id = pp.player_id AND pp.key = 'privacy.show_leaderboard'  "
    " WHERE COALESCE(pp.value, 'true') = 'true'  "
    " ORDER BY ba.balance DESC;  "
    ////////////////
};


///////////// S2S /////////////////////////

static const char *engine_bootstrap_sql_statements[] = {
  /* --- S2S keyring (HMAC) --- */
  "CREATE TABLE IF NOT EXISTS s2s_keys("
    "  key_id TEXT PRIMARY KEY,"
    "  key_b64 TEXT NOT NULL,"
    "  is_default_tx INTEGER NOT NULL DEFAULT 0,"
    "  active INTEGER NOT NULL DEFAULT 1,"
    "  created_ts INTEGER NOT NULL" ");",
  "INSERT OR IGNORE INTO s2s_keys(key_id,key_b64,is_default_tx,active,created_ts)"
    "VALUES('k0','c3VwZXJzZWNyZXRrZXlzZWNyZXRrZXlzZWNyZXQxMjM0NTY3OA==',1,1,strftime('%s','now'));",
  /* --- Engine cron/scheduling --- */
  "CREATE TABLE IF NOT EXISTS cron_tasks("
    "  id INTEGER PRIMARY KEY,"
    "  name TEXT UNIQUE NOT NULL,"
    "  schedule TEXT NOT NULL,"
    "  last_run_at INTEGER,"
    "  next_due_at INTEGER NOT NULL,"
    "  enabled INTEGER NOT NULL DEFAULT 1," "  payload TEXT" ");",
  "INSERT OR IGNORE INTO cron_tasks(name,schedule,last_run_at,next_due_at,enabled,payload) VALUES"
    "('daily_turn_reset','daily@03:00Z',NULL,strftime('%s','now'),1,NULL),"
    "('terra_replenish','daily@04:00Z',NULL,strftime('%s','now'),1,NULL),"
    "('planet_growth','every:10m',NULL,strftime('%s','now'),1,NULL),"
    "('fedspace_cleanup','every:2m',NULL,strftime('%s','now'),1,NULL),"
    "('autouncloak_sweeper','every:15m',NULL,strftime('%s','now'),1,NULL),"
    "('npc_step','every:30s',NULL,strftime('%s','now'),1,NULL),"
    "('broadcast_ttl_cleanup','every:5m',NULL,strftime('%s','now'),1,NULL),"
    "('daily_news_compiler','daily@06:00Z',NULL,strftime('%s','now','start of day','+1 day','utc','6 hours'),1,NULL),"
    "('traps_process','every:1m',NULL,strftime('%s','now'),1,NULL),"
    "('cleanup_old_news','daily@07:00Z',NULL,strftime('%s','now','start of day','+1 day','utc','7 hours'),1,NULL),"
    "('limpet_ttl_cleanup','every:5m',NULL,strftime('%s','now'),1,NULL),"
    "('daily_lottery_draw','daily@23:00Z',NULL,strftime('%s','now','start of day','+1 day','utc','23 hours'),1,NULL),"
    "('deadpool_resolution_cron','daily@01:00Z',NULL,strftime('%s','now','start of day','+1 day','utc','1 hours'),1,NULL),"
    "('tavern_notice_expiry_cron','daily@07:00Z',NULL,strftime('%s','now','start of day','+1 day','utc','7 hours'),1,NULL),"
    "('loan_shark_interest_cron','daily@00:00Z',NULL,strftime('%s','now','start of day','+1 day','utc','0 hours'),1,NULL),"
    "('dividend_payout','daily@05:00Z',NULL,strftime('%s','now','start of day','+1 day','utc','5 hours'),1,NULL),"
    "('daily_stock_price_recalculation','daily@04:30Z',NULL,strftime('%s','now','start of day','+1 day','utc','4 hours','+30 minutes'),1,NULL),"
    "('daily_market_settlement','daily@05:30Z',NULL,strftime('%s','now','start of day','+1 day','utc','5 hours','+30 minutes'),1,NULL),"
    "('system_notice_ttl','daily@00:05Z',NULL,strftime('%s','now','start of day','+1 day','utc','0 hours','+5 minutes'),1,NULL),"
    "('deadletter_retry','every:1m',NULL,strftime('%s','now'),1,NULL),"
    "('daily_corp_tax','daily@05:00Z',NULL,strftime('%s','now','start of day','+1 day','utc','5 hours'),1,NULL);",


  /* --- Server→Engine event rail (separate from your existing system_events) --- */
  "CREATE TABLE IF NOT EXISTS engine_events("
    "  id INTEGER PRIMARY KEY,"
    "  ts INTEGER NOT NULL,"
    "  type TEXT NOT NULL,"
    "  actor_player_id INTEGER,"
    "  sector_id INTEGER,"
    "  payload TEXT NOT NULL,"
    "  idem_key TEXT, " " processed_at INTEGER" ");",
  "CREATE UNIQUE INDEX IF NOT EXISTS idx_engine_events_idem ON engine_events(idem_key) WHERE idem_key IS NOT NULL;",
  "CREATE INDEX IF NOT EXISTS idx_engine_events_ts ON engine_events(ts);",
  "CREATE INDEX IF NOT EXISTS idx_engine_events_actor_ts ON engine_events(actor_player_id, ts);",
  "CREATE INDEX IF NOT EXISTS idx_engine_events_sector_ts ON engine_events(sector_id, ts);",
  /* --- Engine watermark --- */
  "CREATE TABLE IF NOT EXISTS engine_offset("
    "  key TEXT PRIMARY KEY,"
    "  last_event_id INTEGER NOT NULL,"
    "  last_event_ts INTEGER NOT NULL" ");",
  "INSERT OR IGNORE INTO engine_offset(key,last_event_id,last_event_ts) VALUES('events',0,0);",
  /* --- Deadletter for bad events --- */
  "CREATE TABLE IF NOT EXISTS engine_events_deadletter("
    "  id INTEGER PRIMARY KEY,"
    "  ts INTEGER NOT NULL,"
    "  type TEXT NOT NULL,"
    "  payload TEXT NOT NULL,"
    "  error TEXT NOT NULL," "  moved_at INTEGER NOT NULL" ");",
  /* --- Engine→Server command rail --- */
  "CREATE TABLE IF NOT EXISTS engine_commands("
    "  id INTEGER PRIMARY KEY,"
    "  type TEXT NOT NULL,"
    "  payload TEXT NOT NULL,"
    "  status TEXT NOT NULL DEFAULT 'ready',"
    "  priority INTEGER NOT NULL DEFAULT 100,"
    "  attempts INTEGER NOT NULL DEFAULT 0,"
    "  created_at INTEGER NOT NULL,"
    "  due_at INTEGER NOT NULL,"
    "  started_at INTEGER,"
    "  finished_at INTEGER," "  worker TEXT," "  idem_key TEXT" ");",
  "CREATE UNIQUE INDEX IF NOT EXISTS idx_engine_cmds_idem ON engine_commands(idem_key) WHERE idem_key IS NOT NULL;",
  "CREATE INDEX IF NOT EXISTS idx_engine_cmds_status_due ON engine_commands(status, due_at);",
  "CREATE INDEX IF NOT EXISTS idx_engine_cmds_prio_due ON engine_commands(priority, due_at);",
  /* --- Engine audit trail --- */
  "CREATE TABLE IF NOT EXISTS engine_audit("
    "  id INTEGER PRIMARY KEY,"
    "  ts INTEGER NOT NULL,"
    "  cmd_type TEXT NOT NULL,"
    "  correlation_id TEXT,"
    "  actor_player_id INTEGER," "  details TEXT" ");",
  "CREATE TABLE IF NOT EXISTS news_feed(   "
    "  news_id INTEGER PRIMARY KEY,   "
    "  published_ts INTEGER NOT NULL,   "
    "  news_category TEXT NOT NULL,   "
    "  article_text TEXT NOT NULL,   "
    "  author_id INTEGER,   " "  source_ids TEXT" ");",
  "CREATE INDEX IF NOT EXISTS ix_news_feed_pub_ts ON news_feed(published_ts);"
};

static const size_t engine_bootstrap_sql_count =
  sizeof (engine_bootstrap_sql_statements) /
  sizeof (engine_bootstrap_sql_statements[0]);

static const char *CREATE_VIEWS_SQL =
  /* --- Human Readable view --- */
  "CREATE VIEW  IF NOT EXISTS cronjobs AS "
  "SELECT "
  "    id, "
  "    name, "
  "    datetime(next_due_at, 'unixepoch') AS next_due_utc, "
  "    datetime(last_run_at, 'unixepoch') AS last_run_utc "
  "FROM " "    cron_tasks " "ORDER BY " "    next_due_at;\n ";

static const char *MIGRATE_A_SQL = "BEGIN IMMEDIATE;"
  /* engine_offset (consumer high-water mark) */
  "CREATE TABLE IF NOT EXISTS engine_offset ("
  "  key TEXT PRIMARY KEY,"
  "  last_event_id INTEGER NOT NULL," "  last_event_ts INTEGER NOT NULL" ");"
  /* engine_events (durable rail) — keep your existing table; create if missing */
  "CREATE TABLE IF NOT EXISTS engine_events (" "  id              INTEGER PRIMARY KEY," "  ts              INTEGER NOT NULL," "  type            TEXT NOT NULL," "  actor_player_id INTEGER," "  sector_id       INTEGER," "  payload         JSON NOT NULL,"	/* JSON is stored as TEXT in SQLite */
  "  idem_key        TEXT" ");"
  /* Indices per acceptance criteria */
  "CREATE UNIQUE INDEX IF NOT EXISTS ux_engine_events_idem_key "
  "  ON engine_events(idem_key);"
  "CREATE INDEX IF NOT EXISTS ix_engine_events_ts "
  "  ON engine_events(ts);"
  "CREATE INDEX IF NOT EXISTS ix_engine_events_type "
  "  ON engine_events(type);"
  "CREATE INDEX IF NOT EXISTS ix_engine_events_actor_ts "
  "  ON engine_events(actor_player_id, ts);"
  "CREATE INDEX IF NOT EXISTS ix_engine_events_sector_ts "
  "  ON engine_events(sector_id, ts);"
  /* Forward-compat view named exactly 'events' */
  "DROP VIEW IF EXISTS events;"
  "CREATE VIEW IF NOT EXISTS  events AS "
  "  SELECT id, ts, type, actor_player_id, sector_id, payload, idem_key "
  "  FROM engine_events;" "COMMIT;";

static const char *MIGRATE_B_SQL = "BEGIN IMMEDIATE;"
  "CREATE TABLE IF NOT EXISTS engine_offset("
  "  key TEXT PRIMARY KEY,"
  "  last_event_id INTEGER NOT NULL," "  last_event_ts INTEGER NOT NULL" ");"
  /* Durable rail backing table */
  "CREATE TABLE IF NOT EXISTS engine_events("
  "  id              INTEGER PRIMARY KEY,"
  "  ts              INTEGER NOT NULL,"
  "  type            TEXT NOT NULL,"
  "  actor_player_id INTEGER,"
  "  sector_id       INTEGER,"
  "  payload         JSON NOT NULL," "  idem_key        TEXT" ");"
  /* Indices per AC */
  "CREATE UNIQUE INDEX IF NOT EXISTS ux_engine_events_idem_key "
  "  ON engine_events(idem_key);"
  "CREATE INDEX IF NOT EXISTS ix_engine_events_ts "
  "  ON engine_events(ts);"
  "CREATE INDEX IF NOT EXISTS ix_engine_events_type "
  "  ON engine_events(type);"
  "CREATE INDEX IF NOT EXISTS ix_engine_events_actor_ts "
  "  ON engine_events(actor_player_id, ts);"
  "CREATE INDEX IF NOT EXISTS ix_engine_events_sector_ts "
  "  ON engine_events(sector_id, ts);"
  /* Forward-compat view named exactly `events` */
  "DROP VIEW IF EXISTS events;"
  "CREATE VIEW IF NOT EXISTS events AS "
  "  SELECT id, ts, type, actor_player_id, sector_id, payload, idem_key "
  "  FROM engine_events;"
  /* Make the view writable (append-only) */
  "DROP TRIGGER IF EXISTS events_insert;"
  "CREATE TRIGGER IF NOT EXISTS events_insert "
  "INSTEAD OF INSERT ON events "
  "BEGIN "
  "  INSERT INTO engine_events(id, ts, type, actor_player_id, sector_id, payload, idem_key) "
  "  VALUES (NEW.id, NEW.ts, NEW.type, NEW.actor_player_id, NEW.sector_id, NEW.payload, NEW.idem_key);"
  "END;"
  "DROP TRIGGER IF EXISTS events_update;"
  "CREATE TRIGGER IF NOT EXISTS events_update "
  "INSTEAD OF UPDATE ON events "
  "BEGIN "
  "  SELECT RAISE(ABORT, 'events is append-only');"
  "END;"
  "DROP TRIGGER IF EXISTS events_delete;"
  "CREATE TRIGGER IF NOT EXISTS events_delete "
  "INSTEAD OF DELETE ON events "
  "BEGIN " "  SELECT RAISE(ABORT, 'events is append-only');" "END;" "COMMIT;";

static const char *MIGRATE_C_SQL = "BEGIN IMMEDIATE;" "COMMIT;";

/* Number of tables */
static const size_t create_table_count =
  sizeof (create_table_sql) / sizeof (create_table_sql[0]);
/* Number of default inserts */
static const size_t insert_default_count =
  sizeof (insert_default_sql) / sizeof (insert_default_sql[0]);


int
db_engine_bootstrap (void)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      LOGE ("Failed to get DB handle in db_engine_bootstrap");
      return 0;
    }

  char *err_msg = 0;
  int rc = 0;			// Declare rc here
  for (size_t i = 0; i < engine_bootstrap_sql_count; ++i)
    {
      const char *sql_stmt = engine_bootstrap_sql_statements[i];
      int rc = sqlite3_exec (db, sql_stmt, 0, 0, &err_msg);

      if (rc != SQLITE_OK)
	{
	  LOGE ("Engine bootstrap SQL statement failed: %s", sql_stmt);
	  LOGE ("Engine bootstrap failed: %s", err_msg);
	  sqlite3_free (err_msg);
	  return 0;
	}
    }

  rc = sqlite3_exec (db, CREATE_VIEWS_SQL, 0, 0, &err_msg);
  if (rc != SQLITE_OK)
    {
      LOGE ("Engine create views failed: %s", err_msg);
      sqlite3_free (err_msg);
      return 0;
    }
  return 1;
}

static int
urandom_bytes (void *buf, size_t n)
{
  int fd = open ("/dev/urandom", O_RDONLY);
  if (fd < 0)
    return -1;
  ssize_t rd = read (fd, buf, n);
  close (fd);
  return (rd == (ssize_t) n) ? 0 : -1;
}

static void
to_hex (const unsigned char *in, size_t n, char out_hex[ /*2n+1 */ ])
{
  static const char hexd[] = "0123456789abcdef";
  for (size_t i = 0; i < n; ++i)
    {
      out_hex[2 * i + 0] = hexd[(in[i] >> 4) & 0xF];
      out_hex[2 * i + 1] = hexd[(in[i]) & 0xF];
    }
  out_hex[2 * n] = '\0';
}

static int
gen_session_token (char out64[65])
{
  unsigned char rnd[32];
  if (urandom_bytes (rnd, sizeof (rnd)) != 0)
    return -1;
  to_hex (rnd, sizeof (rnd), out64);
  return 0;
}


int
db_seed_cron_tasks (sqlite3 *db)
{
  if (!db)
    return SQLITE_MISUSE;

  /* Wrap in a small transaction for atomicity */
  char *err = NULL;
  int rc = sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &err);
  if (rc != SQLITE_OK)
    {
      if (err)
	sqlite3_free (err);
      return rc;
    }

  /* Ensure table exists before seeding (no-op if already created) */
  rc = sqlite3_exec (db,
		     "CREATE TABLE IF NOT EXISTS cron_tasks ("
		     "  id INTEGER PRIMARY KEY,"
		     "  name TEXT NOT NULL UNIQUE,"
		     "  schedule TEXT NOT NULL,"
		     "  enabled INTEGER NOT NULL DEFAULT 1,"
		     "  last_run_at INTEGER,"
		     "  next_due_at INTEGER,"
		     "  payload TEXT" ");", NULL, NULL, &err);
  if (rc != SQLITE_OK)
    goto done;

  /* Seed: broadcast TTL cleanup runs every 5 minutes.
     Idempotent: only inserts if the name doesn't exist. */
  rc = sqlite3_exec (db,
		     "INSERT INTO cron_tasks(name, schedule, enabled, next_due_at) "
		     "SELECT 'broadcast_ttl_cleanup','every:5m',1,strftime('%s','now') "
		     "WHERE NOT EXISTS (SELECT 1 FROM cron_tasks WHERE name='broadcast_ttl_cleanup');",
		     NULL, NULL, &err);
  if (rc != SQLITE_OK)
    goto done;

  /* Seed: daily bank interest tick runs daily */
  rc = sqlite3_exec (db,
		     "INSERT INTO cron_tasks(name, schedule, enabled, next_due_at) "
		     "SELECT 'daily_bank_interest_tick','daily@00:00Z',1,strftime('%s','now','start of day','+1 day','utc','0 hours') "
		     "WHERE NOT EXISTS (SELECT 1 FROM cron_tasks WHERE name='daily_bank_interest_tick');",
		     NULL, NULL, &err);
  if (rc != SQLITE_OK)
    goto done;

done:
  {
    int rc2 = (rc == SQLITE_OK)
      ? sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL)
      : sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
    (void) rc2;
  }
  if (err)
    sqlite3_free (err);
  return rc;
}




int
db_init (void)
{
  sqlite3_stmt *stmt = NULL;
  int ret_code = -1;		// Default to error
  int rc;

  pthread_mutex_lock (&db_mutex);

  // If the database is already initialized, just return success.
  if (db_handle)
    {
      ret_code = 0;
      goto cleanup;
    }

  /* Step 1: open or create DB file */
  rc = sqlite3_open (DEFAULT_DB_NAME, &db_handle);
  if (rc != SQLITE_OK)
    {
      LOGE ("DB Open Failed (%s): %s (rc=%d)", DEFAULT_DB_NAME,
	    sqlite3_errstr (rc), rc);
      sqlite3_close (db_handle);
      LOGE ("FATAL ERROR: Could not open database! Code: %d, Message: %s", rc,
	    sqlite3_errmsg (db_handle));
      return -1;
    }


  // Ensure mandatory schemas (we already hold db_mutex)
  (void) db_ensure_auth_schema_unlocked ();
  (void) db_ensure_idempotency_schema_unlocked ();


  /* Step 2: check if config table exists */
  const char *sql =
    "SELECT name FROM sqlite_master WHERE type='table' AND name='config';";

  rc = sqlite3_prepare_v2 (db_handle, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("DB prepare check error: %s", sqlite3_errmsg (db_handle));
      ret_code = -1;
      goto cleanup;
    }

  rc = sqlite3_step (stmt);
  int table_exists = (rc == SQLITE_ROW);

  // If the query failed for some reason, `rc` will not be SQLITE_ROW or SQLITE_DONE.
  // We should treat this as an error.
  if (rc != SQLITE_ROW && rc != SQLITE_DONE)
    {
      LOGE ("DB step check error: %s", sqlite3_errmsg (db_handle));
      ret_code = -1;
      goto cleanup;
    }

  /* Step 3: if no config table, create schema + defaults */
  if (!table_exists)
    {
      if (db_create_tables_unlocked (false) != 0)
	{
	  LOGE ("Failed to create tables");
	  ret_code = -1;
	  goto cleanup;
	}
      if (db_insert_defaults_unlocked () != 0)
	{
	  LOGE ("Failed to insert default data");
	  ret_code = -1;
	  goto cleanup;
	}
      if (db_seed_ai_qa_bot_bank_account_unlocked () != 0)
	{
	  LOGE ("Failed to seed AI QA bot bank account");
	  ret_code = -1;
	  goto cleanup;
	}

    }
  else
    {
      /* // Even if config table exists, ensure all other tables/views are up-to-date */
      /* // This handles cases where new tables/views are added after initial setup */
      /* fprintf (stderr, "Schema detected -- ensuring all tables and views are up-to-date...\n"); */
      /* if (db_create_tables_unlocked (true) != 0) */
      /*        { */
      /*          fprintf (stderr, "Failed to update tables/views\n"); */
      /*          ret_code = -1; */
      /*          goto cleanup; */
      /*        } */
    }
  if (!db_engine_bootstrap ())
    {
      LOGE ("PROBLEM WITH ENGINE CREATION");
    }

  (void) db_seed_cron_tasks (db_handle);


  // If we've made it here, all steps were successful.
  ret_code = 0;
  LOGI ("db_init completed successfully");

cleanup:
  /* Step 4: Finalize the statement if it was successfully prepared. */
  if (stmt)
    {
      sqlite3_finalize (stmt);
    }

  /* Step 5: If an error occurred, clean up the database handle. */
  if (ret_code != 0)
    {
      // Only close if the handle is valid
      if (db_handle)
	{
	  sqlite3_close (db_handle);
	  db_handle = NULL;
	}
    }

  // 6. Release the lock at the end.
  pthread_mutex_unlock (&db_mutex);

  return ret_code;
}


int
db_load_ports (int *server_port, int *s2s_port)
{
  sqlite3_stmt *stmt = NULL;
  int rc;
  int ret_code = -1;

  if (!server_port || !s2s_port)
    {
      return -1;
    }

  pthread_mutex_lock (&db_mutex);

  if (!db_handle)
    {
      goto cleanup;
    }

  const char *sql = "SELECT server_port, s2s_port FROM config WHERE id = 1;";

  rc = sqlite3_prepare_v2 (db_handle, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("DB prepare for port loading error: %s",
	    sqlite3_errmsg (db_handle));
      goto cleanup;
    }

  rc = sqlite3_step (stmt);
  if (rc == SQLITE_ROW)
    {
      *server_port = sqlite3_column_int (stmt, 0);
      *s2s_port = sqlite3_column_int (stmt, 1);
      ret_code = 0;		// Success
    }
  else
    {
      LOGE ("DB step for port loading error: %s", sqlite3_errmsg (db_handle));
    }

cleanup:
  if (stmt)
    {
      sqlite3_finalize (stmt);
    }
  pthread_mutex_unlock (&db_mutex);
  return ret_code;
}


static int
db_seed_ai_qa_bot_bank_account_unlocked (void)
{
  int rc;
  int player_id = -1;
  int account_id = -1;
  sqlite3 *db = db_get_handle ();

  // Get the player_id for 'ai_qa_bot'
  sqlite3_stmt *st = NULL;
  const char *sql_get_player_id =
    "SELECT id FROM players WHERE name = 'ai_qa_bot';";
  rc = sqlite3_prepare_v2 (db, sql_get_player_id, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("DB seed ai_qa_bot: Failed to prepare player ID lookup: %s",
	    sqlite3_errmsg (db));
      return -1;
    }
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      player_id = sqlite3_column_int (st, 0);
    }
  sqlite3_finalize (st);

  if (player_id == -1)
    {
      LOGE ("DB seed ai_qa_bot: Player 'ai_qa_bot' not found.");
      return -1;
    }

  // Check if account already exists
  rc = h_get_account_id_unlocked (db, "player", player_id, &account_id);
  if (rc == SQLITE_OK)
    {
      // Account already exists, nothing to do
      return 0;
    }
  else if (rc == SQLITE_NOTFOUND)
    {
      // Account does not exist, create it
      rc =
	h_create_bank_account_unlocked (db, "player", player_id, 10000,
					&account_id);
      if (rc != SQLITE_OK)
	{
	  LOGE
	    ("DB seed ai_qa_bot: Failed to create bank account for 'ai_qa_bot' (rc=%d)",
	     rc);
	  return -1;
	}
    }
  else
    {
      LOGE ("DB seed ai_qa_bot: Error checking for existing account (rc=%d)",
	    rc);
      return -1;
    }

  return 0;
}


/* Public, thread-safe wrapper */
int
db_create_tables (bool schema_exists)
{
  int rc;
  pthread_mutex_lock (&db_mutex);
  rc = db_create_tables_unlocked (schema_exists);
  pthread_mutex_unlock (&db_mutex);
  return rc;
}

/* Actual work; assumes db_mutex is already held */
static int
db_create_tables_unlocked (bool schema_exists)
{
  char *errmsg = NULL;
  int ret_code = -1;
  sqlite3 *db = db_get_handle ();
  if (!db)
    return -1;

  int rc;

  for (size_t i = 0; i < create_table_count; i++)
    {
      const char *sql_statement = create_table_sql[i];

      // If schema_exists is true, skip CREATE TABLE IF NOT EXISTS statements
      if (schema_exists
	  && strstr (sql_statement,
		     "CREATE TABLE IF NOT EXISTS") == sql_statement)
	{
	  continue;		// Skip this statement
	}

      rc = sqlite3_exec (db_handle, sql_statement, 0, 0, &errmsg);
      if (rc != SQLITE_OK)
	{
	  LOGE ("SQL error at step %zu: %s", i, errmsg);
	  LOGE ("Failing SQL: %s", sql_statement);
	  sqlite3_free (errmsg);
	  return -1;
	}
    }

  ret_code = 0;

  if (schema_exists)
    {
      // Execute migration scripts
      if (sqlite3_exec (db, MIGRATE_A_SQL, 0, 0, &errmsg) != SQLITE_OK)
	{
	  LOGE ("MIGRATE_A_SQL failed: %s", errmsg);
	  sqlite3_free (errmsg);
	  return -1;
	}
      if (sqlite3_exec (db, MIGRATE_B_SQL, 0, 0, &errmsg) != SQLITE_OK)
	{
	  LOGE ("MIGRATE_B_SQL failed: %s", errmsg);
	  sqlite3_free (errmsg);
	  return -1;
	}
      if (sqlite3_exec (db, MIGRATE_C_SQL, 0, 0, &errmsg) != SQLITE_OK)
	{
	  LOGE ("MIGRATE_C_SQL failed: %s", errmsg);
	  sqlite3_free (errmsg);
	  return -1;
	}
    }

  return ret_code;
}

/* Public, thread-safe wrapper */
int
db_insert_defaults (void)
{
  int rc;
  pthread_mutex_lock (&db_mutex);
  rc = db_insert_defaults_unlocked ();
  pthread_mutex_unlock (&db_mutex);
  return rc;
}

/* Actual work; assumes db_mutex is already held */
static int
db_insert_defaults_unlocked (void)
{
  char *errmsg = NULL;
  int ret_code = -1;
  sqlite3 *db = db_get_handle ();
  if (!db)
    return -1;
  int rc; // Declared rc here


  LOGI("db_insert_defaults_unlocked: Starting default data insertion.");

  for (size_t i = 0; i < insert_default_count; i++)
    {
      LOGD("db_insert_defaults_unlocked: Executing default SQL statement %zu: %s", i, insert_default_sql[i]);
      if (sqlite3_exec (db, insert_default_sql[i], NULL, NULL, &errmsg) !=
	  SQLITE_OK)
	{
	  LOGE ("DB insert_defaults error (%zu): %s", i, errmsg);
	  LOGE ("Failing SQL: %s", insert_default_sql[i]);
	  goto cleanup;
	}
    }
  
  // Ensure bank accounts for system, players, and ports
  const char *insert_default_bank_account_sql[] = {
    "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) VALUES ('system', 0, 'CRD', 1000000000);", // System bank
    "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) SELECT 'player', id, 'CRD', 1000 FROM players WHERE name = 'System';",
    "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) SELECT 'player', id, 'CRD', 1000 FROM players WHERE name = 'Federation Administrator';",
    "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) SELECT 'player', id, 'CRD', 1000 FROM players WHERE name = 'newguy';",
    "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) SELECT 'player', id, 'CRD', 1000 FROM players WHERE name = 'ai_qa_bot';",
    // Player accounts for goodguy, badguy, admin will be created on auth.register if not present
    "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) SELECT 'port', id, 'CRD', 100000 FROM ports WHERE name = 'Earth Port';"
  };

  for (size_t i = 0; i < sizeof(insert_default_bank_account_sql) / sizeof(insert_default_bank_account_sql[0]); i++) {
    rc = sqlite3_exec(db, insert_default_bank_account_sql[i], NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        LOGE("db_insert_defaults_unlocked: SQL error inserting default bank account: %s", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }
  }

  ret_code = 0;
  LOGI("db_insert_defaults_unlocked: Default data insertion completed successfully.");
cleanup:
  if (errmsg)
    sqlite3_free (errmsg);
  return ret_code;
}


void
db_close (void)
{
  // 1. Acquire the lock to prevent any other thread from using the handle
  // while we are closing it and setting it to NULL.
  pthread_mutex_lock (&db_mutex);

  if (db_handle)
    {
      sqlite3_close (db_handle);
      db_handle = NULL;
    }

  // 2. Release the lock.
  pthread_mutex_unlock (&db_mutex);
}

int
db_create (const char *table, json_t *row)
{
  (void) row;
  int ret_code = -1;		// Default to error

  // 1. Acquire the lock before accessing the database.
  pthread_mutex_lock (&db_mutex);

  if (!db_handle)
    {
      goto cleanup;
    }

  /* TODO: Build INSERT SQL dynamically based on JSON keys/values */
  LOGE ("db_create(%s, row) called (not implemented)", table);

  ret_code = 0;			// Assuming success for the placeholder

cleanup:
  // 2. Release the lock at the end.
  pthread_mutex_unlock (&db_mutex);

  return ret_code;
}

json_t *
db_read (const char *table, int id)
{
  json_t *result = NULL;

  // 1. Acquire the lock before accessing the database.
  pthread_mutex_lock (&db_mutex);

  if (!db_handle)
    {
      goto cleanup;
    }

  /* TODO: Prepare SELECT ... WHERE id=? and return json_t * */
  LOGE ("db_read(%s, %d) called (not implemented)", table, id);

  // result should be set here on success

cleanup:
  // 2. Release the lock at the end.
  pthread_mutex_unlock (&db_mutex);

  return result;
}

int
db_update (const char *table, int id, json_t *row)
{
  (void) row;
  int ret_code = -1;		// Default to error

  // 1. Acquire the lock before accessing the database.
  pthread_mutex_lock (&db_mutex);

  if (!db_handle)
    {
      goto cleanup;
    }

  /* TODO: Build UPDATE SQL dynamically */
  LOGE ("db_update(%s, %d, row) called (not implemented)", table, id);

  ret_code = 0;			// Assuming success for the placeholder

cleanup:
  // 2. Release the lock at the end.
  pthread_mutex_unlock (&db_mutex);

  return ret_code;
}

int
db_delete (const char *table, int id)
{
  int ret_code = -1;		// Default to error

  // 1. Acquire the lock before accessing the database.
  pthread_mutex_lock (&db_mutex);

  if (!db_handle)
    {
      goto cleanup;
    }

  /* TODO: Prepare DELETE ... WHERE id=? */
  LOGE ("db_delete(%s, %d) called (not implemented)", table, id);

  ret_code = 0;			// Assuming success for the placeholder

cleanup:
  // 2. Release the lock at the end.
  pthread_mutex_unlock (&db_mutex);

  return ret_code;
}

/* Helper: safe text access (returns "" if NULL) */
// This function is already fine as-is because it doesn't access the global
// database handle, only the statement passed to it.
static const char *
col_text_or_empty (sqlite3_stmt *st, int col)
{
  const unsigned char *t = sqlite3_column_text (st, col);
  return t ? (const char *) t : "";
}



/* Public, thread-safe wrapper */
int
db_ensure_auth_schema (void)
{
  int rc;
  pthread_mutex_lock (&db_mutex);
  rc = db_ensure_auth_schema_unlocked ();
  pthread_mutex_unlock (&db_mutex);
  return rc;
}

/* Actual work; assumes db_mutex is already held */
static int
db_ensure_auth_schema_unlocked (void)
{
  sqlite3 *db = db_get_handle ();
  char *errmsg = NULL;
  int rc = SQLITE_ERROR;

  if (!db)
    return SQLITE_ERROR;

  rc = sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    goto fail;

  rc = sqlite3_exec (db,
		     "CREATE TABLE IF NOT EXISTS sessions ("
		     "  token      TEXT PRIMARY KEY, "
		     "  player_id  INTEGER NOT NULL, "
		     "  expires    INTEGER NOT NULL, "
		     "  created_at INTEGER NOT NULL"
		     ");", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    goto rollback;

  rc = sqlite3_exec (db,
		     "CREATE INDEX IF NOT EXISTS idx_sessions_player  ON sessions(player_id);",
		     NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    goto rollback;

  rc = sqlite3_exec (db,
		     "CREATE INDEX IF NOT EXISTS idx_sessions_expires ON sessions(expires);",
		     NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    goto rollback;

  rc = sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    goto fail;

  rc = SQLITE_OK;
rollback:

  if (rc != SQLITE_OK)
    sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
fail:
  if (errmsg)
    {
      LOGE ("[DB] auth schema: %s", errmsg);
      sqlite3_free (errmsg);
    }
  return rc;
}


///////////////////////
/// Helpers

/* ---------- db_sector_scan_snapshot: thread-safe, single statement ---------- */
/* Out shape (core fields only; handler will add adjacency + flags):
   {
     "name": TEXT,
     "safe_zone": INTEGER,          // 0/1 from sectors.safe_zone
     "port_present": INTEGER,       // 0/1 (COUNT>0)
     "ships": INTEGER,
     "planets": INTEGER,
     "mines": INTEGER,              // 0 for now (placeholder)
     "fighters": INTEGER,           // 0 for now (placeholder)
     "beacon": TEXT or NULL
   }
*/
int
db_sector_scan_snapshot (int sector_id, json_t **out_core)
{
  if (out_core)
    *out_core = NULL;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_ERROR;

  const char *sql =
    "SELECT s.name, COALESCE(s.safe_zone,0) AS safe_zone, "
    "  (SELECT CASE WHEN COUNT(1)>0 THEN 1 ELSE 0 END FROM ports    p  WHERE p.sector_id = s.id) AS port_present, "
    "  (SELECT COUNT(1)                        FROM ships    sh WHERE sh.sector_id = s.id) AS ships, "
    "  (SELECT COUNT(1)                        FROM planets  pl WHERE pl.sector_id = s.id) AS planets, "
    "  0 AS mines, 0 AS fighters, "
    "  (SELECT b.text FROM beacons b WHERE b.sector_id = s.id LIMIT 1) AS beacon "
    "FROM sectors s WHERE s.id = ?1";

  int rc = SQLITE_ERROR;
  sqlite3_stmt *st = NULL;

  pthread_mutex_lock (&db_mutex);

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    goto done;

  sqlite3_bind_int (st, 1, sector_id);

  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      const char *name = (const char *) sqlite3_column_text (st, 0);
      int safe_zone = sqlite3_column_int (st, 1);
      int port_present = sqlite3_column_int (st, 2);
      int ships = sqlite3_column_int (st, 3);
      int planets = sqlite3_column_int (st, 4);
      int mines = sqlite3_column_int (st, 5);
      int fighters = sqlite3_column_int (st, 6);
      const char *beacon = (const char *) sqlite3_column_text (st, 7);

      json_t *core = json_object ();
      json_object_set_new (core, "name",
			   json_string (name ? name : "Unknown"));
      json_object_set_new (core, "safe_zone", json_integer (safe_zone));
      json_object_set_new (core, "port_present", json_integer (port_present));
      json_object_set_new (core, "ships", json_integer (ships));
      json_object_set_new (core, "planets", json_integer (planets));
      json_object_set_new (core, "mines", json_integer (mines));
      json_object_set_new (core, "fighters", json_integer (fighters));
      if (beacon && *beacon)
	json_object_set_new (core, "beacon", json_string (beacon));
      else
	json_object_set_new (core, "beacon", json_null ());

      if (out_core)
	*out_core = core;
      rc = SQLITE_OK;
    }
  else
    {
      rc = SQLITE_ERROR;	/* caller maps to 1401 */
    }

done:
  if (st)
    sqlite3_finalize (st);
  pthread_mutex_unlock (&db_mutex);
  return rc;
}



int
db_session_create (int player_id, int ttl_seconds, char token_out[65])
{
  sqlite3 *db = NULL;
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;	// Default to error

  // 1. Acquire the lock FIRST.
  pthread_mutex_lock (&db_mutex);

  if (!token_out || player_id <= 0 || ttl_seconds <= 0)
    {
      rc = SQLITE_MISUSE;
      goto cleanup;
    }

  if (gen_session_token (token_out) != 0)
    {
      rc = SQLITE_ERROR;
      goto cleanup;
    }

  db = db_get_handle ();
  if (!db)
    {
      rc = SQLITE_ERROR;
      goto cleanup;
    }

  time_t now = time (NULL);
  long long exp = (long long) now + ttl_seconds;

  const char *sql =
    "INSERT INTO sessions(token, player_id, expires, created_at) VALUES(?1, ?2, ?3, ?4);";

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    goto cleanup;

  sqlite3_bind_text (st, 1, token_out, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (st, 2, player_id);
  sqlite3_bind_int64 (st, 3, exp);
  sqlite3_bind_int64 (st, 4, (sqlite3_int64) now);

  rc = sqlite3_step (st);

  // Check if the step was successful
  if (rc == SQLITE_DONE)
    {
      rc = SQLITE_OK;
    }
  else
    {
      rc = SQLITE_ERROR;
    }

cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  // 2. Release the lock at the end.
  pthread_mutex_unlock (&db_mutex);

  return rc;
}



int
db_session_lookup (const char *token, int *out_player_id,
		   long long *out_expires_epoch)
{
  sqlite3 *db = NULL;
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;	// Default to error

  // 1. Acquire the lock before any database access
  pthread_mutex_lock (&db_mutex);

  // Sanity checks
  if (!token)
    {
      rc = SQLITE_MISUSE;
      goto cleanup;
    }

  // Initialize output variables
  if (out_player_id)
    *out_player_id = 0;
  if (out_expires_epoch)
    *out_expires_epoch = 0;

  db = db_get_handle ();
  if (!db)
    {
      rc = SQLITE_ERROR;
      goto cleanup;
    }

  const char *sql = "SELECT player_id, expires FROM sessions WHERE token=?1;";
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    goto cleanup;

  sqlite3_bind_text (st, 1, token, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step (st);

  if (rc == SQLITE_ROW)
    {
      int pid = sqlite3_column_int (st, 0);
      long long exp = sqlite3_column_int64 (st, 1);

      time_t now = time (NULL);
      if (exp <= (long long) now)
	{
	  rc = SQLITE_NOTFOUND;	// Token expired
	  goto cleanup;
	}

      if (out_player_id)
	*out_player_id = pid;
      if (out_expires_epoch)
	*out_expires_epoch = exp;

      rc = SQLITE_OK;
    }
  else
    {
      // No row found or an error occurred during step
      rc = SQLITE_NOTFOUND;
    }

cleanup:
  // 2. Finalize the statement if it was prepared
  if (st)
    {
      sqlite3_finalize (st);
    }

  // 3. Release the lock before returning
  pthread_mutex_unlock (&db_mutex);

  return rc;
}

/* Revoke */
int
db_session_revoke (const char *token)
{
  sqlite3 *db = NULL;
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;	// Default to error

  // 1. Acquire the lock FIRST to ensure thread safety
  pthread_mutex_lock (&db_mutex);

  if (!token)
    {
      rc = SQLITE_MISUSE;
      goto cleanup;
    }

  db = db_get_handle ();
  if (!db)
    {
      rc = SQLITE_ERROR;
      goto cleanup;
    }

  const char *sql = "DELETE FROM sessions WHERE token=?1;";
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    goto cleanup;

  sqlite3_bind_text (st, 1, token, -1, SQLITE_TRANSIENT);

  rc = sqlite3_step (st);

  if (rc == SQLITE_DONE)
    {
      rc = SQLITE_OK;
    }

cleanup:
  // 2. Finalize the statement if it was prepared
  if (st)
    {
      sqlite3_finalize (st);
    }

  // 3. Release the lock at the end
  pthread_mutex_unlock (&db_mutex);

  return rc;
}

/* Revoke - Internal Unlocked Helper */
static int
db_session_revoke_unlocked (const char *token)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_ERROR;

  const char *sql = "DELETE FROM sessions WHERE token=?1;";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_text (st, 1, token, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? SQLITE_OK : rc;
}

/* Lookup - Internal Unlocked Helper */
static int
db_session_lookup_unlocked (const char *token, int *out_player_id,
			    long long *out_expires_epoch)
{
  if (!token)
    return SQLITE_MISUSE;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_ERROR;

  const char *sql = "SELECT player_id, expires FROM sessions WHERE token=?1;";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_text (st, 1, token, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step (st);

  if (rc == SQLITE_ROW)
    {
      int pid = sqlite3_column_int (st, 0);
      long long exp = sqlite3_column_int64 (st, 1);

      sqlite3_finalize (st);

      time_t now = time (NULL);
      if (exp <= (long long) now)
	return SQLITE_NOTFOUND;	/* expired */

      if (out_player_id)
	*out_player_id = pid;
      if (out_expires_epoch)
	*out_expires_epoch = exp;

      return SQLITE_OK;
    }

  sqlite3_finalize (st);
  return SQLITE_NOTFOUND;
}

/* Create - Internal Unlocked Helper */
static int
db_session_create_unlocked (int player_id, int ttl_seconds,
			    char token_out[65])
{
  if (!token_out || player_id <= 0 || ttl_seconds <= 0)
    return SQLITE_MISUSE;

  if (gen_session_token (token_out) != 0)
    return SQLITE_ERROR;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_ERROR;

  time_t now = time (NULL);
  long long exp = (long long) now + ttl_seconds;

  const char *sql =
    "INSERT INTO sessions(token, player_id, expires, created_at) VALUES(?1, ?2, ?3, ?4);";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_text (st, 1, token_out, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (st, 2, player_id);
  sqlite3_bind_int64 (st, 3, exp);
  sqlite3_bind_int64 (st, 4, (sqlite3_int64) now);

  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
}

/* The public, thread-safe session refresh function */
int
db_session_refresh (const char *old_token, int ttl_seconds,
		    char token_out[65], int *out_player_id)
{
  if (!old_token || !token_out)
    return SQLITE_MISUSE;

  int pid = 0;
  long long exp = 0;
  int rc = SQLITE_ERROR;

  // 1. Acquire the lock to make the entire sequence atomic.
  pthread_mutex_lock (&db_mutex);

  // 2. Perform all operations using the unlocked helpers.
  rc = db_session_lookup_unlocked (old_token, &pid, &exp);
  if (rc != SQLITE_OK)
    goto cleanup;

  rc = db_session_revoke_unlocked (old_token);
  if (rc != SQLITE_OK)
    goto cleanup;

  rc = db_session_create_unlocked (pid, ttl_seconds, token_out);
  if (rc == SQLITE_OK && out_player_id)
    *out_player_id = pid;

cleanup:
  // 3. Release the lock before returning.
  pthread_mutex_unlock (&db_mutex);

  return rc;
}


/* Public, thread-safe wrapper */
int
db_ensure_idempotency_schema (void)
{
  int rc;
  pthread_mutex_lock (&db_mutex);
  rc = db_ensure_idempotency_schema_unlocked ();
  pthread_mutex_unlock (&db_mutex);
  return rc;
}

/* Actual work; assumes db_mutex is already held */
static int
db_ensure_idempotency_schema_unlocked (void)
{
  sqlite3 *db = db_get_handle ();
  char *errmsg = NULL;
  int rc = SQLITE_ERROR;

  if (!db)
    return SQLITE_ERROR;

  rc = sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    goto fail;

  rc = sqlite3_exec (db,
		     "CREATE TABLE IF NOT EXISTS idempotency ("
		     "  key       TEXT PRIMARY KEY, "
		     "  cmd       TEXT NOT NULL, "
		     "  req_fp    TEXT NOT NULL, "
		     "  response  TEXT, "
		     "  created_at  INTEGER NOT NULL, "
		     "  updated_at  INTEGER" ");", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    goto rollback;

  rc = sqlite3_exec (db,
		     "CREATE INDEX IF NOT EXISTS idx_idemp_cmd ON idempotency(cmd);",
		     NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    goto rollback;

  rc = sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    goto fail;

  rc = SQLITE_OK;
rollback:

  if (rc != SQLITE_OK)
    sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
fail:
  if (errmsg)
    {
      LOGE ("[DB] idempotency schema: %s", errmsg);
      sqlite3_free (errmsg);
    }
  return rc;
}


int
db_idemp_try_begin (const char *key, const char *cmd, const char *req_fp)
{
  sqlite3 *db = NULL;
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;	// Default to error

  // 1. Acquire the lock before accessing the database
  pthread_mutex_lock (&db_mutex);

  if (!key || !cmd || !req_fp)
    {
      rc = SQLITE_MISUSE;
      goto cleanup;
    }

  db = db_get_handle ();
  if (!db)
    {
      rc = SQLITE_ERROR;
      goto cleanup;
    }

  time_t now = time (NULL);
  const char *sql =
    "INSERT INTO idempotency(key, cmd, req_fp, created_at) VALUES(?1, ?2, ?3, ?4);";

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    goto cleanup;

  sqlite3_bind_text (st, 1, key, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 2, cmd, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 3, req_fp, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64 (st, 4, (sqlite3_int64) now);

  rc = sqlite3_step (st);

  if (rc == SQLITE_DONE)
    {
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_CONSTRAINT)
    {
      rc = SQLITE_CONSTRAINT;
    }
  else
    {
      rc = SQLITE_ERROR;
    }

cleanup:
  // 2. Finalize the statement if it was prepared
  if (st)
    {
      sqlite3_finalize (st);
    }

  // 3. Release the lock before returning
  pthread_mutex_unlock (&db_mutex);

  return rc;
}


int
db_idemp_fetch (const char *key, char **out_cmd, char **out_req_fp,
		char **out_response_json)
{
  sqlite3 *db = NULL;
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;	// Default to error

  // 1. Acquire the lock FIRST to ensure thread safety
  pthread_mutex_lock (&db_mutex);

  if (!key)
    {
      rc = SQLITE_MISUSE;
      goto cleanup;
    }

  // Ensure output pointers are NULL on failure
  if (out_cmd)
    *out_cmd = NULL;
  if (out_req_fp)
    *out_req_fp = NULL;
  if (out_response_json)
    *out_response_json = NULL;

  db = db_get_handle ();
  if (!db)
    {
      rc = SQLITE_ERROR;
      goto cleanup;
    }

  const char *sql =
    "SELECT cmd, req_fp, response FROM idempotency WHERE key=?1;";

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    goto cleanup;

  sqlite3_bind_text (st, 1, key, -1, SQLITE_TRANSIENT);

  rc = sqlite3_step (st);

  if (rc == SQLITE_ROW)
    {
      if (out_cmd)
	{
	  const unsigned char *t = sqlite3_column_text (st, 0);
	  *out_cmd = t ? strdup ((const char *) t) : NULL;
	}
      if (out_req_fp)
	{
	  const unsigned char *t = sqlite3_column_text (st, 1);
	  *out_req_fp = t ? strdup ((const char *) t) : NULL;
	}
      if (out_response_json)
	{
	  const unsigned char *t = sqlite3_column_text (st, 2);
	  *out_response_json = t ? strdup ((const char *) t) : NULL;
	}

      rc = SQLITE_OK;
    }
  else
    {
      rc = SQLITE_NOTFOUND;
    }

cleanup:
  // 2. Finalize the statement if it was prepared
  if (st)
    {
      sqlite3_finalize (st);
    }

  // 3. Release the lock before returning
  pthread_mutex_unlock (&db_mutex);

  return rc;
}

int
db_idemp_store_response (const char *key, const char *response_json)
{
  sqlite3 *db = NULL;
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;	// Default to error

  // 1. Acquire the lock FIRST to ensure thread safety
  pthread_mutex_lock (&db_mutex);

  if (!key || !response_json)
    {
      rc = SQLITE_MISUSE;
      goto cleanup;
    }

  db = db_get_handle ();
  if (!db)
    {
      rc = SQLITE_ERROR;
      goto cleanup;
    }

  time_t now = time (NULL);
  const char *sql =
    "UPDATE idempotency SET response=?1, updated_at=?2 WHERE key=?3;";

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    goto cleanup;

  sqlite3_bind_text (st, 1, response_json, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64 (st, 2, (sqlite3_int64) now);
  sqlite3_bind_text (st, 3, key, -1, SQLITE_TRANSIENT);

  rc = sqlite3_step (st);

  if (rc == SQLITE_DONE)
    {
      rc = SQLITE_OK;
    }
  else
    {
      rc = SQLITE_ERROR;
    }

cleanup:
  // 2. Finalize the statement if it was prepared
  if (st)
    {
      sqlite3_finalize (st);
    }

  // 3. Release the lock before returning
  pthread_mutex_unlock (&db_mutex);

  return rc;
}

/* Helper: prepare, bind one int, and return stmt or NULL */
// This is now an internal helper. The caller must hold the mutex.
static sqlite3_stmt *
prep1i_unlocked (sqlite3 *db, const char *sql, int v)
{
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    return NULL;
  sqlite3_bind_int (st, 1, v);
  return st;
}


int
db_sector_info_json (int sector_id, json_t **out)
{
  sqlite3 *db = NULL;
  sqlite3_stmt *st = NULL;
  json_t *root = NULL;
  int rc = SQLITE_ERROR;	// Default to error

  // 1. Acquire the lock FIRST to ensure thread safety
  pthread_mutex_lock (&db_mutex);

  if (out)
    *out = NULL;

  db = db_get_handle ();
  if (!db)
    goto cleanup;

  root = json_object ();
  json_object_set_new (root, "sector_id", json_integer (sector_id));

  /* 0) Sector core: name (+ optional beacon if present in schema) */
  {
    const char *sql_min = "SELECT name FROM sectors WHERE id=?1;";
    if (sqlite3_prepare_v2 (db, sql_min, -1, &st, NULL) == SQLITE_OK)
      {
	sqlite3_bind_int (st, 1, sector_id);
	if (sqlite3_step (st) == SQLITE_ROW)
	  {
	    const char *nm = (const char *) sqlite3_column_text (st, 0);
	    if (nm)
	      json_object_set_new (root, "name", json_string (nm));
	  }
	sqlite3_finalize (st);
	st = NULL;
      }

    const char *sql_rich =
      "SELECT "
      "  COALESCE(beacon_text, ''), COALESCE(beacon_by, 0), "
      "  COALESCE(security_level, 0), COALESCE(safe_zone, 0) "
      "FROM sectors WHERE id=?1;";
    if (sqlite3_prepare_v2 (db, sql_rich, -1, &st, NULL) == SQLITE_OK)
      {
	sqlite3_bind_int (st, 1, sector_id);
	if (sqlite3_step (st) == SQLITE_ROW)
	  {
	    const char *btxt = (const char *) sqlite3_column_text (st, 0);
	    int bby = sqlite3_column_int (st, 1);
	    int sec_level = sqlite3_column_int (st, 2);
	    int safe = sqlite3_column_int (st, 3);

	    if (btxt && btxt[0])
	      {
		json_t *b =
		  json_pack ("{s:s, s:i}", "text", btxt, "by_player_id", bby);
		json_object_set_new (root, "beacon", b);
	      }
	    if (sec_level != 0 || safe != 0)
	      {
		json_t *sec =
		  json_pack ("{s:i, s:b}", "level", sec_level, "is_safe_zone",
			     safe ? 1 : 0);
		json_object_set_new (root, "security", sec);
	      }
	  }
	sqlite3_finalize (st);
	st = NULL;
      }
  }
/* 1) Adjacency via sector_adjacency(neighbors CSV) */
  {
    const char *sql =
      "SELECT neighbors FROM sector_adjacency WHERE sector_id=?1;";
    if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) == SQLITE_OK)
      {
	sqlite3_bind_int (st, 1, sector_id);
	if (sqlite3_step (st) == SQLITE_ROW)
	  {
	    const unsigned char *neighbors = sqlite3_column_text (st, 0);
	    json_t *adj = parse_neighbors_csv (neighbors);
	    if (json_array_size (adj) > 0)
	      json_object_set_new (root, "adjacent", adj);
	    else
	      json_decref (adj);
	  }
	sqlite3_finalize (st);
	st = NULL;
      }
  }

  /* 2) Port (first port in sector, if any) via sector_ports */
  {
    const char *sql =
      "SELECT port_id, port_name, COALESCE(type_code,''), COALESCE(is_open,1) "
      "FROM sector_ports WHERE sector_id=?1 ORDER BY port_id LIMIT 1;";
    if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) == SQLITE_OK)
      {
	sqlite3_bind_int (st, 1, sector_id);
	if (sqlite3_step (st) == SQLITE_ROW)
	  {
	    int pid = sqlite3_column_int (st, 0);
	    const char *pname = (const char *) sqlite3_column_text (st, 1);
	    const char *ptype = (const char *) sqlite3_column_text (st, 2);
	    int is_open = sqlite3_column_int (st, 3);

	    json_t *port = json_object ();
	    json_object_set_new (port, "id", json_integer (pid));
	    if (pname)
	      json_object_set_new (port, "name", json_string (pname));
	    if (ptype)
	      json_object_set_new (port, "type", json_string (ptype));
	    json_object_set_new (port, "status",
				 json_string (is_open ? "open" : "closed"));
	    json_object_set_new (root, "port", port);
	  }
	sqlite3_finalize (st);
	st = NULL;
      }
  }

  /* 3) Planets via sector_planets */
  {
    const char *sql =
      "SELECT planet_id, planet_name, COALESCE(owner_id,0) "
      "FROM sector_planets WHERE sector_id=?1 ORDER BY planet_id;";
    if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) == SQLITE_OK)
      {
	sqlite3_bind_int (st, 1, sector_id);
	json_t *arr = json_array ();
	while (sqlite3_step (st) == SQLITE_ROW)
	  {
	    int id = sqlite3_column_int (st, 0);
	    const char *nm = (const char *) sqlite3_column_text (st, 1);
	    int owner = sqlite3_column_int (st, 2);
	    json_t *pl =
	      json_pack ("{s:i, s:i}", "id", id, "owner_id", owner);
	    if (nm)
	      json_object_set_new (pl, "name", json_string (nm));
	    json_array_append_new (arr, pl);
	  }
	sqlite3_finalize (st);
	st = NULL;

	if (json_array_size (arr) > 0)
	  json_object_set_new (root, "planets", arr);
	else
	  json_decref (arr);
      }
  }

  /* 4) Entities via ships_by_sector (treat them as 'ship' entities) */
  {
    const char *sql =
      "SELECT ship_id, COALESCE(ship_name,''), COALESCE(owner_player_id,0) "
      "FROM ships_by_sector WHERE sector_id=?1 ORDER BY ship_id;";
    if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) == SQLITE_OK)
      {
	sqlite3_bind_int (st, 1, sector_id);
	json_t *arr = json_array ();
	while (sqlite3_step (st) == SQLITE_ROW)
	  {
	    int id = sqlite3_column_int (st, 0);
	    const char *nm = (const char *) sqlite3_column_text (st, 1);
	    int owner = sqlite3_column_int (st, 2);
	    json_t *e =
	      json_pack ("{s:i, s:s, s:i}", "id", id, "kind", "ship",
			 "owner_id", owner);
	    if (nm && *nm)
	      json_object_set_new (e, "name", json_string (nm));
	    json_array_append_new (arr, e);
	  }
	sqlite3_finalize (st);
	st = NULL;

	if (json_array_size (arr) > 0)
	  json_object_set_new (root, "entities", arr);
	else
	  json_decref (arr);
      }
  }

  /* 5) Security/topology flags via sector_summary (if present) */
  {
    const char *sql =
      "SELECT "
      "  COALESCE(degree, NULL), "
      "  COALESCE(dead_in, NULL), "
      "  COALESCE(dead_out, NULL), "
      "  COALESCE(is_isolated, NULL) "
      "FROM sector_summary WHERE sector_id=?1;";
    if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) == SQLITE_OK)
      {
	sqlite3_bind_int (st, 1, sector_id);
	if (sqlite3_step (st) == SQLITE_ROW)
	  {
	    int has_any = 0;
	    json_t *sec = json_object ();

	    if (sqlite3_column_type (st, 0) != SQLITE_NULL)
	      {
		json_object_set_new (sec, "degree",
				     json_integer (sqlite3_column_int
						   (st, 0)));
		has_any = 1;
	      }
	    if (sqlite3_column_type (st, 1) != SQLITE_NULL)
	      {
		json_object_set_new (sec, "dead_in",
				     json_integer (sqlite3_column_int
						   (st, 1)));
		has_any = 1;
	      }
	    if (sqlite3_column_type (st, 2) != SQLITE_NULL)
	      {
		json_object_set_new (sec, "dead_out",
				     json_integer (sqlite3_column_int
						   (st, 2)));
		has_any = 1;
	      }
	    if (sqlite3_column_type (st, 3) != SQLITE_NULL)
	      {
		json_object_set_new (sec, "is_isolated",
				     sqlite3_column_int (st,
							 3) ? json_true () :
				     json_false ());
		has_any = 1;
	      }
	    if (has_any)
	      json_object_set_new (root, "security", sec);
	    else
	      json_decref (sec);
	  }
	sqlite3_finalize (st);
	st = NULL;
      }
  }

  if (out)
    {
      *out = root;
      rc = SQLITE_OK;
    }
  else
    {
      json_decref (root);
      root = NULL;
      rc = SQLITE_OK;
    }

cleanup:
  if (st)
    sqlite3_finalize (st);

  pthread_mutex_unlock (&db_mutex);

  return rc;
}


/* Parse "2,3,4,5" -> [2,3,4,5] */
static json_t *
parse_neighbors_csv (const unsigned char *txt)
{
  json_t *arr = json_array ();
  if (!txt)
    return arr;
  const char *p = (const char *) txt;

  while (*p)
    {
      while (*p == ' ' || *p == '\t')
	p++;			/* trim left */
      const char *start = p;
      while (*p && *p != ',')
	p++;
      int len = (int) (p - start);
      if (len > 0)
	{
	  char buf[32];
	  if (len >= (int) sizeof (buf))
	    len = (int) sizeof (buf) - 1;	/* defensive */
	  memcpy (buf, start, len);
	  buf[len] = '\0';
	  int id = atoi (buf);
	  if (id > 0)
	    json_array_append_new (arr, json_integer (id));
	}
      if (*p == ',')
	p++;			/* skip comma */
    }
  return arr;
}

/* ---------- BASIC SECTOR (id + name) ---------- */


int
db_sector_basic_json (int sector_id, json_t **out_obj)
{
  sqlite3 *dbh = NULL;
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;	// Default to error

  // Acquire the lock first
  pthread_mutex_lock (&db_mutex);

  if (!out_obj)
    goto cleanup;		// Nothing to return the data to

  *out_obj = NULL;
  dbh = db_get_handle ();
  if (!dbh)
    goto cleanup;

  const char *sql = "SELECT id, name FROM sectors WHERE id = ?";
  rc = sqlite3_prepare_v2 (dbh, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    goto cleanup;

  sqlite3_bind_int (st, 1, sector_id);

  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      *out_obj = json_pack ("{s:i s:s}",
			    "sector_id", sqlite3_column_int (st, 0),
			    "name", (const char *) sqlite3_column_text (st,
									1));
      rc = *out_obj ? SQLITE_OK : SQLITE_NOMEM;
    }
  else
    {
      // If sector not found, still return an object with sector_id
      *out_obj = json_pack ("{s:i}", "sector_id", sector_id);
      rc = *out_obj ? SQLITE_OK : SQLITE_NOMEM;
    }

cleanup:
  // Finalize the statement if it was prepared
  if (st)
    {
      sqlite3_finalize (st);
    }

  // Release the lock
  pthread_mutex_unlock (&db_mutex);

  return rc;
}


/* ---------- ADJACENT WARPS (from sector_warps) ---------- */


/**
 * @brief Retrieves a list of adjacent sectors for a given sector, returning them as a JSON array.
 * * This function is thread-safe as all database operations are protected by a mutex lock.
 * It queries the database for adjacent sectors, trying a standard schema first and
 * falling back to a schema with quoted reserved words if necessary.
 *
 * @param sector_id The ID of the sector to query.
 * @param out_array A pointer to a json_t* where the resulting JSON array will be stored.
 * @return SQLITE_OK on success, or an SQLite error code on failure.
 */
//////////////////
int
db_adjacent_sectors_json (int sector_id, json_t **out_array)
{
  if (out_array)
    *out_array = NULL;
  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_ERROR;

  const char *sql =
    "SELECT to_sector FROM sector_warps WHERE from_sector = ?1 ORDER BY to_sector";
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;

  pthread_mutex_lock (&db_mutex);

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    goto done;

  sqlite3_bind_int (st, 1, sector_id);

  json_t *arr = json_array ();
  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      int to = sqlite3_column_int (st, 0);
      json_array_append_new (arr, json_integer (to));
    }
  if (rc == SQLITE_DONE)
    {
      if (out_array)
	*out_array = arr;
      rc = SQLITE_OK;
    }
  else
    {
      json_decref (arr);
      rc = SQLITE_ERROR;
    }

done:
  if (st)
    sqlite3_finalize (st);
  pthread_mutex_unlock (&db_mutex);
  return rc;
}

////////////////

/* ---------- PORTS AT SECTOR (visible only) ---------- */


int
db_port_info_json (int port_id, json_t **out_obj)
{
  sqlite3_stmt *st = NULL;
  json_t *port = NULL;
  json_t *commodities_array = NULL;
  int rc = SQLITE_ERROR;	// Default to error
  sqlite3 *dbh = NULL;

  pthread_mutex_lock (&db_mutex);

  if (!out_obj)
    goto cleanup;

  *out_obj = NULL;

  dbh = db_get_handle ();
  if (!dbh)
    goto cleanup;

  port = json_object ();
  if (!port)
    {
      rc = SQLITE_NOMEM;
      goto cleanup;
    }

  // First, get the basic port info and commodity on-hand/max values
  const char *port_sql =
    "SELECT id, name, type, techlevel, credits, sector, "
    "ore_on_hand, organics_on_hand, equipment_on_hand, petty_cash "
    "FROM ports WHERE id=?;";
  rc = sqlite3_prepare_v2 (dbh, port_sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    goto cleanup;

  sqlite3_bind_int (st, 1, port_id);
  if (sqlite3_step (st) != SQLITE_ROW)
    {
      rc = SQLITE_NOTFOUND;
      goto cleanup;
    }

  json_object_set_new (port, "id", json_integer (sqlite3_column_int (st, 0)));
  json_object_set_new (port, "name",
		       json_string ((const char *)
				    sqlite3_column_text (st, 1)));
  json_object_set_new (port, "type",
		       json_string ((const char *)
				    sqlite3_column_text (st, 2)));
  json_object_set_new (port, "tech_level",
		       json_integer (sqlite3_column_int (st, 3)));
  json_object_set_new (port, "credits",
		       json_integer (sqlite3_column_int (st, 4)));
  json_object_set_new (port, "sector_id",
		       json_integer (sqlite3_column_int (st, 5)));
  json_object_set_new (port, "petty_cash", json_integer (sqlite3_column_int (st, 9)));	// petty_cash

  commodities_array = json_array ();
  if (!commodities_array)
    {
      rc = SQLITE_NOMEM;
      goto cleanup;
    }

  // Add Ore info
  json_array_append_new (commodities_array, json_pack ("{s:s, s:i}", "commodity", "ore", "quantity", sqlite3_column_int (st, 6)));	// ore_on_hand
  // Add Organics info
  json_array_append_new (commodities_array, json_pack ("{s:s, s:i}", "commodity", "organics", "quantity", sqlite3_column_int (st, 7)));	// organics_on_hand
  // Add Equipment info
  json_array_append_new (commodities_array, json_pack ("{s:s, s:i}", "commodity", "equipment", "quantity", sqlite3_column_int (st, 8)));	// equipment_on_hand

  sqlite3_finalize (st);
  st = NULL;

  rc = SQLITE_OK;

cleanup:
  if (st)
    sqlite3_finalize (st);

  if (rc == SQLITE_OK)
    {
      json_object_set_new (port, "commodities", commodities_array);
      *out_obj = port;
    }
  else
    {
      if (port)
	json_decref (port);
      if (commodities_array)
	json_decref (commodities_array);
    }

  pthread_mutex_unlock (&db_mutex);

  return rc;
}

/* ---------- PLAYERS AT SECTOR (lightweight: id + name) ---------- */

int
db_players_at_sector_json (int sector_id, json_t **out_array)
{
  sqlite3 *dbh = NULL;
  sqlite3_stmt *st = NULL;
  json_t *arr = NULL;
  int rc = SQLITE_ERROR;	// Default to error

  // 1. Acquire the lock FIRST
  pthread_mutex_lock (&db_mutex);

  if (!out_array)
    goto cleanup;

  *out_array = NULL;
  dbh = db_get_handle ();
  if (!dbh)
    goto cleanup;

  /* Prefer explicit 'sector' and 'name' columns */
  const char *sql =
    "SELECT id, COALESCE(name, player_name) AS pname FROM players WHERE sector = ? ORDER BY id";
  rc = sqlite3_prepare_v2 (dbh, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      /* Fallback if some builds use 'sector' instead of 'sector' */
      const char *sql2 =
	"SELECT id, COALESCE(name, player_name) AS pname FROM players WHERE sector = ? ORDER BY id";
      rc = sqlite3_prepare_v2 (dbh, sql2, -1, &st, NULL);
      if (rc != SQLITE_OK)
	goto cleanup;
    }

  sqlite3_bind_int (st, 1, sector_id);

  arr = json_array ();
  if (!arr)
    {
      rc = SQLITE_NOMEM;
      goto cleanup;
    }

  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      const unsigned char *nm = sqlite3_column_text (st, 1);
      json_t *o = json_pack ("{s:i s:s}",
			     "id", sqlite3_column_int (st, 0),
			     "name", nm ? (const char *) nm : "");
      if (!o)
	{
	  rc = SQLITE_NOMEM;
	  goto cleanup;
	}
      json_array_append_new (arr, o);
    }

  if (rc == SQLITE_DONE)
    {
      *out_array = arr;
      rc = SQLITE_OK;
    }
  else
    {
      json_decref (arr);
      arr = NULL;
    }

cleanup:
  if (st)
    sqlite3_finalize (st);

  // 2. Release the lock LAST
  pthread_mutex_unlock (&db_mutex);

  return rc;
}


/* ---------- BEACONS AT SECTOR (optional table) ---------- */

int
db_beacons_at_sector_json (int sector_id, json_t **out_array)
{
  sqlite3 *dbh = NULL;
  sqlite3_stmt *st = NULL;
  json_t *arr = NULL;
  int rc = SQLITE_ERROR;

  // 1. Acquire the lock FIRST
  pthread_mutex_lock (&db_mutex);

  if (!out_array)
    goto cleanup;

  *out_array = NULL;

  dbh = db_get_handle ();
  if (!dbh)
    goto cleanup;

  const char *sql =
    "SELECT id, owner_id, message "
    "FROM beacons WHERE sector_id = ? ORDER BY id";

  rc = sqlite3_prepare_v2 (dbh, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    goto cleanup;

  sqlite3_bind_int (st, 1, sector_id);

  arr = json_array ();
  if (!arr)
    {
      rc = SQLITE_NOMEM;
      goto cleanup;
    }

  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      json_t *o = json_pack ("{s:i s:i s:s}",
			     "id", sqlite3_column_int (st, 0),
			     "owner_id", sqlite3_column_int (st, 1),
			     "message",
			     (const char *) sqlite3_column_text (st, 2));
      if (!o)
	{
	  rc = SQLITE_NOMEM;
	  json_decref (arr);
	  arr = NULL;
	  goto cleanup;
	}
      json_array_append_new (arr, o);
    }

  if (rc == SQLITE_DONE)
    {
      *out_array = arr;
      rc = SQLITE_OK;
    }
  else
    {
      json_decref (arr);
      arr = NULL;
    }

cleanup:
  if (st)
    sqlite3_finalize (st);

  // 2. Release the lock LAST
  pthread_mutex_unlock (&db_mutex);

  return rc;
}


/* ---------- PLANETS AT SECTOR ---------- */

int
db_planets_at_sector_json (int sector_id, json_t **out_array)
{
  sqlite3 *dbh = NULL;
  sqlite3_stmt *st = NULL;
  json_t *arr = NULL;
  int rc = SQLITE_ERROR;	// Default to error

  // 1. Acquire the lock FIRST
  pthread_mutex_lock (&db_mutex);

  if (!out_array)
    goto cleanup;

  *out_array = NULL;

  dbh = db_get_handle ();
  if (!dbh)
    goto cleanup;

  const char *sql =
    "SELECT id, name, owner, fighters, colonist, "
    "fuel, organics, equipment, citadel_level FROM planets WHERE sector = ? ORDER BY id;";

  rc = sqlite3_prepare_v2 (dbh, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    goto cleanup;

  sqlite3_bind_int (st, 1, sector_id);

  arr = json_array ();
  if (!arr)
    {
      rc = SQLITE_NOMEM;
      goto cleanup;
    }

  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      json_t *o_planet = json_object ();
      if (!o_planet)
	{
	  rc = SQLITE_NOMEM;
	  goto cleanup;
	}

      json_object_set_new (o_planet, "id",
			   json_integer (sqlite3_column_int (st, 0)));
      json_object_set_new (o_planet, "name",
			   json_string ((const char *)
					sqlite3_column_text (st, 1)));
      json_object_set_new (o_planet, "owner_id",
			   (sqlite3_column_type (st, 2) ==
			    SQLITE_NULL) ? json_null () :
			   json_integer (sqlite3_column_int (st, 2)));
      json_object_set_new (o_planet, "fighters",
			   (sqlite3_column_type (st, 3) ==
			    SQLITE_NULL) ? json_null () :
			   json_integer (sqlite3_column_int (st, 3)));

      json_t *colonists = json_object ();
      if (!colonists)
	{
	  json_decref (o_planet);
	  rc = SQLITE_NOMEM;
	  goto cleanup;
	}
      json_object_set_new (colonists, "fuel",
			   (sqlite3_column_type (st, 4) ==
			    SQLITE_NULL) ? json_null () :
			   json_integer (sqlite3_column_int (st, 4)));
      json_object_set_new (colonists, "organics",
			   (sqlite3_column_type (st, 5) ==
			    SQLITE_NULL) ? json_null () :
			   json_integer (sqlite3_column_int (st, 5)));
      json_object_set_new (colonists, "equipment",
			   (sqlite3_column_type (st, 6) ==
			    SQLITE_NULL) ? json_null () :
			   json_integer (sqlite3_column_int (st, 6)));
      json_object_set_new (o_planet, "colonists", colonists);

      json_t *resources = json_object ();
      if (!resources)
	{
	  json_decref (o_planet);
	  rc = SQLITE_NOMEM;
	  goto cleanup;
	}
      json_object_set_new (resources, "fuel",
			   (sqlite3_column_type (st, 7) ==
			    SQLITE_NULL) ? json_null () :
			   json_integer (sqlite3_column_int (st, 7)));
      json_object_set_new (resources, "organics",
			   (sqlite3_column_type (st, 8) ==
			    SQLITE_NULL) ? json_null () :
			   json_integer (sqlite3_column_int (st, 8)));
      json_object_set_new (resources, "equipment",
			   (sqlite3_column_type (st, 9) ==
			    SQLITE_NULL) ? json_null () :
			   json_integer (sqlite3_column_int (st, 9)));
      json_object_set_new (o_planet, "resources", resources);

      json_object_set_new (o_planet, "citadel_level",
			   (sqlite3_column_type (st, 10) ==
			    SQLITE_NULL) ? json_null () :
			   json_integer (sqlite3_column_int (st, 10)));

      json_array_append_new (arr, o_planet);
    }

  if (rc != SQLITE_DONE)
    {
      json_decref (arr);
      arr = NULL;
      goto cleanup;
    }

  *out_array = arr;
  rc = SQLITE_OK;

cleanup:
  if (st)
    sqlite3_finalize (st);

  // 2. Release the lock LAST
  pthread_mutex_unlock (&db_mutex);

  return rc;
}

int
db_player_set_sector (int player_id, int sector_id)
{
  sqlite3 *dbh = NULL;
  sqlite3_stmt *st_find_ship = NULL;
  sqlite3_stmt *st_update_player = NULL;
  sqlite3_stmt *st_update_ship = NULL;

  int ret_code = SQLITE_ERROR;
  int rc;
  bool transaction_started = false;

  // 1. Acquire the lock at the beginning of the function.
  pthread_mutex_lock (&db_mutex);

  dbh = db_get_handle ();
  if (!dbh)
    {
      goto cleanup;
    }

  // 2. Start the transaction. This is the first database command.
  rc = sqlite3_exec (dbh, "BEGIN;", NULL, NULL, NULL);
  if (rc != SQLITE_OK)
    {
      ret_code = rc;
      goto cleanup;
    }
  transaction_started = true;	// Mark transaction as started

  // First, get the player's active ship ID
  const char *sql_find_ship = "SELECT ship FROM players WHERE id=?;";
  rc = sqlite3_prepare_v2 (dbh, sql_find_ship, -1, &st_find_ship, NULL);
  if (rc != SQLITE_OK)
    {
      ret_code = rc;
      goto cleanup;
    }
  sqlite3_bind_int (st_find_ship, 1, player_id);
  rc = sqlite3_step (st_find_ship);
  int ship_id = -1;
  if (rc == SQLITE_ROW)
    {
      ship_id = sqlite3_column_int (st_find_ship, 0);
      rc = sqlite3_step (st_find_ship);	// Move past the single row
    }

  if (rc != SQLITE_DONE)
    {
      ret_code = rc;
      goto cleanup;
    }

  // Update the player's sector in the players table
  const char *sql_update_player = "UPDATE players SET sector=? WHERE id=?;";
  rc =
    sqlite3_prepare_v2 (dbh, sql_update_player, -1, &st_update_player, NULL);
  if (rc != SQLITE_OK)
    {
      ret_code = rc;
      goto cleanup;
    }
  sqlite3_bind_int (st_update_player, 1, sector_id);
  sqlite3_bind_int (st_update_player, 2, player_id);
  rc = sqlite3_step (st_update_player);
  if (rc != SQLITE_DONE)
    {
      ret_code = rc;
      goto cleanup;
    }

  // Update the ship's location using the retrieved ship ID
  const char *sql_update_ship = "UPDATE ships SET sector=? WHERE id=?;";
  rc = sqlite3_prepare_v2 (dbh, sql_update_ship, -1, &st_update_ship, NULL);
  if (rc != SQLITE_OK)
    {
      ret_code = rc;
      goto cleanup;
    }
  sqlite3_bind_int (st_update_ship, 1, sector_id);
  sqlite3_bind_int (st_update_ship, 2, ship_id);
  rc = sqlite3_step (st_update_ship);
  if (rc != SQLITE_DONE)
    {
      ret_code = rc;
      goto cleanup;
    }

  // All operations succeeded, set the return code to OK.
  ret_code = SQLITE_OK;

cleanup:
  // 3. Finalize all prepared statements.
  if (st_find_ship)
    sqlite3_finalize (st_find_ship);
  if (st_update_player)
    sqlite3_finalize (st_update_player);
  if (st_update_ship)
    sqlite3_finalize (st_update_ship);

  // 4. Handle the transaction based on the final status.
  if (transaction_started)
    {
      if (ret_code == SQLITE_OK)
	{
	  sqlite3_exec (dbh, "COMMIT;", NULL, NULL, NULL);
	}
      else
	{
	  sqlite3_exec (dbh, "ROLLBACK;", NULL, NULL, NULL);
	}
    }

  // 5. Release the lock at the very end.
  pthread_mutex_unlock (&db_mutex);

  return ret_code;
}

int
db_player_set_alignment (int player_id, int alignment)
{
  sqlite3 *db = db_get_handle ();
  if (!db || player_id <= 0)
    {
      return SQLITE_MISUSE;
    }

  int rc = SQLITE_ERROR;
  sqlite3_stmt *stmt = NULL;

  pthread_mutex_lock (&db_mutex);

  static const char *SQL_UPDATE_ALIGNMENT =
    "UPDATE players SET alignment = ? WHERE id = ?;";
  rc = sqlite3_prepare_v2 (db, SQL_UPDATE_ALIGNMENT, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("ERROR: db_player_set_alignment prepare failed: %s",
	    sqlite3_errmsg (db));
      goto cleanup;
    }

  sqlite3_bind_int (stmt, 1, alignment);
  sqlite3_bind_int (stmt, 2, player_id);

  rc = sqlite3_step (stmt);
  if (rc != SQLITE_DONE)
    {
      LOGE ("ERROR: db_player_set_alignment execution failed: %s",
	    sqlite3_errmsg (db));
      goto cleanup;
    }

  rc = SQLITE_OK;

cleanup:
  if (stmt)
    sqlite3_finalize (stmt);
  pthread_mutex_unlock (&db_mutex);
  return rc;
}

int
db_player_get_sector (int player_id, int *out_sector)
{
  sqlite3_stmt *st = NULL;
  int ret_code = SQLITE_ERROR;
  int rc;			// For SQLite's intermediate return codes.

  // 1. Acquire the lock at the very beginning of the function.
  pthread_mutex_lock (&db_mutex);

  // Initialize the output value to a safe default.
  if (out_sector)
    *out_sector = 0;

  sqlite3 *dbh = db_get_handle ();
  if (!dbh)
    {
      // The database handle is not valid, set return code and go to cleanup.
      goto cleanup;
    }

  const char *sql =
    "SELECT T2.sector FROM players AS T1 JOIN ships AS T2 ON T1.ship = T2.id WHERE T1.id = ?;";

  // 2. Prepare the statement. This is the first point of failure.
  rc = sqlite3_prepare_v2 (dbh, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      ret_code = rc;
      goto cleanup;
    }

  sqlite3_bind_int (st, 1, player_id);

  // 3. Step the statement.
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      // A row was returned successfully.
      if (out_sector)
	{
	  *out_sector =
	    sqlite3_column_type (st,
				 0) ==
	    SQLITE_NULL ? 0 : sqlite3_column_int (st, 0);
	}
      ret_code = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      // Query completed with no rows, which is a successful result.
      ret_code = SQLITE_OK;
    }
  else
    {
      // An error occurred during the step.
      ret_code = rc;
    }

cleanup:
  // 4. Finalize the statement. This must be done whether the function succeeded or failed.
  if (st)
    {
      sqlite3_finalize (st);
    }

  // 5. Release the lock. This is the final step before returning.
  pthread_mutex_unlock (&db_mutex);

  // 6. Return the final status code from a single point.
  return ret_code;
}


/* int */
/* db_player_info_json (int player_id, json_t **out) */
/* { */
/*   sqlite3_stmt *st = NULL; */
/*   json_t *obj = NULL; */
/*   int ret_code = SQLITE_ERROR; */

/*   // 1. Acquire the lock at the beginning of the function. */
/*   pthread_mutex_lock (&db_mutex); */

/*   // Initialize the output pointer. */
/*   if (out) */
/*     *out = NULL; */

/*   // Get the database handle. */
/*   sqlite3 *dbh = db_get_handle (); */
/*   if (!dbh) */
/*     { */
/*       goto cleanup; */
/*     } */

/*   // --- CORRECTED SQL QUERY --- */
/*   const char *sql = "SELECT " */
/*     " p.id, p.number, p.name, p.sector, " */
/*     " s.id                      AS ship_id, " */
/*     " COALESCE(s.id, 0)         AS ship_number, " */
/*     " COALESCE(s.name, '')      AS ship_name, " */
/*     " COALESCE(s.type_id, 0)    AS ship_type_id, " */
/*     " COALESCE(st.name, '')     AS ship_type_name, " */
/*     " COALESCE(s.holds, 0) AS ship_holds, " */
/*     " COALESCE(s.fighters, 0)   AS ship_fighters, " */
/*     " COALESCE(sectors.name, 'Unknown') AS sector_name " */
/*     "FROM players p " */
/*     "LEFT JOIN ships s      ON s.id = p.ship " */
/*     "LEFT JOIN shiptypes st ON st.id = s.type_id " */
/*     "LEFT JOIN sectors      ON sectors.id = p.sector " */
/*     "WHERE p.id = ?"; */

/*   // 2. Prepare the statement. Check for errors and jump to cleanup. */
/*   int rc = sqlite3_prepare_v2 (dbh, sql, -1, &st, NULL); */
/*   if (rc != SQLITE_OK) */
/*     { */
/*       ret_code = rc; */
/*       goto cleanup; */
/*     } */

/*   // 3. Bind the parameter. */
/*   sqlite3_bind_int (st, 1, player_id); */

/*   // 4. Get results and create the JSON object. */
/*   rc = sqlite3_step (st); */
/*   if (rc == SQLITE_ROW) */
/*     { */
/*       int pid = sqlite3_column_int (st, 0); */
/*       int pnum = sqlite3_column_int (st, 1); */
/*       const char *pname = (const char *) sqlite3_column_text (st, 2); */
/*       int psector = sqlite3_column_int (st, 3); */
/*       int ship_id = sqlite3_column_int (st, 4); */
/*       int ship_number = sqlite3_column_int (st, 5); */
/*       const char *sname = (const char *) sqlite3_column_text (st, 6); */
/*       int stype_id = sqlite3_column_int (st, 7); */
/*       const char *stype = (const char *) sqlite3_column_text (st, 8); */
/*       int sholds = sqlite3_column_int (st, 9); */
/*       int sfighters = sqlite3_column_int (st, 10); */
/*       const char *sector_name = (const char *) sqlite3_column_text (st, 11); */

/*       obj = json_pack ("{s:i, s:s, s:i, s:i, s:s, s:i, s:i, s:s, s:i, s:i, s:i, s:s}", */
/*                        "player_id", pid, */
/*                        "player_name", pname ? pname : "", */
/*                        "player_number", pnum, */
/*                        "sector_id", psector, */
/*                        "sector_name", sector_name ? sector_name : "Unknown", */
/*                        "ship_id", ship_id, */
/*                        "ship_number", ship_number, */
/*                        "ship_name", sname ? sname : "", */
/*                        "ship_type_id", stype_id, */
/*                        "ship_holds", sholds, */
/*                        "ship_fighters", sfighters, */
/*                        "ship_type_name", stype ? stype : ""); */

/*       ret_code = SQLITE_OK; */
/*     } */
/*   else if (rc == SQLITE_DONE) */
/*     { */
/*       ret_code = SQLITE_OK; */
/*       obj = json_object (); */
/*     } */
/*   else */
/*     { */
/*       ret_code = rc; */
/*     } */

/* cleanup: */
/*   // 5. Always finalize the SQLite statement if it was created. */
/*   if (st) */
/*     { */
/*       sqlite3_finalize (st); */
/*     } */

/*   // 6. Set the output pointer and ensure proper JSON cleanup. */
/*   if (ret_code == SQLITE_OK) */
/*     { */
/*       if (out) */
/*         *out = obj; */
/*       else */
/*         json_decref (obj); */
/*     } */
/*   else */
/*     { */
/*       if (obj) */
/*         json_decref (obj); */
/*     } */

/*   // 7. Always release the lock at the very end. */
/*   pthread_mutex_unlock (&db_mutex); */

/*   // 8. Single point of return. */
/*   return ret_code; */
/* } */





int
db_player_info_json (int player_id, json_t **out)
{
  sqlite3_stmt *st = NULL;
  json_t *root_obj = NULL;	/* Renamed from obj */
  int ret_code = SQLITE_ERROR;

  pthread_mutex_lock (&db_mutex);

  if (out)
    *out = NULL;

  sqlite3 *dbh = db_get_handle ();
  if (!dbh)
    {
      goto cleanup;
    }

  // This SQL query is correct from our last fix
  const char *sql = "SELECT " " p.id, p.number, p.name, "	// Player info (0, 1, 2)
    " p.ship, "			// Player's ship ID (3)
    " s.id, "			// (4)
    " s.name, "			// (5)
    " s.type_id, "		// (6)
    " st.name, "		// (7)
    " s.holds, "		// (8)
    " s.fighters, "		// (9)
    " s.sector, "		// (10)
    " sec.name, "		// (11)
    " own.player_id AS owner_id, "	// (12)
    " COALESCE( (SELECT name FROM players WHERE id=own.player_id), 'derelict') AS owner_name, "	// (13)
    " s.ore, s.organics, s.equipment, s.colonists "	// (14, 15, 16, 17)
    "FROM players p " "LEFT JOIN ships s      ON s.id = p.ship " "LEFT JOIN shiptypes st ON st.id = s.type_id " "LEFT JOIN sectors sec  ON sec.id = s.sector "	// Join on ship's sector
    "LEFT JOIN ship_ownership own ON s.id = own.ship_id AND own.role_id = 1 "	// Join for owner
    "WHERE p.id = ?";

  int rc = sqlite3_prepare_v2 (dbh, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      ret_code = rc;
      goto cleanup;
    }

  sqlite3_bind_int (st, 1, player_id);

  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      // --- Create the nested objects ---
      root_obj = json_object ();
      json_t *player_obj = json_object ();
      json_t *ship_obj = json_object ();

      if (!root_obj || !player_obj || !ship_obj)
	{
	  // Out of memory
	  json_decref (root_obj);
	  json_decref (player_obj);
	  json_decref (ship_obj);
	  ret_code = SQLITE_NOMEM;
	  goto cleanup;
	}

      json_object_set_new (root_obj, "player", player_obj);
      json_object_set_new (root_obj, "ship", ship_obj);

      // --- Populate "player" object ---
      int p_id = sqlite3_column_int (st, 0);
      int p_number = sqlite3_column_int (st, 1);
      const char *p_name = (const char *) sqlite3_column_text (st, 2);
      //int p_ship = sqlite3_column_int (st, 3);	// Player's ship ID
      json_object_set_new (player_obj, "id", json_integer (p_id));
      json_object_set_new (player_obj, "number", json_integer (p_number));
      json_object_set_new (player_obj, "name", json_string (p_name));

      // --- Populate "ship" object (with location) ---
      int s_id = sqlite3_column_int (st, 4);	// s.id
      int s_number = sqlite3_column_int (st, 4);	// s.id again, for ship_number
      const char *s_name = (const char *) sqlite3_column_text (st, 5);	// s.name
      json_object_set_new (ship_obj, "id", json_integer (s_id));
      json_object_set_new (ship_obj, "number", json_integer (s_number));
      json_object_set_new (ship_obj, "name", json_string (s_name));

      json_t *ship_type_obj = json_object ();
      int st_id = sqlite3_column_int (st, 7);
      const char *st_name = (const char *) sqlite3_column_text (st, 8);
      json_object_set_new (ship_type_obj, "id", json_integer (st_id));
      json_object_set_new (ship_type_obj, "name", json_string (st_name));
      json_object_set_new (ship_obj, "type", ship_type_obj);

      int s_holds = sqlite3_column_int (st, 8);
      int s_fighters = sqlite3_column_int (st, 9);
      json_object_set_new (ship_obj, "holds", json_integer (s_holds));
      json_object_set_new (ship_obj, "fighters", json_integer (s_fighters));

      json_t *location_obj = json_object ();
      int loc_sector_id = sqlite3_column_int (st, 10);
      const char *loc_sector_name =
	(const char *) sqlite3_column_text (st, 11);
      json_object_set_new (location_obj, "sector_id",
			   json_integer (loc_sector_id));
      json_object_set_new (location_obj, "sector_name",
			   json_string (loc_sector_name));
      json_object_set_new (ship_obj, "location", location_obj);

      // Add owner information to ship_obj
      int owner_id = sqlite3_column_int (st, 12);
      const char *owner_name = (const char *) sqlite3_column_text (st, 13);
      json_t *owner_obj = json_object ();
      json_object_set_new (owner_obj, "id", json_integer (owner_id));
      json_object_set_new (owner_obj, "name", json_string (owner_name));
      json_object_set_new (ship_obj, "owner", owner_obj);

      // Add cargo information to ship_obj
      int ore = sqlite3_column_int (st, 14);
      int organics = sqlite3_column_int (st, 15);
      int equipment = sqlite3_column_int (st, 16);
      int colonists = sqlite3_column_int (st, 17);
      json_t *cargo_obj = json_pack ("{s:i s:i s:i s:i}",
				     "ore", ore,
				     "organics", organics,
				     "equipment", equipment,
				     "colonists", colonists);
      json_object_set_new (ship_obj, "cargo", cargo_obj);

      ret_code = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      // No player found, return empty object
      ret_code = SQLITE_OK;
      root_obj = json_object ();
    }
  else
    {
      ret_code = rc;
    }

cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }

  if (ret_code == SQLITE_OK)
    {
      if (out)
	*out = root_obj;
      else
	json_decref (root_obj);
    }
  else
    {
      if (root_obj)
	json_decref (root_obj);
    }

  pthread_mutex_unlock (&db_mutex);

  return ret_code;
}

// Mapping from client-friendly field names to player_info_v1 view column names
static const struct {
    const char *client_name;
    const char *view_name;
} player_field_map[] = {
    {"id", "player_id"},
    {"username", "player_name"},
    {"number", "player_number"},
    {"sector", "sector_id"},
    {"sector_name", "sector_name"},
    {"credits", "petty_cash"}, // Client requested "credits" maps to "petty_cash" in view
    {"alignment", "alignment"},
    {"experience", "experience"},
    {"ship_number", "ship_number"},
    {"ship_id", "ship_id"},
    {"ship_name", "ship_name"},
    {"ship_type_id", "ship_type_id"},
    {"ship_type_name", "ship_type_name"},
    {"ship_holds_capacity", "ship_holds_capacity"},
    {"ship_holds_current", "ship_holds_current"},
    {"ship_fighters", "ship_fighters"},
    {"ship_mines", "ship_mines"},
    {"ship_limpets", "ship_limpets"},
    {"ship_genesis", "ship_genesis"},
    {"ship_photons", "ship_photons"},
    {"ship_beacons", "ship_beacons"},
    {"ship_colonists", "ship_colonists"},
    {"ship_equipment", "ship_equipment"},
    {"ship_organics", "ship_organics"},
    {"ship_ore", "ship_ore"},
    {"ship_ported", "ship_ported"},
    {"ship_onplanet", "ship_onplanet"},
    {"approx_worth", "approx_worth"},
    {NULL, NULL} // Sentinel
};

// Function to get the corresponding view column name
static const char* get_player_view_column_name(const char *client_name) {
    for (int i = 0; player_field_map[i].client_name != NULL; i++) {
        if (strcmp(client_name, player_field_map[i].client_name) == 0) {
            return player_field_map[i].view_name;
        }
    }
    return NULL; // Not found
}

int db_player_info_selected_fields(int player_id, const json_t *fields_array, json_t **out) {
    sqlite3 *db = db_get_handle();
    sqlite3_stmt *stmt = NULL;
    int rc = SQLITE_ERROR;
    *out = NULL;

    if (!db || !fields_array || !json_is_array(fields_array)) {
        LOGE("db_player_info_selected_fields: Invalid arguments provided.\n");
        return SQLITE_MISUSE;
    }

    json_t *selected_view_columns_json = json_array();
    if (!selected_view_columns_json) {
        LOGE("db_player_info_selected_fields: Failed to allocate JSON array for selected columns.\n");
        return SQLITE_NOMEM;
    }

    size_t index;
    json_t *value;

    // Build the SELECT clause dynamically
    json_array_foreach(fields_array, index, value) {
        const char *client_field_name = json_string_value(value);
        if (client_field_name) {
            const char *view_col_name = get_player_view_column_name(client_field_name);
            if (view_col_name) {
                // Append the view's column name, not the client's.
                // We'll map back to client name in the result parsing.
                json_array_append_new(selected_view_columns_json, json_string(view_col_name));
            } else {
                LOGW("db_player_info_selected_fields: Requested field '%s' not mapped to a view column. Ignoring.\n", client_field_name);
            }
        }
    }

    if (json_array_size(selected_view_columns_json) == 0) {
        LOGW("db_player_info_selected_fields: No valid fields to select. Returning empty object.\n");
        *out = json_object(); // Return an empty object if no fields were valid.
        json_decref(selected_view_columns_json);
        return SQLITE_OK;
    }

    // Allocate buffer for the dynamic SELECT clause (e.g., "col1, col2, col3")
    // Max 256 chars per column name, plus ", " separator. Max ~30 fields from player_field_map.
    // 30 * (256 + 2) is ~7.5KB. So 8KB is a safe buffer size.
    char select_clause_buffer[8192]; 
    select_clause_buffer[0] = '\0';
    size_t current_buffer_len = 0;

    json_array_foreach(selected_view_columns_json, index, value) {
        const char *col_name_in_view = json_string_value(value);
        if (col_name_in_view) {
            if (current_buffer_len > 0) {
                strncat(select_clause_buffer, ", ", sizeof(select_clause_buffer) - current_buffer_len - 1);
                current_buffer_len += 2;
            }
            strncat(select_clause_buffer, col_name_in_view, sizeof(select_clause_buffer) - current_buffer_len - 1);
            current_buffer_len += strlen(col_name_in_view);
        }
    }
    json_decref(selected_view_columns_json); // Free the temporary array


    // Construct the full SQL query
    char *sql_query = sqlite3_mprintf(
        "SELECT %s FROM player_info_v1 WHERE player_id = ?1;",
        select_clause_buffer
    );

    if (!sql_query) {
        LOGE("db_player_info_selected_fields: SQL query string allocation failed.\n");
        return SQLITE_NOMEM;
    }

    pthread_mutex_lock(&db_mutex);

    rc = sqlite3_prepare_v2(db, sql_query, -1, &stmt, NULL);
    sqlite3_free(sql_query); // Free sqlite3_mprintf'd string immediately

    if (rc != SQLITE_OK) {
        LOGE("db_player_info_selected_fields: Failed to prepare statement for player %d: %s\n", player_id, sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return rc;
    }

    sqlite3_bind_int(stmt, 1, player_id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        *out = json_object();
        if (!*out) {
            LOGE("db_player_info_selected_fields: Failed to allocate output JSON object.\n");
            sqlite3_finalize(stmt);
            pthread_mutex_unlock(&db_mutex);
            return SQLITE_NOMEM;
        }

        // Iterate through the original requested fields_array to populate the output JSON
        // This ensures the keys in the output match the client's request
        json_array_foreach(fields_array, index, value) {
            const char *client_field_name = json_string_value(value);
            if (!client_field_name) continue;

            const char *view_col_name = get_player_view_column_name(client_field_name);
            if (!view_col_name) {
                // This case should ideally be covered by the initial validation/logging
                continue;
            }
            
            // Find the column index in the result set by the view's column name
            int col_idx = sqlite3_column_type(stmt, 0) == SQLITE_NULL ? -1 : sqlite3_column_count(stmt); // Initialize to invalid
            for (int c = 0; c < sqlite3_column_count(stmt); ++c) {
                if (strcmp(sqlite3_column_name(stmt, c), view_col_name) == 0) {
                    col_idx = c;
                    break;
                }
            }

            if (col_idx != -1) {
                json_t *col_value = NULL;
                switch (sqlite3_column_type(stmt, col_idx)) {
                    case SQLITE_INTEGER:
                        col_value = json_integer(sqlite3_column_int64(stmt, col_idx));
                        break;
                    case SQLITE_FLOAT:
                        col_value = json_real(sqlite3_column_double(stmt, col_idx));
                        break;
                    case SQLITE_TEXT:
                        col_value = json_string((const char*)sqlite3_column_text(stmt, col_idx));
                        break;
                    case SQLITE_NULL:
                        col_value = json_null();
                        break;
                    default:
                        LOGW("db_player_info_selected_fields: Unsupported SQLITE type for column '%s'.\n", view_col_name);
                        break;
                }
                if (col_value) {
                    json_object_set_new(*out, client_field_name, col_value); // Use client_field_name for output key
                }
            } else {
                LOGW("db_player_info_selected_fields: Internal error: Column '%s' (for client field '%s') not found in query result for player %d.\n", view_col_name, client_field_name, player_id);
            }
        }
        rc = SQLITE_OK;
    } else if (rc == SQLITE_DONE) {
        LOGI("db_player_info_selected_fields: Player ID %d not found.\n", player_id);
        rc = SQLITE_NOTFOUND;
    } else {
        LOGE("db_player_info_selected_fields: Query execution failed for player %d: %s\n", player_id, sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);
    return rc;
}



int
db_sector_beacon_text (int sector_id, char **out_text)
{
  // Initialize all pointers to NULL for a clean slate.
  sqlite3_stmt *st = NULL;

  // The final return code for the function.
  int ret_code = SQLITE_ERROR;

  // 1. Acquire the lock at the beginning of the function.
  pthread_mutex_lock (&db_mutex);

  // Initialize the output pointer.
  if (out_text)
    *out_text = NULL;

  // Get the database handle.
  sqlite3 *dbh = db_get_handle ();
  if (!dbh)
    {
      ret_code = SQLITE_ERROR;
      goto cleanup;
    }

  const char *sql = "SELECT beacon FROM sectors WHERE id=?";

  // 2. Prepare the statement. Check for errors and jump to cleanup if needed.
  int rc = sqlite3_prepare_v2 (dbh, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      ret_code = rc;
      goto cleanup;
    }

  // 3. Bind the parameter.
  sqlite3_bind_int (st, 1, sector_id);

  // 4. Get results. A successful step will return SQLITE_ROW.
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      const unsigned char *txt = sqlite3_column_text (st, 0);
      if (txt && *txt)
	{
	  const char *c = (const char *) txt;
	  if (out_text)
	    {
	      // The caller is now responsible for freeing this memory.
	      *out_text = strdup (c);
	    }
	}
      ret_code = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      // No rows were returned, which is a successful query.
      ret_code = SQLITE_OK;
    }
  else
    {
      // An error occurred during sqlite3_step.
      ret_code = rc;
    }

cleanup:
  // 5. Always finalize the SQLite statement if it was created.
  if (st)
    {
      sqlite3_finalize (st);
    }

  // 6. Always release the lock at the very end.
  pthread_mutex_unlock (&db_mutex);

  // 7. A single return statement, as per the pattern.
  return ret_code;
}


int
db_ships_at_sector_json (int player_id, int sector_id, json_t **out)
{
  (void) player_id;
  sqlite3_stmt *st = NULL;
  int ret_code = SQLITE_ERROR;	/* default to error until succeeded */
  if (out)
    *out = NULL;

  /* 1) Lock DB */
  pthread_mutex_lock (&db_mutex);

  /* 2) Result array */
  json_t *ships = json_array ();
  if (!ships)
    {
      ret_code = SQLITE_NOMEM;
      goto cleanup;
    }

  /* 3) Query: ship name, type name, owner name, ship id (by sector) */
  const char *sql = "SELECT s.name, st.name, p.name, s.id " "FROM ships s " "LEFT JOIN shiptypes st ON s.type_id = st.id " "LEFT JOIN ship_ownership so ON s.id = so.ship_id AND so.role_id = 1 "	/* Assuming role_id 1 is the owner */
    "LEFT JOIN players p ON so.player_id = p.id " "WHERE s.sector=?;";

  int rc = sqlite3_prepare_v2 (db_get_handle (), sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      ret_code = rc;
      goto cleanup;
    }

  sqlite3_bind_int (st, 1, sector_id);

  /* 4) Build JSON rows */
  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      const char *ship_name = (const char *) sqlite3_column_text (st, 0);
      const char *ship_type_name = (const char *) sqlite3_column_text (st, 1);
      const char *owner_name = (const char *) sqlite3_column_text (st, 2);
      int ship_id = sqlite3_column_int (st, 3);

      json_t *ship = json_object ();
      if (!ship)
	{
	  ret_code = SQLITE_NOMEM;
	  goto cleanup;
	}

      /* Optional generic keys for client consistency (match ports): */
      json_object_set_new (ship, "id", json_integer (ship_id));
      json_object_set_new (ship, "name",
			   json_string (ship_name ? ship_name : ""));
      json_object_set_new (ship, "type",
			   json_string (ship_type_name ? ship_type_name :
					""));

      /* Legacy / verbose keys you were already using: */
      json_object_set_new (ship, "ship_name",
			   json_string (ship_name ? ship_name : ""));
      json_object_set_new (ship, "ship_type",
			   json_string (ship_type_name ? ship_type_name :
					""));

      /* Owner: default to "derelict" when NULL/empty */
      if (owner_name && *owner_name)
	{
	  json_object_set_new (ship, "owner", json_string (owner_name));
	}
      else
	{
	  json_object_set_new (ship, "owner", json_string ("derelict"));
	}

      json_array_append_new (ships, ship);
    }

  /* 5) Success path */
  ret_code = SQLITE_OK;
  if (out)
    {
      *out = ships;		/* transfer ownership */
      ships = NULL;		/* prevent cleanup from freeing it */
    }

cleanup:
  if (st)
    sqlite3_finalize (st);

  /* Only free if we did NOT transfer ownership */
  if (ships)
    json_decref (ships);

  pthread_mutex_unlock (&db_mutex);
  return ret_code;
}



int
db_ports_at_sector_json (int sector_id, json_t **out_array)
{
  // Initialize all pointers to NULL for a clean slate.
  *out_array = NULL;
  json_t *ports = NULL;
  sqlite3_stmt *st = NULL;

  int rc;			// For SQLite return codes.
  int ret_code = -1;		// The final return code for the function.

  // 1. Acquire the lock at the beginning of the function.
  pthread_mutex_lock (&db_mutex);

  // Allocate the JSON array. If this fails, we jump to cleanup.
  ports = json_array ();
  if (!ports)
    {
      ret_code = SQLITE_NOMEM;
      goto cleanup;
    }

  const char *sql = "SELECT id, name, type FROM ports WHERE sector=?;";

  // 2. Prepare the statement. Check for errors and jump to cleanup if needed.
  rc = sqlite3_prepare_v2 (db_get_handle (), sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      ret_code = rc;
      goto cleanup;
    }

  sqlite3_bind_int (st, 1, sector_id);

  // 3. Loop through the results. If an error occurs (e.g., failed json_object() allocation),
  // we jump to cleanup to free all resources and the mutex.
  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      int port_id = sqlite3_column_int (st, 0);
      const char *port_name = (const char *) sqlite3_column_text (st, 1);
      const char *port_type = (const char *) sqlite3_column_text (st, 2);

      json_t *port = json_object ();
      if (!port)
	{
	  ret_code = SQLITE_NOMEM;
	  goto cleanup;
	}

      json_object_set_new (port, "id", json_integer (port_id));
      json_object_set_new (port, "name", json_string (port_name));
      json_object_set_new (port, "type", json_string (port_type));

      json_array_append_new (ports, port);
    }

  // 4. If the loop finished, set the final return code to success.
  ret_code = SQLITE_OK;
  *out_array = ports;
  ports = NULL;			// We've transferred ownership, so set to NULL to prevent freeing in cleanup.

cleanup:
  // 5. Always finalize the SQLite statement if it was created.
  if (st)
    {
      sqlite3_finalize (st);
    }

  // 6. Always clean up the `ports` array if it was allocated but not returned.
  if (ports)
    {
      json_decref (ports);
    }

  // 7. Always release the lock at the very end.
  pthread_mutex_unlock (&db_mutex);

  return ret_code;
}

// In database.c
int
db_sector_has_beacon (int sector_id)
{
  sqlite3_stmt *stmt;
  // 1. Acquire the lock before any database interaction.
  pthread_mutex_lock (&db_mutex);
  // 2. Prepare the statement. This is the first place an error could occur.
  const char *sql = "SELECT beacon FROM sectors WHERE id = ?;";


  if (sqlite3_prepare_v2 (db_get_handle (), sql, -1, &stmt, NULL) !=
      SQLITE_OK)
    {
      /* fprintf (stderr, "SQL error in db_sector_has_beacon: %s\n", */
      /*               sqlite3_errmsg (db_get_handle ())); */
      /* return -1;             // Indicates an error */
      // If preparation fails, we jump to the cleanup block to release the lock.
      goto cleanup;
    }

  // 3. Bind the parameter.
  sqlite3_bind_int (stmt, 1, sector_id);

  // 4. Execute the statement.
  int rc = sqlite3_step (stmt);
  int has_beacon = 0;
  if (rc == SQLITE_ROW)
    {
      if (sqlite3_column_text (stmt, 0) != NULL)
	{
	  has_beacon = 1;
	}
    }
  // The cleanup label is the single point of exit.
cleanup:
  // 5. Finalize the statement if it was successfully prepared.
  if (stmt)
    {
      sqlite3_finalize (stmt);
    }

  // 6. Release the lock at the end of the function, regardless of success or failure.
  pthread_mutex_unlock (&db_mutex);
  return has_beacon;
}



int
db_sector_set_beacon (int sector_id, const char *beacon_text, int player_id)
{
  sqlite3 *dbh = db_get_handle ();
  sqlite3_stmt *st_sel = NULL, *st_upd = NULL;
  sqlite3_stmt *st_asset = NULL;
  int rc = SQLITE_ERROR, had_beacon = 0;

  pthread_mutex_lock (&db_mutex);

  // 1. SELECT: Check for existing beacon
  const char *sql_sel = "SELECT beacon FROM sectors WHERE id=?1;";
  rc = sqlite3_prepare_v2 (dbh, sql_sel, -1, &st_sel, NULL);
  if (rc != SQLITE_OK)
    goto cleanup;

  sqlite3_bind_int (st_sel, 1, sector_id);
  rc = sqlite3_step (st_sel);
  if (rc == SQLITE_ROW)
    {
      const unsigned char *txt = sqlite3_column_text (st_sel, 0);
      had_beacon = (txt && txt[0]) ? 1 : 0;
    }
  else if (rc != SQLITE_DONE)
    {
      goto cleanup;
    }
  sqlite3_finalize (st_sel);
  st_sel = NULL;

  // 2. UPDATE: Update the sectors table (beacon text)
  const char *sql_upd = "UPDATE sectors SET beacon=?1 WHERE id=?2;";
  rc = sqlite3_prepare_v2 (dbh, sql_upd, -1, &st_upd, NULL);
  if (rc != SQLITE_OK)
    goto cleanup;

  if (had_beacon && (beacon_text == NULL || *beacon_text == '\0'))
    {
      // Explode/Clear: Set sectors.beacon to NULL
      sqlite3_bind_null (st_upd, 1);
    }
  else
    {
      if (beacon_text && *beacon_text)
	sqlite3_bind_text (st_upd, 1, beacon_text, -1, SQLITE_TRANSIENT);
      else
	sqlite3_bind_null (st_upd, 1);
    }
  sqlite3_bind_int (st_upd, 2, sector_id);

  rc = sqlite3_step (st_upd);
  if (rc == SQLITE_DONE)
    rc = SQLITE_OK;

  sqlite3_finalize (st_upd);
  st_upd = NULL;

  if (rc != SQLITE_OK)
    goto cleanup;

  // 3. Asset Tracking: Update the sector_assets table (ownership)

  if (had_beacon && (beacon_text == NULL || *beacon_text == '\0'))
    {
      // Case A: Beacon removed (Exploded/Cancelled) - DELETE asset ownership
      const char *sql_del_asset =
	"DELETE FROM sector_assets WHERE sector=?1 AND asset_type='3';";
      rc = sqlite3_exec (dbh, sql_del_asset, NULL, NULL, NULL);
      if (rc != SQLITE_OK)
	LOGE
	  ("db_sector_set_beacon: DELETE asset failed for sector %d, rc=%d",
	   sector_id, rc);
    }
  else if (beacon_text && *beacon_text)
    {
      // Case B: Beacon set or updated - INSERT OR REPLACE asset ownership
      const char *sql_ins_asset = "INSERT OR REPLACE INTO sector_assets (sector, asset_type, player, quantity, deployed_at) " "VALUES (?1, ?2, ?3, 1, ?4);";	// *** ADDED: quantity and deployed_at ***

      rc = sqlite3_prepare_v2 (dbh, sql_ins_asset, -1, &st_asset, NULL);
      if (rc != SQLITE_OK)
	{
	  LOGE
	    ("db_sector_set_beacon: INSERT asset PREPARE failed for sector %d, rc=%d. Msg: %s",
	     sector_id, rc, sqlite3_errmsg (dbh));
	  goto cleanup;
	}

      // Get current timestamp here if not available globally
      int64_t now_s = (int64_t) time (NULL);

      sqlite3_bind_int (st_asset, 1, sector_id);
      sqlite3_bind_text (st_asset, 2, "3", -1, SQLITE_STATIC);	// Use "3" as you were using
      sqlite3_bind_int (st_asset, 3, player_id);	// BIND the player_id
      sqlite3_bind_int64 (st_asset, 4, now_s);	// *** NEW: BIND deployed_at ***

      rc = sqlite3_step (st_asset);
      if (rc == SQLITE_DONE)
	rc = SQLITE_OK;

      sqlite3_finalize (st_asset);
      st_asset = NULL;
    }

cleanup:
  if (st_sel)
    sqlite3_finalize (st_sel);
  if (st_upd)
    sqlite3_finalize (st_upd);
  if (st_asset)
    sqlite3_finalize (st_asset);

  pthread_mutex_unlock (&db_mutex);
  return rc;
}




// In database.c
int
db_player_has_beacon_on_ship (int player_id)
{
  sqlite3_stmt *stmt;
  // 1. Acquire the lock before any database interaction.
  pthread_mutex_lock (&db_mutex);

  const char *sql =
    "SELECT T2.beacons FROM players AS T1 JOIN ships AS T2 ON T1.ship = T2.id WHERE T1.id = ?;";
  // 2. Prepare the statement. This is the first place an error could occur.
  int rc = sqlite3_prepare_v2 (db_get_handle (), sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      // If preparation fails, we jump to the cleanup block to release the lock.
      goto cleanup;
    }

  // 3. Bind the parameter.
  sqlite3_bind_int (stmt, 1, player_id);

  // 4. Execute the statement.
  int has_beacon = 0;
  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      if (sqlite3_column_int (stmt, 0) > 0)
	{
	  has_beacon = 1;
	}
    }
  // The cleanup label is the single point of exit.
cleanup:
  // 5. Finalize the statement if it was successfully prepared.
  if (stmt)
    {
      sqlite3_finalize (stmt);
    }

  // 6. Release the lock at the end of the function, regardless of success or failure.
  pthread_mutex_unlock (&db_mutex);
  // sqlite3_finalize(stmt);
  return has_beacon;
}


// This is the correct, thread-safe way to implement the function.
int
db_player_decrement_beacon_count (int player_id)
{
  sqlite3_stmt *stmt = NULL;
  int rc = -1;			// Initialize return code to a non-OK value.

  // 1. Acquire the lock before any database interaction.
  pthread_mutex_lock (&db_mutex);

  const char *sql =
    "UPDATE ships SET beacons = beacons - 1 WHERE id = (SELECT ship FROM players WHERE id = ?);";

  // 2. Prepare the statement. This is the first place an error could occur.
  rc = sqlite3_prepare_v2 (db_get_handle (), sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      // If preparation fails, we jump to the cleanup block to release the lock.
      goto cleanup;
    }

  // 3. Bind the parameter.
  sqlite3_bind_int (stmt, 1, player_id);

  // 4. Execute the statement.
  rc = sqlite3_step (stmt);

  // The cleanup label is the single point of exit.
cleanup:
  // 5. Finalize the statement if it was successfully prepared.
  if (stmt)
    {
      sqlite3_finalize (stmt);
    }

  // 6. Release the lock at the end of the function, regardless of success or failure.
  pthread_mutex_unlock (&db_mutex);

  return rc;
}


// Post-bigbang fix: chain trap sectors and bridge to the main graph.
// Returns SQLITE_OK on success (or no work to do), otherwise an SQLite error code.
int
db_chain_traps_and_bridge (int fedspace_max /* typically 10 */ )
{
  sqlite3 *dbh = db_get_handle ();
  if (!dbh)
    return SQLITE_ERROR;

  pthread_mutex_lock (&db_mutex);

  int rc = SQLITE_ERROR;
  sqlite3_stmt *st_traps = NULL;
  sqlite3_stmt *st_ins = NULL;
  sqlite3_stmt *st_anchor = NULL;

  // We do everything atomically.
  if (sqlite3_exec (dbh, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK)
    goto cleanup;

  // 1) Collect trap sector ids (id > fedspace_max), i.e., sectors with 0 in and 0 out.
  //    NB: Uses the sector_warps indexes you already create. :contentReference[oaicite:0]{index=0}
  const char *sql_traps =
    "WITH ow AS (SELECT from_sector AS id, COUNT(*) AS c FROM sector_warps GROUP BY from_sector), "
    "     iw AS (SELECT to_sector   AS id, COUNT(*) AS c FROM sector_warps GROUP BY to_sector) "
    "SELECT s.id "
    "FROM sectors s "
    "LEFT JOIN ow ON ow.id = s.id "
    "LEFT JOIN iw ON iw.id = s.id "
    "WHERE s.id > ?1 AND COALESCE(ow.c,0)=0 AND COALESCE(iw.c,0)=0 "
    "ORDER BY s.id;";

  rc = sqlite3_prepare_v2 (dbh, sql_traps, -1, &st_traps, NULL);
  if (rc != SQLITE_OK)
    goto cleanup;

  sqlite3_bind_int (st_traps, 1, fedspace_max);

  // We’ll store trap ids in memory first.
  int cap = 64, n = 0;
  int *traps = (int *) malloc (sizeof (int) * cap);
  if (!traps)
    {
      rc = SQLITE_NOMEM;
      goto cleanup;
    }

  while ((rc = sqlite3_step (st_traps)) == SQLITE_ROW)
    {
      if (n == cap)
	{
	  cap *= 2;
	  int *tmp = (int *) realloc (traps, sizeof (int) * cap);
	  if (!tmp)
	    {
	      free (traps);
	      rc = SQLITE_NOMEM;
	      goto cleanup;
	    }
	  traps = tmp;
	}
      traps[n++] = sqlite3_column_int (st_traps, 0);
    }
  if (rc != SQLITE_DONE)
    {
      free (traps);
      goto cleanup;
    }

  // Nothing to do? Just commit and leave OK.
  if (n == 0)
    {
      sqlite3_exec (dbh, "COMMIT;", NULL, NULL, NULL);
      rc = SQLITE_OK;
      goto cleanup_unlock_only;
    }

  // 2) Chain them bidirectionally: (a<->b), (b<->c), ...
  const char *sql_ins =
    "INSERT OR IGNORE INTO sector_warps(from_sector, to_sector) VALUES (?1, ?2);";
  rc = sqlite3_prepare_v2 (dbh, sql_ins, -1, &st_ins, NULL);
  if (rc != SQLITE_OK)
    {
      free (traps);
      goto cleanup;
    }

  for (int i = 0; i + 1 < n; ++i)
    {
      // forward
      sqlite3_bind_int (st_ins, 1, traps[i]);
      sqlite3_bind_int (st_ins, 2, traps[i + 1]);
      if ((rc = sqlite3_step (st_ins)) != SQLITE_DONE)
	{
	  free (traps);
	  goto cleanup;
	}
      sqlite3_reset (st_ins);

      // reverse
      sqlite3_bind_int (st_ins, 1, traps[i + 1]);
      sqlite3_bind_int (st_ins, 2, traps[i]);
      if ((rc = sqlite3_step (st_ins)) != SQLITE_DONE)
	{
	  free (traps);
	  goto cleanup;
	}
      sqlite3_reset (st_ins);
    }

  // 3) Pick a random “anchor” sector that looks like it’s in the main graph:
  //    any non-FedSpace sector that already participates in at least one warp
  //    (incoming OR outgoing). Then bridge anchor <-> first_trap.
  const char *sql_anchor =
    "WITH x AS ("
    "  SELECT s.id "
    "  FROM sectors s "
    "  WHERE s.id > ?1 AND EXISTS ("
    "    SELECT 1 FROM sector_warps w "
    "    WHERE w.from_sector = s.id OR w.to_sector = s.id"
    "  )" ") SELECT id FROM x ORDER BY RANDOM() LIMIT 1;";

  rc = sqlite3_prepare_v2 (dbh, sql_anchor, -1, &st_anchor, NULL);
  if (rc != SQLITE_OK)
    {
      free (traps);
      goto cleanup;
    }
  sqlite3_bind_int (st_anchor, 1, fedspace_max);

  int anchor = 0;
  rc = sqlite3_step (st_anchor);
  if (rc == SQLITE_ROW)
    {
      anchor = sqlite3_column_int (st_anchor, 0);
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      anchor = 0;
      rc = SQLITE_OK;
    }				// no candidate
  else
    {
      free (traps);
      goto cleanup;
    }

  // If we didn’t find any anchor (e.g., no other warps were built yet),
  // fall back to FedSpace 1 as the anchor (if allowed by your rules).
  if (anchor == 0)
    anchor = fedspace_max + 1;	// very small fallback; adjust if needed.

  // Bridge: anchor <-> traps[0]
  sqlite3_bind_int (st_ins, 1, anchor);
  sqlite3_bind_int (st_ins, 2, traps[0]);
  if ((rc = sqlite3_step (st_ins)) != SQLITE_DONE)
    {
      free (traps);
      goto cleanup;
    }
  sqlite3_reset (st_ins);

  sqlite3_bind_int (st_ins, 1, traps[0]);
  sqlite3_bind_int (st_ins, 2, anchor);
  if ((rc = sqlite3_step (st_ins)) != SQLITE_DONE)
    {
      free (traps);
      goto cleanup;
    }
  sqlite3_reset (st_ins);
  int ferringhi = traps[0];
  free (traps);

  // All good.
  sqlite3_exec (dbh, "COMMIT;", NULL, NULL, NULL);
  rc = SQLITE_OK;
  goto cleanup_unlock_only;

cleanup:
  // If we started a txn, roll it back.
  sqlite3_exec (dbh, "ROLLBACK;", NULL, NULL, NULL);

cleanup_unlock_only:
  if (st_traps)
    sqlite3_finalize (st_traps);
  if (st_ins)
    sqlite3_finalize (st_ins);
  if (st_anchor)
    sqlite3_finalize (st_anchor);
  pthread_mutex_unlock (&db_mutex);
  // return rc;
  return ferringhi;
}


int
db_ships_inspectable_at_sector_json (int player_id, int sector_id,
				     json_t **out_array)
{
  if (!out_array)
    return SQLITE_MISUSE;
  *out_array = NULL;

  static const char *SQL =
    "SELECT "
    "  s.id AS ship_id, "
    "  COALESCE(NULLIF(s.name,''), st.name || ' #' || s.id) AS ship_name, "
    "  st.id AS type_id, "
    "  st.name AS type_name, " "  s.sector AS sector_id, "
    /* owner from ship_ownership (NOT pilot) */
    "  own.player_id AS owner_id, "
    "  COALESCE( (SELECT name FROM players WHERE id = own.player_id), 'derelict') AS owner_name, "
    /* derelict/boardable == unpiloted, regardless of owner */
    "  CASE WHEN pil.id IS NULL THEN 1 ELSE 0 END AS is_derelict, " "  s.fighters, s.shields, " "  s.holds AS holds_total, (s.holds - s.holds) AS holds_free, " "  s.ore, s.organics, s.equipment, s.colonists, " "  COALESCE(s.flags,0) AS flags, s.id AS registration, COALESCE(s.perms, 731) AS perms " "FROM ships s " "LEFT JOIN shiptypes      st  ON st.id = s.type " "LEFT JOIN ship_ownership own ON own.ship_id = s.id " "LEFT JOIN players pil ON pil.ship = s.id "	/* current pilot (if any) */
    "WHERE s.sector = ? "
    "  AND (pil.id IS NULL OR pil.id != ?) "
    "ORDER BY is_derelict DESC, s.id ASC;";

  int rc = SQLITE_OK;
  sqlite3_stmt *stmt = NULL;
  json_t *arr = NULL;

  pthread_mutex_lock (&db_mutex);

  rc = sqlite3_prepare_v2 (db_handle, SQL, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    goto fail_locked;

  sqlite3_bind_int (stmt, 1, sector_id);
  sqlite3_bind_int (stmt, 2, player_id);

  arr = json_array ();
  if (!arr)
    {
      rc = SQLITE_NOMEM;
      goto fail_locked;
    }

  while ((rc = sqlite3_step (stmt)) == SQLITE_ROW)
    {
      int c = 0;
      int ship_id = sqlite3_column_int (stmt, c++);
      const char *ship_nm = (const char *) sqlite3_column_text (stmt, c++);
      if (!ship_nm)
	ship_nm = "";
      int type_id = sqlite3_column_int (stmt, c++);
      const char *type_nm = (const char *) sqlite3_column_text (stmt, c++);
      if (!type_nm)
	type_nm = "";
      int sector_row = sqlite3_column_int (stmt, c++);
      int owner_id =
	(sqlite3_column_type (stmt, c) ==
	 SQLITE_NULL) ? 0 : sqlite3_column_int (stmt, c);
      c++;
      const char *owner_nm = (const char *) sqlite3_column_text (stmt, c++);
      if (!owner_nm)
	owner_nm = "derelict";
      int is_derelict = sqlite3_column_int (stmt, c++);
      int fighters = sqlite3_column_int (stmt, c++);
      int shields = sqlite3_column_int (stmt, c++);
      int holds_total = sqlite3_column_int (stmt, c++);
      int holds_free = sqlite3_column_int (stmt, c++);
      int ore = sqlite3_column_int (stmt, c++);
      int organics = sqlite3_column_int (stmt, c++);
      int equipment = sqlite3_column_int (stmt, c++);
      int colonists = sqlite3_column_int (stmt, c++);
      int flags = sqlite3_column_int (stmt, c++);
      const char *reg = (const char *) sqlite3_column_text (stmt, c++);
      if (!reg)
	reg = "";
      int perms = sqlite3_column_int (stmt, c++);
      char perm_str[8];
      snprintf (perm_str, sizeof (perm_str), "%03d", perms);

      json_t *row = json_pack ("{s:i s:s s:o s:i s:o s:o s:o s:o s:o s:s}",
			       "id", ship_id,
			       "name", ship_nm,
			       "type", json_pack ("{s:i s:s}", "id", type_id,
						  "name", type_nm),
			       "sector_id", sector_row,
			       "owner", json_pack ("{s:i s:s}", "id",
						   owner_id, "name",
						   owner_nm),
			       "flags", json_pack ("{s:b s:b s:i}",
						   "derelict",
						   is_derelict != 0,
						   "boardable",
						   is_derelict != 0, "raw",
						   flags),
			       "defence", json_pack ("{s:i s:i}", "shields",
						     shields, "fighters",
						     fighters),
			       "holds", json_pack ("{s:i s:i}", "total",
						   holds_total, "free",
						   holds_free),
			       "cargo", json_pack ("{s:i s:i s:i s:i}", "ore",
						   ore, "organics", organics,
						   "equipment", equipment,
						   "colonists", colonists),
			       "perms", json_pack ("{s:i s:s}", "value",
						   perms, "octal", perm_str),
			       "registration", reg);

      if (!row)
	{
	  rc = SQLITE_NOMEM;
	  goto fail_locked;
	}
      json_array_append_new (arr, row);
    }

  if (rc != SQLITE_DONE)
    goto fail_locked;

  sqlite3_finalize (stmt);
  pthread_mutex_unlock (&db_mutex);

  *out_array = arr;
  return SQLITE_OK;

fail_locked:
  if (stmt)
    sqlite3_finalize (stmt);
  pthread_mutex_unlock (&db_mutex);
  if (arr)
    json_decref (arr);
  return rc;
}



//////////////////////////////////////////////////////////////////////////////////////////

int
db_ship_flags_set (int ship_id, int mask)
{
  int rc;

  pthread_mutex_lock (&db_mutex);

  rc = sqlite3_exec (db_handle, "BEGIN IMMEDIATE", NULL, NULL, NULL);
  if (rc != SQLITE_OK)
    goto out_unlock;

  sqlite3_stmt *st = NULL;
  rc = sqlite3_prepare_v2 (db_handle,
			   "UPDATE ships SET flags = COALESCE(flags,0) | ? WHERE id=?;",
			   -1, &st, NULL);
  if (rc == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, mask);
      sqlite3_bind_int (st, 2, ship_id);
      rc = (sqlite3_step (st) == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
    }
  sqlite3_finalize (st);

  if (rc == SQLITE_OK)
    sqlite3_exec (db_handle, "COMMIT", NULL, NULL, NULL);
  else
    sqlite3_exec (db_handle, "ROLLBACK", NULL, NULL, NULL);

out_unlock:
  pthread_mutex_unlock (&db_mutex);
  return rc;
}

int
db_ship_flags_clear (int ship_id, int mask)
{
  int rc;

  pthread_mutex_lock (&db_mutex);

  rc = sqlite3_exec (db_handle, "BEGIN IMMEDIATE", NULL, NULL, NULL);
  if (rc != SQLITE_OK)
    goto out_unlock;

  sqlite3_stmt *st = NULL;
  rc = sqlite3_prepare_v2 (db_handle,
			   "UPDATE ships SET flags = COALESCE(flags,0) & ~? WHERE id=?;",
			   -1, &st, NULL);
  if (rc == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, mask);
      sqlite3_bind_int (st, 2, ship_id);
      rc = (sqlite3_step (st) == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
    }
  sqlite3_finalize (st);

  if (rc == SQLITE_OK)
    sqlite3_exec (db_handle, "COMMIT", NULL, NULL, NULL);
  else
    sqlite3_exec (db_handle, "ROLLBACK", NULL, NULL, NULL);

out_unlock:
  pthread_mutex_unlock (&db_mutex);
  return rc;
}


//////////////////////////////////////////////////////


// Unlocked helper for ship claiming. Assumes db_mutex is held and transaction is active (or not needed).


static int


h_ship_claim_unlocked (sqlite3 *db, int player_id, int sector_id, int ship_id, json_t **out_ship)


{


  // const int LOCK_MASK = SHIPF_LOCKED;        /* if you defined flags; else 0 */


  int rc = SQLITE_OK;


  sqlite3_stmt *stmt = NULL;





  LOGD("h_ship_claim_unlocked: checking ship_id=%d, sector_id=%d", ship_id, sector_id);





  static const char *SQL_CHECK = "SELECT s.id FROM ships s " "LEFT JOIN players pil ON pil.ship = s.id "	// Changed from s.number to s.id


    "WHERE s.id=? AND s.sector=? "


    "  AND pil.id IS NULL AND (s.flags % 10) >= 1;";





  rc = sqlite3_prepare_v2 (db, SQL_CHECK, -1, &stmt, NULL);


  if (rc != SQLITE_OK)


    {


      LOGE("h_ship_claim_unlocked: prepare failed: %s", sqlite3_errmsg(db));


      return rc;


    }





  sqlite3_bind_int (stmt, 1, ship_id);


  sqlite3_bind_int (stmt, 2, sector_id);





  rc = sqlite3_step (stmt);


  sqlite3_finalize (stmt);


  stmt = NULL;


  if (rc != SQLITE_ROW)


    {


      LOGE("h_ship_claim_unlocked: Ship check failed. rc=%d. ship_id=%d, sector_id=%d. (Maybe flags or pilot issue?)", rc, ship_id, sector_id);


      return SQLITE_CONSTRAINT;


    }


  


  LOGD("h_ship_claim_unlocked: Ship check passed.");





  /* Optional: strip defence so claims aren’t “free upgrades” */


  static const char *SQL_NORMALISE =


    "UPDATE ships SET fighters=fighters, shields=shields WHERE id=? AND 1=0;";


  // "UPDATE ships SET fighters=0, shields=0 WHERE id=?;";


  rc = sqlite3_prepare_v2 (db, SQL_NORMALISE, -1, &stmt, NULL);


  if (rc != SQLITE_OK)


    return rc;


  sqlite3_bind_int (stmt, 1, ship_id);


  rc = sqlite3_step (stmt);


  sqlite3_finalize (stmt);


  stmt = NULL;


  if (rc != SQLITE_DONE)


    return rc;





  /* Switch current pilot */


  static const char *SQL_SET_PLAYER_SHIP =


    "UPDATE players " "SET ship = ? " "WHERE id = ?;";


  rc = sqlite3_prepare_v2 (db, SQL_SET_PLAYER_SHIP, -1, &stmt, NULL);


  if (rc != SQLITE_OK)


    return rc;


  sqlite3_bind_int (stmt, 1, ship_id);


  sqlite3_bind_int (stmt, 2, player_id);


  rc = sqlite3_step (stmt);


  sqlite3_finalize (stmt);


  stmt = NULL;


  if (rc != SQLITE_DONE)


    return rc;








  /* Grant ownership to the claimer and mark as primary */


  static const char *SQL_CLR_OLD_PRIMARY =


    "UPDATE ship_ownership SET is_primary=0 WHERE player_id=?;";


  rc = sqlite3_prepare_v2 (db, SQL_CLR_OLD_PRIMARY, -1, &stmt, NULL);


  if (rc != SQLITE_OK)


    return rc;


  sqlite3_bind_int (stmt, 1, player_id);


  rc = sqlite3_step (stmt);


  sqlite3_finalize (stmt);


  stmt = NULL;


  if (rc != SQLITE_DONE)


    return rc;





  // Delete existing owner record for this ship (if any)


  static const char *SQL_DELETE_OLD_OWNER =


    "DELETE FROM ship_ownership WHERE ship_id=? AND role_id=1;";


  rc = sqlite3_prepare_v2 (db, SQL_DELETE_OLD_OWNER, -1, &stmt, NULL);


  if (rc != SQLITE_OK)


    return rc;


  sqlite3_bind_int (stmt, 1, ship_id);


  rc = sqlite3_step (stmt);


  sqlite3_finalize (stmt);


  stmt = NULL;


  if (rc != SQLITE_DONE)


    return rc;





  /* Insert new owner record */


  static const char *SQL_INSERT_NEW_OWNER =


    "INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary, acquired_at) "


    "VALUES (?, ?, 1, 1, strftime('%s','now'));";


  rc = sqlite3_prepare_v2 (db, SQL_INSERT_NEW_OWNER, -1, &stmt, NULL);


  if (rc != SQLITE_OK)


    return rc;


  sqlite3_bind_int (stmt, 1, ship_id);


  sqlite3_bind_int (stmt, 2, player_id);


  rc = sqlite3_step (stmt);


  sqlite3_finalize (stmt);


  stmt = NULL;


  if (rc != SQLITE_DONE)


    return rc;








  /* Fetch snapshot for reply (owner from ship_ownership, pilot = you now) */


  static const char *SQL_FETCH =


    "SELECT s.id AS ship_id, "


    "       COALESCE(NULLIF(s.name,''), st.name || ' #' || s.id) AS ship_name, "


    "       st.id AS type_id, st.name AS type_name, "


    "       s.sector AS sector_id, "


    "       own.player_id AS owner_id, "


    "       COALESCE( (SELECT name FROM players WHERE id=own.player_id), 'derelict') AS owner_name, "


    "       0 AS is_derelict, "


    "       s.fighters, s.shields, "


    "       s.holds AS holds_total, (s.holds - s.holds) AS holds_free, "


    "       s.ore, s.organics, s.equipment, s.colonists, "


    "       COALESCE(s.flags, 731) AS perms "


    "FROM ships s "


    "LEFT JOIN shiptypes st      ON st.id = s.type_id "


    "LEFT JOIN ship_ownership own ON own.ship_id = s.id " "WHERE s.id=?;";


  rc = sqlite3_prepare_v2 (db, SQL_FETCH, -1, &stmt, NULL);


  if (rc != SQLITE_OK)


    return rc;


  sqlite3_bind_int (stmt, 1, ship_id);





  rc = sqlite3_step (stmt);


  if (rc != SQLITE_ROW)


    {


      sqlite3_finalize (stmt);


      return rc; // Or some error


    }





  int c = 0;


  int ship_id_row = sqlite3_column_int (stmt, c++);


  const char *ship_nm = (const char *) sqlite3_column_text (stmt, c++);


  if (!ship_nm)


    ship_nm = "";


  int type_id = sqlite3_column_int (stmt, c++);


  const char *type_nm = (const char *) sqlite3_column_text (stmt, c++);


  if (!type_nm)


    type_nm = "";


  int sector_row = sqlite3_column_int (stmt, c++);


  int owner_id =


    (sqlite3_column_type (stmt, c) ==


     SQLITE_NULL) ? 0 : sqlite3_column_int (stmt, c);


  c++;


  const char *owner_nm = (const char *) sqlite3_column_text (stmt, c++);


  if (!owner_nm)


    owner_nm = "derelict";


  int is_derelict = sqlite3_column_int (stmt, c++);


  int fighters = sqlite3_column_int (stmt, c++);


  int shields = sqlite3_column_int (stmt, c++);


  int holds_total = sqlite3_column_int (stmt, c++);


  int holds_free = sqlite3_column_int (stmt, c++);


  int ore = sqlite3_column_int (stmt, c++);


  int organics = sqlite3_column_int (stmt, c++);


  int equipment = sqlite3_column_int (stmt, c++);


  int colonists = sqlite3_column_int (stmt, c++);


  int flags = sqlite3_column_int (stmt, c++);


  const char *reg = (const char *) sqlite3_column_text (stmt, c++);


  if (!reg)


    reg = "";





  /* build the JSON for the claimed ship */


  int perms = sqlite3_column_int (stmt, c++);


  char perm_str[8];


  snprintf (perm_str, sizeof (perm_str), "%03d", perms);





  if (out_ship) {


      *out_ship = json_pack ("{s:i s:s s:o s:i s:o s:o s:o s:o s:o s:s}",


                     "id", ship_id_row,	/* <-- use the value from the SELECT row */


                     "name", ship_nm,


                     "type", json_pack ("{s:i s:s}", "id",


                            type_id, "name", type_nm),


                     "sector_id", sector_row,


                     "owner", json_pack ("{s:i s:s}", "id",


                             owner_id, "name",


                             owner_nm),


                     "flags", json_pack ("{s:b s:b s:i}",


                             "derelict",


                             is_derelict != 0,


                             "boardable",


                             is_derelict != 0,


                             "raw", flags),


                     "defence", json_pack ("{s:i s:i}", "shields",


                               shields, "fighters",


                               fighters),


                     "holds", json_pack ("{s:i s:i}", "total",


                             holds_total, "free",


                             holds_free),


                     "cargo", json_pack ("{s:i s:i s:i s:i}",


                             "ore", ore, "organics",


                             organics, "equipment",


                             equipment, "colonists",


                             colonists),


                     "perms", json_pack ("{s:i s:s}", "value",


                             perms, "octal",


                             perm_str),


                     "registration", reg);


  }





  sqlite3_finalize (stmt);


  return SQLITE_OK;


}





int


db_ship_claim (int player_id, int sector_id, int ship_id, json_t **out_ship)


{


  if (!out_ship)


    return SQLITE_MISUSE;


  *out_ship = NULL;





  sqlite3 *db = db_get_handle();


  int rc;





  pthread_mutex_lock (&db_mutex);


  rc = sqlite3_exec (db, "BEGIN IMMEDIATE", NULL, NULL, NULL);


  if (rc != SQLITE_OK) {


      pthread_mutex_unlock (&db_mutex);


      return rc;


  }





  rc = h_ship_claim_unlocked(db, player_id, sector_id, ship_id, out_ship);





  if (rc == SQLITE_OK) {


      sqlite3_exec (db, "COMMIT", NULL, NULL, NULL);


  } else {


      sqlite3_exec (db, "ROLLBACK", NULL, NULL, NULL);


      if (*out_ship) {


          json_decref(*out_ship);


          *out_ship = NULL;


      }


  }





  pthread_mutex_unlock (&db_mutex);


  return rc;


}

////////////////////////////////////////////////////////////////////
int
db_ship_rename_if_owner (int player_id, int ship_id, const char *new_name)
{
  if (!new_name || !*new_name)
    return SQLITE_MISUSE;

  int rc = SQLITE_OK;
  sqlite3_stmt *st = NULL;

  pthread_mutex_lock (&db_mutex);

  rc = sqlite3_exec (db_handle, "BEGIN IMMEDIATE", NULL, NULL, NULL);
  if (rc != SQLITE_OK)
    goto out_unlock;

  /* Verify ownership in ship_ownership */
  static const char *SQL_OWN =
    "SELECT 1 FROM ship_ownership WHERE ship_id=? AND player_id=?;";
  rc = sqlite3_prepare_v2 (db_handle, SQL_OWN, -1, &st, NULL);
  if (rc != SQLITE_OK)
    goto rollback;
  sqlite3_bind_int (st, 1, ship_id);
  sqlite3_bind_int (st, 2, player_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  st = NULL;
  if (rc != SQLITE_ROW)
    {
      rc = SQLITE_CONSTRAINT;
      goto rollback;
    }

  /* Do the rename */
  static const char *SQL_REN = "UPDATE ships SET name=? WHERE id=?;";
  rc = sqlite3_prepare_v2 (db_handle, SQL_REN, -1, &st, NULL);
  if (rc != SQLITE_OK)
    goto rollback;
  sqlite3_bind_text (st, 1, new_name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (st, 2, ship_id);
  rc = (sqlite3_step (st) == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
  sqlite3_finalize (st);
  st = NULL;
  if (rc != SQLITE_OK)
    goto rollback;

  sqlite3_exec (db_handle, "COMMIT", NULL, NULL, NULL);
  pthread_mutex_unlock (&db_mutex);
  return SQLITE_OK;

rollback:

  sqlite3_exec (db_handle, "ROLLBACK", NULL, NULL, NULL);
out_unlock:
  if (st)
    sqlite3_finalize (st);
  pthread_mutex_unlock (&db_mutex);
  return rc;
}


int
db_destroy_ship (sqlite3 *db, int player_id, int ship_id)
{
  int rc = SQLITE_ERROR;
  sqlite3_stmt *stmt = NULL;
  int ship_sector = 0;
  pthread_mutex_lock (&db_mutex);

  rc = sqlite3_exec (db, "BEGIN IMMEDIATE", NULL, NULL, NULL);
  if (rc != SQLITE_OK)
    goto out_unlock;

  // 1. Get ship details before deletion for logging/event creation
  char ship_name[256] = { 0 };
  // int ship_sector = 0;
  {
    const char *sql_get_ship_info =
      "SELECT name, sector FROM ships WHERE id = ?;";
    rc = sqlite3_prepare_v2 (db, sql_get_ship_info, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
      goto rollback;
    sqlite3_bind_int (stmt, 1, ship_id);
    if (sqlite3_step (stmt) == SQLITE_ROW)
      {
	strncpy (ship_name, (const char *) sqlite3_column_text (stmt, 0),
		 sizeof (ship_name) - 1);
	ship_sector = sqlite3_column_int (stmt, 1);
      }
    sqlite3_finalize (stmt);
    stmt = NULL;
  }

  // 2. Delete from ships table
  const char *sql_delete_ship = "DELETE FROM ships WHERE id = ?;";
  rc = sqlite3_prepare_v2 (db, sql_delete_ship, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    goto rollback;
  sqlite3_bind_int (stmt, 1, ship_id);
  rc = sqlite3_step (stmt);
  sqlite3_finalize (stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE)
    goto rollback;

  // 3. Delete from ship_ownership table
  const char *sql_delete_ownership =
    "DELETE FROM ship_ownership WHERE ship_id = ?;";
  rc = sqlite3_prepare_v2 (db, sql_delete_ownership, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    goto rollback;
  sqlite3_bind_int (stmt, 1, ship_id);
  rc = sqlite3_step (stmt);
  sqlite3_finalize (stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE)
    goto rollback;

  // 4. Update player's active ship to 0 if it was the destroyed ship
  const char *sql_update_player =
    "UPDATE players SET ship = 0 WHERE id = ? AND ship = ?;";
  rc = sqlite3_prepare_v2 (db, sql_update_player, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    goto rollback;
  sqlite3_bind_int (stmt, 1, player_id);
  sqlite3_bind_int (stmt, 2, ship_id);
  rc = sqlite3_step (stmt);
  sqlite3_finalize (stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE)
    goto rollback;

  // Log ship destroyed event
  json_t *payload = json_object ();
  json_object_set_new (payload, "ship_id", json_integer (ship_id));
  json_object_set_new (payload, "ship_name", json_string (ship_name));
  json_object_set_new (payload, "player_id", json_integer (player_id));
  //  h_log_engine_event((long long)time(NULL), "ship.destroyed", "player", player_id, ship_sector, payload, NULL);


  rc = sqlite3_exec (db, "COMMIT", NULL, NULL, NULL);
  if (rc != SQLITE_OK)
    goto out_unlock;

  pthread_mutex_unlock (&db_mutex);
  return SQLITE_OK;

rollback:
  sqlite3_exec (db, "ROLLBACK", NULL, NULL, NULL);
out_unlock:
  if (stmt)
    sqlite3_finalize (stmt);
  pthread_mutex_unlock (&db_mutex);
  return rc;
}

int
db_create_initial_ship (int player_id, const char *ship_name, int sector_id)
{
  sqlite3 *db = db_get_handle ();
  if (!db || player_id <= 0 || !ship_name || !*ship_name || sector_id <= 0)
    {
      LOGE("db_create_initial_ship: Invalid input player_id=%d, ship_name='%s', sector_id=%d", player_id, ship_name, sector_id);
      return -1;		// Invalid input
    }

  int rc = SQLITE_ERROR;
  sqlite3_stmt *stmt = NULL;
  int ship_type_id = -1;
  int new_ship_id = -1;

  // 1. Get shiptype_id for "Scout Marauder"
  static const char *SQL_GET_SHIPTYPE_ID =
    "SELECT id FROM shiptypes WHERE name = 'Scout Marauder';";
  rc = sqlite3_prepare_v2 (db, SQL_GET_SHIPTYPE_ID, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE("db_create_initial_ship: Failed to prepare shiptype ID lookup: %s", sqlite3_errmsg(db));
      return -1;
    }
  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      ship_type_id = sqlite3_column_int (stmt, 0);
      LOGI("db_create_initial_ship: Found 'Scout Marauder' with ID: %d", ship_type_id);
    } else {
      LOGE("db_create_initial_ship: 'Scout Marauder' shiptype not found in DB. rc: %d", rc);
    }
  sqlite3_finalize (stmt);
  stmt = NULL;

  if (ship_type_id == -1)
    {
      LOGE("db_create_initial_ship: Ship type ID for 'Scout Marauder' is -1.");
      return -1;
    }

  // 2. Create a new ship in the ships table
  static const char *SQL_CREATE_SHIP = 
    "INSERT INTO ships ("
    "  type_id, name, sector, "
    "  holds, fighters, shields, "
    "  ore, organics, equipment, colonists, "
    "  flags, perms, "
    "  attack, "
    "  mines, limpets, genesis, "
    "  detonators, probes, photons, beacons, "
    "  cloaking_devices, "
    "  has_transwarp, has_planet_scanner, has_long_range_scanner"
    ") "
    "SELECT "
    "  st.id, ?, ?, "
    "  st.initialholds, st.maxfighters, st.maxshields, "
    "  0, 0, 0, 0, "
    "  777, 731, "
    "  st.maxattack, "
    "  0, 0, 0, "
    "  0, 0, 0, 0, "
    "  0, "
    "  st.can_transwarp, st.can_planet_scan, st.can_long_range_scan "
    "FROM shiptypes st WHERE st.id = ?;";

  rc = sqlite3_prepare_v2 (db, SQL_CREATE_SHIP, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE("db_create_initial_ship: Failed to prepare SQL_CREATE_SHIP: %s", sqlite3_errmsg(db));
      return -1;
    }

  sqlite3_bind_text (stmt, 1, ship_name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (stmt, 2, sector_id);
  sqlite3_bind_int (stmt, 3, ship_type_id);	// shiptype_id for WHERE clause

  rc = sqlite3_step (stmt);
  if (rc != SQLITE_DONE)
    {
      LOGE("db_create_initial_ship: Failed to execute SQL_CREATE_SHIP: %s", sqlite3_errmsg(db));
      sqlite3_finalize (stmt);
      return -1;
    }
  sqlite3_finalize (stmt);
  stmt = NULL;

  new_ship_id = sqlite3_last_insert_rowid (db);
  if (new_ship_id <= 0) {
      LOGE("db_create_initial_ship: Failed to get new_ship_id after insert.");
      return -1;
  }
  LOGI("db_create_initial_ship: New ship created with ID: %d", new_ship_id);

  // 3. Assign ownership using h_ship_claim_unlocked
  // db_ship_claim also updates players.ship and sets primary status
  // Since we are already in a transaction, use the unlocked version
  rc = h_ship_claim_unlocked(db, player_id, sector_id, new_ship_id, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE("db_create_initial_ship: Failed to claim new ship: %s", sqlite3_errmsg(db));
      goto rollback;
    }
  LOGI("db_create_initial_ship: Ship claimed by player %d.", player_id);

  rc = sqlite3_exec (db, "COMMIT", NULL, NULL, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE("db_create_initial_ship: Failed to commit transaction: %s", sqlite3_errmsg(db));
      goto rollback;
    }
  LOGI("db_create_initial_ship: Ship creation and claim committed successfully.");

  pthread_mutex_unlock (&db_mutex);
  return new_ship_id;

rollback:
  sqlite3_exec (db, "ROLLBACK", NULL, NULL, NULL);
  LOGE("db_create_initial_ship: Transaction rolled back. Error: %s", sqlite3_errmsg(db));

  if (stmt)
    sqlite3_finalize (stmt);
  pthread_mutex_unlock (&db_mutex);
  return -1;
}

/* Decide if a player_id is an NPC (engine policy). 
   Adjust the predicate as you formalize NPC flags. */
static int
db_is_npc_player (int player_id)
{
  int rc, is_npc = 0;
  sqlite3_stmt *st = NULL;

  /* Example policy:
     - players.flags has an NPC bit, OR
     - player name is 'Ferrengi' or 'Imperial Starship' (fallback) */
  rc = sqlite3_prepare_v2 (db_handle, "SELECT " "  CASE " "    WHEN (flags & 0x0008) != 0 THEN 1 "	/* PFLAG_NPC example */
			   "    WHEN name IN ('Ferrengi','Imperial Starship') THEN 1 "
			   "    ELSE 0 "
			   "  END "
			   "FROM players WHERE id=?;", -1, &st, NULL);
  if (rc != SQLITE_OK)
    return 0;
  sqlite3_bind_int (st, 1, player_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    is_npc = sqlite3_column_int (st, 0);
  sqlite3_finalize (st);
  return is_npc;
}

/* Call when a ship’s pilot changes (or after spawning an NPC ship). */
int
db_apply_lock_policy_for_pilot (int ship_id, int new_pilot_player_id_or_0)
{
  int rc;

  pthread_mutex_lock (&db_mutex);
  rc = sqlite3_exec (db_handle, "BEGIN IMMEDIATE", NULL, NULL, NULL);
  if (rc != SQLITE_OK)
    {
      pthread_mutex_unlock (&db_mutex);
      return rc;
    }

  if (new_pilot_player_id_or_0 > 0
      && db_is_npc_player (new_pilot_player_id_or_0))
    {
      /* NPC piloted -> lock it */
      sqlite3_stmt *st = NULL;
      rc = sqlite3_prepare_v2 (db_handle,
			       "UPDATE ships SET flags = COALESCE(flags,0) | ? WHERE id=?;",
			       -1, &st, NULL);
      if (rc == SQLITE_OK)
	{
	  sqlite3_bind_int (st, 1, SHIPF_LOCKED);
	  sqlite3_bind_int (st, 2, ship_id);
	  rc = (sqlite3_step (st) == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
	}
      sqlite3_finalize (st);
    }
  else
    {
      /* Not NPC piloted (or unpiloted) -> clear LOCK (engine managed) */
      sqlite3_stmt *st = NULL;
      rc = sqlite3_prepare_v2 (db_handle,
			       "UPDATE ships SET flags = COALESCE(flags,0) & ~? WHERE id=?;",
			       -1, &st, NULL);
      if (rc == SQLITE_OK)
	{
	  sqlite3_bind_int (st, 1, SHIPF_LOCKED);
	  sqlite3_bind_int (st, 2, ship_id);
	  rc = (sqlite3_step (st) == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
	}
      sqlite3_finalize (st);
    }

  if (rc == SQLITE_OK)
    sqlite3_exec (db_handle, "COMMIT", NULL, NULL, NULL);
  else
    sqlite3_exec (db_handle, "ROLLBACK", NULL, NULL, NULL);

  pthread_mutex_unlock (&db_mutex);
  return rc;
}


/* Thread-safe: picks a random name into `out`.
   Returns SQLITE_OK on success, SQLITE_NOTFOUND if table empty, or SQLite error code. */
int
db_rand_npc_shipname (char *out, size_t out_sz)
{
  if (out && out_sz)
    out[0] = '\0';
  int rc;

  pthread_mutex_lock (&db_mutex);

  sqlite3_stmt *st = NULL;
  static const char *SQL =
    "SELECT name FROM npc_shipnames ORDER BY RANDOM() LIMIT 1;";

  rc =
    sqlite3_prepare_v3 (db_handle, SQL, -1, SQLITE_PREPARE_PERSISTENT, &st,
			NULL);
  if (rc == SQLITE_OK)
    {
      rc = sqlite3_step (st);
      if (rc == SQLITE_ROW)
	{
	  const unsigned char *uc = sqlite3_column_text (st, 0);
	  const char *name = (const char *) (uc ? (const char *) uc : "");
	  // const char *name = (const char *) (uc ? uc : "");
	  if (out && out_sz)
	    {
	      /* safe copy; always NUL-terminated */
	      snprintf (out, out_sz, "%s", name);
	    }
	  rc = SQLITE_OK;
	}
      else if (rc == SQLITE_DONE)
	{
	  rc = SQLITE_NOTFOUND;	/* no rows in npc_shipnames */
	}
      else
	{
	  /* rc is already an error code from sqlite3_step */
	}
    }
  if (st)
    sqlite3_finalize (st);
  pthread_mutex_unlock (&db_mutex);
  return rc;
}


int
db_ensure_ship_perms_column (void)
{
  int rc;
  pthread_mutex_lock (&db_mutex);
  rc = db_ensure_ship_perms_column_unlocked ();
  pthread_mutex_unlock (&db_mutex);
  return rc;
}


/////////////////////////////////

/* Unlocked: caller MUST already hold db_mutex */
static int
column_exists_unlocked (sqlite3 *db, const char *table, const char *col)
{
  sqlite3_stmt *st = NULL;
  int exists = 0;
  char sql[256];
  snprintf (sql, sizeof (sql), "PRAGMA table_info(%s);", table);
  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    return 0;
  while (sqlite3_step (st) == SQLITE_ROW)
    {
      const unsigned char *name = sqlite3_column_text (st, 1);
      if (name && strcmp ((const char *) name, col) == 0)
	{
	  exists = 1;
	  break;
	}
    }
  sqlite3_finalize (st);
  return exists;
}

/* Optional wrapper for callers that do NOT already hold db_mutex */
static int
column_exists (sqlite3 *db, const char *table, const char *col)
{
  int ret;
  pthread_mutex_lock (&db_mutex);
  ret = column_exists_unlocked (db, table, col);
  pthread_mutex_unlock (&db_mutex);
  return ret;
}

/* Unlocked: caller already holds db_mutex */
static int
db_ensure_ship_perms_column_unlocked (void)
{
  int rc = SQLITE_OK;
  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_ERROR;

  if (!column_exists_unlocked (db, "ships", "perms"))
    {
      char *errmsg = NULL;
      rc = sqlite3_exec (db,
			 "ALTER TABLE ships ADD COLUMN perms INTEGER NOT NULL DEFAULT 731;",
			 NULL, NULL, &errmsg);
      if (rc != SQLITE_OK)
	{
	  LOGE ("ALTER TABLE ships ADD COLUMN perms failed: %s",
		errmsg ? errmsg : "(unknown)");
	  sqlite3_free (errmsg);
	  return rc;
	}
    }
  return SQLITE_OK;
}

/* ---------- SECTOR SCAN CORE (thread-safe, single statement) ---------- */
/* Shape returned via *out_obj:
   {
     "name": TEXT,
     "safe_zone": INT,
     "port_count": INT,
     "ship_count": INT,
     "planet_count": INT,
     "beacon_text": TEXT
   }
*/
int
db_sector_scan_core (int sector_id, json_t **out_obj)
{
  if (out_obj)
    *out_obj = NULL;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_ERROR;

  const char *sql =
    "SELECT s.name, "
    "       0 AS safe_zone, "
    "       (SELECT COUNT(1) FROM ports   p  WHERE p.sector  = s.id) AS port_count, "
    "       (SELECT COUNT(1) FROM ships   sh WHERE sh.sector = s.id) AS ship_count, "
    "       (SELECT COUNT(1) FROM planets pl WHERE pl.sector  = s.id) AS planet_count, "
    "       s.beacon AS beacon_text " "FROM sectors s WHERE s.id = ?1";

  int rc = SQLITE_ERROR;
  sqlite3_stmt *st = NULL;

  /* Full critical section: prepare → bind → step → finalize */
  pthread_mutex_lock (&db_mutex);

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("[scan_core] prepare failed (sector=%d): %s",
	    sector_id, sqlite3_errmsg (db));
      goto done;
    }


  sqlite3_bind_int (st, 1, sector_id);

  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      const char *name = (const char *) sqlite3_column_text (st, 0);
      int safe_zone = sqlite3_column_int (st, 1);
      int port_count = sqlite3_column_int (st, 2);
      int ship_count = sqlite3_column_int (st, 3);
      int planet_count = sqlite3_column_int (st, 4);
      const char *btxt = (const char *) sqlite3_column_text (st, 5);

      json_t *o = json_object ();
      json_object_set_new (o, "name", json_string (name ? name : "Unknown"));
      json_object_set_new (o, "safe_zone", json_integer (safe_zone));
      json_object_set_new (o, "port_count", json_integer (port_count));
      json_object_set_new (o, "ship_count", json_integer (ship_count));
      json_object_set_new (o, "planet_count", json_integer (planet_count));
      json_object_set_new (o, "beacon_text", json_string (btxt ? btxt : ""));

      if (out_obj)
	*out_obj = o;
      rc = SQLITE_OK;
    }
  else
    {
      rc = SQLITE_ERROR;	/* no row → sector missing */
    }

done:
  if (st)
    sqlite3_finalize (st);
  pthread_mutex_unlock (&db_mutex);
  return rc;
}



int
db_notice_create (const char *title, const char *body,
		  const char *severity, time_t expires_at)
{
  static const char *SQL =
    "INSERT INTO system_notice (created_at, title, body, severity, expires_at) "
    "VALUES (strftime('%s','now'), ?1, ?2, ?3, ?4);";

  sqlite3 *db = db_get_handle ();
  if (!db)
    return -1;

  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2 (db, SQL, -1, &stmt, NULL) != SQLITE_OK)
    return -1;

  sqlite3_bind_text (stmt, 1, title ? title : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, 2, body ? body : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, 3, severity ? severity : "info", -1,
		     SQLITE_TRANSIENT);
  if (expires_at > 0)
    sqlite3_bind_int64 (stmt, 4, (sqlite3_int64) expires_at);
  else
    sqlite3_bind_null (stmt, 4);

  int rc = sqlite3_step (stmt);
  int ok = (rc == SQLITE_DONE);
  sqlite3_finalize (stmt);

  if (!ok)
    return -1;
  return (int) sqlite3_last_insert_rowid (db);
}

json_t *
db_notice_list_unseen_for_player (int player_id)
{
  static const char *SQL =
    "SELECT n.id, n.created_at, n.title, n.body, n.severity, n.expires_at "
    "FROM system_notice AS n "
    "LEFT JOIN notice_seen AS ns "
    "  ON ns.notice_id = n.id AND ns.player_id = ?1 "
    "WHERE (n.expires_at IS NULL OR n.expires_at > strftime('%s','now')) "
    "  AND ns.notice_id IS NULL " "ORDER BY n.created_at DESC;";

  sqlite3 *db = db_get_handle ();
  if (!db)
    return NULL;

  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2 (db, SQL, -1, &stmt, NULL) != SQLITE_OK)
    return NULL;

  sqlite3_bind_int (stmt, 1, player_id);

  json_t *arr = json_array ();
  if (!arr)
    {
      sqlite3_finalize (stmt);
      return NULL;
    }

  while (sqlite3_step (stmt) == SQLITE_ROW)
    {
      int id = sqlite3_column_int (stmt, 0);
      time_t created = (time_t) sqlite3_column_int64 (stmt, 1);
      const unsigned char *title = sqlite3_column_text (stmt, 2);
      const unsigned char *body = sqlite3_column_text (stmt, 3);
      const unsigned char *sev = sqlite3_column_text (stmt, 4);
      sqlite3_int64 expires_i64 = sqlite3_column_type (stmt, 5) == SQLITE_NULL
	? 0 : sqlite3_column_int64 (stmt, 5);

      json_t *obj = json_pack ("{s:i, s:i, s:s, s:s, s:s, s:i}",
			       "id", id,
			       "created_at", (int) created,
			       "title", title ? (const char *) title : "",
			       "body", body ? (const char *) body : "",
			       "severity", sev ? (const char *) sev : "info",
			       "expires_at", (int) expires_i64);
      if (obj)
	json_array_append_new (arr, obj);
    }

  sqlite3_finalize (stmt);
  return arr;
}

int
db_notice_mark_seen (int notice_id, int player_id)
{
  static const char *SQL =
    "INSERT OR REPLACE INTO notice_seen (notice_id, player_id, seen_at) "
    "VALUES (?1, ?2, strftime('%s','now'));";

  sqlite3 *db = db_get_handle ();
  if (!db)
    return -1;

  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2 (db, SQL, -1, &stmt, NULL) != SQLITE_OK)
    return -1;

  sqlite3_bind_int (stmt, 1, notice_id);
  sqlite3_bind_int (stmt, 2, player_id);

  int rc = sqlite3_step (stmt);
  sqlite3_finalize (stmt);
  return (rc == SQLITE_DONE) ? 0 : -1;
}


int
db_player_name (int64_t player_id, char **out)
{
  if (!out)
    return -2;
  *out = NULL;

  /* Use the same handle + mutex model as the rest of database.c */
  pthread_mutex_lock (&db_mutex);
  sqlite3 *dbh = db_get_handle ();
  if (!dbh)
    {
      pthread_mutex_unlock (&db_mutex);
      return -3;
    }

  static const char *SQL =
    "SELECT COALESCE(name, '') AS pname "
    "FROM players " "WHERE id = ?1 " "LIMIT 1";

  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (dbh, SQL, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      pthread_mutex_unlock (&db_mutex);
      return -4;
    }

  rc = sqlite3_bind_int64 (st, 1, (sqlite3_int64) player_id);
  if (rc != SQLITE_OK)
    {
      sqlite3_finalize (st);
      pthread_mutex_unlock (&db_mutex);
      return -5;
    }

  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      const unsigned char *txt = sqlite3_column_text (st, 0);
      if (txt && txt[0])
	{
	  size_t n = strlen ((const char *) txt);
	  char *dup = (char *) malloc (n + 1);
	  if (!dup)
	    {
	      sqlite3_finalize (st);
	      pthread_mutex_unlock (&db_mutex);
	      return -6;
	    }
	  memcpy (dup, txt, n + 1);
	  *out = dup;		/* caller will free() */
	  sqlite3_finalize (st);
	  pthread_mutex_unlock (&db_mutex);
	  return 0;
	}
      /* had a row but empty name — treat as not found */
    }
  sqlite3_finalize (st);
  pthread_mutex_unlock (&db_mutex);
  return -1;			/* not found */
}

int db_get_ship_name(sqlite3 *db, int ship_id, char **out_name) {
    if (!out_name) return -2;
    *out_name = NULL;

    pthread_mutex_lock(&db_mutex);
    sqlite3 *dbh = db;
    if (!dbh) {
        pthread_mutex_unlock(&db_mutex);
        return -3;
    }

    static const char *SQL = "SELECT COALESCE(name, '') FROM ships WHERE id = ?1 LIMIT 1";
    sqlite3_stmt *st = NULL;
    int rc = sqlite3_prepare_v2(dbh, SQL, -1, &st, NULL);
    if (rc != SQLITE_OK) {
        LOGE("db_get_ship_name: prepare failed: %s", sqlite3_errmsg(dbh));
        pthread_mutex_unlock(&db_mutex);
        return -4;
    }

    sqlite3_bind_int(st, 1, ship_id);
    rc = sqlite3_step(st);

    if (rc == SQLITE_ROW) {
        const unsigned char *txt = sqlite3_column_text(st, 0);
        if (txt && txt[0]) {
            *out_name = strdup((const char*)txt);
            if (!*out_name) {
                rc = SQLITE_NOMEM;
            } else {
                rc = SQLITE_OK;
            }
        } else {
            rc = SQLITE_NOTFOUND; // Ship found, but name is empty
        }
    } else if (rc == SQLITE_DONE) {
        rc = SQLITE_NOTFOUND; // No ship with that ID
    } else {
        LOGE("db_get_ship_name: execution failed: %s", sqlite3_errmsg(dbh));
        rc = SQLITE_ERROR;
    }

    sqlite3_finalize(st);
    pthread_mutex_unlock(&db_mutex);
    return rc;
}

int db_get_port_name(sqlite3 *db, int port_id, char **out_name) {
    if (!out_name) return -2;
    *out_name = NULL;

    pthread_mutex_lock(&db_mutex);
    sqlite3 *dbh = db;
    if (!dbh) {
        pthread_mutex_unlock(&db_mutex);
        return -3;
    }

    static const char *SQL = "SELECT COALESCE(name, '') FROM ports WHERE id = ?1 LIMIT 1";
    sqlite3_stmt *st = NULL;
    int rc = sqlite3_prepare_v2(dbh, SQL, -1, &st, NULL);
    if (rc != SQLITE_OK) {
        LOGE("db_get_port_name: prepare failed: %s", sqlite3_errmsg(dbh));
        pthread_mutex_unlock(&db_mutex);
        return -4;
    }

    sqlite3_bind_int(st, 1, port_id);
    rc = sqlite3_step(st);

    if (rc == SQLITE_ROW) {
        const unsigned char *txt = sqlite3_column_text(st, 0);
        if (txt && txt[0]) {
            *out_name = strdup((const char*)txt);
            if (!*out_name) {
                rc = SQLITE_NOMEM;
            } else {
                rc = SQLITE_OK;
            }
        } else {
            rc = SQLITE_NOTFOUND; // Port found, but name is empty
        }
    } else if (rc == SQLITE_DONE) {
        rc = SQLITE_NOTFOUND; // No port with that ID
    } else {
        LOGE("db_get_port_name: execution failed: %s", sqlite3_errmsg(dbh));
        rc = SQLITE_ERROR;
    }

    sqlite3_finalize(st);
    pthread_mutex_unlock(&db_mutex);
    return rc;
}

// Note: This SQL omits the 'id' (autoincrement) and 'processed_at' (defaults to NULL)
static const char *INSERT_ENGINE_EVENT_SQL =
  "INSERT INTO engine_events (ts, type, actor_owner_type, actor_owner_id, sector_id, payload) "
  "VALUES (?, ?, ?, ?, ?, ?);";

int
db_log_engine_event (long long ts,
		     const char *type,
		     const char *actor_owner_type, int actor_player_id,
		     int sector_id, json_t *payload, const char *idem_key)
{
  (void) idem_key;
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *stmt = NULL;
  int rc = SQLITE_ERROR;

  // 1. Convert the JSON payload object into a serialised string
  char *payload_str = json_dumps (payload, JSON_COMPACT);
  if (!payload_str)
    {
      // fprintf(stderr, "Error: Failed to serialize JSON payload.\n");
      return SQLITE_NOMEM;	// No memory or serialization error
    }

  // 2. Prepare the statement
  rc = sqlite3_prepare_v2 (db, INSERT_ENGINE_EVENT_SQL, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      // fprintf(stderr, "DB Error (prepare): %s\n", sqlite3_errmsg(db));
      goto cleanup;
    }

  // 3. Bind parameters
  sqlite3_bind_int64 (stmt, 1, ts);
  sqlite3_bind_text (stmt, 2, type, -1, SQLITE_STATIC);

  // Bind owner type and ID
  if (actor_owner_type && strlen (actor_owner_type) > 0)
    {
      sqlite3_bind_text (stmt, 3, actor_owner_type, -1, SQLITE_STATIC);
    }
  else
    {
      sqlite3_bind_null (stmt, 3);
    }

  if (actor_player_id > 0)
    {
      sqlite3_bind_int (stmt, 4, actor_player_id);
    }
  else
    {
      sqlite3_bind_null (stmt, 4);
    }

  if (sector_id > 0)
    {
      sqlite3_bind_int (stmt, 5, sector_id);
    }
  else
    {
      sqlite3_bind_null (stmt, 5);
    }

  // Bind the JSON string (SQLITE_TRANSIENT copies the string, safe for us to free)
  sqlite3_bind_text (stmt, 6, payload_str, -1, SQLITE_TRANSIENT);

  // 4. Execute the statement
  rc = sqlite3_step (stmt);
  if (rc != SQLITE_DONE)
    {
      // fprintf(stderr, "DB Error (step): %s\n", sqlite3_errmsg(db));
      rc = SQLITE_ERROR;	// Set a general error code for return
    }
  else
    {
      rc = SQLITE_OK;		// Success
    }

cleanup:
  // 5. Cleanup
  sqlite3_finalize (stmt);
  // Free the string created by json_dumps
  free (payload_str);

  return rc;
}


int
db_news_insert_feed_item (int ts, const char *category, const char *scope,
			  const char *headline, const char *body,
			  json_t *context_data)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *stmt = NULL;
  int rc = SQLITE_ERROR;
  char *article_text = NULL;
  char *context_str = NULL;
  int expiration_ts = ts + NEWS_EXPIRATION_SECONDS;	// NEWS_EXPIRATION_SECONDS is defined in server_cron.h

  // Combine headline and body into article_text
  // Add scope to context_data if it's not null
  if (scope && strlen (scope) > 0)
    {
      if (!context_data)
	{
	  context_data = json_object ();
	}
      json_object_set_new (context_data, "scope", json_string (scope));
    }

  if (context_data)
    {
      context_str = json_dumps (context_data, JSON_COMPACT);
      if (!context_str)
	{
	  LOGE
	    ("ERROR: db_news_insert_feed_item Failed to serialize context_data.");
	  goto cleanup;
	}
      if (asprintf
	  (&article_text, "HEADLINE: %s\nBODY: %s\nCONTEXT: %s", headline,
	   body, context_str) == -1)
	{
	  LOGE
	    ("ERROR: db_news_insert_feed_item Failed to allocate article_text with context.");
	  goto cleanup;
	}
    }
  else
    {
      if (asprintf (&article_text, "HEADLINE: %s\nBODY: %s", headline, body)
	  == -1)
	{
	  LOGE
	    ("ERROR: db_news_insert_feed_item Failed to allocate article_text without context.");
	  goto cleanup;
	}
    }

  if (!article_text)
    {
      LOGE
	("ERROR: db_news_insert_feed_item Failed to allocate article_text.");
      goto cleanup;
    }

  const char *sql =
    "INSERT INTO news_feed (published_ts, expiration_ts, news_category, article_text) "
    "VALUES (?, ?, ?, ?);";

  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("ERROR: db_news_insert_feed_item prepare failed: %s",
	    sqlite3_errmsg (db));
      goto cleanup;
    }

  sqlite3_bind_int (stmt, 1, ts);
  sqlite3_bind_int (stmt, 2, expiration_ts);
  sqlite3_bind_text (stmt, 3, category, -1, SQLITE_STATIC);
  sqlite3_bind_text (stmt, 4, article_text, -1, SQLITE_TRANSIENT);

  rc = sqlite3_step (stmt);
  if (rc != SQLITE_DONE)
    {
      LOGE ("ERROR: db_news_insert_feed_item execution failed: %s",
	    sqlite3_errmsg (db));
      rc = SQLITE_ERROR;
    }
  else
    {
      rc = SQLITE_OK;
    }

cleanup:
  if (stmt)
    sqlite3_finalize (stmt);
  if (article_text)
    free (article_text);
  if (context_str)
    free (context_str);
  if (context_data)
    json_decref (context_data);	// Decref if created locally or passed in

  return rc;
}

int
db_is_sector_fedspace (int ck_sector)
{
  sqlite3 *db = db_get_handle ();
  //sqlite3_stmt *stmt = NULL;
  int rc = SQLITE_ERROR;

  static const char *FEDSPACE_SQL =
    "SELECT sector_id from stardock_location where sector_id=?1;";

  sqlite3_stmt *st = NULL;

  /* Full critical section: prepare → bind → step → finalize */
  pthread_mutex_lock (&db_mutex);

  rc = sqlite3_prepare_v2 (db, FEDSPACE_SQL, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_is_sector_fedspace: %s", sqlite3_errmsg (db));
      goto done;
    }

  sqlite3_bind_int (st, 1, ck_sector);

  rc = sqlite3_step (st);
  int sec_ret = 1;
  if (rc == SQLITE_ROW)
    {
      sec_ret = sqlite3_column_int (st, 0);
    }

  if ((ck_sector == sec_ret) || (ck_sector >= 1 && ck_sector <= 10))
    {
      rc = 1;
    }
  else
    {
      rc = 0;
    }

done:
  if (st)
    sqlite3_finalize (st);
  pthread_mutex_unlock (&db_mutex);
  return rc;
}



/* Returns the port_id (primary key) for a given sector, or -1 on error/not found */
int
db_get_port_id_by_sector (int sector_id)
{
  sqlite3 *db = db_get_handle ();	// Get the handle
  sqlite3_stmt *stmt = NULL;
  int port_id = -1;

  const char *sql = "SELECT id FROM ports WHERE sector=?1;";

  pthread_mutex_lock (&db_mutex);	// Critical section starts

  if (sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
      LOGE ("db_get_port_id_by_sector: %s", sqlite3_errmsg (db));
      goto done;
    }

  sqlite3_bind_int (stmt, 1, sector_id);

  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      port_id = sqlite3_column_int (stmt, 0);
    }

done:
  sqlite3_finalize (stmt);
  pthread_mutex_unlock (&db_mutex);	// Critical section ends
  return port_id;
}


int
db_get_ship_sector_id (sqlite3 *db, int ship_id)
{
  sqlite3_stmt *st = NULL;
  int rc;			// For SQLite's intermediate return codes.
  int out_sector = 0;

  // 1. Acquire the lock at the very beginning of the function.
  pthread_mutex_lock (&db_mutex);

  const char *sql = "SELECT sector FROM ships WHERE id=?";

  // 2. Prepare the statement. Use the passed 'db' handle.
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      // Log error if preparation fails
      LOGE ("Failed to prepare SQL statement for ship sector ID.");
      goto cleanup;
    }

  sqlite3_bind_int (st, 1, ship_id);

  // 3. Step the statement.
  rc = sqlite3_step (st);

  if (rc == SQLITE_ROW)
    {
      // Data found: safely read the column, treating NULL as 0
      out_sector =
	sqlite3_column_type (st,
			     0) == SQLITE_NULL ? 0 : sqlite3_column_int (st,
									 0);
    }
  else if (rc != SQLITE_DONE)
    {
      // Handle step errors (not SQLITE_ROW and not SQLITE_DONE)
      LOGE ("Error stepping SQL statement for ship sector ID.");
      // out_sector remains 0
    }
  // If rc == SQLITE_DONE, the ship was not found, and out_sector remains 0, which is correct.

cleanup:
  // 4. Finalize the statement. This must be done whether the function succeeded or failed.
  if (st)
    {
      sqlite3_finalize (st);
    }

  // 5. Release the lock. This is the final step before returning.
  pthread_mutex_unlock (&db_mutex);

  // 6. Return the result. 0 on error or not found, >0 on success.
  return out_sector;
}


void
h_generate_hex_uuid (char *buffer, size_t buffer_size)
{
  if (buffer_size < 33)
    {				// 32 chars + null terminator
      return;
    }
  // A simple pseudo-random hex string generation. Not a true UUID.
  // For a proper UUID, platform-specific functions or libuuid would be better.
  // This is sufficient for grouping transactions in this context.
  const char *hex_chars = "0123456789abcdef";
  for (size_t i = 0; i < 32; ++i)
    {
      buffer[i] = hex_chars[rand () % 16];
    }
  buffer[32] = '\0';
}


// Helper to get the account_id for a system-owned account (e.g., for fees/taxes)
int
h_get_system_account_id_unlocked (sqlite3 *db, const char *system_owner_type,
				  int system_owner_id, int *account_id_out)
{
  int rc = h_get_account_id_unlocked (db, system_owner_type, system_owner_id,
				      account_id_out);
  if (rc == SQLITE_NOTFOUND)
    {
      // Create the system account if it doesn't exist
      rc =
	h_create_bank_account_unlocked (db, system_owner_type,
					system_owner_id, 0, account_id_out);
      if (rc != SQLITE_OK)
	{
	  LOGE
	    ("h_get_system_account_id_unlocked: Failed to create system account %s:%d (rc=%d)",
	     system_owner_type, system_owner_id, rc);
	}
    }
  return rc;
}

// New helper to get account_id from owner_type and owner_id
int
h_get_account_id_unlocked (sqlite3 *db, const char *owner_type, int owner_id,
			   int *account_id_out)
{
  sqlite3_stmt *st = NULL;
  int rc;
  int account_id = -1;

  // Select the account ID
  const char *SQL_GET_ID =
    "SELECT id FROM bank_accounts WHERE owner_type = ?1 AND owner_id = ?2 AND currency = 'CRD'";
  rc = sqlite3_prepare_v2 (db, SQL_GET_ID, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_get_account_id_unlocked: Failed to prepare statement: %s",
	    sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_text (st, 1, owner_type, -1, SQLITE_STATIC);
  sqlite3_bind_int (st, 2, owner_id);

  if (sqlite3_step (st) == SQLITE_ROW)
    {
      account_id = sqlite3_column_int (st, 0);
      rc = SQLITE_OK;
    }
  else
    {
      rc = SQLITE_NOTFOUND;
    }

  sqlite3_finalize (st);
  *account_id_out = account_id;
  return rc;
}


// Helper to create a bank account and return its ID
int
h_create_bank_account_unlocked (sqlite3 *db, const char *owner_type,
				int owner_id, long long initial_balance,
				int *account_id_out)
{
  sqlite3_stmt *stmt = NULL;
  int rc = SQLITE_ERROR;
  // Current timestamp in epoch days for last_interest_tick
  int current_epoch_day = (int) (time (NULL) / (24 * 60 * 60));
  const char *sql = "INSERT INTO bank_accounts (owner_type, owner_id, currency, balance, interest_rate_bp, last_interest_tick, tx_alert_threshold, is_active) VALUES (?1, ?2, 'CRD', ?3, 0, ?4, 0, 1);";	// Using 'CRD' as default currency

  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_create_bank_account_unlocked: Failed to prepare statement: %s",
	    sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_text (stmt, 1, owner_type, -1, SQLITE_STATIC);
  sqlite3_bind_int (stmt, 2, owner_id);
  sqlite3_bind_int64 (stmt, 3, initial_balance);
  sqlite3_bind_int (stmt, 4, current_epoch_day);

  rc = sqlite3_step (stmt);
  if (rc == SQLITE_DONE)
    {
      *account_id_out = (int) sqlite3_last_insert_rowid (db);
      rc = SQLITE_OK;
    }
  else
    {
      LOGE ("h_create_bank_account_unlocked: Failed to execute statement: %s",
	    sqlite3_errmsg (db));
    }
  sqlite3_finalize (stmt);
  return rc;
}


// Helper to get a specific integer config value
long long
h_get_config_int_unlocked (sqlite3 *db, const char *key,
			   long long default_value)
{
  sqlite3_stmt *stmt = NULL;
  long long value = default_value;

  // Direct bind of column name is not possible with ?, so use sqlite3_mprintf
  char *dynamic_sql =
    sqlite3_mprintf ("SELECT %q FROM config WHERE id = 1;", key);
  if (!dynamic_sql)
    {
      LOGE
	("h_get_config_int_unlocked: Memory allocation failed for SQL query.");
      return default_value;
    }

  int rc = sqlite3_prepare_v2 (db, dynamic_sql, -1, &stmt, NULL);
  sqlite3_free (dynamic_sql);	// Free dynamic SQL string immediately
  if (rc != SQLITE_OK)
    {
      LOGE
	("h_get_config_int_unlocked: Failed to prepare statement for key %s: %s",
	 key, sqlite3_errmsg (db));
      goto cleanup;
    }

  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      value = sqlite3_column_int64 (stmt, 0);
    }
  else
    {
      LOGW
	("h_get_config_int_unlocked: Config key %s not found or error. Using default value %lld.",
	 key, default_value);
    }

cleanup:
  if (stmt)
    {
      sqlite3_finalize (stmt);
    }
  return value;
}

// Helper to get tx_alert_threshold for an account
static long long
h_get_account_alert_threshold_unlocked (sqlite3 *db, int account_id,
					const char *owner_type)
{
  long long threshold = 0;	// Default to 0 (disabled)
  sqlite3_stmt *stmt = NULL;
  int rc;

  const char *sql =
    "SELECT tx_alert_threshold FROM bank_accounts WHERE id = ?;";
  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
	("h_get_account_alert_threshold_unlocked: Failed to prepare statement: %s",
	 sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int (stmt, 1, account_id);
  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      threshold = sqlite3_column_int64 (stmt, 0);
    }
  sqlite3_finalize (stmt);
  stmt = NULL;			// Reset stmt pointer

  // If account-specific threshold is 0, fall back to global defaults from config
  if (threshold == 0)
    {
      if (strcmp (owner_type, "player") == 0)
	{
	  threshold =
	    h_get_config_int_unlocked (db, "bank_alert_threshold_player",
				       1000000);
	}
      else if (strcmp (owner_type, "corp") == 0)
	{
	  threshold =
	    h_get_config_int_unlocked (db, "bank_alert_threshold_corp",
				       5000000);
	}
      // For other owner_types, if no specific config, threshold remains 0
    }

cleanup:
  return threshold;
}

/******************************************************************************
 *  BANKING – FINAL CANONICAL VERSIONS
 *  Matches signatures in database.h exactly.
 *  Must replace ALL previous versions of these functions.
 ******************************************************************************/

/*
 * Internal helper: Add credits. Caller must hold db_mutex.
 */
int
h_add_credits_unlocked (sqlite3 *db,
			int account_id,
			long long amount,
			const char *tx_type,
			const char *tx_group_id, long long *new_balance_out)
{
  if (!db || account_id <= 0 || amount <= 0 || !tx_type)
    return SQLITE_MISUSE;

  sqlite3_stmt *stmt = NULL;
  int rc;

  const char *sql =
    "INSERT INTO bank_transactions (account_id, tx_type, direction, amount, currency, ts, tx_group_id) "
    "VALUES (?, ?, 'CREDIT', ?, 'CRD', strftime('%s','now'), ?);";

  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_int (stmt, 1, account_id);
  sqlite3_bind_text (stmt, 2, tx_type, -1, SQLITE_STATIC);
  sqlite3_bind_int64 (stmt, 3, amount);

  if (tx_group_id)
    sqlite3_bind_text (stmt, 4, tx_group_id, -1, SQLITE_STATIC);
  else
    sqlite3_bind_null (stmt, 4);

  rc = sqlite3_step (stmt);
  sqlite3_finalize (stmt);
  if (rc != SQLITE_DONE)
    return rc;

  if (new_balance_out)
    {
      sqlite3_stmt *b = NULL;
      rc =
	sqlite3_prepare_v2 (db,
			    "SELECT balance FROM bank_accounts WHERE id=?;",
			    -1, &b, NULL);
      if (rc == SQLITE_OK)
	{
	  sqlite3_bind_int (b, 1, account_id);
	  if (sqlite3_step (b) == SQLITE_ROW)
	    *new_balance_out = sqlite3_column_int64 (b, 0);
	}
      sqlite3_finalize (b);
    }

  return SQLITE_OK;
}

/*
 * Internal helper: Deduct credits. Caller must hold db_mutex.
 */
int
h_deduct_credits_unlocked (sqlite3 *db,
			   int account_id,
			   long long amount,
			   const char *tx_type,
			   const char *tx_group_id,
			   long long *new_balance_out)
{
  if (!db || account_id <= 0 || amount <= 0 || !tx_type)
    return SQLITE_MISUSE;

  sqlite3_stmt *stmt = NULL;
  int rc;

  const char *sql =
    "INSERT INTO bank_transactions (account_id, tx_type, direction, amount, currency, ts, tx_group_id) "
    "VALUES (?, ?, 'DEBIT', ?, 'CRD', strftime('%s','now'), ?);";

  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_int (stmt, 1, account_id);
  sqlite3_bind_text (stmt, 2, tx_type, -1, SQLITE_STATIC);
  sqlite3_bind_int64 (stmt, 3, amount);

  if (tx_group_id)
    sqlite3_bind_text (stmt, 4, tx_group_id, -1, SQLITE_STATIC);
  else
    sqlite3_bind_null (stmt, 4);

  rc = sqlite3_step (stmt);
  sqlite3_finalize (stmt);

  /* SQLITE_CONSTRAINT means “insufficient funds” because of trigger */
  if (rc != SQLITE_DONE)
    return rc;

  if (new_balance_out)
    {
      sqlite3_stmt *b = NULL;
      rc =
	sqlite3_prepare_v2 (db,
			    "SELECT balance FROM bank_accounts WHERE id=?;",
			    -1, &b, NULL);
      if (rc == SQLITE_OK)
	{
	  sqlite3_bind_int (b, 1, account_id);
	  if (sqlite3_step (b) == SQLITE_ROW)
	    *new_balance_out = sqlite3_column_int64 (b, 0);
	}
      sqlite3_finalize (b);
    }

  return SQLITE_OK;
}

/*
 * Public: Add credits to owner's account.
 */
int
h_add_credits (sqlite3 *db, const char *owner_type, int owner_id,
		   long long amount, const char *tx_type,
		   const char *tx_group_id, long long *new_balance_out)
{
  if (!db || owner_type == NULL || owner_id <= 0 || amount <= 0 || tx_type == NULL) {
      LOGE("h_add_credits: Invalid input. owner_type=%s, owner_id=%d, amount=%lld, tx_type=%s", owner_type, owner_id, amount, tx_type);
      return SQLITE_MISUSE;
  }
  
  int rc;
  pthread_mutex_lock (&db_mutex);

  // Get or create account ID
  int account_id = -1;
  rc = h_get_account_id_unlocked (db, owner_type, owner_id, &account_id);
  if (rc == SQLITE_NOTFOUND)
    {
      LOGD("h_add_credits: Account not found for %s:%d, creating new account.", owner_type, owner_id);
      rc =
	h_create_bank_account_unlocked (db, owner_type, owner_id, 0,
					&account_id);
      if (rc != SQLITE_OK)
	{
	  LOGE("h_add_credits: Failed to create bank account for %s:%d, rc=%d", owner_type, owner_id, rc);
	  pthread_mutex_unlock (&db_mutex);
	  return rc;
	}
    }
  else if (rc != SQLITE_OK)
    {
      LOGE("h_add_credits: Failed to get bank account for %s:%d, rc=%d", owner_type, owner_id, rc);
      pthread_mutex_unlock (&db_mutex);
      return rc;
    }

  LOGD("h_add_credits: Adding %lld credits to account %d (%s:%d)", amount, account_id, owner_type, owner_id);
  rc = h_add_credits_unlocked (db, account_id, amount, tx_type, tx_group_id, new_balance_out);
  if (rc != SQLITE_OK) {
      LOGE("h_add_credits: h_add_credits_unlocked failed for account %d, rc=%d", account_id, rc);
  } else {
      LOGD("h_add_credits: Credits added successfully. New balance: %lld", new_balance_out ? *new_balance_out : -1);
  }

  pthread_mutex_unlock (&db_mutex);
  return rc;
}

/*
 * Public: Deduct credits from owner's account.
 */
int
h_deduct_credits (sqlite3 *db,
		  const char *owner_type,
		  int owner_id,
		  long long amount,
		  const char *tx_type,
		  const char *tx_group_id, long long *new_balance_out)
{
  if (!db || !owner_type || amount <= 0)
    return SQLITE_MISUSE;

  pthread_mutex_lock (&db_mutex);

  int account_id = -1;
  int rc = h_get_account_id_unlocked (db, owner_type, owner_id, &account_id);
  if (rc != SQLITE_OK)
    {
      pthread_mutex_unlock (&db_mutex);
      return rc;
    }

  rc = h_deduct_credits_unlocked (db, account_id, amount,
				  tx_type ? tx_type : "DEBIT",
				  tx_group_id, new_balance_out);

  pthread_mutex_unlock (&db_mutex);
  return rc;
}



// Function to calculate fees and taxes for a given transaction type and amount
int
calculate_fees (sqlite3 *db, const char *tx_type, long long base_amount,
		const char *owner_type, fee_result_t *out)
{
  if (!db || !tx_type || !out)
    {
      return SQLITE_MISUSE;
    }

  out->fee_total = 0;
  out->fee_to_bank = 0;
  out->tax_to_system = 0;

  sqlite3_stmt *stmt = NULL;
  int rc = SQLITE_ERROR;
  long long current_time = time (NULL);

  // Query for active fee rules
  const char *sql = "SELECT fee_code, value, is_percentage, min_tx_amount, max_tx_amount " "FROM bank_fee_schedules " "WHERE tx_type = ?1 " "  AND (owner_type IS NULL OR owner_type = ?2) " "  AND currency = 'CRD' "	// Assuming CRD for now
    "  AND effective_from <= ?3 " "  AND (effective_to IS NULL OR effective_to >= ?3) " "ORDER BY fee_code;";	// Deterministic order

  pthread_mutex_lock (&db_mutex);

  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("calculate_fees: Failed to prepare statement: %s",
	    sqlite3_errmsg (db));
      goto cleanup;
    }

  sqlite3_bind_text (stmt, 1, tx_type, -1, SQLITE_STATIC);
  sqlite3_bind_text (stmt, 2, owner_type, -1, SQLITE_STATIC);
  sqlite3_bind_int64 (stmt, 3, current_time);

  while (sqlite3_step (stmt) == SQLITE_ROW)
    {
      const char *fee_code = (const char *) sqlite3_column_text (stmt, 0);
      long long value = sqlite3_column_int64 (stmt, 1);
      int is_percentage = sqlite3_column_int (stmt, 2);
      long long min_tx_amount = sqlite3_column_int64 (stmt, 3);
      long long max_tx_amount = sqlite3_column_type (stmt, 4) == SQLITE_NULL ? -1 : sqlite3_column_int64 (stmt, 4);	// -1 for no max

      // Apply min/max transaction amount filters
      if (base_amount < min_tx_amount)
	continue;
      if (max_tx_amount != -1 && base_amount > max_tx_amount)
	continue;

      long long charge = 0;
      if (is_percentage)
	{
	  charge = (base_amount * value) / 10000;	// value is in basis points
	}
      else
	{
	  charge = value;	// flat fee
	}

      // Clamp charge at 0 and max (optional, config-driven)
      if (charge < 0)
	charge = 0;
      // if (charge > MAX_CHARGE_AMOUNT) charge = MAX_CHARGE_AMOUNT; // Needs global config

      out->fee_total += charge;

      // Distribute charge to specific categories based on fee_code
      // This is a placeholder; actual distribution logic may be more complex
      if (strcmp (fee_code, "TAX_RATE") == 0 || strcmp (tx_type, "TAX") == 0)
	{
	  out->tax_to_system += charge;
	}
      else
	{
	  out->fee_to_bank += charge;
	}
    }

  rc = SQLITE_OK;

cleanup:
  if (stmt)
    {
      sqlite3_finalize (stmt);
    }
  pthread_mutex_unlock (&db_mutex);
  return rc;
}

/*
 * Helper function to update commodity stock on a planet.
 * Returns SQLITE_OK on success, or an SQLite error code.
 * If new_quantity is not NULL, it will be set to the commodity's new quantity.
 */
int
h_update_planet_stock (sqlite3 *db, int planet_id, const char *commodity_code,
		       int quantity_change, int *new_quantity)
{
  sqlite3_stmt *stmt = NULL;
  int rc = SQLITE_ERROR;

  if (!db || !commodity_code || planet_id <= 0)
    {
      return SQLITE_MISUSE;
    }

  pthread_mutex_lock (&db_mutex);

  const char *sql =
    "UPDATE planet_goods SET quantity = quantity + ? "
    "WHERE planet_id = ? AND commodity = ?;";

  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_update_planet_stock: Failed to prepare statement: %s",
	    sqlite3_errmsg (db));
      goto cleanup;
    }

  sqlite3_bind_int (stmt, 1, quantity_change);
  sqlite3_bind_int (stmt, 2, planet_id);
  sqlite3_bind_text (stmt, 3, commodity_code, -1, SQLITE_STATIC);

  rc = sqlite3_step (stmt);
  if (rc != SQLITE_DONE)
    {
      LOGE ("h_update_planet_stock: Failed to execute statement: %s",
	    sqlite3_errmsg (db));
      goto cleanup;
    }

  if (new_quantity)
    {
      sqlite3_stmt *qty_stmt = NULL;
      const char *qty_sql =
	"SELECT quantity FROM planet_goods WHERE planet_id = ? AND commodity = ?;";
      rc = sqlite3_prepare_v2 (db, qty_sql, -1, &qty_stmt, NULL);
      if (rc == SQLITE_OK)
	{
	  sqlite3_bind_int (qty_stmt, 1, planet_id);
	  sqlite3_bind_text (qty_stmt, 2, commodity_code, -1, SQLITE_STATIC);
	  if (sqlite3_step (qty_stmt) == SQLITE_ROW)
	    {
	      *new_quantity = sqlite3_column_int (qty_stmt, 0);
	    }
	  sqlite3_finalize (qty_stmt);
	}
      else
	{
	  LOGE
	    ("h_update_planet_stock: Failed to prepare quantity statement: %s",
	     sqlite3_errmsg (db));
	}
    }

  rc = SQLITE_OK;

cleanup:
  if (stmt)
    {
      sqlite3_finalize (stmt);
    }
  pthread_mutex_unlock (&db_mutex);
  return rc;
}

/*
 * Helper function to update commodity stock on a port.
 * Returns SQLITE_OK on success, or an SQLite error code.
 * If new_quantity is not NULL, it will be set to the commodity's new quantity.
 */
int
h_update_port_stock (sqlite3 *db, int port_id, const char *commodity_code,
		     int quantity_change, int *new_quantity)
{
  sqlite3_stmt *stmt = NULL;
  int rc = SQLITE_ERROR;

  if (!db || !commodity_code || port_id <= 0)
    {
      return SQLITE_MISUSE;
    }

  pthread_mutex_lock (&db_mutex);

  const char *sql =
    "UPDATE ports SET "
    "ore_on_hand = CASE WHEN ?3 = 'ore' THEN ore_on_hand + ?1 ELSE ore_on_hand END, "
    "organics_on_hand = CASE WHEN ?3 = 'organics' THEN organics_on_hand + ?1 ELSE organics_on_hand END, "
    "equipment_on_hand = CASE WHEN ?3 = 'equipment' THEN equipment_on_hand + ?1 ELSE equipment_on_hand END, "
    "slaves_on_hand = CASE WHEN ?3 = 'SLV' THEN slaves_on_hand + ?1 ELSE slaves_on_hand END, "
    "weapons_on_hand = CASE WHEN ?3 = 'WPN' THEN weapons_on_hand + ?1 ELSE weapons_on_hand END, "
    "drugs_on_hand = CASE WHEN ?3 = 'DRG' THEN drugs_on_hand + ?1 ELSE drugs_on_hand END "
    "WHERE id = ?2;";

  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_update_port_stock: Failed to prepare statement: %s",
	    sqlite3_errmsg (db));
      goto cleanup;
    }

  sqlite3_bind_int (stmt, 1, quantity_change);
  sqlite3_bind_int (stmt, 2, port_id);
  sqlite3_bind_text (stmt, 3, commodity_code, -1, SQLITE_STATIC);

  rc = sqlite3_step (stmt);
  if (rc != SQLITE_DONE)
    {
      LOGE ("h_update_port_stock: Failed to execute statement: %s",
	    sqlite3_errmsg (db));
      goto cleanup;
    }

  if (new_quantity)
    {
      sqlite3_stmt *qty_stmt = NULL;
      const char *qty_sql =
	"SELECT CASE "
	"WHEN ?2 = 'ore' THEN ore_on_hand "
	"WHEN ?2 = 'organics' THEN organics_on_hand "
	"WHEN ?2 = 'equipment' THEN equipment_on_hand "
    "WHEN ?2 = 'SLV' THEN slaves_on_hand "
    "WHEN ?2 = 'WPN' THEN weapons_on_hand "
    "WHEN ?2 = 'DRG' THEN drugs_on_hand "
	"ELSE 0 END " "FROM ports WHERE id = ?1;";
      rc = sqlite3_prepare_v2 (db, qty_sql, -1, &qty_stmt, NULL);
      if (rc == SQLITE_OK)
	{
	  sqlite3_bind_int (qty_stmt, 1, port_id);
	  sqlite3_bind_text (qty_stmt, 2, commodity_code, -1, SQLITE_STATIC);
	  if (sqlite3_step (qty_stmt) == SQLITE_ROW)
	    {
	      *new_quantity = sqlite3_column_int (qty_stmt, 0);
	    }
	  sqlite3_finalize (qty_stmt);
	}
      else
	{
	  LOGE
	    ("h_update_port_stock: Failed to prepare quantity statement: %s",
	     sqlite3_errmsg (db));
	}
    }

  rc = SQLITE_OK;

cleanup:
  if (stmt)
    {
      sqlite3_finalize (stmt);
    }
  pthread_mutex_unlock (&db_mutex);
  return rc;
}

int
db_fighters_at_sector_json (int sector_id, json_t **out_array)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  json_t *arr = NULL;
  int rc = SQLITE_ERROR;

  pthread_mutex_lock (&db_mutex);

  if (!out_array)
    goto cleanup;

  *out_array = NULL;
  arr = json_array ();
  if (!arr)
    {
      rc = SQLITE_NOMEM;
      goto cleanup;
    }

  const char *sql =
    "SELECT player, corporation, quantity "
    "FROM sector_assets WHERE sector = ? AND asset_type = ?;";

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    goto cleanup;

  sqlite3_bind_int (st, 1, sector_id);
  sqlite3_bind_int (st, 2, ASSET_FIGHTER);

  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      int player_id = sqlite3_column_int (st, 0);
      int corporation_id = sqlite3_column_int (st, 1);
      int quantity = sqlite3_column_int (st, 2);

      json_t *obj = json_object ();
      if (!obj)
	{
	  rc = SQLITE_NOMEM;
	  goto cleanup;
	}

      if (corporation_id > 0)
	{
	  json_object_set_new (obj, "owner_type", json_string ("corporation"));
	  json_object_set_new (obj, "owner_id", json_integer (corporation_id));
	}
      else if (player_id > 0)
	{
	  json_object_set_new (obj, "owner_type", json_string ("player"));
	  json_object_set_new (obj, "owner_id", json_integer (player_id));
	}
      else
        {
	  // Should not happen if asset has an owner
          json_object_set_new (obj, "owner_type", json_string ("unknown"));
          json_object_set_new (obj, "owner_id", json_integer (0));
        }
      
      json_object_set_new (obj, "quantity", json_integer (quantity));
      json_array_append_new (arr, obj);
    }

  if (rc == SQLITE_DONE)
    {
      *out_array = arr;
      rc = SQLITE_OK;
      arr = NULL; // Transfer ownership
    }
  else
    {
      // Error in sqlite3_step
      rc = SQLITE_ERROR;
    }

cleanup:
  if (st)
    sqlite3_finalize (st);
  if (arr)
    json_decref (arr);
  pthread_mutex_unlock (&db_mutex);
  return rc;
}

int
db_mines_at_sector_json (int sector_id, json_t **out_array)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  json_t *arr = NULL;
  int rc = SQLITE_ERROR;

  pthread_mutex_lock (&db_mutex);

  if (!out_array)
    goto cleanup;

  *out_array = NULL;
  arr = json_array ();
  if (!arr)
    {
      rc = SQLITE_NOMEM;
      goto cleanup;
    }

  const char *sql =
    "SELECT player, corporation, quantity, asset_type "
    "FROM sector_assets WHERE sector = ? AND asset_type IN (?, ?);";

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    goto cleanup;

  sqlite3_bind_int (st, 1, sector_id);
  sqlite3_bind_int (st, 2, ASSET_MINE);
  sqlite3_bind_int (st, 3, ASSET_LIMPET_MINE);

  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      int player_id = sqlite3_column_int (st, 0);
      int corporation_id = sqlite3_column_int (st, 1);
      int quantity = sqlite3_column_int (st, 2);
      int asset_type = sqlite3_column_int (st, 3);

      json_t *obj = json_object ();
      if (!obj)
	{
	  rc = SQLITE_NOMEM;
	  goto cleanup;
	}

      if (corporation_id > 0)
	{
	  json_object_set_new (obj, "owner_type", json_string ("corporation"));
	  json_object_set_new (obj, "owner_id", json_integer (corporation_id));
	}
      else if (player_id > 0)
	{
	  json_object_set_new (obj, "owner_type", json_string ("player"));
	  json_object_set_new (obj, "owner_id", json_integer (player_id));
	}
      else
        {
	  // Should not happen if asset has an owner
          json_object_set_new (obj, "owner_type", json_string ("unknown"));
          json_object_set_new (obj, "owner_id", json_integer (0));
        }
      
      json_object_set_new (obj, "quantity", json_integer (quantity));
      json_object_set_new (obj, "mine_type", json_integer (asset_type)); // Add mine type

      json_array_append_new (arr, obj);
    }

  if (rc == SQLITE_DONE)
    {
      *out_array = arr;
      rc = SQLITE_OK;
      arr = NULL; // Transfer ownership
    }
  else
    {
      // Error in sqlite3_step
      rc = SQLITE_ERROR;
    }

cleanup:
  if (st)
    sqlite3_finalize (st);
  if (arr)
    json_decref (arr);
  pthread_mutex_unlock (&db_mutex);
  return rc;
}
/*
* =================================================================================
* GENERIC BANK BALANCE FUNCTIONS
* =================================================================================
*
* This section provides a generic way to retrieve bank balances for different
* entity types (player, corp, npc, port, planet).
*
* It uses a single static helper function `h_get_bank_balance` to perform
* the database query, and then exposes public wrapper functions for each
* entity type.
*
*/

/**
 * @brief Generic helper to get a bank balance for any owner type.
 * @param owner_type The type of the owner (e.g., "player", "corp").
 * @param owner_id The ID of the owner.
 * @param out_balance Pointer to store the resulting balance.
 * @return SQLITE_OK on success, or an error code.
 */
static int
h_get_bank_balance (const char *owner_type, int owner_id,
		    long long *out_balance)
{
  if (out_balance == NULL)
    {
      return SQLITE_MISUSE;
    }

  *out_balance = 0;		// Default to 0

  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return SQLITE_ERROR;
    }

  int account_id = -1;
  // Note: This helper already locks. We need to reconsider mutex strategy for nested calls.
  // For now, assume this is called within a wider mutex scope or will acquire its own.
  // Given the public wrappers don't lock, this needs to be thread-safe.
  // Let's modify h_get_account_id_unlocked to be h_get_account_id and acquire its own lock.
  // Or, ensure h_get_bank_balance itself is thread-safe.
  // The previous h_get_account_id_unlocked is meant to be called when db_mutex is already held.
  // h_get_bank_balance is static and called by public functions that also lock, so it's okay.

  int rc_account_id =
    h_get_account_id_unlocked (db, owner_type, owner_id, &account_id);

  if (rc_account_id == SQLITE_NOTFOUND)
    {
      return SQLITE_OK;		// No account means balance is 0
    }
  else if (rc_account_id != SQLITE_OK)
    {
      return rc_account_id;	// Error in getting account_id
    }

  const char *sql = "SELECT balance FROM bank_accounts WHERE id = ?1;";	// Use account_id
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }

  sqlite3_bind_int (st, 1, account_id);

  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      *out_balance = sqlite3_column_int64 (st, 0);
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      // Should not happen if h_get_account_id_unlocked found an account, but handle defensively.
      rc = SQLITE_OK;
    }
  else
    {
      rc = SQLITE_ERROR;
    }

  sqlite3_finalize (st);
  return rc;
}

/**
 * @brief Gets the bank balance for a player.
 */
int
db_get_player_bank_balance (int player_id, long long *out_balance)
{
  return h_get_bank_balance ("player", player_id, out_balance);
}

/**
 * @brief Gets the bank balance for a corporation.
 */
int
db_get_corp_bank_balance (int corp_id, long long *out_balance)
{
  return h_get_bank_balance ("corp", corp_id, out_balance);
}

/**
 * @brief Gets the bank balance for an NPC.
 */
int
db_get_npc_bank_balance (int npc_id, long long *out_balance)
{
  return h_get_bank_balance ("npc", npc_id, out_balance);
}

/**
 * @brief Gets the bank balance for a port.
 */
int
db_get_port_bank_balance (int port_id, long long *out_balance)
{
  return h_get_bank_balance ("port", port_id, out_balance);
}

/**
 * @brief Gets the bank balance for a planet.
 */
int
db_get_planet_bank_balance (int planet_id, long long *out_balance)
{
  return h_get_bank_balance ("planet", planet_id, out_balance);
}

int
db_bank_account_exists (const char *owner_type, int owner_id)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return -1;		// Error
    }
  int account_id = -1;
  int rc;
  pthread_mutex_lock (&db_mutex);
  rc = h_get_account_id_unlocked (db, owner_type, owner_id, &account_id);
  pthread_mutex_unlock (&db_mutex);

  if (rc == SQLITE_OK)
    {
      return 1;			// Account exists
    }
  else if (rc == SQLITE_NOTFOUND)
    {
      return 0;			// Account does not exist
    }
  else
    {
      return -1;		// Error
    }
}

/*
* =================================================================================
* BATCH DATABASE FUNCTIONS (Corrected - No Internal Transactions)
* =================================================================================
*
* These functions perform single database operations. For atomicity across
* multiple operations (like a transfer), the calling code is responsible for
* wrapping the calls in a transaction.
*
* Example:
*   sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
*   int rc = db_bank_transfer(...);
*   if (rc == SQLITE_OK) {
*       sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
*   } else {
*       sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
*   }
*/

/**
 * @brief Creates a new bank account for a given owner.
 * @param owner_type The type of the owner (e.g., "player", "corp").
 * @param owner_id The ID of the owner.
 * @param initial_balance The initial balance for the account.
 * @param account_id_out Pointer to store the ID of the newly created account.
 * @return SQLITE_OK on success, or an SQLite error code.
 */
int
db_bank_create_account (const char *owner_type, int owner_id,
			long long initial_balance, int *account_id_out)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return SQLITE_ERROR;
    }

  int rc;
  pthread_mutex_lock (&db_mutex);
  rc =
    h_create_bank_account_unlocked (db, owner_type, owner_id, initial_balance,
				    account_id_out);
  pthread_mutex_unlock (&db_mutex);
  return rc;
}

int
db_bank_deposit (const char *owner_type, int owner_id, long long amount)
{
  if (amount <= 0)
    return SQLITE_MISUSE;

  return h_add_credits (db_get_handle (), owner_type, owner_id, amount, "DEPOSIT",	// tx_type
			NULL,	// tx_group_id
			NULL	// new_balance_out
    );
}

int
db_bank_withdraw (const char *owner_type, int owner_id, long long amount)
{
  if (amount <= 0)
    return SQLITE_MISUSE;

  return h_deduct_credits (db_get_handle (), owner_type, owner_id, amount, "WITHDRAWAL",	// tx_type
			   NULL,	// tx_group_id
			   NULL	// new_balance_out
    );
}

/**
 * @brief Transfers an amount between two bank accounts.
 * @return SQLITE_OK on success, or an error code from withdraw/deposit.
 */
int
db_bank_transfer (const char *from_owner_type, int from_owner_id,
		  const char *to_owner_type, int to_owner_id,
		  long long amount)
{
  pthread_mutex_lock (&db_mutex);
  int rc =
    h_bank_transfer_unlocked (db_get_handle (), from_owner_type,
			      from_owner_id,
			      to_owner_type, to_owner_id,
			      amount, "TRANSFER", NULL);
  pthread_mutex_unlock (&db_mutex);
  return rc;
}

/* int h_bank_transfer_unlocked (sqlite3 * db, */
/*                               const char *from_owner_type, int from_owner_id, */
/*                               const char *to_owner_type, int to_owner_id, */
/*                               long long amount, */
/*                               const char *tx_type, const char *tx_group_id) */
/* { */
/*   int from_account_id, to_account_id; */
/*   int rc; */

/*   // Get source account ID */
/*   rc = h_get_account_id_unlocked(db, from_owner_type, from_owner_id, &from_account_id); */
/*   if (rc != SQLITE_OK) { */
/*       // If source account doesn't exist, and it's not a system account, it's an error. */
/*       // System accounts might be implicit. */
/*       if (strcmp(from_owner_type, "system") != 0 && strcmp(from_owner_type, "gov") != 0) { */
/*           LOGW("h_bank_transfer_unlocked: Source account %s:%d not found.", from_owner_type, from_owner_id); */
/*           return SQLITE_NOTFOUND; */
/*       } */
/*       // For system/gov, if not found, it implies no balance to deduct from, so treat as insufficient funds */
/*       LOGW("h_bank_transfer_unlocked: Implicit system/gov source account %s:%d not found or created. Treating as insufficient funds.", from_owner_type, from_owner_id); */
/*       return SQLITE_CONSTRAINT; // Special return for insufficient funds */
/*   } */

/*   // Get or create destination account ID */
/*   rc = h_get_account_id_unlocked(db, to_owner_type, to_owner_id, &to_account_id); */
/*   if (rc == SQLITE_NOTFOUND) { */
/*       rc = h_create_bank_account_unlocked(db, to_owner_type, to_owner_id, 0, &to_account_id); */
/*       if (rc != SQLITE_OK) { */
/*           LOGE("h_bank_transfer_unlocked: Failed to create destination account %s:%d: %s", */
/*                to_owner_type, to_owner_id, sqlite3_errmsg(db)); */
/*           return rc; */
/*       } */
/*   } else if (rc != SQLITE_OK) { */
/*       LOGE("h_bank_transfer_unlocked: Failed to get destination account %s:%d: %s", */
/*            to_owner_type, to_owner_id, sqlite3_errmsg(db)); */
/*       return rc; */
/*   } */

/*   // Deduct from source */
/*   rc = h_deduct_credits_unlocked(db, from_account_id, amount, tx_type, tx_group_id, NULL); */
/*   if (rc != SQLITE_OK) { */
/*       LOGW("h_bank_transfer_unlocked: Failed to deduct %lld from %s:%d (account %d). Error: %d", */
/*            amount, from_owner_type, from_owner_id, from_account_id, rc); */
/*       return rc; // Insufficient funds or other deduction error */
/*   } */

/*   // Add to destination */
/*   rc = h_add_credits_unlocked(db, to_account_id, amount, tx_type, tx_group_id, NULL); */
/*   if (rc != SQLITE_OK) { */
/*       LOGE("h_bank_transfer_unlocked: Failed to add %lld to %s:%d (account %d). This should not happen after successful deduction. Error: %d", */
/*            amount, to_owner_type, to_owner_id, to_account_id, rc); */
/*       // Attempt to refund the source account (critical error recovery) */
/*       h_add_credits_unlocked(db, from_account_id, amount, "REFUND", "TRANSFER_FAILED", NULL); */
/*       return rc; */
/*   } */

/*   return SQLITE_OK; */
/* } */


extern long long h_get_account_alert_threshold_unlocked (sqlite3 * db,
							 int account_id,
							 const char
							 *owner_type);
extern int h_get_account_id_unlocked (sqlite3 * db, const char *owner_type,
				      int owner_id, int *account_id_out);
extern int h_create_bank_account_unlocked (sqlite3 * db,
					   const char *owner_type,
					   int owner_id,
					   long long initial_balance,
					   int *account_id_out);
extern int h_deduct_credits_unlocked (sqlite3 * db, int account_id,
				      long long amount, const char *tx_type,
				      const char *tx_group_id,
				      long long *new_balance_out);
extern int h_add_credits_unlocked (sqlite3 * db, int account_id,
				   long long amount, const char *tx_type,
				   const char *tx_group_id,
				   long long *new_balance_out);
extern int db_notice_create (const char *title, const char *body, const char *severity, time_t expires_at);	// Existing system notice creation

// --- NEW HELPER: For creating personalized bank alert notices ---
// This helper function creates a system notice but includes context data
// that can be used by a communication module to target specific players
// or filter for relevant notices.
static int
h_create_personal_bank_alert_notice (sqlite3 *db,
				     const char *target_owner_type,
				     int target_owner_id, const char *title,
				     const char *body,
				     json_t *transfer_context)
{
  if (!db || !target_owner_type || target_owner_id <= 0 || !title || !body)
    {
      LOGE ("h_create_personal_bank_alert_notice: Invalid arguments.");
      return -1;
    }

  json_t *full_context = json_object ();
  if (!full_context)
    {
      LOGE ("h_create_personal_bank_alert_notice: OOM creating context.");
      return -1;
    }

  // Add target owner info to the context
  json_object_set_new (full_context, "target_owner_type",
		       json_string (target_owner_type));
  json_object_set_new (full_context, "target_owner_id",
		       json_integer (target_owner_id));

  // Add transfer-specific context
  if (transfer_context)
    {
      // Deep copy the transfer_context into full_context to avoid decref issues later
      json_t *transfer_context_copy = json_deep_copy (transfer_context);
      if (transfer_context_copy)
	{
	  json_object_update (full_context, transfer_context_copy);
	  json_decref (transfer_context_copy);	// The update function increments ref, so we decref original copy
	}
      else
	{
	  LOGE
	    ("h_create_personal_bank_alert_notice: OOM copying transfer_context.");
	}
    }

  // Dump context as body for db_notice_create
  char *context_str = json_dumps (full_context, JSON_COMPACT);
  if (!context_str)
    {
      LOGE ("h_create_personal_bank_alert_notice: OOM dumping context JSON.");
      json_decref (full_context);
      return -1;
    }

  // For now, severity will be "info", expires_at will be current time + 7 days
  // The actual delivery will be handled by the subscription/notification system.
  time_t expires_at_ts = time (NULL) + (7 * 24 * 60 * 60);	// 7 days from now
  int rc = db_notice_create (title, context_str, "info", expires_at_ts);	// Use the original db_notice_create
  free (context_str);
  json_decref (full_context);

  if (rc < 0)
    {
      LOGE
	("h_create_personal_bank_alert_notice: Failed to create system notice.");
      return -1;
    }
  return 0;
}


/**
 * @brief Transfers credits between two owners.
 * Updated for Issue 376: Now triggers bank alerts if transfer size exceeds defined thresholds.
 *
 * @param db The SQLite database handle.
 * @param from_owner_type The type of the sender's owner (e.g., "player", "corp").
 * @param from_owner_id The ID of the sender's owner.
 * @param to_owner_type The type of the recipient's owner.
 * @param to_owner_id The ID of the recipient's owner.
 * @param amount The amount to transfer.
 * @param tx_type The transaction type (e.g., "TRANSFER").
 * @param tx_group_id An optional group ID for related transactions.
 * @return SQLITE_OK on success, or an SQLite error code.
 */
int
h_bank_transfer_unlocked (sqlite3 *db,
			  const char *from_owner_type, int from_owner_id,
			  const char *to_owner_type, int to_owner_id,
			  long long amount,
			  const char *tx_type, const char *tx_group_id)
{
  int from_account_id, to_account_id;
  int rc;

  // Get source account ID
  rc =
    h_get_account_id_unlocked (db, from_owner_type, from_owner_id,
			       &from_account_id);
  if (rc != SQLITE_OK)
    {
      // If source account doesn't exist, and it's not a system account, it's an error.
      // System accounts might be implicit.
      if (strcmp (from_owner_type, "system") != 0
	  && strcmp (from_owner_type, "gov") != 0)
	{
	  LOGW ("h_bank_transfer_unlocked: Source account %s:%d not found.",
		from_owner_type, from_owner_id);
	  return SQLITE_NOTFOUND;
	}
      // For system/gov, if not found, it implies no balance to deduct from, so treat as insufficient funds
      LOGW
	("h_bank_transfer_unlocked: Implicit system/gov source account %s:%d not found or created. Treating as insufficient funds.",
	 from_owner_type, from_owner_id);
      return SQLITE_CONSTRAINT;	// Special return for insufficient funds
    }

  // Get or create destination account ID
  rc =
    h_get_account_id_unlocked (db, to_owner_type, to_owner_id,
			       &to_account_id);
  if (rc == SQLITE_NOTFOUND)
    {
      rc =
	h_create_bank_account_unlocked (db, to_owner_type, to_owner_id, 0,
					&to_account_id);
      if (rc != SQLITE_OK)
	{
	  LOGE
	    ("h_bank_transfer_unlocked: Failed to create destination account %s:%d: %s",
	     to_owner_type, to_owner_id, sqlite3_errmsg (db));
	  return rc;
	}
    }
  else if (rc != SQLITE_OK)
    {
      LOGE
	("h_bank_transfer_unlocked: Failed to get destination account %s:%d: %s",
	 to_owner_type, to_owner_id, sqlite3_errmsg (db));
      return rc;
    }

  // Deduct from source
  rc =
    h_deduct_credits_unlocked (db, from_account_id, amount, tx_type,
			       tx_group_id, NULL);
  if (rc != SQLITE_OK)
    {
      LOGW
	("h_bank_transfer_unlocked: Failed to deduct %lld from %s:%d (account %d). Error: %d",
	 amount, from_owner_type, from_owner_id, from_account_id, rc);
      return rc;		// Insufficient funds or other deduction error
    }

  // Add to destination
  rc =
    h_add_credits_unlocked (db, to_account_id, amount, tx_type, tx_group_id,
			    NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
	("h_bank_transfer_unlocked: Failed to add %lld to %s:%d (account %d). This should not happen after successful deduction. Error: %d",
	 amount, to_owner_type, to_owner_id, to_account_id, rc);
      // Attempt to refund the source account (critical error recovery)
      h_add_credits_unlocked (db, from_account_id, amount, "REFUND",
			      "TRANSFER_FAILED", NULL);
      return rc;
    }

  /* --------------------------------------------------------------------------
   * Issue 376: Logic for "bank.alerts"
   * Check thresholds and generate notices for large transfers.
   * -------------------------------------------------------------------------- */
  {
    long long from_threshold = 0;
    long long to_threshold = 0;

    // 1. Check Thresholds
    // We retrieve the alert threshold for both accounts. 
    // h_get_account_alert_threshold_unlocked is assumed to return long long and take owner_type.
    from_threshold =
      h_get_account_alert_threshold_unlocked (db, from_account_id,
					      from_owner_type);
    to_threshold =
      h_get_account_alert_threshold_unlocked (db, to_account_id,
					      to_owner_type);

    // 2. Evaluate Alert
    // Trigger if amount meets/exceeds threshold, provided threshold is positive.
    int alert_sender = (from_threshold > 0 && amount >= from_threshold);
    int alert_receiver = (to_threshold > 0 && amount >= to_threshold);

    if (alert_sender || alert_receiver)
      {
	// Prepare context data common to both notices
	json_t *transfer_context = json_object ();
	if (transfer_context)
	  {
	    json_object_set_new (transfer_context, "amount",
				 json_integer (amount));
	    json_object_set_new (transfer_context, "from_owner_type",
				 json_string (from_owner_type));
	    json_object_set_new (transfer_context, "from_owner_id",
				 json_integer (from_owner_id));
	    json_object_set_new (transfer_context, "to_owner_type",
				 json_string (to_owner_type));
	    json_object_set_new (transfer_context, "to_owner_id",
				 json_integer (to_owner_id));
	    json_object_set_new (transfer_context, "tx_type",
				 json_string (tx_type));
	    // tx_group_id is optional, only add if not NULL
	    if (tx_group_id)
	      {
		json_object_set_new (transfer_context, "tx_group_id",
				     json_string (tx_group_id));
	      }
	  }
	else
	  {
	    LOGE
	      ("h_bank_transfer_unlocked: OOM creating transfer_context for alert.");
	  }


	// 3. Construct and Create Notices
	char title_buffer[128];
	char body_buffer[512];	// Using snprintf for body might be better if complex, else just use context_str

	if (alert_sender)
	  {
	    snprintf (title_buffer, sizeof (title_buffer),
		      "Large Transfer Sent (ID: %d)", from_owner_id);
	    snprintf (body_buffer, sizeof (body_buffer),
		      "You sent %lld credits to %s:%d.", amount,
		      to_owner_type, to_owner_id);
	    h_create_personal_bank_alert_notice (db, from_owner_type,
						 from_owner_id, title_buffer,
						 body_buffer,
						 transfer_context);
	  }
	if (alert_receiver)
	  {
	    snprintf (title_buffer, sizeof (title_buffer),
		      "Large Transfer Received (ID: %d)", to_owner_id);
	    snprintf (body_buffer, sizeof (body_buffer),
		      "You received %lld credits from %s:%d.", amount,
		      from_owner_type, from_owner_id);
	    h_create_personal_bank_alert_notice (db, to_owner_type,
						 to_owner_id, title_buffer,
						 body_buffer,
						 transfer_context);
	  }

	if (transfer_context)
	  {
	    json_decref (transfer_context);
	  }
      }
  }
  /* -------------------------------------------------------------------------- */

  return SQLITE_OK;
}



/* // NOTE: The remaining functions are placeholders and will need to be implemented. */
/* int */
/* db_bank_get_transactions (const char *owner_type, int owner_id, int limit, */
/* 			  json_t **out_array) */
/* { */
/*   return SQLITE_OK; */
/* } */

/* int */
/* db_bank_apply_interest () */
/* { */
/*   return SQLITE_OK; */
/* } */

/* int */
/* db_bank_process_orders () */
/* { */
/*   return SQLITE_OK; */
/* } */

/* int */
/* db_bank_set_flags (const char *owner_type, int owner_id, int flags) */
/* { */
/*   return SQLITE_OK; */
/* } */

/* int */
/* db_bank_get_flags (const char *owner_type, int owner_id, int *out_flags) */
/* { */
/*   return SQLITE_OK; */
/* } */

/* int */
/* db_commodity_get_price (const char *commodity_code, int *out_price) */
/* { */
/*   return SQLITE_OK; */
/* } */

/* int */
/* db_commodity_update_price (const char *commodity_code, int new_price) */
/* { */
/*   return SQLITE_OK; */
/* } */

/* int */
/* db_commodity_create_order (const char *actor_type, int actor_id, */
/* 			   const char *commodity_code, const char *side, */
/* 			   int quantity, int price) */
/* { */
/*   return SQLITE_OK; */
/* } */

/* int */
/* db_commodity_fill_order (int order_id, int quantity) */
/* { */
/*   return SQLITE_OK; */
/* } */

/* int */
/* db_commodity_get_orders (const char *commodity_code, const char *status, */
/* 			 json_t **out_array) */
/* { */
/*   return SQLITE_OK; */
/* } */

/* int */
/* db_commodity_get_trades (const char *commodity_code, int limit, */
/* 			 json_t **out_array) */
/* { */
/*   return SQLITE_OK; */
/* } */

/* int */
/* db_port_get_goods_on_hand (int port_id, const char *commodity_code, */
/* 			   int *out_quantity) */
/* { */
/*   return SQLITE_OK; */
/* } */

/* int */
/* db_port_update_goods_on_hand (int port_id, const char *commodity_code, */
/* 			      int quantity_change) */
/* { */
/*   return SQLITE_OK; */
/* } */

/* int */
/* db_planet_get_goods_on_hand (int planet_id, const char *commodity_code, */
/* 			     int *out_quantity) */
/* { */
/*   return SQLITE_OK; */
/* } */

/* int */
/* db_planet_update_goods_on_hand (int planet_id, const char *commodity_code, */
/* 				int quantity_change) */
/* { */
/*   return SQLITE_OK; */
/* } */


static int
stmt_to_json_array (sqlite3_stmt *st, json_t **out_array)
{
  if (!out_array)
    {
      /* Caller doesn’t want JSON, just drain the cursor */
      int rc;
      while ((rc = sqlite3_step (st)) == SQLITE_ROW)
	;
      if (rc == SQLITE_DONE)
	return SQLITE_OK;
      return rc;
    }

  json_t *arr = json_array ();
  if (!arr)
    return SQLITE_NOMEM;

  int rc;
  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      int cols = sqlite3_column_count (st);
      json_t *obj = json_object ();
      if (!obj)
	{
	  rc = SQLITE_NOMEM;
	  break;
	}

      for (int i = 0; i < cols; i++)
	{
	  const char *col_name = sqlite3_column_name (st, i);
	  int col_type = sqlite3_column_type (st, i);
	  json_t *val = NULL;

	  switch (col_type)
	    {
	    case SQLITE_INTEGER:
	      val = json_integer (sqlite3_column_int64 (st, i));
	      break;
	    case SQLITE_FLOAT:
	      val = json_real (sqlite3_column_double (st, i));
	      break;
	    case SQLITE_TEXT:
	      val = json_string ((const char *) sqlite3_column_text (st, i));
	      break;
	    case SQLITE_NULL:
	    default:
	      val = json_null ();
	      break;
	    }

	  if (!val)
	    {
	      json_decref (obj);
	      rc = SQLITE_NOMEM;
	      goto done;
	    }

	  /* ignore failure here; names come from SQLite */
	  json_object_set_new (obj, col_name, val);
	}

      if (json_array_append_new (arr, obj) != 0)
	{
	  json_decref (obj);
	  rc = SQLITE_NOMEM;
	  break;
	}
    }

done:
  if (rc == SQLITE_DONE)
    {
      *out_array = arr;
      return SQLITE_OK;
    }

  json_decref (arr);
  return rc;
}



/* ----------------------------------------------------------------------
 * Bank: transactions
 * ---------------------------------------------------------------------- */

int
db_bank_get_transactions (const char *owner_type, int owner_id, int limit,
			  const char *tx_type_filter, long long start_date,
			  long long end_date, long long min_amount,
			  long long max_amount, json_t **out_array)
{
  if (!owner_type)
    return SQLITE_MISUSE;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_MISUSE;

  if (limit <= 0)
    limit = 100;

  char sql[1024];
  char where_clause[512];
  int bind_pos = 3;

  // Base query
  strcpy (sql,
	  "SELECT t.* FROM bank_transactions t JOIN bank_accounts a ON t.account_id = a.id WHERE a.owner_type = ?1 AND a.owner_id = ?2 ");

  // Build WHERE clause
  strcpy (where_clause, "");
  if (tx_type_filter)
    {
      strcat (where_clause, "AND t.tx_type = ? ");
    }
  if (start_date > 0)
    {
      strcat (where_clause, "AND t.ts >= ? ");
    }
  if (end_date > 0)
    {
      strcat (where_clause, "AND t.ts <= ? ");
    }
  if (min_amount > 0)
    {
      strcat (where_clause, "AND t.amount >= ? ");
    }
  if (max_amount > 0)
    {
      strcat (where_clause, "AND t.amount <= ? ");
    }

  strcat (sql, where_clause);
  strcat (sql, "ORDER BY t.ts DESC, t.id DESC LIMIT ?");

  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_bank_get_transactions: SQL error: %s on query %s",
	    sqlite3_errmsg (db), sql);
      return rc;
    }

  sqlite3_bind_text (st, 1, owner_type, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (st, 2, owner_id);

  if (tx_type_filter)
    {
      sqlite3_bind_text (st, bind_pos++, tx_type_filter, -1,
			 SQLITE_TRANSIENT);
    }
  if (start_date > 0)
    {
      sqlite3_bind_int64 (st, bind_pos++, start_date);
    }
  if (end_date > 0)
    {
      sqlite3_bind_int64 (st, bind_pos++, end_date);
    }
  if (min_amount > 0)
    {
      sqlite3_bind_int64 (st, bind_pos++, min_amount);
    }
  if (max_amount > 0)
    {
      sqlite3_bind_int64 (st, bind_pos++, max_amount);
    }
  sqlite3_bind_int (st, bind_pos, limit);

  json_t *arr = NULL;
  rc = stmt_to_json_array (st, out_array ? &arr : NULL);
  sqlite3_finalize (st);

  if (rc == SQLITE_OK && out_array)
    *out_array = arr;
  else if (arr)
    json_decref (arr);

  return rc;
}

/* ----------------------------------------------------------------------
 * Bank: interest & orders
 *
 * NOTE: The schema excerpt you provided is too truncated to implement
 * these correctly (we can't reliably see all NOT NULL columns of
 * bank_interest_policy or bank_orders). They remain explicit TODO
 * placeholders instead of silently doing the wrong thing.
 * ---------------------------------------------------------------------- */

int
db_bank_apply_interest (void)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_MISUSE;

  /* TODO: implement based on full bank_interest_policy / bank_accounts
   * schema; for now this is an explicit no-op.
   */
  return SQLITE_OK;
}

int
db_bank_process_orders (void)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_MISUSE;

  /* TODO: implement processing of bank_orders into bank_transactions.
   * Left as a no-op because the bank_orders schema is truncated here.
   */
  return SQLITE_OK;
}

/* ----------------------------------------------------------------------
 * Bank: flags (risk / behaviour flags)
 *
 * Assumes schema roughly:
 *   CREATE TABLE bank_flags (
 *      player_id INTEGER PRIMARY KEY REFERENCES players(id) ON DELETE CASCADE,
 *      flags     INTEGER NOT NULL DEFAULT 0,
 *      risk_tier TEXT NOT NULL DEFAULT 'normal' CHECK(...)
 *   );
 *
 * owner_type is accepted for future extension; currently only 'player'
 * is supported and mapped directly to player_id.
 * ---------------------------------------------------------------------- */

int
db_bank_set_frozen_status (const char *owner_type, int owner_id,
			   int is_frozen)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_MISUSE;

  /* At present the table is keyed by player_id only. */
  if (!owner_type || strcmp (owner_type, "player") != 0)
    return SQLITE_ERROR;

  const char *sql =
    "INSERT INTO bank_flags (player_id, is_frozen) "
    "VALUES (?1, ?2) "
    "ON CONFLICT(player_id) DO UPDATE SET is_frozen = excluded.is_frozen";

  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_int (st, 1, owner_id);
  sqlite3_bind_int (st, 2, is_frozen);

  rc = sqlite3_step (st);
  if (rc == SQLITE_DONE)
    rc = SQLITE_OK;

  sqlite3_finalize (st);
  return rc;
}

int
db_bank_get_frozen_status (const char *owner_type, int owner_id,
			   int *out_is_frozen)
{
  if (!out_is_frozen)
    return SQLITE_MISUSE;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_MISUSE;

  if (!owner_type || strcmp (owner_type, "player") != 0)
    return SQLITE_ERROR;

  const char *sql =
    "SELECT is_frozen " "FROM bank_flags " "WHERE player_id = ?1";

  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_int (st, 1, owner_id);

  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      *out_is_frozen = sqlite3_column_int (st, 0);
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      /* No row → treat as 0 flags. */
      *out_is_frozen = 0;
      rc = SQLITE_OK;
    }

  sqlite3_finalize (st);
  return rc;
}

/* ----------------------------------------------------------------------
 * Commodities: get / update price
 *
 * We don't know the exact price column name, so try a small set of
 * plausible candidates in order.
 * ---------------------------------------------------------------------- */

static int
select_price_with_column (sqlite3 *db, const char *col,
			  const char *commodity_code, int *out_price)
{
  const char *sql_tmpl = "SELECT %s FROM commodities WHERE code = ?1";
  char sql[256];

  snprintf (sql, sizeof sql, sql_tmpl, col);

  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_text (st, 1, commodity_code, -1, SQLITE_TRANSIENT);

  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      if (out_price)
	*out_price = sqlite3_column_int (st, 0);
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      rc = SQLITE_NOTFOUND;
    }

  sqlite3_finalize (st);
  return rc;
}

static int
update_price_with_column (sqlite3 *db, const char *col,
			  const char *commodity_code, int new_price)
{
  const char *sql_tmpl = "UPDATE commodities SET %s = ?1 WHERE code = ?2";
  char sql[256];

  snprintf (sql, sizeof sql, sql_tmpl, col);

  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_int (st, 1, new_price);
  sqlite3_bind_text (st, 2, commodity_code, -1, SQLITE_TRANSIENT);

  rc = sqlite3_step (st);
  if (rc == SQLITE_DONE)
    rc = (sqlite3_changes (db) > 0) ? SQLITE_OK : SQLITE_NOTFOUND;

  sqlite3_finalize (st);
  return rc;
}

int
db_commodity_get_price (const char *commodity_code, int *out_price)
{
  if (!commodity_code || !out_price)
    return SQLITE_MISUSE;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_MISUSE;

  static const char *cols[] = { "last_price", "price", "base_price", NULL };

  for (int i = 0; cols[i]; i++)
    {
      int rc =
	select_price_with_column (db, cols[i], commodity_code, out_price);
      if (rc == SQLITE_OK || rc == SQLITE_NOTFOUND)
	return rc;
      /* SQLITE_ERROR / "no such column" → try next candidate. */
    }

  return SQLITE_ERROR;
}

int
db_commodity_update_price (const char *commodity_code, int new_price)
{
  if (!commodity_code)
    return SQLITE_MISUSE;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_MISUSE;

  static const char *cols[] = { "last_price", "price", "base_price", NULL };

  for (int i = 0; cols[i]; i++)
    {
      int rc =
	update_price_with_column (db, cols[i], commodity_code, new_price);
      if (rc == SQLITE_OK || rc == SQLITE_NOTFOUND)
	return rc;
    }

  return SQLITE_ERROR;
}

/* ----------------------------------------------------------------------
 * Commodities: orders & trades
 * ---------------------------------------------------------------------- */

int
db_commodity_create_order (const char *actor_type, int actor_id,
			   const char *commodity_code, const char *side,
			   int quantity, int price)
{
  if (!actor_type || !commodity_code || !side)
    return SQLITE_MISUSE;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_MISUSE;

  /* Minimal insert – relies on defaults for status, timestamps, etc.
   * May need extending once the full commodity_orders schema is final.
   */
  const char *sql =
    "INSERT INTO commodity_orders "
    "  (commodity_id, actor_type, actor_id, side, quantity, price) "
    "SELECT c.id, ?2, ?3, ?4, ?5, ?6 "
    "FROM commodities c " "WHERE c.code = ?1";

  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_text (st, 1, commodity_code, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 2, actor_type, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (st, 3, actor_id);
  sqlite3_bind_text (st, 4, side, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (st, 5, quantity);
  sqlite3_bind_int (st, 6, price);

  rc = sqlite3_step (st);
  if (rc == SQLITE_DONE)
    rc = (sqlite3_changes (db) > 0) ? SQLITE_OK : SQLITE_NOTFOUND;

  sqlite3_finalize (st);
  return rc;
}

int
db_commodity_fill_order (int order_id, int quantity)
{
  if (order_id <= 0 || quantity <= 0)
    return SQLITE_MISUSE;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_MISUSE;

  /* Fetch current quantity & status */
  const char *sql_sel =
    "SELECT quantity, status " "FROM commodity_orders " "WHERE id = ?1";

  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql_sel, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_int (st, 1, order_id);

  rc = sqlite3_step (st);
  if (rc != SQLITE_ROW)
    {
      sqlite3_finalize (st);
      return (rc == SQLITE_DONE) ? SQLITE_NOTFOUND : rc;
    }

  int current_qty = sqlite3_column_int (st, 0);
  const unsigned char *status_text = sqlite3_column_text (st, 1);

  /* Optional: enforce only 'open' orders */
  if (status_text && strcmp ((const char *) status_text, "open") != 0)
    {
      sqlite3_finalize (st);
      return SQLITE_ERROR;
    }

  sqlite3_finalize (st);

  int remaining = current_qty - quantity;
  if (remaining < 0)
    remaining = 0;

  const char *new_status = (remaining == 0) ? "filled" : "open";

  const char *sql_upd =
    "UPDATE commodity_orders "
    "SET quantity = ?1, status = ?2 " "WHERE id = ?3";

  sqlite3_stmt *st2 = NULL;
  rc = sqlite3_prepare_v2 (db, sql_upd, -1, &st2, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_int (st2, 1, remaining);
  sqlite3_bind_text (st2, 2, new_status, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (st2, 3, order_id);

  rc = sqlite3_step (st2);
  if (rc == SQLITE_DONE)
    rc = SQLITE_OK;

  sqlite3_finalize (st2);
  return rc;
}

int
db_commodity_get_orders (const char *commodity_code, const char *status,
			 json_t **out_array)
{
  if (!commodity_code)
    return SQLITE_MISUSE;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_MISUSE;

  const char *sql_with_status =
    "SELECT o.* "
    "FROM commodity_orders o "
    "JOIN commodities c ON o.commodity_id = c.id "
    "WHERE c.code = ?1 AND o.status = ?2 " "ORDER BY o.id ASC";

  const char *sql_no_status =
    "SELECT o.* "
    "FROM commodity_orders o "
    "JOIN commodities c ON o.commodity_id = c.id "
    "WHERE c.code = ?1 " "ORDER BY o.id ASC";

  sqlite3_stmt *st = NULL;
  int rc;

  if (status)
    {
      rc = sqlite3_prepare_v2 (db, sql_with_status, -1, &st, NULL);
      if (rc != SQLITE_OK)
	return rc;

      sqlite3_bind_text (st, 1, commodity_code, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text (st, 2, status, -1, SQLITE_TRANSIENT);
    }
  else
    {
      rc = sqlite3_prepare_v2 (db, sql_no_status, -1, &st, NULL);
      if (rc != SQLITE_OK)
	return rc;

      sqlite3_bind_text (st, 1, commodity_code, -1, SQLITE_TRANSIENT);
    }

  json_t *arr = NULL;
  rc = stmt_to_json_array (st, out_array ? &arr : NULL);
  sqlite3_finalize (st);

  if (rc == SQLITE_OK && out_array)
    *out_array = arr;
  else if (arr)
    json_decref (arr);

  return rc;
}

int
db_commodity_get_trades (const char *commodity_code, int limit,
			 json_t **out_array)
{
  if (!commodity_code)
    return SQLITE_MISUSE;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_MISUSE;

  if (limit <= 0)
    limit = 100;

  const char *sql =
    "SELECT t.* "
    "FROM commodity_trades t "
    "JOIN commodities c ON t.commodity_id = c.id "
    "WHERE c.code = ?1 " "ORDER BY t.id DESC " "LIMIT ?2";

  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_text (st, 1, commodity_code, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (st, 2, limit);

  json_t *arr = NULL;
  rc = stmt_to_json_array (st, out_array ? &arr : NULL);
  sqlite3_finalize (st);

  if (rc == SQLITE_OK && out_array)
    *out_array = arr;
  else if (arr)
    json_decref (arr);

  return rc;
}

/* ----------------------------------------------------------------------
 * Ports: goods on hand
 *
 * NOTE: The provided schema excerpt shows planet_goods but does not
 * show a dedicated port_goods table. Without the exact port stock
 * schema this would be guesswork, so these remain explicit TODOs.
 * ---------------------------------------------------------------------- */

int
db_port_get_goods_on_hand (int port_id, const char *commodity_code,
			   int *out_quantity)
{
  (void) port_id;
  (void) commodity_code;
  if (out_quantity)
    *out_quantity = 0;

  /* TODO: implement once the port stock schema (port_goods / port_trade) is final. */
  return SQLITE_OK;
}

int
db_port_update_goods_on_hand (int port_id, const char *commodity_code,
			      int quantity_change)
{
  (void) port_id;
  (void) commodity_code;
  (void) quantity_change;

  /* TODO: implement once the port stock schema (port_goods / port_trade) is final. */
  return SQLITE_OK;
}

/* ----------------------------------------------------------------------
 * Planets: goods on hand (planet_goods)
 * ---------------------------------------------------------------------- */

int
db_planet_get_goods_on_hand (int planet_id, const char *commodity_code,
			     int *out_quantity)
{
  if (!commodity_code || !out_quantity)
    return SQLITE_MISUSE;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_MISUSE;

  const char *sql =
    "SELECT quantity "
    "FROM planet_goods " "WHERE planet_id = ?1 AND commodity = ?2";

  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_int (st, 1, planet_id);
  sqlite3_bind_text (st, 2, commodity_code, -1, SQLITE_TRANSIENT);

  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      *out_quantity = sqlite3_column_int (st, 0);
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      *out_quantity = 0;
      rc = SQLITE_OK;
    }

  sqlite3_finalize (st);
  return rc;
}

int
db_planet_update_goods_on_hand (int planet_id, const char *commodity_code,
				int quantity_change)
{
  if (!commodity_code)
    return SQLITE_MISUSE;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_MISUSE;

  int current = 0;
  int rc = db_planet_get_goods_on_hand (planet_id, commodity_code, &current);
  if (rc != SQLITE_OK)
    return rc;

  int new_qty = current + quantity_change;
  if (new_qty < 0)
    new_qty = 0;

  /* Try update first */
  const char *sql_upd =
    "UPDATE planet_goods "
    "SET quantity = ?1 " "WHERE planet_id = ?2 AND commodity = ?3";

  sqlite3_stmt *st = NULL;
  rc = sqlite3_prepare_v2 (db, sql_upd, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_int (st, 1, new_qty);
  sqlite3_bind_int (st, 2, planet_id);
  sqlite3_bind_text (st, 3, commodity_code, -1, SQLITE_TRANSIENT);

  rc = sqlite3_step (st);
  if (rc == SQLITE_DONE && sqlite3_changes (db) > 0)
    {
      sqlite3_finalize (st);
      return SQLITE_OK;
    }

  sqlite3_finalize (st);

  /* No row existed; insert if quantity > 0 */
  if (new_qty == 0)
    return SQLITE_OK;

  const char *sql_ins =
    "INSERT INTO planet_goods (planet_id, commodity, quantity) "
    "VALUES (?1, ?2, ?3)";

  rc = sqlite3_prepare_v2 (db, sql_ins, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_int (st, 1, planet_id);
  sqlite3_bind_text (st, 2, commodity_code, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (st, 3, new_qty);

  rc = sqlite3_step (st);
  if (rc == SQLITE_DONE)
    rc = SQLITE_OK;

  sqlite3_finalize (st);
  return rc;
}




// New helper functions for ship destruction and player status

// db_mark_ship_destroyed: Marks a ship as destroyed.
int
db_mark_ship_destroyed (sqlite3 *db, int ship_id)
{
  sqlite3_stmt *st = NULL;
  int rc;
  const char *sql = "UPDATE ships SET destroyed = 1 WHERE id = ?;";

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_mark_ship_destroyed: prepare error: %s", sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, ship_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);

  if (rc != SQLITE_DONE)
    {
      LOGE ("db_mark_ship_destroyed: execute error: %s", sqlite3_errmsg (db));
      return rc;
    }
  return SQLITE_OK;
}

// db_clear_player_active_ship: Clears the active ship for a player.
int
db_clear_player_active_ship (sqlite3 *db, int player_id)
{
  sqlite3_stmt *st = NULL;
  int rc;
  const char *sql = "UPDATE players SET ship = 0 WHERE id = ?;";	// Set ship to 0 (NULL)

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_clear_player_active_ship: prepare error: %s",
	    sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, player_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);

  if (rc != SQLITE_DONE)
    {
      LOGE ("db_clear_player_active_ship: execute error: %s",
	    sqlite3_errmsg (db));
      return rc;
    }
  return SQLITE_OK;
}

// db_increment_player_stat: Increments a specified player statistic.
int
db_increment_player_stat (sqlite3 *db, int player_id, const char *stat_name)
{
  sqlite3_stmt *st = NULL;
  int rc;
  char *sql_query = NULL;

  // Using sqlite3_mprintf to safely construct the query with a dynamic column name
  sql_query =
    sqlite3_mprintf ("UPDATE players SET %q = %q + 1 WHERE id = ?;",
		     stat_name, stat_name);
  if (!sql_query)
    {
      return SQLITE_NOMEM;
    }

  rc = sqlite3_prepare_v2 (db, sql_query, -1, &st, NULL);
  sqlite3_free (sql_query);	// Free the allocated string immediately after preparing
  if (rc != SQLITE_OK)
    {
      LOGE ("db_increment_player_stat: prepare error for %s: %s", stat_name,
	    sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, player_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);

  if (rc != SQLITE_DONE)
    {
      LOGE ("db_increment_player_stat: execute error for %s: %s", stat_name,
	    sqlite3_errmsg (db));
      return rc;
    }
  return SQLITE_OK;
}

// db_get_player_xp: Retrieves a player's experience points.
int
db_get_player_xp (sqlite3 *db, int player_id)
{
  sqlite3_stmt *st = NULL;
  int rc;
  int xp = 0;
  const char *sql = "SELECT experience FROM players WHERE id = ?;";

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_get_player_xp: prepare error: %s", sqlite3_errmsg (db));
      return 0;			// Return 0 on error
    }
  sqlite3_bind_int (st, 1, player_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      xp = sqlite3_column_int (st, 0);
    }
  else if (rc != SQLITE_DONE)
    {
      LOGE ("db_get_player_xp: execute error: %s", sqlite3_errmsg (db));
    }
  sqlite3_finalize (st);
  return xp;
}

// db_update_player_xp: Updates a player's experience points.
int
db_update_player_xp (sqlite3 *db, int player_id, int new_xp)
{
  sqlite3_stmt *st = NULL;
  int rc;
  const char *sql = "UPDATE players SET experience = ? WHERE id = ?;";

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_update_player_xp: prepare error: %s", sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, new_xp);
  sqlite3_bind_int (st, 2, player_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);

  if (rc != SQLITE_DONE)
    {
      LOGE ("db_update_player_xp: execute error: %s", sqlite3_errmsg (db));
      return rc;
    }
  return SQLITE_OK;
}

// db_shiptype_has_escape_pod: Checks if a ship type has an escape pod.
bool
db_shiptype_has_escape_pod (sqlite3 *db, int ship_id)
{
  sqlite3_stmt *st = NULL;
  int rc;
  int type_id = -1;
  const char *sql = "SELECT type_id FROM ships WHERE id = ?;";

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_shiptype_has_escape_pod: prepare error: %s",
	    sqlite3_errmsg (db));
      return false;		// Safest default
    }
  sqlite3_bind_int (st, 1, ship_id);

  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      type_id = sqlite3_column_int (st, 0);
    }
  else if (rc != SQLITE_DONE)
    {
      LOGE ("db_shiptype_has_escape_pod: execute error: %s",
	    sqlite3_errmsg (db));
    }
  sqlite3_finalize (st);

  // Per game rules, only the Escape Pod (shiptype_id 0) itself lacks an escape pod.
  // If we couldn't find the ship or its type, we default to false for safety.
  if (type_id == -1)
    {
      return false;
    }

  return (type_id != 0);
}

// db_get_player_podded_count_today: Retrieves player's podded count for today.
int
db_get_player_podded_count_today (sqlite3 *db, int player_id)
{
  sqlite3_stmt *st = NULL;
  int rc;
  int count = 0;
  const char *sql =
    "SELECT podded_count_today FROM podded_status WHERE player_id = ?;";

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_get_player_podded_count_today: prepare error: %s",
	    sqlite3_errmsg (db));
      return 0;
    }
  sqlite3_bind_int (st, 1, player_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      count = sqlite3_column_int (st, 0);
    }
  else if (rc == SQLITE_DONE)
    {
      // No entry, create one
      db_create_podded_status_entry (db, player_id);	// This will insert a default row
      // After creation, count is 0
    }
  else
    {
      LOGE ("db_get_player_podded_count_today: execute error: %s",
	    sqlite3_errmsg (db));
    }
  sqlite3_finalize (st);
  return count;
}

// db_get_player_podded_last_reset: Retrieves player's last podded reset timestamp.
long long
db_get_player_podded_last_reset (sqlite3 *db, int player_id)
{
  sqlite3_stmt *st = NULL;
  int rc;
  long long timestamp = 0;
  const char *sql =
    "SELECT podded_last_reset FROM podded_status WHERE player_id = ?;";

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_get_player_podded_last_reset: prepare error: %s",
	    sqlite3_errmsg (db));
      return 0;
    }
  sqlite3_bind_int (st, 1, player_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      timestamp = sqlite3_column_int64 (st, 0);
    }
  else if (rc == SQLITE_DONE)
    {
      // No entry, create one
      db_create_podded_status_entry (db, player_id);	// This will insert a default row
      // After creation, current timestamp should be returned as default
      timestamp = time (NULL);
    }
  else
    {
      LOGE ("db_get_player_podded_last_reset: execute error: %s",
	    sqlite3_errmsg (db));
    }
  sqlite3_finalize (st);
  return timestamp;
}

// db_reset_player_podded_count: Resets player's podded count and updates last reset timestamp.
int
db_reset_player_podded_count (sqlite3 *db, int player_id, long long timestamp)
{
  sqlite3_stmt *st = NULL;
  int rc;
  const char *sql =
    "UPDATE podded_status SET podded_count_today = 0, podded_last_reset = ? WHERE player_id = ?;";

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_reset_player_podded_count: prepare error: %s",
	    sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int64 (st, 1, timestamp);
  sqlite3_bind_int (st, 2, player_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);

  if (rc != SQLITE_DONE)
    {
      LOGE ("db_reset_player_podded_count: execute error: %s",
	    sqlite3_errmsg (db));
      return rc;
    }
  return SQLITE_OK;
}

// db_update_player_podded_status: Updates a player's podded status.
int
db_update_player_podded_status (sqlite3 *db, int player_id,
				const char *status, long long big_sleep_until)
{
  sqlite3_stmt *st = NULL;
  int rc;
  const char *sql =
    "UPDATE podded_status SET status = ?, big_sleep_until = ? WHERE player_id = ?;";

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_update_player_podded_status: prepare error: %s",
	    sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_text (st, 1, status, -1, SQLITE_STATIC);
  sqlite3_bind_int64 (st, 2, big_sleep_until);
  sqlite3_bind_int (st, 3, player_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);

  if (rc != SQLITE_DONE)
    {
      LOGE ("db_update_player_podded_status: execute error: %s",
	    sqlite3_errmsg (db));
      return rc;
    }
  return SQLITE_OK;
}

// db_create_podded_status_entry: Creates a default entry in podded_status for a player.
int
db_create_podded_status_entry (sqlite3 *db, int player_id)
{
  sqlite3_stmt *st = NULL;
  int rc;
  const char *sql =
    "INSERT OR IGNORE INTO podded_status (player_id, podded_count_today, podded_last_reset, status, big_sleep_until) VALUES (?, 0, ?, 'alive', 0);";

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_create_podded_status_entry: prepare error: %s",
	    sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, player_id);
  sqlite3_bind_int64 (st, 2, time (NULL));
  rc = sqlite3_step (st);
  sqlite3_finalize (st);

  if (rc != SQLITE_DONE)
    {
      LOGE ("db_create_podded_status_entry: execute error: %s",
	    sqlite3_errmsg (db));
      return rc;
    }
  return SQLITE_OK;
}

// db_get_shiptype_info: Retrieves holds, fighters, and shields for a shiptype.
int
db_get_shiptype_info (sqlite3 *db, int shiptype_id, int *holds, int *fighters,
		      int *shields)
{
  sqlite3_stmt *st = NULL;
  int rc;
  const char *sql =
    "SELECT initialholds, maxfighters, maxshields FROM shiptypes WHERE id = ?;";

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_get_shiptype_info: prepare error: %s", sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, shiptype_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      *holds = sqlite3_column_int (st, 0);
      *fighters = sqlite3_column_int (st, 1);
      *shields = sqlite3_column_int (st, 2);
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      LOGW ("db_get_shiptype_info: No shiptype found for ID %d.",
	    shiptype_id);
      rc = SQLITE_NOTFOUND;
    }
  else
    {
      LOGE ("db_get_shiptype_info: execute error: %s", sqlite3_errmsg (db));
    }
  sqlite3_finalize (st);
  return rc;
}

int
db_player_land_on_planet (int player_id, int planet_id)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_ERROR;

  pthread_mutex_lock (&db_mutex);

  sqlite3_stmt *st_find_ship = NULL;
  sqlite3_stmt *st_update_ship = NULL;
  sqlite3_stmt *st_update_player = NULL;
  int ship_id = -1;
  int rc = SQLITE_ERROR;

  const char *sql_find_ship = "SELECT ship FROM players WHERE id = ?;";
  if (sqlite3_prepare_v2 (db, sql_find_ship, -1, &st_find_ship, NULL) !=
      SQLITE_OK)
    {
      goto cleanup_land;
    }
  sqlite3_bind_int (st_find_ship, 1, player_id);
  if (sqlite3_step (st_find_ship) == SQLITE_ROW)
    {
      ship_id = sqlite3_column_int (st_find_ship, 0);
    }
  sqlite3_finalize (st_find_ship);
  st_find_ship = NULL;

  if (ship_id == -1)
    {
      goto cleanup_land;
    }

  // Use SAVEPOINT for nested transaction
  sqlite3_exec (db, "SAVEPOINT land_on_planet;", NULL, NULL, NULL);

  const char *sql_update_ship =
    "UPDATE ships SET onplanet = ?, sector = NULL, ported = 0 WHERE id = ?;";
  if (sqlite3_prepare_v2 (db, sql_update_ship, -1, &st_update_ship, NULL) !=
      SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK TO land_on_planet;", NULL, NULL, NULL);
      goto cleanup_land;
    }
  sqlite3_bind_int (st_update_ship, 1, planet_id);
  sqlite3_bind_int (st_update_ship, 2, ship_id);
  if (sqlite3_step (st_update_ship) != SQLITE_DONE)
    {
      sqlite3_exec (db, "ROLLBACK TO land_on_planet;", NULL, NULL, NULL);
      goto cleanup_land;
    }

  const char *sql_update_player =
    "UPDATE players SET lastplanet = ?, sector = NULL WHERE id = ?;";
  if (sqlite3_prepare_v2 (db, sql_update_player, -1, &st_update_player, NULL)
      != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK TO land_on_planet;", NULL, NULL, NULL);
      goto cleanup_land;
    }
  sqlite3_bind_int (st_update_player, 1, planet_id);
  sqlite3_bind_int (st_update_player, 2, player_id);
  if (sqlite3_step (st_update_player) != SQLITE_DONE)
    {
      sqlite3_exec (db, "ROLLBACK TO land_on_planet;", NULL, NULL, NULL);
      goto cleanup_land;
    }

  sqlite3_exec (db, "RELEASE SAVEPOINT land_on_planet;", NULL, NULL, NULL);
  rc = SQLITE_OK;

cleanup_land:
  if (st_find_ship)
    sqlite3_finalize (st_find_ship);
  if (st_update_ship)
    sqlite3_finalize (st_update_ship);
  if (st_update_player)
    sqlite3_finalize (st_update_player);
  pthread_mutex_unlock (&db_mutex);
  return rc;
}

int
db_player_launch_from_planet (int player_id, int *out_sector_id)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_ERROR;

  pthread_mutex_lock (&db_mutex);

  sqlite3_stmt *st = NULL;
  int ship_id = -1;
  int last_planet_id = -1;
  int planet_sector_id = -1;
  int rc = SQLITE_ERROR;

  const char *sql_get_info =
    "SELECT ship, lastplanet FROM players WHERE id = ?;";
  if (sqlite3_prepare_v2 (db, sql_get_info, -1, &st, NULL) != SQLITE_OK)
    {
      goto cleanup_launch;
    }
  sqlite3_bind_int (st, 1, player_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      ship_id = sqlite3_column_int (st, 0);
      last_planet_id = sqlite3_column_int (st, 1);
    }
  sqlite3_finalize (st);
  st = NULL;

  if (ship_id == -1 || last_planet_id <= 0)
    {
      // Player is not on a planet or has no ship
      rc = SQLITE_MISUSE;
      goto cleanup_launch;
    }

  const char *sql_get_planet_sector =
    "SELECT sector FROM planets WHERE id = ?;";
  if (sqlite3_prepare_v2 (db, sql_get_planet_sector, -1, &st, NULL) !=
      SQLITE_OK)
    {
      goto cleanup_launch;
    }
  sqlite3_bind_int (st, 1, last_planet_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      planet_sector_id = sqlite3_column_int (st, 0);
    }
  sqlite3_finalize (st);
  st = NULL;

  if (planet_sector_id <= 0)
    {
      // Planet has no sector, shouldn't happen
      rc = SQLITE_ERROR;
      goto cleanup_launch;
    }

  // Use SAVEPOINT for nested transaction
  sqlite3_exec (db, "SAVEPOINT launch_from_planet;", NULL, NULL, NULL);

  const char *sql_update_ship =
    "UPDATE ships SET onplanet = NULL, sector = ? WHERE id = ?;";
  if (sqlite3_prepare_v2 (db, sql_update_ship, -1, &st, NULL) != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK TO launch_from_planet;", NULL, NULL, NULL);
      goto cleanup_launch;
    }
  sqlite3_bind_int (st, 1, planet_sector_id);
  sqlite3_bind_int (st, 2, ship_id);
  if (sqlite3_step (st) != SQLITE_DONE)
    {
      sqlite3_exec (db, "ROLLBACK TO launch_from_planet;", NULL, NULL, NULL);
      goto cleanup_launch;
    }
  sqlite3_finalize (st);
  st = NULL;

  const char *sql_update_player =
    "UPDATE players SET sector = ? WHERE id = ?;";
  if (sqlite3_prepare_v2 (db, sql_update_player, -1, &st, NULL) != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK TO launch_from_planet;", NULL, NULL, NULL);
      goto cleanup_launch;
    }
  sqlite3_bind_int (st, 1, planet_sector_id);
  sqlite3_bind_int (st, 2, player_id);
  if (sqlite3_step (st) != SQLITE_DONE)
    {
      sqlite3_exec (db, "ROLLBACK TO launch_from_planet;", NULL, NULL, NULL);
      goto cleanup_launch;
    }

  sqlite3_exec (db, "RELEASE SAVEPOINT launch_from_planet;", NULL, NULL,
		NULL);
  rc = SQLITE_OK;
  if (out_sector_id)
    {
      *out_sector_id = planet_sector_id;
    }

cleanup_launch:
  if (st)
    sqlite3_finalize (st);
  pthread_mutex_unlock (&db_mutex);
  return rc;
}



// db_bounty_create: Inserts a new bounty record.
int
db_bounty_create (sqlite3 *db, const char *posted_by_type, int posted_by_id,
		  const char *target_type, int target_id, long long reward,
		  const char *description)
{
  sqlite3_stmt *st = NULL;
  int rc;
  const char *sql =
    "INSERT INTO bounties (posted_by_type, posted_by_id, target_type, target_id, reward, status, posted_ts) "
    "VALUES (?, ?, ?, ?, ?, 'open', strftime('%Y-%m-%dT%H:%M:%fZ','now'));";

  // Note: Ignoring description as per schema verification in plan

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_bounty_create: prepare error: %s", sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_text (st, 1, posted_by_type, -1, SQLITE_STATIC);
  sqlite3_bind_int (st, 2, posted_by_id);
  sqlite3_bind_text (st, 3, target_type, -1, SQLITE_STATIC);
  sqlite3_bind_int (st, 4, target_id);
  sqlite3_bind_int64 (st, 5, reward);

  rc = sqlite3_step (st);
  sqlite3_finalize (st);

  if (rc != SQLITE_DONE)
    {
      LOGE ("db_bounty_create: execute error: %s", sqlite3_errmsg (db));
      return rc;
    }
  return SQLITE_OK;
}

// db_player_get_alignment: Retrieves a player's current alignment.
int
db_player_get_alignment (sqlite3 *db, int player_id, int *alignment)
{
  sqlite3_stmt *st = NULL;
  int rc;
  const char *sql = "SELECT alignment FROM players WHERE id = ?;";

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_player_get_alignment: prepare error: %s",
	    sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, player_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      *alignment = sqlite3_column_int (st, 0);
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      rc = SQLITE_NOTFOUND;
    }
  else
    {
      LOGE ("db_player_get_alignment: execute error: %s",
	    sqlite3_errmsg (db));
    }
  sqlite3_finalize (st);
  return rc;
}

/* Return 1 if path exists from `from` to `to`, 0 if no path, <0 on error. */
int
db_path_exists (sqlite3 *db, int from, int to)
{
  int max_id = 0;
  sqlite3_stmt *st = NULL;
  int rc;

  /* Get max id once; in practice you can cache this in caller. */
  rc = sqlite3_prepare_v2 (db, "SELECT MAX(id) FROM sectors", -1, &st, NULL);
  if (rc != SQLITE_OK)
    return -1;
  if (sqlite3_step (st) == SQLITE_ROW)
    max_id = sqlite3_column_int (st, 0);
  sqlite3_finalize (st);

  if (max_id <= 0 || from <= 0 || from > max_id || to <= 0 || to > max_id)
    return 0;			/* treat as “no path” */

  size_t N = (size_t) max_id + 1;
  unsigned char *seen = calloc (N, 1);
  int *queue = malloc (N * sizeof (int));
  if (!seen || !queue)
    {
      free (seen);
      free (queue);
      return -1;
    }

  /* Prepare neighbour query */
  rc = sqlite3_prepare_v2 (db,
			   "SELECT to_sector FROM sector_warps WHERE from_sector = ?1",
			   -1, &st, NULL);
  if (rc != SQLITE_OK || !st)
    {
      free (seen);
      free (queue);
      return -1;
    }

  int qh = 0, qt = 0;
  queue[qt++] = from;
  seen[from] = 1;
  int found = 0;

  while (qh < qt && !found)
    {
      int u = queue[qh++];

      sqlite3_reset (st);
      sqlite3_clear_bindings (st);
      sqlite3_bind_int (st, 1, u);

      while ((rc = sqlite3_step (st)) == SQLITE_ROW)
	{
	  int v = sqlite3_column_int (st, 0);
	  if (v <= 0 || v > max_id)
	    continue;
	  if (seen[v])
	    continue;
	  seen[v] = 1;
	  if (v == to)
	    {
	      found = 1;
	      break;
	    }
	  queue[qt++] = v;
	}
    }

  sqlite3_finalize (st);
  free (seen);
  free (queue);

  return found;
}

// Implementation of db_get_config_int (thread-safe wrapper)
int
db_get_config_int (sqlite3 *db, const char *key_col_name, int default_value)
{
  pthread_mutex_lock (&db_mutex);
  long long value =
    h_get_config_int_unlocked (db, key_col_name, (long long) default_value);
  pthread_mutex_unlock (&db_mutex);
  return (int) value;
}

// Implementation of db_get_config_bool (thread-safe wrapper)
bool
db_get_config_bool (sqlite3 *db, const char *key_col_name, bool default_value)
{
  pthread_mutex_lock (&db_mutex);
  long long value =
    h_get_config_int_unlocked (db, key_col_name, (long long) default_value);
  pthread_mutex_unlock (&db_mutex);
  return (bool) value;
}
