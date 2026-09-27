-- MySQL key/foreign-key repair for existing v2 installations.
-- Apply after 098 and before 100. DDL commits independently. Values are
-- checked before narrowing; invalid rows abort with the original values intact.
DROP PROCEDURE IF EXISTS migrate_mysql_keys_099;
DROP PROCEDURE IF EXISTS migrate_mysql_foreign_keys_099;
CREATE TABLE IF NOT EXISTS ship_cargo (
    ship_id BIGINT NOT NULL,
    commodity_code VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    quantity BIGINT NOT NULL DEFAULT 0 CHECK (quantity >= 0),
    PRIMARY KEY (ship_id, commodity_code),
    FOREIGN KEY (ship_id) REFERENCES ships (ship_id) ON DELETE CASCADE
) ENGINE=InnoDB;
DELIMITER $$
CREATE PROCEDURE migrate_mysql_keys_099()
BEGIN
  DECLARE done INT DEFAULT 0;
  DECLARE tab_name VARCHAR(64);
  DECLARE col_name VARCHAR(64);
  DECLARE col_size INT;
  DECLARE col_definition TEXT;
  DECLARE current_count INT DEFAULT 0;
  DECLARE table_count INT DEFAULT 0;
  DECLARE longest_value INT DEFAULT 0;
  DECLARE drop_tab VARCHAR(64) DEFAULT NULL;
  DECLARE drop_constraint VARCHAR(64) DEFAULT NULL;
  DECLARE key_cursor CURSOR FOR
SELECT 'config', 'key', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'sessions', 'token', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'idempotency', 'key', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'locks', 'lock_name', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'engine_state', 'state_key', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'shiptypes', 'name', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'planettypes', 'code', 64, 'VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin'
UNION ALL
SELECT 'economy_curve', 'curve_name', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'alignment_band', 'code', 64, 'VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'trade_idempotency', 'key', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'tavern_names', 'name', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'tavern_lottery_state', 'draw_date', 10, 'VARCHAR(10) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'ship_markers', 'marker_type', 64, 'VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'hardware_items', 'code', 64, 'VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'subscriptions', 'event_type', 128, 'VARCHAR(128) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'player_prefs', 'key', 128, 'VARCHAR(128) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'player_bookmarks', 'name', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'player_notes', 'scope', 64, 'VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'player_notes', 'key', 128, 'VARCHAR(128) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'planet_goods', 'commodity', 64, 'VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'commodities', 'code', 64, 'VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'port_commodity_state', 'commodity_code', 64, 'VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'ship_cargo', 'commodity_code', 64, 'VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'clusters', 'name', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'cluster_commodity_index', 'commodity_code', 64, 'VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'currencies', 'code', 16, 'VARCHAR(16) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'entity_stock', 'entity_type', 16, 'VARCHAR(16) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'entity_stock', 'commodity_code', 64, 'VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'planet_production', 'commodity_code', 64, 'VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'bank_accounts', 'currency', 16, 'VARCHAR(16) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT ''CRD'''
UNION ALL
SELECT 'bank_interest_policy', 'currency', 16, 'VARCHAR(16) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT ''CRD'''
UNION ALL
SELECT 'bank_orders', 'currency', 16, 'VARCHAR(16) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT ''CRD'''
UNION ALL
SELECT 'corp_accounts', 'currency', 16, 'VARCHAR(16) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT ''CRD'''
UNION ALL
SELECT 'corp_tx', 'currency', 16, 'VARCHAR(16) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT ''CRD'''
UNION ALL
SELECT 'corp_tx', 'idempotency_key', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin'
UNION ALL
SELECT 'corp_interest_policy', 'currency', 16, 'VARCHAR(16) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT ''CRD'''
UNION ALL
SELECT 'stocks', 'ticker', 16, 'VARCHAR(16) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'stock_indices', 'name', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'credit_ratings', 'entity_type', 16, 'VARCHAR(16) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL CHECK (entity_type IN (''player'', ''corp''))'
UNION ALL
SELECT 'charters', 'name', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'expedition_backers', 'backer_type', 32, 'VARCHAR(32) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL CHECK (backer_type IN (''player'', ''corp''))'
UNION ALL
SELECT 'gov_accounts', 'name', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'research_contributors', 'actor_type', 32, 'VARCHAR(32) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL CHECK (actor_type IN (''player'', ''corp''))'
UNION ALL
SELECT 'charities', 'name', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'temples', 'name', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'guilds', 'name', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'guild_memberships', 'member_type', 32, 'VARCHAR(32) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL CHECK (member_type IN (''player'', ''corp''))'
UNION ALL
SELECT 'economy_policies', 'name', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 's2s_keys', 'key_id', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'cron_tasks', 'name', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'engine_offset', 'key', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'idempotency', 'cmd', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'players', 'name', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'corporations', 'name', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'corporations', 'tag', 16, 'VARCHAR(16) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin'
UNION ALL
SELECT 'corp_members', 'role', 32, 'VARCHAR(32) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT ''Member'''
UNION ALL
SELECT 'corp_log', 'event_type', 128, 'VARCHAR(128) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'mail', 'idempotency_key', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin'
UNION ALL
SELECT 'system_events', 'scope', 64, 'VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'commodity_orders', 'status', 16, 'VARCHAR(16) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT ''open'''
UNION ALL
SELECT 'bank_accounts', 'owner_type', 32, 'VARCHAR(32) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'bank_transactions', 'idempotency_key', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin'
UNION ALL
SELECT 'bank_fee_schedules', 'tx_type', 64, 'VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'bank_fee_schedules', 'owner_type', 32, 'VARCHAR(32) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin'
UNION ALL
SELECT 'bank_fee_schedules', 'currency', 16, 'VARCHAR(16) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT ''CRD'''
UNION ALL
SELECT 'stock_orders', 'status', 16, 'VARCHAR(16) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT ''open'''
UNION ALL
SELECT 'insurance_policies', 'holder_type', 32, 'VARCHAR(32) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL'
UNION ALL
SELECT 'engine_events', 'idem_key', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin'
UNION ALL
SELECT 'engine_commands', 'idem_key', 191, 'VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin'
UNION ALL
SELECT 'engine_commands', 'status', 32, 'VARCHAR(32) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT ''ready''';
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET done = 1;

  -- Preflight every bounded key before starting DDL.
  OPEN key_cursor;
  key_scan: LOOP
    FETCH key_cursor INTO tab_name, col_name, col_size, col_definition;
    IF done THEN LEAVE key_scan; END IF;
    SELECT COUNT(*) INTO table_count FROM information_schema.TABLES
     WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = tab_name;
    IF table_count > 0 THEN
      SET @key_sql = CONCAT('SELECT COALESCE(MAX(CHAR_LENGTH(`', col_name,
                           '`)), 0) INTO @key_longest FROM `', tab_name, '`');
      PREPARE key_stmt FROM @key_sql;
      EXECUTE key_stmt;
      DEALLOCATE PREPARE key_stmt;
      SET longest_value = COALESCE(@key_longest, 0);
      IF longest_value > col_size THEN
        SIGNAL SQLSTATE '45000'
          SET MESSAGE_TEXT = 'key value exceeds target VARCHAR width; no key columns altered';
      END IF;
    END IF;
  END LOOP;
  CLOSE key_cursor;

  -- Detach character-key FKs while parent and child columns are normalized.
  SET done = 0;
  drop_key_fks: LOOP
    SET drop_tab = NULL;
    SET drop_constraint = NULL;
    SELECT DISTINCT k.TABLE_NAME, k.CONSTRAINT_NAME
      INTO drop_tab, drop_constraint
      FROM information_schema.KEY_COLUMN_USAGE k
      JOIN (
SELECT 'config' AS table_name, 'key' AS column_name
UNION ALL
SELECT 'sessions', 'token'
UNION ALL
SELECT 'idempotency', 'key'
UNION ALL
SELECT 'locks', 'lock_name'
UNION ALL
SELECT 'engine_state', 'state_key'
UNION ALL
SELECT 'shiptypes', 'name'
UNION ALL
SELECT 'planettypes', 'code'
UNION ALL
SELECT 'economy_curve', 'curve_name'
UNION ALL
SELECT 'alignment_band', 'code'
UNION ALL
SELECT 'trade_idempotency', 'key'
UNION ALL
SELECT 'tavern_names', 'name'
UNION ALL
SELECT 'tavern_lottery_state', 'draw_date'
UNION ALL
SELECT 'ship_markers', 'marker_type'
UNION ALL
SELECT 'hardware_items', 'code'
UNION ALL
SELECT 'subscriptions', 'event_type'
UNION ALL
SELECT 'player_prefs', 'key'
UNION ALL
SELECT 'player_bookmarks', 'name'
UNION ALL
SELECT 'player_notes', 'scope'
UNION ALL
SELECT 'player_notes', 'key'
UNION ALL
SELECT 'planet_goods', 'commodity'
UNION ALL
SELECT 'commodities', 'code'
UNION ALL
SELECT 'port_commodity_state', 'commodity_code'
UNION ALL
SELECT 'port_commodity_state', 'port_id'
UNION ALL
SELECT 'ship_cargo', 'commodity_code'
UNION ALL
SELECT 'clusters', 'name'
UNION ALL
SELECT 'cluster_commodity_index', 'commodity_code'
UNION ALL
SELECT 'currencies', 'code'
UNION ALL
SELECT 'entity_stock', 'entity_type'
UNION ALL
SELECT 'entity_stock', 'commodity_code'
UNION ALL
SELECT 'planet_production', 'commodity_code'
UNION ALL
SELECT 'bank_accounts', 'currency'
UNION ALL
SELECT 'bank_interest_policy', 'currency'
UNION ALL
SELECT 'bank_orders', 'currency'
UNION ALL
SELECT 'corp_accounts', 'currency'
UNION ALL
SELECT 'corp_tx', 'currency'
UNION ALL
SELECT 'corp_tx', 'idempotency_key'
UNION ALL
SELECT 'corp_interest_policy', 'currency'
UNION ALL
SELECT 'stocks', 'ticker'
UNION ALL
SELECT 'stock_indices', 'name'
UNION ALL
SELECT 'credit_ratings', 'entity_type'
UNION ALL
SELECT 'charters', 'name'
UNION ALL
SELECT 'expedition_backers', 'backer_type'
UNION ALL
SELECT 'gov_accounts', 'name'
UNION ALL
SELECT 'research_contributors', 'actor_type'
UNION ALL
SELECT 'charities', 'name'
UNION ALL
SELECT 'temples', 'name'
UNION ALL
SELECT 'guilds', 'name'
UNION ALL
SELECT 'guild_memberships', 'member_type'
UNION ALL
SELECT 'economy_policies', 'name'
UNION ALL
SELECT 's2s_keys', 'key_id'
UNION ALL
SELECT 'cron_tasks', 'name'
UNION ALL
SELECT 'engine_offset', 'key'
      ) changed_keys
        ON (k.TABLE_NAME = changed_keys.table_name AND k.COLUMN_NAME = changed_keys.column_name)
        OR (k.REFERENCED_TABLE_NAME = changed_keys.table_name
            AND k.REFERENCED_COLUMN_NAME = changed_keys.column_name)
     WHERE k.TABLE_SCHEMA = DATABASE()
       AND k.REFERENCED_TABLE_NAME IS NOT NULL
       AND k.CONSTRAINT_NAME <> 'PRIMARY'
     LIMIT 1;
    IF done = 1 OR drop_tab IS NULL THEN LEAVE drop_key_fks; END IF;
    SET @key_sql = CONCAT('ALTER TABLE `', drop_tab, '` DROP FOREIGN KEY `', drop_constraint, '`');
    PREPARE key_stmt FROM @key_sql;
    EXECUTE key_stmt;
    DEALLOCATE PREPARE key_stmt;
  END LOOP;

  SET done = 0;
  OPEN key_cursor;
  key_alter: LOOP
    FETCH key_cursor INTO tab_name, col_name, col_size, col_definition;
    IF done THEN LEAVE key_alter; END IF;
    SELECT COUNT(*) INTO table_count FROM information_schema.TABLES
     WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = tab_name;
    IF table_count > 0 THEN
      SELECT COUNT(*) INTO current_count
        FROM information_schema.COLUMNS
       WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = tab_name
         AND COLUMN_NAME = col_name AND DATA_TYPE = 'varchar'
         AND CHARACTER_MAXIMUM_LENGTH = col_size
         AND CHARACTER_SET_NAME = 'utf8mb4' AND COLLATION_NAME = 'utf8mb4_bin';
      IF current_count = 0 THEN
        SET @key_sql = CONCAT('ALTER TABLE `', tab_name, '` MODIFY COLUMN `',
                              col_name, '` ', col_definition);
        PREPARE key_stmt FROM @key_sql;
        EXECUTE key_stmt;
        DEALLOCATE PREPARE key_stmt;
      END IF;
    END IF;
  END LOOP;
  CLOSE key_cursor;

  -- Migration 105 initially used INT although ports.port_id is BIGINT.
  SELECT COUNT(*) INTO table_count FROM information_schema.TABLES
   WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'port_commodity_state';
  IF table_count > 0 THEN
    SELECT COUNT(*) INTO current_count FROM information_schema.COLUMNS
     WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'port_commodity_state'
       AND COLUMN_NAME = 'port_id' AND DATA_TYPE = 'bigint';
    IF current_count = 0 THEN
      ALTER TABLE port_commodity_state MODIFY COLUMN port_id BIGINT NOT NULL;
    END IF;
  END IF;
END$$

CREATE PROCEDURE migrate_mysql_foreign_keys_099(IN add_constraints BOOLEAN)
main: BEGIN
  DECLARE done INT DEFAULT 0;
  DECLARE tab_name VARCHAR(64);
  DECLARE col_name VARCHAR(64);
  DECLARE parent_name VARCHAR(64);
  DECLARE parent_col VARCHAR(64);
  DECLARE ref_action VARCHAR(128);
  DECLARE fk_num INT;
  DECLARE fk_exists INT DEFAULT 0;
  DECLARE table_count INT DEFAULT 0;
  DECLARE orphan_count BIGINT DEFAULT 0;
  DECLARE fk_cursor CURSOR FOR
SELECT 'shiptypes', 'required_commission', 'commission', 'commission_id', '', 1
UNION ALL
SELECT 'shiptype_restrictions', 'shiptypes_id', 'shiptypes', 'shiptypes_id', 'ON DELETE CASCADE', 2
UNION ALL
SELECT 'ships', 'type_id', 'shiptypes', 'shiptypes_id', '', 3
UNION ALL
SELECT 'ships', 'sector_id', 'sectors', 'sector_id', '', 4
UNION ALL
SELECT 'players', 'commission_id', 'commission', 'commission_id', '', 5
UNION ALL
SELECT 'players', 'sector_id', 'sectors', 'sector_id', '', 6
UNION ALL
SELECT 'players', 'ship_id', 'ships', 'ship_id', '', 7
UNION ALL
SELECT 'corp_members', 'corporation_id', 'corporations', 'corporation_id', 'ON DELETE CASCADE ON UPDATE CASCADE', 8
UNION ALL
SELECT 'corp_members', 'player_id', 'players', 'player_id', '', 9
UNION ALL
SELECT 'corp_mail', 'corporation_id', 'corporations', 'corporation_id', 'ON DELETE CASCADE', 10
UNION ALL
SELECT 'corp_mail', 'sender_id', 'players', 'player_id', 'ON DELETE SET NULL', 11
UNION ALL
SELECT 'corp_mail_cursors', 'corporation_id', 'corporations', 'corporation_id', 'ON DELETE CASCADE', 12
UNION ALL
SELECT 'corp_mail_cursors', 'player_id', 'players', 'player_id', 'ON DELETE CASCADE', 13
UNION ALL
SELECT 'corp_log', 'corporation_id', 'corporations', 'corporation_id', 'ON DELETE CASCADE', 14
UNION ALL
SELECT 'corp_log', 'actor_id', 'players', 'player_id', 'ON DELETE SET NULL', 15
UNION ALL
SELECT 'taverns', 'sector_id', 'sectors', 'sector_id', '', 16
UNION ALL
SELECT 'taverns', 'name_id', 'tavern_names', 'tavern_names_id', '', 17
UNION ALL
SELECT 'tavern_lottery_tickets', 'player_id', 'players', 'player_id', '', 18
UNION ALL
SELECT 'tavern_deadpool_bets', 'bettor_id', 'players', 'player_id', '', 19
UNION ALL
SELECT 'tavern_deadpool_bets', 'target_id', 'players', 'player_id', '', 20
UNION ALL
SELECT 'tavern_graffiti', 'player_id', 'players', 'player_id', '', 21
UNION ALL
SELECT 'tavern_notices', 'author_id', 'players', 'player_id', '', 22
UNION ALL
SELECT 'corp_recruiting', 'corp_id', 'corporations', 'corporation_id', '', 23
UNION ALL
SELECT 'corp_invites', 'corp_id', 'corporations', 'corporation_id', 'ON DELETE CASCADE', 24
UNION ALL
SELECT 'corp_invites', 'player_id', 'players', 'player_id', 'ON DELETE CASCADE', 25
UNION ALL
SELECT 'tavern_loans', 'player_id', 'players', 'player_id', '', 26
UNION ALL
SELECT 'ports', 'economy_curve_id', 'economy_curve', 'economy_curve_id', '', 27
UNION ALL
SELECT 'ports', 'sector_id', 'sectors', 'sector_id', '', 28
UNION ALL
SELECT 'port_trade', 'port_id', 'ports', 'port_id', '', 29
UNION ALL
SELECT 'sector_warps', 'from_sector', 'sectors', 'sector_id', 'ON DELETE CASCADE', 30
UNION ALL
SELECT 'sector_warps', 'to_sector', 'sectors', 'sector_id', 'ON DELETE CASCADE', 31
UNION ALL
SELECT 'ship_markers', 'ship_id', 'ships', 'ship_id', '', 32
UNION ALL
SELECT 'ship_ownership', 'ship_id', 'ships', 'ship_id', '', 33
UNION ALL
SELECT 'ship_ownership', 'player_id', 'players', 'player_id', '', 34
UNION ALL
SELECT 'planets', 'sector_id', 'sectors', 'sector_id', '', 35
UNION ALL
SELECT 'planets', 'type', 'planettypes', 'planettypes_id', '', 36
UNION ALL
SELECT 'citadel_requirements', 'planet_type_id', 'planettypes', 'planettypes_id', 'ON DELETE CASCADE', 37
UNION ALL
SELECT 'citadels', 'planet_id', 'planets', 'planet_id', 'ON DELETE CASCADE', 38
UNION ALL
SELECT 'citadels', 'owner_id', 'players', 'player_id', '', 39
UNION ALL
SELECT 'turns', 'player_id', 'players', 'player_id', 'ON DELETE CASCADE', 40
UNION ALL
SELECT 'mail', 'sender_id', 'players', 'player_id', 'ON DELETE CASCADE', 41
UNION ALL
SELECT 'mail', 'recipient_id', 'players', 'player_id', 'ON DELETE CASCADE', 42
UNION ALL
SELECT 'subspace', 'sender_id', 'players', 'player_id', 'ON DELETE SET NULL', 43
UNION ALL
SELECT 'subspace_cursors', 'player_id', 'players', 'player_id', 'ON DELETE CASCADE', 44
UNION ALL
SELECT 'subscriptions', 'player_id', 'players', 'player_id', 'ON DELETE CASCADE', 45
UNION ALL
SELECT 'player_bookmarks', 'player_id', 'players', 'player_id', 'ON DELETE CASCADE', 46
UNION ALL
SELECT 'player_bookmarks', 'sector_id', 'sectors', 'sector_id', 'ON DELETE CASCADE', 47
UNION ALL
SELECT 'player_avoid', 'player_id', 'players', 'player_id', 'ON DELETE CASCADE', 48
UNION ALL
SELECT 'player_avoid', 'sector_id', 'sectors', 'sector_id', 'ON DELETE CASCADE', 49
UNION ALL
SELECT 'player_notes', 'player_id', 'players', 'player_id', 'ON DELETE CASCADE', 50
UNION ALL
SELECT 'sector_assets', 'sector_id', 'sectors', 'sector_id', '', 51
UNION ALL
SELECT 'sector_assets', 'owner_id', 'players', 'player_id', '', 52
UNION ALL
SELECT 'limpet_attached', 'ship_id', 'ships', 'ship_id', 'ON DELETE CASCADE', 53
UNION ALL
SELECT 'limpet_attached', 'owner_player_id', 'players', 'player_id', 'ON DELETE CASCADE', 54
UNION ALL
SELECT 'msl_sectors', 'sector_id', 'sectors', 'sector_id', '', 55
UNION ALL
SELECT 'trade_log', 'player_id', 'players', 'player_id', '', 56
UNION ALL
SELECT 'trade_log', 'port_id', 'ports', 'port_id', '', 57
UNION ALL
SELECT 'trade_log', 'sector_id', 'sectors', 'sector_id', '', 58
UNION ALL
SELECT 'stardock_assets', 'sector_id', 'sectors', 'sector_id', '', 59
UNION ALL
SELECT 'stardock_assets', 'owner_id', 'players', 'player_id', '', 60
UNION ALL
SELECT 'shipyard_inventory', 'port_id', 'ports', 'port_id', '', 61
UNION ALL
SELECT 'shipyard_inventory', 'ship_type_id', 'shiptypes', 'shiptypes_id', '', 62
UNION ALL
SELECT 'podded_status', 'player_id', 'players', 'player_id', '', 63
UNION ALL
SELECT 'planet_goods', 'planet_id', 'planets', 'planet_id', '', 64
UNION ALL
SELECT 'ship_cargo', 'ship_id', 'ships', 'ship_id', 'ON DELETE CASCADE', 65
UNION ALL
SELECT 'ship_cargo', 'commodity_code', 'commodities', 'code', 'ON DELETE RESTRICT', 66
UNION ALL
SELECT 'cluster_sectors', 'cluster_id', 'clusters', 'clusters_id', 'ON DELETE CASCADE', 67
UNION ALL
SELECT 'cluster_sectors', 'sector_id', 'sectors', 'sector_id', 'ON DELETE CASCADE', 68
UNION ALL
SELECT 'cluster_commodity_index', 'cluster_id', 'clusters', 'clusters_id', 'ON DELETE CASCADE', 69
UNION ALL
SELECT 'cluster_commodity_index', 'commodity_code', 'commodities', 'code', 'ON DELETE CASCADE', 70
UNION ALL
SELECT 'cluster_player_status', 'cluster_id', 'clusters', 'clusters_id', '', 71
UNION ALL
SELECT 'cluster_player_status', 'player_id', 'players', 'player_id', '', 72
UNION ALL
SELECT 'port_busts', 'port_id', 'ports', 'port_id', '', 73
UNION ALL
SELECT 'port_busts', 'player_id', 'players', 'player_id', '', 74
UNION ALL
SELECT 'commodity_orders', 'commodity_id', 'commodities', 'commodities_id', 'ON DELETE CASCADE', 75
UNION ALL
SELECT 'commodity_trades', 'commodity_id', 'commodities', 'commodities_id', 'ON DELETE CASCADE', 76
UNION ALL
SELECT 'entity_stock', 'commodity_code', 'commodities', 'code', '', 77
UNION ALL
SELECT 'planet_production', 'planet_type_id', 'planettypes', 'planettypes_id', 'ON DELETE CASCADE', 78
UNION ALL
SELECT 'planet_production', 'commodity_code', 'commodities', 'code', 'ON DELETE CASCADE', 79
UNION ALL
SELECT 'bank_accounts', 'currency', 'currencies', 'code', '', 80
UNION ALL
SELECT 'bank_transactions', 'account_id', 'bank_accounts', 'id', '', 81
UNION ALL
SELECT 'bank_interest_policy', 'currency', 'currencies', 'code', '', 82
UNION ALL
SELECT 'bank_orders', 'player_id', 'players', 'player_id', 'ON DELETE CASCADE', 83
UNION ALL
SELECT 'bank_orders', 'currency', 'currencies', 'code', '', 84
UNION ALL
SELECT 'bank_flags', 'player_id', 'players', 'player_id', 'ON DELETE CASCADE', 85
UNION ALL
SELECT 'corp_accounts', 'corp_id', 'corporations', 'corporation_id', 'ON DELETE CASCADE', 86
UNION ALL
SELECT 'corp_accounts', 'currency', 'currencies', 'code', '', 87
UNION ALL
SELECT 'corp_tx', 'corp_id', 'corporations', 'corporation_id', 'ON DELETE CASCADE', 88
UNION ALL
SELECT 'corp_tx', 'currency', 'currencies', 'code', '', 89
UNION ALL
SELECT 'corp_interest_policy', 'currency', 'currencies', 'code', '', 90
UNION ALL
SELECT 'stocks', 'corp_id', 'corporations', 'corporation_id', 'ON DELETE CASCADE', 91
UNION ALL
SELECT 'corp_shareholders', 'corp_id', 'corporations', 'corporation_id', 'ON DELETE CASCADE', 92
UNION ALL
SELECT 'corp_shareholders', 'player_id', 'players', 'player_id', 'ON DELETE CASCADE', 93
UNION ALL
SELECT 'stock_orders', 'player_id', 'players', 'player_id', 'ON DELETE CASCADE', 94
UNION ALL
SELECT 'stock_orders', 'equity_id', 'stocks', 'id', 'ON DELETE CASCADE', 95
UNION ALL
SELECT 'stock_trades', 'equity_id', 'stocks', 'id', 'ON DELETE CASCADE', 96
UNION ALL
SELECT 'stock_trades', 'buyer_id', 'players', 'player_id', 'ON DELETE CASCADE', 97
UNION ALL
SELECT 'stock_trades', 'seller_id', 'players', 'player_id', 'ON DELETE CASCADE', 98
UNION ALL
SELECT 'stock_dividends', 'equity_id', 'stocks', 'id', 'ON DELETE CASCADE', 99
UNION ALL
SELECT 'stock_index_members', 'index_id', 'stock_indices', 'id', 'ON DELETE CASCADE', 100
UNION ALL
SELECT 'stock_index_members', 'equity_id', 'stocks', 'id', 'ON DELETE CASCADE', 101
UNION ALL
SELECT 'insurance_policies', 'fund_id', 'insurance_funds', 'fund_id', 'ON DELETE SET NULL', 102
UNION ALL
SELECT 'insurance_claims', 'policy_id', 'insurance_policies', 'insurance_policies_id', 'ON DELETE CASCADE', 103
UNION ALL
SELECT 'loan_payments', 'loan_id', 'loans', 'loans_id', 'ON DELETE CASCADE', 104
UNION ALL
SELECT 'collateral', 'loan_id', 'loans', 'loans_id', 'ON DELETE CASCADE', 105
UNION ALL
SELECT 'expeditions', 'leader_player_id', 'players', 'player_id', 'ON DELETE CASCADE', 106
UNION ALL
SELECT 'expeditions', 'charter_id', 'charters', 'charters_id', 'ON DELETE SET NULL', 107
UNION ALL
SELECT 'expedition_backers', 'expedition_id', 'expeditions', 'expeditions_id', 'ON DELETE CASCADE', 108
UNION ALL
SELECT 'expedition_returns', 'expedition_id', 'expeditions', 'expeditions_id', 'ON DELETE CASCADE', 109
UNION ALL
SELECT 'futures_contracts', 'buyer_id', 'players', 'player_id', 'ON DELETE CASCADE', 110
UNION ALL
SELECT 'futures_contracts', 'seller_id', 'players', 'player_id', 'ON DELETE CASCADE', 111
UNION ALL
SELECT 'futures_contracts', 'commodity_id', 'commodities', 'commodities_id', 'ON DELETE CASCADE', 112
UNION ALL
SELECT 'tax_ledgers', 'policy_id', 'tax_policies', 'tax_policies_id', 'ON DELETE CASCADE', 113
UNION ALL
SELECT 'research_contributors', 'project_id', 'research_projects', 'research_projects_id', 'ON DELETE CASCADE', 114
UNION ALL
SELECT 'research_results', 'project_id', 'research_projects', 'research_projects_id', 'ON DELETE CASCADE', 115
UNION ALL
SELECT 'laundering_ops', 'from_black_id', 'black_accounts', 'black_accounts_id', 'ON DELETE SET NULL', 116
UNION ALL
SELECT 'laundering_ops', 'to_player_id', 'players', 'player_id', 'ON DELETE SET NULL', 117
UNION ALL
SELECT 'contracts_illicit', 'escrow_black_id', 'black_accounts', 'black_accounts_id', 'ON DELETE SET NULL', 118
UNION ALL
SELECT 'donations', 'charity_id', 'charities', 'charities_id', 'ON DELETE CASCADE', 119
UNION ALL
SELECT 'guild_memberships', 'guild_id', 'guilds', 'guilds_id', 'ON DELETE CASCADE', 120
UNION ALL
SELECT 'guild_dues', 'guild_id', 'guilds', 'guilds_id', 'ON DELETE CASCADE', 121
UNION ALL
SELECT 'player_known_ports', 'player_id', 'players', 'player_id', 'ON DELETE CASCADE', 122
UNION ALL
SELECT 'player_known_ports', 'port_id', 'ports', 'port_id', 'ON DELETE CASCADE', 123
UNION ALL
SELECT 'player_visited_sectors', 'player_id', 'players', 'player_id', 'ON DELETE CASCADE', 124
UNION ALL
SELECT 'player_visited_sectors', 'sector_id', 'sectors', 'sector_id', 'ON DELETE CASCADE', 125
UNION ALL
SELECT 'port_commodity_state', 'port_id', 'ports', 'port_id', 'ON DELETE CASCADE', 126
UNION ALL
SELECT 'port_commodity_state', 'commodity_code', 'commodities', 'code', 'ON DELETE CASCADE', 127;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET done = 1;

  OPEN fk_cursor;
  fk_scan: LOOP
    FETCH fk_cursor INTO tab_name, col_name, parent_name, parent_col, ref_action, fk_num;
    IF done THEN LEAVE fk_scan; END IF;
    SELECT COUNT(*) INTO table_count FROM information_schema.TABLES
     WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME=tab_name;
    IF table_count > 0 THEN
      SET @fk_sql = CONCAT('SELECT COUNT(*) INTO @orphan_count FROM `', tab_name,
        '` c LEFT JOIN `', parent_name, '` p ON CAST(c.`', col_name,
        '` AS BINARY) = CAST(p.`', parent_col, '` AS BINARY)',
        ' WHERE c.`', col_name, '` IS NOT NULL AND p.`', parent_col, '` IS NULL');
      PREPARE fk_stmt FROM @fk_sql;
      EXECUTE fk_stmt;
      DEALLOCATE PREPARE fk_stmt;
      SET orphan_count = COALESCE(@orphan_count, 0);
      IF orphan_count > 0 THEN
        SIGNAL SQLSTATE '45000'
          SET MESSAGE_TEXT = 'orphan values found; no foreign keys added';
      END IF;
    END IF;
  END LOOP;
  CLOSE fk_cursor;

  -- Validate before the key conversion drops any existing constraints. This
  -- allows a failed preflight to leave the current schema constraints intact.
  IF NOT add_constraints THEN
    LEAVE main;
  END IF;

  SET done = 0;
  OPEN fk_cursor;
  fk_add: LOOP
    FETCH fk_cursor INTO tab_name, col_name, parent_name, parent_col, ref_action, fk_num;
    IF done THEN LEAVE fk_add; END IF;
    SELECT COUNT(*) INTO table_count FROM information_schema.TABLES
     WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME=tab_name;
    IF table_count > 0 THEN
      SELECT COUNT(*) INTO fk_exists
        FROM information_schema.KEY_COLUMN_USAGE
       WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = tab_name
         AND COLUMN_NAME = col_name AND REFERENCED_TABLE_NAME = parent_name
         AND REFERENCED_COLUMN_NAME = parent_col;
      IF fk_exists = 0 THEN
        SET @fk_sql = CONCAT('ALTER TABLE `', tab_name, '` ADD CONSTRAINT `fk_v2_099_',
          LPAD(fk_num, 3, '0'), '` FOREIGN KEY (`', col_name, '`) REFERENCES `',
          parent_name, '` (`', parent_col, '`) ',
          IF(ref_action = '', '', CONCAT(' ', ref_action)));
        PREPARE fk_stmt FROM @fk_sql;
        EXECUTE fk_stmt;
        DEALLOCATE PREPARE fk_stmt;
      END IF;
    END IF;
  END LOOP;
  CLOSE fk_cursor;
END main$$
DELIMITER ;

CALL migrate_mysql_foreign_keys_099(FALSE);
CALL migrate_mysql_keys_099();
DROP PROCEDURE migrate_mysql_keys_099;

CREATE TABLE IF NOT EXISTS chat (
    chat_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    sender_id BIGINT NOT NULL,
    recipient_id BIGINT,
    sector_id BIGINT,
    message TEXT NOT NULL,
    sent_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (sender_id) REFERENCES players (player_id),
    FOREIGN KEY (recipient_id) REFERENCES players (player_id),
    FOREIGN KEY (sector_id) REFERENCES sectors (sector_id)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS s2s_peers (
    peer_id VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin PRIMARY KEY,
    host VARCHAR(255) NOT NULL,
    port INT NOT NULL,
    enabled BOOLEAN NOT NULL DEFAULT TRUE,
    shared_key_id VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    last_seen_at TIMESTAMP NULL,
    notes TEXT,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (shared_key_id) REFERENCES s2s_keys (key_id)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS s2s_nonce_seen (
    peer_id VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    nonce VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    msg_ts TIMESTAMP NOT NULL,
    seen_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (peer_id, nonce),
    FOREIGN KEY (peer_id) REFERENCES s2s_peers (peer_id)
) ENGINE=InnoDB;
SET @idx_exists = (SELECT COUNT(*) FROM information_schema.STATISTICS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='chat' AND INDEX_NAME='idx_chat_recipient');
SET @idx_sql = IF(@idx_exists=0, 'CREATE INDEX idx_chat_recipient ON chat (recipient_id)', 'SELECT 1');
PREPARE idx_stmt FROM @idx_sql; EXECUTE idx_stmt; DEALLOCATE PREPARE idx_stmt;
SET @idx_exists = (SELECT COUNT(*) FROM information_schema.STATISTICS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='chat' AND INDEX_NAME='idx_chat_sector');
SET @idx_sql = IF(@idx_exists=0, 'CREATE INDEX idx_chat_sector ON chat (sector_id)', 'SELECT 1');
PREPARE idx_stmt FROM @idx_sql; EXECUTE idx_stmt; DEALLOCATE PREPARE idx_stmt;
SET @idx_exists = (SELECT COUNT(*) FROM information_schema.STATISTICS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='chat' AND INDEX_NAME='idx_chat_sent_at');
SET @idx_sql = IF(@idx_exists=0, 'CREATE INDEX idx_chat_sent_at ON chat (sent_at)', 'SELECT 1');
PREPARE idx_stmt FROM @idx_sql; EXECUTE idx_stmt; DEALLOCATE PREPARE idx_stmt;
SET @idx_exists = (SELECT COUNT(*) FROM information_schema.STATISTICS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='s2s_peers' AND INDEX_NAME='idx_s2s_peers_enabled');
SET @idx_sql = IF(@idx_exists=0, 'CREATE INDEX idx_s2s_peers_enabled ON s2s_peers (enabled)', 'SELECT 1');
PREPARE idx_stmt FROM @idx_sql; EXECUTE idx_stmt; DEALLOCATE PREPARE idx_stmt;
SET @idx_exists = (SELECT COUNT(*) FROM information_schema.STATISTICS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='s2s_peers' AND INDEX_NAME='idx_s2s_peers_host_port');
SET @idx_sql = IF(@idx_exists=0, 'CREATE INDEX idx_s2s_peers_host_port ON s2s_peers (host, port)', 'SELECT 1');
PREPARE idx_stmt FROM @idx_sql; EXECUTE idx_stmt; DEALLOCATE PREPARE idx_stmt;
SET @idx_exists = (SELECT COUNT(*) FROM information_schema.STATISTICS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='s2s_nonce_seen' AND INDEX_NAME='idx_s2s_nonce_seen_at');
SET @idx_sql = IF(@idx_exists=0, 'CREATE INDEX idx_s2s_nonce_seen_at ON s2s_nonce_seen (seen_at)', 'SELECT 1');
PREPARE idx_stmt FROM @idx_sql; EXECUTE idx_stmt; DEALLOCATE PREPARE idx_stmt;

CALL migrate_mysql_foreign_keys_099(TRUE);
DROP PROCEDURE migrate_mysql_foreign_keys_099;
