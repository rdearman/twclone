#include "db_legacy.h"
#include <jansson.h>
#include <string.h>             // For strcasecmp
#include <math.h>               // For floor() function
#include <ctype.h>              // For isalnum, isspace
#include "server_stardock.h"
#include "common.h"
#include "db/repo/repo_database.h"
#include "repo_cmd.h"       // For player petty cash functions
#include "server_players.h"     // For player petty cash functions
#include "server_envelope.h"
#include "errors.h"
#include "server_ports.h"       // For port types, etc.
#include "server_ships.h"       // For h_get_active_ship_id
#include "server_cmds.h"        // For send_response_error, send_json_response and send_error_and_return
#include "server_corporation.h" // For h_is_player_corp_ceo
#include "server_loop.h"        // For idemp_fingerprint_json
#include "server_config.h"
#include "server_log.h"         // For LOGE
#include "server_cron.h"        // For LOGE
#include "server_communication.h"       // For server_broadcast_to_sector
#include "db/db_api.h"
#include "db/sql_driver.h"
#include "game_db.h"
#include "db/repo/repo_database.h"

/* -------------------------------------------------------------------------
 * SQLite removal shim: db_stmt_* provides a minimal "prepared statement" API
 * backed by the new db_api layer (Postgres/SQLite/etc).  This allows this
 * module to preserve its existing control flow while removing any dependency
 * on sqlite3 headers or symbols.
 *
 * Conventions:
 *   - db_stmt_step(): returns DB_ROW while a row is available, DB_DONE when no
 *     more rows, DB_ERR on error.
 *   - Column accessors read from the current row.
 *   - db_stmt_reset(): resets iteration; bindings remain unless cleared.
 * ------------------------------------------------------------------------- */

#ifndef DB_ROW
#define DB_ROW   100
#endif
#ifndef DB_DONE
#define DB_DONE  101
#endif
#ifndef DB_OK
#define DB_OK    0
#endif
#ifndef DB_ERR
#define DB_ERR   (-1)

#ifndef DB_NULL
#define DB_NULL 5
#endif
#endif

/* Old code sometimes used SQLITE_* binding lifetime flags; they are ignored. */
#ifndef DB_BIND_TRANSIENT
#define DB_BIND_TRANSIENT 0
#endif
#ifndef DB_BIND_STATIC
#define DB_BIND_STATIC 0
#endif

typedef struct db_stmt_s {
  db_t *db;
  const char *sql;
  char converted_sql[512];  /* buffer for sql_build() output */
  db_bind_t binds[64];     /* adjust if needed */
  int nbinds;              /* number of bound params (max index) */
  bool started;            /* query has been executed */
  bool had_error;
  db_res_t *res;
  db_error_t err;
} db_stmt_t;

/* Keep a best-effort "last error" for legacy db_errmsg() callsites. */
static __thread char g_db_last_errmsg[256];


static const char *
db_errmsg (db_t *db_unused)
{
  (void)db_unused;
  return g_db_last_errmsg[0] ? g_db_last_errmsg : "db error";
}


static int
stx_begin (db_t *db)
{
  db_error_t err;
  db_error_clear (&err);
  if (!db_tx_begin (db, 0, &err))
    {
      snprintf (g_db_last_errmsg, sizeof(g_db_last_errmsg), "%s", err.message);
      return -1;
    }
  return 0;
}


static int
stx_commit (db_t *db)
{
  db_error_t err;
  db_error_clear (&err);
  if (!db_tx_commit (db, &err))
    {
      snprintf (g_db_last_errmsg, sizeof(g_db_last_errmsg), "%s", err.message);
      return -1;
    }
  return 0;
}


static int
stx_rollback (db_t *db)
{
  db_error_t err;
  db_error_clear (&err);
  (void)db_tx_rollback (db, &err);
  if (err.message && *err.message)
    {
      snprintf (g_db_last_errmsg, sizeof(g_db_last_errmsg), "%s", err.message);
    }
  return 0;
}


static int
st_exec_sql (db_t *db, const char *sql)
{
  db_error_t err;
  db_error_clear (&err);
  if (!db_exec (db, sql, NULL, 0, &err))
    {
      snprintf (g_db_last_errmsg, sizeof(g_db_last_errmsg), "%s", err.message);
      return -1;
    }
  return 0;
}


static char *
st_asprintf (const char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);
#if defined(_GNU_SOURCE) || defined(__GLIBC__)
  char *buf = NULL;


  if (vasprintf (&buf, fmt, ap) < 0)
    {
      buf = NULL;
    }
  va_end (ap);
  return buf;
#else
  va_list ap2;


  va_copy (ap2, ap);
  int n = vsnprintf (NULL, 0, fmt, ap2);


  va_end (ap2);
  if (n < 0)
    {
      va_end (ap); return NULL;
    }
  char *buf = (char *)malloc ((size_t)n + 1);


  if (!buf)
    {
      va_end (ap); return NULL;
    }
  vsnprintf (buf, (size_t)n + 1, fmt, ap);
  va_end (ap);
  return buf;
#endif
}


static void
db_stmt_set_err (db_stmt_t *st, const db_error_t *err)
{
  if (!st || !err)
    {
      return;
    }
  st->had_error = true;
  st->err = *err;
  if (err->message && *err->message)
    {
      snprintf (g_db_last_errmsg, sizeof(g_db_last_errmsg), "%s", err->message);
    }
  else
    {
      snprintf (g_db_last_errmsg, sizeof(g_db_last_errmsg), "db error");
    }
}


static int
db_stmt_prepare_v2 (db_t *db, const char *sql, int nbytes,
                    db_stmt_t **out_stmt, const char **tail)
{
  (void)nbytes;
  if (tail)
    {
      *tail = NULL;
    }
  if (!db || !sql || !out_stmt)
    {
      snprintf (g_db_last_errmsg,
                sizeof(g_db_last_errmsg),
                "db_stmt_prepare_v2: bad args");
      return DB_ERR;
    }

  db_stmt_t *st = (db_stmt_t *)calloc (1, sizeof(*st));


  if (!st)
    {
      snprintf (g_db_last_errmsg,
                sizeof(g_db_last_errmsg),
                "db_stmt_prepare_v2: OOM");
      return DB_ERR;
    }

  st->db = db;
  st->sql = sql;
  st->nbinds = 0;
  st->started = false;
  st->had_error = false;
  st->res = NULL;
  db_error_clear (&st->err);
  *out_stmt = st;
  return DB_OK;
}


static void
db_stmt_clear_bindings (db_stmt_t *st)
{
  if (!st)
    {
      return;
    }
  memset (st->binds, 0, sizeof(st->binds));
  st->nbinds = 0;
}


static void
db_stmt_reset (db_stmt_t *st)
{
  if (!st)
    {
      return;
    }
  if (st->res)
    {
      db_res_finalize (st->res);
      st->res = NULL;
    }
  st->started = false;
  st->had_error = false;
  db_error_clear (&st->err);
}


static void
db_stmt_finalize (db_stmt_t *st)
{
  if (!st)
    {
      return;
    }
  if (st->res)
    {
      db_res_finalize (st->res);
      st->res = NULL;
    }
  free (st);
}


static void
db_stmt_ensure_capacity (db_stmt_t *st, int idx1)
{
  if (!st)
    {
      return;
    }
  if (idx1 > st->nbinds)
    {
      st->nbinds = idx1;
    }
}


static void
db_stmt_bind_i32 (db_stmt_t *st, int idx1, int v)
{
  if (!st || idx1 <= 0 ||
      idx1 > (int)(sizeof(st->binds) / sizeof(st->binds[0])))
    {
      return;
    }
  st->binds[idx1 - 1] = db_bind_i32 (v);
  db_stmt_ensure_capacity (st, idx1);
}


static void
db_stmt_bind_i64 (db_stmt_t *st, int idx1, int64_t v)
{
  if (!st || idx1 <= 0 ||
      idx1 > (int)(sizeof(st->binds) / sizeof(st->binds[0])))
    {
      return;
    }
  st->binds[idx1 - 1] = db_bind_i64 (v);
  db_stmt_ensure_capacity (st, idx1);
}


static void
db_stmt_bind_text (db_stmt_t *st,
                   int idx1,
                   const char *v,
                   int n,
                   int lifetime_ignored)
{
  (void)n;
  (void)lifetime_ignored;
  if (!st || idx1 <= 0 ||
      idx1 > (int)(sizeof(st->binds) / sizeof(st->binds[0])))
    {
      return;
    }
  st->binds[idx1 - 1] = v ? db_bind_text (v) : db_bind_null ();
  db_stmt_ensure_capacity (st, idx1);
}


static void
db_stmt_bind_null (db_stmt_t *st, int idx1)
{
  if (!st || idx1 <= 0 ||
      idx1 > (int)(sizeof(st->binds) / sizeof(st->binds[0])))
    {
      return;
    }
  st->binds[idx1 - 1] = db_bind_null ();
  db_stmt_ensure_capacity (st, idx1);
}


static int
db_stmt_step (db_stmt_t *st)
{
  if (!st)
    {
      snprintf (g_db_last_errmsg,
                sizeof(g_db_last_errmsg),
                "db_stmt_step: null stmt");
      return DB_ERR;
    }

  if (!st->started)
    {
      db_error_t err;


      db_error_clear (&err);
      /* Convert {N} placeholders to backend-specific format */
      sql_build(st->db, st->sql, st->converted_sql, sizeof(st->converted_sql));
      
      if (!db_query (st->db,
                     st->converted_sql,
                     (st->nbinds > 0) ? st->binds : NULL,
                     st->nbinds,
                     &st->res,
                     &err))
        {
          db_stmt_set_err (st, &err);
          return DB_ERR;
        }
      st->started = true;
    }

  db_error_t err;


  db_error_clear (&err);
  if (db_res_step (st->res, &err))
    {
      return DB_ROW;
    }

  /* No row: could be end-of-results or an error */
  if (err.code != DB_OK && err.code != ERR_DB_NO_ROWS)
    {
      db_stmt_set_err (st, &err);
      return DB_ERR;
    }

  return DB_DONE;
}


static int
db_stmt_col_i32 (db_stmt_t *st, int col)
{
  if (!st || !st->res)
    {
      return 0;
    }
  db_error_t err;


  db_error_clear (&err);
  return (int)db_res_col_i64 (st->res, col, &err);
}


static int64_t
db_stmt_col_i64 (db_stmt_t *st, int col)
{
  if (!st || !st->res)
    {
      return 0;
    }
  db_error_t err;


  db_error_clear (&err);
  return db_res_col_i64 (st->res, col, &err);
}


static const unsigned char *
db_stmt_col_text (db_stmt_t *st, int col)
{
  if (!st || !st->res)
    {
      return NULL;
    }
  db_error_t err;


  db_error_clear (&err);
  const char *s = db_res_col_text (st->res, col, &err);


  return (const unsigned char *)s;
}


static bool
db_stmt_col_is_null (db_stmt_t *st, int col)
{
  if (!st || !st->res)
    {
      return true;
    }
  db_error_t err;


  db_error_clear (&err);
  return db_res_col_is_null (st->res, col);
}


struct tavern_settings g_tavern_cfg;


// Static Forward Declarations for helper functions
static bool has_sufficient_funds (db_t *db, int player_id,
                                  long long required_amount);
static int update_player_credits_gambling (db_t *db, int player_id,
                                           long long amount, bool is_win);
static int validate_bet_limits (db_t *db, int player_id,
                                long long bet_amount);
static bool is_player_in_tavern_sector (db_t *db, int sector_id);
bool get_player_loan (db_t *db, int player_id, long long *principal,
                      int *interest_rate, int *due_date, int *is_defaulted);
static void sanitize_text (char *text, size_t max_len);


// Implementation for hardware.list RPC
int
cmd_hardware_list (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  int player_id = ctx->player_id;
  int ship_id = 0;
  int sector_id = 0;
  db_stmt_t *stmt = NULL;       // Declare stmt here
  int port_type = 0;            // Declare port_type here
  // 1. Authenticate player and get ship/sector context
  if (player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Authentication required.", NULL);
      return 0;
    }
  ship_id = h_get_active_ship_id (db, player_id);
  if (ship_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_SHIP_NOT_FOUND,
                                   "No active ship found.", NULL);
      return 0;
    }
  sector_id = ctx->sector_id;   // Get current sector from context
  // 2. Determine location type (Stardock, Class-0, or Other)
  char location_type[16] = "OTHER";
  int port_id = 0;
  const char *sql_loc_check =
    "SELECT port_id, type FROM ports WHERE sector_id = {1} AND (type = 9 OR type = 0);";
  // type 9 for Stardock, 0 for Class-0
  int rc = db_stmt_prepare_v2 (db, sql_loc_check, -1, &stmt, NULL);


  if (rc == DB_OK)
    {
      db_stmt_bind_i32 (stmt, 1, sector_id);
      if (db_stmt_step (stmt) == DB_ROW)
        {
          port_id = db_stmt_col_i32 (stmt, 0);
          port_type = db_stmt_col_i32 (stmt, 1);
          if (port_type == PORT_TYPE_STARDOCK)
            {                   // Stardock
              strncpy (location_type, LOCATION_STARDOCK,
                       sizeof (location_type) - 1);
            }
          else if (port_type == PORT_TYPE_CLASS0)
            {                   // Class-0
              strncpy (location_type, LOCATION_CLASS0,
                       sizeof (location_type) - 1);
            }
        }
      db_stmt_finalize (stmt);
      stmt = NULL;
    }
  if (port_id == 0)
    {                           // No Stardock or Class-0 port in this sector
      json_t *res = json_object ();


      json_object_set_new (res, "sector_id", json_integer (sector_id));
      json_object_set_new (res, "location_type", json_string (location_type));
      json_object_set_new (res, "items", json_array ());


      send_response_ok_take (ctx, root, "hardware.list_v1", &res);
      return 0;
    }
  // 3. Fetch current ship state and shiptype capabilities
  int current_holds = 0, current_fighters = 0, current_shields = 0;
  int current_genesis = 0, current_detonators = 0, current_probes = 0;
  int current_cloaks = 0;       // cloaking_devices in ships table
  int has_transwarp = 0, has_planet_scanner = 0, has_long_range_scanner = 0;
  int max_holds = 0, max_fighters = 0, max_shields = 0;
  int max_genesis = 0, max_detonators_st = 0, max_probes_st = 0;        // _st suffix for shiptype limits
  int can_transwarp = 0, can_planet_scan = 0, can_long_range_scan = 0;
  // Get ship current state
  const char *sql_ship_state =
    "SELECT s.holds, s.fighters, s.shields, s.genesis, s.detonators, s.probes, s.cloaking_devices, s.has_transwarp, s.has_planet_scanner, s.has_long_range_scanner, st.maxholds, st.maxfighters, st.maxshields, st.maxgenesis, st.max_detonators, st.max_probes, st.can_transwarp, st.can_planet_scan, st.can_long_range_scan, st.max_cloaks FROM ships s JOIN shiptypes st ON s.type_id = st.shiptypes_id WHERE s.ship_id = {1};";


  rc = db_stmt_prepare_v2 (db, sql_ship_state, -1, &stmt, NULL);
  if (rc != DB_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB_QUERY_FAILED, "Failed to get ship state.");
      return 0; return 0;
    }
  db_stmt_bind_i32 (stmt, 1, ship_id);
  if (db_stmt_step (stmt) == DB_ROW)
    {
      current_holds = db_stmt_col_i32 (stmt, 0);
      current_fighters = db_stmt_col_i32 (stmt, 1);
      current_shields = db_stmt_col_i32 (stmt, 2);
      current_genesis = db_stmt_col_i32 (stmt, 3);
      current_detonators = db_stmt_col_i32 (stmt, 4);
      current_probes = db_stmt_col_i32 (stmt, 5);
      current_cloaks = db_stmt_col_i32 (stmt, 6);       // cloaking_devices
      has_transwarp = db_stmt_col_i32 (stmt, 7);
      has_planet_scanner = db_stmt_col_i32 (stmt, 8);
      has_long_range_scanner = db_stmt_col_i32 (stmt, 9);
      max_holds = db_stmt_col_i32 (stmt, 10);
      max_fighters = db_stmt_col_i32 (stmt, 11);
      max_shields = db_stmt_col_i32 (stmt, 12);
      max_genesis = db_stmt_col_i32 (stmt, 13);
      max_detonators_st = db_stmt_col_i32 (stmt, 14);
      max_probes_st = db_stmt_col_i32 (stmt, 15);
      can_transwarp = db_stmt_col_i32 (stmt, 16);
      can_planet_scan = db_stmt_col_i32 (stmt, 17);
      can_long_range_scan = db_stmt_col_i32 (stmt, 18);
    }
  db_stmt_finalize (stmt);
  stmt = NULL;
  json_t *items_array = json_array ();
  const char *sql_items =
    "SELECT code, name, price, max_per_ship, category FROM hardware_items WHERE enabled = 1 AND ({1} = '"
    LOCATION_STARDOCK "' OR ({2} = '" LOCATION_CLASS0 "'));";


  rc = db_stmt_prepare_v2 (db, sql_items, -1, &stmt, NULL);
  if (rc != DB_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB_QUERY_FAILED,
                           "Failed to get hardware items.");
      return 0; return 0;
    }
  db_stmt_bind_text (stmt, 1, location_type, -1, DB_BIND_STATIC);
  db_stmt_bind_text (stmt, 2, location_type, -1, DB_BIND_STATIC);
  while (db_stmt_step (stmt) == DB_ROW)
    {
      const char *code = (const char *) db_stmt_col_text (stmt, 0);
      const char *name = (const char *) db_stmt_col_text (stmt, 1);
      int price = db_stmt_col_i32 (stmt, 2);
      int max_per_ship_hw = db_stmt_col_is_null (stmt,
                                                 3) ? -1 :
                            db_stmt_col_i32 (stmt,
                                             3);                                                // -1 means use shiptype max
      const char *category = (const char *) db_stmt_col_text (stmt, 4);
      int max_purchase = 0;
      bool ship_has_capacity = true;
      bool item_supported = true;


      if (strcasecmp (category, HW_CATEGORY_FIGHTER) == 0)
        {
          max_purchase = MAX (0, max_fighters - current_fighters);
        }
      else if (strcasecmp (category, HW_CATEGORY_SHIELD) == 0)
        {
          max_purchase = MAX (0, max_shields - current_shields);
        }
      else if (strcasecmp (category, HW_CATEGORY_HOLD) == 0)
        {
          max_purchase = MAX (0, max_holds - current_holds);
        }
      else if (strcasecmp (category, HW_CATEGORY_SPECIAL) == 0)
        {
          if (strcasecmp (code, HW_ITEM_GENESIS) == 0)
            {
              int limit = (max_per_ship_hw != HW_MAX_PER_SHIP_DEFAULT
                           && max_per_ship_hw <
                           max_genesis) ? max_per_ship_hw : max_genesis;


              max_purchase = MAX (0, limit - current_genesis);
            }
          else if (strcasecmp (code, HW_ITEM_DETONATOR) == 0)
            {
              int limit = (max_per_ship_hw != HW_MAX_PER_SHIP_DEFAULT
                           && max_per_ship_hw <
                           max_detonators_st) ? max_per_ship_hw :
                          max_detonators_st;


              max_purchase = MAX (0, limit - current_detonators);
            }
          else if (strcasecmp (code, HW_ITEM_PROBE) == 0)
            {
              int limit = (max_per_ship_hw != HW_MAX_PER_SHIP_DEFAULT
                           && max_per_ship_hw <
                           max_probes_st) ? max_per_ship_hw : max_probes_st;


              max_purchase = MAX (0, limit - current_probes);
            }
          else
            {
              item_supported = false;   // Unknown special item
            }
        }
      else if (strcasecmp (category, HW_CATEGORY_MODULE) == 0)
        {
          if (strcasecmp (code, HW_ITEM_CLOAK) == 0)
            {
              max_purchase = (current_cloaks == 0) ? 1 : 0;
            }
          else if (strcasecmp (code, HW_ITEM_TWARP) == 0)
            {
              if (!can_transwarp)
                {
                  item_supported = false;
                }
              max_purchase = (can_transwarp && has_transwarp == 0) ? 1 : 0;
            }
          else if (strcasecmp (code, HW_ITEM_PSCANNER) == 0)
            {
              if (!can_planet_scan)
                {
                  item_supported = false;
                }
              max_purchase = (can_planet_scan
                              && has_planet_scanner == 0) ? 1 : 0;
            }
          else if (strcasecmp (code, HW_ITEM_LSCANNER) == 0)
            {
              if (!can_long_range_scan)
                {
                  item_supported = false;
                }
              max_purchase = (can_long_range_scan
                              && has_long_range_scanner == 0) ? 1 : 0;
            }
          else
            {
              item_supported = false;
            }
        }
      else
        {
          item_supported = false;
        }
      if (max_purchase <= 0)
        {
          ship_has_capacity = false;
        }
      if (item_supported
          && (max_purchase > HW_MIN_QUANTITY
              || strcmp (category, HW_CATEGORY_MODULE) == 0))
        {
          json_t *item_obj = json_object ();


          json_object_set_new (item_obj, "code", json_string (code));
          json_object_set_new (item_obj, "name", json_string (name));
          json_object_set_new (item_obj, "price", json_integer (price));
          json_object_set_new (item_obj, "max_purchase",
                               json_integer (max_purchase));
          json_object_set_new (item_obj, "ship_has_capacity",
                               json_boolean (ship_has_capacity));
          json_array_append_new (items_array, item_obj);
        }
    }
  db_stmt_finalize (stmt);
  {
    json_t *res = json_object ();


    json_object_set_new (res, "sector_id", json_integer (sector_id));
    json_object_set_new (res, "location_type", json_string (location_type));
    json_object_set (res, "items", items_array);


    send_response_ok_take (ctx, root, "hardware.list_v1", &res);
  }
  return 0;
}


// Implementation for hardware.buy RPC
int
cmd_hardware_buy (client_ctx_t *ctx,
                  json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_INVALID_SCHEMA,
                           "Missing data object.");
      return 0;
    }
  const char *code = json_string_value (json_object_get (data, "code"));
  int quantity = json_integer_value (json_object_get (data, "quantity"));


  if (!code || quantity <= 0)
    {
      send_response_error (ctx, root, 1301,
                           "Missing or invalid 'quantity'.");
      return 0;
    }
  // 1. Get Context
  int player_id = ctx->player_id;
  int sector_id = ctx->sector_id;
  int ship_id = h_get_active_ship_id (db, player_id);


  if (ship_id <= 0)
    {
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND,
                           "No active ship.");
      return 0;
    }
  // 2. Check Port Location (Stardock or Class 0)
  db_stmt_t *stmt = NULL;
  int port_type = -1;
  const char *sql_port =
    "SELECT type FROM ports WHERE sector_id = {1} AND (type = 9 OR type = 0);";


  if (db_stmt_prepare_v2 (db, sql_port, -1, &stmt, NULL) == DB_OK)
    {
      db_stmt_bind_i32 (stmt, 1, sector_id);
      if (db_stmt_step (stmt) == DB_ROW)
        {
          port_type = db_stmt_col_i32 (stmt, 0);
        }
    }
  db_stmt_finalize (stmt);
  if (port_type == -1)
    {
      send_response_error (ctx,
                           root,
                           1811,
                           "Hardware can only be purchased at Stardock or Class-0 ports.");
      return 0;
    }
  // 3. Get Item Details
  int price = 0;
  int requires_stardock = 0;
  int sold_in_class0 = 0;
  int max_per_ship = 0;
  char category[32] = { 0 };
  const char *sql_item =
    "SELECT price, requires_stardock, sold_in_class0, max_per_ship, category FROM hardware_items WHERE code = {1} AND enabled = 1;";
  bool item_found = false;


  if (db_stmt_prepare_v2 (db, sql_item, -1, &stmt, NULL) == DB_OK)
    {
      db_stmt_bind_text (stmt, 1, code, -1, DB_BIND_STATIC);
      if (db_stmt_step (stmt) == DB_ROW)
        {
          price = db_stmt_col_i32 (stmt, 0);
          requires_stardock = db_stmt_col_i32 (stmt, 1);
          sold_in_class0 = db_stmt_col_i32 (stmt, 2);
          if (!db_stmt_col_is_null (stmt, 3))
            {
              max_per_ship = db_stmt_col_i32 (stmt, 3);
            }
          const char *cat = (const char *) db_stmt_col_text (stmt, 4);


          if (cat)
            {
              strncpy (category, cat, sizeof (category) - 1);
            }
          item_found = true;
        }
    }
  db_stmt_finalize (stmt);
  if (!item_found)
    {
      send_response_error (ctx, root, 1812,
                           "Invalid or unavailable hardware item.");
      return 0;
    }
  // 4. Validate Port Type vs Item Requirements
  if (requires_stardock && port_type != 9)
    {
      send_response_error (ctx,
                           root,
                           1811,
                           "This hardware item is not sold at Class-0 ports.");
      return 0;
    }
  if (!sold_in_class0 && port_type == 0)
    {
      send_response_error (ctx,
                           root,
                           1811,
                           "This hardware item is not sold at Class-0 ports.");
      return 0;
    }
  // 5. Map Code to Column and Check Limits
  const char *col_name = NULL;


  // Mappings
  if (strcmp (code, "FIGHTERS") == 0)
    {
      col_name = "fighters";
    }
  else if (strcmp (code, "SHIELDS") == 0)
    {
      col_name = "shields";
    }
  else if (strcmp (code, "HOLDS") == 0)
    {
      col_name = "holds";
    }
  else if (strcmp (code, "GENESIS") == 0)
    {
      col_name = "genesis";
    }
  else if (strcmp (code, "MINES") == 0)
    {
      col_name = "mines";
    }
  else if (strcmp (code, "BEACONS") == 0)
    {
      col_name = "beacons";
    }
  else if (strcmp (code, "CLOAK") == 0)
    {
      col_name = "cloaking_devices";
    }
  else if (strcmp (code, "DETONATOR") == 0)
    {
      col_name = "detonators";
    }
  else if (strcmp (code, "PROBE") == 0)
    {
      col_name = "probes";
    }
  else if (strcmp (code, "LSCANNER") == 0)
    {
      col_name = "has_long_range_scanner";
    }
  else if (strcmp (code, "PSCANNER") == 0)
    {
      col_name = "has_planet_scanner";
    }
  else if (strcmp (code, "TWARP") == 0)
    {
      col_name = "has_transwarp";
    }
  if (!col_name)
    {
      send_response_error (ctx,
                           root,
                           500,
                           "Server configuration error: Unknown item mapping.");
      return 0;
    }
  // Check Limits logic
  int current_val = 0;
  int max_limit = 999999999;
  char sql_info[512];
  bool is_max_check_needed = true;
  char limit_col[64] = { 0 };


  if (strcmp (col_name, "fighters") == 0)
    {
      strcpy (limit_col, "maxfighters");
    }
  else if (strcmp (col_name, "shields") == 0)
    {
      strcpy (limit_col, "maxshields");
    }
  else if (strcmp (col_name, "holds") == 0)
    {
      strcpy (limit_col, "maxholds");
    }
  else if (strcmp (col_name, "genesis") == 0)
    {
      strcpy (limit_col, "maxgenesis");
    }
  else if (strcmp (col_name, "mines") == 0)
    {
      strcpy (limit_col, "maxmines");
    }
  else if (strcmp (col_name, "beacons") == 0)
    {
      strcpy (limit_col, "maxbeacons");
    }
  else if (strcmp (col_name, "cloaking_devices") == 0)
    {
      strcpy (limit_col, "max_cloaks");
    }
  else if (strcmp (col_name, "detonators") == 0)
    {
      strcpy (limit_col, "max_detonators");
    }
  else if (strcmp (col_name, "probes") == 0)
    {
      strcpy (limit_col, "max_probes");
    }
  else if (strncmp (col_name, "has_", 4) == 0)
    {
      is_max_check_needed = false;
      max_limit = 1;
    }
  else
    {
      is_max_check_needed = false;
    }
  if (is_max_check_needed)
    {
      snprintf (sql_info,
                sizeof (sql_info),
                "SELECT s.%s, st.%s FROM ships s JOIN shiptypes st ON s.type_id = st.shiptypes_id WHERE s.ship_id = {1};",
                col_name,
                limit_col);
    }
  else
    {
      snprintf (sql_info,
                sizeof (sql_info),
                "SELECT %s, 0 FROM ships WHERE ship_id = {1};", col_name);
    }
  if (db_stmt_prepare_v2 (db, sql_info, -1, &stmt, NULL) == DB_OK)
    {
      db_stmt_bind_i32 (stmt, 1, ship_id);
      if (db_stmt_step (stmt) == DB_ROW)
        {
          current_val = db_stmt_col_i32 (stmt, 0);
          if (is_max_check_needed)
            {
              max_limit = db_stmt_col_i32 (stmt, 1);
            }
        }
      else
        {
          db_stmt_finalize (stmt);
          send_response_error (ctx, root, 500, "Ship info not found.");
          return 0;
        }
    }
  else
    {
      LOGE ("cmd_hardware_buy: SQL prepare failed: %s", db_errmsg (db));
      send_response_error (ctx, root, 500,
                           "Database error checking limits.");
      return 0;
    }
  db_stmt_finalize (stmt);
  // 6. Check Logic
  if (max_per_ship > 0)
    {
      if (current_val + quantity > max_per_ship)
        {
          if (strcmp (col_name, "cloaking_devices") == 0)
            {
              send_response_error (ctx, root, 1814,
                                   "Cloaking Device already installed.");
              return 0;
            }
          else
            {
              send_response_error (ctx,
                                   root,
                                   1814,
                                   "Purchase would exceed ship type's maximum capacity.");
              return 0;
            }
        }
    }
  if (current_val + quantity > max_limit)
    {
      send_response_error (ctx,
                           root,
                           1814,
                           "Purchase would exceed ship type's maximum capacity.");
      return 0;
    }
  // 7. Check Funds
  long long total_cost = (long long) price * quantity;
  long long balance = 0;


  h_get_player_petty_cash (db, player_id, &balance);
  if (balance < total_cost)
    {
      send_response_error (ctx, root, 1813,
                           "Insufficient credits on ship for purchase.");
      return 0;
    }

/* 8. Execute transaction (deduct + update must be atomic) */
  db_error_t err;


  db_error_clear (&err);

  if (!db_tx_begin (db,0, &err))
    {
      LOGE ("cmd_hardware_buy: tx begin failed: %s (code=%d backend=%d)",
            err.message, err.code, err.backend_code);
      send_response_error (ctx, root, 500, "Database busy.");
      return 0; return 0;
    }

/* Deduct player petty cash */
  long long new_balance = 0;
  int rc_deduct =
    h_deduct_player_petty_cash_unlocked (db, player_id, total_cost,
                                         &new_balance);


  if (rc_deduct != 0)
    {
      /* ensure we undo any partial work */
      db_tx_rollback (db, &err);
      send_response_error (ctx, root, 1813, "Insufficient credits.");
      return 0; return 0;
    }

/* Update ship hardware column: build SQL safely (still dynamic column name) */
  char *sql_upd =
    st_asprintf ("UPDATE ships SET %s = %s + {1} WHERE ship_id = {2};",
                 col_name, col_name);


  if (!sql_upd)
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx, root, 500, "Out of memory.");
      return 0; return 0;
    }

  db_bind_t p_upd[2];


  p_upd[0] = db_bind_i32 (quantity);
  p_upd[1] = db_bind_i32 (ship_id);

  if (!db_exec (db, sql_upd, p_upd, 2, &err))
    {
      free (sql_upd);
      db_tx_rollback (db, &err);
      send_response_error (ctx, root, 500, "Database update failed.");
      return 0; return 0;
    }

  free (sql_upd);

/* Commit */
  if (!db_tx_commit (db, &err))
    {
      /* commit can fail on serialization/deadlock etc */
      db_tx_rollback (db, &err);
      send_response_error (ctx, root, 1813, "Purchase failed (concurrent).");
      return 0; return 0;
    }

/* 9. Response continues... */

  int final_val = current_val + quantity;
  json_t *ship_obj = json_object ();


  json_object_set_new (ship_obj, col_name, json_integer (final_val));
  if (strcmp (col_name, "cloaking_devices") == 0)
    {
      json_object_set_new (ship_obj, "cloaks_installed",
                           json_integer (final_val));
    }
  if (strcmp (col_name, "genesis") == 0)
    {
      json_object_set_new (ship_obj, "genesis_torps",
                           json_integer (final_val));
    }
  if (strcmp (col_name, "has_transwarp") == 0)
    {
      // The test expects "has_transwarp" which matches the col_name,
      // but verify if there's an alias.
      // "has_transwarp" is fine.
    }
  json_t *resp = json_object ();


  json_object_set_new (resp, "code", json_string (code));
  json_object_set_new (resp, "quantity", json_integer (quantity));
  json_object_set_new (resp, "credits_spent",
                       json_integer ((json_int_t) total_cost));
  json_object_set (resp, "ship", ship_obj);


  send_response_ok_take (ctx, root, "hardware.purchase_v1", &resp);
  return 0;
}


int
cmd_shipyard_sell (client_ctx_t *ctx, json_t *root)
{
  send_response_error (ctx,
                       root,
                       ERR_NOT_IMPLEMENTED,
                       "Not implemented: shipyard.sell");
  return 0;
}


// Implementation for shipyard.list RPC
int
cmd_shipyard_list (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return 0;
    }
  int player_id = ctx->player_id;
  int sector_id = ctx->sector_id;
  db_stmt_t *stmt = NULL;
  // Check if player is docked at a port of type 9 or 10 in the current sector
  const char *sql_loc = "SELECT p.port_id FROM ports p "
                        "JOIN ships s ON s.ported = p.port_id "
                        "JOIN players pl ON pl.ship_id = s.ship_id "
                        "WHERE p.sector_id = {1} AND pl.player_id = {2} AND (p.type = 9 OR p.type = 10);";
  int rc = db_stmt_prepare_v2 (db, sql_loc, -1, &stmt, NULL);


  if (rc != DB_OK)
    {
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                           "Failed to check shipyard location.");
      return 0;
    }
  db_stmt_bind_i32 (stmt, 1, sector_id);
  db_stmt_bind_i32 (stmt, 2, player_id);
  rc = db_stmt_step (stmt);
  if (rc != DB_ROW)
    {
      db_stmt_finalize (stmt);
      send_response_error (ctx, root, ERR_NOT_AT_SHIPYARD,
                           "You are not docked at a shipyard.");
      return 0; return 0;
    }
  int port_id = db_stmt_col_i32 (stmt, 0);


  db_stmt_finalize (stmt);
  stmt = NULL;
  // Load configuration
  struct twconfig *cfg = config_load ();


  if (!cfg)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR,
                           "Could not load server configuration.");
      return 0;
    }
  // Get current player and ship info
  const char *sql_info = "SELECT "
                         "p.alignment, p.commission_id, p.experience, "
                         "s.ship_id, s.type_id, st.name, st.basecost, "
                         "s.fighters, s.shields, s.mines, s.limpets, s.genesis, s.detonators, s.probes, s.cloaking_devices, "
                         "s.colonists, s.ore, s.organics, s.equipment, "
                         "s.has_transwarp, s.has_planet_scanner, s.has_long_range_scanner "
                         "FROM players p JOIN ships s ON p.ship_id = s.ship_id JOIN shiptypes st ON s.type_id = st.shiptypes_id "
                         "WHERE p.player_id = {1};";


  rc = db_stmt_prepare_v2 (db, sql_info, -1, &stmt, NULL);
  if (rc != DB_OK)
    {
      free (cfg);
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                           "Failed to fetch player/ship info.");
      return 0;
    }
  db_stmt_bind_i32 (stmt, 1, player_id);
  if (db_stmt_step (stmt) != DB_ROW)
    {
      db_stmt_finalize (stmt);
      free (cfg);
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND,
                           "Could not find player's active ship.");
      return 0;
    }
  // Current player/ship state
  int player_alignment = db_stmt_col_i32 (stmt, 0);
  int player_commission = db_stmt_col_i32 (stmt, 1);
  int player_experience = db_stmt_col_i32 (stmt, 2);
  long long current_ship_basecost = db_stmt_col_i64 (stmt, 6);
  int current_fighters = db_stmt_col_i32 (stmt, 7);
  int current_shields = db_stmt_col_i32 (stmt, 8);
  int current_mines = db_stmt_col_i32 (stmt, 9);
  int current_limpets = db_stmt_col_i32 (stmt, 10);
  int current_genesis = db_stmt_col_i32 (stmt, 11);
  int current_detonators = db_stmt_col_i32 (stmt, 12);
  int current_probes = db_stmt_col_i32 (stmt, 13);
  int current_cloaks = db_stmt_col_i32 (stmt, 14);
  long long current_cargo =
    db_stmt_col_i64 (stmt, 15) + db_stmt_col_i64 (stmt,
                                                  16) +
    db_stmt_col_i64 (stmt, 17) + db_stmt_col_i64 (stmt, 18);
  int has_transwarp = db_stmt_col_i32 (stmt, 19);
  int has_planet_scanner = db_stmt_col_i32 (stmt, 20);
  int has_long_range_scanner = db_stmt_col_i32 (stmt, 21);
  long trade_in_value =
    floor (current_ship_basecost *
           (cfg->shipyard_trade_in_factor_bp / 10000.0));
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "sector_id", json_integer (sector_id));
  json_object_set_new (response_data, "is_shipyard", json_true ());
  json_t *current_ship_obj = json_object ();


  json_object_set_new (current_ship_obj, "type",
                       json_string ((const char *)
                                    db_stmt_col_text (stmt, 5)));
  json_object_set_new (current_ship_obj, "base_price",
                       json_integer (current_ship_basecost));
  json_object_set_new (current_ship_obj, "trade_in_value",
                       json_integer (trade_in_value));
  json_object_set_new (response_data, "current_ship", current_ship_obj);
  db_stmt_finalize (stmt);
  stmt = NULL;
  // Fetch shipyard inventory and build "available" array
  json_t *available_array = json_array ();
  const char *sql_inventory =
    "SELECT si.ship_type_id, st.name, st.basecost, st.required_alignment, st.required_commission, st.required_experience, st.maxholds, st.maxfighters, st.maxshields, st.maxgenesis, st.max_detonators, st.max_probes, st.max_cloaks, st.can_transwarp, st.can_planet_scan, st.can_long_range_scan, st.maxmines, st.maxlimpets "
    "FROM shipyard_inventory si JOIN shiptypes st ON si.ship_type_id = st.shiptypes_id "
    "WHERE si.port_id = {1} AND si.enabled = 1 AND st.enabled = 1;";


  rc = db_stmt_prepare_v2 (db, sql_inventory, -1, &stmt, NULL);
  if (rc != DB_OK)
    {
      json_decref (response_data);
      free (cfg);
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                           "Failed to fetch shipyard inventory.");
      return 0;
    }
  db_stmt_bind_i32 (stmt, 1, port_id);
  while (db_stmt_step (stmt) == DB_ROW)
    {
      json_t *ship_obj = json_object ();
      json_t *reasons_array = json_array ();
      bool eligible = true;
      const char *type_name = (const char *) db_stmt_col_text (stmt, 1);
      long long new_ship_basecost = db_stmt_col_i64 (stmt, 2);
      long long net_cost;
      if (__builtin_sub_overflow(new_ship_basecost, trade_in_value, &net_cost))
        {
          /* Underflow shouldn't happen but handle defensively */
          net_cost = 0;
        }


      /* Corporate Flagship: CEO-only */
      if (type_name && !strcasecmp (type_name, "Corporate Flagship"))
        {
          int dummy_corp_id = 0;


          if (!h_is_player_corp_ceo (db, player_id, &dummy_corp_id))
            {
              eligible = false;
              json_array_append_new (reasons_array,
                                     json_string ("must_be_corp_ceo"));
            }
        }
      json_object_set_new (ship_obj, "type", json_string (type_name));
      json_object_set_new (ship_obj, "name", json_string (type_name));  // Using type_name as name for now
      json_object_set_new (ship_obj, "base_price",
                           json_integer (new_ship_basecost));
      json_object_set_new (ship_obj, "shipyard_price",
                           json_integer (new_ship_basecost));                                   // No markup for now
      json_object_set_new (ship_obj, "trade_in_value",
                           json_integer (trade_in_value));
      json_object_set_new (ship_obj, "net_cost", json_integer (net_cost));
      // Eligibility Checks
      if (!db_stmt_col_is_null (stmt, 3)
          && player_alignment < db_stmt_col_i32 (stmt, 3))
        {
          eligible = false;
          json_array_append_new (reasons_array,
                                 json_string ("alignment_too_low"));
        }
      if (!db_stmt_col_is_null (stmt, 4)
          && player_commission < db_stmt_col_i32 (stmt, 4))
        {
          eligible = false;
          json_array_append_new (reasons_array,
                                 json_string ("commission_too_low"));
        }
      if (!db_stmt_col_is_null (stmt, 5)
          && player_experience < db_stmt_col_i32 (stmt, 5))
        {
          eligible = false;
          json_array_append_new (reasons_array,
                                 json_string ("experience_too_low"));
        }
      if (cfg->shipyard_require_cargo_fit
          && current_cargo > db_stmt_col_i64 (stmt, 6))
        {
          eligible = false;
          json_array_append_new (reasons_array,
                                 json_string ("cargo_would_not_fit"));
        }
      if (cfg->shipyard_require_fighters_fit
          && current_fighters > db_stmt_col_i32 (stmt, 7))
        {
          eligible = false;
          json_array_append_new (reasons_array,
                                 json_string ("fighters_exceed_capacity"));
        }
      if (cfg->shipyard_require_shields_fit
          && current_shields > db_stmt_col_i32 (stmt, 8))
        {
          eligible = false;
          json_array_append_new (reasons_array,
                                 json_string ("shields_exceed_capacity"));
        }
      if (cfg->shipyard_require_hardware_compat)
        {
          if (current_genesis > db_stmt_col_i32 (stmt, 9))
            {
              eligible = false;
              json_array_append_new (reasons_array,
                                     json_string ("genesis_exceed_capacity"));
            }
          if (current_detonators > db_stmt_col_i32 (stmt, 10))
            {
              eligible = false;
              json_array_append_new (reasons_array,
                                     json_string
                                       ("detonators_exceed_capacity"));
            }
          if (current_probes > db_stmt_col_i32 (stmt, 11))
            {
              eligible = false;
              json_array_append_new (reasons_array,
                                     json_string ("probes_exceed_capacity"));
            }
          if (current_cloaks > db_stmt_col_i32 (stmt, 12))
            {
              eligible = false;
              json_array_append_new (reasons_array,
                                     json_string ("cloak_not_supported"));
            }
          if (has_transwarp && !db_stmt_col_i32 (stmt, 13))
            {
              eligible = false;
              json_array_append_new (reasons_array,
                                     json_string ("transwarp_not_supported"));
            }
          if (has_planet_scanner && !db_stmt_col_i32 (stmt, 14))
            {
              eligible = false;
              json_array_append_new (reasons_array,
                                     json_string
                                       ("planet_scan_not_supported"));
            }
          if (has_long_range_scanner && !db_stmt_col_i32 (stmt, 15))
            {
              eligible = false;
              json_array_append_new (reasons_array,
                                     json_string
                                       ("long_range_not_supported"));
            }
          if (current_mines > db_stmt_col_i32 (stmt, 16))
            {
              eligible = false;
              json_array_append_new (reasons_array,
                                     json_string ("mines_exceed_capacity"));
            }
          if (current_limpets > db_stmt_col_i32 (stmt, 17))
            {
              eligible = false;
              json_array_append_new (reasons_array,
                                     json_string ("limpets_exceed_capacity"));
            }
        }
      json_object_set_new (ship_obj, "eligible", json_boolean (eligible));
      json_object_set_new (ship_obj, "reasons", reasons_array);
      json_array_append_new (available_array, ship_obj);
    }
  db_stmt_finalize (stmt);
  json_object_set_new (response_data, "available", available_array);
  send_response_ok_take (ctx, root, "shipyard.list_v1", &response_data);
  free (cfg);
  return 0;
}


// Implementation for shipyard.upgrade RPC
int
cmd_shipyard_upgrade (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data payload.");
      return 0;
    }
  int new_type_id = 0;
  const char *new_ship_name = NULL;


  if (!json_get_int_flexible (data, "new_type_id", &new_type_id)
      || new_type_id <= 0)
    {
      send_response_error (ctx, root, ERR_MISSING_FIELD,
                           "Missing or invalid 'new_type_id'.");
      return 0;
    }
  new_ship_name = json_get_string_or_null (data, "new_ship_name");
  if (!new_ship_name || strlen (new_ship_name) == 0)
    {
      send_response_error (ctx, root, ERR_MISSING_FIELD,
                           "Missing or invalid 'new_ship_name'.");
      return 0;
    }
  // FIX: Declare variables here
  db_stmt_t *stmt = NULL;
  // int port_id = 0; // Unused
  // FIX: Define the missing SQL query string
  const char *sql_loc = "SELECT p.port_id FROM ports p "
                        "JOIN ships s ON s.ported = p.port_id "
                        "JOIN players pl ON pl.ship_id = s.ship_id "
                        "WHERE p.sector_id = {1} AND pl.player_id = {2} AND (p.type = 9 OR p.type = 10);";


  // Begin transaction
  if (stx_begin (db) !=
      DB_OK)
    {
      send_response_error (ctx, root, ERR_DB,
                           "Failed to start transaction.");
      return 0;
    }
  // Re-validate location
  int rc = db_stmt_prepare_v2 (db, sql_loc, -1, &stmt, NULL);


  if (rc != DB_OK)
    {
      stx_rollback (db);
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                           "Failed to check shipyard location.");
      return 0;
    }
  db_stmt_bind_i32 (stmt, 1, ctx->sector_id);
  db_stmt_bind_i32 (stmt, 2, ctx->player_id);
  rc = db_stmt_step (stmt);
  if (rc != DB_ROW)
    {
      db_stmt_finalize (stmt);
      stx_rollback (db);
      send_response_error (ctx, root, ERR_NOT_AT_SHIPYARD,
                           "You are not docked at a shipyard.");
      return 0; return 0;
    }
  // port_id = db_stmt_col_i32 (stmt, 0); // Unused
  db_stmt_finalize (stmt);
  stmt = NULL;
  struct twconfig *cfg = config_load ();


  if (!cfg)
    {
      stx_rollback (db);
      send_response_error (ctx, root, ERR_SERVER_ERROR,
                           "Could not load server configuration.");
      return 0;
    }
  if (strlen (new_ship_name) > (size_t) cfg->max_ship_name_length)
    {
      free (cfg);
      stx_rollback (db);
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "Ship name is too long.");
      return 0;
    }
  const char *sql_info = "SELECT "
                         "p.alignment, p.commission_id, p.experience, s.credits, "
                         "s.fighters, s.shields, s.mines, s.limpets, s.genesis, s.detonators, s.probes, s.cloaking_devices, "
                         "s.colonists, s.ore, s.organics, s.equipment, "
                         "s.has_transwarp, s.has_planet_scanner, s.has_long_range_scanner, s.ship_id, s.type_id, st.basecost "
                         "FROM players p JOIN ships s ON p.ship_id = s.ship_id JOIN shiptypes st ON s.type_id = st.shiptypes_id "
                         "WHERE p.player_id = {1};";


  rc = db_stmt_prepare_v2 (db, sql_info, -1, &stmt, NULL);
  if (rc != DB_OK)
    {
      db_stmt_finalize (stmt);
      stx_rollback (db);
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                           "Failed to check shipyard location.");
      return 0; return 0;
    }
  db_stmt_bind_i32 (stmt, 1, ctx->player_id);
  rc = db_stmt_step (stmt);
  if (rc != DB_ROW)
    {
      db_stmt_finalize (stmt);
      stx_rollback (db);
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND,
                           "Could not find player's active ship.");
      return 0; return 0;
    }
  int player_alignment = db_stmt_col_i32 (stmt, 0);
  int player_commission = db_stmt_col_i32 (stmt, 1);
  int player_experience = db_stmt_col_i32 (stmt, 2);
  long long current_credits = db_stmt_col_i64 (stmt, 3);
  int current_fighters = db_stmt_col_i32 (stmt, 4);
  int current_shields = db_stmt_col_i32 (stmt, 5);
  int current_mines = db_stmt_col_i32 (stmt, 6);
  int current_limpets = db_stmt_col_i32 (stmt, 7);
  int current_genesis = db_stmt_col_i32 (stmt, 8);
  int current_detonators = db_stmt_col_i32 (stmt, 9);
  int current_probes = db_stmt_col_i32 (stmt, 10);
  int current_cloaks = db_stmt_col_i32 (stmt, 11);
  long long current_cargo =
    db_stmt_col_i64 (stmt, 12) + db_stmt_col_i64 (stmt,
                                                  13) +
    db_stmt_col_i64 (stmt, 14) + db_stmt_col_i64 (stmt, 15);
  int has_transwarp = db_stmt_col_i32 (stmt, 16);
  int has_planet_scanner = db_stmt_col_i32 (stmt, 17);
  int has_long_range_scanner = db_stmt_col_i32 (stmt, 18);
  int current_ship_id = db_stmt_col_i32 (stmt, 19);
  long long old_ship_basecost = db_stmt_col_i64 (stmt, 21);


  db_stmt_finalize (stmt);
  stmt = NULL;
  const char *sql_target_type =
    "SELECT basecost, required_alignment, required_commission, required_experience, maxholds, maxfighters, maxshields, maxgenesis, max_detonators, max_probes, max_cloaks, can_transwarp, can_planet_scan, can_long_range_scan, maxmines, maxlimpets, name FROM shiptypes WHERE shiptypes_id = {1} AND enabled = 1;";


  rc = db_stmt_prepare_v2 (db, sql_target_type, -1, &stmt, NULL);
  if (rc != DB_OK || db_stmt_step (stmt) != DB_ROW)
    {
      if (stmt)
        {
          db_stmt_finalize (stmt);
        }
      free (cfg);
      stx_rollback (db);
      send_response_error (ctx,
                           root,
                           ERR_SHIPYARD_INVALID_SHIP_TYPE,
                           "Target ship type not found or is not available.");
      return 0;
    }
  bool eligible_for_upgrade = true;


  if (!db_stmt_col_is_null (stmt, 1)
      && player_alignment < db_stmt_col_i32 (stmt, 1))
    {
      eligible_for_upgrade = false;
    }
  if (!db_stmt_col_is_null (stmt, 2)
      && player_commission < db_stmt_col_i32 (stmt, 2))
    {
      eligible_for_upgrade = false;
    }
  if (!db_stmt_col_is_null (stmt, 3)
      && player_experience < db_stmt_col_i32 (stmt, 3))
    {
      eligible_for_upgrade = false;
    }
  int new_max_holds = db_stmt_col_i32 (stmt, 4);


  if (cfg->shipyard_require_cargo_fit && current_cargo > new_max_holds)
    {
      eligible_for_upgrade = false;
    }
  int new_max_fighters = db_stmt_col_i32 (stmt, 5);


  if (cfg->shipyard_require_fighters_fit
      && current_fighters > new_max_fighters)
    {
      eligible_for_upgrade = false;
    }
  int new_max_shields = db_stmt_col_i32 (stmt, 6);


  if (cfg->shipyard_require_shields_fit && current_shields > new_max_shields)
    {
      eligible_for_upgrade = false;
    }
  if (cfg->shipyard_require_hardware_compat)
    {
      if (current_genesis > db_stmt_col_i32 (stmt, 7))
        {
          eligible_for_upgrade = false;
        }
      if (current_detonators > db_stmt_col_i32 (stmt, 8))
        {
          eligible_for_upgrade = false;
        }
      if (current_probes > db_stmt_col_i32 (stmt, 9))
        {
          eligible_for_upgrade = false;
        }
      if (current_cloaks > db_stmt_col_i32 (stmt, 10))
        {
          eligible_for_upgrade = false;
        }
      if (has_transwarp && !db_stmt_col_i32 (stmt, 11))
        {
          eligible_for_upgrade = false;
        }
      if (has_planet_scanner && !db_stmt_col_i32 (stmt, 12))
        {
          eligible_for_upgrade = false;
        }
      if (has_long_range_scanner && !db_stmt_col_i32 (stmt, 13))
        {
          eligible_for_upgrade = false;
        }
      if (current_mines > db_stmt_col_i32 (stmt, 14))
        {
          eligible_for_upgrade = false;
        }
      if (current_limpets > db_stmt_col_i32 (stmt, 15))
        {
          eligible_for_upgrade = false;
        }
    }
  if (!eligible_for_upgrade)
    {
      db_stmt_finalize (stmt);
      free (cfg);
      stx_rollback (db);
      send_response_error (ctx,
                           root,
                           ERR_SHIPYARD_REQUIREMENTS_NOT_MET,
                           "Ship upgrade requirements not met (capacity or capabilities).");
      return 0;
    }
  long long new_shiptype_basecost = db_stmt_col_i64 (stmt, 0);
  const char *target_ship_name =
    (const char *) db_stmt_col_text (stmt, 16);


  if (target_ship_name
      && strcasecmp (target_ship_name, "Corporate Flagship") == 0)
    {
      int dummy_corp_id = 0;


      if (!h_is_player_corp_ceo (db, ctx->player_id, &dummy_corp_id))
        {
          db_stmt_finalize (stmt);
          free (cfg);
          stx_rollback (db);
          send_response_error (ctx,
                               root,
                               ERR_SHIPYARD_REQUIREMENTS_NOT_MET,
                               "Only a corporation CEO can purchase a Corporate Flagship.");
          return 0;
        }
    }
  db_stmt_finalize (stmt);
  stmt = NULL;
  long trade_in_value =
    floor (old_ship_basecost * (cfg->shipyard_trade_in_factor_bp / 10000.0));
  long tax = floor (new_shiptype_basecost * (cfg->shipyard_tax_bp / 10000.0));
  long long temp_sum, final_cost;
  /* Check for overflow in: new_basecost - trade_in + tax */
  if (__builtin_sub_overflow(new_shiptype_basecost, trade_in_value, &temp_sum) ||
      __builtin_add_overflow(temp_sum, tax, &final_cost))
    {
      /* Overflow in cost calculation - treat as unaffordable */
      free (cfg);
      stx_rollback (db);
      send_response_error (ctx, root, ERR_SHIPYARD_INSUFFICIENT_FUNDS,
                           "Ship cost calculation overflow.");
      return 0;
    }


  if (current_credits < final_cost)
    {
      free (cfg);
      stx_rollback (db);
      send_response_error (ctx, root, ERR_SHIPYARD_INSUFFICIENT_FUNDS,
                           "Insufficient credits for ship upgrade.");
      return 0;
    }
  const char *sql_update =
    "UPDATE ships SET type_id = {1}, name = {2}, credits = credits - {3} WHERE ship_id = {4};";


  rc = db_stmt_prepare_v2 (db, sql_update, -1, &stmt, NULL);
  if (rc != DB_OK)
    {
      free (cfg);
      stx_rollback (db);
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                           "Failed to prepare ship update.");
      return 0;
    }
  db_stmt_bind_i32 (stmt, 1, new_type_id);
  db_stmt_bind_text (stmt, 2, new_ship_name, -1, DB_BIND_STATIC);
  db_stmt_bind_i64 (stmt, 3, final_cost);
  db_stmt_bind_i32 (stmt, 4, current_ship_id);
  if (db_stmt_step (stmt) != DB_DONE)
    {
      db_stmt_finalize (stmt);
      free (cfg);
      stx_rollback (db);
      send_response_error (ctx, root, ERR_DB,
                           "Failed to execute ship upgrade.");
      return 0;
    }
  db_stmt_finalize (stmt);
  if (stx_commit (db) != DB_OK)
    {
      free (cfg);
      send_response_error (ctx, root, ERR_DB,
                           "Failed to commit transaction.");
      return 0;
    }
  json_t *event_payload = json_object ();


  json_object_set_new (event_payload, "player_id",
                       json_integer (ctx->player_id));
  json_object_set_new (event_payload, "new_type_id",
                       json_integer (new_type_id));


  db_log_engine_event (time (NULL), "shipyard.upgrade", "player",
                       ctx->player_id, ctx->sector_id, event_payload, NULL);

  free (cfg);
  return 0;
}


// Function to load tavern settings from the database
int
tavern_settings_load (void)
{
  db_t *db = game_db_get_handle ();
  db_stmt_t *stmt = NULL;
  const char *sql =
    "SELECT max_bet_per_transaction, daily_max_wager, enable_dynamic_wager_limit, graffiti_max_posts, notice_expires_days, buy_round_cost, buy_round_alignment_gain, loan_shark_enabled FROM tavern_settings WHERE tavern_settings_id = 1;";
  int rc = db_stmt_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != DB_OK)
    {
      LOGE ("Tavern settings prepare error: %s", db_errmsg (db));
      return -1;
    }
  if (db_stmt_step (stmt) == DB_ROW)
    {
      g_tavern_cfg.max_bet_per_transaction = db_stmt_col_i32 (stmt, 0);
      g_tavern_cfg.daily_max_wager = db_stmt_col_i32 (stmt, 1);
      g_tavern_cfg.enable_dynamic_wager_limit = db_stmt_col_i32 (stmt, 2);
      g_tavern_cfg.graffiti_max_posts = db_stmt_col_i32 (stmt, 3);
      g_tavern_cfg.notice_expires_days = db_stmt_col_i32 (stmt, 4);
      g_tavern_cfg.buy_round_cost = db_stmt_col_i32 (stmt, 5);
      g_tavern_cfg.buy_round_alignment_gain = db_stmt_col_i32 (stmt, 6);
      g_tavern_cfg.loan_shark_enabled = db_stmt_col_i32 (stmt, 7);
    }
  else
    {
      LOGE ("Tavern settings not found in database. Using defaults.");
      // Set default values if not found in DB
      g_tavern_cfg.max_bet_per_transaction = 5000;
      g_tavern_cfg.daily_max_wager = 50000;
      g_tavern_cfg.enable_dynamic_wager_limit = 0;
      g_tavern_cfg.graffiti_max_posts = 100;
      g_tavern_cfg.notice_expires_days = 7;
      g_tavern_cfg.buy_round_cost = 1000;
      g_tavern_cfg.buy_round_alignment_gain = 5;
      g_tavern_cfg.loan_shark_enabled = 1;
    }
  db_stmt_finalize (stmt);
  return 0;
}


// Helper function to check if a player is in a tavern sector
static bool
is_player_in_tavern_sector (db_t *db, int sector_id)
{
  db_stmt_t *stmt = NULL;
  const char *sql =
    "SELECT 1 FROM taverns WHERE sector_id = {1} AND enabled = 1;";
  bool in_tavern = false;
  if (db_stmt_prepare_v2 (db, sql, -1, &stmt, NULL) != DB_OK)
    {
      LOGE ("is_player_in_tavern_sector: Failed to prepare statement: %s",
            db_errmsg (db));
      return false;
    }
  db_stmt_bind_i32 (stmt, 1, sector_id);
  if (db_stmt_step (stmt) == DB_ROW)
    {
      in_tavern = true;
    }
  db_stmt_finalize (stmt);
  return in_tavern;
}


// Helper to retrieve player loan details
bool
get_player_loan (db_t *db, int player_id, long long *principal,
                 int *interest_rate, int *due_date, int *is_defaulted)
{
  db_stmt_t *stmt = NULL;
  const char *sql =
    "SELECT principal, interest_rate, due_date, is_defaulted FROM tavern_loans WHERE player_id = {1};";
  bool found = false;
  if (db_stmt_prepare_v2 (db, sql, -1, &stmt, NULL) != DB_OK)
    {
      LOGE ("get_player_loan: Failed to prepare statement: %s",
            db_errmsg (db));
      return false;
    }
  db_stmt_bind_i32 (stmt, 1, player_id);
  if (db_stmt_step (stmt) == DB_ROW)
    {
      if (principal)
        {
          *principal = db_stmt_col_i64 (stmt, 0);
        }
      if (interest_rate)
        {
          *interest_rate = db_stmt_col_i32 (stmt, 1);
        }
      if (due_date)
        {
          *due_date = db_stmt_col_i32 (stmt, 2);
        }
      if (is_defaulted)
        {
          *is_defaulted = db_stmt_col_i32 (stmt, 3);
        }
      found = true;
    }
  db_stmt_finalize (stmt);
  return found;
}


// Helper function to sanitize text input
static void
sanitize_text (char *text, size_t max_len)
{
  if (!text)
    {
      return;
    }
  size_t len = strnlen (text, max_len);


  for (int i = 0; i < len; i++)
    {
      // Allow basic alphanumeric, spaces, and common punctuation
      if (!isalnum ((unsigned char) text[i])
          && !isspace ((unsigned char) text[i])
          && strchr (".,!?-:;'\"()[]{}", text[i]) == NULL)
        {
          text[i] = '_';        // Replace disallowed characters
        }
    }
  // Ensure null-termination
  text[len > (max_len - 1) ? (max_len - 1) : len] = '\0';
}


// Helper function to validate and apply bet limits
// Returns 0 on success, -1 if bet exceeds transaction limit, -2 if bet exceeds daily limit, -3 if exceeds dynamic limit
static int
validate_bet_limits (db_t *db, int player_id, long long bet_amount)
{
  // Check max bet per transaction
  if (bet_amount > g_tavern_cfg.max_bet_per_transaction)
    {
      return -1;
    }
  // Check daily maximum wager - Placeholder for future implementation
  // This will require tracking daily wagers in the database.
  // For now, this check is a no-op, always returning success for this specific limit.
  // A robust solution would involve a `player_daily_wager` table and a cron job to reset it.
  // Check dynamic wager limit (if enabled)
  if (g_tavern_cfg.enable_dynamic_wager_limit)
    {
      long long player_credits = 0;
      db_stmt_t *stmt = NULL;
      const char *sql_credits =
        "SELECT credits FROM players WHERE player_id = {1};";


      if (db_stmt_prepare_v2 (db, sql_credits, -1, &stmt, NULL) == DB_OK)
        {
          db_stmt_bind_i32 (stmt, 1, player_id);
          if (db_stmt_step (stmt) == DB_ROW)
            {
              player_credits = db_stmt_col_i64 (stmt, 0);
            }
          db_stmt_finalize (stmt);
        }
      else
        {
          LOGE
            ("validate_bet_limits: Failed to prepare credits statement: %s",
            db_errmsg (db));
          // If we can't get credits, we can't apply dynamic limit, so fail safe
          return -3;
        }
      // Example dynamic limit: bet cannot exceed 10% of liquid credits
      if (bet_amount > (player_credits / 10))
        {
          return -3;            // Exceeds dynamic wager limit
        }
    }
  return 0;                     // Success
}


// Function to handle player credit changes for gambling (deducts bet, adds winnings)
static int
update_player_credits_gambling (db_t *db, int player_id, long long amount,
                                bool is_win)
{
  const char *sql_update = is_win ?
                           "UPDATE players SET credits = credits + {1} WHERE player_id = {2};"
    : "UPDATE players SET credits = credits - {1} WHERE player_id = {2};";
  db_stmt_t *stmt = NULL;
  int rc = -1;
  if (db_stmt_prepare_v2 (db, sql_update, -1, &stmt, NULL) == DB_OK)
    {
      db_stmt_bind_i64 (stmt, 1, amount);
      db_stmt_bind_i32 (stmt, 2, player_id);
      if (db_stmt_step (stmt) == DB_DONE)
        {
          rc = 0;               // Success
        }
      else
        {
          LOGE
          (
            "update_player_credits_gambling: Failed to update player credits: %s",
            db_errmsg (db));
        }
      db_stmt_finalize (stmt);
    }
  else
    {
      LOGE ("update_player_credits_gambling: Failed to prepare statement: %s",
            db_errmsg (db));
    }
  return rc;
}


// Helper to check for sufficient funds
static bool
has_sufficient_funds (db_t *db, int player_id, long long required_amount)
{
  long long player_credits = 0;
  db_stmt_t *stmt = NULL;
  const char *sql = "SELECT credits FROM players WHERE player_id = {1};";
  if (db_stmt_prepare_v2 (db, sql, -1, &stmt, NULL) == DB_OK)
    {
      db_stmt_bind_i32 (stmt, 1, player_id);
      if (db_stmt_step (stmt) == DB_ROW)
        {
          player_credits = db_stmt_col_i64 (stmt, 0);
        }
      db_stmt_finalize (stmt);
    }
  else
    {
      LOGE ("has_sufficient_funds: Failed to prepare credits statement: %s",
            db_errmsg (db));
      return false;
    }
  return player_credits >= required_amount;
}


// Helper to check for loan default
bool
check_loan_default (db_t *db, int player_id, int current_time)
{
  long long principal = 0;
  int due_date = 0;
  int is_defaulted = 0;
  if (get_player_loan
        (db, player_id, &principal, NULL, &due_date,
        &is_defaulted) == DB_OK)
    {
      if (is_defaulted == 0 && current_time > due_date && principal > 0)
        {
          // Mark as defaulted
          const char *sql_default =
            "UPDATE tavern_loans SET is_defaulted = 1 WHERE player_id = {1};";
          db_stmt_t *stmt = NULL;


          if (db_stmt_prepare_v2 (db, sql_default, -1, &stmt, NULL) ==
              DB_OK)
            {
              db_stmt_bind_i32 (stmt, 1, player_id);
              if (db_stmt_step (stmt) != DB_DONE)
                {
                  LOGE
                    ("check_loan_default: Failed to mark loan as defaulted: %s",
                    db_errmsg (db));
                }
              db_stmt_finalize (stmt);
            }
          else
            {
              LOGE
                ("check_loan_default: Failed to prepare default statement: %s",
                db_errmsg (db));
            }
          return true;          // Just defaulted
        }
      else if (is_defaulted == 1)
        {
          return true;          // Already defaulted
        }
    }
  return false;                 // Not defaulted or no loan
}


// Helper to apply interest to a loan
int
apply_loan_interest (db_t *db, int player_id, long long current_principal,
                     int interest_rate_bp)
{
  long long interest_amount = (current_principal * interest_rate_bp) / 10000;   // interest_rate_bp is basis points
  long long new_principal = current_principal + interest_amount;
  db_stmt_t *stmt = NULL;
  const char *sql_update =
    "UPDATE tavern_loans SET principal = {1} WHERE player_id = {2};";
  int rc = db_stmt_prepare_v2 (db, sql_update, -1, &stmt, NULL);
  if (rc != DB_OK)
    {
      LOGE ("apply_loan_interest: Failed to prepare statement: %s",
            db_errmsg (db));
      return rc;
    }
  db_stmt_bind_i64 (stmt, 1, new_principal);
  db_stmt_bind_i32 (stmt, 2, player_id);
  if (db_stmt_step (stmt) != DB_DONE)
    {
      LOGE
      (
        "apply_loan_interest: Failed to update loan principal for player %d: %s",
        player_id,
        db_errmsg (db));
      rc = DB_ERR;
    }
  else
    {
      rc = DB_OK;
    }
  db_stmt_finalize (stmt);
  return rc;
}


// Implementation for tavern.lottery.buy_ticket RPC
int
cmd_tavern_lottery_buy_ticket (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return 0;
    }
  if (!is_player_in_tavern_sector (db, ctx->sector_id))
    {
      send_response_error (ctx, root, ERR_NOT_AT_TAVERN,
                           "You are not in a tavern sector.");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data payload.");
      return 0;
    }
  int ticket_number = 0;


  if (!json_get_int_flexible (data, "number", &ticket_number)
      || ticket_number <= 0 || ticket_number > 999)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "Lottery ticket number must be between 1 and 999.");
      return 0;
    }
  // Determine ticket price (example: 100 credits)
  long long ticket_price = 100; // This could be configurable in tavern_settings
  // Validate bet limits
  int limit_check = validate_bet_limits (db, ctx->player_id, ticket_price);


  if (limit_check == -1)
    {
      send_response_error (ctx, root, ERR_TAVERN_BET_TOO_HIGH,
                           "Bet exceeds maximum per transaction.");
      return 0;
    }
  else if (limit_check == -2)
    {
      send_response_error (ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED,
                           "Daily wager limit exceeded.");
      return 0;
    }
  else if (limit_check == -3)
    {
      send_response_error (ctx,
                           root,
                           ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
                           "Dynamic wager limit exceeded based on liquid assets.");
      return 0;
    }
  // Check player funds
  if (!has_sufficient_funds (db, ctx->player_id, ticket_price))
    {
      send_response_error (ctx, root, ERR_INSUFFICIENT_FUNDS,
                           "Insufficient credits to buy lottery ticket.");
      return 0;
    }
  // Get current draw date
  char draw_date_str[32];
  time_t now = time (NULL);
  struct tm *tm_info = localtime (&now);


  strftime (draw_date_str, sizeof (draw_date_str), "%Y-%m-%d", tm_info);
  // Deduct ticket price and insert ticket
  // This should ideally be wrapped in a transaction, but per user instruction, we avoid explicit BEGIN/COMMIT.
  // The calling context should manage the transaction.
  int rc = update_player_credits_gambling (db,
                                           ctx->player_id,
                                           ticket_price,
                                           false);      // Deduct


  if (rc != 0)
    {
      send_response_error (ctx, root, ERR_DB,
                           "Failed to deduct credits for lottery ticket.");
      return 0;
    }
  const char *sql_insert_ticket =
    "INSERT INTO tavern_lottery_tickets (draw_date, player_id, number, cost, purchased_at) VALUES ({1}, {2}, {3}, {4}, {5});";
  db_stmt_t *stmt = NULL;


  rc = db_stmt_prepare_v2 (db, sql_insert_ticket, -1, &stmt, NULL);
  if (rc != DB_OK)
    {
      // Rollback credits if this fails and no explicit transaction is used
      update_player_credits_gambling (db, ctx->player_id, ticket_price, true);  // Refund credits
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                           "Failed to prepare lottery ticket insert.");
      return 0;
    }
  db_stmt_bind_text (stmt, 1, draw_date_str, -1, DB_BIND_STATIC);
  db_stmt_bind_i32 (stmt, 2, ctx->player_id);
  db_stmt_bind_i32 (stmt, 3, ticket_number);
  db_stmt_bind_i64 (stmt, 4, ticket_price);
  db_stmt_bind_i32 (stmt, 5, (int) now);
  if (db_stmt_step (stmt) != DB_DONE)
    {
      db_stmt_finalize (stmt);
      update_player_credits_gambling (db, ctx->player_id, ticket_price, true);  // Refund credits
      send_response_error (ctx, root, ERR_DB,
                           "Failed to insert lottery ticket.");
      return 0;
    }
  db_stmt_finalize (stmt);
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "status",
                       json_string ("Ticket purchased"));
  json_object_set_new (response_data, "ticket_number",
                       json_integer (ticket_number));
  json_object_set_new (response_data, "draw_date",
                       json_string (draw_date_str));


  send_response_ok_take (ctx,
                         root,
                         "tavern.lottery.buy_ticket_v1", &response_data);
  return 0;
}


// Implementation for tavern.lottery.status RPC
int
cmd_tavern_lottery_status (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return 0;
    }
  if (!is_player_in_tavern_sector (db, ctx->sector_id))
    {
      send_response_error (ctx, root, ERR_NOT_AT_TAVERN,
                           "You are not in a tavern sector.");
      return 0;
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "draw_date", json_null ());
  json_object_set_new (response_data, "winning_number", json_null ());
  json_object_set_new (response_data, "jackpot", json_integer (0));
  json_object_set_new (response_data, "player_tickets", json_array ());
  // Get current draw date
  char draw_date_str[32];
  time_t now = time (NULL);
  struct tm *tm_info = localtime (&now);


  strftime (draw_date_str, sizeof (draw_date_str), "%Y-%m-%d", tm_info);
  json_object_set_new (response_data, "current_draw_date",
                       json_string (draw_date_str));
  // Query current lottery state
  db_stmt_t *stmt = NULL;
  const char *sql_state =
    "SELECT draw_date, winning_number, jackpot FROM tavern_lottery_state WHERE draw_date = {1};";
  int rc = db_stmt_prepare_v2 (db, sql_state, -1, &stmt, NULL);


  if (rc != DB_OK)
    {
      json_decref (response_data);
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                           "Failed to prepare lottery state query.");
      return 0;
    }
  db_stmt_bind_text (stmt, 1, draw_date_str, -1, DB_BIND_STATIC);
  if (db_stmt_step (stmt) == DB_ROW)
    {
      json_object_set_new (response_data, "draw_date",
                           json_string ((const char *)
                                        db_stmt_col_text (stmt, 0)));
      if (!db_stmt_col_is_null (stmt, 1))
        {
          json_object_set_new (response_data, "winning_number",
                               json_integer (db_stmt_col_i32 (stmt, 1)));
        }
      json_object_set_new (response_data, "jackpot",
                           json_integer (db_stmt_col_i64 (stmt, 2)));
    }
  db_stmt_finalize (stmt);
  stmt = NULL;
  // Query player's tickets for the current draw
  json_t *player_tickets_array = json_array ();
  const char *sql_player_tickets =
    "SELECT number, cost, purchased_at FROM tavern_lottery_tickets WHERE player_id = {1} AND draw_date = {2};";


  rc = db_stmt_prepare_v2 (db, sql_player_tickets, -1, &stmt, NULL);
  if (rc != DB_OK)
    {
      json_decref (response_data);
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                           "Failed to prepare player tickets query.");
      return 0;
    }
  db_stmt_bind_i32 (stmt, 1, ctx->player_id);
  db_stmt_bind_text (stmt, 2, draw_date_str, -1, DB_BIND_STATIC);
  while (db_stmt_step (stmt) == DB_ROW)
    {
      json_t *ticket_obj = json_object ();


      json_object_set_new (ticket_obj, "number",
                           json_integer (db_stmt_col_i32 (stmt, 0)));
      json_object_set_new (ticket_obj, "cost",
                           json_integer (db_stmt_col_i64 (stmt, 1)));
      json_object_set_new (ticket_obj, "purchased_at",
                           json_integer (db_stmt_col_i32 (stmt, 2)));
      json_array_append_new (player_tickets_array, ticket_obj);
    }
  db_stmt_finalize (stmt);
  json_object_set_new (response_data, "player_tickets", player_tickets_array);
  send_response_ok_take (ctx, root, "tavern.lottery.status_v1",
                         &response_data);
  return 0;
}


// Implementation for tavern.deadpool.place_bet RPC
int
cmd_tavern_deadpool_place_bet (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return 0;
    }
  if (!is_player_in_tavern_sector (db, ctx->sector_id))
    {
      send_response_error (ctx, root, ERR_NOT_AT_TAVERN,
                           "You are not in a tavern sector.");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data payload.");
      return 0;
    }
  int target_id = 0;
  long long bet_amount = 0;


  if (!json_get_int_flexible (data, "target_id", &target_id)
      || target_id <= 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "Invalid target_id.");
      return 0;
    }
  if (!json_get_int64_flexible (data, "amount", &bet_amount)
      || bet_amount <= 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "Bet amount must be positive.");
      return 0;
    }
  if (target_id == ctx->player_id)
    {
      send_response_error (ctx, root, ERR_TAVERN_BET_ON_SELF,
                           "Cannot place a bet on yourself.");
      return 0;
    }
  // Check if target player exists
  db_stmt_t *stmt = NULL;
  const char *sql_target_exists = "SELECT 1 FROM players WHERE player_id = {1};";


  if (db_stmt_prepare_v2 (db, sql_target_exists, -1, &stmt, NULL) !=
      DB_OK)
    {
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                           "Failed to check target player existence.");
      return 0;
    }
  db_stmt_bind_i32 (stmt, 1, target_id);
  if (db_stmt_step (stmt) != DB_ROW)
    {
      db_stmt_finalize (stmt);
      send_response_error (ctx, root, ERR_TAVERN_PLAYER_NOT_FOUND,
                           "Target player not found.");
      return 0;
    }
  db_stmt_finalize (stmt);
  stmt = NULL;
  // Validate bet limits
  int limit_check = validate_bet_limits (db, ctx->player_id, bet_amount);


  if (limit_check == -1)
    {
      send_response_error (ctx, root, ERR_TAVERN_BET_TOO_HIGH,
                           "Bet exceeds maximum per transaction.");
      return 0;
    }
  else if (limit_check == -2)
    {
      send_response_error (ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED,
                           "Daily wager limit exceeded.");
      return 0;
    }
  else if (limit_check == -3)
    {
      send_response_error (ctx,
                           root,
                           ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
                           "Dynamic wager limit exceeded based on liquid assets.");
      return 0;
    }
  // Check player funds
  if (!has_sufficient_funds (db, ctx->player_id, bet_amount))
    {
      send_response_error (ctx, root, ERR_INSUFFICIENT_FUNDS,
                           "Insufficient credits to place bet.");
      return 0;
    }
  // Deduct bet amount
  int rc = update_player_credits_gambling (db, ctx->player_id, bet_amount,
                                           false);      // Deduct


  if (rc != 0)
    {
      send_response_error (ctx, root, ERR_DB,
                           "Failed to deduct credits for bet.");
      return 0;
    }
  // Calculate expires_at (e.g., 24 hours from now)
  time_t now = time (NULL);
  time_t expires_at = now + (24 * 60 * 60);     // 24 hours
  // Calculate simple odds (placeholder: 10000 = 100%)
  // This could be more complex, e.g., based on target's alignment, ship strength, etc.
  int odds_bp = get_random_int (5000, 15000);   // Example: 50%-150% odds
  // Insert bet into tavern_deadpool_bets
  const char *sql_insert_bet =
    "INSERT INTO tavern_deadpool_bets (bettor_id, target_id, amount, odds_bp, placed_at, expires_at, resolved) VALUES ({1}, {2}, {3}, {4}, {5}, {6}, 0);";


  rc = db_stmt_prepare_v2 (db, sql_insert_bet, -1, &stmt, NULL);
  if (rc != DB_OK)
    {
      update_player_credits_gambling (db, ctx->player_id, bet_amount, true);    // Refund credits
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                           "Failed to prepare dead pool bet insert.");
      return 0;
    }
  db_stmt_bind_i32 (stmt, 1, ctx->player_id);
  db_stmt_bind_i32 (stmt, 2, target_id);
  db_stmt_bind_i64 (stmt, 3, bet_amount);
  db_stmt_bind_i32 (stmt, 4, odds_bp);
  db_stmt_bind_i32 (stmt, 5, (int) now);
  db_stmt_bind_i32 (stmt, 6, (int) expires_at);
  if (db_stmt_step (stmt) != DB_DONE)
    {
      db_stmt_finalize (stmt);
      update_player_credits_gambling (db, ctx->player_id, bet_amount, true);    // Refund credits
      send_response_error (ctx, root, ERR_DB,
                           "Failed to insert dead pool bet.");
      return 0;
    }
  db_stmt_finalize (stmt);
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "status",
                       json_string ("Dead Pool bet placed."));
  json_object_set_new (response_data, "target_id", json_integer (target_id));
  json_object_set_new (response_data, "amount", json_integer (bet_amount));
  json_object_set_new (response_data, "odds_bp", json_integer (odds_bp));


  send_response_ok_take (ctx,
                         root,
                         "tavern.deadpool.place_bet_v1", &response_data);
  return 0;
}


// Implementation for tavern.dice.play RPC
int
cmd_tavern_dice_play (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return 0;
    }
  if (!is_player_in_tavern_sector (db, ctx->sector_id))
    {
      send_response_error (ctx, root, ERR_NOT_AT_TAVERN,
                           "You are not in a tavern sector.");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data payload.");
      return 0;
    }
  long long bet_amount = 0;


  if (!json_get_int64_flexible (data, "amount", &bet_amount)
      || bet_amount <= 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "Bet amount must be positive.");
      return 0;
    }
  // Validate bet limits
  int limit_check = validate_bet_limits (db, ctx->player_id, bet_amount);


  if (limit_check == -1)
    {
      send_response_error (ctx, root, ERR_TAVERN_BET_TOO_HIGH,
                           "Bet exceeds maximum per transaction.");
      return 0;
    }
  else if (limit_check == -2)
    {
      send_response_error (ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED,
                           "Daily wager limit exceeded.");
      return 0;
    }
  else if (limit_check == -3)
    {
      send_response_error (ctx,
                           root,
                           ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
                           "Dynamic wager limit exceeded based on liquid assets.");
      return 0;
    }
  // Check player funds
  if (!has_sufficient_funds (db, ctx->player_id, bet_amount))
    {
      send_response_error (ctx, root, ERR_INSUFFICIENT_FUNDS,
                           "Insufficient credits to play dice.");
      return 0;
    }
  // Deduct bet amount
  int rc = update_player_credits_gambling (db, ctx->player_id, bet_amount,
                                           false);      // Deduct


  if (rc != 0)
    {
      send_response_error (ctx, root, ERR_DB,
                           "Failed to deduct credits for dice game.");
      return 0;
    }
  // Simulate dice roll (2d6, win on 7)
  int die1 = get_random_int (1, 6);
  int die2 = get_random_int (1, 6);
  int total = die1 + die2;
  bool win = (total == 7);
  long long winnings = 0;


  if (win)
    {
      winnings = bet_amount * 2;        // Example: 2x payout for winning
      rc = update_player_credits_gambling (db, ctx->player_id, winnings, true); // Add winnings
      if (rc != 0)
        {
          // Log error, but don't prevent response
          LOGE
            ("cmd_tavern_dice_play: Failed to add winnings to player credits.");
        }
    }
  // Get updated player credits for response
  long long current_credits = 0;
  db_stmt_t *stmt = NULL;
  const char *sql_credits = "SELECT credits FROM players WHERE player_id = {1};";


  if (db_stmt_prepare_v2 (db, sql_credits, -1, &stmt, NULL) == DB_OK)
    {
      db_stmt_bind_i32 (stmt, 1, ctx->player_id);
      if (db_stmt_step (stmt) == DB_ROW)
        {
          current_credits = db_stmt_col_i64 (stmt, 0);
        }
      db_stmt_finalize (stmt);
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data,
                       "status", json_string ("Dice game played."));
  json_object_set_new (response_data, "die1", json_integer (die1));
  json_object_set_new (response_data, "die2", json_integer (die2));
  json_object_set_new (response_data, "total", json_integer (total));
  json_object_set_new (response_data, "win", json_boolean (win));
  json_object_set_new (response_data, "winnings", json_integer (winnings));
  json_object_set_new (response_data, "player_credits",
                       json_integer (current_credits));


  send_response_ok_take (ctx, root, "tavern.dice.play_v1", &response_data);
  return 0;
}


// Implementation for tavern.highstakes.play RPC
int
cmd_tavern_highstakes_play (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return 0;
    }
  if (!is_player_in_tavern_sector (db, ctx->sector_id))
    {
      send_response_error (ctx, root, ERR_NOT_AT_TAVERN,
                           "You are not in a tavern sector.");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data payload.");
      return 0;
    }
  long long bet_amount = 0;
  int rounds = 0;


  if (!json_get_int64_flexible (data, "amount", &bet_amount)
      || bet_amount <= 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "Bet amount must be positive.");
      return 0;
    }
  if (!json_get_int_flexible (data, "rounds", &rounds) || rounds <= 0
      || rounds > 5)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "Rounds must be between 1 and 5.");
      return 0;
    }
  // Validate initial bet limits
  int limit_check = validate_bet_limits (db, ctx->player_id, bet_amount);


  if (limit_check == -1)
    {
      send_response_error (ctx, root, ERR_TAVERN_BET_TOO_HIGH,
                           "Bet exceeds maximum per transaction.");
      return 0;
    }
  else if (limit_check == -2)
    {
      send_response_error (ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED,
                           "Daily wager limit exceeded.");
      return 0;
    }
  else if (limit_check == -3)
    {
      send_response_error (ctx,
                           root,
                           ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
                           "Dynamic wager limit exceeded based on liquid assets.");
      return 0;
    }
  // Check player funds for initial bet
  if (!has_sufficient_funds (db, ctx->player_id, bet_amount))
    {
      send_response_error (ctx,
                           root,
                           ERR_INSUFFICIENT_FUNDS,
                           "Insufficient credits for initial high-stakes bet.");
      return 0;
    }
  // Deduct initial bet amount
  int rc = update_player_credits_gambling (db, ctx->player_id, bet_amount,
                                           false);      // Deduct


  if (rc != 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Failed to deduct credits for high-stakes game.");
      return 0;
    }
  long long current_pot = bet_amount;
  bool player_won_all_rounds = true;


  for (int i = 0; i < rounds; i++)
    {
      // Simulate a biased coin flip (e.g., 60% chance to win)
      int roll = get_random_int (1, 100);


      if (roll <= 60)
        {                       // Win this round
          current_pot *= 2;
        }
      else
        {                       // Lose this round
          player_won_all_rounds = false;
          break;                // Game ends on first loss
        }
    }
  long long winnings = 0;


  if (player_won_all_rounds)
    {
      winnings = current_pot;
      rc = update_player_credits_gambling (db, ctx->player_id, winnings, true); // Add final pot
      if (rc != 0)
        {
          LOGE
          (
            "cmd_tavern_highstakes_play: Failed to add winnings to player credits.");
        }
    }
  // Get updated player credits for response
  long long player_credits_after_game = 0;
  db_stmt_t *stmt_credits = NULL;
  const char *sql_credits = "SELECT credits FROM players WHERE player_id = {1};";


  if (db_stmt_prepare_v2 (db, sql_credits, -1, &stmt_credits, NULL) ==
      DB_OK)
    {
      db_stmt_bind_i32 (stmt_credits, 1, ctx->player_id);
      if (db_stmt_step (stmt_credits) == DB_ROW)
        {
          player_credits_after_game = db_stmt_col_i64 (stmt_credits, 0);
        }
      db_stmt_finalize (stmt_credits);
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "status",
                       json_string ("High-stakes game played."));
  json_object_set_new (response_data, "initial_bet",
                       json_integer (bet_amount));
  json_object_set_new (response_data, "rounds_played",
                       json_integer (player_won_all_rounds ? rounds
                                     : (rc == 0 ? rounds : 0)));
  json_object_set_new (response_data, "final_pot",
                       json_integer (current_pot));
  json_object_set_new (response_data, "player_won",
                       json_boolean (player_won_all_rounds));
  json_object_set_new (response_data, "winnings", json_integer (winnings));
  json_object_set_new (response_data, "player_credits",
                       json_integer (player_credits_after_game));


  send_response_ok_take (ctx, root, "tavern.highstakes.play_v1",
                         &response_data);
  return 0;
}


// Implementation for tavern.raffle.buy_ticket RPC
int
cmd_tavern_raffle_buy_ticket (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return 0;
    }
  if (!is_player_in_tavern_sector (db, ctx->sector_id))
    {
      send_response_error (ctx, root, ERR_NOT_AT_TAVERN,
                           "You are not in a tavern sector.");
      return 0;
    }
  // Fixed ticket price for raffle
  long long ticket_price = 10;  // Example: 10 credits per raffle ticket
  // Validate bet limits (using ticket_price as the bet)
  int limit_check = validate_bet_limits (db, ctx->player_id, ticket_price);


  if (limit_check == -1)
    {
      send_response_error (ctx,
                           root,
                           ERR_TAVERN_BET_TOO_HIGH,
                           "Raffle ticket price exceeds maximum per transaction.");
      return 0;
    }
  else if (limit_check == -2)
    {
      send_response_error (ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED,
                           "Daily wager limit exceeded.");
      return 0;
    }
  else if (limit_check == -3)
    {
      send_response_error (ctx,
                           root,
                           ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
                           "Dynamic wager limit exceeded based on liquid assets.");
      return 0;
    }
  // Check player funds
  if (!has_sufficient_funds (db, ctx->player_id, ticket_price))
    {
      send_response_error (ctx, root, ERR_INSUFFICIENT_FUNDS,
                           "Insufficient credits to buy raffle ticket.");
      return 0;
    }
  // Deduct ticket price
  int rc = update_player_credits_gambling (db,
                                           ctx->player_id,
                                           ticket_price,
                                           false);      // Deduct


  if (rc != 0)
    {
      send_response_error (ctx, root, ERR_DB,
                           "Failed to deduct credits for raffle ticket.");
      return 0;
    }
  long long current_pot = 0;
  // int last_win_ts = 0;
  // long long last_payout = 0;
  // int last_winner_id = 0;
  // Get and update raffle state
  db_stmt_t *stmt = NULL;
  const char *sql_get_raffle =
    "SELECT pot, last_winner_id, last_payout, last_win_ts FROM tavern_raffle_state WHERE tavern_raffle_state_id = 1;";


  if (db_stmt_prepare_v2 (db, sql_get_raffle, -1, &stmt, NULL) == DB_OK)
    {
      if (db_stmt_step (stmt) == DB_ROW)
        {
          current_pot = db_stmt_col_i64 (stmt, 0);
          // last_winner_id = db_stmt_col_i32 (stmt, 1);
          // last_payout = db_stmt_col_i64 (stmt, 2);
          // last_win_ts = db_stmt_col_i32 (stmt, 3);
        }
      db_stmt_finalize (stmt);
    }
  else
    {
      LOGE
      (
        "cmd_tavern_raffle_buy_ticket: Failed to prepare get raffle state statement: %s",
        db_errmsg (db));
      update_player_credits_gambling (db, ctx->player_id, ticket_price, true);  // Refund
      send_response_error (ctx, root, ERR_DB,
                           "Failed to retrieve raffle state.");
      return 0;
    }
  current_pot += ticket_price;  // Add ticket price to pot
  bool player_wins = (get_random_int (1, 1000) == 1);   // 1 in 1000 chance to win
  long long winnings = 0;
  const char *sql_update_raffle = NULL;


  if (player_wins)
    {
      winnings = current_pot;
      rc = update_player_credits_gambling (db, ctx->player_id, winnings, true); // Add winnings
      if (rc != 0)
        {
          LOGE
          (
            "cmd_tavern_raffle_buy_ticket: Failed to add winnings to player credits for raffle.");
        }
      // Reset pot and record win
      sql_update_raffle =
        "UPDATE tavern_raffle_state SET pot = 0, last_winner_id = {1}, last_payout = {2}, last_win_ts = {3} WHERE tavern_raffle_state_id = 1;";
      rc = db_stmt_prepare_v2 (db, sql_update_raffle, -1, &stmt, NULL);
      if (rc == DB_OK)
        {
          db_stmt_bind_i32 (stmt, 1, ctx->player_id);
          db_stmt_bind_i64 (stmt, 2, winnings);
          db_stmt_bind_i32 (stmt, 3, (int) time (NULL));
          if (db_stmt_step (stmt) != DB_DONE)
            {
              LOGE
              (
                "cmd_tavern_raffle_buy_ticket: Failed to update raffle state on win: %s",
                db_errmsg (db));
            }
          db_stmt_finalize (stmt);
        }
      else
        {
          LOGE
          (
            "cmd_tavern_raffle_buy_ticket: Failed to prepare update raffle state on win: %s",
            db_errmsg (db));
        }
      current_pot = 0;          // Pot reset after win
    }
  else
    {
      // Just update pot if no win
      sql_update_raffle =
        "UPDATE tavern_raffle_state SET pot = {1} WHERE tavern_raffle_state_id = 1;";
      rc = db_stmt_prepare_v2 (db, sql_update_raffle, -1, &stmt, NULL);
      if (rc == DB_OK)
        {
          db_stmt_bind_i64 (stmt, 1, current_pot);
          if (db_stmt_step (stmt) != DB_DONE)
            {
              LOGE
                ("cmd_tavern_raffle_buy_ticket: Failed to update raffle pot: %s",
                db_errmsg (db));
            }
          db_stmt_finalize (stmt);
        }
      else
        {
          LOGE
          (
            "cmd_tavern_raffle_buy_ticket: Failed to prepare update raffle pot statement: %s",
            db_errmsg (db));
        }
    }
  // Get updated player credits for response
  long long player_credits_after_game = 0;
  db_stmt_t *stmt_credits = NULL;
  const char *sql_credits = "SELECT credits FROM players WHERE player_id = {1};";


  if (db_stmt_prepare_v2 (db, sql_credits, -1, &stmt_credits, NULL) ==
      DB_OK)
    {
      db_stmt_bind_i32 (stmt_credits, 1, ctx->player_id);
      if (db_stmt_step (stmt_credits) == DB_ROW)
        {
          player_credits_after_game = db_stmt_col_i64 (stmt_credits, 0);
        }
      db_stmt_finalize (stmt_credits);
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "status",
                       json_string (player_wins ? "You won the raffle!" :
                                    "You bought a raffle ticket."));
  json_object_set_new (response_data, "player_wins",
                       json_boolean (player_wins));
  json_object_set_new (response_data, "winnings", json_integer (winnings));
  json_object_set_new (response_data, "current_pot",
                       json_integer (current_pot));
  json_object_set_new (response_data, "player_credits",
                       json_integer (player_credits_after_game));


  send_response_ok_take (ctx,
                         root, "tavern.raffle.buy_ticket_v1", &response_data);
  return 0;
}


// Implementation for tavern.trader.buy_password RPC
int
cmd_tavern_trader_buy_password (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return 0;
    }
  if (!is_player_in_tavern_sector (db, ctx->sector_id))
    {
      send_response_error (ctx, root, ERR_NOT_AT_TAVERN,
                           "You are not in a tavern sector.");
      return 0;
    }
  // Check player alignment (example: must be < 0 for underground access)
  long long player_alignment = 0;
  db_stmt_t *stmt_align = NULL;
  const char *sql_align = "SELECT alignment FROM players WHERE player_id = {1};";


  if (db_stmt_prepare_v2 (db, sql_align, -1, &stmt_align, NULL) == DB_OK)
    {
      db_stmt_bind_i32 (stmt_align, 1, ctx->player_id);
      if (db_stmt_step (stmt_align) == DB_ROW)
        {
          player_alignment = db_stmt_col_i64 (stmt_align, 0);
        }
      db_stmt_finalize (stmt_align);
    }
  else
    {
      LOGE
      (
        "cmd_tavern_trader_buy_password: Failed to prepare alignment statement: %s",
        db_errmsg (db));
      send_response_error (ctx, root, ERR_DB,
                           "Failed to retrieve player alignment.");
      return 0;
    }
  if (player_alignment >= 0)
    {                           // Example threshold
      send_response_error (ctx,
                           root,
                           ERR_TAVERN_TOO_HONORABLE,
                           "You are too honorable to access the underground.");
      return 0;
    }
  long long password_price = 5000;      // Fixed price for underground password
  // Validate bet limits (using password_price as the bet)
  int limit_check = validate_bet_limits (db,
                                         ctx->player_id,
                                         password_price);


  if (limit_check == -1)
    {
      send_response_error (ctx,
                           root,
                           ERR_TAVERN_BET_TOO_HIGH,
                           "Password price exceeds maximum transaction limit.");
      return 0;
    }
  else if (limit_check == -2)
    {
      send_response_error (ctx,
                           root,
                           ERR_TAVERN_DAILY_WAGER_EXCEEDED,
                           "Daily wager limit exceeded for password purchase.");
      return 0;
    }
  else if (limit_check == -3)
    {
      send_response_error (ctx,
                           root,
                           ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
                           "Dynamic wager limit exceeded for password purchase.");
      return 0;
    }
  // Check player funds
  if (!has_sufficient_funds (db, ctx->player_id, password_price))
    {
      send_response_error (ctx,
                           root,
                           ERR_INSUFFICIENT_FUNDS,
                           "Insufficient credits to buy underground password.");
      return 0;
    }
  // Deduct password price
  int rc = update_player_credits_gambling (db,
                                           ctx->player_id,
                                           password_price,
                                           false);      // Deduct


  if (rc != 0)
    {
      send_response_error (ctx, root, ERR_DB,
                           "Failed to deduct credits for password.");
      return 0;
    }
  // In a real implementation, this would update a player flag or an access table.
  // For now, we just return a success message.
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "status",
                       json_string ("Underground password purchased."));
  json_object_set_new (response_data, "password",
                       json_string ("UndergroundAccessCode-XYZ"));                              // Example password
  json_object_set_new (response_data, "cost", json_integer (password_price));


  send_response_ok_take (ctx,
                         root,
                         "tavern.trader.buy_password_v1", &response_data);
  return 0;
}


// Implementation for tavern.graffiti.post RPC
int
cmd_tavern_graffiti_post (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return 0;
    }
  if (!is_player_in_tavern_sector (db, ctx->sector_id))
    {
      send_response_error (ctx, root, ERR_NOT_AT_TAVERN,
                           "You are not in a tavern sector.");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data payload.");
      return 0;
    }
  const char *post_text_raw = json_get_string_or_null (data, "text");


  if (!post_text_raw || strlen (post_text_raw) == 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "Graffiti text cannot be empty.");
      return 0;
    }
  char post_text[256];          // Max length for graffiti post


  strncpy (post_text, post_text_raw, sizeof (post_text) - 1);
  post_text[sizeof (post_text) - 1] = '\0';
  sanitize_text (post_text, sizeof (post_text));
  if (strlen (post_text) == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "Graffiti text became empty after sanitization.");
      return 0;
    }
  time_t now = time (NULL);
  // Insert new graffiti post
  const char *sql_insert =
    "INSERT INTO tavern_graffiti (player_id, text, created_at) VALUES ({1}, {2}, {3});";
  db_stmt_t *stmt = NULL;
  int rc = db_stmt_prepare_v2 (db, sql_insert, -1, &stmt, NULL);


  if (rc != DB_OK)
    {
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                           "Failed to prepare graffiti insert.");
      return 0;
    }
  db_stmt_bind_i32 (stmt, 1, ctx->player_id);
  db_stmt_bind_text (stmt, 2, post_text, -1, DB_BIND_STATIC);
  db_stmt_bind_i32 (stmt, 3, (int) now);
  if (db_stmt_step (stmt) != DB_DONE)
    {
      db_stmt_finalize (stmt);
      send_response_error (ctx, root, ERR_DB,
                           "Failed to insert graffiti post.");
      return 0;
    }
  db_stmt_finalize (stmt);
  // Optional: Implement FIFO logic - if count exceeds graffiti_max_posts, delete the oldest
  const char *sql_count = "SELECT COUNT(*) FROM tavern_graffiti;";
  long long current_graffiti_count = 0;


  if (db_stmt_prepare_v2 (db, sql_count, -1, &stmt, NULL) == DB_OK)
    {
      if (db_stmt_step (stmt) == DB_ROW)
        {
          current_graffiti_count = db_stmt_col_i64 (stmt, 0);
        }
      db_stmt_finalize (stmt);
    }
  if (current_graffiti_count > g_tavern_cfg.graffiti_max_posts)
    {
      const char *sql_delete_oldest =
        "DELETE FROM tavern_graffiti WHERE id IN (SELECT id FROM tavern_graffiti ORDER BY created_at ASC LIMIT {1});";


      if (db_stmt_prepare_v2 (db, sql_delete_oldest, -1, &stmt, NULL) ==
          DB_OK)
        {
          db_stmt_bind_i32 (stmt, 1,
                            current_graffiti_count -
                            g_tavern_cfg.graffiti_max_posts);
          if (db_stmt_step (stmt) != DB_DONE)
            {
              LOGE
              (
                "cmd_tavern_graffiti_post: Failed to delete oldest graffiti posts: %s",
                db_errmsg (db));
            }
          db_stmt_finalize (stmt);
        }
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "status",
                       json_string ("Graffiti posted successfully."));
  json_object_set_new (response_data, "text", json_string (post_text));
  json_object_set_new (response_data, "created_at",
                       json_integer ((long long) now));


  send_response_ok_take (ctx, root, "tavern.graffiti.post_v1",
                         &response_data);
  return 0;
}


// Implementation for tavern.round.buy RPC
int
cmd_tavern_round_buy (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return 0;
    }
  if (!is_player_in_tavern_sector (db, ctx->sector_id))
    {
      send_response_error (ctx, root, ERR_NOT_AT_TAVERN,
                           "You are not in a tavern sector.");
      return 0;
    }
  long long cost = g_tavern_cfg.buy_round_cost;
  int alignment_gain = g_tavern_cfg.buy_round_alignment_gain;
  // Validate bet limits (using cost as the bet)
  int limit_check = validate_bet_limits (db, ctx->player_id, cost);


  if (limit_check == -1)
    {
      send_response_error (ctx,
                           root,
                           ERR_TAVERN_BET_TOO_HIGH,
                           "Cost to buy a round exceeds transaction limit.");
      return 0;
    }
  else if (limit_check == -2)
    {
      send_response_error (ctx,
                           root,
                           ERR_TAVERN_DAILY_WAGER_EXCEEDED,
                           "Daily wager limit exceeded for buying a round.");
      return 0;
    }
  else if (limit_check == -3)
    {
      send_response_error (ctx,
                           root,
                           ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
                           "Dynamic wager limit exceeded for buying a round.");
      return 0;
    }
  // Check player funds
  if (!has_sufficient_funds (db, ctx->player_id, cost))
    {
      send_response_error (ctx, root, ERR_INSUFFICIENT_FUNDS,
                           "Insufficient credits to buy a round.");
      return 0;
    }
  // Deduct cost
  int rc = update_player_credits_gambling (db,
                                           ctx->player_id,
                                           cost,
                                           false);      // Deduct


  if (rc != 0)
    {
      send_response_error (ctx, root, ERR_DB,
                           "Failed to deduct credits for buying a round.");
      return 0;
    }
  // Increase player's alignment
  const char *sql_update_alignment =
    "UPDATE players SET alignment = alignment + {1} WHERE player_id = {2};";
  db_stmt_t *stmt = NULL;


  if (db_stmt_prepare_v2 (db, sql_update_alignment, -1, &stmt, NULL) ==
      DB_OK)
    {
      db_stmt_bind_i32 (stmt, 1, alignment_gain);
      db_stmt_bind_i32 (stmt, 2, ctx->player_id);
      if (db_stmt_step (stmt) != DB_DONE)
        {
          LOGE ("cmd_tavern_round_buy: Failed to update player alignment: %s",
                db_errmsg (db));
        }
      db_stmt_finalize (stmt);
    }
  else
    {
      LOGE
      (
        "cmd_tavern_round_buy: Failed to prepare alignment update statement: %s",
        db_errmsg (db));
    }
  // Broadcast message to all online players in the sector
  json_t *broadcast_payload = json_object ();


  json_object_set_new (broadcast_payload, "message",
                       json_string ("A round has been bought for everyone!"));
  json_object_set_new (broadcast_payload, "player_id",
                       json_integer (ctx->player_id));
  json_object_set_new (broadcast_payload, "sector_id",
                       json_integer (ctx->sector_id));


  server_broadcast_to_sector (ctx->sector_id, "tavern.round.bought",
                              broadcast_payload);
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "status",
                       json_string ("Round bought successfully!"));
  json_object_set_new (response_data, "cost", json_integer (cost));
  json_object_set_new (response_data, "alignment_gain",
                       json_integer (alignment_gain));


  send_response_ok_take (ctx, root, "tavern.round.buy_v1", &response_data);
  return 0;
}


// Implementation for tavern.loan.take RPC
int
cmd_tavern_loan_take (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return 0;
    }
  if (!is_player_in_tavern_sector (db, ctx->sector_id))
    {
      send_response_error (ctx, root, ERR_NOT_AT_TAVERN,
                           "You are not in a tavern sector.");
      return 0;
    }
  if (!g_tavern_cfg.loan_shark_enabled)
    {
      send_response_error (ctx, root, ERR_TAVERN_LOAN_SHARK_DISABLED,
                           "The Loan Shark is not currently available.");
      return 0;
    }
  long long current_loan_principal = 0;


  if (get_player_loan
        (db, ctx->player_id, &current_loan_principal, NULL, NULL,
        NULL) == DB_OK && current_loan_principal > 0)
    {
      send_response_error (ctx, root, ERR_TAVERN_LOAN_OUTSTANDING,
                           "You already have an outstanding loan.");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data payload.");
      return 0;
    }
  long long loan_amount = 0;


  if (!json_get_int64_flexible (data, "amount", &loan_amount)
      || loan_amount <= 0 || loan_amount > 100000)
    {                           // Example max loan
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "Loan amount must be positive and not exceed 100,000 credits.");
      return 0;
    }
  int interest_rate_bp = 1000;  // Example: 10% interest (1000 basis points)
  time_t now = time (NULL);
  time_t due_date = now + (7 * 24 * 60 * 60);   // Due in 7 days
  // Insert new loan
  const char *sql_insert_loan =
    "INSERT INTO tavern_loans (player_id, principal, interest_rate, due_date, is_defaulted) VALUES ({1}, {2}, {3}, to_timestamp({4}), 0);";
  db_stmt_t *stmt = NULL;
  int rc = db_stmt_prepare_v2 (db, sql_insert_loan, -1, &stmt, NULL);


  if (rc != DB_OK)
    {
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                           "Failed to prepare loan insert.");
      return 0; return 0;
    }
  db_stmt_bind_i32 (stmt, 1, ctx->player_id);
  db_stmt_bind_i64 (stmt, 2, loan_amount);
  db_stmt_bind_i32 (stmt, 3, interest_rate_bp);
  db_stmt_bind_i32 (stmt, 4, (int) due_date);
  if (db_stmt_step (stmt) != DB_DONE)
    {
      db_stmt_finalize (stmt);
      send_response_error (ctx, root, ERR_DB,
                           "Failed to insert loan.");
      return 0; return 0;
    }
  db_stmt_finalize (stmt);
  // Add loan amount to player's credits
  rc = update_player_credits_gambling (db, ctx->player_id, loan_amount, true);  // Add
  if (rc != 0)
    {
      LOGE
        ("cmd_tavern_loan_take: Failed to add loan amount to player credits.");
      // Consider rolling back the loan insert here if transactions were explicit
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "status",
                       json_string ("Loan taken successfully!"));
  json_object_set_new (response_data, "amount", json_integer (loan_amount));
  json_object_set_new (response_data, "interest_rate_bp",
                       json_integer (interest_rate_bp));
  json_object_set_new (response_data, "due_date",
                       json_integer ((long long) due_date));


  send_response_ok_take (ctx, root, "tavern.loan.take_v1", &response_data);
  return 0;
}


// Implementation for tavern.loan.pay RPC
int
cmd_tavern_loan_pay (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return 0;
    }
  if (!is_player_in_tavern_sector (db, ctx->sector_id))
    {
      send_response_error (ctx, root, ERR_NOT_AT_TAVERN,
                           "You are not in a tavern sector.");
      return 0;
    }
  if (!g_tavern_cfg.loan_shark_enabled)
    {
      send_response_error (ctx, root, ERR_TAVERN_LOAN_SHARK_DISABLED,
                           "The Loan Shark is not currently available.");
      return 0;
    }
  long long current_loan_principal = 0;
  int current_loan_interest_rate = 0;
  int current_loan_due_date = 0;
  int current_loan_is_defaulted = 0;


  if (get_player_loan
        (db, ctx->player_id, &current_loan_principal,
        &current_loan_interest_rate, &current_loan_due_date,
        &current_loan_is_defaulted) != DB_OK
      || current_loan_principal <= 0)
    {
      send_response_error (ctx, root, ERR_TAVERN_NO_LOAN,
                           "You do not have an outstanding loan.");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data payload.");
      return 0;
    }
  long long pay_amount = 0;


  if (!json_get_int64_flexible (data, "amount", &pay_amount)
      || pay_amount <= 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "Payment amount must be positive.");
      return 0;
    }
  if (!has_sufficient_funds (db, ctx->player_id, pay_amount))
    {
      send_response_error (ctx, root, ERR_INSUFFICIENT_FUNDS,
                           "Insufficient credits to make payment.");
      return 0;
    }
  // Deduct payment amount from player's credits
  int rc = update_player_credits_gambling (db, ctx->player_id, pay_amount,
                                           false);      // Deduct


  if (rc != 0)
    {
      send_response_error (ctx, root, ERR_DB,
                           "Failed to deduct credits for loan payment.");
      return 0;
    }
  long long new_principal = current_loan_principal - pay_amount;


  if (new_principal < 0)
    {
      new_principal = 0;        // Cannot overpay beyond principal
    }
  const char *sql_update_loan = NULL;
  db_stmt_t *stmt = NULL;


  if (new_principal == 0)
    {
      // Loan fully paid, delete it
      sql_update_loan = "DELETE FROM tavern_loans WHERE player_id = {1};";
      rc = db_stmt_prepare_v2 (db, sql_update_loan, -1, &stmt, NULL);
      if (rc != DB_OK)
        {
          update_player_credits_gambling (db, ctx->player_id, pay_amount, true);        // Refund
          send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                               "Failed to prepare loan delete.");
          return 0;
        }
      db_stmt_bind_i32 (stmt, 1, ctx->player_id);
    }
  else
    {
      // Update remaining principal and reset default status if paying
      sql_update_loan =
        "UPDATE tavern_loans SET principal = {1}, is_defaulted = 0 WHERE player_id = {2};";
      rc = db_stmt_prepare_v2 (db, sql_update_loan, -1, &stmt, NULL);
      if (rc != DB_OK)
        {
          update_player_credits_gambling (db, ctx->player_id, pay_amount, true);        // Refund
          send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                               "Failed to prepare loan update.");
          return 0;
        }
      db_stmt_bind_i64 (stmt, 1, new_principal);
      db_stmt_bind_i32 (stmt, 2, ctx->player_id);
    }
  if (db_stmt_step (stmt) != DB_DONE)
    {
      db_stmt_finalize (stmt);
      stmt = NULL;
      update_player_credits_gambling (db, ctx->player_id, pay_amount, true);    // Refund
      send_response_error (ctx, root, ERR_DB,
                           "Failed to update loan principal.");
      return 0;
    }
  db_stmt_finalize (stmt);
  stmt = NULL;

  json_t *response_data = json_object ();


  json_object_set_new (response_data, "status",
                       json_string (new_principal ==
                                    0 ? "Loan fully paid!" :
                                    "Loan payment successful."));
  json_object_set_new (response_data, "paid_amount",
                       json_integer (pay_amount));
  json_object_set_new (response_data, "remaining_principal",
                       json_integer (new_principal));


  send_response_ok_take (ctx, root, "tavern.loan.pay_v1", &response_data);
  return 0;
}


// Implementation for tavern.rumour.get_hint RPC
int
cmd_tavern_rumour_get_hint (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return 0;
    }
  if (!is_player_in_tavern_sector (db, ctx->sector_id))
    {
      send_response_error (ctx, root, ERR_NOT_AT_TAVERN,
                           "You are not in a tavern sector.");
      return 0;
    }
  long long hint_cost = 50;     // Fixed price for a rumour hint
  // Validate bet limits
  int limit_check = validate_bet_limits (db,
                                         ctx->player_id,
                                         hint_cost);


  if (limit_check == -1)
    {
      send_response_error (ctx, root, ERR_TAVERN_BET_TOO_HIGH,
                           "Hint cost exceeds maximum transaction limit.");
      return 0;
    }
  else if (limit_check == -2)
    {
      send_response_error (ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED,
                           "Daily wager limit exceeded for hint.");
      return 0;
    }
  else if (limit_check == -3)
    {
      send_response_error (ctx, root,
                           ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
                           "Dynamic wager limit exceeded for hint.");
      return 0;
    }
  // Check player funds
  if (!has_sufficient_funds (db, ctx->player_id, hint_cost))
    {
      send_response_error (ctx, root, ERR_INSUFFICIENT_FUNDS,
                           "Insufficient credits to buy a rumour hint.");
      return 0;
    }
  // Deduct cost
  int rc = update_player_credits_gambling (db, ctx->player_id, hint_cost,
                                           false);      // Deduct


  if (rc != 0)
    {
      send_response_error (ctx, root, ERR_DB,
                           "Failed to deduct credits for hint.");
      return 0;
    }
  // Placeholder for generating a real hint. For now, a generic one.
  const char *hint_messages[] = {
    "I heard a Federation patrol is heading towards sector 42.",
    "There's a whisper of rare organics in the outer rim.",
    "The market for equipment on planet X is about to crash.",
    "Beware of pirates near the nebula in sector 103.",
    "Someone saw a derelict Imperial Starship in uncharted space."
  };
  const char *random_hint = hint_messages[get_random_int (0,
                                                          (sizeof
                                                           (hint_messages) /
                                                           sizeof
                                                           (hint_messages[0]))
                                                          - 1)];
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "status",
                       json_string ("Rumour acquired."));
  json_object_set_new (response_data, "hint", json_string (random_hint));
  json_object_set_new (response_data, "cost", json_integer (hint_cost));


  send_response_ok_take (ctx, root, "tavern.rumour.get_hint_v1",
                         &response_data);
  return 0;
}


// Implementation for tavern.barcharts.get_prices_summary RPC
int
cmd_tavern_barcharts_get_prices_summary (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return 0;
    }
  if (!is_player_in_tavern_sector (db, ctx->sector_id))
    {
      send_response_error (ctx, root, ERR_NOT_AT_TAVERN,
                           "You are not in a tavern sector.");
      return 0;
    }
  long long summary_cost = 100; // Fixed price for market summary
  // Validate bet limits
  int limit_check = validate_bet_limits (db, ctx->player_id, summary_cost);


  if (limit_check == -1)
    {
      send_response_error (ctx,
                           root,
                           ERR_TAVERN_BET_TOO_HIGH,
                           "Summary cost exceeds maximum transaction limit.");
      return 0;
    }
  else if (limit_check == -2)
    {
      send_response_error (ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED,
                           "Daily wager limit exceeded for summary.");
      return 0;
    }
  else if (limit_check == -3)
    {
      send_response_error (ctx, root,
                           ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
                           "Dynamic wager limit exceeded for summary.");
      return 0;
    }
  // Check player funds
  if (!has_sufficient_funds (db, ctx->player_id, summary_cost))
    {
      send_response_error (ctx, root, ERR_INSUFFICIENT_FUNDS,
                           "Insufficient credits to buy market summary.");
      return 0;
    }
  // Deduct cost
  int rc = update_player_credits_gambling (db,
                                           ctx->player_id,
                                           summary_cost,
                                           false);      // Deduct


  if (rc != 0)
    {
      send_response_error (ctx, root, ERR_DB,
                           "Failed to deduct credits for summary.");
      return 0;
    }
  json_t *prices_array = json_array ();
  db_stmt_t *stmt = NULL;
  // Placeholder: Query for top commodity prices (simplified for now)
  const char *sql_prices =
    "SELECT p.sector_id, c.name, pt.mode, pt.maxproduct, (c.base_price * (10000 + c.volatility * (RANDOM() % 200 - 100)) / 10000) AS price "
    "FROM ports p JOIN port_trade pt ON p.port_id = pt.port_id JOIN commodities c ON c.code = pt.commodity "
    "ORDER BY price DESC LIMIT 5;";                                                                                                                                                                                                                                                                     // Top 5 prices example


  if (db_stmt_prepare_v2 (db, sql_prices, -1, &stmt, NULL) != DB_OK)
    {
      LOGE
      (
        "cmd_tavern_barcharts_get_prices_summary: Failed to prepare prices query: %s",
        db_errmsg (db));
      update_player_credits_gambling (db, ctx->player_id, summary_cost, true);  // Refund
      send_response_error (ctx, root, ERR_DB,
                           "Failed to retrieve market summary.");
      return 0;
    }
  while (db_stmt_step (stmt) == DB_ROW)
    {
      json_t *price_obj = json_object ();


      json_object_set_new (price_obj, "sector_id",
                           json_integer (db_stmt_col_i32 (stmt, 0)));
      json_object_set_new (price_obj, "commodity",
                           json_string ((const char *)
                                        db_stmt_col_text (stmt, 1)));
      json_object_set_new (price_obj, "type",
                           json_string ((const char *)
                                        db_stmt_col_text (stmt, 2)));
      json_object_set_new (price_obj, "amount",
                           json_integer (db_stmt_col_i32 (stmt, 3)));
      json_object_set_new (price_obj, "price",
                           json_integer (db_stmt_col_i32 (stmt, 4)));
      json_array_append_new (prices_array, price_obj);
    }
  db_stmt_finalize (stmt);
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "status",
                       json_string ("Market summary acquired."));
  json_object_set (response_data, "prices", prices_array);
  json_object_set_new (response_data, "cost", json_integer (summary_cost));


  send_response_ok_take (ctx,
                         root,
                         "tavern.barcharts.get_prices_summary_v1",
                         &response_data);
  return 0;
}

