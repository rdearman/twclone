#include <sqlite3.h>
#include <stdio.h>
#include <stdint.h>
#include <jansson.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>

#include "server_players.h"
#include "database.h"
#include "errors.h"
#include "config.h"
#include "server_cmds.h"
#include "server_rules.h"
#include "db_player_settings.h"
#include "server_envelope.h"
#include "server_auth.h"
#include "server_log.h"
#include "common.h"
#include "server_ships.h"

extern sqlite3 *db_get_handle (void);

static const char *
get_turn_error_message (TurnConsumeResult result)
{
  switch (result)
    {
    case TURN_CONSUME_SUCCESS:
      return "Turn consumed successfully.";
    case TURN_CONSUME_ERROR_DB_FAIL:
      return "Database failure prevented turn consumption. Please try again.";
    case TURN_CONSUME_ERROR_PLAYER_NOT_FOUND:
      return "Player entity not found in turn registry.";
    case TURN_CONSUME_ERROR_NO_TURNS:
      return "You have run out of turns and cannot perform this action.";
    default:
      return "An unknown error occurred during turn consumption.";
    }
}

int
handle_turn_consumption_error (client_ctx_t *ctx,
			       TurnConsumeResult consume_result,
			       const char *cmd, json_t *root,
			       json_t *meta_data)
{
  const char *reason_str = NULL;
  switch (consume_result)
    {
    case TURN_CONSUME_ERROR_DB_FAIL:
      reason_str = "db_failure";
      break;
    case TURN_CONSUME_ERROR_PLAYER_NOT_FOUND:
      reason_str = "player_not_found";
      break;
    case TURN_CONSUME_ERROR_NO_TURNS:
      reason_str = "no_turns_remaining";
      break;
    default:
      reason_str = "unknown_error";
      break;
    }

  json_t *meta = meta_data ? json_copy (meta_data) : json_object ();

  if (meta)
    {
      json_object_set_new (meta, "reason", json_string (reason_str));
      json_object_set_new (meta, "command", json_string (cmd));
      const char *user_message = get_turn_error_message (consume_result);
      send_enveloped_refused (ctx->fd, root, ERR_REF_NO_TURNS, user_message,
			      NULL);
      json_decref (meta);
    }
  return 0;
}

TurnConsumeResult
h_consume_player_turn (sqlite3 *db_conn, client_ctx_t *ctx,
		       const char *reason_cmd)
{
  // ... implementation
  return TURN_CONSUME_SUCCESS;
}

int
h_get_credits (sqlite3 *db, const char *owner_type, int owner_id,
	       long long *credits_out)
{
  // ... implementation
  return 0;
}

int
h_get_cargo_space_free (sqlite3 *db, int player_id, int *free_out)
{
  // ... implementation
  return 0;
}

int
player_credits (client_ctx_t *ctx)
{
  // ... implementation
  return 0;
}

int
cargo_space_free (client_ctx_t *ctx)
{
  // ... implementation
  return 0;
}

int
h_update_ship_cargo (sqlite3 *db, int player_id, const char *commodity,
		     int delta, int *new_qty_out)
{
  // ... implementation
  return 0;
}

int
h_deduct_ship_credits (sqlite3 *db, int player_id, int amount,
		       int *new_balance)
{
  long long new_balance_ll = 0;
  int rc =
    h_deduct_credits (db, "player", player_id, amount,
		      "SHIP_CREDIT_DEDUCTION", NULL, &new_balance_ll);
  if (rc == SQLITE_OK && new_balance)
    {
      *new_balance = (int) new_balance_ll;
    }
  return rc;
}

int
h_get_player_sector (int player_id)
{
  // ... implementation
  return 0;
}

int
h_send_message_to_player (int player_id, int sender_id, const char *subject,
			  const char *message)
{
  // ... implementation
  return 0;
}

int
h_decloak_ship (sqlite3 *db, int ship_id)
{
  // ... implementation
  return 0;
}

int
cmd_player_my_info (client_ctx_t *ctx, json_t *root)
{
  // ... implementation
  return 0;
}

int
cmd_player_list_online (client_ctx_t *ctx, json_t *root)
{
  // ... implementation
  return 0;
}

int
cmd_player_set_settings (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "player.set_settings");
  return 0;
}

int
cmd_player_get_prefs (client_ctx_t *ctx, json_t *root)
{
  // ... implementation
  return 0;
}

int
cmd_player_set_topics (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "player.set_topics");
  return 0;
}

int
cmd_player_get_topics (client_ctx_t *ctx, json_t *root)
{
  // ... implementation
  return 0;
}

int
cmd_player_set_bookmarks (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "player.set_bookmarks");
  return 0;
}

int
cmd_player_get_bookmarks (client_ctx_t *ctx, json_t *root)
{
  // ... implementation
  return 0;
}

int
cmd_player_set_avoids (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "player.set_avoids");
  return 0;
}

int
cmd_player_get_avoids (client_ctx_t *ctx, json_t *root)
{
  // ... implementation
  return 0;
}

int
cmd_player_get_notes (client_ctx_t *ctx, json_t *root)
{
  // ... implementation
  return 0;
}

int
cmd_player_set_prefs (client_ctx_t *ctx, json_t *root)
{
  // ... implementation
  return 0;
}

void
cmd_nav_bookmark_add (client_ctx_t *ctx, json_t *root)
{
  // ... implementation
}

void
cmd_nav_bookmark_remove (client_ctx_t *ctx, json_t *root)
{
  // ... implementation
}

void
cmd_nav_bookmark_list (client_ctx_t *ctx, json_t *root)
{
  // ... implementation
}

void
cmd_nav_avoid_add (client_ctx_t *ctx, json_t *root)
{
  // ... implementation
}

void
cmd_nav_avoid_remove (client_ctx_t *ctx, json_t *root)
{
  // ... implementation
}

void
cmd_nav_avoid_list (client_ctx_t *ctx, json_t *root)
{
  // ... implementation
}

int
h_deduct_player_petty_cash (sqlite3 *db, int player_id, long long amount,
			    long long *new_balance_out)
{
  // ... implementation
  return 0;
}

int
h_add_player_petty_cash (sqlite3 *db, int player_id, long long amount,
			 long long *new_balance_out)
{
  // ... implementation
  return 0;
}

int
h_get_player_petty_cash (sqlite3 *db, int player_id, long long *credits_out)
{
  // ... implementation
  return 0;
}

int
cmd_player_get_settings (client_ctx_t *ctx, json_t *root)
{
  // ... implementation
  return 0;
}

int
cmd_bank_history (client_ctx_t *ctx, json_t *root)
{
  // ... implementation
  return 0;
}

int
cmd_bank_leaderboard (client_ctx_t *ctx, json_t *root)
{
  // ... implementation
  return 0;
}

int
cmd_bank_deposit (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, 400, "Missing data object.");
      return 0;
    }

  json_t *j_amount = json_object_get (data, "amount");
  if (!json_is_integer (j_amount) || json_integer_value (j_amount) <= 0)
    {
      send_enveloped_error (ctx->fd, root, 400,
			    "Deposit amount must be a positive integer.");
      return 0;
    }

  long long amount = json_integer_value (j_amount);
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      send_enveloped_error (ctx->fd, root, 500, "No database handle.");
      return 0;
    }

  long long player_petty_cash = 0;
  int rc = h_get_player_petty_cash (db, ctx->player_id, &player_petty_cash);
  if (rc != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 500,
			    "Failed to retrieve petty cash balance.");
      return 0;
    }

  if (player_petty_cash < amount)
    {
      send_enveloped_refused (ctx->fd, root, ERR_INSUFFICIENT_FUNDS,
			      "Insufficient petty cash to deposit.", NULL);
      return 0;
    }

  sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);

  rc = h_deduct_player_petty_cash (db, ctx->player_id, amount, NULL);
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      if (rc == SQLITE_CONSTRAINT)
	{
	  send_enveloped_refused (ctx->fd, root, ERR_INSUFFICIENT_FUNDS,
				  "Insufficient petty cash to deposit.",
				  NULL);
	}
      else
	{
	  send_enveloped_error (ctx->fd, root, 500,
				"Failed to deduct from petty cash.");
	}
      return 0;
    }

  long long new_bank_balance = 0;
  rc =
    h_add_credits (db, "player", ctx->player_id, amount, "DEPOSIT", NULL,
		   &new_bank_balance);

  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, 500,
			    "Failed to deposit credits to bank.");
      return 0;
    }

  sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);

  json_t *payload =
    json_pack ("{s:i, s:I}", "player_id", ctx->player_id, "new_balance",
	       new_bank_balance);
  send_enveloped_ok (ctx->fd, root, "bank.deposit.confirmed", payload);
  json_decref (payload);

  return 0;
}

int
cmd_bank_transfer (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, ERR_NOT_AUTHENTICATED,
			      "Not authenticated", NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_SCHEMA,
			    "data must be object");
      return 0;
    }

  json_t *j_to_player_id = json_object_get (data, "to_player_id");
  json_t *j_amount = json_object_get (data, "amount");

  if (!json_is_integer (j_to_player_id)
      || json_integer_value (j_to_player_id) <= 0)
    {
      send_enveloped_refused (ctx->fd, root, ERR_INVALID_ARG,
			      "Invalid recipient player ID", NULL);
      return 0;
    }

  if (!json_is_integer (j_amount) || json_integer_value (j_amount) <= 0)
    {
      send_enveloped_refused (ctx->fd, root, ERR_INVALID_ARG,
			      "Invalid transfer amount", NULL);
      return 0;
    }

  int to_player_id = json_integer_value (j_to_player_id);
  long long amount = json_integer_value (j_amount);

  if (ctx->player_id == to_player_id)
    {
      send_enveloped_refused (ctx->fd, root, ERR_INVALID_ARG,
			      "Cannot transfer to self", NULL);
      return 0;
    }

  sqlite3 *db = db_get_handle ();
  long long from_balance = 0;
  long long to_balance = 0;
  int rc;
  char tx_group_id[33];
  h_generate_hex_uuid (tx_group_id, sizeof (tx_group_id));

  sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);

  rc = h_get_credits (db, "player", ctx->player_id, &from_balance);
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN,
			    "Failed to get sender balance");
      return 0;
    }
  if (from_balance < amount)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_refused (ctx->fd, root, ERR_INSUFFICIENT_FUNDS,
			      "Insufficient funds", NULL);
      return 0;
    }

  rc =
    h_deduct_credits (db, "player", ctx->player_id, amount, "TRANSFER",
		      tx_group_id, &from_balance);
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN,
			    "Failed to deduct funds");
      return 0;
    }

  rc =
    h_add_credits (db, "player", to_player_id, amount, "TRANSFER",
		   tx_group_id, &to_balance);
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN,
			    "Failed to add funds to recipient");
      return 0;
    }

  sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);

  json_t *payload = json_pack ("{s:i, s:i, s:I, s:I}",
			       "from_player_id", ctx->player_id,
			       "to_player_id", to_player_id,
			       "from_balance", from_balance,
			       "to_balance", to_balance);
  send_enveloped_ok (ctx->fd, root, "bank.transfer.confirmed", payload);
  json_decref (payload);
  return 0;
}

int
cmd_bank_withdraw (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, ERR_NOT_AUTHENTICATED,
			      "Not authenticated", NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_SCHEMA,
			    "data must be object");
      return 0;
    }
  json_t *j_amount = json_object_get (data, "amount");

  if (!json_is_integer (j_amount) || json_integer_value (j_amount) <= 0)
    {
      send_enveloped_refused (ctx->fd, root, ERR_INVALID_ARG,
			      "Invalid withdrawal amount", NULL);
      return 0;
    }

  long long amount = json_integer_value (j_amount);
  sqlite3 *db = db_get_handle ();
  long long new_balance = 0;
  int rc;

  sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);

  rc =
    h_deduct_credits (db, "player", ctx->player_id, amount, "WITHDRAWAL",
		      NULL, &new_balance);
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      if (rc == SQLITE_CONSTRAINT)
	{
	  send_enveloped_refused (ctx->fd, root, ERR_INSUFFICIENT_FUNDS,
				  "Insufficient funds", NULL);
	}
      else
	{
	  send_enveloped_error (ctx->fd, root, ERR_UNKNOWN,
				"Failed to withdraw funds");
	}
      return 0;
    }

  rc = h_add_player_petty_cash (db, ctx->player_id, amount, NULL);
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN,
			    "Failed to add to petty cash");
      return 0;
    }

  sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);

  json_t *payload = json_pack ("{s:I}", "new_balance", new_balance);
  send_enveloped_ok (ctx->fd, root, "bank.withdraw.confirmed", payload);
  json_decref (payload);
  return 0;
}

int
h_get_player_bank_account_id (sqlite3 *db, int player_id)
{
  sqlite3_stmt *st = NULL;
  int account_id = -1;
  const char *sql =
    "SELECT id FROM bank_accounts WHERE owner_type = 'player' AND owner_id = ?;";

  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      LOGE ("h_get_player_bank_account_id: Failed to prepare statement: %s",
	    sqlite3_errmsg (db));
      return -1;
    }

  sqlite3_bind_int (st, 1, player_id);

  if (sqlite3_step (st) == SQLITE_ROW)
    {
      account_id = sqlite3_column_int (st, 0);
    }

  sqlite3_finalize (st);
  return account_id;
}

int
cmd_bank_balance (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "bank.balance");
  return 0;
}

int
spawn_starter_ship (sqlite3 *db, int player_id, int sector_id)
{
  // ... implementation
  return 0;
}

int
destroy_ship_and_handle_side_effects (client_ctx_t *ctx, int sector_id,
				      int player_id)
{
  // ... implementation
  return 0;
}
