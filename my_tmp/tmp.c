                                                                                                                                                                                                                                              
int
db_load_ports (int *server_port, int *s2s_port)
{
  int ret_code = 0;
  if (!server_port || !s2s_port)
    {
      return -1;
    }
  db_t *db = game_db_get_handle ();

  if (!db)
    {
      return -1;
    }

  /* Try to load server_port */
  {
    const char *sql = "SELECT value FROM config WHERE key = $1 AND type = 'int' LIMIT 1;";
    db_res_t *res = NULL;
    db_error_t err;
    memset (&err, 0, sizeof (err));

    if (db_query (db, sql, (db_bind_t[]){ db_bind_text ("server_port") }, 1, &res, &err))
      {
        if (db_res_step (res, &err))
          {
            *server_port = db_res_col_i32 (res, 0, &err);
          }
        else
          {
            LOGW ("[config] 'server_port' missing or invalid in DB, using default: %d", *server_port);
          }
        db_res_finalize (res);
      }
    else
      {
        LOGW ("[config] 'server_port' missing or invalid in DB, using default: %d", *server_port);
      }
  }

  /* Try to load s2s_port */
  {
    const char *sql = "SELECT value FROM config WHERE key = $1 AND type = 'int' LIMIT 1;";
    db_res_t *res = NULL;
    db_error_t err;
    memset (&err, 0, sizeof (err));

    if (db_query (db, sql, (db_bind_t[]){ db_bind_text ("s2s_port") }, 1, &res, &err))
      {
        if (db_res_step (res, &err))
          {
            *s2s_port = db_res_col_i32 (res, 0, &err);
          }
        else
          {
            LOGW ("[config] 's2s_port' missing or invalid in DB, using default: %d", *s2s_port);
          }
        db_res_finalize (res);
      }
    else
      {
        LOGW ("[config] 's2s_port' missing or invalid in DB, using default: %d", *s2s_port);
      }
  }

  return ret_code;
}

int
cmd_mines_recall (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    {
      return 0;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      send_response_error (ctx,
			   root,
			   ERR_SERVICE_UNAVAILABLE, "Database unavailable");
      return 0;
    }
  /* 1. Parse input */
  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error (ctx,
			   root,
			   ERR_MISSING_FIELD, "Missing required field: data");
      return 0;
    }
  json_t *j_sector_id = json_object_get (data, "sector_id");
  json_t *j_asset_id = json_object_get (data, "asset_id");


  if (!j_sector_id || !json_is_integer (j_sector_id) ||
      !j_asset_id || !json_is_integer (j_asset_id))
    {
      send_response_error (ctx,
			   root,
			   ERR_CURSOR_INVALID,
			   "Missing required field or invalid type: sector_id/asset_id");
      return 0;
    }
  int requested_sector_id = (int) json_integer_value (j_sector_id);
  int asset_id = (int) json_integer_value (j_asset_id);
  /* 2. Load player, ship, current_sector_id */
  int player_ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (player_ship_id <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_SHIP_NOT_FOUND,
			   "No active ship found for player.");
      return 0;
    }
  int player_current_sector_id = -1;
  int ship_mines_current = 0;
  int ship_mines_max = 0;
  sqlite3_stmt *stmt_player_ship = NULL;
  const char *sql_player_ship =
    "SELECT s.sector, s.mines, st.maxmines "
    "FROM ships s "
    "JOIN shiptypes st ON s.type_id = st.id " "WHERE s.id = ?1;";


  if (sqlite3_prepare_v2 (db, sql_player_ship, -1, &stmt_player_ship, NULL) !=
      SQLITE_OK)
    {
      send_response_error (ctx,
			   root,
			   ERR_DB, "Failed to prepare player ship query.");
      return 0;
    }
  sqlite3_bind_int (stmt_player_ship, 1, player_ship_id);
  if (sqlite3_step (stmt_player_ship) == SQLITE_ROW)
    {
      player_current_sector_id = sqlite3_column_int (stmt_player_ship, 0);
      ship_mines_current = sqlite3_column_int (stmt_player_ship, 1);
      ship_mines_max = sqlite3_column_int (stmt_player_ship, 2);
    }
  sqlite3_finalize (stmt_player_ship);
  if (player_current_sector_id <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_SECTOR_NOT_FOUND,
			   "Could not determine player's current sector.");
      return 0;
    }
  /* 3. Verify asset belongs to player and is in current sector */
  sqlite3_stmt *stmt_asset = NULL;
  const char *sql_asset = "SELECT quantity, asset_type FROM sector_assets " "WHERE id = ?1 AND player = ?2 AND sector = ?3 AND asset_type IN (1, 4);";	// Mines only


  if (sqlite3_prepare_v2 (db, sql_asset, -1, &stmt_asset, NULL) != SQLITE_OK)
    {
      send_response_error (ctx, root, ERR_DB,
			   "Failed to prepare asset query.");
      return 0;
    }
  sqlite3_bind_int (stmt_asset, 1, asset_id);
  sqlite3_bind_int (stmt_asset, 2, ctx->player_id);
  sqlite3_bind_int (stmt_asset, 3, requested_sector_id);
  int asset_quantity = 0;
  int asset_type = 0;


  if (sqlite3_step (stmt_asset) == SQLITE_ROW)
    {
      asset_quantity = sqlite3_column_int (stmt_asset, 0);
      asset_type = sqlite3_column_int (stmt_asset, 1);
    }
  sqlite3_finalize (stmt_asset);
  if (asset_quantity <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_NOT_FOUND,
			   "Mine asset not found or does not belong to you in this sector.");
      return 0;
    }
  /* 4. Check if ship has capacity for recalled mines */
  if (ship_mines_current + asset_quantity > ship_mines_max)
    {
      json_t *data_opt = json_object ();


      json_object_set_new (data_opt, "reason",
			   json_string ("insufficient_mine_capacity"));
      send_response_refused_steal (ctx,
				   root,
				   REF_INSUFFICIENT_CAPACITY,
				   "Insufficient ship capacity to recall all mines.",
				   data_opt);
      return 0;
    }
  /* 5. Transaction: delete asset, credit ship */
  char *errmsg = NULL;


  if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
	{
	  sqlite3_free (errmsg);
	}
      send_response_error (ctx, root, ERR_DB, "Could not start transaction");
      return 0;
    }
  // Delete the asset from sector_assets
  sqlite3_stmt *stmt_delete = NULL;
  const char *sql_delete = "DELETE FROM sector_assets WHERE id = ?1;";


  if (sqlite3_prepare_v2 (db, sql_delete, -1, &stmt_delete, NULL) !=
      SQLITE_OK)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_error (ctx,
			   root,
			   ERR_DB, "Failed to prepare delete asset query.");
      return 0;
    }
  sqlite3_bind_int (stmt_delete, 1, asset_id);
  if (sqlite3_step (stmt_delete) != SQLITE_DONE)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_error (ctx,
			   root,
			   ERR_DB, "Failed to delete asset from sector.");
      sqlite3_finalize (stmt_delete);
      return 0;
    }
  sqlite3_finalize (stmt_delete);
  // Credit mines to ship
  sqlite3_stmt *stmt_credit = NULL;
  const char *sql_credit =
    "UPDATE ships SET mines = mines + ?1 WHERE id = ?2;";


  if (sqlite3_prepare_v2 (db, sql_credit, -1, &stmt_credit, NULL) !=
      SQLITE_OK)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_error (ctx,
			   root,
			   ERR_DB, "Failed to prepare credit ship query.");
      return 0;
    }
  sqlite3_bind_int (stmt_credit, 1, asset_quantity);
  sqlite3_bind_int (stmt_credit, 2, player_ship_id);
  if (sqlite3_step (stmt_credit) != SQLITE_DONE)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_error (ctx, root, ERR_DB,
			   "Failed to credit mines to ship.");
      sqlite3_finalize (stmt_credit);
      return 0;
    }
  sqlite3_finalize (stmt_credit);
  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
	{
	  sqlite3_free (errmsg);
	}
      send_response_error (ctx, root, ERR_DB, "Commit failed");
      return 0;
    }
  /* 6. Emit engine_event via h_log_engine_event */
  {
    json_t *evt = json_object ();


    json_object_set_new (evt, "sector_id",
			 json_integer (requested_sector_id));
    json_object_set_new (evt, "player_id", json_integer (ctx->player_id));
    json_object_set_new (evt, "asset_id", json_integer (asset_id));
    json_object_set_new (evt, "amount", json_integer (asset_quantity));
    json_object_set_new (evt, "asset_type", json_integer (asset_type));
    json_object_set_new (evt, "event_ts",
			 json_integer ((json_int_t) time (NULL)));
    (void) db_log_engine_event ((long long) time (NULL), "mines.recalled",
				NULL, ctx->player_id, requested_sector_id,
				evt, NULL);
  }
  /* 7. Send enveloped_ok response */
  json_t *out = json_object ();


  json_object_set_new (out, "sector_id", json_integer (requested_sector_id));
  json_object_set_new (out, "asset_id", json_integer (asset_id));
  json_object_set_new (out, "recalled", json_integer (asset_quantity));
  json_object_set_new (out, "remaining_in_sector", json_integer (0));	// All recalled
  json_object_set_new (out, "asset_type", json_integer (asset_type));
  send_response_ok_take (ctx, root, "combat.mines.recalled", &out);
  return 0;
}


int
h_update_entity_stock (sqlite3 *db,
		       const char *entity_type,
		       int entity_id,
		       const char *commodity_code,
		       int quantity_delta, int *new_quantity_out)
{
  int current_quantity = 0;
  // Get current quantity (ignore error if not found, assume 0)
  h_get_entity_stock_quantity (db,
			       entity_type,
			       entity_id, commodity_code, &current_quantity);

  int new_quantity = current_quantity + quantity_delta;


  if (new_quantity < 0)
    {
      new_quantity = 0;		// Prevent negative stock
    }
  sqlite3_stmt *stmt = NULL;
  const char *sql_upsert =
    "INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price, last_updated_ts) "
    "VALUES (?1, ?2, ?3, ?4, 0, strftime('%s','now')) "
    "ON CONFLICT(entity_type, entity_id, commodity_code) DO UPDATE SET quantity = ?4, last_updated_ts = strftime('%s','now');";

  int rc = sqlite3_prepare_v2 (db, sql_upsert, -1, &stmt, NULL);


  if (rc != SQLITE_OK)
    {
      LOGE ("h_update_entity_stock: prepare failed: %s", sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_text (stmt, 1, entity_type, -1, SQLITE_STATIC);
  sqlite3_bind_int (stmt, 2, entity_id);
  sqlite3_bind_text (stmt, 3, commodity_code, -1, SQLITE_STATIC);
  sqlite3_bind_int (stmt, 4, new_quantity);

  rc = sqlite3_step (stmt);
  if (rc != SQLITE_DONE)
    {
      LOGE ("h_update_entity_stock: upsert failed: %s", sqlite3_errmsg (db));
      sqlite3_finalize (stmt);
      return SQLITE_ERROR;
    }
  sqlite3_finalize (stmt);

  if (new_quantity_out)
    {
      *new_quantity_out = new_quantity;
    }
  return SQLITE_OK;
}
