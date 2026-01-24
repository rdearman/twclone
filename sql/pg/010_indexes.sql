-- Generated from sqlite_schema.sql -> Postgres
CREATE INDEX idx_sessions_player ON sessions (player_id);

CREATE INDEX idx_sessions_expires ON sessions (expires);

CREATE INDEX idx_idemp_cmd ON idempotency (cmd);

CREATE INDEX idx_locks_until ON locks (until_ms);

CREATE INDEX idx_corp_invites_corp ON corp_invites (corp_id);

CREATE INDEX idx_corp_invites_player ON corp_invites (player_id);

CREATE UNIQUE INDEX idx_turns_player ON turns (player_id);

CREATE INDEX ix_trade_log_ts ON trade_log (timestamp);

CREATE INDEX ix_stardock_owner ON stardock_assets (owner_id);

CREATE INDEX idx_cluster_sectors_sector ON cluster_sectors (sector_id);

CREATE INDEX idx_player_block_blocked ON player_block (blocked_id);

CREATE INDEX idx_notice_seen_player ON notice_seen (player_id, seen_at DESC);

CREATE INDEX idx_system_notice_active ON system_notice (expires_at, created_at DESC);

CREATE INDEX idx_warps_from ON sector_warps (from_sector);

CREATE INDEX idx_warps_to ON sector_warps (to_sector);

CREATE INDEX idx_ports_loc ON ports (sector_id);

CREATE INDEX idx_planets_sector ON planets (sector_id);

CREATE INDEX idx_citadels_planet ON citadels (planet_id);

CREATE INDEX ix_warps_from_to ON sector_warps (from_sector, to_sector);

CREATE INDEX idx_players_sector ON players (sector_id);

CREATE INDEX idx_players_ship ON players (ship_id);

CREATE UNIQUE INDEX idx_players_name ON players (name);

CREATE INDEX idx_ship_own_ship ON ship_ownership (ship_id);

CREATE UNIQUE INDEX idx_ports_loc_number ON ports (sector_id, number);

CREATE UNIQUE INDEX idx_mail_idem_recipient ON mail (idempotency_key, recipient_id)
WHERE
    idempotency_key IS NOT NULL;

CREATE INDEX idx_mail_inbox ON mail (recipient_id, deleted, archived, sent_at DESC);

CREATE INDEX idx_mail_unread ON mail (recipient_id, read_at);

CREATE INDEX idx_mail_sender ON mail (sender_id, sent_at DESC);

CREATE INDEX idx_subspace_time ON subspace (posted_at DESC);

CREATE UNIQUE INDEX ux_corp_name_uc ON corporations (upper(name));

CREATE UNIQUE INDEX ux_corp_tag_uc ON corporations (upper(tag))
WHERE
    tag IS NOT NULL;

CREATE INDEX ix_corporations_owner ON corporations (owner_id);

CREATE INDEX idx_ship_own_player ON ship_ownership (player_id);

CREATE INDEX ix_corp_members_player ON corp_members (player_id);

CREATE INDEX ix_corp_members_role ON corp_members (corporation_id, ROLE);

CREATE INDEX idx_corp_mail_corp ON corp_mail (corporation_id, posted_at DESC);

CREATE INDEX idx_corp_log_corp_time ON corp_log (corporation_id, created_at DESC);

CREATE INDEX idx_corp_log_type ON corp_log (event_type, created_at DESC);

CREATE INDEX idx_sys_events_time ON system_events (created_at DESC);

CREATE INDEX idx_sys_events_scope ON system_events (scope);

CREATE INDEX idx_subscriptions_player ON subscriptions (player_id, enabled);

CREATE INDEX idx_subs_enabled ON subscriptions (enabled);

CREATE INDEX idx_subs_event ON subscriptions (event_type);

CREATE INDEX idx_player_prefs_player ON player_prefs (player_prefs_id);

CREATE INDEX idx_bookmarks_player ON player_bookmarks (player_id);

CREATE INDEX idx_avoid_player ON player_avoid (player_id);

CREATE INDEX idx_notes_player ON player_notes (player_id);

CREATE INDEX idx_port_busts_player ON port_busts (player_id);

CREATE INDEX idx_commodity_orders_comm ON commodity_orders (commodity_id, status);

CREATE INDEX idx_traps_trigger ON traps (armed, trigger_at);

CREATE UNIQUE INDEX idx_bank_accounts_owner ON bank_accounts (owner_type, owner_id, currency);

CREATE INDEX idx_bank_transactions_account_ts ON bank_transactions (account_id, ts DESC);

CREATE INDEX idx_bank_transactions_tx_group ON bank_transactions (tx_group_id);

CREATE UNIQUE INDEX idx_bank_transactions_idem ON bank_transactions (account_id, idempotency_key)
WHERE
    idempotency_key IS NOT NULL;

CREATE INDEX idx_bank_fee_active ON bank_fee_schedules (tx_type, owner_type, currency, effective_from, effective_to);

CREATE INDEX idx_corp_tx_corp_ts ON corp_tx (corp_id, ts);

CREATE INDEX idx_stock_orders_stock ON stock_orders (equity_id, status);

CREATE INDEX idx_policies_holder ON insurance_policies (holder_type, holder_id);

CREATE UNIQUE INDEX idx_engine_events_idem ON engine_events (idem_key)
WHERE
    idem_key IS NOT NULL;

CREATE INDEX idx_engine_events_ts ON engine_events (ts);

CREATE INDEX idx_engine_events_actor_ts ON engine_events (actor_player_id, ts);

CREATE INDEX idx_engine_events_sector_ts ON engine_events (sector_id, ts);

CREATE UNIQUE INDEX idx_engine_cmds_idem ON engine_commands (idem_key)
WHERE
    idem_key IS NOT NULL;

CREATE INDEX idx_engine_cmds_status_due ON engine_commands (status, due_at);

CREATE INDEX idx_engine_cmds_prio_due ON engine_commands (priority, due_at);

CREATE INDEX ix_news_feed_pub_ts ON news_feed (published_ts);

