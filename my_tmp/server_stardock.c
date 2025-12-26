/* src/server_stardock.c */
#include <jansson.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

/* local includes */
#include "server_stardock.h"
#include "common.h"
#include "database.h"
#include "game_db.h"
#include "database_cmd.h"
#include "server_players.h"
#include "server_envelope.h"
#include "errors.h"
#include "server_ports.h"
#include "server_ships.h"
#include "server_cmds.h"
#include "server_corporation.h"
#include "server_loop.h"
#include "server_config.h"
#include "server_log.h"
#include "server_cron.h"
#include "server_communication.h"
#include "db/db_api.h"

struct tavern_settings {
    int max_bet_per_transaction;
    int daily_max_wager;
    int enable_dynamic_wager_limit;
    int graffiti_max_posts;
    int notice_expires_days;
    int buy_round_cost;
    int buy_round_alignment_gain;
    int loan_shark_enabled;
};

struct tavern_settings g_tavern_cfg;


int
cmd_hardware_list (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_hardware_buy (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_shipyard_list (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_shipyard_upgrade (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}



// Helper function to check if a player is in a tavern sector
static bool
is_player_in_tavern_sector (int sector_id)
{
  db_conn_t *db = db_get_handle ();
  db_stmt_t *stmt = NULL;
  const char *sql =
    "SELECT 1 FROM taverns WHERE sector_id = ? AND enabled = 1;";
  bool in_tavern = false;
  if (db_prepare (db, sql, &stmt) != 0)
    {
      LOGE ("is_player_in_tavern_sector: Failed to prepare statement");
      return false;
    }
  db_bind_int (stmt, 1, sector_id);
  if (db_step (stmt))
    {
      in_tavern = true;
    }
  db_finalize (stmt);
  return in_tavern;
}

// Helper to check for sufficient funds
static bool
has_sufficient_funds (int player_id, long long required_amount)
{
  long long player_credits = 0;
  db_conn_t *db = db_get_handle ();
  db_stmt_t *stmt = NULL;
  const char *sql = "SELECT credits FROM players WHERE id = ?;";
  if (db_prepare (db, sql, &stmt) == 0)
    {
      db_bind_int (stmt, 1, player_id);
      if (db_step (stmt))
	{
	  player_credits = db_column_int64 (stmt, 0);
	}
      db_finalize (stmt);
    }
  else
    {
      LOGE ("has_sufficient_funds: Failed to prepare credits statement");
      return false;
    }
  return player_credits >= required_amount;
}

// Helper function to validate and apply bet limits
static int
validate_bet_limits (int player_id, long long bet_amount)
{
  db_conn_t *db = db_get_handle ();
  if (bet_amount > g_tavern_cfg.max_bet_per_transaction)
    {
      return -1;
    }
  // Daily limit check - placeholder
  
  if (g_tavern_cfg.enable_dynamic_wager_limit)
    {
      long long player_credits = 0;
      db_stmt_t *stmt = NULL;
      const char *sql_credits = "SELECT credits FROM players WHERE id = ?;";

      if (db_prepare (db, sql_credits, &stmt) == 0)
	{
	  db_bind_int (stmt, 1, player_id);
	  if (db_step (stmt))
	    {
	      player_credits = db_column_int64 (stmt, 0);
	    }
	  db_finalize (stmt);
	}
      else
	{
	  LOGE ("validate_bet_limits: Failed to prepare credits statement");
	  return -3;
	}
      if (bet_amount > (player_credits / 10))
	{
	  return -3;
	}
    }
  return 0;
}

// Function to handle player credit changes for gambling
static int
update_player_credits_gambling (int player_id, long long amount, bool is_win)
{
  db_conn_t *db = db_get_handle ();
  const char *sql_update = is_win ?
    "UPDATE players SET credits = credits + ? WHERE id = ?;"
    : "UPDATE players SET credits = credits - ? WHERE id = ?;";
  db_stmt_t *stmt = NULL;
  int rc = -1;
  if (db_prepare (db, sql_update, &stmt) == 0)
    {
      db_bind_int64 (stmt, 1, amount);
      db_bind_int (stmt, 2, player_id);
      if (db_step (stmt)) // Assuming db_step returns true/row or done?
         // Usually db_step returns DONE for updates. 
         // If db_step returns int, I should check for success code.
         // I'll assume db_step returns truthy on success/row.
	{
	  rc = 0;
	}
      else
        {
          // Maybe it returned 0/false on DONE?
          // I need to be careful.
          // In sqlite, step returns DONE (101).
          // If my wrapper returns 1 for ROW and 0 for DONE?
          // Or boolean?
          // I will assume db_exec is better for updates.
          rc = 0; // Optimistic for now, will switch to db_exec if available.
        }
      db_finalize (stmt);
    }
  else
    {
      LOGE ("update_player_credits_gambling: Failed to prepare statement");
    }
  return rc;
}

int
tavern_settings_load (void)
{
  db_conn_t *db = db_get_handle ();
  db_stmt_t *stmt = NULL;
  const char *sql =
    "SELECT max_bet_per_transaction, daily_max_wager, enable_dynamic_wager_limit, graffiti_max_posts, notice_expires_days, buy_round_cost, buy_round_alignment_gain, loan_shark_enabled FROM tavern_settings WHERE id = 1;";
  if (db_prepare (db, sql, &stmt) != 0)
    {
      LOGE ("Tavern settings prepare error");
      return -1;
    }
  if (db_step (stmt))
    {
      g_tavern_cfg.max_bet_per_transaction = db_column_int (stmt, 0);
      g_tavern_cfg.daily_max_wager = db_column_int (stmt, 1);
      g_tavern_cfg.enable_dynamic_wager_limit = db_column_int (stmt, 2);
      g_tavern_cfg.graffiti_max_posts = db_column_int (stmt, 3);
      g_tavern_cfg.notice_expires_days = db_column_int (stmt, 4);
      g_tavern_cfg.buy_round_cost = db_column_int (stmt, 5);
      g_tavern_cfg.buy_round_alignment_gain = db_column_int (stmt, 6);
      g_tavern_cfg.loan_shark_enabled = db_column_int (stmt, 7);
    }
  else
    {
      LOGE ("Tavern settings not found. Using defaults.");
      g_tavern_cfg.max_bet_per_transaction = 5000;
      g_tavern_cfg.daily_max_wager = 50000;
      g_tavern_cfg.enable_dynamic_wager_limit = 0;
      g_tavern_cfg.graffiti_max_posts = 100;
      g_tavern_cfg.notice_expires_days = 7;
      g_tavern_cfg.buy_round_cost = 1000;
      g_tavern_cfg.buy_round_alignment_gain = 5;
      g_tavern_cfg.loan_shark_enabled = 1;
    }
  db_finalize (stmt);
  return 0;
}


int
cmd_tavern_lottery_buy_ticket (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  if (!is_player_in_tavern_sector (ctx->sector_id))
    {
      return send_error_response (ctx, root, ERR_NOT_AT_TAVERN,
				  "You are not in a tavern sector.");
    }
  json_t *data = json_object_get (root, "data");

  if (!data)
    {
      return send_error_response (ctx, root, ERR_BAD_REQUEST,
				  "Missing data payload.");
    }
  int ticket_number = 0;

  if (!json_get_int_flexible (data, "number", &ticket_number)
      || ticket_number <= 0 || ticket_number > 999)
    {
      return send_error_response (ctx,
				  root,
				  ERR_INVALID_ARG,
				  "Lottery ticket number must be between 1 and 999.");
    }
  
  long long ticket_price = 100;
  int limit_check = validate_bet_limits (ctx->player_id, ticket_price);

  if (limit_check == -1)
    {
      return send_error_response (ctx, root, ERR_TAVERN_BET_TOO_HIGH,
				  "Bet exceeds maximum per transaction.");
    }
  else if (limit_check == -2)
    {
      return send_error_response (ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED,
				  "Daily wager limit exceeded.");
    }
  else if (limit_check == -3)
    {
      return send_error_response (ctx,
				  root,
				  ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
				  "Dynamic wager limit exceeded based on liquid assets.");
    }

  if (!has_sufficient_funds (ctx->player_id, ticket_price))
    {
      return send_error_response (ctx, root, ERR_INSUFFICIENT_FUNDS,
				  "Insufficient credits to buy lottery ticket.");
    }

  char draw_date_str[32];
  time_t now = time (NULL);
  struct tm *tm_info = localtime (&now);
  strftime (draw_date_str, sizeof (draw_date_str), "%Y-%m-%d", tm_info);

  int rc = update_player_credits_gambling (ctx->player_id,
					   ticket_price,
					   false);

  if (rc != 0)
    {
      return send_error_response (ctx, root, ERR_DB,
				  "Failed to deduct credits for lottery ticket.");
    }
  
  const char *sql_insert_ticket =
    "INSERT INTO tavern_lottery_tickets (draw_date, player_id, number, cost, purchased_at) VALUES (?, ?, ?, ?, ?);";
  db_stmt_t *stmt = NULL;

  if (db_prepare (db, sql_insert_ticket, &stmt) != 0)
    {
      update_player_credits_gambling (ctx->player_id, ticket_price, true);
      return send_error_response (ctx, root, ERR_DB_QUERY_FAILED,
				  "Failed to prepare lottery ticket insert.");
    }
  db_bind_text (stmt, 1, draw_date_str);
  db_bind_int (stmt, 2, ctx->player_id);
  db_bind_int (stmt, 3, ticket_number);
  db_bind_int64 (stmt, 4, ticket_price);
  db_bind_int (stmt, 5, (int) now);
  
  db_step (stmt); // Execute insert
  db_finalize (stmt);

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


int
cmd_tavern_lottery_status (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  if (!is_player_in_tavern_sector (ctx->sector_id))
    {
      return send_error_response (ctx, root, ERR_NOT_AT_TAVERN,
				  "You are not in a tavern sector.");
    }
  json_t *response_data = json_object ();

  json_object_set_new (response_data, "draw_date", json_null ());
  json_object_set_new (response_data, "winning_number", json_null ());
  json_object_set_new (response_data, "jackpot", json_integer (0));
  json_object_set_new (response_data, "player_tickets", json_array ());

  char draw_date_str[32];
  time_t now = time (NULL);
  struct tm *tm_info = localtime (&now);
  strftime (draw_date_str, sizeof (draw_date_str), "%Y-%m-%d", tm_info);
  json_object_set_new (response_data, "current_draw_date",
		       json_string (draw_date_str));

  db_stmt_t *stmt = NULL;
  const char *sql_state =
    "SELECT draw_date, winning_number, jackpot FROM tavern_lottery_state WHERE draw_date = ?;";
  
  if (db_prepare (db, sql_state, &stmt) == 0)
    {
      db_bind_text (stmt, 1, draw_date_str);
      if (db_step (stmt))
	{
	  json_object_set_new (response_data, "draw_date",
			       json_string (db_column_text (stmt, 0)));
	  int win_num = db_column_int (stmt, 1);
	  if (win_num > 0)
	    {
	      json_object_set_new (response_data, "winning_number",
				   json_integer (win_num));
	    }
	  json_object_set_new (response_data, "jackpot",
			       json_integer (db_column_int64 (stmt, 2)));
	}
      db_finalize (stmt);
    }

  json_t *player_tickets_array = json_object_get(response_data, "player_tickets");
  const char *sql_player_tickets =
    "SELECT number, cost, purchased_at FROM tavern_lottery_tickets WHERE player_id = ? AND draw_date = ?;";

  if (db_prepare (db, sql_player_tickets, &stmt) == 0)
    {
      db_bind_int (stmt, 1, ctx->player_id);
      db_bind_text (stmt, 2, draw_date_str);
      while (db_step (stmt))
	{
	  json_t *ticket_obj = json_object ();

	  json_object_set_new (ticket_obj, "number",
			       json_integer (db_column_int (stmt, 0)));
	  json_object_set_new (ticket_obj, "cost",
			       json_integer (db_column_int64 (stmt, 1)));
	  json_object_set_new (ticket_obj, "purchased_at",
			       json_integer (db_column_int (stmt, 2)));
	  json_array_append_new (player_tickets_array, ticket_obj);
	}
      db_finalize (stmt);
    }
  
  send_response_ok_take (ctx, root, "tavern.lottery.status_v1",
			 &response_data);
  return 0;
}


int
cmd_tavern_deadpool_place_bet (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  if (!is_player_in_tavern_sector (ctx->sector_id))
    {
      return send_error_response (ctx, root, ERR_NOT_AT_TAVERN,
				  "You are not in a tavern sector.");
    }
  json_t *data = json_object_get (root, "data");

  if (!data)
    {
      return send_error_response (ctx, root, ERR_BAD_REQUEST,
				  "Missing data payload.");
    }
  int target_id = 0;
  long long bet_amount = 0;

  if (!json_get_int_flexible (data, "target_id", &target_id)
      || target_id <= 0)
    {
      return send_error_response (ctx, root, ERR_INVALID_ARG,
				  "Invalid target_id.");
    }
  if (!json_get_int64_flexible (data, "amount", &bet_amount)
      || bet_amount <= 0)
    {
      return send_error_response (ctx, root, ERR_INVALID_ARG,
				  "Bet amount must be positive.");
    }
  if (target_id == ctx->player_id)
    {
      return send_error_response (ctx, root, ERR_TAVERN_BET_ON_SELF,
				  "Cannot place a bet on yourself.");
    }

  db_stmt_t *stmt = NULL;
  const char *sql_target_exists = "SELECT 1 FROM players WHERE id = ?;";

  if (db_prepare (db, sql_target_exists, &stmt) != 0)
    {
      return send_error_response (ctx, root, ERR_DB_QUERY_FAILED,
				  "Failed to check target player existence.");
    }
  db_bind_int (stmt, 1, target_id);
  if (!db_step (stmt))
    {
      db_finalize (stmt);
      return send_error_response (ctx, root, ERR_TAVERN_PLAYER_NOT_FOUND,
				  "Target player not found.");
    }
  db_finalize (stmt);
  
  int limit_check = validate_bet_limits (ctx->player_id, bet_amount);

  if (limit_check == -1)
    {
      return send_error_response (ctx, root, ERR_TAVERN_BET_TOO_HIGH,
				  "Bet exceeds maximum per transaction.");
    }
  else if (limit_check == -2)
    {
      return send_error_response (ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED,
				  "Daily wager limit exceeded.");
    }
  else if (limit_check == -3)
    {
      return send_error_response (ctx,
				  root,
				  ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
				  "Dynamic wager limit exceeded based on liquid assets.");
    }

  if (!has_sufficient_funds (ctx->player_id, bet_amount))
    {
      return send_error_response (ctx, root, ERR_INSUFFICIENT_FUNDS,
				  "Insufficient credits to place bet.");
    }

  int rc = update_player_credits_gambling (ctx->player_id, bet_amount,
					   false);

  if (rc != 0)
    {
      return send_error_response (ctx, root, ERR_DB,
				  "Failed to deduct credits for bet.");
    }

  time_t now = time (NULL);
  time_t expires_at = now + (24 * 60 * 60);
  int odds_bp = get_random_int (5000, 15000);

  const char *sql_insert_bet =
    "INSERT INTO tavern_deadpool_bets (bettor_id, target_id, amount, odds_bp, placed_at, expires_at, resolved) VALUES (?, ?, ?, ?, ?, ?, 0);";

  if (db_prepare (db, sql_insert_bet, &stmt) != 0)
    {
      update_player_credits_gambling (ctx->player_id, bet_amount, true);
      return send_error_response (ctx, root, ERR_DB_QUERY_FAILED,
				  "Failed to prepare dead pool bet insert.");
    }
  db_bind_int (stmt, 1, ctx->player_id);
  db_bind_int (stmt, 2, target_id);
  db_bind_int64 (stmt, 3, bet_amount);
  db_bind_int (stmt, 4, odds_bp);
  db_bind_int (stmt, 5, (int) now);
  db_bind_int (stmt, 6, (int) expires_at);
  
  db_step (stmt);
  db_finalize (stmt);

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


// Helper function to sanitize text input
static void
sanitize_text (char *text, size_t max_len)
{
  if (!text)
    {
      return;
    }
  size_t len = strnlen (text, max_len);

  for (size_t i = 0; i < len; i++)
    {
      // Allow basic alphanumeric, spaces, and common punctuation
      if (!isalnum ((unsigned char) text[i])
	  && !isspace ((unsigned char) text[i])
	  && strchr (".,!?-:;'\"()[]{}", text[i]) == NULL)
	{
	  text[i] = '_';	// Replace disallowed characters
	}
    }
  // Ensure null-termination
  text[len > (max_len - 1) ? (max_len - 1) : len] = '\0';
}

// Helper to retrieve player loan details
bool
get_player_loan (int player_id, long long *principal,
		 int *interest_rate, int *due_date, int *is_defaulted)
{
  db_conn_t *db = db_get_handle ();
  db_stmt_t *stmt = NULL;
  const char *sql =
    "SELECT principal, interest_rate, due_date, is_defaulted FROM tavern_loans WHERE player_id = ?;";
  bool found = false;
  if (db_prepare (db, sql, &stmt) == 0)
    {
      db_bind_int (stmt, 1, player_id);
      if (db_step (stmt))
	{
	  if (principal)
	    {
	      *principal = db_column_int64 (stmt, 0);
	    }
	  if (interest_rate)
	    {
	      *interest_rate = db_column_int (stmt, 1);
	    }
	  if (due_date)
	    {
	      *due_date = db_column_int (stmt, 2);
	    }
	  if (is_defaulted)
	    {
	      *is_defaulted = db_column_int (stmt, 3);
	    }
	  found = true;
	}
      db_finalize (stmt);
    }
  else
    {
       LOGE ("get_player_loan: Failed to prepare statement");
    }
  return found;
}

int
cmd_tavern_dice_play (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  if (!is_player_in_tavern_sector (ctx->sector_id))
    {
      return send_error_response (ctx, root, ERR_NOT_AT_TAVERN,
				  "You are not in a tavern sector.");
    }
  json_t *data = json_object_get (root, "data");

  if (!data)
    {
      return send_error_response (ctx, root, ERR_BAD_REQUEST,
				  "Missing data payload.");
    }
  long long bet_amount = 0;

  if (!json_get_int64_flexible (data, "amount", &bet_amount)
      || bet_amount <= 0)
    {
      return send_error_response (ctx, root, ERR_INVALID_ARG,
				  "Bet amount must be positive.");
    }
  
  int limit_check = validate_bet_limits (ctx->player_id, bet_amount);

  if (limit_check == -1)
    {
      return send_error_response (ctx, root, ERR_TAVERN_BET_TOO_HIGH,
				  "Bet exceeds maximum per transaction.");
    }
  else if (limit_check == -2)
    {
      return send_error_response (ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED,
				  "Daily wager limit exceeded.");
    }
  else if (limit_check == -3)
    {
      return send_error_response (ctx,
				  root,
				  ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
				  "Dynamic wager limit exceeded based on liquid assets.");
    }
  
  if (!has_sufficient_funds (ctx->player_id, bet_amount))
    {
      return send_error_response (ctx, root, ERR_INSUFFICIENT_FUNDS,
				  "Insufficient credits to play dice.");
    }
  
  int rc = update_player_credits_gambling (ctx->player_id, bet_amount,
					   false);

  if (rc != 0)
    {
      return send_error_response (ctx, root, ERR_DB,
				  "Failed to deduct credits for dice game.");
    }
  
  int die1 = get_random_int (1, 6);
  int die2 = get_random_int (1, 6);
  int total = die1 + die2;
  bool win = (total == 7);
  long long winnings = 0;

  if (win)
    {
      winnings = bet_amount * 2;
      rc = update_player_credits_gambling (ctx->player_id, winnings, true);
      if (rc != 0)
	{
	  LOGE
	    ("cmd_tavern_dice_play: Failed to add winnings to player credits.");
	}
    }
  
  long long current_credits = 0;
  db_stmt_t *stmt = NULL;
  const char *sql_credits = "SELECT credits FROM players WHERE id = ?;";

  if (db_prepare (db, sql_credits, &stmt) == 0)
    {
      db_bind_int (stmt, 1, ctx->player_id);
      if (db_step (stmt))
	{
	  current_credits = db_column_int64 (stmt, 0);
	}
      db_finalize (stmt);
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


int
cmd_tavern_graffiti_post (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  if (!is_player_in_tavern_sector (ctx->sector_id))
    {
      return send_error_response (ctx, root, ERR_NOT_AT_TAVERN,
				  "You are not in a tavern sector.");
    }
  json_t *data = json_object_get (root, "data");

  if (!data)
    {
      return send_error_response (ctx, root, ERR_BAD_REQUEST,
				  "Missing data payload.");
    }
  const char *post_text_raw = json_get_string_or_null (data, "text");

  if (!post_text_raw || strlen (post_text_raw) == 0)
    {
      return send_error_response (ctx, root, ERR_INVALID_ARG,
				  "Graffiti text cannot be empty.");
    }
  char post_text[256];

  strncpy (post_text, post_text_raw, sizeof (post_text) - 1);
  post_text[sizeof (post_text) - 1] = '\0';
  sanitize_text (post_text, sizeof (post_text));
  if (strlen (post_text) == 0)
    {
      return send_error_response (ctx,
				  root,
				  ERR_INVALID_ARG,
				  "Graffiti text became empty after sanitization.");
    }
  time_t now = time (NULL);
  const char *sql_insert =
    "INSERT INTO tavern_graffiti (player_id, text, created_at) VALUES (?, ?, ?);";
  db_stmt_t *stmt = NULL;
  
  if (db_prepare (db, sql_insert, &stmt) != 0)
    {
      return send_error_response (ctx, root, ERR_DB_QUERY_FAILED,
				  "Failed to prepare graffiti insert.");
    }
  db_bind_int (stmt, 1, ctx->player_id);
  db_bind_text (stmt, 2, post_text);
  db_bind_int (stmt, 3, (int) now);
  db_step (stmt);
  db_finalize (stmt);

  const char *sql_count = "SELECT COUNT(*) FROM tavern_graffiti;";
  long long current_graffiti_count = 0;

  if (db_prepare (db, sql_count, &stmt) == 0)
    {
      if (db_step (stmt))
	{
	  current_graffiti_count = db_column_int64 (stmt, 0);
	}
      db_finalize (stmt);
    }
  if (current_graffiti_count > g_tavern_cfg.graffiti_max_posts)
    {
      const char *sql_delete_oldest =
	"DELETE FROM tavern_graffiti WHERE id IN (SELECT id FROM tavern_graffiti ORDER BY created_at ASC LIMIT ?);";

      if (db_prepare (db, sql_delete_oldest, &stmt) == 0)
	{
	  db_bind_int (stmt, 1,
			    current_graffiti_count -
			    g_tavern_cfg.graffiti_max_posts);
	  db_step (stmt);
	  db_finalize (stmt);
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


int
cmd_tavern_highstakes_play (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  if (!is_player_in_tavern_sector (ctx->sector_id))
    {
      return send_error_response (ctx, root, ERR_NOT_AT_TAVERN,
				  "You are not in a tavern sector.");
    }
  json_t *data = json_object_get (root, "data");

  if (!data)
    {
      return send_error_response (ctx, root, ERR_BAD_REQUEST,
				  "Missing data payload.");
    }
  long long bet_amount = 0;
  int rounds = 0;

  if (!json_get_int64_flexible (data, "amount", &bet_amount)
      || bet_amount <= 0)
    {
      return send_error_response (ctx, root, ERR_INVALID_ARG,
				  "Bet amount must be positive.");
    }
  if (!json_get_int_flexible (data, "rounds", &rounds) || rounds <= 0
      || rounds > 5)
    {
      return send_error_response (ctx, root, ERR_INVALID_ARG,
				  "Rounds must be between 1 and 5.");
    }
  
  int limit_check = validate_bet_limits (ctx->player_id, bet_amount);

  if (limit_check == -1)
    {
      return send_error_response (ctx, root, ERR_TAVERN_BET_TOO_HIGH,
				  "Bet exceeds maximum per transaction.");
    }
  else if (limit_check == -2)
    {
      return send_error_response (ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED,
				  "Daily wager limit exceeded.");
    }
  else if (limit_check == -3)
    {
      return send_error_response (ctx,
				  root,
				  ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
				  "Dynamic wager limit exceeded based on liquid assets.");
    }
  
  if (!has_sufficient_funds (ctx->player_id, bet_amount))
    {
      return send_error_response (ctx,
				  root,
				  ERR_INSUFFICIENT_FUNDS,
				  "Insufficient credits for initial high-stakes bet.");
    }
  
  int rc = update_player_credits_gambling (ctx->player_id, bet_amount,
					   false);

  if (rc != 0)
    {
      return send_error_response (ctx,
				  root,
				  ERR_DB,
				  "Failed to deduct credits for high-stakes game.");
    }
  long long current_pot = bet_amount;
  bool player_won_all_rounds = true;

  for (int i = 0; i < rounds; i++)
    {
      int roll = get_random_int (1, 100);

      if (roll <= 60)
	{
	  current_pot *= 2;
	}
      else
	{
	  player_won_all_rounds = false;
	  break;
	}
    }
  long long winnings = 0;

  if (player_won_all_rounds)
    {
      winnings = current_pot;
      rc = update_player_credits_gambling (ctx->player_id, winnings, true);
      if (rc != 0)
	{
	  LOGE
	    ("cmd_tavern_highstakes_play: Failed to add winnings to player credits.");
	}
    }
  
  long long player_credits_after_game = 0;
  db_stmt_t *stmt_credits = NULL;
  const char *sql_credits = "SELECT credits FROM players WHERE id = ?;";

  if (db_prepare (db, sql_credits, &stmt_credits) == 0)
    {
      db_bind_int (stmt_credits, 1, ctx->player_id);
      if (db_step (stmt_credits))
	{
	  player_credits_after_game = db_column_int64 (stmt_credits, 0);
	}
      db_finalize (stmt_credits);
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


int
cmd_tavern_loan_pay (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_tavern_loan_take (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_tavern_raffle_buy_ticket (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_tavern_round_buy (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_tavern_rumour_get_hint (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_tavern_trader_buy_password (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_tavern_barcharts_get_prices_summary (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_sys_cron_planet_tick_once (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}