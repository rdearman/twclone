int handle_combat_flee (client_ctx_t *ctx, json_t *root) {
  db_t *db = game_db_get_handle (); if (!db) return 0;
  int ship_id = h_get_active_ship_id (db, ctx->player_id); if (ship_id <= 0) return 0;
  h_decloak_ship (db, ship_id);
  TurnConsumeResult tc = h_consume_player_turn (db, ctx, 1);
  if (tc != TURN_CONSUME_SUCCESS) return handle_turn_consumption_error (ctx, tc, "combat.flee", root, NULL);
  int mass = 100, engine = 10, sector_id = 0; if (db_combat_get_flee_info(db, ship_id, &mass, &sector_id) != 0) return 0;
  if (((double) engine * 10.0) / ((double) mass + 1.0) > 0.5) {
    int dest = 0; db_combat_get_first_adjacent_sector(db, sector_id, &dest);
    if (dest > 0) {
      if (db_combat_move_ship_and_player(db, ship_id, ctx->player_id, dest) == 0) {
        h_handle_sector_entry_hazards (db, ctx, dest);
        json_t *res = json_object (); json_object_set_new (res, "success", json_true ()); json_object_set_new (res, "to_sector", json_integer (dest));
        send_response_ok_take (ctx, root, "combat.flee", &res); return 0;
      }
    }
  }
  json_t *res = json_object (); json_object_set_new (res, "success", json_false ()); send_response_ok_take (ctx, root, "combat.flee", &res); return 0;
}

int cmd_deploy_fighters_list (client_ctx_t *ctx, json_t *root) { return cmd_deploy_assets_list_internal (ctx, root, "deploy.fighters.list_v1", "fighters", db_combat_list_fighters); }
int cmd_deploy_mines_list (client_ctx_t *ctx, json_t *root) { return cmd_deploy_assets_list_internal (ctx, root, "deploy.mines.list_v1", "mines", db_combat_list_mines); }

int cmd_deploy_assets_list_internal (client_ctx_t *ctx, json_t *root, const char *list_type, const char *asset_key, int (*list_func)(db_t*, int, json_t**)) {
  (void)asset_key; db_t *db = game_db_get_handle (); if (!db) return 0;
  json_t *entries = NULL; if (list_func(db, ctx->player_id, &entries) != 0) return 0;
  json_t *jdata = json_object (); json_object_set_new (jdata, "total", json_integer (json_array_size(entries))); json_object_set_new (jdata, "entries", entries);
  send_response_ok_take (ctx, root, list_type, &jdata); return 0;
}

int cmd_combat_deploy_fighters (client_ctx_t *ctx, json_t *root) {
  if (!require_auth (ctx, root)) return 0;
  db_t *db = game_db_get_handle (); if (!db) return 0;
  json_t *data = json_object_get (root, "data"); if (!data) return 0;
  int amount = (int)json_integer_value(json_object_get(data, "amount")), offense = (int)json_integer_value(json_object_get(data, "offense"));
  int corp_id = (int)json_integer_value(json_object_get(data, "corporation_id")), ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0) return 0; h_decloak_ship (db, ship_id);
  db_error_t err; db_error_clear(&err); if (!db_tx_begin(db, DB_TX_DEFAULT, &err)) return 0;
  int sid = -1; if (db_combat_get_ship_sector_locked(db, ship_id, &sid) != 0 || sid <= 0) { db_tx_rollback(db, &err); return 0; }
  if (db_combat_lock_sector(db, sid) != 0) { db_tx_rollback(db, &err); return 0; }
  int total = 0; db_combat_sum_sector_fighters(db, sid, &total); if (total + amount > SECTOR_FIGHTER_CAP) { db_tx_rollback(db, &err); return 0; }
  if (db_combat_consume_ship_fighters(db, ship_id, amount) != 0) { db_tx_rollback(db, &err); return 0; }
  db_combat_insert_sector_fighters(db, sid, ctx->player_id, corp_id, offense, amount);
  int64_t aid = 0; db_combat_get_max_asset_id(db, sid, ctx->player_id, 2, &aid);
  db_tx_commit(db, &err); if (sid >= 1 && sid <= 10) iss_summon(sid, ctx->player_id);
  json_t *out = json_object(); json_object_set_new(out, "asset_id", json_integer(aid)); send_response_ok_take(ctx, root, "combat.fighters.deployed", &out); return 0;
}
