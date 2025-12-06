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
#include "common.h"
#include "server_config.h"
#include "database.h"
#include "database_cmd.h"
#include "server_log.h"
#include "server_cron.h"
/* * CHANGE 1: Replace the global static handle with Thread Local Storage (TLS).
 * "static __thread" ensures every thread has its own pointer initialized to NULL.
 */
// OLD: static sqlite3 *db_handle = NULL;
static __thread sqlite3 *tls_db = NULL;
/* * CHANGE 2: Keep the mutex definition to prevent link errors,
 * but we will stop initializing or using it.
 */
pthread_mutex_t db_mutex;
// Helper flag to ensure initialization runs only once
//static sqlite3 *db_handle = NULL;
extern sqlite3 *g_db;
/* Unlocked helpers (call only when db_mutex is already held) */
static int db_ensure_auth_schema_unlocked (void);
static int db_ensure_idempotency_schema_unlocked (void);
static int db_create_tables_unlocked (bool schema_exists);
static int db_insert_defaults_unlocked (void);
static int db_seed_ai_qa_bot_bank_account_unlocked (void);
static int db_check_legacy_schema (sqlite3 *db);
int db_get_int_config (sqlite3 *db, const char *key, int *out);
int db_seed_cron_tasks (sqlite3 *db);
void db_handle_close_and_reset (void);
/* src/database.c */
/* src/database.c */
void
db_safe_rollback (sqlite3 *db, const char *context_name)
{
  if (!db)
    {
      return;
    }
  char *errmsg = NULL;
  int rc = sqlite3_exec (db, "ROLLBACK;", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    {
      /* Issue 142: Log rollback failures explicitly */
      LOGE ("FATAL [%s]: Rollback failed (rc=%d): %s",
            context_name ? context_name : "unknown",
            rc,
            errmsg ? errmsg : "No error message");
      sqlite3_free (errmsg);
    }
  else
    {
      // Optional: Debug log for successful rollback if you want high verbosity
      // LOGD("[%s]: Rollback successful.", context_name);
    }
}


static void
db_configure_connection (sqlite3 *db)
{
  char *err_msg = NULL;
  /* Set Busy Timeout to 5000ms to handle write contention */
  sqlite3_busy_timeout (db, 5000);
  /* Enable Write-Ahead Logging (WAL) for concurrency */
  int rc = sqlite3_exec (db, "PRAGMA journal_mode=WAL;", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK)
    {
      // fprintf(stderr, "DB Error setting WAL mode: %s\n", err_msg);
      sqlite3_free (err_msg);
    }
  /* * FIX: DISABLE FOREIGN KEYS.
   * The legacy schema relies on "Sector 0" (Void) and potentially out-of-order
   * inserts. Enforcing keys causes "constraint failed" errors during init.
   */
  // sqlite3_exec(db, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL);
  sqlite3_exec (db, "PRAGMA foreign_keys=OFF;", NULL, NULL, NULL);
}


void
db_handle_close_and_reset (void)
{
  /* // Only proceed if the handle is open */
  /* if (db_handle != NULL) */
  /*   { */
  /*     sqlite3_close (db_handle); */
  /*     db_handle = NULL; */
  /*   } */
  /* * Previously closed the global handle.
   * Now, it closes the handle for the calling thread (usually the parent before fork,
   * or the child immediately after fork).
   */
  db_close_thread ();
}


// New helpers to manage global db_mutex externally
void
db_mutex_lock (void)
{
  /* NO-OP: Global locking is disabled in favor of per-thread connections. */
  //db_mutex_lock();
}


void
db_mutex_unlock (void)
{
  /* NO-OP: Global locking is disabled in favor of per-thread connections. */
  // db_mutex_unlock();
}


sqlite3 *
db_get_handle (void)
{
  /* If this thread already has a connection, return it */
  if (tls_db != NULL)
    {
      return tls_db;
    }
  /* * Open a new connection for this thread.
   * flags:
   * - READWRITE | CREATE: Standard file modes.
   * - NOMUTEX: We are guaranteeing single-thread access, so SQLite
   * doesn't need internal mutexes.
   * - URI: Allows using URI filenames if config requires it.
   */
  int rc = sqlite3_open_v2 (DEFAULT_DB_NAME, &tls_db,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                            SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_URI,
                            NULL);
  if (rc != SQLITE_OK)
    {
      // fprintf(stderr, "FATAL: Cannot open database for thread: %s\n", sqlite3_errmsg(tls_db));
      /* In a real server, you might handle this more gracefully, but for now: */
      if (tls_db)
        {
          sqlite3_close (tls_db);
        }
      tls_db = NULL;
      return NULL;
    }
  /* Configure PRAGMAs and timeouts */
  db_configure_connection (tls_db);
  return tls_db;
}


/* src/database.c */
void
db_close_thread (void)
{
  if (tls_db != NULL)
    {
      LOGI ("DEBUG: Closing thread-local DB connection.");  // Add this!
      sqlite3_close (tls_db);
      tls_db = NULL;
    }
}


int
db_commands_accept (const char *cmd_type,
                    const char *idem_key,
                    json_t *payload,
                    int *out_cmd_id, int *out_duplicate, int *out_due_at)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      /* handle fatal error: log + return error code */
    }
  if (!cmd_type || !idem_key || !payload)
    {
      return -1;
    }
  // Ensure idempotency index exists once (no-op if already there)
  sqlite3_exec (db,
                "CREATE UNIQUE INDEX IF NOT EXISTS idx_engine_cmds_idem "
                "ON engine_commands(idem_key);", NULL, NULL, NULL);
  const char *SQL_INS =
    "INSERT INTO engine_commands("
    "  type, payload, status, priority, attempts, created_at, due_at, idem_key"
    ") VALUES ("
    "  ?,    json(?), 'ready', 100,      0,        strftime('%s','now'), strftime('%s','now'), ?"
    ");";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, SQL_INS, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      return -2;
    }
  char *payload_str = json_dumps (payload, JSON_COMPACT);
  sqlite3_bind_text (st, 1, cmd_type, -1, SQLITE_STATIC);
  sqlite3_bind_text (st, 2, payload_str, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 3, idem_key, -1, SQLITE_STATIC);
  int dup = 0, cmd_id = 0, due_at = 0;
  rc = sqlite3_step (st);
  if (rc == SQLITE_DONE)
    {
      cmd_id = (int) sqlite3_last_insert_rowid (db);
      dup = 0;
    }
  else if (rc == SQLITE_CONSTRAINT)
    {
      // Duplicate: fetch existing id + due_at
      const char *SQL_GET =
        "SELECT id, COALESCE(due_at, strftime('%s','now')) "
        "FROM engine_commands WHERE idem_key = ?;";
      sqlite3_stmt *gt = NULL;
      sqlite3_prepare_v2 (db, SQL_GET, -1, &gt, NULL);
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
      sqlite3_prepare_v2 (db, SQL_DUE, -1, &sd, NULL);
      sqlite3_bind_int (sd, 1, cmd_id);
      if (sqlite3_step (sd) == SQLITE_ROW)
        {
          due_at = sqlite3_column_int (sd, 0);
        }
      sqlite3_finalize (sd);
    }
  if (out_cmd_id)
    {
      *out_cmd_id = cmd_id;
    }
  if (out_duplicate)
    {
      *out_duplicate = dup;
    }
  if (out_due_at)
    {
      *out_due_at = due_at;
    }
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
  db_mutex_lock ();
  // Build the SQL query string
  const char *template = " UPDATE %s SET %s = ? WHERE id = ?; ";
  sql = sqlite3_mprintf (template, table, column);
  if (!sql)
    {
      db_mutex_unlock ();
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
  db_mutex_unlock ();
  return rc;
}


/**
 * @brief Creates a default bank account for a player.
 * @param db The SQLite database connection.
 * @param player_id The ID of the player for whom to create the account.
 * @return 0 on success, -1 on failure.
 */
int db_bank_account_create_default_for_player(sqlite3 *db, int player_id) {
    sqlite3_stmt *stmt;
    int rc;

    // 1. Check if a bank account already exists for this player/currency combination
    const char *check_sql = "SELECT id FROM bank_accounts WHERE owner_type = 'player' AND owner_id = ? AND currency = 'CRD';";
    rc = sqlite3_prepare_v2(db, check_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOGE("Failed to prepare check statement for default bank account: %s", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_int(stmt, 1, player_id);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        // Account already exists, return success
        LOGD("Default bank account for player %d (CRD) already exists.", player_id);
        sqlite3_finalize(stmt);
        return 0;
    }
    sqlite3_finalize(stmt);

    // 2. If account does not exist, proceed with insert
    const char *insert_sql = "INSERT INTO bank_accounts (owner_type, owner_id, currency, balance) VALUES (?, ?, ?, ?);";
    rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOGE("Failed to prepare insert statement for default bank account: %s", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, "player", -1, SQLITE_STATIC); // owner_type
    sqlite3_bind_int(stmt, 2, player_id); // owner_id
    sqlite3_bind_text(stmt, 3, "CRD", -1, SQLITE_STATIC); // currency
    sqlite3_bind_int(stmt, 4, 0); // balance

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        LOGE("Failed to create default bank account for player %d: %s", player_id, sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }

    sqlite3_finalize(stmt);
    LOGD("Created default bank account for player %d", player_id);
    return 0;
}


const char *create_table_sql[] = {
/* Advisory locks */
  "CREATE TABLE IF NOT EXISTS locks ("
  "  lock_name TEXT PRIMARY KEY," "  owner TEXT," "  until_ms INTEGER" ");",
  "CREATE INDEX IF NOT EXISTS idx_locks_until ON locks(until_ms);",
/* Engine KV */
  "CREATE TABLE IF NOT EXISTS engine_state ("
  "  state_key TEXT PRIMARY KEY," "  state_val TEXT NOT NULL" ");",
/* ----------------------- New Key-Value-Type Config ------------------- */
  " CREATE TABLE IF NOT EXISTS config ( "
  "  key TEXT PRIMARY KEY, "
  "  value TEXT NOT NULL, "
  "  type TEXT NOT NULL CHECK (type IN ('int', 'bool', 'string', 'double')) "
  " ); ",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('turnsperday', '120', 'int');",
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
  /* "   FOREIGN KEY(owner_id) REFERENCES players(id) ON DELETE SET NULL ON UPDATE CASCADE,  " */
  "   CHECK (tag IS NULL OR (length(tag) BETWEEN 2 AND 5 AND tag GLOB '[A-Za-z0-9]*')) "
  " ); ",
  " CREATE TABLE IF NOT EXISTS corp_members ( "
  "   corp_id INTEGER NOT NULL,  "
  "   player_id INTEGER NOT NULL,  "
  "   role TEXT NOT NULL DEFAULT 'Member',  "
  "   join_date DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),  "
  "   PRIMARY KEY (corp_id, player_id),  "
  "   FOREIGN KEY(corp_id) REFERENCES corporations(id) ON DELETE CASCADE ON UPDATE CASCADE,  "
  /*  "   FOREIGN KEY(player_id) REFERENCES players(id) ON DELETE CASCADE ON UPDATE CASCADE,  " */
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
  "CREATE TABLE IF NOT EXISTS commision ( "
  " id INTEGER PRIMARY KEY, "
  " is_evil BOOLEAN NOT NULL DEFAULT 0 CHECK (is_evil IN (0,1)), "
  " min_exp INTEGER NOT NULL, "
  " description TEXT NOT NULL ); ",
  " CREATE TABLE IF NOT EXISTS alignment_band   ("
  "   id            INTEGER PRIMARY KEY  ,"
  "   code          TEXT NOT NULL UNIQUE,      "
  "   name          TEXT NOT NULL,             "
  "   min_align     INTEGER NOT NULL  ,"
  "   max_align     INTEGER NOT NULL  ,"
  "   is_good       INTEGER NOT NULL DEFAULT 0 CHECK (is_good IN (0,1))  ,"
  "   is_evil       INTEGER NOT NULL DEFAULT 0 CHECK (is_evil IN (0,1))  ,"
  "   can_buy_iss   INTEGER NOT NULL DEFAULT 0 CHECK (can_buy_iss IN (0,1))  ,"
  "   can_rob_ports INTEGER NOT NULL DEFAULT 0 CHECK (can_rob_ports IN (0,1))  ,"
  "   notes         TEXT"
  " )  ;",
  " INSERT INTO alignment_band (id, code, name, min_align, max_align, is_good, is_evil, can_buy_iss, can_rob_ports  )"
  " VALUES "
  "   (1, 'VERY_GOOD',  'Very Good',    750,  2000, 1, 0, 1, 0)  ,"
  "   (2, 'GOOD',       'Good',         250,   749, 1, 0, 0, 0)  ,"
  "   (3, 'NEUTRAL',    'Neutral',     -249,   249, 0, 0, 0, 0)  ,"
  "   (4, 'SHADY',      'Shady',       -500,  -250, 0, 1, 0, 1)  ,"
  "   (5, 'VERY_EVIL',  'Very Evil',  -1000,  -501, 0, 1, 0, 1)  ,"
  "   (6, 'MONSTROUS',  'Monstrous',  -2000, -1001, 0, 1, 0, 1)  ;",
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
  " CREATE TABLE IF NOT EXISTS corp_invites ( "
  "   id INTEGER PRIMARY KEY AUTOINCREMENT, " "   corp_id INTEGER NOT NULL, "
  "   player_id INTEGER NOT NULL, " "   invited_at INTEGER NOT NULL, "                                                                                                                                  /* unix epoch seconds */
  "   expires_at INTEGER NOT NULL, "    /* unix epoch seconds */
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
  " CREATE TABLE IF NOT EXISTS ports ( "
  " id INTEGER PRIMARY KEY AUTOINCREMENT,  " " number INTEGER, "
  " name TEXT NOT NULL, " " sector INTEGER NOT NULL, "                                                                                                            /* FK to sectors.id */
  " size INTEGER, "
  " techlevel INTEGER, "
  " petty_cash INTEGER NOT NULL DEFAULT 0,  "
  " invisible INTEGER DEFAULT 0,  "
  " type INTEGER DEFAULT 1,  "
  "   economy_curve_id INTEGER NOT NULL DEFAULT 1,  "
  "   FOREIGN KEY (economy_curve_id) REFERENCES economy_curve(id),  "
  "   FOREIGN KEY (sector) REFERENCES sectors(id)); ",
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
  " sector INTEGER DEFAULT 1,  "
  " ship INTEGER,  "
  " experience INTEGER DEFAULT 0,  "
  " alignment INTEGER DEFAULT 0, "
  " commission INTEGER DEFAULT 1,  "
  " credits INTEGER DEFAULT 1500,  "
  " flags INTEGER,  "
  " login_time INTEGER,  "
  " last_update INTEGER,  "
  " intransit INTEGER,  "
  " beginmove INTEGER,  "
  " movingto INTEGER,  "
  " loggedin INTEGER,  "
  " lastplanet INTEGER,  "
  " score INTEGER,  "
  " last_news_read_timestamp INTEGER DEFAULT 0, "
  " FOREIGN KEY (commission) REFERENCES commision(id) "
  " );  ",
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
  "   enabled INTEGER NOT NULL DEFAULT 1, "
  "   FOREIGN KEY (required_commission) REFERENCES commision(id) "
  " );  ",
  " CREATE TABLE IF NOT EXISTS ships (  "
  "   id INTEGER PRIMARY KEY AUTOINCREMENT,  "
  "   name TEXT NOT NULL,  "
  "   type_id INTEGER, /* Foreign Key to shiptypes.id */  "
  "   attack INTEGER,  "
  "   holds INTEGER DEFAULT 1,  "
  "   mines INTEGER, /* Current quantity carried */  "
  "   limpets INTEGER, /* Current quantity carried */  "
  "   fighters INTEGER DEFAULT 1, /* Current quantity carried */  "
  "   genesis INTEGER, /* Current quantity carried */  "
  "   detonators INTEGER NOT NULL DEFAULT 0, "
  "   probes INTEGER NOT NULL DEFAULT 0, "
  "   photons INTEGER, /* Current quantity carried */  "
  "   sector INTEGER, /* Foreign Key to sectors.id */  "
  "   shields INTEGER DEFAULT 1,  "
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
  " CREATE TABLE IF NOT EXISTS planets ( "
  " id INTEGER PRIMARY KEY AUTOINCREMENT,  " " num INTEGER,  "                                          /* legacy planet ID */
  " sector INTEGER NOT NULL,  "         /* FK to sectors.id */
  " name TEXT NOT NULL,  " " owner_id INTEGER NOT NULL, "       /* Generic owner ID */
  " owner_type TEXT NOT NULL DEFAULT 'player', "        /* 'player' or 'corporation' */
  " class TEXT NOT NULL DEFAULT 'M', "          /* Canonical class string */
  " population INTEGER,  " " type INTEGER,  "   /* FK to planettypes.id */
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
  "     WHEN (SELECT COUNT(*) FROM planets) >= (SELECT CAST(value AS INTEGER) FROM config WHERE key = 'max_total_planets') "
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
  " CREATE TABLE IF NOT EXISTS citadels ( "
  " id INTEGER PRIMARY KEY AUTOINCREMENT,  "
  " planet_id INTEGER UNIQUE NOT NULL,  "                                                                                       /* 1:1 link to planets.id */
  " level INTEGER,  " " treasury INTEGER,  " " militaryReactionLevel INTEGER,  "
  " qCannonAtmosphere INTEGER,  " " qCannonSector INTEGER,  "
  " planetaryShields INTEGER,  " " transporterlvl INTEGER,  "
  " interdictor INTEGER,  " " upgradePercent REAL,  " " upgradestart INTEGER,  "
  " owner INTEGER,  "                                                                                                                                                                                                                                                                                           /* FK to players.id */
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
  " CREATE TABLE IF NOT EXISTS sessions ( " "   token       TEXT PRIMARY KEY, " /* 64-hex opaque */
  "   player_id   INTEGER NOT NULL, " "   expires     INTEGER NOT NULL, "       /* epoch seconds (UTC) */
  "   created_at  INTEGER NOT NULL "    /* epoch seconds */
  " ); "
  " CREATE INDEX IF NOT EXISTS idx_sessions_player ON sessions(player_id); "
  " CREATE INDEX IF NOT EXISTS idx_sessions_expires ON sessions(expires); ",
  " CREATE TABLE IF NOT EXISTS turns( "
  "   player INTEGER NOT NULL, "
  "   turns_remaining INTEGER NOT NULL, "
  "   last_update TIMESTAMP NOT NULL, "
  "   PRIMARY KEY (player), "
  "   FOREIGN KEY (player) REFERENCES players(id) ON DELETE CASCADE ); ",
  "CREATE UNIQUE INDEX IF NOT EXISTS idx_turns_player ON turns(player);",
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
  /* Law Enforcement & Robbery */
  "CREATE TABLE IF NOT EXISTS law_enforcement ("
  "    id INTEGER PRIMARY KEY CHECK (id = 1),"
  "    robbery_evil_threshold INTEGER DEFAULT -10,"
  "    robbery_xp_per_hold    INTEGER DEFAULT 20,"
  "    robbery_credits_per_xp INTEGER DEFAULT 10,"
  "    robbery_bust_chance_base REAL DEFAULT 0.05,"
  "    robbery_turn_cost      INTEGER DEFAULT 1,"
  "    good_guy_bust_bonus      REAL DEFAULT 0.10,"
  "    pro_criminal_bust_delta  REAL DEFAULT -0.02,"
  "    evil_cluster_bust_bonus  REAL DEFAULT 0.05,"
  "    good_align_penalty_mult  REAL DEFAULT 3.0,"
  "    robbery_real_bust_ttl_days INTEGER DEFAULT 7"
  ");",
  "CREATE TABLE IF NOT EXISTS port_busts ("
  "    port_id      INTEGER NOT NULL,"
  "    player_id    INTEGER NOT NULL,"
  "    last_bust_at INTEGER NOT NULL,"
  "    bust_type    TEXT    NOT NULL,"
  "    active       INTEGER NOT NULL DEFAULT 1,"
  "    PRIMARY KEY (port_id, player_id),"
  "    FOREIGN KEY (port_id) REFERENCES ports(id),"
  "    FOREIGN KEY (player_id) REFERENCES players(id)"
  ");",
  "CREATE INDEX IF NOT EXISTS idx_port_busts_player ON port_busts (player_id);",
  "CREATE TABLE IF NOT EXISTS player_last_rob ("
  "    player_id    INTEGER PRIMARY KEY,"
  "    port_id      INTEGER NOT NULL,"
  "    last_attempt_at INTEGER NOT NULL,"
  "    was_success  INTEGER NOT NULL"
  ");",
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
  // New table for unified stock and price
  " CREATE TABLE IF NOT EXISTS entity_stock (  "
  "    entity_type      TEXT NOT NULL CHECK(entity_type IN ('port','planet')),  "
  "    entity_id        INTEGER NOT NULL,  "
  "    commodity_code   TEXT NOT NULL,  "
  "    quantity         INTEGER NOT NULL,  "
  "    price            INTEGER NULL,  "
  "    last_updated_ts  INTEGER NOT NULL DEFAULT (strftime('%s','now')),  "
  "    PRIMARY KEY (entity_type, entity_id, commodity_code),  "
  "    FOREIGN KEY (commodity_code) REFERENCES commodities(code)  "
  " );  ",
  // New table for economy configuration curves
  " CREATE TABLE IF NOT EXISTS economy_curve (  "
  "    id                      INTEGER PRIMARY KEY,  "
  "    curve_name              TEXT NOT NULL UNIQUE,  "
  "    base_restock_rate       REAL NOT NULL,  "
  "    price_elasticity        REAL NOT NULL,  "
  "    target_stock            INTEGER NOT NULL,  "
  "    volatility_factor       REAL NOT NULL  "
  " );  ",
  // New table for planet production rates
  " CREATE TABLE IF NOT EXISTS planet_production (  "
  "    planet_type_id      INTEGER NOT NULL,  "
  "    commodity_code      TEXT NOT NULL,  "
  "    base_prod_rate      INTEGER NOT NULL,  "
  "    base_cons_rate      INTEGER NOT NULL,  "
  "    PRIMARY KEY (planet_type_id, commodity_code),  "
  "    FOREIGN KEY (planet_type_id) REFERENCES planettypes(id),  "
  "    FOREIGN KEY (commodity_code) REFERENCES commodities(code)  "
  " );  ",
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
const char *insert_default_sql[] = {
  /* Config defaults - Key-Value-Type Schema */
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('turnsperday', '120', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('maxwarps_per_sector', '6', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('startingcredits', '10000000', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('startingfighters', '10', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('startingholds', '20', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('processinterval', '1', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('autosave', '5', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('max_ports', '200', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('max_planets_per_sector', '6', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('max_total_planets', '300', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('max_citadel_level', '6', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('number_of_planet_types', '8', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('max_ship_name_length', '50', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('ship_type_count', '8', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('hash_length', '128', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('default_nodes', '500', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('buff_size', '1024', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('max_name_length', '50', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('planet_type_count', '8', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('server_port', '1234', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('s2s_port', '4321', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('bank_alert_threshold_player', '1000000', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('bank_alert_threshold_corp', '5000000', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('genesis_enabled', '1', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('genesis_block_at_cap', '0', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('genesis_navhaz_delta', '0', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('genesis_class_weight_M', '10', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('genesis_class_weight_K', '10', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('genesis_class_weight_O', '10', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('genesis_class_weight_L', '10', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('genesis_class_weight_C', '10', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('genesis_class_weight_H', '10', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('genesis_class_weight_U', '5', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('shipyard_enabled', '1', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('shipyard_trade_in_factor_bp', '5000', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('shipyard_require_cargo_fit', '1', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('shipyard_require_fighters_fit', '1', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('shipyard_require_shields_fit', '1', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('shipyard_require_hardware_compat', '1', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('shipyard_tax_bp', '1000', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('illegal_allowed_neutral', '0', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('max_cloak_duration', '24', 'int');",
  "INSERT OR IGNORE INTO config (key, value, type) VALUES ('corporation_creation_fee', '1000', 'int');",
  "INSERT OR IGNORE INTO law_enforcement (id) VALUES (1);",
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
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 0, 'Civilian'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 0, 'Civilian'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 100, 'Cadet'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 100, 'Thug'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 400, 'Ensign'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 400, 'Pirate'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 1000, 'Lieutenant'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 1000, 'Raider'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 2500, 'Lt. Commander'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 2500, 'Marauder'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 5000, 'Commander'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 5000, 'Buccaneer'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 10000, 'Captain'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 10000, 'Corsair'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 20000, 'Commodore'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 20000, 'Terrorist'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 35000, 'Rear Admiral'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 35000, 'Anarchist'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 75000, 'Vice Admiral'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 75000, 'Warlord'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 100000, 'Admiral'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 100000, 'Despot'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 150000, 'Fleet Admiral'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 150000, 'Tyrant'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 200000, 'Grand Admiral'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 200000, 'Warmonger'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 300000, 'Lord Commander'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 300000, 'Dread Pirate'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 400000, 'High Commander'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 400000, 'Cosmic Destroyer'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 550000, 'Star Marshal'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 550000, 'Galactic Menace'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 700000, 'Grand Star Marshal'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 700000, 'Void Reaver'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 1000000, 'Supreme Commander'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 1000000, 'Grim Reaper'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 1500000, 'Galactic Commander'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 1500000, 'Annihilator'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 2000000, 'Galactic Captain'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 3000000, 'Supreme Annihilator'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 4000000, 'Galactic Commodore'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 4000000, 'Chaos Bringer'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (0, 5000000, 'Galactic Admiral'); ",
  "INSERT OR IGNORE INTO commision (is_evil, min_exp, description) values (1, 5000000, 'Death Lord'); ",
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
  " INSERT OR IGNORE INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate) VALUES "
  " ((SELECT id FROM planets WHERE name='Orion Hideout'), 'organics', 100, 100, 0); ",
  /* Near Zero Capacity for Organics */
  " INSERT OR IGNORE INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate) VALUES "
  " ((SELECT id FROM planets WHERE name='Orion Hideout'), 'equipment', 30000000, 30000000, 10); ",
  /* Fedspace sectors 1–10 */
  "INSERT OR IGNORE INTO sectors (id, name, beacon, nebulae) VALUES (1, 'Fedspace 1System Volume', 'The Federation -- Do Not Dump!', 'The Federation');",
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
  /**************************************   HERE  **********************************************************************/
  /* Fixed Earth Port Insert */

  "INSERT OR IGNORE INTO ports (number, name, sector, size, techlevel, invisible, economy_curve_id) "
  " VALUES (1, 'Earth Port', 1, 10, 10, 0, 1); ",
  /* Initial Stock for Earth Port */
  " INSERT OR IGNORE INTO entity_stock (entity_type, entity_id, commodity_code, quantity) VALUES ('port', 1, 'ORE', 10000); ",
  " INSERT OR IGNORE INTO entity_stock (entity_type, entity_id, commodity_code, quantity) VALUES ('port', 1, 'ORG', 10000); ",
  " INSERT OR IGNORE INTO entity_stock (entity_type, entity_id, commodity_code, quantity) VALUES ('port', 1, 'EQU', 10000); ",
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
  /* CHANGE: Added 'commission' column and value '1' to these inserts */
  "INSERT OR IGNORE INTO players (number, name, passwd, sector, ship, type, commission) VALUES (1, 'System', 'BOT',1,1,1,1);",
  "INSERT OR IGNORE INTO players (number, name, passwd, sector, ship, type, commission) VALUES (1, 'Federation Administrator', 'BOT',1,1,1,1);",
  "INSERT OR IGNORE INTO players (number, name, passwd, sector, ship, type, commission) VALUES (7, 'newguy', 'pass123',1,1,2,1);",
  /* ---- AI QA Bot Test Player ---- */
  "INSERT INTO ships (name, type_id, attack, holds, fighters, shields, sector, onplanet, ported) SELECT 'QA Bot 1', T.id, T.maxattack, T.maxholds, T.maxfighters, T.maxshields, 1, 0, 0 FROM shiptypes T WHERE T.name = 'Scout Marauder';",
  "INSERT OR IGNORE INTO players (number, name, passwd, sector, credits, type, commission) VALUES (8, 'ai_qa_bot', 'quality', 1, 10000, 2, 1);",
  "UPDATE players SET ship = (SELECT id FROM ships WHERE name = 'QA Bot 1') WHERE name = 'ai_qa_bot';",
  "INSERT INTO ship_ownership (player_id, ship_id, is_primary, role_id) VALUES ((SELECT id FROM players WHERE name = 'ai_qa_bot'), (SELECT id FROM ships WHERE name = 'QA Bot 1'), 1, 1);",
  /* ----------------------------- */
  "INSERT INTO ship_ownership (player_id, ship_id, is_primary, role_id) VALUES (1,1,1,0);",
  "INSERT INTO player_types (description) VALUES ('NPC');"
  "INSERT INTO player_types (description) VALUES ('Human Player');"
/* ------------------------------------------------------------------------------------- */
/* Insert five maxed-out Orion Syndicate ships (FULL SCHEMA APPLIED) */
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
  "INSERT OR IGNORE INTO players (type, name, passwd, sector, experience, alignment, credits) "
  "SELECT 1, 'Zydras, Heavy Fighter Captain', '', P.sector, 100, -100, 1000 FROM planets P WHERE P.num=3 UNION ALL "
  "SELECT 1, 'Krell, Scout Captain', '', P.sector, 100, -100, 1000 FROM planets P WHERE P.num=3 UNION ALL "
  "SELECT 1, 'Vex, Contraband Captain', '', P.sector, 100, -100, 1000 FROM planets P WHERE P.num=3 UNION ALL "
  "SELECT 1, 'Jaxx, Smuggler Captain', '', P.sector, 100, -100, 1000 FROM planets P WHERE P.num=3 UNION ALL "
  "SELECT 1, 'Sira, Market Guard Captain', '', P.sector, 100, -100, 1000 FROM planets P WHERE P.num=3;"
/* Insert 5 Orion Captains (Players) - Removed non-schema columns (faction_id, empire, turns) */
  "INSERT OR IGNORE INTO players (type, name, passwd, sector, experience, alignment, credits, commission) "
  "SELECT 1, 'Zydras, Heavy Fighter Captain', '', P.sector, 100, -100, 1000, 1 FROM planets P WHERE P.num=3 UNION ALL "
  "SELECT 1, 'Krell, Scout Captain', '', P.sector, 100, -100, 1000, 1 FROM planets P WHERE P.num=3 UNION ALL "
  "SELECT 1, 'Vex, Contraband Captain', '', P.sector, 100, -100, 1000, 1 FROM planets P WHERE P.num=3 UNION ALL "
  "SELECT 1, 'Jaxx, Smuggler Captain', '', P.sector, 100, -100, 1000, 1 FROM planets P WHERE P.num=3 UNION ALL "
  "SELECT 1, 'Sira, Market Guard Captain', '', P.sector, 100, -100, 1000, 1 FROM planets P WHERE P.num=3;"
  "SELECT 1, 'Fer', '', P.sector, 200, -100, 1000, 1 FROM planets P WHERE P.num=2;"
  /* ------------------------------------------------------------------------------------- */
  /*  -- 1. Create the Orion Syndicate Corporation */
  "INSERT INTO corporations (name, owner_id, tag) VALUES ('Orion Syndicate', (SELECT id FROM players WHERE name LIKE 'Zydras%'), 'ORION');",
  "INSERT INTO corporations (name, owner_id, tag) VALUES ('Ferrengi Alliance', (SELECT id FROM players WHERE name LIKE'Fer%'), 'FENG');",
  /* Ferringhi Homeworld (Planet ID 2) Commodities */
  /* -- Ore (High Capacity) */
  " INSERT INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate) VALUES "
  " (2, 'ore', 10000000, 10000000, 0); \n",
  /* -- Organics (Very High Capacity/Focus) */
  " INSERT INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate) VALUES "
  " (2, 'organics', 50000000, 50000000, 10); \n ",
  /* -- Equipment (High Capacity) */
  " INSERT INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate) VALUES "
  " (2, 'equipment', 10000000, 10000000, 0); \n ",
  " INSERT INTO turns (player, turns_remaining, last_update)"
  " SELECT "
  "    id, "
  "    100, "
  "    strftime('%s', 'now') "
  " FROM " "    players " "WHERE " "    type = 2; "

  "INSERT OR IGNORE INTO economy_curve (id, curve_name, base_restock_rate, price_elasticity, target_stock, volatility_factor) VALUES (1, 'default', 0.1, 0.5, 10000, 0.2);"

  ///////////////////////
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
                                   "  last_event_id INTEGER NOT NULL,"
                                   "  last_event_ts INTEGER NOT NULL" ");"
                                   /* engine_events (durable rail) — keep your existing table; create if missing */
                                   "CREATE TABLE IF NOT EXISTS engine_events ("
                                   "  id              INTEGER PRIMARY KEY,"
                                   "  ts              INTEGER NOT NULL,"
                                   "  type            TEXT NOT NULL,"
                                   "  actor_player_id INTEGER,"
                                   "  sector_id       INTEGER,"
                                   "  payload         JSON NOT NULL,"                                                                                                                                                                                           /* JSON is stored as TEXT in SQLite */
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
                                   "  last_event_id INTEGER NOT NULL,"
                                   "  last_event_ts INTEGER NOT NULL" ");"
                                   /* Durable rail backing table */
                                   "CREATE TABLE IF NOT EXISTS engine_events("
                                   "  id              INTEGER PRIMARY KEY,"
                                   "  ts              INTEGER NOT NULL,"
                                   "  type            TEXT NOT NULL,"
                                   "  actor_player_id INTEGER,"
                                   "  sector_id       INTEGER,"
                                   "  payload         JSON NOT NULL,"
                                   "  idem_key        TEXT" ");"
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
                                   "BEGIN "
                                   "  SELECT RAISE(ABORT, 'events is append-only');"
                                   "END;" "COMMIT;";
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
  int rc = 0;                   // Declare rc here
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
    {
      return -1;
    }
  ssize_t rd = read (fd, buf, n);
  close (fd);
  return (rd == (ssize_t) n) ? 0 : -1;
}


static void
to_hex (const unsigned char *in, size_t n, char out_hex[] /*2n+1 */  )
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
    {
      return -1;
    }
  to_hex (rnd, sizeof (rnd), out64);
  return 0;
}


int
db_seed_cron_tasks (sqlite3 *db)
{
  if (!db)
    {
      return SQLITE_MISUSE;
    }
  /* Wrap in a small transaction for atomicity */
  char *err = NULL;
  int rc = sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &err);
  if (rc != SQLITE_OK)
    {
      if (err)
        {
          sqlite3_free (err);
        }
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
    {
      goto done;
    }
  /* Seed: broadcast TTL cleanup runs every 5 minutes.
     Idempotent: only inserts if the name doesn't exist. */
  rc = sqlite3_exec (db,
                     "INSERT INTO cron_tasks(name, schedule, enabled, next_due_at) "
                     "SELECT 'broadcast_ttl_cleanup','every:5m',1,strftime('%s','now') "
                     "WHERE NOT EXISTS (SELECT 1 FROM cron_tasks WHERE name='broadcast_ttl_cleanup');",
                     NULL,
                     NULL,
                     &err);
  if (rc != SQLITE_OK)
    {
      goto done;
    }
  /* Seed: daily bank interest tick runs daily */
  rc = sqlite3_exec (db,
                     "INSERT INTO cron_tasks(name, schedule, enabled, next_due_at) "
                     "SELECT 'daily_bank_interest_tick','daily@00:00Z',1,strftime('%s','now','start of day','+1 day','utc','0 hours') "
                     "WHERE NOT EXISTS (SELECT 1 FROM cron_tasks WHERE name='daily_bank_interest_tick');",
                     NULL,
                     NULL,
                     &err);
  if (rc != SQLITE_OK)
    {
      goto done;
    }
done:
  if (rc == SQLITE_OK)
    {
      char *msg = NULL;
      if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &msg) != SQLITE_OK)
        {
          LOGE ("db_seed_cron_tasks: Commit failed: %s", msg);
          sqlite3_free (msg);
        }
    }
  else
    {
      /* Fix Issue 142: Use safe rollback with logging */
      db_safe_rollback (db, "db_seed_cron_tasks");
    }
  if (err)
    {
      sqlite3_free (err);
    }
  return rc;
}


int
db_init (void)
{
  sqlite3_stmt *stmt = NULL;
  int ret_code = -1;            // Default to error
  int rc;
  // Get the main thread's connection
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      LOGE (
        "FATAL: Could not acquire DB handle for main thread initialization.");
      return -1;
    }
  else
    LOGI ("DB handle aquired");
  // Ensure mandatory schemas
  // Note: These functions call db_get_handle() internally, so they are safe.
  if (db_ensure_auth_schema_unlocked () != SQLITE_OK)
    {
      LOGI ("db_ensure_auth_schema_unlocked: FAIL");
      return -1;
    }
  if (db_ensure_idempotency_schema_unlocked () != SQLITE_OK)
    {
      LOGI ("db_ensure_idempotency_schema_unlocked: FAIL");
      return -1;
    }
  /* Step 2: check if config table exists */
  const char *sql =
    "SELECT name FROM sqlite_master WHERE type='table' AND name='config';";
  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("DB prepare check error: %s", sqlite3_errmsg (db));
      ret_code = -1;
      goto cleanup;
    }
  rc = sqlite3_step (stmt);
  int table_exists = (rc == SQLITE_ROW);
  if (rc != SQLITE_ROW && rc != SQLITE_DONE)
    {
      LOGE ("DB step check error: %s", sqlite3_errmsg (db));
      ret_code = -1;
      goto cleanup;
    }
  sqlite3_finalize (stmt);
  stmt = NULL;
  if (table_exists)
    {
      if (db_check_legacy_schema (db) != 0)
        {
          ret_code = -1;
          goto cleanup;
        }
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
      // Schema exists logic (currently empty/commented out in original)
    }
  if (!db_engine_bootstrap ())
    {
      LOGE ("PROBLEM WITH ENGINE CREATION");
    }
  // Seed cron tasks using the local db handle
  (void) db_seed_cron_tasks (db);
  // If we've made it here, all steps were successful.
  ret_code = 0;
  LOGD ("db_init completed successfully");
cleanup:
  /* Step 4: Finalize the statement if it was successfully prepared. */
  if (stmt)
    {
      sqlite3_finalize (stmt);
    }
  // DO NOT close the handle here. This is the main thread's persistent connection.
  return ret_code;
}


static int
db_check_legacy_schema (sqlite3 *db)
{
  // Check if 'config' table has 'type' column
  const char *sql =
    "SELECT 1 FROM pragma_table_info('config') WHERE name='type';";
  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      return 0;                  // Assume no legacy if check fails
    }
  rc = sqlite3_step (stmt);
  int has_type = (rc == SQLITE_ROW);
  sqlite3_finalize (stmt);
  if (!has_type)
    {
      LOGE (
        "FATAL: Legacy 'config' table detected. Please backup and delete your database file (e.g. %s) to migrate to the new Key-Value-Type schema.",
        DEFAULT_DB_NAME);
      return -1;
    }
  return 0;
}


int
db_get_int_config (sqlite3 *db, const char *key, int *out)
{
  sqlite3_stmt *stmt = NULL;
  int rc;
  int ret_code = -1;
  const char *sql = "SELECT value FROM config WHERE key = ? AND type = 'int';";
  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_text (stmt, 1, key, -1, SQLITE_STATIC);
  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      const char *val_str = (const char *)sqlite3_column_text (stmt, 0);
      if (val_str)
        {
          char *endptr;
          errno = 0;
          long val = strtol (val_str, &endptr, 10);
          if (errno == 0 && endptr != val_str && *endptr == '\0')
            {
              *out = (int)val;
              ret_code = 0;
            }
        }
    }
  sqlite3_finalize (stmt);
  return ret_code;
}


int
db_load_ports (int *server_port, int *s2s_port)
{
  int ret_code = 0;
  if (!server_port || !s2s_port)
    {
      return -1;
    }
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return -1;
    }
  /* Try to load server_port */
  if (db_get_int_config (db, "server_port", server_port) != 0)
    {
      LOGW (
        "[config] 'server_port' missing or invalid in DB, using default: %d",
        *server_port);
    }
  /* Try to load s2s_port */
  if (db_get_int_config (db, "s2s_port", s2s_port) != 0)
    {
      LOGW ("[config] 's2s_port' missing or invalid in DB, using default: %d",
            *s2s_port);
    }
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
          (
            "DB seed ai_qa_bot: Failed to create bank account for 'ai_qa_bot' (rc=%d)",
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
  db_mutex_lock ();
  rc = db_create_tables_unlocked (schema_exists);
  db_mutex_unlock ();
  return rc;
}


/* /\* Actual work; assumes db_mutex is already held *\/ */
/* static int */
/* db_create_tables_unlocked (bool schema_exists) */
/* { */
/*   char *errmsg = NULL; */
/*   int ret_code = -1; */
/*   sqlite3 *db = db_get_handle (); */
/*   if (!db) */
/*     { */
/*       return -1; */
/*     } */
/*   int rc; */
/*   for (size_t i = 0; i < create_table_count; i++) */
/*     { */
/*       const char *sql_statement = create_table_sql[i]; */
/*       // If schema_exists is true, skip CREATE TABLE IF NOT EXISTS statements */
/*       if (schema_exists */
/*           && strstr (sql_statement, */
/*                      "CREATE TABLE IF NOT EXISTS") == sql_statement) */
/*         { */
/*           continue;             // Skip this statement */
/*         } */
/*       rc = sqlite3_exec (db_handle, sql_statement, 0, 0, &errmsg); */
/*       if (rc != SQLITE_OK) */
/*         { */
/*           LOGE ("SQL error at step %zu: %s", i, errmsg); */
/*           LOGE ("Failing SQL: %s", sql_statement); */
/*           sqlite3_free (errmsg); */
/*           return -1; */
/*         } */
/*     } */
/*   ret_code = 0; */
/*   if (schema_exists) */
/*     { */
/*       // Execute migration scripts */
/*       if (sqlite3_exec (db, MIGRATE_A_SQL, 0, 0, &errmsg) != SQLITE_OK) */
/*         { */
/*           LOGE ("MIGRATE_A_SQL failed: %s", errmsg); */
/*           sqlite3_free (errmsg); */
/*           return -1; */
/*         } */
/*       if (sqlite3_exec (db, MIGRATE_B_SQL, 0, 0, &errmsg) != SQLITE_OK) */
/*         { */
/*           LOGE ("MIGRATE_B_SQL failed: %s", errmsg); */
/*           sqlite3_free (errmsg); */
/*           return -1; */
/*         } */
/*       if (sqlite3_exec (db, MIGRATE_C_SQL, 0, 0, &errmsg) != SQLITE_OK) */
/*         { */
/*           LOGE ("MIGRATE_C_SQL failed: %s", errmsg); */
/*           sqlite3_free (errmsg); */
/*           return -1; */
/*         } */
/*     } */
/*   return ret_code; */
/* } */
static int
db_create_tables_unlocked (bool schema_exists)
{
  char *errmsg = NULL;
  int ret_code = -1;
  // Get the thread-local connection
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return -1;
    }
  int rc;
  for (size_t i = 0; i < create_table_count; i++)
    {
      const char *sql_statement = create_table_sql[i];
      // If schema_exists is true, skip CREATE TABLE IF NOT EXISTS statements
      if (schema_exists
          && strstr (sql_statement,
                     "CREATE TABLE IF NOT EXISTS") == sql_statement)
        {
          continue;             // Skip this statement
        }
      // CRITICAL FIX: Use 'db', not 'db_handle'
      rc = sqlite3_exec (db, sql_statement, 0, 0, &errmsg);
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
  db_mutex_lock ();
  rc = db_insert_defaults_unlocked ();
  db_mutex_unlock ();
  return rc;
}


/* /\* Actual work; assumes db_mutex is already held *\/ */
/* static int */
/* db_insert_defaults_unlocked (void) */
/* { */
/*   char *errmsg = NULL; */
/*   int ret_code = -1; */
/*   sqlite3 *db = db_get_handle (); */
/*   if (!db) */
/*     { */
/*       return -1; */
/*     } */
/*   int rc; // Declared rc here */
/*   LOGD ("db_insert_defaults_unlocked: Starting default data insertion."); */
/*   for (size_t i = 0; i < insert_default_count; i++) */
/*     { */
/*       LOGD ( */
/*         "db_insert_defaults_unlocked: Executing default SQL statement %zu: %s", */
/*         i, */
/*         insert_default_sql[i]); */
/*       if (sqlite3_exec (db, insert_default_sql[i], NULL, NULL, &errmsg) != */
/*           SQLITE_OK) */
/*         { */
/*           LOGE ("DB insert_defaults error (%zu): %s", i, errmsg); */
/*           LOGE ("Failing SQL: %s", insert_default_sql[i]); */
/*           goto cleanup; */
/*         } */
/*     } */
/*   // Ensure bank accounts for system, players, and ports */
/*   const char *insert_default_bank_account_sql[] = { */
/*     "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) VALUES ('system', 0, 'CRD', 1000000000);", */
/*     // System bank */
/*     "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) SELECT 'player', id, 'CRD', 1000 FROM players WHERE name = 'System';", */
/*     "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) SELECT 'player', id, 'CRD', 1000 FROM players WHERE name = 'Federation Administrator';", */
/*     "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) SELECT 'player', id, 'CRD', 1000 FROM players WHERE name = 'newguy';", */
/*     "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) SELECT 'player', id, 'CRD', 1000 FROM players WHERE name = 'ai_qa_bot';", */
/*     // Player accounts for goodguy, badguy, admin will be created on auth.register if not present */
/*     "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) SELECT 'port', id, 'CRD', 100000 FROM ports WHERE name = 'Earth Port';" */
/*   }; */
/*   for (size_t i = */
/*          0; */
/*        i < */
/*        sizeof(insert_default_bank_account_sql) / */
/*        sizeof(insert_default_bank_account_sql[0]); */
/*        i++) */
/*     { */
/*       rc = sqlite3_exec (db, */
/*                          insert_default_bank_account_sql[i], */
/*                          NULL, */
/*                          NULL, */
/*                          &errmsg); */
/*       if (rc != SQLITE_OK) */
/*         { */
/*           LOGE ( */
/*             "db_insert_defaults_unlocked: SQL error inserting default bank account: %s", */
/*             errmsg); */
/*           sqlite3_free (errmsg); */
/*           return -1; */
/*         } */
/*     } */
/*   ret_code = 0; */
/*   LOGD ( */
/*     "db_insert_defaults_unlocked: Default data insertion completed successfully."); */
/* cleanup: */
/*   if (errmsg) */
/*     { */
/*       sqlite3_free (errmsg); */
/*     } */
/*   return ret_code; */
/* } */
static int
db_insert_defaults_unlocked (void)
{
  char *errmsg = NULL;
  int ret_code = -1;
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return -1;
    }
  int rc;
  LOGD ("db_insert_defaults_unlocked: Starting default data insertion.");
  for (size_t i = 0; i < insert_default_count; i++)
    {
      // Logging reduced to avoid spam, but can remain if desired
      // LOGD ("db_insert_defaults_unlocked: Executing default SQL statement %zu", i);
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
    "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) VALUES ('system', 0, 'CRD', 1000000000);",
    // System bank
    "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) SELECT 'player', id, 'CRD', 1000 FROM players WHERE name = 'System';",
    "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) SELECT 'player', id, 'CRD', 1000 FROM players WHERE name = 'Federation Administrator';",
    "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) SELECT 'player', id, 'CRD', 1000 FROM players WHERE name = 'newguy';",
    "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) SELECT 'player', id, 'CRD', 1000 FROM players WHERE name = 'ai_qa_bot';",
    // Player accounts for goodguy, badguy, admin will be created on auth.register if not present
    "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) SELECT 'port', id, 'CRD', 100000 FROM ports WHERE name = 'Earth Port';"
  };
  for (size_t i =
         0;
       i <
       sizeof(insert_default_bank_account_sql) /
       sizeof(insert_default_bank_account_sql[0]);
       i++)
    {
      rc = sqlite3_exec (db,
                         insert_default_bank_account_sql[i],
                         NULL,
                         NULL,
                         &errmsg);
      if (rc != SQLITE_OK)
        {
          LOGE (
            "db_insert_defaults_unlocked: SQL error inserting default bank account: %s",
            errmsg);
          sqlite3_free (errmsg);
          return -1;
        }
    }
  ret_code = 0;
  LOGD (
    "db_insert_defaults_unlocked: Default data insertion completed successfully.");
cleanup:
  if (errmsg)
    {
      sqlite3_free (errmsg);
    }
  return ret_code;
}


void
db_close (void)
{
  // New way!
  db_close_thread ();
}


/* Public, thread-safe wrapper */
int
db_ensure_auth_schema (void)
{
  int rc;
  db_mutex_lock ();
  rc = db_ensure_auth_schema_unlocked ();
  db_mutex_unlock ();
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
    {
      return SQLITE_ERROR;
    }
  rc = sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    {
      goto fail;
    }
  rc = sqlite3_exec (db,
                     "CREATE TABLE IF NOT EXISTS sessions ("
                     "  token      TEXT PRIMARY KEY, "
                     "  player_id  INTEGER NOT NULL, "
                     "  expires    INTEGER NOT NULL, "
                     "  created_at INTEGER NOT NULL"
                     ");", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    {
      goto rollback;
    }
  rc = sqlite3_exec (db,
                     "CREATE INDEX IF NOT EXISTS idx_sessions_player  ON sessions(player_id);",
                     NULL,
                     NULL,
                     &errmsg);
  if (rc != SQLITE_OK)
    {
      goto rollback;
    }
  rc = sqlite3_exec (db,
                     "CREATE INDEX IF NOT EXISTS idx_sessions_expires ON sessions(expires);",
                     NULL,
                     NULL,
                     &errmsg);
  if (rc != SQLITE_OK)
    {
      goto rollback;
    }
  rc = sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    {
      goto fail;
    }
  rc = SQLITE_OK;
rollback:
  if (rc != SQLITE_OK)
    {
      db_safe_rollback (db, "db_ensure_auth_schema_unlocked");
    }
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
    {
      *out_core = NULL;
    }
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return SQLITE_ERROR;
    }
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
  db_mutex_lock ();
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      goto done;
    }
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
        {
          json_object_set_new (core, "beacon", json_string (beacon));
        }
      else
        {
          json_object_set_new (core, "beacon", json_null ());
        }
      if (out_core)
        {
          *out_core = core;
        }
      rc = SQLITE_OK;
    }
  else
    {
      rc = SQLITE_ERROR;        /* caller maps to 1401 */
    }
done:
  if (st)
    {
      sqlite3_finalize (st);
    }
  db_mutex_unlock ();
  return rc;
}


int
db_session_create (int player_id, int ttl_seconds, char token_out[65])
{
  sqlite3 *db = NULL;
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;        // Default to error
  // 1. Acquire the lock FIRST.
  // db_mutex_lock(); // Removed: db_get_handle() is recursive
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
    {
      goto cleanup;
    }
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
  // db_mutex_unlock(); // Removed: db_get_handle() is recursive
  return rc;
}


int
db_session_lookup (const char *token, int *out_player_id,
                   long long *out_expires_epoch)
{
  sqlite3 *db = NULL;
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;        // Default to error
  // 1. Acquire the lock before any database access
  db_mutex_lock ();
  // Sanity checks
  if (!token)
    {
      rc = SQLITE_MISUSE;
      goto cleanup;
    }
  // Initialize output variables
  if (out_player_id)
    {
      *out_player_id = 0;
    }
  if (out_expires_epoch)
    {
      *out_expires_epoch = 0;
    }
  db = db_get_handle ();
  if (!db)
    {
      rc = SQLITE_ERROR;
      goto cleanup;
    }
  const char *sql = "SELECT player_id, expires FROM sessions WHERE token=?1;";
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      goto cleanup;
    }
  sqlite3_bind_text (st, 1, token, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      int pid = sqlite3_column_int (st, 0);
      long long exp = sqlite3_column_int64 (st, 1);
      time_t now = time (NULL);
      if (exp <= (long long) now)
        {
          rc = SQLITE_NOTFOUND; // Token expired
          goto cleanup;
        }
      if (out_player_id)
        {
          *out_player_id = pid;
        }
      if (out_expires_epoch)
        {
          *out_expires_epoch = exp;
        }
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
  db_mutex_unlock ();
  return rc;
}


/* Revoke */
int
db_session_revoke (const char *token)
{
  sqlite3 *db = NULL;
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;        // Default to error
  // 1. Acquire the lock FIRST to ensure thread safety
  db_mutex_lock ();
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
    {
      goto cleanup;
    }
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
  db_mutex_unlock ();
  return rc;
}


/* Revoke - Internal Unlocked Helper */
static int
db_session_revoke_unlocked (const char *token)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return SQLITE_ERROR;
    }
  const char *sql = "DELETE FROM sessions WHERE token=?1;";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
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
    {
      return SQLITE_MISUSE;
    }
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return SQLITE_ERROR;
    }
  const char *sql = "SELECT player_id, expires FROM sessions WHERE token=?1;";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_text (st, 1, token, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      int pid = sqlite3_column_int (st, 0);
      long long exp = sqlite3_column_int64 (st, 1);
      sqlite3_finalize (st);
      time_t now = time (NULL);
      if (exp <= (long long) now)
        {
          return SQLITE_NOTFOUND; /* expired */
        }
      if (out_player_id)
        {
          *out_player_id = pid;
        }
      if (out_expires_epoch)
        {
          *out_expires_epoch = exp;
        }
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
    {
      return SQLITE_MISUSE;
    }
  if (gen_session_token (token_out) != 0)
    {
      return SQLITE_ERROR;
    }
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return SQLITE_ERROR;
    }
  time_t now = time (NULL);
  long long exp = (long long) now + ttl_seconds;
  const char *sql =
    "INSERT INTO sessions(token, player_id, expires, created_at) VALUES(?1, ?2, ?3, ?4);";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
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
    {
      return SQLITE_MISUSE;
    }
  int pid = 0;
  long long exp = 0;
  int rc = SQLITE_ERROR;
  // 1. Acquire the lock to make the entire sequence atomic.
  db_mutex_lock ();
  // 2. Perform all operations using the unlocked helpers.
  rc = db_session_lookup_unlocked (old_token, &pid, &exp);
  if (rc != SQLITE_OK)
    {
      goto cleanup;
    }
  rc = db_session_revoke_unlocked (old_token);
  if (rc != SQLITE_OK)
    {
      goto cleanup;
    }
  rc = db_session_create_unlocked (pid, ttl_seconds, token_out);
  if (rc == SQLITE_OK && out_player_id)
    {
      *out_player_id = pid;
    }
cleanup:
  // 3. Release the lock before returning.
  db_mutex_unlock ();
  return rc;
}


/* Public, thread-safe wrapper */
int
db_ensure_idempotency_schema (void)
{
  int rc;
  db_mutex_lock ();
  rc = db_ensure_idempotency_schema_unlocked ();
  db_mutex_unlock ();
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
    {
      return SQLITE_ERROR;
    }
  rc = sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    {
      goto fail;
    }
  rc = sqlite3_exec (db,
                     "CREATE TABLE IF NOT EXISTS idempotency ("
                     "  key       TEXT PRIMARY KEY, "
                     "  cmd       TEXT NOT NULL, "
                     "  req_fp    TEXT NOT NULL, "
                     "  response  TEXT, "
                     "  created_at  INTEGER NOT NULL, "
                     "  updated_at  INTEGER" ");", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    {
      goto rollback;
    }
  rc = sqlite3_exec (db,
                     "CREATE INDEX IF NOT EXISTS idx_idemp_cmd ON idempotency(cmd);",
                     NULL,
                     NULL,
                     &errmsg);
  if (rc != SQLITE_OK)
    {
      goto rollback;
    }
  rc = sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    {
      goto fail;
    }
  rc = SQLITE_OK;
rollback:
  if (rc != SQLITE_OK)
    {
      db_safe_rollback (db, "db_ensure_idempotency_schema_unlocked");
    }
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
  int rc = SQLITE_ERROR;        // Default to error
  // 1. Acquire the lock before accessing the database
  db_mutex_lock ();
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
    {
      goto cleanup;
    }
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
  db_mutex_unlock ();
  return rc;
}


int
db_idemp_fetch (const char *key, char **out_cmd, char **out_req_fp,
                char **out_response_json)
{
  sqlite3 *db = NULL;
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;        // Default to error
  // 1. Acquire the lock FIRST to ensure thread safety
  db_mutex_lock ();
  if (!key)
    {
      rc = SQLITE_MISUSE;
      goto cleanup;
    }
  // Ensure output pointers are NULL on failure
  if (out_cmd)
    {
      *out_cmd = NULL;
    }
  if (out_req_fp)
    {
      *out_req_fp = NULL;
    }
  if (out_response_json)
    {
      *out_response_json = NULL;
    }
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
    {
      goto cleanup;
    }
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
  db_mutex_unlock ();
  return rc;
}


int
db_idemp_store_response (const char *key, const char *response_json)
{
  sqlite3 *db = NULL;
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;        // Default to error
  // 1. Acquire the lock FIRST to ensure thread safety
  db_mutex_lock ();
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
    {
      goto cleanup;
    }
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
  db_mutex_unlock ();
  return rc;
}

