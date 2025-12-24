/* src/server_players.c */
#include <stdio.h>
#include <stdint.h>
#include <jansson.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <limits.h>

/* local includes */
#include "server_players.h"
#include "database.h"
#include "game_db.h"
#include "database_cmd.h"
#include "errors.h"
#include "config.h"
#include "server_cmds.h"
#include "server_rules.h"
#include "db_player_settings.h"
#include "common.h"
#include "server_envelope.h"
#include "server_auth.h"
#include "server_log.h"
#include "server_ships.h"
#include "server_loop.h"
#include "server_bank.h"
#include "db/db_api.h"

/* Constants */
enum
{ MAX_BOOKMARKS = 64, MAX_BM_NAME = 64 };
enum
{ MAX_AVOIDS = 64 };
static const char *DEFAULT_PLAYER_FIELDS[] = {
  "id",
  "username",
  "credits",
  "sector",
  "faction",
  NULL
};


/* Externs */
extern client_node_t *g_clients;
extern pthread_mutex_t g_clients_mu;


/* ==================================================================== */
/* STATIC HELPER DEFINITIONS                                            */
/* ==================================================================== */

int
h_player_is_npc (db_t *db, int player_id)
{
  if (!db || player_id <= 0) return 0;
  
  const char *sql = "SELECT is_npc FROM players WHERE id = $1;";
  db_bind_t params[] = { db_bind_i32(player_id) };
  db_res_t *res = NULL;
  db_error_t err;
  int is_npc = 0;

  if (db_query(db, sql, params, 1, &res, &err)) {
      if (db_res_step(res, &err)) {
          is_npc = db_res_col_i32(res, 0, &err);
      }
      db_res_finalize(res);
  }
  return is_npc;
}


static int
is_ascii_printable (const char *s)
{
  if (!s) return 0;
  for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
    {
      if (*p < 0x20 || *p > 0x7E) return 0;
    }
  return 1;
}


static int
len_leq (const char *s, size_t m)
{
  return s && strlen (s) <= m;
}


static int
is_valid_key (const char *s, size_t max)
{
  if (!s) return 0;
  size_t n = strlen (s);
  if (n == 0 || n > max) return 0;
  for (size_t i = 0; i < n; ++i)
    {
      char c = s[i];
      if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
            c == '_' || c == '-'))
        {
          return 0;
        }
    }
  return 1;
}

static json_t *
prefs_as_array (db_t *db, int64_t pid)
{
    if (!db) return json_array();
    db_error_t err;
    db_error_clear(&err);

    db_bind_t params[1];
    params[0] = db_bind_i64(pid);

    const char *sql = "SELECT key, type, value FROM player_prefs WHERE player_id = $1;";
    db_res_t *res = NULL;
    if (!db_query(db, sql, params, 1, &res, &err)) {
        LOGE("prefs_as_array: query failed: %s", err.message);
        return json_array();
    }

    json_t *arr = json_array();
    while (db_res_step(res, &err)) {
        const char *k = db_res_col_text(res, 0, &err);
        const char *t = db_res_col_text(res, 1, &err);
        const char *v = db_res_col_text(res, 2, &err);

        json_t *pref_obj = json_object();
        json_object_set_new(pref_obj, "key",   json_string(k ? k : ""));
        json_object_set_new(pref_obj, "type",  json_string(t ? t : "string"));
        json_object_set_new(pref_obj, "value", json_string(v ? v : ""));
        json_array_append_new(arr, pref_obj);
    }
    db_res_finalize(res);
    return arr;
}


static json_t *
bookmarks_as_array (db_t *db, int64_t pid)
{
  if (!db) return json_array ();
  const char *sql = "SELECT name, sector_id FROM player_bookmarks WHERE player_id = $1 ORDER BY name;";
  db_bind_t params[] = { db_bind_i64(pid) };
  db_res_t *res = NULL;
  db_error_t err;
  json_t *arr = json_array ();

  if (db_query(db, sql, params, 1, &res, &err)) {
      while (db_res_step(res, &err)) {
          json_t *bm_obj = json_object ();
          json_object_set_new (bm_obj, "name", json_string (db_res_col_text(res, 0, &err)));
          json_object_set_new (bm_obj, "sector_id", json_integer (db_res_col_i32(res, 1, &err)));
          json_array_append_new (arr, bm_obj);
      }
      db_res_finalize(res);
  }
  return arr;
}


static json_t *
avoid_as_array (db_t *db, int64_t pid)
{
  if (!db) return json_array ();
  const char *sql = "SELECT sector_id FROM player_avoid WHERE player_id = $1 ORDER BY sector_id;";
  db_bind_t params[] = { db_bind_i64(pid) };
  db_res_t *res = NULL;
  db_error_t err;
  json_t *arr = json_array ();

  if (db_query(db, sql, params, 1, &res, &err)) {
      while (db_res_step(res, &err)) {
          json_array_append_new (arr, json_integer (db_res_col_i32(res, 0, &err)));
      }
      db_res_finalize(res);
  }
  return arr;
}


static json_t *
subscriptions_as_array (db_t *db, int64_t pid)
{
    if (!db) return json_array();
    db_error_t err;
    db_error_clear(&err);

    db_bind_t params[1];
    params[0] = db_bind_i64(pid);

    static const char *SQL =
        "SELECT event_type AS topic, locked, enabled, delivery, filter_json "
        "FROM subscriptions WHERE player_id = $1 ORDER BY locked DESC, event_type;";

    db_res_t *res = NULL;
    if (!db_query(db, SQL, params, 1, &res, &err)) {
        return json_array();
    }

    json_t *arr = json_array();
    while (db_res_step(res, &err)) {
        const char *topic    = db_res_col_text(res, 0, &err);
        int locked           = (int) db_res_col_i64(res, 1, &err);
        int enabled          = (int) db_res_col_i64(res, 2, &err);
        const char *delivery = db_res_col_text(res, 3, &err);
        const char *filter   = db_res_col_is_null(res, 4) ? NULL : db_res_col_text(res, 4, &err);

        json_t *one = json_object();
        json_object_set_new(one, "topic",   json_string(topic ? topic : ""));
        json_object_set_new(one, "locked",  json_boolean(locked ? 1 : 0));
        json_object_set_new(one, "enabled", json_boolean(enabled ? 1 : 0));
        json_object_set_new(one, "delivery", json_string(delivery ? delivery : "push"));
        if (filter) json_object_set_new(one, "filter", json_string(filter));
        json_array_append_new(arr, one);
    }
    db_res_finalize(res);
    return arr;
}

/* ==================================================================== */
/* CORE LOGIC HANDLERS                                                  */
/* ==================================================================== */

static const char *
get_turn_error_message (TurnConsumeResult result)
{
  switch (result)
    {
      case TURN_CONSUME_SUCCESS: return "Turn consumed successfully.";
      case TURN_CONSUME_ERROR_DB_FAIL: return "Database failure.";
      case TURN_CONSUME_ERROR_PLAYER_NOT_FOUND: return "Player not found.";
      case TURN_CONSUME_ERROR_NO_TURNS: return "No turns remaining.";
      default: return "Unknown error.";
    }
}

int
handle_turn_consumption_error (client_ctx_t *ctx, TurnConsumeResult consume_result,
                               const char *cmd, json_t *root, json_t *meta_data)
{
  const char *reason_str = "unknown_error";
  if (consume_result == TURN_CONSUME_ERROR_DB_FAIL) reason_str = "db_failure";
  else if (consume_result == TURN_CONSUME_ERROR_PLAYER_NOT_FOUND) reason_str = "player_not_found";
  else if (consume_result == TURN_CONSUME_ERROR_NO_TURNS) reason_str = "no_turns_remaining";

  json_t *meta = meta_data ? json_copy (meta_data) : json_object ();
  if (meta)
    {
      json_object_set_new (meta, "reason", json_string (reason_str));
      json_object_set_new (meta, "command", json_string (cmd));
      send_response_refused_steal (ctx, root, ERR_REF_NO_TURNS, get_turn_error_message (consume_result), NULL);
      json_decref (meta);
    }
  return 0;
}

TurnConsumeResult
h_consume_player_turn (db_t *db, client_ctx_t *ctx, int turns_to_consume)
{
  if (!db || !ctx) return TURN_CONSUME_ERROR_DB_FAIL;
  int player_id = ctx->player_id;
  if (turns_to_consume <= 0) return TURN_CONSUME_ERROR_INVALID_AMOUNT;

  const char *sql = "UPDATE turns SET turns_remaining = turns_remaining - $1, last_update = strftime('%s', 'now') "
                    "WHERE player = $2 AND turns_remaining >= $1 RETURNING turns_remaining;";
  db_bind_t params[] = { db_bind_i32(turns_to_consume), db_bind_i32(player_id) };
  db_res_t *res = NULL;
  db_error_t err;
  TurnConsumeResult result = TURN_CONSUME_ERROR_NO_TURNS;

  if (db_query(db, sql, params, 2, &res, &err)) {
      if (db_res_step(res, &err)) result = TURN_CONSUME_SUCCESS;
      db_res_finalize(res);
  } else result = TURN_CONSUME_ERROR_DB_FAIL;

  return result;
}

int
h_get_player_bank_account_id (db_t *db, int player_id)
{
  int account_id = -1;
  int rc = h_get_account_id_unlocked (db, "player", player_id, &account_id);
  return (rc == 0) ? account_id : -1;
}

int
h_get_credits (db_t *db, const char *owner_type, int owner_id, long long *credits_out)
{
  if (!db || !owner_type || !credits_out) return -1;
  *credits_out = 0;
  const char *sql = "SELECT balance FROM bank_accounts WHERE owner_type = $1 AND owner_id = $2;";
  db_bind_t params[] = { db_bind_text(owner_type), db_bind_i32(owner_id) };
  db_res_t *res = NULL;
  db_error_t err;
  int rc = ERR_DB_NOT_FOUND;
  if (db_query(db, sql, params, 2, &res, &err)) {
      if (db_res_step(res, &err)) {
          *credits_out = db_res_col_i64(res, 0, &err);
          rc = 0;
      }
      db_res_finalize(res);
  } else rc = err.code;
  return rc;
}

int
h_get_cargo_space_free (db_t *db, int player_id, int *free_out)
{
  if (!db || player_id <= 0 || !free_out) return -1;
  *free_out = 0;
  const char *sql = "SELECT ship_holds_capacity, ship_holds_current FROM player_info_v1 WHERE player_id = $1;";
  db_bind_t params[] = { db_bind_i32(player_id) };
  db_res_t *res = NULL;
  db_error_t err;
  int rc = ERR_NOT_FOUND;
  if (db_query(db, sql, params, 1, &res, &err)) {
      if (db_res_step(res, &err)) {
          int cap = db_res_col_i32(res, 0, &err);
          int cur = db_res_col_i32(res, 1, &err);
          *free_out = cap - cur;
          rc = 0;
      }
      db_res_finalize(res);
  } else rc = err.code;
  return rc;
}

int
player_credits (client_ctx_t *ctx)
{
  if (!ctx || ctx->player_id <= 0) return 0;
  db_t *db = game_db_get_handle();
  long long c = 0;
  if (h_get_credits (db, "player", ctx->player_id, &c) != 0) return 0;
  return (int)c;
}

int
cargo_space_free (client_ctx_t *ctx)
{
  if (!ctx || ctx->player_id <= 0) return 0;
  db_t *db = game_db_get_handle();
  int f = 0;
  if (h_get_cargo_space_free (db, ctx->player_id, &f) != 0) return 0;
  return f;
}

int
h_deduct_ship_credits (db_t *db, int player_id, int amount, int *new_balance)
{
  if (!db || player_id <= 0) return -1;
  long long p_bal = 0;
  int rc = h_deduct_credits (db, "player", player_id, (long long) amount, "DEBIT", NULL, &p_bal);
  if (rc == 0 && new_balance) *new_balance = (int) p_bal;
  return rc;
}

int
h_get_player_sector (db_t *db, int player_id)
{
  if (!db || player_id <= 0) return 0;
  const char *sql = "SELECT sector FROM players WHERE id = $1;";
  db_bind_t params[] = { db_bind_i32(player_id) };
  db_res_t *res = NULL;
  db_error_t err;
  int sector = 0;
  if (db_query(db, sql, params, 1, &res, &err)) {
      if (db_res_step(res, &err)) sector = db_res_col_i32(res, 0, &err);
      db_res_finalize(res);
  }
  return sector;
}

int
h_decloak_ship (db_t *db, int ship_id)
{
  if (!db || ship_id <= 0) return ERR_DB_MISUSE;
  db_error_t err;
  int player_id = 0;
  const char *sql_sel = "SELECT player_id FROM ship_ownership WHERE ship_id=$1 AND is_primary=1";
  db_bind_t p_sel[] = { db_bind_i32(ship_id) };
  db_res_t *res_sel = NULL;
  if (db_query(db, sql_sel, p_sel, 1, &res_sel, &err)) {
      if (db_res_step(res_sel, &err)) player_id = db_res_col_i32(res_sel, 0, &err);
      db_res_finalize(res_sel);
  }
  const char *sql_upd = "UPDATE ships SET cloaked = NULL WHERE id = $1 AND cloaked IS NOT NULL";
  db_bind_t p_upd[] = { db_bind_i32(ship_id) };
  int64_t affected = 0;
  if (db_exec_rows_affected(db, sql_upd, p_upd, 1, &affected, &err)) {
      if (affected > 0 && player_id > 0) {
          json_t *payload = json_object();
          json_object_set_new(payload, "ship_id", json_integer(ship_id));
          db_log_engine_event(time(NULL), "ship.decloak", "player", player_id, 0, payload, NULL);
      }
      return 0;
  }
  return err.code;
}

int
h_player_apply_progress (db_t *db, int player_id, long long delta_xp, int delta_align, const char *reason)
{
  if (!db || player_id <= 0) return ERR_DB_MISUSE;
  db_error_t err;
  for (int retry = 0; retry < 3; retry++) {
      if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err)) {
          if (err.code == ERR_DB_BUSY) { usleep (100000); continue; }
          return err.code;
      }
      db_res_t *res = NULL;
      int cur_align = 0; long long cur_xp = 0;
      const char *sql_sel = "SELECT alignment, experience FROM players WHERE id=$1 FOR UPDATE";
      db_bind_t p_sel[] = { db_bind_i32 (player_id) };
      if (!db_query (db, sql_sel, p_sel, 1, &res, &err)) { goto rollback; }
      if (db_res_step (res, &err)) {
          cur_align = db_res_col_i32 (res, 0, &err);
          cur_xp = db_res_col_i64 (res, 1, &err);
      } else { db_res_finalize (res); goto rollback; }
      db_res_finalize (res);
      long long new_xp = cur_xp + delta_xp; if (new_xp < 0) new_xp = 0;
      int new_align = cur_align + delta_align;
      if (new_align > 2000) new_align = 2000; if (new_align < -2000) new_align = -2000;
      const char *sql_upd = "UPDATE players SET experience=$1, alignment=$2 WHERE id=$3";
      db_bind_t p_upd[] = { db_bind_i64 (new_xp), db_bind_i32 (new_align), db_bind_i32 (player_id) };
      if (!db_exec (db, sql_upd, p_upd, 3, &err)) goto rollback;
      if (db_player_update_commission (db, player_id) != 0) goto rollback;
      if (!db_tx_commit (db, &err)) goto rollback;
      LOGD ("Player %d progress updated: %s", player_id, reason ? reason : "N/A");
      return 0;
rollback:
      db_tx_rollback (db, &err);
      if (err.code == ERR_DB_BUSY) { usleep (100000); continue; }
      return err.code;
  }
  return ERR_DB_BUSY;
}

int
h_deduct_player_petty_cash (db_t *db, int player_id, long long amount, long long *new_balance_out)
{
  if (amount < 0) return ERR_DB_MISUSE;
  if (!db) return ERR_DB_CLOSED;
  const char *sql = "UPDATE players SET credits = credits - $1 WHERE id = $2 AND credits >= $1 RETURNING credits;";
  db_bind_t params[] = { db_bind_i64(amount), db_bind_i32(player_id) };
  db_res_t *res = NULL; db_error_t err;
  int rc = ERR_DB_CONSTRAINT;
  if (db_query(db, sql, params, 2, &res, &err)) {
      if (db_res_step(res, &err)) {
          if (new_balance_out) *new_balance_out = db_res_col_i64(res, 0, &err);
          rc = 0;
      }
      db_res_finalize(res);
  } else rc = err.code;
  return rc;
}

int
h_add_player_petty_cash (db_t *db, int player_id, long long amount, long long *new_balance_out)
{
  if (amount < 0) return ERR_DB_MISUSE;
  if (!db) return ERR_DB_CLOSED;
  const char *sql = "UPDATE players SET credits = credits + $1 WHERE id = $2 RETURNING credits;";
  db_bind_t params[] = { db_bind_i64(amount), db_bind_i32(player_id) };
  db_res_t *res = NULL; db_error_t err;
  int rc = ERR_NOT_FOUND;
  if (db_query(db, sql, params, 2, &res, &err)) {
      if (db_res_step(res, &err)) {
          if (new_balance_out) *new_balance_out = db_res_col_i64(res, 0, &err);
          rc = 0;
      }
      db_res_finalize(res);
  } else rc = err.code;
  return rc;
}

int h_add_player_petty_cash_unlocked (db_t *db, int player_id, long long amount, long long *out) { return h_add_player_petty_cash (db, player_id, amount, out); }
int h_deduct_player_petty_cash_unlocked (db_t *db, int player_id, long long amount, long long *out) { return h_deduct_player_petty_cash (db, player_id, amount, out); }

int
auth_player_get_type (int player_id)
{
  db_t *db = game_db_get_handle ();
  if (!db || player_id <= 0) return 0;
  const char *sql = "SELECT type FROM players WHERE id = $1;";
  db_bind_t params[] = { db_bind_i32(player_id) };
  db_res_t *res = NULL; db_error_t err; int type = 0;
  if (db_query(db, sql, params, 1, &res, &err)) {
      if (db_res_step(res, &err)) type = db_res_col_i32(res, 0, &err);
      db_res_finalize(res);
  }
  return type;
}

int
h_get_player_petty_cash (db_t *db, int player_id, long long *credits_out)
{
  if (!db || player_id <= 0 || !credits_out) return ERR_DB_MISUSE;
  const char *sql = "SELECT credits FROM players WHERE id=$1";
  db_bind_t params[] = { db_bind_i32(player_id) };
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, sql, params, 1, &res, &err)) {
      if (db_res_step(res, &err)) {
          *credits_out = db_res_col_i64(res, 0, &err);
          db_res_finalize(res); return 0;
      }
      db_res_finalize(res); return ERR_NOT_FOUND;
  }
  return err.code;
}

int
h_player_build_title_payload (db_t *db, int player_id, json_t **out_json)
{
  if (!db || player_id <= 0 || !out_json) return ERR_DB_MISUSE;
  const char *sql = "SELECT alignment, experience, commission FROM players WHERE id=$1;";
  db_bind_t params[] = { db_bind_i32(player_id) };
  db_res_t *res = NULL; db_error_t err;
  int align = 0, comm_id = 0; long long exp = 0;
  if (db_query(db, sql, params, 1, &res, &err)) {
      if (db_res_step(res, &err)) {
          align = db_res_col_i32(res, 0, &err);
          exp = db_res_col_i64(res, 1, &err);
          comm_id = db_res_col_i32(res, 2, &err);
      } else { db_res_finalize(res); return ERR_NOT_FOUND; }
      db_res_finalize(res);
  } else return err.code;

  char *band_code = NULL, *band_name = NULL;
  int is_good = 0, is_evil = 0, can_iss = 0, can_rob = 0;
  db_alignment_band_for_value (db, align, NULL, &band_code, &band_name, &is_good, &is_evil, &can_iss, &can_rob);
  int det_comm_id = 0, comm_is_evil = 0; char *comm_title = NULL;
  db_commission_for_player (db, is_evil, exp, &det_comm_id, &comm_title, &comm_is_evil);
  if (comm_id != det_comm_id) db_player_update_commission (db, player_id);
  json_t *obj = json_object ();
  json_object_set_new (obj, "title", json_string (comm_title ? comm_title : "Unknown"));
  json_object_set_new (obj, "commission", json_integer (det_comm_id));
  json_object_set_new (obj, "alignment", json_integer (align));
  json_object_set_new (obj, "experience", json_integer (exp));
  json_t *band = json_object ();
  json_object_set_new (band, "code", json_string (band_code ? band_code : "UNKNOWN"));
  json_object_set_new (band, "name", json_string (band_name ? band_name : "Unknown"));
  json_object_set_new (band, "is_good", json_boolean (is_good));
  json_object_set_new (band, "is_evil", json_boolean (is_evil));
  json_object_set_new (band, "can_buy_iss", json_boolean (can_iss));
  json_object_set_new (band, "can_rob_ports", json_boolean (can_rob));
  json_object_set_new (obj, "alignment_band", band);
  if (band_code) free (band_code); if (band_name) free (band_name); if (comm_title) free (comm_title);
  *out_json = obj; return 0;
}

int
h_send_message_to_player (db_t *db, int player_id, int sender_id, const char *subject, const char *message)
{
  if (!db || player_id <= 0) return ERR_DB_CLOSED;
  const char *sql = "INSERT INTO mail (sender_id, recipient_id, subject, body) VALUES ($1, $2, $3, $4);";
  db_bind_t params[] = { db_bind_i32(sender_id), db_bind_i32(player_id), db_bind_text(subject), db_bind_text(message) };
  db_error_t err;
  if (!db_exec(db, sql, params, 4, &err)) return err.code;
  return 0;
}

int
spawn_starter_ship (db_t *db, int player_id, int sector_id)
{
  if (!db || player_id <= 0 || sector_id <= 0) return ERR_INVALID_ARG;
  db_error_t err;
  const char *sql_type = "SELECT id, initialholds, maxfighters, maxshields FROM shiptypes WHERE name = 'Scout Marauder' LIMIT 1;";
  db_res_t *res = NULL; int ship_type_id = -1, holds = 0, fighters = 0, shields = 0;
  if (db_query(db, sql_type, NULL, 0, &res, &err)) {
      if (db_res_step(res, &err)) {
          ship_type_id = db_res_col_i32(res, 0, &err); holds = db_res_col_i32(res, 1, &err);
          fighters = db_res_col_i32(res, 2, &err); shields = db_res_col_i32(res, 3, &err);
      }
      db_res_finalize(res);
  }
  if (ship_type_id == -1) return ERR_NOT_FOUND;
  const char *sql_ins = "INSERT INTO ships (type_id, name, holds, fighters, shields, sector, hull) VALUES ($1, 'Starter Ship', $2, $3, $4, $5, 100);";
  db_bind_t params[] = { db_bind_i32(ship_type_id), db_bind_i32(holds), db_bind_i32(fighters), db_bind_i32(shields), db_bind_i32(sector_id) };
  int64_t ship_id_64 = 0;
  if (!db_exec_insert_id(db, sql_ins, params, 5, &ship_id_64, &err)) return err.code;
  int ship_id = (int)ship_id_64;
  const char *sql_player = "UPDATE players SET ship = $1, sector = $2 WHERE id = $3;";
  db_bind_t p_params[] = { db_bind_i32(ship_id), db_bind_i32(sector_id), db_bind_i32(player_id) };
  if (!db_exec(db, sql_player, p_params, 3, &err)) return err.code;
  const char *sql_own = "INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary) VALUES ($1, $2, 1, 1);";
  db_bind_t o_params[] = { db_bind_i32(ship_id), db_bind_i32(player_id) };
  if (!db_exec(db, sql_own, o_params, 2, &err)) return err.code;
  return 0;
}

int
destroy_ship_and_handle_side_effects (client_ctx_t *ctx, int player_id)
{
  db_t *db = game_db_get_handle (); if (!db) return -1;
  int ship_id = h_get_active_ship_id (db, player_id); if (ship_id <= 0) return 0;
  db_error_t err; if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err)) return err.code;
  bool ok = true;
  db_bind_t s_params[] = { db_bind_i32(ship_id) };
  if (!db_exec(db, "DELETE FROM ships WHERE id = $1;", s_params, 1, &err)) ok = false;
  if (ok) {
      db_bind_t p_params[] = { db_bind_i32(player_id) };
      if (!db_exec(db, "UPDATE players SET ship = NULL WHERE id = $1;", p_params, 1, &err)) ok = false;
  }
  if (!ok) { db_tx_rollback(db, NULL); return err.code; }
  if (!db_tx_commit(db, &err)) return err.code;
  h_send_message_to_player (db, player_id, 0, "Ship Destroyed", "Your ship was destroyed.");
  return 0;
}

/* ==================================================================== */
/* COMMAND HANDLERS                                                     */
/* ==================================================================== */
static int
h_set_prefs (client_ctx_t *ctx, json_t *prefs)
{
  if (!json_is_object (prefs)) return -1;
  db_t *db = game_db_get_handle (); if (!db) return -1;
  void *it = json_object_iter (prefs);
  while (it) {
      const char *key = json_object_iter_key (it);
      json_t *val = json_object_iter_value (it);
      char buf[512] = { 0 }; const char *sval = "";
      if (!is_valid_key (key, 64)) { it = json_object_iter_next (prefs, it); continue; }
      if (json_is_string (val)) sval = json_string_value (val);
      else if (json_is_integer (val)) { snprintf (buf, sizeof (buf), "%lld", (long long) json_integer_value (val)); sval = buf; }
      else if (json_is_boolean (val)) sval = json_is_true (val) ? "1" : "0";
      else { it = json_object_iter_next (prefs, it); continue; }
      db_prefs_set_one (db, ctx->player_id, key, PT_STRING, sval);
      it = json_object_iter_next (prefs, it);
  }
  return 0;
}

static int
h_set_bookmarks (client_ctx_t *ctx, json_t *list)
{
  if (!json_is_array (list)) return -1;
  db_t *db = game_db_get_handle (); if (!db) return -1;
  json_t *curr = bookmarks_as_array (db, ctx->player_id);
  size_t idx; json_t *val;
  json_array_foreach (curr, idx, val) {
    const char *name = json_string_value (json_object_get (val, "name"));
    if (name) db_bookmark_remove (db, ctx->player_id, name);
  }
  json_decref (curr);
  json_array_foreach (list, idx, val) {
    const char *name = json_string_value (json_object_get (val, "name"));
    int sector_id = (int)json_integer_value (json_object_get (val, "sector_id"));
    if (name && sector_id > 0) db_bookmark_upsert (db, ctx->player_id, name, sector_id);
  }
  return 0;
}

static int
h_set_avoids (client_ctx_t *ctx, json_t *list)
{
  if (!json_is_array (list)) return -1;
  db_t *db = game_db_get_handle (); if (!db) return -1;
  json_t *curr = avoid_as_array (db, ctx->player_id);
  size_t idx; json_t *val;
  json_array_foreach (curr, idx, val) {
    if (json_is_integer (val)) db_avoid_remove (db, ctx->player_id, json_integer_value (val));
  }
  json_decref (curr);
  json_array_foreach (list, idx, val) {
    if (json_is_integer (val)) db_avoid_add (db, ctx->player_id, json_integer_value (val));
  }
  return 0;
}

static int
h_set_subscriptions (client_ctx_t *ctx, json_t *list)
{
  if (!json_is_array (list)) return -1;
  db_t *db = game_db_get_handle (); if (!db) return -1;
  json_t *curr = subscriptions_as_array (db, ctx->player_id);
  size_t idx; json_t *val;
  json_array_foreach (curr, idx, val) {
    const char *topic = json_string_value (json_object_get (val, "topic"));
    if (topic) db_subscribe_disable (db, ctx->player_id, topic, NULL);
  }
  json_decref (curr);
  json_array_foreach (list, idx, val) {
    if (json_is_string (val)) db_subscribe_upsert (db, ctx->player_id, json_string_value (val), NULL, 0);
    else if (json_is_object (val)) {
        const char *topic = json_string_value (json_object_get (val, "topic"));
        if (topic) db_subscribe_upsert (db, ctx->player_id, topic, NULL, 0);
    }
  }
  return 0;
}

int
cmd_player_set_settings (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0) { send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required"); return 0; }
  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data)) { send_response_error (ctx, root, ERR_INVALID_SCHEMA, "data must be object"); return 0; }
  json_t *prefs = json_object_get (data, "prefs"); if (prefs) h_set_prefs (ctx, prefs);
  json_t *bookmarks = json_object_get (data, "bookmarks"); if (bookmarks) h_set_bookmarks (ctx, bookmarks);
  json_t *avoid = json_object_get (data, "avoid"); if (avoid) h_set_avoids (ctx, avoid);
  json_t *subs = json_object_get (data, "subscriptions"); if (subs) h_set_subscriptions (ctx, subs);
  json_t *topics = json_object_get (data, "topics"); if (topics) h_set_subscriptions (ctx, topics);
  return cmd_player_get_settings (ctx, root);
}

int
cmd_player_get_settings (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0) { send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required"); return 0; }
  db_t *db = game_db_get_handle();
  json_t *prefs = prefs_as_array (db, ctx->player_id);
  json_t *bm = bookmarks_as_array (db, ctx->player_id);
  json_t *avoid = avoid_as_array (db, ctx->player_id);
  json_t *subs = subscriptions_as_array (db, ctx->player_id);
  json_t *data = json_object ();
  json_object_set_new (data, "prefs", prefs); json_object_set_new (data, "bookmarks", bm);
  json_object_set_new (data, "avoid", avoid); json_object_set_new (data, "subscriptions", subs);
  send_response_ok_take (ctx, root, "player.settings_v1", &data);
  return 0;
}

int
cmd_player_get_prefs (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0) { send_response_refused_steal (ctx, root, ERR_NOT_AUTHENTICATED, "Not authenticated", NULL); return 0; }
  db_t *db = game_db_get_handle();
  json_t *prefs = prefs_as_array (db, ctx->player_id);
  json_t *data = json_object ();
  json_object_set_new (data, "prefs", prefs);
  send_response_ok_take (ctx, root, "player.prefs_v1", &data);
  return 0;
}

int
cmd_player_set_topics (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0) { send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required"); return 0; }
  json_t *data = json_object_get (root, "data");
  json_t *topics = json_object_get (data, "topics");
  if (!topics) topics = json_object_get (data, "subscriptions");
  if (h_set_subscriptions (ctx, topics) != 0) { send_response_error (ctx, root, ERR_INVALID_ARG, "Invalid topics list"); return 0; }
  return cmd_player_get_topics (ctx, root);
}

int
cmd_player_get_topics (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle();
  json_t *topics = subscriptions_as_array (db, ctx->player_id);
  json_t *out = json_object ();
  json_object_set_new (out, "topics", topics ? topics : json_array ());
  send_response_ok_take (ctx, root, "player.subscriptions", &out);
  return 0;
}

int
cmd_player_set_bookmarks (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0) { send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required"); return 0; }
  json_t *data = json_object_get (root, "data");
  json_t *bm = json_object_get (data, "bookmarks");
  if (h_set_bookmarks (ctx, bm) != 0) { send_response_error (ctx, root, ERR_INVALID_ARG, "Invalid bookmarks list"); return 0; }
  return cmd_player_get_bookmarks (ctx, root);
}

int
cmd_player_get_bookmarks (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle();
  json_t *bookmarks = bookmarks_as_array (db, ctx->player_id);
  json_t *out = json_object ();
  json_object_set_new (out, "bookmarks", bookmarks ? bookmarks : json_array ());
  send_response_ok_take (ctx, root, "player.bookmarks", &out);
  return 0;
}

int
cmd_player_set_avoids (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0) { send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required"); return 0; }
  json_t *data = json_object_get (root, "data");
  json_t *av = json_object_get (data, "avoid");
  if (h_set_avoids (ctx, av) != 0) { send_response_error (ctx, root, ERR_INVALID_ARG, "Invalid avoid list"); return 0; }
  return cmd_player_get_avoids (ctx, root);
}

int
cmd_player_get_avoids (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle();
  json_t *avoid = avoid_as_array (db, ctx->player_id);
  json_t *out = json_object ();
  json_object_set_new (out, "avoid", avoid ? avoid : json_array ());
  send_response_ok_take (ctx, root, "avoids", &out);
  return 0;
}

static json_t *
players_list_notes (db_t *db, client_ctx_t *ctx, json_t *req)
{
  (void) db; (void) ctx; (void) req;
  return json_array ();
}

int
cmd_player_get_notes (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle();
  json_t *notes = players_list_notes (db, ctx, root);
  json_t *out = json_object ();
  json_object_set_new (out, "notes", notes ? notes : json_array ());
  send_response_ok_take (ctx, root, "player.notes", &out);
  return 0;
}

int
cmd_player_set_prefs (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0) { send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "auth required"); return -1; }
  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data)) { send_response_error (ctx, root, ERR_INVALID_SCHEMA, "data must be object"); return -1; }
  json_t *patch = json_object_get (data, "patch"); json_t *prefs = json_is_object (patch) ? patch : data;
  db_t *db = game_db_get_handle(); if (!db) return -1;
  void *it = json_object_iter (prefs);
  while (it) {
      const char *key = json_object_iter_key (it); json_t *val = json_object_iter_value (it);
      if (!is_valid_key (key, 64)) { send_response_error (ctx, root, ERR_INVALID_ARG, "invalid key"); return -1; }
      const char *sval = ""; char buf[512] = { 0 };
      if (json_is_string (val)) { sval = json_string_value (val); if (!is_ascii_printable (sval) || strlen (sval) > 256) { send_response_error (ctx, root, ERR_INVALID_ARG, "string invalid"); return -1; } }
      else if (json_is_integer (val)) { snprintf (buf, sizeof (buf), "%lld", (long long) json_integer_value (val)); sval = buf; }
      else if (json_is_boolean (val)) sval = json_is_true (val) ? "1" : "0";
      else { send_response_error (ctx, root, ERR_INVALID_ARG, "unsupported type"); return -1; }
      if (db_prefs_set_one (db, ctx->player_id, key, PT_STRING, sval) != 0) { send_response_error (ctx, root, ERR_UNKNOWN, "db error"); return -1; }
      it = json_object_iter_next (prefs, it);
  }
  json_t *resp = json_object (); json_object_set_new (resp, "ok", json_boolean (1));
  send_response_ok_take (ctx, root, "player.prefs.updated", &resp);
  return 0;
}

void
cmd_nav_bookmark_add (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle (); if (ctx->player_id <= 0) { send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "auth required"); return; }
  json_t *data = json_object_get (root, "data");
  const char *name = json_string_value (json_object_get (data, "name"));
  int sector_id = (int)json_integer_value (json_object_get (data, "sector_id"));
  if (!name || !is_ascii_printable (name) || !len_leq (name, MAX_BM_NAME)) { send_response_error (ctx, root, ERR_INVALID_ARG, "invalid name"); return; }
  if (sector_id <= 0) { send_response_error (ctx, root, ERR_INVALID_ARG, "invalid sector"); return; }
  if (db_bookmark_upsert (db, ctx->player_id, name, sector_id) != 0) { send_response_error (ctx, root, ERR_UNKNOWN, "db error"); return; }
  json_t *resp = json_object (); json_object_set_new (resp, "name", json_string (name)); json_object_set_new (resp, "sector_id", json_integer (sector_id));
  send_response_ok_take (ctx, root, "nav.bookmark.added", &resp);
}

void
cmd_nav_bookmark_remove (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle (); if (ctx->player_id <= 0) return;
  const char *name = json_string_value (json_object_get (json_object_get (root, "data"), "name"));
  if (db_bookmark_remove (db, ctx->player_id, name) != 0) { send_response_error (ctx, root, ERR_NOT_FOUND, "not found"); return; }
  json_t *resp = json_object (); json_object_set_new (resp, "name", json_string (name));
  send_response_ok_take (ctx, root, "nav.bookmark.removed", &resp);
}

void
cmd_nav_avoid_add (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle (); if (ctx->player_id <= 0) return;
  json_t *data = json_object_get (root, "data");
  int sector_id = (int)json_integer_value (json_object_get (data, "sector_id"));
  if (sector_id <= 0) { send_response_error (ctx, root, ERR_BAD_REQUEST, "Invalid sector"); return; }
  if (db_avoid_add (db, ctx->player_id, sector_id) != 0) { send_response_error (ctx, root, ERR_UNKNOWN, "db error"); return; }
  send_response_ok_borrow (ctx, root, "nav.avoid.added", NULL);
}

void
cmd_nav_avoid_remove (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle (); if (ctx->player_id <= 0) return;
  json_t *data = json_object_get (root, "data");
  int sector_id = (int)json_integer_value (json_object_get (data, "sector_id"));
  if (sector_id <= 0) { send_response_error (ctx, root, ERR_BAD_REQUEST, "Invalid sector"); return; }
  if (db_avoid_remove (db, ctx->player_id, sector_id) != 0) { send_response_error (ctx, root, ERR_NOT_FOUND, "Avoid not found"); return; }
  send_response_ok_borrow (ctx, root, "nav.avoid.removed", NULL);
}

int cmd_insurance_policies_list (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }

int cmd_insurance_policies_buy (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }

int cmd_insurance_claim_file (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }

int
cmd_player_list_online (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db) return -1;

  json_t *players_array = json_array ();
  const char *sql =
    "SELECT id, name, alignment, experience, approx_worth "
    "FROM player_info_v1 WHERE id IN (SELECT player_id FROM sessions) "
    "ORDER BY name ASC;";
  db_res_t *res = NULL;
  db_error_t err;

  if (db_query(db, sql, NULL, 0, &res, &err)) {
      while (db_res_step(res, &err)) {
          int pid = db_res_col_i32(res, 0, &err);
          const char *name = db_res_col_text(res, 1, &err);
          int align = db_res_col_i32(res, 2, &err);
          long long exp = db_res_col_i64(res, 3, &err);
          long long nw = db_res_col_i64(res, 4, &err);

          json_t *player_obj = json_object ();
          json_object_set_new (player_obj, "player_id", json_integer (pid));
          json_object_set_new (player_obj, "name", json_string (name ? name : ""));
          json_object_set_new (player_obj, "alignment", json_integer (align));
          json_object_set_new (player_obj, "experience", json_integer (exp));
          json_object_set_new (player_obj, "approx_worth", json_integer (nw));

          json_t *title = NULL;
          if (h_player_build_title_payload (db, pid, &title) == 0 && title)
            {
              json_object_set_new (player_obj, "rank", title);
            }
          json_array_append_new (players_array, player_obj);
      }
      db_res_finalize(res);
  } else {
      json_decref(players_array);
      send_response_error(ctx, root, ERR_DB, "DB error");
      return -1;
  }

  json_t *payload = json_object ();
  json_object_set_new (payload, "online_players", players_array);
  send_response_ok_take (ctx, root, "player.list_online.response", &payload);
  return 0;
}


int
cmd_player_rankings (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Authentication required.");
      return 0;
    }
  db_t *db = game_db_get_handle ();
  if (!db) return -1;

  json_t *data = json_object_get (root, "data");
  const char *order_by = "experience";
  int limit = 10;
  int offset = 0;

  if (json_is_object (data))
    {
      json_t *by = json_object_get (data, "by");
      if (json_is_string (by))
        {
          const char *req = json_string_value (by);
          if (strcmp (req, "experience") == 0 || strcmp (req, "net_worth") == 0) order_by = req;
          else
            {
              send_response_error (ctx, root, ERR_INVALID_ARG, "Invalid criterion");
              return 0;
            }
        }
      json_t *j_lim = json_object_get (data, "limit");
      if (json_is_integer (j_lim)) limit = (int)json_integer_value (j_lim);
      if (limit > 100) limit = 100;
      json_t *j_off = json_object_get (data, "offset");
      if (json_is_integer (j_off)) offset = (int)json_integer_value (j_off);
    }

  char sql[512];
  if (strcmp (order_by, "experience") == 0)
    {
      snprintf (sql, sizeof (sql), "SELECT id, name, alignment, experience FROM players ORDER BY experience DESC LIMIT %d OFFSET %d;", limit, offset);
    }
  else
    {
      snprintf (sql, sizeof (sql),
                "SELECT p.id, p.name, p.alignment, p.experience, COALESCE(ba.balance, 0) as net_worth "
                "FROM players p LEFT JOIN bank_accounts ba ON p.id = ba.owner_id AND ba.owner_type = 'player' "
                "ORDER BY net_worth DESC LIMIT %d OFFSET %d;", limit, offset);
    }

  db_res_t *res = NULL;
  db_error_t err;
  json_t *rankings = json_array ();

  if (db_query(db, sql, NULL, 0, &res, &err)) {
      int rank_num = offset + 1;
      while (db_res_step(res, &err)) {
          int pid = db_res_col_i32(res, 0, &err);
          const char *name = db_res_col_text(res, 1, &err);
          int align = db_res_col_i32(res, 2, &err);
          long long exp = db_res_col_i64(res, 3, &err);

          json_t *entry = json_object ();
          json_object_set_new (entry, "rank", json_integer (rank_num++));
          json_object_set_new (entry, "id", json_integer (pid));
          json_object_set_new (entry, "username", json_string (name ? name : ""));
          json_object_set_new (entry, "alignment", json_integer (align));
          json_object_set_new (entry, "experience", json_integer (exp));

          if (strcmp (order_by, "net_worth") == 0)
            {
              long long nw = db_res_col_i64(res, 4, &err);
              char buf[32];
              snprintf (buf, sizeof (buf), "%lld.00", nw);
              json_object_set_new (entry, "net_worth", json_string (buf));
            }

          json_t *title = NULL;
          if (h_player_build_title_payload (db, pid, &title) == 0 && title)
            {
              json_object_set_new (entry, "title_info", title);
            }
          json_array_append_new (rankings, entry);
      }
      db_res_finalize(res);
  } else {
      json_decref(rankings);
      send_response_error(ctx, root, ERR_DB, "DB error");
      return -1;
  }

  json_t *resp = json_object ();
  json_object_set_new (resp, "rankings", rankings);
  send_response_ok_take (ctx, root, "player.rankings", &resp);
  return 0;
}
void cmd_nav_bookmark_list (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; }

void cmd_nav_avoid_list (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; }

int cmd_get_news (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
