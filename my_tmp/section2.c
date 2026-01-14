int apply_sector_quasar_on_entry (client_ctx_t *ctx, int sector_id) {
  if (sector_id == 1) return 0;
  db_t *db = game_db_get_handle (); if (!db) return 0;
  int shot_fired = 0; json_t *planets = NULL;
  if (db_combat_get_planet_quasar_info(db, sector_id, &planets) == 0) {
    size_t i; json_t *p; json_array_foreach(planets, i, p) {
      int planet_id = json_integer_value(json_object_get(p, "planet_id")), owner_id = json_integer_value(json_object_get(p, "owner_id"));
      const char *owner_type = json_string_value(json_object_get(p, "owner_type"));
      int base_strength = json_integer_value(json_object_get(p, "base_strength")), reaction = json_integer_value(json_object_get(p, "reaction"));
      int p_corp_id = (owner_type && (strcasecmp (owner_type, "corp") == 0 || strcasecmp (owner_type, "corporation") == 0)) ? owner_id : 0;
      if (is_asset_hostile (owner_id, p_corp_id, ctx->player_id, ctx->corp_id)) {
        int pct = (reaction == 1) ? 125 : (reaction >= 2) ? 150 : 100;
        int damage = (int)floor ((double)base_strength * (double)pct / 100.0);
        char source_desc[64]; snprintf (source_desc, sizeof (source_desc), "Quasar Sector Shot (Planet %d)", planet_id);
        if (h_apply_quasar_damage (ctx, damage, source_desc)) shot_fired = 1; else shot_fired = 2; break;
      }
    }
    json_decref(planets);
  }
  return (shot_fired == 1);
}

int apply_armid_mines_on_entry (client_ctx_t *ctx, int new_sector_id, armid_encounter_t *out_enc) {
  db_t *db = game_db_get_handle (); if (!db) return -1;
  int ship_id = h_get_active_ship_id (db, ctx->player_id); if (ship_id <= 0) return 0;
  if (out_enc) { memset (out_enc, 0, sizeof (*out_enc)); out_enc->sector_id = new_sector_id; }
  ship_t ship_stats = {0}; db_error_t err; db_error_clear (&err);
  if (db_ship_get_combat_stats (db, ship_id, &ship_stats, &err) <= 0) return 0;
  int dmg_p_m = db_get_config_int (db, "armid_damage_per_mine", 25);
  if (g_armid_config.armid.enabled) {
    json_t *mines = NULL; if (db_combat_select_armid_mines_locked(db, new_sector_id, &mines) == 0) {
      size_t i; json_t *m; json_array_foreach(mines, i, m) {
        int mine_id = json_integer_value(json_object_get(m, "id")), qty = json_integer_value(json_object_get(m, "quantity"));
        int owner = json_integer_value(json_object_get(m, "owner_id")), corp = json_integer_value(json_object_get(m, "corp_id"));
        if (!is_asset_hostile (owner, corp, ctx->player_id, ctx->corp_id)) continue;
        armid_damage_breakdown_t d = {0}; apply_armid_damage_to_ship (&ship_stats, qty * dmg_p_m, &d);
        db_combat_delete_sector_asset(db, mine_id);
        if (out_enc) { out_enc->armid_triggered += qty; out_enc->shields_lost += d.shields_lost; out_enc->fighters_lost += d.fighters_lost; out_enc->hull_lost += d.hull_lost; }
        db_combat_persist_ship_damage(db, ship_id, ship_stats.hull, ship_stats.shields, 0);
        if (ship_stats.hull <= 0) { destroy_ship_and_handle_side_effects (ctx, ctx->player_id); if (out_enc) out_enc->destroyed = true; break; }
      }
      json_decref(mines);
    }
  }
  return 0;
}

int apply_limpet_mines_on_entry (client_ctx_t *ctx, int new_sector_id, armid_encounter_t *out_enc) {
  (void)out_enc; db_t *db = game_db_get_handle (); if (!db) return -1;
  int ship_id = h_get_active_ship_id (db, ctx->player_id); if (ship_id <= 0) return 0;
  if (!g_cfg.mines.limpet.enabled) return 0;
  json_t *limpets = NULL; if (db_combat_select_limpets_locked(db, new_sector_id, &limpets) != 0) return -1;
  int64_t now_ts = (int64_t)time (NULL);
  size_t i; json_t *limpet; json_array_foreach(limpets, i, limpet) {
    int aid = json_integer_value(json_object_get(limpet, "id")), qty = json_integer_value(json_object_get(limpet, "quantity"));
    int owner = json_integer_value(json_object_get(limpet, "owner_id")), corp = json_integer_value(json_object_get(limpet, "corp_id"));
    int64_t ttl = json_integer_value(json_object_get(limpet, "ttl"));
    sector_asset_t asset = { .quantity = qty, .ttl = (time_t)ttl };
    if (!is_asset_hostile (owner, corp, ctx->player_id, ctx->corp_id)) continue;
    if (!armid_stack_is_active (&asset, (time_t)now_ts)) continue;
    bool attached = false; if (db_combat_check_limpet_attached(db, ship_id, owner, &attached) != 0 || attached) continue;
    if (db_combat_decrement_or_delete_asset(db, aid, qty) == 0) {
      db_combat_attach_limpet(db, ship_id, owner, now_ts);
      json_t *event_data = json_object (); json_object_set_new (event_data, "target_ship_id", json_integer (ship_id));
      db_log_engine_event (now_ts, "combat.limpet.attached", "player", owner, new_sector_id, event_data, NULL);
    }
  }
  json_decref(limpets); return 0;
}
