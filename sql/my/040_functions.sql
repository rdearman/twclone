-- Generated from PostgreSQL 040_functions.sql -> MySQL
-- BigBang stored procedures/functions for MySQL

DELIMITER $$

-- ---------------------------------------------------------------------------
-- Advisory lock helpers (simplified for MySQL - uses dummy tables)
-- ---------------------------------------------------------------------------
CREATE PROCEDURE bigbang_lock(IN p_key BIGINT)
BEGIN
    -- MySQL doesn't have advisory locks; this is a no-op
    -- Locking logic should be handled at application level
END$$

CREATE PROCEDURE bigbang_unlock(IN p_key BIGINT)
BEGIN
    -- MySQL doesn't have advisory locks; this is a no-op
END$$

-- ---------------------------------------------------------------------------
-- 1) Sectors
-- ---------------------------------------------------------------------------
CREATE FUNCTION generate_sectors(target_count INT) RETURNS BIGINT
DETERMINISTIC
BEGIN
    DECLARE v_max_id BIGINT;
    DECLARE v_added BIGINT DEFAULT 0;
    DECLARE v_lock_key BIGINT DEFAULT 72002001;
    DECLARE v_beacons LONGTEXT DEFAULT 'For a good time, warp to Sector 69.|The Federation is lying to you regarding Ore prices!|Don''t trust the auto-pilot...|Red Dragon Corp rules this void!|Will trade sister for a Genesis Torpedo.|I hid 500,000 credits in Sector... [text unreadable]|The Feds is watching you.|Mining is for droids. Real pilots steal.|If you can read this, you''re in range of my cannons.|Turn back. Here there be dragons.';
    
    CALL bigbang_lock(v_lock_key);
    
    SELECT COALESCE(MAX(sector_id), 0) INTO v_max_id FROM sectors;
    
    IF v_max_id >= target_count THEN
        CALL bigbang_unlock(v_lock_key);
        RETURN 0;
    END IF;
    
    INSERT INTO sectors (sector_id, name, beacon, nebulae)
    SELECT 
        gs AS sector_id,
        CASE WHEN gs > 10 AND RAND() < 0.6 THEN CONCAT('Nebula_', gs) ELSE NULL END AS name,
        CASE WHEN RAND() < 0.02 THEN ELT(FLOOR(RAND() * 10) + 1, 
            'For a good time, warp to Sector 69.',
            'The Federation is lying to you regarding Ore prices!',
            'Don''t trust the auto-pilot...',
            'Red Dragon Corp rules this void!',
            'Will trade sister for a Genesis Torpedo.',
            'I hid 500,000 credits in Sector... [text unreadable]',
            'The Feds is watching you.',
            'Mining is for droids. Real pilots steal.',
            'If you can read this, you''re in range of my cannons.',
            'Turn back. Here there be dragons.')
        ELSE NULL END AS beacon,
        CASE WHEN gs > 10 AND RAND() < 0.6 THEN CONCAT('Nebula_', gs) ELSE NULL END AS nebulae
    FROM (
        SELECT @counter := @counter + 1 AS gs FROM 
        (SELECT @counter := v_max_id - 1) init,
        (SELECT 1 UNION SELECT 2 UNION SELECT 3 UNION SELECT 4 UNION SELECT 5) a,
        (SELECT 1 UNION SELECT 2 UNION SELECT 3 UNION SELECT 4 UNION SELECT 5) b
    ) generated_series
    WHERE gs <= target_count;
    
    SELECT ROW_COUNT() INTO v_added;
    CALL bigbang_unlock(v_lock_key);
    RETURN v_added;
END$$

-- ---------------------------------------------------------------------------
-- 2) Ports
-- ---------------------------------------------------------------------------
CREATE FUNCTION generate_ports(target_sectors INT) RETURNS BIGINT
DETERMINISTIC
BEGIN
    DECLARE v_added BIGINT DEFAULT 0;
    DECLARE v_lock_key BIGINT DEFAULT 72002002;
    DECLARE v_target INT;
    DECLARE v_port_ratio INT;
    DECLARE v_commodity_id INT;
    DECLARE v_base_price INT;
    DECLARE v_code TEXT;
    DECLARE v_cluster_alignment INT;
    DECLARE port_cursor CURSOR FOR SELECT port_id, type, sector_id FROM new_ports;
    DECLARE CONTINUE HANDLER FOR NOT FOUND SET @done = 1;
    DECLARE v_port_id INT;
    DECLARE v_port_type INT;
    DECLARE v_sector_id INT;
    DECLARE @done INT DEFAULT 0;
    
    CALL bigbang_lock(v_lock_key);
    
    SELECT CAST(value AS INTEGER) INTO v_port_ratio FROM config WHERE key = 'port_ratio';
    IF v_port_ratio IS NULL OR v_port_ratio > 100 OR v_port_ratio < 0 THEN
        SET v_port_ratio = 40;
    END IF;
    
    IF target_sectors IS NULL THEN
        SELECT COALESCE(MAX(sector_id), 0) INTO v_target FROM sectors;
    ELSE
        SET v_target = target_sectors;
    END IF;
    
    IF v_target < 0 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'target_sectors must be >= 0';
    END IF;
    
    CALL generate_sectors(v_target);
    
    CREATE TEMPORARY TABLE new_ports (
        port_id INT,
        type INT,
        sector_id INT
    );
    
    INSERT INTO new_ports (port_id, type, sector_id)
    SELECT port_id, type, sector_id FROM (
        SELECT 
            NULL AS port_id,
            FLOOR(RAND() * 8 + 1) AS type,
            s.sector_id AS sector_id
        FROM sectors s
        WHERE s.sector_id > 10
            AND s.sector_id <= v_target
            AND NOT EXISTS (SELECT 1 FROM ports p WHERE p.sector_id = s.sector_id)
            AND RAND() <= (v_port_ratio / 100.0)
    ) AS new_data;
    
    OPEN port_cursor;
    port_loop: LOOP
        FETCH port_cursor INTO v_port_id, v_port_type, v_sector_id;
        IF @done THEN LEAVE port_loop; END IF;
        
        -- Insert port_trade records based on type
        IF v_port_type = 1 THEN
            INSERT INTO port_trade (port_id, commodity, mode) 
            VALUES (v_port_id, 'organics', 'buy'), 
                   (v_port_id, 'equipment', 'buy'), 
                   (v_port_id, 'ore', 'sell');
        ELSEIF v_port_type = 2 THEN
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES (v_port_id, 'ore', 'sell'),
                   (v_port_id, 'organics', 'sell'),
                   (v_port_id, 'equipment', 'buy');
        ELSEIF v_port_type = 3 THEN
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES (v_port_id, 'ore', 'buy'),
                   (v_port_id, 'organics', 'sell'),
                   (v_port_id, 'equipment', 'sell');
        ELSEIF v_port_type = 4 THEN
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES (v_port_id, 'ore', 'buy'),
                   (v_port_id, 'organics', 'sell'),
                   (v_port_id, 'equipment', 'buy');
        ELSEIF v_port_type = 5 THEN
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES (v_port_id, 'ore', 'buy'),
                   (v_port_id, 'organics', 'buy'),
                   (v_port_id, 'equipment', 'sell');
        ELSEIF v_port_type = 6 THEN
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES (v_port_id, 'ore', 'sell'),
                   (v_port_id, 'organics', 'buy'),
                   (v_port_id, 'equipment', 'sell');
        ELSEIF v_port_type = 7 THEN
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES (v_port_id, 'ore', 'buy'),
                   (v_port_id, 'organics', 'buy'),
                   (v_port_id, 'equipment', 'buy');
        ELSEIF v_port_type = 8 THEN
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES (v_port_id, 'ore', 'sell'),
                   (v_port_id, 'organics', 'sell'),
                   (v_port_id, 'equipment', 'sell');
        END IF;
    END LOOP;
    CLOSE port_cursor;
    
    DROP TEMPORARY TABLE IF EXISTS new_ports;
    SELECT ROW_COUNT() INTO v_added;
    CALL bigbang_unlock(v_lock_key);
    RETURN v_added;
END$$

-- ---------------------------------------------------------------------------
-- 2.5) Stardock
-- ---------------------------------------------------------------------------
CREATE FUNCTION generate_stardock() RETURNS BIGINT
DETERMINISTIC
BEGIN
    DECLARE v_sector INT;
    DECLARE v_max_sector INT;
    DECLARE v_lock_key BIGINT DEFAULT 72002006;
    DECLARE v_exists BOOLEAN;
    DECLARE v_port_id INT;
    
    CALL bigbang_lock(v_lock_key);
    
    SELECT EXISTS(SELECT 1 FROM ports WHERE type = 9) INTO v_exists;
    IF v_exists THEN
        CALL bigbang_unlock(v_lock_key);
        RETURN 0;
    END IF;
    
    SELECT MAX(sector_id) INTO v_max_sector FROM sectors;
    SET v_sector = 11 + FLOOR(RAND() * (v_max_sector - 10));
    
    INSERT INTO ports (number, name, sector_id, type, size, techlevel, petty_cash)
    VALUES (v_sector, 'Stardock', v_sector, 9, 10, 10, 1000000)
    ON DUPLICATE KEY UPDATE 
        name = VALUES(name), 
        type = VALUES(type), 
        size = VALUES(size), 
        techlevel = VALUES(techlevel), 
        petty_cash = VALUES(petty_cash);
    
    SELECT port_id INTO v_port_id FROM ports WHERE sector_id = v_sector AND type = 9 LIMIT 1;
    
    IF v_port_id IS NOT NULL THEN
        INSERT INTO stardock_assets (sector_id, owner_id, fighters, defenses, ship_capacity)
        VALUES (v_sector, 1, 10000, 500, 100)
        ON DUPLICATE KEY UPDATE 
            fighters = VALUES(fighters),
            defenses = VALUES(defenses),
            ship_capacity = VALUES(ship_capacity);
        
        INSERT INTO shipyard_inventory (port_id, ship_type_id, enabled)
        SELECT v_port_id, shiptypes_id, 1
        FROM shiptypes
        WHERE can_purchase = TRUE
        ON DUPLICATE KEY UPDATE enabled = VALUES(enabled);
    END IF;
    
    CALL bigbang_unlock(v_lock_key);
    RETURN 1;
END$$

-- ---------------------------------------------------------------------------
-- 3) Planets
-- ---------------------------------------------------------------------------
CREATE FUNCTION generate_planets(target_count INT) RETURNS BIGINT
DETERMINISTIC
BEGIN
    DECLARE v_existing BIGINT;
    DECLARE v_to_add BIGINT;
    DECLARE v_added BIGINT DEFAULT 0;
    DECLARE v_lock_key BIGINT DEFAULT 72002003;
    DECLARE v_sector_count BIGINT;
    DECLARE v_base_num BIGINT;
    DECLARE v_ptype BIGINT;
    
    CALL bigbang_lock(v_lock_key);
    
    SELECT COUNT(*) INTO v_existing FROM planets;
    SET v_to_add = GREATEST(target_count - v_existing, 0);
    
    IF v_to_add = 0 THEN
        CALL bigbang_unlock(v_lock_key);
        RETURN 0;
    END IF;
    
    SELECT COUNT(*) INTO v_sector_count FROM sectors;
    IF v_sector_count = 0 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Cannot generate planets: no sectors exist';
    END IF;
    
    SELECT COALESCE(MAX(num), 0) + 1 INTO v_base_num FROM planets;
    
    SELECT planettypes_id INTO v_ptype FROM planettypes ORDER BY planettypes_id LIMIT 1;
    IF v_ptype IS NULL THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Cannot generate planets: no planettypes exist';
    END IF;
    
    INSERT INTO planets (num, sector_id, name, owner_id, owner_type, class, type, created_at, created_by)
    SELECT 
        @num := @num + 1 AS num,
        ((@num - 1) % v_sector_count + 1) AS sector_id,
        CONCAT('Planet ', @num) AS name,
        0 AS owner_id,
        'none' AS owner_type,
        'M' AS class,
        v_ptype AS type,
        NOW() AS created_at,
        0 AS created_by
    FROM (SELECT @num := v_base_num - 1) init,
         (SELECT 1 UNION SELECT 2 UNION SELECT 3 UNION SELECT 4 UNION SELECT 5) a,
         (SELECT 1 UNION SELECT 2 UNION SELECT 3 UNION SELECT 4 UNION SELECT 5) b
    WHERE @num < v_base_num + v_to_add - 1;
    
    SELECT ROW_COUNT() INTO v_added;
    CALL bigbang_unlock(v_lock_key);
    RETURN v_added;
END$$

-- ---------------------------------------------------------------------------
-- 4) seed_factions()
-- ---------------------------------------------------------------------------
CREATE FUNCTION seed_factions() RETURNS BIGINT
DETERMINISTIC
BEGIN
    DECLARE v_added BIGINT DEFAULT 0;
    DECLARE v_rows BIGINT;
    DECLARE v_lock_key BIGINT DEFAULT 72002004;
    
    CALL bigbang_lock(v_lock_key);
    
    INSERT INTO corporations (name, owner_id, tag, description)
    SELECT 'Imperial', NULL, 'IMP', 'Imperial faction'
    WHERE NOT EXISTS (SELECT 1 FROM corporations WHERE LOWER(name) = LOWER('Imperial'));
    
    SET v_rows = ROW_COUNT();
    SET v_added = v_added + v_rows;
    
    INSERT INTO corporations (name, owner_id, tag, description)
    SELECT 'Ferringhi', NULL, 'FER', 'Ferringhi faction'
    WHERE NOT EXISTS (SELECT 1 FROM corporations WHERE LOWER(name) = LOWER('Ferringhi'));
    
    SET v_rows = ROW_COUNT();
    SET v_added = v_added + v_rows;
    
    CALL bigbang_unlock(v_lock_key);
    RETURN v_added;
END$$

-- ---------------------------------------------------------------------------
-- 5) Clusters
-- ---------------------------------------------------------------------------
CREATE FUNCTION generate_clusters(target_count INT) RETURNS BIGINT
DETERMINISTIC
BEGIN
    DECLARE v_added BIGINT DEFAULT 0;
    DECLARE v_lock_key BIGINT DEFAULT 72002005;
    DECLARE v_max_sector BIGINT;
    DECLARE v_center INT;
    DECLARE v_name TEXT;
    DECLARE v_align INT;
    DECLARE v_cluster_id INT;
    DECLARE i INT DEFAULT 1;
    
    CALL bigbang_lock(v_lock_key);
    
    DELETE FROM cluster_sectors;
    
    SELECT MAX(sector_id) INTO v_max_sector FROM sectors;
    IF v_max_sector IS NULL THEN
        CALL bigbang_unlock(v_lock_key);
        RETURN 0;
    END IF;
    
    INSERT INTO clusters (name, role, kind, center_sector, law_severity, alignment)
    VALUES ('Federation Core', 'FED', 'FACTION', 1, 3, 100)
    ON DUPLICATE KEY UPDATE role = VALUES(role), kind = VALUES(kind), center_sector = VALUES(center_sector);
    
    WHILE i < target_count DO
        SET v_center = 11 + FLOOR(RAND() * (v_max_sector - 10));
        SET v_name = CONCAT('Cluster ', v_center);
        SET v_align = FLOOR(RAND() * 200) - 100;
        
        INSERT INTO clusters (name, role, kind, center_sector, law_severity, alignment)
        VALUES (v_name, 'RANDOM', 'RANDOM', v_center, 1, v_align)
        ON DUPLICATE KEY UPDATE role = VALUES(role), kind = VALUES(kind), center_sector = VALUES(center_sector);
        
        SET i = i + 1;
    END WHILE;
    
    INSERT INTO cluster_sectors (cluster_id, sector_id)
    SELECT clusters_id, center_sector FROM clusters
    ON DUPLICATE KEY UPDATE sector_id = VALUES(sector_id);
    
    INSERT INTO cluster_sectors (cluster_id, sector_id)
    SELECT c.clusters_id, w.to_sector
    FROM clusters c
    JOIN sector_warps w ON w.from_sector = c.center_sector
    ON DUPLICATE KEY UPDATE sector_id = VALUES(sector_id);
    
    SELECT ROW_COUNT() INTO v_added;
    CALL bigbang_unlock(v_lock_key);
    RETURN v_added;
END$$

-- ---------------------------------------------------------------------------
-- 6) Taverns
-- ---------------------------------------------------------------------------
CREATE FUNCTION generate_taverns(p_ratio_pct INT) RETURNS BIGINT
DETERMINISTIC
BEGIN
    DECLARE v_added BIGINT DEFAULT 0;
    DECLARE v_lock_key BIGINT DEFAULT 72002007;
    DECLARE v_ratio DECIMAL(4, 2);
    
    CALL bigbang_lock(v_lock_key);
    
    SET v_ratio = p_ratio_pct / 100.0;
    
    INSERT INTO taverns (sector_id, name_id)
    SELECT s.sector_id, (SELECT tavern_names_id FROM tavern_names ORDER BY RAND() LIMIT 1)
    FROM sectors s
    WHERE s.sector_id > 10
        AND NOT EXISTS (SELECT 1 FROM taverns t WHERE t.sector_id = s.sector_id)
        AND RAND() < v_ratio;
    
    SELECT ROW_COUNT() INTO v_added;
    CALL bigbang_unlock(v_lock_key);
    RETURN v_added;
END$$

-- ---------------------------------------------------------------------------
-- 7) Player Registration - Auto-create starter ship
-- ---------------------------------------------------------------------------
CREATE FUNCTION register_player(
    p_username TEXT, 
    p_password TEXT, 
    p_ship_name TEXT, 
    p_is_npc BOOLEAN, 
    p_sector_id INTEGER
) RETURNS BIGINT
DETERMINISTIC
BEGIN
    DECLARE v_player_id BIGINT;
    DECLARE v_ship_id BIGINT;
    DECLARE v_ship_name TEXT;
    DECLARE v_starting_credits BIGINT;
    DECLARE v_turns_per_day INTEGER;
    
    SET v_ship_name = COALESCE(p_ship_name, 'Used Scout Marauder');
    
    SELECT CAST(value AS UNSIGNED) INTO v_starting_credits
    FROM config WHERE key = 'startingcredits';
    SELECT CAST(value AS UNSIGNED) INTO v_turns_per_day
    FROM config WHERE key = 'turnsperday';
    
    SET v_starting_credits = COALESCE(v_starting_credits, 5000);
    SET v_turns_per_day = COALESCE(v_turns_per_day, 500);
    
    INSERT INTO players (name, passwd, sector_id, credits, type, is_npc, loggedin, commission_id)
    VALUES (p_username, p_password, p_sector_id, v_starting_credits, 2, p_is_npc, NOW(), 1);
    
    SET v_player_id = LAST_INSERT_ID();
    
    INSERT INTO ships (name, type_id, sector_id, holds, fighters, shields)
    VALUES (v_ship_name, 2, p_sector_id, 10, 1, 1);
    
    SET v_ship_id = LAST_INSERT_ID();
    
    UPDATE players SET ship_id = v_ship_id WHERE player_id = v_player_id;
    
    INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary)
    VALUES (v_ship_id, v_player_id, 1, TRUE);
    
    INSERT INTO bank_accounts (owner_type, owner_id, currency, balance)
    VALUES ('player', v_player_id, 'CRD', 0)
    ON DUPLICATE KEY UPDATE balance = VALUES(balance);
    
    INSERT INTO turns (player_id, turns_remaining, last_update)
    VALUES (v_player_id, v_turns_per_day, NOW())
    ON DUPLICATE KEY UPDATE turns_remaining = VALUES(turns_remaining);
    
    INSERT INTO player_prefs (player_prefs_id, key, type, value)
    VALUES (v_player_id, 'default_preferences_initialized', 'bool', 'true')
    ON DUPLICATE KEY UPDATE value = VALUES(value);
    
    RETURN v_player_id;
END$$

-- ---------------------------------------------------------------------------
-- 8) Missing Stubs
-- ---------------------------------------------------------------------------
CREATE PROCEDURE setup_npc_homeworlds()
BEGIN
    DECLARE v_ptype INT;
    
    SELECT planettypes_id INTO v_ptype FROM planettypes WHERE code = 'M' LIMIT 1;
    
    INSERT IGNORE INTO planets (num, sector_id, name, owner_id, owner_type, class, type, created_at, created_by)
    VALUES (1, 1, 'Terra', 0, 'player', 'M', v_ptype, NOW(), 0);
END$$

CREATE PROCEDURE setup_ferringhi_alliance()
BEGIN
    DECLARE v_ptype INT;
    DECLARE v_ferringhi_corp_id INT;
    DECLARE v_ferringhi_sector INT DEFAULT 469;
    
    SELECT planettypes_id INTO v_ptype FROM planettypes WHERE code = 'M' LIMIT 1;
    SELECT id INTO v_ferringhi_corp_id FROM corporations WHERE LOWER(name) = LOWER('Ferringhi') LIMIT 1;
    SELECT COALESCE(exit_sector, 469) INTO v_ferringhi_sector FROM longest_tunnels LIMIT 1;
    
    IF v_ferringhi_sector IS NULL OR v_ferringhi_sector = 0 THEN
        SET v_ferringhi_sector = 469;
    END IF;
    
    INSERT IGNORE INTO planets (num, sector_id, name, owner_id, owner_type, class, type, created_at, created_by)
    VALUES (2, v_ferringhi_sector, 'Ferringhi Homeworld', COALESCE(v_ferringhi_corp_id, 0), 'player', 'M', v_ptype, NOW(), 0);
    
    INSERT IGNORE INTO sector_assets (sector_id, owner_id, corporation_id, asset_type, offensive_setting, quantity, deployed_at)
    VALUES (v_ferringhi_sector, 0, COALESCE(v_ferringhi_corp_id, 2), 2, 3, 50000, NOW());
    
    INSERT IGNORE INTO sector_assets (sector_id, owner_id, corporation_id, asset_type, quantity, deployed_at)
    VALUES (v_ferringhi_sector, 0, COALESCE(v_ferringhi_corp_id, 2), 1, 250, NOW());
END$$

CREATE PROCEDURE setup_orion_syndicate()
BEGIN
    DECLARE v_ptype INT;
    DECLARE v_orion_corp_id INT;
    DECLARE v_orion_sector INT DEFAULT 386;
    
    SELECT planettypes_id INTO v_ptype FROM planettypes WHERE code = 'M' LIMIT 1;
    SELECT id INTO v_orion_corp_id FROM corporations WHERE LOWER(name) = LOWER('Orion Syndicate') LIMIT 1;
    SELECT COALESCE(exit_sector, 386) INTO v_orion_sector FROM longest_tunnels LIMIT 1 OFFSET 1;
    
    IF v_orion_sector IS NULL OR v_orion_sector = 0 THEN
        SET v_orion_sector = FLOOR(RAND() * (989 - 11 + 1)) + 11;
    END IF;
    
    INSERT IGNORE INTO planets (num, sector_id, name, owner_id, owner_type, class, type, created_at, created_by)
    VALUES (3, v_orion_sector, 'Orion Hideout', COALESCE(v_orion_corp_id, 0), 'player', 'M', v_ptype, NOW(), 0);
    
    INSERT IGNORE INTO sector_assets (sector_id, owner_id, corporation_id, asset_type, offensive_setting, quantity, deployed_at)
    VALUES (v_orion_sector, 4, COALESCE(v_orion_corp_id, 1), 2, 2, 50000, NOW());
    
    INSERT IGNORE INTO sector_assets (sector_id, owner_id, corporation_id, asset_type, quantity, deployed_at)
    VALUES (v_orion_sector, 4, COALESCE(v_orion_corp_id, 1), 1, 250, NOW());
    
    INSERT IGNORE INTO ports (sector_id, name, type, port_code, created_by, created_at)
    VALUES (v_orion_sector, 'Orion Black Market', 10, 9998, 0, NOW());
END$$

CREATE PROCEDURE spawn_initial_fleet()
BEGIN
    -- Stub implementation
END$$

CREATE PROCEDURE apply_game_defaults()
BEGIN
    -- Stub implementation
END$$

DELIMITER ;

