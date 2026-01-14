int cmd_combat_deploy_mines (client_ctx_t *ctx, json_t *root) {
  if (!require_auth (ctx, root)) return 0;
  db_t *db = game_db_get_handle (); if (!db) return 0;
  json_t *data = json_object_get (root, "data"); if (!data) return 0;
  int amount = (int)json_integer_value(json_object_get(data, "amount")), mine_type = (int)json_integer_value(json_object_get(data, "mine_type"));
  if (mine_type == 0) mine_type = 1;
  int ship_id = h_get_active_ship_id (db, ctx->player_id); if (ship_id <= 0) return 0;
  h_decloak_ship(db, ship_id);
  db_error_t err; db_error_clear(&err); if (!db_tx_begin(db, DB_TX_DEFAULT, &err)) return 0;
  int sid = -1; db_combat_get_ship_sector_locked(db, ship_id, &sid); db_combat_lock_sector(db, sid);
  if (db_combat_consume_ship_mines(db, ship_id, mine_type, amount) != 0) { db_tx_rollback(db, &err); return 0; }
  db_combat_insert_sector_mines(db, sid, ctx->player_id, ctx->corp_id, mine_type, 1, amount);
  int64_t aid = 0; db_combat_get_max_asset_id(db, sid, ctx->player_id, mine_type, &aid);
  db_tx_commit(db, &err);
  json_t *out = json_object(); json_object_set_new(out, "asset_id", json_integer(aid)); send_response_ok_take(ctx, root, "combat.mines.deployed", &out); return 0;
}

int cmd_combat_lay_mines (client_ctx_t *ctx, json_t *root) { return cmd_combat_deploy_mines (ctx, root); }

static int load_ship_combat_stats_locked (db_t *db, int ship_id, combat_ship_t *out, bool skip_locked) {
  repo_combat_ship_full_t fs; if (db_combat_load_ship_full_locked(db, ship_id, &fs, skip_locked) != 0) return -1;
  out->id = fs.id; out->hull = fs.hull; out->shields = fs.shields; out->fighters = fs.fighters; out->sector = fs.sector;
  strncpy(out->name, fs.name, sizeof(out->name)); out->attack_power = fs.attack_power; out->defense_power = fs.defense_power;
  out->max_attack = fs.max_attack; out->player_id = fs.player_id; out->corp_id = fs.corp_id; return 0;
}

static int persist_ship_damage (db_t *db, combat_ship_t *ship, int fighters_lost) {
  return db_combat_persist_ship_damage(db, ship->id, ship->hull, ship->shields, fighters_lost);
}

int handle_ship_attack (client_ctx_t *ctx, json_t *root, json_t *data, db_t *db) {
  int target_id = 0; json_get_int_flexible(data, "target_ship_id", &target_id);
  int attacker_id = h_get_active_ship_id (db, ctx->player_id); if (attacker_id <= 0) return 0;
  db_error_t err; db_error_clear(&err); db_tx_begin(db, DB_TX_DEFAULT, &err);
  combat_ship_t att={0}, def={0};
  load_ship_combat_stats_locked(db, attacker_id, &att, true); load_ship_combat_stats_locked(db, target_id, &def, true);
  int s_lost=0, h_lost=0; apply_combat_damage(&def, 10, &s_lost, &h_lost);
  persist_ship_damage(db, &def, 0); db_tx_commit(db, &err);
  json_t *resp = json_object(); json_object_set_new(resp, "success", json_true());
  send_response_ok_take (ctx, root, "combat.attack.result", &resp); return 0;
}

int cmd_combat_status (client_ctx_t *ctx, json_t *root) {
  db_t *db = game_db_get_handle (); if (!db) return 0;
  int ship_id = h_get_active_ship_id (db, ctx->player_id); if (ship_id <= 0) return 0;
  combat_ship_t ship = {0}; load_ship_combat_stats_unlocked(db, ship_id, &ship);
  json_t *res = json_object(); json_object_set_new(res, "hull", json_integer(ship.hull));
  send_response_ok_take (ctx, root, "combat.status", &res); return 0;
}

static int h_apply_quasar_damage (client_ctx_t *ctx, int damage, const char *source_desc) {
  db_t *db = game_db_get_handle (); if (!db) return 0;
  int ship_id = h_get_active_ship_id (db, ctx->player_id); if (ship_id <= 0) return 0;
  db_error_t err; db_error_clear(&err); db_tx_begin(db, DB_TX_DEFAULT, &err);
  combat_ship_t ship = {0}; load_ship_combat_stats_locked(db, ship_id, &ship, true);
  ship.hull -= damage; persist_ship_damage(db, &ship, 0); db_tx_commit(db, &err);
  if (ship.hull <= 0) destroy_ship_and_handle_side_effects(ctx, ctx->player_id);
  return 0;
}

static void apply_combat_damage (combat_ship_t *target, int damage, int *shields_lost, int *hull_lost) {
  *shields_lost = 0; *hull_lost = 0; if (!target || damage <= 0) return;
  int rem = damage; if (target->shields > 0) { int a = MIN(rem, target->shields); target->shields -= a; rem -= a; *shields_lost = a; }
  if (rem > 0) { target->hull -= rem; *hull_lost = rem; }
}

json_t * db_get_stardock_sectors (void) {
  db_t *db = game_db_get_handle (); if (!db) return NULL;
  int *s = NULL; int count = 0; db_combat_get_stardock_locations(db, &s, &count);
  json_t *arr = json_array(); for(int i=0; i<count; i++) json_array_append_new(arr, json_integer(s[i]));
  free(s); return arr;
}

int destroy_ship_and_handle_side_effects (client_ctx_t *ctx, int player_id) { return 0; }
int h_decloak_ship (db_t *db, int ship_id) { return 0; }

int h_handle_sector_entry_hazards (db_t *db, client_ctx_t *ctx, int sector_id) {
  int ship_id = h_get_active_ship_id(db, ctx->player_id); if (ship_id <= 0) return 0;
  json_t *f = NULL; db_combat_get_sector_fighters_locked(db, sector_id, &f);
  size_t i; json_t *item;
  json_array_foreach(f, i, item) {
    int owner = json_integer_value(json_object_get(item, "owner_id"));
    if (is_asset_hostile(owner, 0, ctx->player_id, ctx->corp_id)) h_apply_quasar_damage(ctx, 10, "Fighters");
  }
  json_decref(f); return 0;
}

int h_trigger_atmosphere_quasar (db_t *db, client_ctx_t *ctx, int planet_id) {
  int owner=0, base=0, react=0; char type[32]={0};
  if (db_combat_get_planet_atmosphere_quasar(db, planet_id, &owner, type, &base, &react) == 0) {
    if (is_asset_hostile(owner, 0, ctx->player_id, ctx->corp_id)) h_apply_quasar_damage(ctx, base, "Quasar");
  }
  return 0;
}

int cmd_mines_recall (client_ctx_t *ctx, json_t *root) {
  db_t *db = game_db_get_handle (); if (!db) return 0;
  json_t *data = json_object_get(root, "data");
  int sid = json_integer_value(json_object_get(data, "sector_id")), aid = json_integer_value(json_object_get(data, "asset_id"));
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  int cur=0, max=0, ship_sid=0; db_combat_get_ship_mine_capacity(db, ship_id, &ship_sid, &cur, &max);
  int qty=0, type=0; db_combat_get_asset_info(db, aid, ctx->player_id, sid, &qty, &type);
  db_error_t err; db_tx_begin(db, DB_TX_DEFAULT, &err);
  db_combat_recall_mines(db, aid, ship_id, qty); db_tx_commit(db, &err);
  json_t *out = json_object(); json_object_set_new(out, "recalled", json_integer(qty));
  send_response_ok_take(ctx, root, "combat.mines.recalled", &out); return 0;
}

int cmd_combat_sweep_mines (client_ctx_t *ctx, json_t *root) {
  db_t *db = game_db_get_handle (); if (!db) return 0;
  json_t *data = json_object_get(root, "data");
  int tsid = json_integer_value(json_object_get(data, "target_sector_id"));
  json_t *mines = NULL; db_combat_select_mines_locked(db, tsid, 1, &mines);
  size_t i; json_t *m;
  json_array_foreach(mines, i, m) db_combat_delete_sector_asset(db, json_integer_value(json_object_get(m, "id")));
  json_decref(mines);
  json_t *out = json_object(); json_object_set_new(out, "success", json_true());
  send_response_ok_take(ctx, root, "combat.mines_swept_v1", &out); return 0;
}

int cmd_fighters_recall (client_ctx_t *ctx, json_t *root) {
  db_t *db = game_db_get_handle (); if (!db) return 0;
  json_t *data = json_object_get(root, "data");
  int aid = json_integer_value(json_object_get(data, "asset_id"));
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  db_combat_add_ship_fighters(db, ship_id, 10); db_combat_delete_sector_asset(db, aid);
  json_t *out = json_object(); json_object_set_new(out, "success", json_true());
  send_response_ok_take(ctx, root, "combat.fighters.deployed", &out); return 0;
}

int cmd_combat_scrub_mines (client_ctx_t *ctx, json_t *root) {
  db_t *db = game_db_get_handle (); if (!db) return 0;
  json_t *out = json_object(); json_object_set_new(out, "success", json_true());
  send_response_ok_take(ctx, root, "combat.mines_scrubbed_v1", &out); return 0;
}
