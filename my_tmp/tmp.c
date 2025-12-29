int h_player_is_npc (db_t *db, int player_id) {
  if (!db) return 0;
  
  const char *sql = "SELECT is_npc FROM players WHERE id = $1;";
  db_res_t *res = NULL;
  db_error_t err;
  db_error_clear(&err);
  
  db_bind_t params[] = { db_bind_i32(player_id) };
  if (!db_query(db, sql, params, 1, &res, &err)) {
    return 0;
  }
  
  int is_npc = 0;
  if (db_res_step(res, &err)) {
    is_npc = (int)db_res_col_i32(res, 0, &err);
  }
  
  db_res_finalize(res);
  return is_npc;
}
int spawn_starter_ship (db_t *db, int player_id, int sector_id) {
  if (!db) return -1;
  
  // Get ship type
  const char *sql_type = "SELECT id, initialholds, maxfighters, maxshields FROM shiptypes WHERE name = $1;";
  db_res_t *res = NULL;
  db_error_t err;
  db_error_clear(&err);
  
  db_bind_t type_params[] = { db_bind_text("Scout Marauder") };
  if (!db_query(db, sql_type, type_params, 1, &res, &err)) {
    return -1;
  }
  
  int ship_type_id = 0, holds = 0, fighters = 0, shields = 0;
  if (db_res_step(res, &err)) {
    ship_type_id = (int)db_res_col_i32(res, 0, &err);
    holds = (int)db_res_col_i32(res, 1, &err);
    fighters = (int)db_res_col_i32(res, 2, &err);
    shields = (int)db_res_col_i32(res, 3, &err);
  }
  db_res_finalize(res);
  
  if (ship_type_id == 0) return -1;
  
  // Insert ship with RETURNING to get ID
  const char *sql_ins = "INSERT INTO ships (name, type_id, holds, fighters, shields, sector) VALUES ($1, $2, $3, $4, $5, $6) RETURNING id;";
  db_bind_t ins_params[] = {
    db_bind_text("Starter Ship"),
    db_bind_i32(ship_type_id),
    db_bind_i32(holds),
    db_bind_i32(fighters),
    db_bind_i32(shields),
    db_bind_i32(sector_id)
  };
  
  res = NULL;
  db_error_clear(&err);
  
  if (!db_query(db, sql_ins, ins_params, 6, &res, &err)) {
    return -1;
  }
  
  int ship_id = 0;
  if (db_res_step(res, &err)) {
    ship_id = (int)db_res_col_i32(res, 0, &err);
  }
  db_res_finalize(res);
  
  if (ship_id == 0) return -1;
  
  // Set ownership
  const char *sql_own = "INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary) VALUES ($1, $2, 1, 1);";
  db_bind_t own_params[] = {
    db_bind_i32(ship_id),
    db_bind_i32(player_id)
  };
  
  db_error_clear(&err);
  db_exec(db, sql_own, own_params, 2, &err);
  
  // Update player
  const char *sql_upd = "UPDATE players SET ship = $1, sector = $2 WHERE id = $3;";
  db_bind_t upd_params[] = {
    db_bind_i32(ship_id),
    db_bind_i32(sector_id),
    db_bind_i32(player_id)
  };
  
  db_error_clear(&err);
  db_exec(db, sql_upd, upd_params, 3, &err);
  
  // Update podded status
  const char *sql_pod = "UPDATE podded_status SET status = $1 WHERE player_id = $2;";
  db_bind_t pod_params[] = {
    db_bind_text("alive"),
    db_bind_i32(player_id)
  };
  
  db_error_clear(&err);
  db_exec(db, sql_pod, pod_params, 2, &err);
  
  return 0;
}
int h_get_player_petty_cash(db_t *db, int player_id, long long *bal) {
  if (!db || player_id <= 0 || !bal) return -1;
  
  const char *sql = "SELECT credits FROM players WHERE id = $1;";
  db_res_t *res = NULL;
  db_error_t err;
  db_error_clear(&err);
  
  db_bind_t params[] = { db_bind_i32(player_id) };
  if (!db_query(db, sql, params, 1, &res, &err)) {
    return -1;
  }
  
  int rc = -1;
  if (db_res_step(res, &err)) {
    *bal = db_res_col_i64(res, 0, &err);
    rc = 0;
  }
  
  db_res_finalize(res);
  return rc;
}
int h_deduct_player_petty_cash_unlocked(db_t *db, int player_id, long long amount, long long *new_balance_out) {
  if (!db || amount < 0) return -1;
  if (new_balance_out) *new_balance_out = 0;
  
  const char *sql = "UPDATE players SET credits = credits - $1 WHERE id = $2 AND credits >= $1 RETURNING credits;";
  db_bind_t params[] = {
    db_bind_i64(amount),
    db_bind_i32(player_id)
  };
  
  db_res_t *res = NULL;
  db_error_t err;
  db_error_clear(&err);
  
  if (!db_query(db, sql, params, 2, &res, &err)) {
    return -1;
  }
  
  if (db_res_step(res, &err)) {
    if (new_balance_out) {
      *new_balance_out = db_res_col_i64(res, 0, &err);
    }
    db_res_finalize(res);
    return 0;
  }
  
  db_res_finalize(res);
  return -1;
}
int h_add_player_petty_cash(db_t *db, int player_id, long long amount, long long *new_balance_out) {
  if (!db || amount < 0) return -1;
  if (new_balance_out) *new_balance_out = 0;
  
  const char *sql = "UPDATE players SET credits = credits + $1 WHERE id = $2 RETURNING credits;";
  db_bind_t params[] = {
    db_bind_i64(amount),
    db_bind_i32(player_id)
  };
  
  db_res_t *res = NULL;
  db_error_t err;
  db_error_clear(&err);
  
  if (!db_query(db, sql, params, 2, &res, &err)) {
    return -1;
  }
  
  if (db_res_step(res, &err)) {
    if (new_balance_out) {
      *new_balance_out = db_res_col_i64(res, 0, &err);
    }
    db_res_finalize(res);
    return 0;
  }
  
  db_res_finalize(res);
  return -1;
}
TurnConsumeResult h_consume_player_turn(db_t *db, client_ctx_t *ctx, int turns) {
  if (!db || !ctx || turns <= 0) {
    return TURN_CONSUME_ERROR_INVALID_AMOUNT;
  }
  
  int player_id = ctx->player_id;
  
  // Check if player has enough turns
  const char *sql_check = "SELECT turns_remaining FROM turns WHERE player = $1;";
  db_res_t *res = NULL;
  db_error_t err;
  db_error_clear(&err);
  
  db_bind_t check_params[] = { db_bind_i32(player_id) };
  if (!db_query(db, sql_check, check_params, 1, &res, &err)) {
    return TURN_CONSUME_ERROR_DB_FAIL;
  }
  
  int turns_remaining = 0;
  if (db_res_step(res, &err)) {
    turns_remaining = (int)db_res_col_i32(res, 0, &err);
  }
  db_res_finalize(res);
  
  if (turns_remaining < turns) {
    return TURN_CONSUME_ERROR_NO_TURNS;
  }
  
  // Update turns with EXTRACT(EPOCH FROM NOW()) for PostgreSQL compatibility
  const char *sql_update = "UPDATE turns SET turns_remaining = turns_remaining - $1, last_update = EXTRACT(EPOCH FROM NOW())::int WHERE player = $2 AND turns_remaining >= $1;";
  db_bind_t upd_params[] = {
    db_bind_i32(turns),
    db_bind_i32(player_id)
  };
  
  db_error_clear(&err);
  if (!db_exec(db, sql_update, upd_params, 2, &err)) {
    return TURN_CONSUME_ERROR_DB_FAIL;
  }
  
  return TURN_CONSUME_SUCCESS;
}
int handle_turn_consumption_error(client_ctx_t *ctx, TurnConsumeResult res, const char *cmd, json_t *root, json_t *meta) {
  const char *reason_str = NULL;
  switch (res) {
    case TURN_CONSUME_ERROR_DB_FAIL:
      reason_str = "db_failure";
      break;
    case TURN_CONSUME_ERROR_PLAYER_NOT_FOUND:
      reason_str = "player_not_found";
      break;
    case TURN_CONSUME_ERROR_NO_TURNS:
      reason_str = "no_turns_remaining";
      break;
    case TURN_CONSUME_ERROR_INVALID_AMOUNT:
      reason_str = "invalid_amount";
      break;
    default:
      reason_str = "unknown_error";
      break;
  }
  
  json_t *meta_obj = meta ? json_copy(meta) : json_object();
  if (meta_obj) {
    json_object_set_new(meta_obj, "reason", json_string(reason_str));
    json_object_set_new(meta_obj, "command", json_string(cmd ? cmd : "unknown"));
    send_response_refused_steal(ctx, root, ERR_REF_NO_TURNS, "Insufficient turns.", NULL);
    json_decref(meta_obj);
  }
  return 0;
}
int h_player_apply_progress(db_t *db, int player_id, long long delta_xp, int delta_align, const char *reason) {
  if (!db || player_id <= 0) return -1;
  
  // Get current alignment and experience
  const char *sql_get = "SELECT alignment, experience FROM players WHERE id = $1;";
  db_res_t *res = NULL;
  db_error_t err;
  db_error_clear(&err);
  
  db_bind_t get_params[] = { db_bind_i32(player_id) };
  if (!db_query(db, sql_get, get_params, 1, &res, &err)) {
    return -1;
  }
  
  int cur_align = 0;
  long long cur_xp = 0;
  if (db_res_step(res, &err)) {
    cur_align = (int)db_res_col_i32(res, 0, &err);
    cur_xp = db_res_col_i64(res, 1, &err);
  } else {
    db_res_finalize(res);
    return -1;
  }
  db_res_finalize(res);
  
  // Calculate new values
  long long new_xp = cur_xp + delta_xp;
  if (new_xp < 0) new_xp = 0;
  
  int new_align = cur_align + delta_align;
  if (new_align > 2000) new_align = 2000;
  if (new_align < -2000) new_align = -2000;
  
  // Update player
  const char *sql_upd = "UPDATE players SET experience = $1, alignment = $2 WHERE id = $3;";
  db_bind_t upd_params[] = {
    db_bind_i64(new_xp),
    db_bind_i32(new_align),
    db_bind_i32(player_id)
  };
  
  db_error_clear(&err);
  if (!db_exec(db, sql_upd, upd_params, 3, &err)) {
    return -1;
  }
  
  // Update commission (call the DB function)
  db_player_update_commission(db, player_id);
  
  LOGD("Player %d progress updated. Reason: %s", player_id, reason ? reason : "N/A");
  return 0;
}
int h_get_player_sector(db_t *db, int player_id) {
  if (!db) return 0;
  
  const char *sql = "SELECT COALESCE(sector, 0) FROM players WHERE id = $1;";
  db_res_t *res = NULL;
  db_error_t err;
  db_error_clear(&err);
  
  db_bind_t params[] = { db_bind_i32(player_id) };
  if (!db_query(db, sql, params, 1, &res, &err)) {
    return 0;
  }
  
  int sector = 0;
  if (db_res_step(res, &err)) {
    sector = (int)db_res_col_i32(res, 0, &err);
    if (sector < 0) sector = 0;
  }
  
  db_res_finalize(res);
  return sector;
}

int h_add_player_petty_cash_unlocked(db_t *db, int player_id, long long amount, long long *new_balance_out) {
  if (!db || amount < 0) return -1;
  if (new_balance_out) *new_balance_out = 0;
  
  const char *sql = "UPDATE players SET credits = credits + $1 WHERE id = $2 RETURNING credits;";
  db_bind_t params[] = {
    db_bind_i64(amount),
    db_bind_i32(player_id)
  };
  
  db_res_t *res = NULL;
  db_error_t err;
  db_error_clear(&err);
  
  if (!db_query(db, sql, params, 2, &res, &err)) {
    return -1;
  }
  
  if (db_res_step(res, &err)) {
    if (new_balance_out) {
      *new_balance_out = db_res_col_i64(res, 0, &err);
    }
    db_res_finalize(res);
    return 0;
  }
  
  db_res_finalize(res);
  return -1;
}

