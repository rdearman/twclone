#include "db/repo/repo_corporation.h"
#include "db/repo/repo_cmd.h"
#include <jansson.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <strings.h>
#include <pthread.h>
#include <ctype.h>		// Required for isalnum and isupper
#include "server_corporation.h"
#include "server_config.h"
#include "db/repo/repo_database.h"
#include "game_db.h"
#include "server_log.h"
#include "server_envelope.h"
#include "errors.h"
#include "server_players.h"
#include "common.h"
#include "server_cron.h"
#include "db/sql_driver.h"


int
h_get_player_corp_role (db_t *db, int player_id, int corp_id,
			char *role_buffer, size_t buffer_size)
{
  return repo_corp_get_player_role (db, player_id, corp_id, role_buffer,
				    buffer_size);
}


int
h_is_player_corp_ceo (db_t *db, int player_id, int *out_corp_id)
{
  if (!db || player_id <= 0)
    {
      return 0;
    }
  return repo_corp_is_player_ceo (db, player_id, out_corp_id);
}


int
h_get_player_corp_id (db_t *db, int player_id)
{
  return repo_corp_get_player_corp_id (db, player_id);
}


int
h_get_corp_bank_account_id (db_t *db, int corp_id)
{
  return repo_corp_get_bank_account_id (db, corp_id);
}


int
h_get_corp_credit_rating (db_t *db, int corp_id, int *rating)
{
  return repo_corp_get_credit_rating (db, corp_id, rating);
}


int
h_get_corp_stock_id (db_t *db, int corp_id, int *out_stock_id)
{
  return repo_corp_get_stock_id (db, corp_id, out_stock_id);
}


int
h_get_stock_info (db_t *db, int stock_id, char **out_ticker,
		  int *out_corp_id, int *out_total_shares,
		  int *out_par_value, int *out_current_price,
		  long long *out_last_dividend_ts)
{
  return repo_corp_get_stock_info (db, stock_id, out_ticker, out_corp_id,
				   out_total_shares, out_par_value,
				   out_current_price, out_last_dividend_ts);
}


int
h_update_player_shares (db_t *db, int player_id, int stock_id,
			int quantity_change)
{
  if (quantity_change == 0)
    {
      return 0;			// No change needed
    }


  if (quantity_change > 0)
    {
      int rc =
	repo_corp_add_shares (db, player_id, stock_id, quantity_change);
      if (rc != 0)
	{
	  LOGE ("h_update_player_shares: Failed to add shares: %d", rc);
	  return rc;
	}
    }
  else
    {
      int64_t rows_affected = 0;
      int rc =
	repo_corp_deduct_shares (db, player_id, stock_id, quantity_change,
				 &rows_affected);

      if (rc != 0)
	{
	  LOGE ("h_update_player_shares: Failed to deduct shares: %d", rc);
	  return rc;
	}

      if (rows_affected == 0)
	{
	  LOGW
	    ("h_update_player_shares: Player %d has insufficient shares for stock %d.",
	     player_id, stock_id);
	  return ERR_DB_CONSTRAINT;
	}
    }

  // Clean up 0-share entries
  repo_corp_delete_zero_shares (db);
  return 0;
}


int
cmd_corp_transfer_ceo (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *data = root ? json_object_get (root, "data") : NULL;


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "Missing data payload.");
      return 0;
    }
  int target_player_id = 0;


  if (!json_get_int_flexible (data, "target_player_id", &target_player_id) ||
      target_player_id <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_MISSING_FIELD,
			   "Missing or invalid 'target_player_id'.");
      return 0;
    }
  if (target_player_id == ctx->player_id)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG,
			   "Cannot transfer CEO role to yourself.");
      return 0;
    }
  /* Ensure caller is an active CEO and grab corp_id */
  int corp_id = 0;


  if (!h_is_player_corp_ceo (db, ctx->player_id, &corp_id) || corp_id <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_PERMISSION_DENIED,
			   "Only active corporation CEOs may transfer leadership.");
      return 0;
    }
  /* Ensure target is a member of the same corp */
  char target_role_buf[16];
  const char *target_role = NULL;

  if (repo_corp_check_member_role
      (db, corp_id, target_player_id, target_role_buf,
       sizeof (target_role_buf)) == 0)
    {
      target_role = target_role_buf;
    }

  if (!target_role)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG,
			   "Target player is not a member of your corporation.");
      return 0;
    }

  /* Guard: current CEO must NOT be flying the Corporate Flagship */
  char ship_name_buf[128];
  bool is_flagship = false;

  if (repo_corp_get_player_ship_type_name
      (db, ctx->player_id, ship_name_buf, sizeof (ship_name_buf)) == 0)
    {
      if (ship_name_buf[0] != '\0'
	  && !strcasecmp (ship_name_buf, "Corporate Flagship"))
	{
	  is_flagship = true;
	}
    }

  if (is_flagship)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_CORP_STATE,
			   "You cannot transfer CEO while piloting the Corporate Flagship.");
      return 0;
    }

  /* Perform the transfer in a transaction */
  db_error_t err;
  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, err.code,
			   "Failed to start transaction.");
      return 0;
    }

  bool ok = true;

  /* Demote current CEO to Officer */
  if (repo_corp_demote_ceo (db, corp_id, ctx->player_id) != 0)
    {
      ok = false;
    }

  /* Ensure target has a membership row */
  if (ok)
    {
      if (repo_corp_insert_member_ignore
	  (db, corp_id, target_player_id, "Member") != 0)
	{
	  ok = false;
	}
    }

  /* Promote target to Leader */
  if (ok)
    {
      if (repo_corp_promote_leader (db, corp_id, target_player_id) != 0)
	{
	  ok = false;
	}
    }

  /* Update corporations.owner_id */
  if (ok)
    {
      if (repo_corp_update_owner (db, corp_id, target_player_id) != 0)
	{
	  ok = false;
	}
    }

  if (!ok)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_DB, "Failed to transfer CEO role.");
      return 0;
    }

  if (!db_tx_commit (db, &err))
    {
      send_response_error (ctx, root, err.code,
			   "Failed to commit transaction.");
      return 0;
    }

  json_t *resp = json_object ();


  json_object_set_new (resp, "corp_id", json_integer (corp_id));
  json_object_set_new (resp, "new_ceo_player_id",
		       json_integer (target_player_id));
  send_response_ok_take (ctx, root, "corp.transfer_ceo.success", &resp);
  return 0;
}


int
cmd_corp_create (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "Missing data object.");
      return 0;
    }
  const char *name;
  json_t *j_name = json_object_get (data, "name");


  if (!json_is_string (j_name) || (name = json_string_value (j_name)) == NULL
      || name[0] == '\0')
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_BAD_REQUEST,
				   "Missing or invalid corporation name.",
				   NULL);
      return 0;
    }

  if (h_get_player_corp_id (db, ctx->player_id) > 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_INVALID_ARG,
				   "You are already a member of a corporation.",
				   NULL);
      return 0;
    }

  long long creation_fee = g_cfg.corporation_creation_fee;
  long long player_new_balance;
  int player_bank_account_id = 0;


  if (h_get_account_id_unlocked (db,
				 "player",
				 ctx->player_id,
				 &player_bank_account_id) != 0)
    {
      player_bank_account_id = 0;	// Ensure 0 on failure
    }


  if (player_bank_account_id <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_DB, "Could not retrieve player bank account.");
      return 0;
    }

  db_error_t err;


  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, err.code, "Database busy (create)");
      return 0;
    }

  if (h_deduct_credits_unlocked (db,
				 player_bank_account_id,
				 creation_fee,
				 "CORP_CREATION_FEE",
				 NULL, &player_new_balance) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_refused_steal (ctx,
				   root,
				   ERR_INSUFFICIENT_FUNDS,
				   "Insufficient funds.", NULL);
      return 0;
    }

  int64_t new_corp_id = 0;
  int rc = repo_corp_create (db, name, ctx->player_id, &new_corp_id);
  if (rc != 0)
    {
      db_tx_rollback (db, NULL);
      if (rc == ERR_DB_CONSTRAINT)
	{
	  send_response_refused_steal (ctx,
				       root,
				       ERR_NAME_TAKEN,
				       "A corporation with that name already exists.",
				       NULL);
	}
      else
	{
	  send_response_error (ctx, root, rc, "Database error.");
	}
      return 0;
    }

  int corp_id = (int) new_corp_id;

  if (repo_corp_insert_member (db, corp_id, ctx->player_id, "Leader") != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_DB, "Failed to add CEO.");
      return 0;
    }

  if (repo_corp_create_bank_account (db, corp_id) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_DB, "Failed to create bank.");
      return 0;
    }

  if (repo_corp_transfer_planets_to_corp (db, corp_id, ctx->player_id) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_DB, "Failed to update planets.");
      return 0;
    }

  if (!db_tx_commit (db, &err))
    {
      send_response_error (ctx, root, err.code, "Commit failed.");
      return 0;
    }

  ctx->corp_id = corp_id;
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "corp_id", json_integer (corp_id));
  json_object_set_new (response_data, "name", json_string (name));
  send_response_ok_take (ctx, root, "corp.create.success", &response_data);
  return 0;
}


int
cmd_corp_join (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "Missing data object.");
      return 0;
    }
  int corp_id;
  json_t *j_corp_id = json_object_get (data, "corp_id");


  if (!json_is_integer (j_corp_id))
    {
      send_response_error (ctx,
			   root,
			   ERR_BAD_REQUEST, "Missing or invalid 'corp_id'.");
      return 0;
    }
  corp_id = (int) json_integer_value (j_corp_id);
  if (h_get_player_corp_id (db, ctx->player_id) > 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG,
			   "You are already in a corporation.");
      return 0;
    }

  long long expires_at = 0;
  if (repo_corp_get_invite_expiry (db, corp_id, ctx->player_id, &expires_at)
      != 0)
    {
      expires_at = 0;
    }

  if (expires_at == 0 || expires_at < (long long) time (NULL))
    {
      send_response_error (ctx,
			   root,
			   ERR_PERMISSION_DENIED,
			   "You do not have a valid invitation to join this corporation.");
      return 0;
    }

  db_error_t err;
  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, err.code, "Database busy (join)");
      return 0;
    }

  if (repo_corp_insert_member_basic (db, corp_id, ctx->player_id, "Member") !=
      0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx,
			   root,
			   ERR_DB,
			   "Database error while joining corporation.");
      return 0;
    }

  if (repo_corp_delete_invite (db, corp_id, ctx->player_id) != 0)
    {
      // Not critical if delete fails, but log it
      LOGW ("cmd_corp_join: Failed to delete invite");
    }

  if (!db_tx_commit (db, &err))
    {
      send_response_error (ctx, root, err.code, "Commit failed (join)");
      return 0;
    }

  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
		       json_string ("Successfully joined the corporation."));
  json_object_set_new (response_data, "corp_id", json_integer (corp_id));
  send_response_ok_take (ctx, root, "corp.join.success", &response_data);
  ctx->corp_id = corp_id;
  return 0;
}


int
cmd_corp_list (client_ctx_t *ctx, json_t *root)
{
  (void) ctx;
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *corp_array = json_array ();
  db_res_t *res = NULL;
  db_error_t err;


  if ((res = repo_corp_list (db, &err)) != NULL)
    {
      while (db_res_step (res, &err))
	{
	  json_t *corp_obj = json_object ();


	  json_object_set_new (corp_obj, "corp_id",
			       json_integer (db_res_col_i32 (res, 0, &err)));
	  json_object_set_new (corp_obj, "name",
			       json_string (db_res_col_text (res, 1, &err)));
	  const char *tag = db_res_col_text (res, 2, &err);


	  if (tag)
	    {
	      json_object_set_new (corp_obj, "tag", json_string (tag));
	    }
	  json_object_set_new (corp_obj, "ceo_id",
			       json_integer (db_res_col_i32 (res, 3, &err)));
	  const char *ceo_name = db_res_col_text (res, 4, &err);


	  if (ceo_name)
	    {
	      json_object_set_new (corp_obj, "ceo_name",
				   json_string (ceo_name));
	    }
	  json_object_set_new (corp_obj, "member_count",
			       json_integer (db_res_col_i32 (res, 5, &err)));
	  json_array_append_new (corp_array, corp_obj);
	}
      db_res_finalize (res);
    }
  else
    {
      LOGE ("cmd_corp_list: Query failed: %s", err.message);
      json_decref (corp_array);
      send_response_error (ctx,
			   root,
			   ERR_DB,
			   "Database error while fetching corporation list.");
      return 0;
    }

  json_t *response_data = json_object ();


  json_object_set_new (response_data, "corporations", corp_array);
  send_response_ok_take (ctx, root, "corp.list.success", &response_data);
  return 0;
}


int
cmd_corp_roster (client_ctx_t *ctx, json_t *root)
{
  (void) ctx;
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "Missing data object.");
      return 0;
    }
  int corp_id;
  json_t *j_corp_id = json_object_get (data, "corp_id");


  if (!json_is_integer (j_corp_id))
    {
      send_response_error (ctx,
			   root,
			   ERR_BAD_REQUEST, "Missing or invalid 'corp_id'.");
      return 0;
    }
  corp_id = (int) json_integer_value (j_corp_id);
  json_t *roster_array = json_array ();
  db_res_t *res = NULL;
  db_error_t err;


  if ((res = repo_corp_roster (db, corp_id, &err)) != NULL)
    {
      while (db_res_step (res, &err))
	{
	  json_t *member_obj = json_object ();


	  json_object_set_new (member_obj, "player_id",
			       json_integer (db_res_col_i32 (res, 0, &err)));
	  json_object_set_new (member_obj, "name",
			       json_string (db_res_col_text (res, 1, &err)));
	  json_object_set_new (member_obj, "role",
			       json_string (db_res_col_text (res, 2, &err)));
	  json_array_append_new (roster_array, member_obj);
	}
      db_res_finalize (res);
    }
  else
    {
      LOGE ("cmd_corp_roster: Query failed: %s", err.message);
      json_decref (roster_array);
      send_response_error (ctx,
			   root,
			   ERR_DB, "Database error while fetching roster.");
      return 0;
    }

  json_t *response_data = json_object ();


  json_object_set_new (response_data, "corp_id", json_integer (corp_id));
  json_object_set_new (response_data, "roster", roster_array);
  send_response_ok_take (ctx, root, "corp.roster.success", &response_data);
  return 0;
}


int
cmd_corp_kick (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "Missing data object.");
      return 0;
    }
  int target_player_id;
  json_t *j_target_id = json_object_get (data, "target_player_id");


  if (!json_is_integer (j_target_id))
    {
      send_response_error (ctx,
			   root,
			   ERR_BAD_REQUEST,
			   "Missing or invalid 'target_player_id'.");
      return 0;
    }
  target_player_id = (int) json_integer_value (j_target_id);
  if (ctx->player_id == target_player_id)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG, "You cannot kick yourself.");
      return 0;
    }
  int kicker_corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (kicker_corp_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG, "You are not in a corporation.");
      return 0;
    }
  int target_corp_id = h_get_player_corp_id (db, target_player_id);


  if (target_corp_id != kicker_corp_id)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG,
			   "Target player is not in your corporation.");
      return 0;
    }
  char kicker_role[16];
  char target_role[16];


  h_get_player_corp_role (db,
			  ctx->player_id,
			  kicker_corp_id, kicker_role, sizeof (kicker_role));
  h_get_player_corp_role (db,
			  target_player_id,
			  target_corp_id, target_role, sizeof (target_role));
  bool can_kick = false;


  if (strcasecmp (kicker_role, "Leader") == 0 && (strcasecmp (target_role,
							      "Officer") == 0
						  || strcasecmp (target_role,
								 "Member") ==
						  0))
    {
      can_kick = true;
    }
  else if (strcasecmp (kicker_role, "Officer") == 0
	   && strcasecmp (target_role, "Member") == 0)
    {
      can_kick = true;
    }
  if (!can_kick)
    {
      send_response_error (ctx,
			   root,
			   ERR_PERMISSION_DENIED,
			   "Your rank is not high enough to kick this member.");
      return 0;
    }

  int rc = repo_corp_delete_member (db, kicker_corp_id, target_player_id);
  if (rc != 0)
    {
      LOGE ("cmd_corp_kick: Failed to delete member: %d", rc);
      send_response_error (ctx,
			   root,
			   ERR_DB, "Database error while kicking member.");
      return 0;
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
		       json_string
		       ("Player successfully kicked from the corporation."));
  json_object_set_new (response_data, "kicked_player_id",
		       json_integer (target_player_id));
  send_response_ok_take (ctx, root, "corp.kick.success", &response_data);
  return 0;
}


int
cmd_corp_dissolve (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  int corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (corp_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG, "You are not in a corporation.");
      return 0;
    }
  char role[16];


  h_get_player_corp_role (db, ctx->player_id, corp_id, role, sizeof (role));
  if (strcasecmp (role, "Leader") != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_PERMISSION_DENIED,
			   "Only the corporation's leader can dissolve it.");
      return 0;
    }

  db_error_t err;


  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, err.code, "Database busy (dissolve)");
      return 0;
    }

  if (repo_corp_transfer_planets_to_player (db, corp_id) != 0)
    {
      db_tx_rollback (db, NULL);
      LOGE
	("cmd_corp_dissolve: Failed to update planet ownership for corp %d",
	 corp_id);
      send_response_error (ctx, root, ERR_DB,
			   "Database error during corporation dissolution.");
      return 0;
    }

  if (repo_corp_delete (db, corp_id) != 0)
    {
      db_tx_rollback (db, NULL);
      LOGE ("cmd_corp_dissolve: Failed to delete corporation %d", corp_id);
      send_response_error (ctx,
			   root,
			   ERR_DB,
			   "Database error during corporation dissolution.");
      return 0;
    }

  if (!db_tx_commit (db, &err))
    {
      send_response_error (ctx, root, err.code, "Commit failed (dissolve)");
      return 0;
    }

  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
		       json_string ("Corporation has been dissolved."));
  json_object_set_new (response_data,
		       "dissolved_corp_id", json_integer (corp_id));
  send_response_ok_take (ctx, root, "corp.dissolve.success", &response_data);
  ctx->corp_id = 0;
  return 0;
}


int
cmd_corp_leave (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  int corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (corp_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG, "You are not in a corporation.");
      return 0;
    }
  char role[16];


  h_get_player_corp_role (db, ctx->player_id, corp_id, role, sizeof (role));
  if (strcasecmp (role, "Leader") == 0)
    {
      int member_count = 0;
      if (repo_corp_get_member_count (db, corp_id, &member_count) != 0)
	{
	  member_count = 0;
	}

      if (member_count > 1)
	{
	  send_response_error (ctx,
			       root,
			       ERR_INVALID_ARG,
			       "You must transfer leadership before leaving the corporation.");
	  return 0;
	}

      db_error_t err;
      if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
	{
	  send_response_error (ctx, root, err.code, "Database busy (leave)");
	  return 0;
	}

      if (repo_corp_delete (db, corp_id) != 0)
	{
	  db_tx_rollback (db, NULL);
	  send_response_error (ctx,
			       root,
			       ERR_DB, "Database error during dissolution.");
	  return 0;
	}

      if (!db_tx_commit (db, &err))
	{
	  send_response_error (ctx, root, err.code, "Commit failed (leave)");
	  return 0;
	}

      json_t *response_data = json_object ();


      json_object_set_new (response_data, "message",
			   json_string
			   ("You were the last member. The corporation has been dissolved."));
      send_response_ok_take (ctx, root, "corp.leave.dissolved",
			     &response_data);
    }
  else
    {
      if (repo_corp_delete_member (db, corp_id, ctx->player_id) != 0)
	{
	  send_response_error (ctx,
			       root,
			       ERR_DB,
			       "Database error while leaving corporation.");
	  return 0;
	}
      json_t *response_data = json_object ();


      json_object_set_new (response_data, "message",
			   json_string ("You have left the corporation."));
      send_response_ok_take (ctx, root, "corp.leave.success", &response_data);
    }
  ctx->corp_id = 0;
  return 0;
}


int
cmd_corp_invite (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "Missing data object.");
      return 0;
    }
  int target_player_id;
  json_t *j_target_id = json_object_get (data, "target_player_id");


  if (!json_is_integer (j_target_id))
    {
      send_response_error (ctx,
			   root,
			   ERR_BAD_REQUEST,
			   "Missing or invalid 'target_player_id'.");
      return 0;
    }
  target_player_id = (int) json_integer_value (j_target_id);
  if (ctx->player_id == target_player_id)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG, "You cannot invite yourself.");
      return 0;
    }
  int inviter_corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (inviter_corp_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG,
			   "You must be in a corporation to send invites.");
      return 0;
    }
  char inviter_role[16];


  h_get_player_corp_role (db,
			  ctx->player_id,
			  inviter_corp_id,
			  inviter_role, sizeof (inviter_role));
  if (strcasecmp (inviter_role, "Leader") != 0 && strcasecmp (inviter_role,
							      "Officer") != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_PERMISSION_DENIED,
			   "You must be a Leader or Officer to invite players.");
      return 0;
    }
  if (h_get_player_corp_id (db, target_player_id) > 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG,
			   "The player you are trying to invite is already in a corporation.");
      return 0;
    }
  long long expires_at = (long long) time (NULL) + 86400;
  int rc =
    repo_corp_upsert_invite (db, inviter_corp_id, target_player_id,
			     time (NULL), expires_at);

  if (rc != 0)
    {
      LOGE ("cmd_corp_invite: Failed to insert invite: %d", rc);
      send_response_error (ctx,
			   root,
			   ERR_DB,
			   "Database error while sending invitation.");
      return 0;
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
		       json_string ("Invitation sent successfully."));
  json_object_set_new (response_data, "corp_id",
		       json_integer (inviter_corp_id));
  json_object_set_new (response_data, "target_player_id",
		       json_integer (target_player_id));
  send_response_ok_take (ctx, root, "corp.invite.success", &response_data);
  return 0;
}


int
cmd_corp_balance (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  int corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (corp_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG, "You are not in a corporation.");
      return 0;
    }
  long long balance = 0;


  if (db_get_corp_bank_balance (db, corp_id, &balance) != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_SERVER_ERROR,
			   "Failed to retrieve corporation balance.");
      return 0;
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "corp_id", json_integer (corp_id));
  json_object_set_new (response_data, "balance", json_integer (balance));
  send_response_ok_take (ctx, root, "corp.balance.success", &response_data);
  return 0;
}


int
cmd_corp_deposit (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "Missing data payload.");
      return 0;
    }
  long long amount = 0;
  json_t *j_amount = json_object_get (data, "amount");


  if (json_is_integer (j_amount))
    {
      amount = json_integer_value (j_amount);
    }
  if (amount <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_MISSING_FIELD, "Missing or invalid 'amount'.");
      return 0;
    }
  int corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (corp_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG, "You are not in a corporation.");
      return 0;
    }
  if (db_bank_transfer (db, "player", ctx->player_id, "corp", corp_id,
			amount) != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INSUFFICIENT_FUNDS,
			   "Transfer failed. Check your balance.");
      return 0;
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
		       json_string ("Deposit successful."));
  json_object_set_new (response_data, "amount", json_integer (amount));
  send_response_ok_take (ctx, root, "corp.deposit.success", &response_data);
  return 0;
}


int
cmd_corp_withdraw (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "Missing data payload.");
      return 0;
    }
  long long amount = 0;
  json_t *j_amount = json_object_get (data, "amount");


  if (json_is_integer (j_amount))
    {
      amount = json_integer_value (j_amount);
    }
  if (amount <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_MISSING_FIELD, "Missing or invalid 'amount'.");
      return 0;
    }
  int corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (corp_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG, "You are not in a corporation.");
      return 0;
    }
  char role[16];


  h_get_player_corp_role (db, ctx->player_id, corp_id, role, sizeof (role));
  if (strcasecmp (role, "Leader") != 0 && strcasecmp (role, "Officer") != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_PERMISSION_DENIED,
			   "You do not have permission to withdraw funds.");
      return 0;
    }
  if (db_bank_transfer (db, "corp", corp_id, "player", ctx->player_id,
			amount) != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INSUFFICIENT_FUNDS,
			   "Transfer failed. Check corporation balance.");
      return 0;
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
		       json_string ("Withdrawal successful."));
  json_object_set_new (response_data, "amount", json_integer (amount));
  send_response_ok_take (ctx, root, "corp.withdraw.success", &response_data);
  return 0;
}


int
cmd_corp_statement (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  int limit = 20;		// default limit


  if (data)
    {
      json_t *j_limit = json_object_get (data, "limit");


      if (json_is_integer (j_limit))
	{
	  limit = (int) json_integer_value (j_limit);
	}
    }
  int corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (corp_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG, "You are not in a corporation.");
      return 0;
    }
  json_t *transactions = NULL;


  if (db_bank_get_transactions (db, "corp", corp_id, limit, NULL, 0, 0,	// tx_type_filter, start_date, end_date
				0, 0,	// min_amount, max_amount
				&transactions) != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_SERVER_ERROR,
			   "Failed to retrieve corporation transactions.");
      if (transactions)
	{
	  json_decref (transactions);
	}
      return 0;
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "corp_id", json_integer (corp_id));
  json_object_set_new (response_data, "transactions", transactions);
  send_response_ok_take (ctx, root, "corp.statement.success", &response_data);
  return 0;
}


int
cmd_corp_status (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  int corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (corp_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG, "You are not in a corporation.");
      return 0;
    }

  db_res_t *res_corp = NULL;
  db_error_t err;
  json_t *response_data = json_object ();


  if ((res_corp = repo_corp_get_info (db, corp_id, &err)) != NULL)
    {
      if (db_res_step (res_corp, &err))
	{
	  json_object_set_new (response_data, "corp_id",
			       json_integer (corp_id));
	  json_object_set_new (response_data, "name",
			       json_string (db_res_col_text (res_corp, 0,
							     &err)));
	  const char *tag = db_res_col_text (res_corp, 1, &err);


	  if (tag)
	    {
	      json_object_set_new (response_data, "tag", json_string (tag));
	    }
	  json_object_set_new (response_data, "created_at",
			       json_integer (db_res_col_i32 (res_corp, 2,
							     &err)));
	  json_object_set_new (response_data, "ceo_id",
			       json_integer (db_res_col_i32 (res_corp, 3,
							     &err)));
	}
      db_res_finalize (res_corp);
    }
  else
    {
      LOGE ("cmd_corp_status: corp info query failed: %s", err.message);
      send_response_error (ctx, root, ERR_DB, "Database error.");
      json_decref (response_data);
      return 0;
    }

  int member_count = 0;
  if (repo_corp_get_member_count (db, corp_id, &member_count) == 0)
    {
      json_object_set_new (response_data,
			   "member_count", json_integer (member_count));
    }
  else
    {
      LOGE ("cmd_corp_status: member count query failed");
      send_response_error (ctx, root, ERR_DB, "Database error.");
      json_decref (response_data);
      return 0;
    }

  char role[16];


  h_get_player_corp_role (db, ctx->player_id, corp_id, role, sizeof (role));
  json_object_set_new (response_data, "your_role", json_string (role));
  send_response_ok_take (ctx, root, "corp.status.success", &response_data);
  return 0;
}


int
cmd_stock_ipo_register (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "Missing data object.");
      return 0;
    }
  /* Ensure caller is an active CEO and grab corp_id */
  int corp_id = 0;


  if (!h_is_player_corp_ceo (db, ctx->player_id, &corp_id) || corp_id <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_PERMISSION_DENIED,
			   "Only corporation CEOs can register for IPO.");
      return 0;
    }
  /* Check if already publicly traded */
  int stock_id = 0;


  if (h_get_corp_stock_id (db, corp_id, &stock_id) == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG,
			   "Your corporation is already publicly traded.");
      return 0;
    }
  /* Check credit rating */
  int credit_rating = 0;


  if (h_get_corp_credit_rating (db, corp_id, &credit_rating) != 0
      || credit_rating < 400)
    {				// Assuming 400 is a "Default" threshold
      send_response_error (ctx,
			   root,
			   ERR_INVALID_CORP_STATE,
			   "Corporation credit rating is too low to go public.");
      return 0;
    }
  const char *ticker;
  json_t *j_ticker = json_object_get (data, "ticker");


  if (!json_is_string (j_ticker)
      || (ticker = json_string_value (j_ticker)) == NULL)
    {
      send_response_error (ctx,
			   root,
			   ERR_BAD_REQUEST, "Missing or invalid 'ticker'.");
      return 0;
    }
  // Basic ticker validation: 3-5 uppercase alphanumeric characters
  if (strlen (ticker) < 3 || strlen (ticker) > 5)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG,
			   "Ticker must be 3-5 characters long.");
      return 0;
    }
  for (size_t i = 0; i < strlen (ticker); i++)
    {
      if (!isalnum ((unsigned char) ticker[i])
	  || !isupper ((unsigned char) ticker[i]))
	{
	  send_response_error (ctx,
			       root,
			       ERR_INVALID_ARG,
			       "Ticker must be uppercase alphanumeric characters.");
	  return 0;
	}
    }
  int total_shares;


  if (!json_get_int_flexible (data, "total_shares", &total_shares)
      || total_shares <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_BAD_REQUEST,
			   "Missing or invalid 'total_shares'.");
      return 0;
    }
  int par_value;


  if (!json_get_int_flexible (data, "par_value", &par_value) || par_value < 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_BAD_REQUEST,
			   "Missing or invalid 'par_value'.");
      return 0;
    }
  int64_t new_stock_id_64 = 0;
  int rc =
    repo_corp_register_stock (db, corp_id, ticker, total_shares, par_value,
			      &new_stock_id_64);


  if (rc != 0)
    {
      LOGE ("cmd_stock_ipo_register: Failed to insert stock: %d", rc);
      if (rc == ERR_DB_CONSTRAINT)
	{
	  send_response_error (ctx,
			       root,
			       ERR_NAME_TAKEN,
			       "A stock with that ticker already exists.");
	}
      else
	{
	  send_response_error (ctx,
			       root,
			       ERR_DB,
			       "Database error during IPO registration.");
	}
      return 0;
    }
  int new_stock_id = (int) new_stock_id_64;

  // Distribute initial shares to the corporation itself (as a shareholder)
  rc = h_update_player_shares (db, 0, new_stock_id, total_shares);	// player_id 0 for corporation


  if (rc != 0)
    {
      LOGE
	("cmd_stock_ipo_register: Failed to distribute initial shares to corp %d for stock %d: %d",
	 corp_id, new_stock_id, rc);
      // This is a critical error, consider rolling back or marking stock invalid
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
		       json_string
		       ("Corporation successfully registered for IPO."));
  json_object_set_new (response_data, "corp_id", json_integer (corp_id));
  json_object_set_new (response_data, "stock_id",
		       json_integer (new_stock_id));
  json_object_set_new (response_data, "ticker", json_string (ticker));
  send_response_ok_take (ctx, root, "stock.ipo.register.success",
			 &response_data);
  json_t *payload = json_object ();


  json_object_set_new (payload, "corp_id", json_integer (corp_id));
  json_object_set_new (payload, "stock_id", json_integer (new_stock_id));
  json_object_set_new (payload, "ticker", json_string (ticker));

  db_log_engine_event (time (NULL), "stock.ipo.registered", "corp", corp_id,
		       0, payload, NULL);
  json_decref (payload);
  return 0;
}


int
cmd_stock_buy (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "Missing data object.");
      return 0;
    }
  int stock_id;


  if (!json_get_int_flexible (data, "stock_id", &stock_id) || stock_id <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_BAD_REQUEST, "Missing or invalid 'stock_id'.");
      return 0;
    }
  int quantity;


  if (!json_get_int_flexible (data, "quantity", &quantity) || quantity <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_BAD_REQUEST, "Missing or invalid 'quantity'.");
      return 0;
    }
  char *ticker = NULL;
  int corp_id = 0;
  int total_shares = 0;
  int par_value = 0;
  int current_price = 0;
  long long last_dividend_ts = 0;
  int rc = h_get_stock_info (db, stock_id, &ticker, &corp_id, &total_shares,
			     &par_value, &current_price, &last_dividend_ts);


  if (rc != 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "Stock not found.");
      free (ticker);
      return 0;
    }
  long long total_cost = (long long) quantity * current_price;
  // Check player balance
  long long player_balance;


  if (db_get_player_bank_balance (db, ctx->player_id, &player_balance) != 0
      || player_balance < total_cost)
    {
      send_response_error (ctx,
			   root,
			   ERR_INSUFFICIENT_FUNDS,
			   "Insufficient funds to purchase shares.");
      free (ticker);
      return 0;
    }
  // Perform transfer
  rc = db_bank_transfer (db,
			 "player",
			 ctx->player_id, "corp", corp_id, total_cost);
  if (rc != 0)
    {
      LOGE ("cmd_stock_buy: Bank transfer failed for player %d, stock %d: %d",
	    ctx->player_id, stock_id, rc);
      send_response_error (ctx,
			   root,
			   ERR_SERVER_ERROR,
			   "Failed to complete share purchase due to banking error.");
      free (ticker);
      return 0;
    }
  // Update player shares
  rc = h_update_player_shares (db, ctx->player_id, stock_id, quantity);
  if (rc != 0)
    {
      LOGE
	("cmd_stock_buy: Failed to update player shares for player %d, stock %d: %d",
	 ctx->player_id, stock_id, rc);
      // Critical error: funds transferred, but shares not updated. Manual intervention needed or complex rollback.
      send_response_error (ctx,
			   root,
			   ERR_SERVER_ERROR,
			   "Failed to update player shares after purchase.");
      free (ticker);
      return 0;
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
		       json_string ("Shares purchased successfully."));
  json_object_set_new (response_data, "stock_id", json_integer (stock_id));
  json_object_set_new (response_data, "ticker", json_string (ticker));
  json_object_set_new (response_data, "quantity", json_integer (quantity));
  json_object_set_new (response_data, "total_cost",
		       json_integer (total_cost));
  send_response_ok_take (ctx, root, "stock.buy.success", &response_data);
  json_t *payload = json_object ();


  json_object_set_new (payload, "player_id", json_integer (ctx->player_id));
  json_object_set_new (payload, "stock_id", json_integer (stock_id));
  json_object_set_new (payload, "quantity", json_integer (quantity));
  json_object_set_new (payload, "cost", json_integer (total_cost));

  db_log_engine_event (time (NULL), "stock.buy", "player", ctx->player_id, 0,
		       payload, NULL);
  json_decref (payload);
  free (ticker);
  return 0;
}


int
cmd_stock_dividend_set (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "Missing data object.");
      return 0;
    }
  /* Ensure caller is an active CEO and grab corp_id */
  int corp_id = 0;


  if (!h_is_player_corp_ceo (db, ctx->player_id, &corp_id) || corp_id <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_PERMISSION_DENIED,
			   "Only corporation CEOs can set dividends.");
      return 0;
    }
  int stock_id = 0;


  if (h_get_corp_stock_id (db, corp_id, &stock_id) != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG,
			   "Your corporation is not publicly traded.");
      return 0;
    }
  int amount_per_share;


  if (!json_get_int_flexible (data, "amount_per_share",
			      &amount_per_share) || amount_per_share < 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_BAD_REQUEST,
			   "Missing or invalid 'amount_per_share'.");
      return 0;
    }
  // Get total shares to calculate total dividend payout
  int total_shares = 0;
  int rc = h_get_stock_info (db,
			     stock_id,
			     NULL,
			     NULL,
			     &total_shares,
			     NULL,
			     NULL,
			     NULL);


  if (rc != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_SERVER_ERROR,
			   "Failed to retrieve stock information.");
      return 0;
    }
  long long total_payout = (long long) amount_per_share * total_shares;
  // Check if corporation has enough funds
  long long corp_balance;


  if (db_get_corp_bank_balance (db, corp_id,
				&corp_balance) != 0 ||
      corp_balance < total_payout)
    {
      send_response_error (ctx,
			   root,
			   ERR_INSUFFICIENT_FUNDS,
			   "Corporation has insufficient funds to declare this dividend.");
      return 0;
    }

  rc =
    repo_corp_declare_dividend (db, stock_id, amount_per_share, time (NULL));


  if (rc != 0)
    {
      LOGE ("cmd_stock_dividend_set: Failed to insert dividend: %d", rc);
      send_response_error (ctx,
			   root,
			   ERR_DB, "Database error declaring dividend.");
      return 0;
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
		       json_string ("Dividend declared successfully."));
  json_object_set_new (response_data, "stock_id", json_integer (stock_id));
  json_object_set_new (response_data, "amount_per_share",
		       json_integer (amount_per_share));
  json_object_set_new (response_data,
		       "total_payout", json_integer (total_payout));
  send_response_ok_take (ctx, root, "stock.dividend.set.success",
			 &response_data);
  json_t *payload = json_object ();


  json_object_set_new (payload, "corp_id", json_integer (corp_id));
  json_object_set_new (payload, "stock_id", json_integer (stock_id));
  json_object_set_new (payload, "amount_per_share",
		       json_integer (amount_per_share));
  json_object_set_new (payload, "total_payout", json_integer (total_payout));

  db_log_engine_event (time (NULL),
		       "stock.dividend.declared",
		       "corp", corp_id, 0, payload, NULL);
  json_decref (payload);
  return 0;
}


int
cmd_stock_sell (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "Missing data object.");
      return 0;
    }
  int stock_id;


  if (!json_get_int_flexible (data, "stock_id", &stock_id) || stock_id <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_BAD_REQUEST, "Missing or invalid 'stock_id'.");
      return 0;
    }
  int quantity;


  if (!json_get_int_flexible (data, "quantity", &quantity) || quantity <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_BAD_REQUEST, "Missing or invalid 'quantity'.");
      return 0;
    }

  char *ticker = NULL;
  int corp_id = 0;
  int current_price = 0;


  if (h_get_stock_info (db,
			stock_id,
			&ticker,
			&corp_id, NULL, NULL, &current_price, NULL) != 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "Stock not found.");
      return 0;
    }

  long long total_proceeds = (long long) quantity * current_price;

  // Verify shares owned
  int shares_owned = 0;
  if (repo_corp_get_shares_owned (db, ctx->player_id, stock_id, &shares_owned)
      != 0)
    {
      shares_owned = 0;
    }

  if (shares_owned < quantity)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG, "Insufficient shares to sell.");
      free (ticker);
      return 0;
    }

  // Perform transfer: corp to player
  if (db_bank_transfer (db,
			"corp",
			corp_id,
			"player", ctx->player_id, total_proceeds) != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INSUFFICIENT_FUNDS,
			   "Corporation has insufficient funds to buy back shares.");
      free (ticker);
      return 0;
    }

  // Update player shares
  if (h_update_player_shares (db, ctx->player_id, stock_id, -quantity) != 0)
    {
      LOGE
	("cmd_stock_sell: Failed to update player shares for player %d, stock %d",
	 ctx->player_id, stock_id);
      send_response_error (ctx, root, ERR_SERVER_ERROR,
			   "Failed to update shares.");
      free (ticker);
      return 0;
    }

  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
		       json_string ("Shares sold successfully."));
  json_object_set_new (response_data, "stock_id", json_integer (stock_id));
  json_object_set_new (response_data, "ticker", json_string (ticker));
  json_object_set_new (response_data, "quantity", json_integer (quantity));
  json_object_set_new (response_data, "total_proceeds",
		       json_integer (total_proceeds));
  send_response_ok_take (ctx, root, "stock.sell.success", &response_data);

  json_t *payload = json_object ();


  json_object_set_new (payload, "player_id", json_integer (ctx->player_id));
  json_object_set_new (payload, "stock_id", json_integer (stock_id));
  json_object_set_new (payload, "quantity", json_integer (quantity));
  json_object_set_new (payload, "proceeds", json_integer (total_proceeds));
  db_log_engine_event (time (NULL),
		       "stock.sell",
		       "player", ctx->player_id, 0, payload, NULL);
  json_decref (payload);

  free (ticker);
  return 0;
}


int
cmd_stock (client_ctx_t *ctx, json_t *root)
{
  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "Missing data object.");
      return 0;
    }
  const char *subcommand =
    json_string_value (json_object_get (data, "subcommand"));


  if (!subcommand)
    {
      send_response_error (ctx,
			   root,
			   ERR_BAD_REQUEST, "Missing 'subcommand' in data.");
      return 0;
    }
  if (strcasecmp (subcommand, "ipo.register") == 0)
    {
      return cmd_stock_ipo_register (ctx, root);
    }
  else if (strcasecmp (subcommand, "buy") == 0)
    {
      return cmd_stock_buy (ctx, root);
    }
  else if (strcasecmp (subcommand, "sell") == 0)
    {
      return cmd_stock_sell (ctx, root);
    }
  else if (strcasecmp (subcommand, "dividend.set") == 0)
    {
      return cmd_stock_dividend_set (ctx, root);
    }
  else
    {
      send_response_error (ctx,
			   root,
			   ERR_NOT_IMPLEMENTED,
			   "Stock subcommand not implemented.");
      return 0;
    }
}


int
h_corp_is_publicly_traded (db_t *db, int corp_id, bool *is_publicly_traded)
{
  return repo_corp_is_public (db, corp_id, is_publicly_traded);
}


int
h_daily_corp_tax (db_t *db, int64_t now_s)
{
  if (!db)
    {
      return ERR_INVALID_ARG;
    }

  db_res_t *res_corps = NULL;
  db_error_t err;


  if ((res_corps = repo_corp_get_all_corps (db, &err)) == NULL)
    {
      LOGE ("h_daily_corp_tax: Failed to fetch corporations: %s",
	    err.message);
      return err.code;
    }

  while (db_res_step (res_corps, &err))
    {
      int corp_id = db_res_col_i32 (res_corps, 0, &err);
      const char *corp_name = db_res_col_text (res_corps, 1, &err);
      long long balance = 0;


      if (db_get_corp_bank_balance (db, corp_id, &balance) == 0
	  && balance > 0)
	{
	  long long tax_amount = (balance * CORP_TAX_RATE_BP) / 10000;


	  if (tax_amount > 0)
	    {
	      if (h_deduct_credits (db,
				    "corp",
				    corp_id,
				    tax_amount, "TAX", NULL, NULL) == 0)
		{
		  LOGI ("Daily tax of %lld deducted from corp %s (%d)",
			tax_amount, corp_name, corp_id);
		}
	    }
	}
    }
  db_res_finalize (res_corps);
  return 0;
}


int
h_dividend_payout (db_t *db, int64_t now_s)
{
  if (!db)
    {
      return ERR_INVALID_ARG;
    }

  db_res_t *res_unpaid = NULL;
  db_error_t err;


  if ((res_unpaid = repo_corp_get_unpaid_dividends (db, &err)) == NULL)
    {
      LOGE ("h_dividend_payout: Failed to fetch unpaid dividends: %s",
	    err.message);
      return err.code;
    }

  while (db_res_step (res_unpaid, &err))
    {
      int div_id = db_res_col_i32 (res_unpaid, 0, &err);
      int stock_id = db_res_col_i32 (res_unpaid, 1, &err);
      int amount_per_share = db_res_col_i32 (res_unpaid, 2, &err);

      int corp_id = 0;
      int total_shares = 0;


      if (h_get_stock_info (db,
			    stock_id,
			    NULL,
			    &corp_id, &total_shares, NULL, NULL, NULL) != 0)
	{
	  continue;
	}

      long long total_payout = (long long) amount_per_share * total_shares;
      long long corp_balance = 0;


      if (db_get_corp_bank_balance (db, corp_id,
				    &corp_balance) == 0 &&
	  corp_balance >= total_payout)
	{
	  if (db_tx_begin (db, DB_TX_IMMEDIATE, &err))
	    {
	      bool ok = true;


	      if (h_deduct_credits (db,
				    "corp",
				    corp_id,
				    total_payout,
				    "DIVIDEND", NULL, NULL) != 0)
		{
		  ok = false;
		}

	      if (ok)
		{
		  db_res_t *res_holders = NULL;


		  if ((res_holders =
		       repo_corp_get_stock_holders (db, stock_id,
						    &err)) != NULL)
		    {
		      while (db_res_step (res_holders, &err))
			{
			  int player_id =
			    db_res_col_i32 (res_holders, 0, &err);
			  int shares = db_res_col_i32 (res_holders, 1, &err);
			  long long player_payout = (long long) shares *
			    amount_per_share;


			  if (player_id == 0)	// Corporation itself
			    {
			      h_add_credits (db,
					     "corp",
					     corp_id,
					     player_payout,
					     "DIVIDEND_PAYOUT", NULL, NULL);
			    }
			  else
			    {
			      h_add_credits (db,
					     "player",
					     player_id,
					     player_payout,
					     "DIVIDEND_PAYOUT", NULL, NULL);
			    }
			}
		      db_res_finalize (res_holders);
		    }
		  else
		    {
		      ok = false;
		    }
		}

	      if (ok)
		{
		  if (repo_corp_mark_dividend_paid (db, div_id, now_s) != 0)
		    {
		      ok = false;
		    }
		}

	      if (ok)
		{
		  db_tx_commit (db, &err);
		  LOGI
		    ("Dividend payout for stock %d (div_id %d) completed. Total: %lld",
		     stock_id, div_id, total_payout);
		}
	      else
		{
		  db_tx_rollback (db, NULL);
		  LOGE ("Dividend payout failed for stock %d (div_id %d)",
			stock_id, div_id);
		}
	    }
	}
      else
	{
	  LOGW
	    ("Corp %d has insufficient funds for dividend payout of stock %d",
	     corp_id, stock_id);
	}
    }
  db_res_finalize (res_unpaid);
  return 0;
}
