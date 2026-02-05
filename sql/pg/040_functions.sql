-- 100_procs.sql
-- BigBang stored procedures/functions for Postgres.
-- Fixed: Column names and missing stubs.
BEGIN;
-- ---------------------------------------------------------------------------
-- Advisory lock helpers (prevents two bigbang runs colliding)
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION bigbang_lock (p_key bigint)
    RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE
    ok boolean;
BEGIN
    -- Don't block forever waiting for the advisory lock
    -- Fail fast if another session holds it
    ok := pg_try_advisory_lock(p_key);
    IF NOT ok THEN
        RAISE EXCEPTION 'bigbang_lock: could not acquire advisory lock %, another session is running bigbang', p_key;
    END IF;
END;
$$;
CREATE OR REPLACE FUNCTION bigbang_unlock (p_key bigint)
    RETURNS void
    LANGUAGE plpgsql
    AS $$
BEGIN
    PERFORM
        pg_advisory_unlock(p_key);
END;
$$;
-- ---------------------------------------------------------------------------
-- 1) Sectors
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION generate_sectors (target_count int)
    RETURNS integer
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_max_id bigint;
    v_added bigint := 0;
    v_lock_key bigint := 72002001;
    v_beacons text[] := ARRAY['For a good time, warp to Sector 69.', 'The Federation is lying to you regarding Ore prices!', 'Don''t trust the auto-pilot...', 'Red Dragon Corp rules this void!', 'Will trade sister for a Genesis Torpedo.', 'I hid 500,000 credits in Sector... [text unreadable]', 'The Feds is watching you.', 'Mining is for droids. Real pilots steal.', 'If you can read this, you''re in range of my cannons.', 'Turn back. Here there be dragons.'];
BEGIN
    IF target_count IS NULL OR target_count < 0 THEN
        RAISE EXCEPTION 'target_count must be >= 0';
    END IF;
    PERFORM
        bigbang_lock (v_lock_key);
    SELECT
        COALESCE(MAX(sector_id), 0) INTO v_max_id
    FROM
        sectors;
    IF v_max_id >= target_count THEN
        PERFORM
            bigbang_unlock (v_lock_key);
        RETURN 0;
    END IF;
    INSERT INTO sectors (sector_id, name, beacon, nebulae)
    SELECT
        gs AS sector_id,
        sub.name_val AS name,
        CASE WHEN random() < 0.02 THEN
            v_beacons[1 + floor(random() * array_length(v_beacons, 1))::int]
        ELSE
            NULL
        END AS beacon,
        sub.name_val AS nebulae
    FROM
        generate_series(v_max_id + 1, target_count) AS gs,
    LATERAL (
        SELECT
            CASE WHEN gs > 10
                AND random() < 0.6 THEN
                constellation_name ()
            ELSE
                NULL
            END AS name_val) sub;
    GET DIAGNOSTICS v_added = ROW_COUNT;
    PERFORM
        bigbang_unlock (v_lock_key);
    RETURN v_added;
END;
$$;
-- ---------------------------------------------------------------------------
-- 2) Ports
-- ---------------------------------------------------------------------------

CREATE OR REPLACE FUNCTION generate_ports (target_sectors int DEFAULT NULL)
    RETURNS integer
    LANGUAGE plpgsql
AS $$
DECLARE
    v_added bigint := 0;
    v_lock_key bigint := 72002002;
    v_target int;
    v_port_ratio int;
    v_port_rec record;

    v_size int;
    v_cap int;

    v_code_ore text;
    v_code_org text;
    v_code_equ text;

    v_base_ore int;
    v_base_org int;
    v_base_equ int;

BEGIN
    PERFORM bigbang_lock(v_lock_key);

    /* Get config value for port_ratio, default to 40 if not set */
    SELECT value::int INTO v_port_ratio
      FROM config
     WHERE key = 'port_ratio';

    IF v_port_ratio IS NULL OR v_port_ratio > 100 OR v_port_ratio < 0 THEN
        v_port_ratio := 40;
    END IF;

    IF target_sectors IS NULL THEN
        SELECT COALESCE(MAX(sector_id), 0)::int INTO v_target
          FROM sectors;
    ELSE
        v_target := target_sectors;
    END IF;

    IF v_target < 0 THEN
        RAISE EXCEPTION 'target_sectors must be >= 0';
    END IF;

    PERFORM generate_sectors(v_target);

    /* Temporary table to hold newly created port IDs and types */
    CREATE TEMP TABLE new_ports (
        port_id  int,
        type     int,
        sector_id int
    ) ON COMMIT DROP;

    /* Insert ports respecting port_ratio, and capture their IDs and types */
    WITH inserted_ports AS (
        INSERT INTO ports (number, name, sector_id, type)
        SELECT
            s.sector_id AS number,
            randomname() AS name,
            s.sector_id AS sector_id,
            floor(random() * 8 + 1)::int AS type
        FROM sectors s
        WHERE s.sector_id > 10                       /* Don't create ports in Fedspace core */
          AND s.sector_id <= v_target
          AND NOT EXISTS (
                SELECT 1
                  FROM ports p
                 WHERE p.sector_id = s.sector_id
          )
          AND random() <= (v_port_ratio / 100.0)
        RETURNING port_id, type, sector_id
    )
    INSERT INTO new_ports
    SELECT port_id, type, sector_id
      FROM inserted_ports;

    GET DIAGNOSTICS v_added = ROW_COUNT;

    /*
     * Resolve commodity codes + base prices once.
     * Supports either short codes (ORE/ORG/EQU) or long codes (ore/organics/equipment).
     */
    SELECT c.code, c.base_price INTO v_code_ore, v_base_ore
      FROM commodities c
     WHERE c.illegal = FALSE
       AND (upper(c.code) = 'ORE' OR lower(c.code) = 'ore')
     ORDER BY (upper(c.code) = 'ORE') DESC
     LIMIT 1;

    SELECT c.code, c.base_price INTO v_code_org, v_base_org
      FROM commodities c
     WHERE c.illegal = FALSE
       AND (upper(c.code) = 'ORG' OR lower(c.code) = 'organics')
     ORDER BY (upper(c.code) = 'ORG') DESC
     LIMIT 1;

    SELECT c.code, c.base_price INTO v_code_equ, v_base_equ
      FROM commodities c
     WHERE c.illegal = FALSE
       AND (upper(c.code) = 'EQU' OR lower(c.code) = 'equipment')
     ORDER BY (upper(c.code) = 'EQU') DESC
     LIMIT 1;

    IF v_code_ore IS NULL OR v_code_org IS NULL OR v_code_equ IS NULL THEN
        RAISE EXCEPTION
            'generate_ports: missing required commodities. Need ORE/ORG/EQU (or ore/organics/equipment) in commodities table.';
    END IF;

    /* Seed trade modes + stock for the new ports */
    FOR v_port_rec IN
        SELECT port_id, type, sector_id
          FROM new_ports
    LOOP
        /* Populate port_trade based on type (unchanged behaviour) */
        IF v_port_rec.type = 1 THEN
            /* SBB (Sell Ore, Buy Organics/Equipment) */
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES
                (v_port_rec.port_id, 'organics', 'buy'),
                (v_port_rec.port_id, 'equipment', 'buy'),
                (v_port_rec.port_id, 'ore',       'sell');

        ELSIF v_port_rec.type = 2 THEN
            /* SSB */
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES
                (v_port_rec.port_id, 'ore',       'sell'),
                (v_port_rec.port_id, 'organics',  'sell'),
                (v_port_rec.port_id, 'equipment', 'buy');

        ELSIF v_port_rec.type = 3 THEN
            /* BSS */
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES
                (v_port_rec.port_id, 'ore',       'buy'),
                (v_port_rec.port_id, 'organics',  'sell'),
                (v_port_rec.port_id, 'equipment', 'sell');

        ELSIF v_port_rec.type = 4 THEN
            /* BSB */
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES
                (v_port_rec.port_id, 'ore',       'buy'),
                (v_port_rec.port_id, 'organics',  'sell'),
                (v_port_rec.port_id, 'equipment', 'buy');

        ELSIF v_port_rec.type = 5 THEN
            /* BBS */
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES
                (v_port_rec.port_id, 'ore',       'buy'),
                (v_port_rec.port_id, 'organics',  'buy'),
                (v_port_rec.port_id, 'equipment', 'sell');

        ELSIF v_port_rec.type = 6 THEN
            /* SBS */
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES
                (v_port_rec.port_id, 'ore',       'sell'),
                (v_port_rec.port_id, 'organics',  'buy'),
                (v_port_rec.port_id, 'equipment', 'sell');

        ELSIF v_port_rec.type = 7 THEN
            /* BBB (Specialty Port) */
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES
                (v_port_rec.port_id, 'ore',       'buy'),
                (v_port_rec.port_id, 'organics',  'buy'),
                (v_port_rec.port_id, 'equipment', 'buy');

        ELSIF v_port_rec.type = 8 THEN
            /* SSS (Specialty Port) */
            INSERT INTO port_trade (port_id, commodity, mode)
            VALUES
                (v_port_rec.port_id, 'ore',       'sell'),
                (v_port_rec.port_id, 'organics',  'sell'),
                (v_port_rec.port_id, 'equipment', 'sell');
        END IF;

        /*
         * Capacity model used by server_ports.c:
         * max_capacity = ports.size * 1000
         * If size is NULL, default to 10 (matches your observed 10000 caps).
         */
        SELECT COALESCE(p.size, 10) INTO v_size
          FROM ports p
         WHERE p.port_id = v_port_rec.port_id;

        v_cap := v_size * 1000;

        /*
         * Seed initial stock:
         * - If port mode is 'buy'  => low stock (0..10% of cap)
         * - If port mode is 'sell' => high stock (60..95% of cap)
         *
         * Prices seeded to base_price (runtime code adjusts as needed).
         */
        INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price)
        VALUES
            ('port', v_port_rec.port_id, v_code_ore,
                CASE
                    WHEN EXISTS (SELECT 1 FROM port_trade pt WHERE pt.port_id = v_port_rec.port_id AND pt.commodity='ore' AND pt.mode='buy')
                        THEN GREATEST(0, (v_cap * (random() * 0.10))::int)
                    ELSE GREATEST(1, (v_cap * (0.60 + random() * 0.35))::int)
                END,
                v_base_ore
            ),
            ('port', v_port_rec.port_id, v_code_org,
                CASE
                    WHEN EXISTS (SELECT 1 FROM port_trade pt WHERE pt.port_id = v_port_rec.port_id AND pt.commodity='organics'  AND pt.mode='buy')
                        THEN GREATEST(0, (v_cap * (random() * 0.10))::int)
                    ELSE GREATEST(1, (v_cap * (0.60 + random() * 0.35))::int)
                END,
                v_base_org
            ),
            ('port', v_port_rec.port_id, v_code_equ,
                CASE
                    WHEN EXISTS (SELECT 1 FROM port_trade pt WHERE pt.port_id = v_port_rec.port_id AND pt.commodity='equipment' AND pt.mode='buy')
                        THEN GREATEST(0, (v_cap * (random() * 0.10))::int)
                    ELSE GREATEST(1, (v_cap * (0.60 + random() * 0.35))::int)
                END,
                v_base_equ
            )
        ON CONFLICT DO NOTHING;

        /*
         * Illegal goods are intentionally NOT seeded here.
         * clusters aren't populated yet, so this generator stays “legal-only”.
         */
    END LOOP;

    /* Create bank accounts for the new ports */
    INSERT INTO bank_accounts (owner_type, owner_id, currency, balance, interest_rate_bp, is_active)
    SELECT 'port', np.port_id, 'CRD', 50000, 0, TRUE
    FROM new_ports np;

    PERFORM bigbang_unlock(v_lock_key);
    RETURN v_added;
END;
$$;



-- ---------------------------------------------------------------------------
-- 2.5) Stardock
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION generate_stardock ()
    RETURNS integer
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_sector int;
    v_max_sector int;
    v_lock_key bigint := 72002006;
    v_exists boolean;
    v_port_id int;
BEGIN
    PERFORM
        bigbang_lock (v_lock_key);
    -- Check if Stardock exists
    SELECT
        EXISTS (
            SELECT
                1
            FROM
                ports
            WHERE
                type = 9) INTO v_exists;
    IF v_exists THEN
        PERFORM
            bigbang_unlock (v_lock_key);
        RETURN 0;
    END IF;
    SELECT
        MAX(sector_id) INTO v_max_sector
    FROM
        sectors;
    -- Pick random sector_id > 10
    v_sector := 11 + floor(random() * (v_max_sector - 10));
    -- Insert Stardock (Type 9, Size 10, Tech 10)
    INSERT INTO ports (number, name, sector_id, type, size, techlevel, petty_cash)
        VALUES (v_sector, 'Stardock', v_sector, 9, 10, 10, 1000000)
    ON CONFLICT (sector_id, number)
        DO UPDATE SET
            name = EXCLUDED.name, type = EXCLUDED.type, size = EXCLUDED.size, techlevel = EXCLUDED.techlevel, petty_cash = EXCLUDED.petty_cash
        RETURNING
            port_id INTO v_port_id;
    -- Create bank account for Stardock
    IF v_port_id IS NOT NULL THEN
        INSERT INTO bank_accounts (owner_type, owner_id, currency, balance, interest_rate_bp, is_active)
        VALUES ('port', v_port_id, 'CRD', 1000000, 0, TRUE)
        ON CONFLICT DO NOTHING;
    END IF;
    -- Seed Stardock Assets
    IF v_port_id IS NOT NULL THEN
        INSERT INTO stardock_assets (sector_id, owner_id, fighters, defenses, ship_capacity)
            VALUES (v_sector, 1, 10000, 500, 100) -- Assuming owner_id 1 is System/Fed
        ON CONFLICT (sector_id)
            DO NOTHING;
        -- Seed Shipyard with all purchasable ships
        INSERT INTO shipyard_inventory (port_id, ship_type_id, enabled)
        SELECT
            v_port_id,
            shiptypes_id,
            TRUE
        FROM
            shiptypes
        WHERE
            can_purchase = TRUE
        ON CONFLICT (port_id,
            ship_type_id)
            DO NOTHING;
    END IF;
    PERFORM
        bigbang_unlock (v_lock_key);
    RETURN 1;
END;
$$;
-- ---------------------------------------------------------------------------
-- 3) Planets
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION generate_planets (target_count int)
    RETURNS integer
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_existing bigint;
    v_to_add bigint;
    v_added bigint := 0;
    v_lock_key bigint := 72002003;
    v_sector_count bigint;
    v_base_num bigint;
    v_ptype bigint;
BEGIN
    IF target_count IS NULL OR target_count < 0 THEN
        RAISE EXCEPTION 'target_count must be >= 0';
    END IF;
    PERFORM
        bigbang_lock (v_lock_key);
    SELECT
        COUNT(*) INTO v_existing
    FROM
        planets;
    v_to_add := GREATEST (target_count - v_existing, 0);
    IF v_to_add = 0 THEN
        PERFORM
            bigbang_unlock (v_lock_key);
        RETURN 0;
    END IF;
    SELECT
        COUNT(*) INTO v_sector_count
    FROM
        sectors;
    IF v_sector_count = 0 THEN
        RAISE EXCEPTION 'Cannot generate planets: no sectors exist';
    END IF;
    SELECT
        COALESCE(MAX(num), 0) + 1 INTO v_base_num
    FROM
        planets;
    SELECT
        planettypes_id INTO v_ptype
    FROM
        planettypes
    ORDER BY
        planettypes_id
    LIMIT 1;
    IF v_ptype IS NULL THEN
        RAISE EXCEPTION 'Cannot generate planets: no planettypes exist';
    END IF;
    INSERT INTO planets (num, sector_id, name, owner_id, owner_type, class, type, created_at, created_by)
    SELECT
        (v_base_num + gs - 1) AS num,
        ((gs - 1) % v_sector_count + 1)::int AS sector_id,
        format('Planet %s', (v_base_num + gs - 1)) AS name,
        0 AS owner_id,
        'none' AS owner_type,
        'M' AS class,
        v_ptype AS type,
        now() AS created_at,
        0 AS created_by
    FROM
        generate_series(1, v_to_add) AS gs;
    GET DIAGNOSTICS v_added = ROW_COUNT;
    PERFORM
        bigbang_unlock (v_lock_key);
    RETURN v_added;
END;
$$;
-- ---------------------------------------------------------------------------
-- 4) seed_factions()
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION seed_factions ()
    RETURNS integer
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_added bigint := 0;
    v_rows bigint;
    -- Temp variable to capture ROW_COUNT
    v_lock_key bigint := 72002004;
BEGIN
    PERFORM
        bigbang_lock (v_lock_key);
    INSERT INTO corporations (name, owner_id, tag, description)
    SELECT
        'Imperial',
        NULL,
        'IMP',
        'Imperial faction'
    WHERE
        NOT EXISTS (
            SELECT
                1
            FROM
                corporations
            WHERE
                tag = 'IMP');
    -- Corrected: Capture row count first, then add to total
    GET DIAGNOSTICS v_rows = ROW_COUNT;
    v_added := v_added + v_rows;
    
    INSERT INTO corporations (name, owner_id, tag, description)
    SELECT
        'Ferengi Alliance',
        NULL,
        'FENG',
        'Ferengi faction'
    WHERE
        NOT EXISTS (
            SELECT
                1
            FROM
                corporations
            WHERE
                tag = 'FENG');
    GET DIAGNOSTICS v_rows = ROW_COUNT;
    v_added := v_added + v_rows;

    INSERT INTO corporations (name, owner_id, tag, description)
    SELECT
        'Orion Syndicate',
        NULL,
        'ORION',
        'Orion faction'
    WHERE
        NOT EXISTS (
            SELECT
                1
            FROM
                corporations
            WHERE
                tag = 'ORION');
    GET DIAGNOSTICS v_rows = ROW_COUNT;
    v_added := v_added + v_rows;

    PERFORM
        bigbang_unlock (v_lock_key);
    RETURN v_added;
END;
$$;
-- ---------------------------------------------------------------------------
-- 5) Clusters
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION generate_clusters (target_count int DEFAULT 10)
    RETURNS integer
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_added bigint := 0;
    v_lock_key bigint := 72002005;
    v_max_sector bigint;
    v_center int;
    v_name text;
    v_align int;
    v_cluster_id int;
BEGIN
    PERFORM
        bigbang_lock (v_lock_key);
    -- Truncate for idempotency
    TRUNCATE cluster_sectors;
    SELECT
        MAX(sector_id) INTO v_max_sector
    FROM
        sectors;
    IF v_max_sector IS NULL THEN
        PERFORM
            bigbang_unlock (v_lock_key);
        RETURN 0;
    END IF;
    -- 1. Federation Core (Fixed)
    INSERT INTO clusters (name, ROLE, kind, center_sector, law_severity, alignment)
        VALUES ('Federation Core', 'FED', 'FACTION', 1, 3, 100)
    ON CONFLICT (name)
        DO UPDATE SET ROLE = EXCLUDED.role, kind = EXCLUDED.kind, center_sector = EXCLUDED.center_sector, law_severity = EXCLUDED.law_severity, alignment = EXCLUDED.alignment;
    -- 2. Random Clusters
    FOR i IN 1.. (target_count - 1)
    LOOP
        v_center := 11 + floor(random() * (v_max_sector - 10));
        v_name := format('Cluster %s', v_center);
        v_align := floor(random() * 200) - 100;
        -- -100 to 100
        INSERT INTO clusters (name, ROLE, kind, center_sector, law_severity, alignment)
            VALUES (v_name, 'RANDOM', 'RANDOM', v_center, 1, v_align)
        ON CONFLICT (name)
            DO UPDATE SET ROLE = EXCLUDED.role, kind = EXCLUDED.kind, center_sector = EXCLUDED.center_sector, law_severity = EXCLUDED.law_severity, alignment = EXCLUDED.alignment;
    END LOOP;
    -- 3. Populate cluster_sectors table
    FOR v_cluster_id,
    v_center IN
    SELECT
        clusters_id,
        center_sector
    FROM
        clusters LOOP
            -- Add center sector_id
            INSERT INTO cluster_sectors (cluster_id, sector_id)
                VALUES (v_cluster_id, v_center)
            ON CONFLICT
                DO NOTHING;
            -- Add adjacent sectors
            INSERT INTO cluster_sectors (cluster_id, sector_id)
            SELECT
                v_cluster_id,
                to_sector
            FROM
                sector_warps
            WHERE
                from_sector = v_center
            ON CONFLICT
                DO NOTHING;
        END LOOP;
    GET DIAGNOSTICS v_added = ROW_COUNT;
    PERFORM
        bigbang_unlock (v_lock_key);
    RETURN v_added;
END;
$$;
-- ---------------------------------------------------------------------------
-- 6) Taverns
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION generate_taverns (p_ratio_pct int DEFAULT 20)
    RETURNS integer
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_added bigint := 0;
    v_lock_key bigint := 72002007;
    v_ratio numeric := p_ratio_pct / 100.0;
BEGIN
    PERFORM
        bigbang_lock (v_lock_key);
    INSERT INTO taverns (sector_id, name_id)
    SELECT
        s.sector_id,
        (
            SELECT
                tavern_names_id
            FROM
                tavern_names
            ORDER BY
                random()
            LIMIT 1)
FROM
    sectors s
WHERE
    s.sector_id > 10 -- No taverns in Fedspace core
        AND NOT EXISTS (
            SELECT
                1
            FROM
                taverns t
            WHERE
                t.sector_id = s.sector_id)
        AND random() < v_ratio;
    GET DIAGNOSTICS v_added = ROW_COUNT;
    PERFORM
        bigbang_unlock (v_lock_key);
    RETURN v_added;
END;
$$;
-- ---------------------------------------------------------------------------
-- 7) Player Registration - Auto-create starter ship
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION register_player (p_username text, p_password text, p_ship_name text DEFAULT NULL, p_is_npc boolean DEFAULT FALSE, p_sector_id integer DEFAULT 1, p_player_type integer DEFAULT 2)
    RETURNS integer
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_player_id integer;
    v_ship_id integer;
    v_ship_name text;
    v_starting_credits bigint;
    v_turns_per_day integer;
BEGIN
    -- 1. Check if player already exists
    SELECT player_id INTO v_player_id FROM players WHERE name = p_username;

    IF FOUND THEN
        RETURN v_player_id;
    END IF;

    -- Use provided ship name or default
    v_ship_name := COALESCE(p_ship_name, 'Used Scout Marauder');
    -- Get config values
    SELECT
        CAST(value AS bigint) INTO v_starting_credits
    FROM
        config
    WHERE
        key = 'startingcredits';
    SELECT
        CAST(value AS integer) INTO v_turns_per_day
    FROM
        config
    WHERE
        key = 'turnsperday';
    v_starting_credits := COALESCE(v_starting_credits, 5000);
    v_turns_per_day := COALESCE(v_turns_per_day, 500);
    -- Insert new player
    INSERT INTO players (name, passwd, sector_id, credits, type, is_npc, loggedin, commission_id)
        VALUES (p_username, p_password, p_sector_id, v_starting_credits, p_player_type, p_is_npc, now(), 1)
    RETURNING
        player_id INTO v_player_id;

    -- Create starter ship (Scout Marauder, type_id = 2)
    INSERT INTO ships (name, type_id, sector_id, holds, fighters, shields)
        VALUES (v_ship_name, 2, p_sector_id, 10, 1, 1)
    RETURNING
        ship_id INTO v_ship_id;

    -- Assign ship to player
    UPDATE
        players
    SET
        ship_id = v_ship_id
    WHERE
        player_id = v_player_id;

    -- Create ship ownership record
    INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary)
        VALUES (v_ship_id, v_player_id, 1, TRUE);

    -- Create bank account for player
    INSERT INTO bank_accounts (owner_type, owner_id, currency, balance)
        VALUES ('player', v_player_id, 'CRD', 0)
    ON CONFLICT
        DO NOTHING;

    -- Initialize turns for player
    INSERT INTO turns (player_id, turns_remaining, last_update)
        VALUES (v_player_id, v_turns_per_day, now())
    ON CONFLICT
        DO NOTHING;

    -- Create player preferences record
    INSERT INTO player_prefs (player_prefs_id, key, type, value)
        VALUES (v_player_id, 'default_preferences_initialized', 'bool', 'true')
    ON CONFLICT
        DO NOTHING;

    RETURN v_player_id;
END;
$$;
-- ---------------------------------------------------------------------------
-- 8) Missing Stubs (Added to prevent bigbang failure)
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION setup_npc_homeworlds ()
    RETURNS void
    LANGUAGE plpgsql
    AS $$
BEGIN
    -- Terra is always in sector 1, no need for dynamic placement
    INSERT INTO planets (planet_id, num, sector_id, name, owner_id, owner_type, class, type, created_at, created_by)
    SELECT 1, 1, 1, 'Terra', 0, 'player', 'M', planettypes_id, now(), 0
    FROM planettypes WHERE code = 'M' LIMIT 1
    ON CONFLICT (planet_id) DO UPDATE SET name = EXCLUDED.name, sector_id = EXCLUDED.sector_id;
END;
$$;

CREATE OR REPLACE FUNCTION setup_ferringhi_alliance ()
    RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_ferringhi_sector integer;
    v_ptype bigint;
    v_ferringhi_corp_id bigint;
BEGIN
    -- Get Ferringhi planet type
    SELECT planettypes_id INTO v_ptype
    FROM planettypes WHERE code = 'M' LIMIT 1;
    
    -- Get Ferengi Alliance corporation ID by tag
    SELECT corporation_id INTO v_ferringhi_corp_id
    FROM corporations WHERE tag = 'FENG' LIMIT 1;
    
    -- Pick a random exit sector from longest_tunnels
    SELECT exit_sector INTO v_ferringhi_sector
    FROM longest_tunnels 
    WHERE exit_sector > 10
    ORDER BY random() LIMIT 1;
    
    -- If no tunnels exist, default to a high sector
    IF v_ferringhi_sector IS NULL OR v_ferringhi_sector = 0 THEN
        v_ferringhi_sector := 469;
    END IF;
    
    -- Create Ferenginar planet (planet_id 2)
    INSERT INTO planets (planet_id, num, sector_id, name, owner_id, owner_type, class, type, created_at, created_by)
    SELECT 2, 2, v_ferringhi_sector, 'Ferenginar', COALESCE(v_ferringhi_corp_id, 0), 
           'corp', 'M', v_ptype, now(), 0
    ON CONFLICT (planet_id) DO UPDATE SET sector_id = EXCLUDED.sector_id, owner_id = EXCLUDED.owner_id, owner_type = 'corp', name = EXCLUDED.name;
    
    -- Deploy defensive fighters and mines at Ferengi sector
    IF v_ferringhi_corp_id IS NOT NULL THEN
        INSERT INTO sector_assets (sector_id, owner_id, corporation_id, asset_type, offensive_setting, quantity, deployed_at)
        VALUES (v_ferringhi_sector, NULL, v_ferringhi_corp_id, 2, 3, 50000, now())
        ON CONFLICT DO NOTHING;
        
        INSERT INTO sector_assets (sector_id, owner_id, corporation_id, asset_type, quantity, deployed_at)
        VALUES (v_ferringhi_sector, NULL, v_ferringhi_corp_id, 1, 250, now())
        ON CONFLICT DO NOTHING;
    END IF;
END;
$$;

CREATE OR REPLACE FUNCTION setup_orion_syndicate ()
    RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_orion_sector integer;
    v_ptype bigint;
    v_orion_corp_id bigint;
    v_port_id integer;
    v_ferengi_sector integer;
BEGIN
    -- Get Orion planet type
    SELECT planettypes_id INTO v_ptype
    FROM planettypes WHERE code = 'M' LIMIT 1;
    
    -- Get Orion Syndicate corporation ID by tag
    SELECT corporation_id INTO v_orion_corp_id
    FROM corporations WHERE tag = 'ORION' LIMIT 1;

    -- Get current Ferengi sector to avoid it
    SELECT sector_id INTO v_ferengi_sector FROM planets WHERE planet_id = 2;
    
    -- Pick a random exit sector from longest_tunnels that isn't Ferenginar
    SELECT exit_sector INTO v_orion_sector
    FROM longest_tunnels 
    WHERE exit_sector > 10 AND exit_sector != COALESCE(v_ferengi_sector, 0)
    ORDER BY random() LIMIT 1;
    
    -- If no second tunnel or default conflict, use random sector
    IF v_orion_sector IS NULL OR v_orion_sector = 0 THEN
        v_orion_sector := (random() * 989)::int + 11;
    END IF;
    
    -- Create Orion Hideout planet (planet_id 3)
    INSERT INTO planets (planet_id, num, sector_id, name, owner_id, owner_type, class, type, created_at, created_by)
    SELECT 3, 3, v_orion_sector, 'Orion Hideout', COALESCE(v_orion_corp_id, 0), 
           'corp', 'M', v_ptype, now(), 0
    ON CONFLICT (planet_id) DO UPDATE SET sector_id = EXCLUDED.sector_id, owner_id = EXCLUDED.owner_id, owner_type = 'corp', name = EXCLUDED.name;
    
    -- Deploy defensive fighters and mines at Orion sector
    IF v_orion_corp_id IS NOT NULL THEN
        INSERT INTO sector_assets (sector_id, owner_id, corporation_id, asset_type, offensive_setting, quantity, deployed_at)
        VALUES (v_orion_sector, NULL, v_orion_corp_id, 2, 2, 50000, now())
        ON CONFLICT DO NOTHING;
        
        INSERT INTO sector_assets (sector_id, owner_id, corporation_id, asset_type, quantity, deployed_at)
        VALUES (v_orion_sector, NULL, v_orion_corp_id, 1, 250, now())
        ON CONFLICT DO NOTHING;
    END IF;
    
    -- Create Orion Black Market port (type 10 = blackmarket stardock)
    INSERT INTO ports (sector_id, name, type, number)
    VALUES (v_orion_sector, 'Orion Black Market Dock', 10, v_orion_sector)
    ON CONFLICT (sector_id, number) DO UPDATE SET sector_id = EXCLUDED.sector_id, name = EXCLUDED.name
    RETURNING port_id INTO v_port_id;

    -- Ensure bank account exists (even if port already existed)
    IF v_port_id IS NULL THEN
         SELECT port_id INTO v_port_id FROM ports WHERE sector_id = v_orion_sector AND type = 10;
    END IF;

    IF v_port_id IS NOT NULL THEN
         INSERT INTO bank_accounts (owner_type, owner_id, currency, balance, interest_rate_bp, is_active)
         VALUES ('port', v_port_id, 'CRD', 1000000, 0, TRUE)
         ON CONFLICT (owner_type, owner_id, currency) DO NOTHING;
    END IF;
END;
$$;

CREATE OR REPLACE FUNCTION spawn_orion_fleet ()
    RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_orion_sector integer;
    v_ship_id integer;
BEGIN
    -- Get Orion home sector
    SELECT center_sector INTO v_orion_sector
    FROM clusters WHERE name = 'Orion Syndicate' LIMIT 1;
    
    IF v_orion_sector IS NULL THEN
        RETURN;
    END IF;
    
    -- Spawn Zydras with Heavy Fighter Patrol (shiptypes_id 18)
    INSERT INTO ships (name, type_id, sector_id, holds, fighters, shields, destroyed)
    VALUES ('Zydras Heavy Cruiser', 18, v_orion_sector, 10, 5, 3, FALSE)
    RETURNING ship_id INTO v_ship_id;
    INSERT INTO ship_ownership (ship_id, player_id, role_id) VALUES (v_ship_id, 5, 1) ON CONFLICT DO NOTHING;
    UPDATE players SET ship_id = v_ship_id WHERE player_id = 5;
    
    -- Spawn Krell with Scout/Looter (shiptypes_id 19)
    INSERT INTO ships (name, type_id, sector_id, holds, fighters, shields, destroyed)
    VALUES ('Krell Scout', 19, v_orion_sector, 10, 2, 1, FALSE)
    RETURNING ship_id INTO v_ship_id;
    INSERT INTO ship_ownership (ship_id, player_id, role_id) VALUES (v_ship_id, 6, 1) ON CONFLICT DO NOTHING;
    UPDATE players SET ship_id = v_ship_id WHERE player_id = 6;
    
    -- Spawn Vex with Contraband Runner (shiptypes_id 20)
    INSERT INTO ships (name, type_id, sector_id, holds, fighters, shields, destroyed)
    VALUES ('Vex Contraband Runner', 20, v_orion_sector, 10, 3, 2, FALSE)
    RETURNING ship_id INTO v_ship_id;
    INSERT INTO ship_ownership (ship_id, player_id, role_id) VALUES (v_ship_id, 7, 1) ON CONFLICT DO NOTHING;
    UPDATE players SET ship_id = v_ship_id WHERE player_id = 7;
    
    -- Spawn Jaxx with Smuggler's Kiss (shiptypes_id 21)
    INSERT INTO ships (name, type_id, sector_id, holds, fighters, shields, destroyed)
    VALUES ('Jaxx Smuggler', 21, v_orion_sector, 10, 3, 2, FALSE)
    RETURNING ship_id INTO v_ship_id;
    INSERT INTO ship_ownership (ship_id, player_id, role_id) VALUES (v_ship_id, 8, 1) ON CONFLICT DO NOTHING;
    UPDATE players SET ship_id = v_ship_id WHERE player_id = 8;
    
    -- Spawn Sira with Black Market Guard (shiptypes_id 22)
    INSERT INTO ships (name, type_id, sector_id, holds, fighters, shields, destroyed)
    VALUES ('Sira Market Guard', 22, v_orion_sector, 10, 4, 2, FALSE)
    RETURNING ship_id INTO v_ship_id;
    INSERT INTO ship_ownership (ship_id, player_id, role_id) VALUES (v_ship_id, 9, 1) ON CONFLICT DO NOTHING;
    UPDATE players SET ship_id = v_ship_id WHERE player_id = 9;
    -- Add all Orion captains to the Orion Syndicate corporation (by name, not ID)
    -- All are Officers; Zydras is the Owner at corporation level
    INSERT INTO corp_members (corporation_id, player_id, role)
    SELECT 
        (SELECT corporation_id FROM corporations WHERE tag = 'ORION' LIMIT 1),
        player_id,
        'Officer' as role
    FROM players
    WHERE name IN (
        'Zydras, Heavy Fighter Captain',
        'Krell, Scout Captain',
        'Vex, Contraband Captain',
        'Jaxx, Smuggler Captain',
        'Sira, Market Guard Captain'
    )
    AND is_npc = TRUE
    ON CONFLICT DO NOTHING;
END;
$$;
CREATE OR REPLACE FUNCTION spawn_initial_fleet ()
    RETURNS void
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_sector_count bigint;
    v_st_rec record;
    v_sector_id integer;
    v_name text;
    v_ship_id integer;
    v_exists boolean;
BEGIN
    -- 1. Seed npc_shipnames if empty
    SELECT EXISTS (SELECT 1 FROM npc_shipnames) INTO v_exists;
    IF NOT v_exists THEN
        INSERT INTO npc_shipnames (name) VALUES
        ('Kobayashi Maru'), ('Botany Bay'), ('Reliant'), ('Enterprise'), ('Heart of Gold'),
        ('Millennium Falcon'), ('Nostromo'), ('Sulaco'), ('Event Horizon'), ('Icarus'),
        ('Prometheus'), ('Serenity'), ('Galactica'), ('Pegasus'), ('Normandy'),
        ('Pillar of Autumn'), ('In Amber Clad'), ('Forward Unto Dawn'), ('Spirit of Fire'),
        ('Infinity'), ('Defiant'), ('Voyager'), ('Discovery'), ('Protostar'),
        ('Daedalus'), ('Odyssey'), ('Equinox'), ('Thunderchild'), ('Yamato'), ('Arcadia');
    END IF;

    -- 2. Get Sector Count
    SELECT count(*) INTO v_sector_count FROM sectors;
    IF v_sector_count < 11 THEN
        RETURN; -- Not enough sectors
    END IF;

    -- 3. Iterate Ship Types and Create Derelicts
    FOR v_st_rec IN
        SELECT shiptypes_id, name FROM shiptypes WHERE enabled = TRUE
    LOOP
        -- Pick random name
        SELECT name INTO v_name FROM npc_shipnames ORDER BY random() LIMIT 1;
        IF v_name IS NULL THEN
            v_name := 'Derelict ' || v_st_rec.name;
        END IF;

        -- Pick random sector > 10 (avoid FedSpace)
        v_sector_id := 11 + floor(random() * (v_sector_count - 10));

        -- Insert Ship (Ownerless by default as we don't insert into ship_ownership)
        INSERT INTO ships (name, type_id, sector_id, holds, fighters, shields, destroyed)
        VALUES (v_name, v_st_rec.shiptypes_id, v_sector_id, 10, 1, 1, FALSE)
        RETURNING ship_id INTO v_ship_id;

        -- Optional: Add random cargo? (Old code didn't, but we could)
    END LOOP;
END;
$$;
CREATE OR REPLACE FUNCTION apply_game_defaults ()
    RETURNS void
    LANGUAGE plpgsql
    AS $$
BEGIN
END;
$$;

-- ============================================================================
-- MSL (Major Space Lanes) Generation - Moved from Server Startup
-- ============================================================================
-- Computes paths from FedSpace (1-10) to all Stardocks and marks those sectors
-- as MSL. This must be called after sector warps are generated.

CREATE OR REPLACE FUNCTION generate_msl()
    RETURNS integer
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_lock_key bigint := 72002010;
    i int;
    v_inserted int;
    v_msl_count int;
BEGIN
    PERFORM bigbang_lock(v_lock_key);

    -- Safety: don't let this run forever if something goes wrong
    PERFORM set_config('statement_timeout', '60s', true);

    DELETE FROM msl_sectors;

    -- Distances/parents for one shortest-path tree (multi-source from 1..10)
    CREATE TEMP TABLE sector_distances (
        sector_id  int PRIMARY KEY,
        dist       int NOT NULL,
        parent_id  int NOT NULL
    ) ON COMMIT DROP;

    -- Seed FedSpace
    INSERT INTO sector_distances(sector_id, dist, parent_id)
    SELECT gs, 0, 0
    FROM generate_series(1,10) gs;

    -- BFS outwards up to depth 100 (safe bound)
    FOR i IN 0..99 LOOP
        INSERT INTO sector_distances(sector_id, dist, parent_id)
        SELECT DISTINCT ON (sw.to_sector)
            sw.to_sector::int,
            i + 1,
            sw.from_sector::int
        FROM sector_distances sd
        JOIN sector_warps sw
          ON sw.from_sector = sd.sector_id
        WHERE sd.dist = i
          AND NOT EXISTS (
              SELECT 1 FROM sector_distances d2 WHERE d2.sector_id = sw.to_sector
          )
        ORDER BY sw.to_sector, sw.from_sector;  -- deterministic parent tie-break

        GET DIAGNOSTICS v_inserted = ROW_COUNT;
        EXIT WHEN v_inserted = 0;
    END LOOP;

    -- Base MSL always includes FedSpace core
    INSERT INTO msl_sectors(sector_id)
    SELECT generate_series(1,10)
    ON CONFLICT DO NOTHING;

    -- Backtrack from each reachable stardock using parent pointers
    INSERT INTO msl_sectors(sector_id)
    WITH RECURSIVE trace AS (
        SELECT d.sector_id, d.parent_id, d.dist
        FROM sector_distances d
        JOIN ports p ON p.sector_id = d.sector_id AND p.type = 9

        UNION ALL

        SELECT d.sector_id, d.parent_id, d.dist
        FROM sector_distances d
        JOIN trace t ON d.sector_id = t.parent_id
        WHERE t.dist > 0
    )
    SELECT DISTINCT sector_id
    FROM trace
    ON CONFLICT DO NOTHING;

    SELECT COUNT(*) INTO v_msl_count FROM msl_sectors;

    RAISE NOTICE 'MSL Generation: % sectors', v_msl_count;

    PERFORM bigbang_unlock(v_lock_key);
    RETURN v_msl_count;
END;
$$;

-- ============================================================================
-- Cluster Generation v2 (Flood-Fill with ~50% Coverage)
-- ============================================================================
-- Generates clusters with real regions using radius flood-fill (BFS)
-- Federation cluster includes sectors 1-10 and all MSL sectors.
-- Other clusters spread to ~50% of remaining universe.

CREATE OR REPLACE FUNCTION generate_clusters_v2 (target_coverage_pct int DEFAULT 50)
    RETURNS integer
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_lock_key bigint := 72002011;
    v_max_sector bigint;
    v_fed_cluster_id bigint;
    v_orion_cluster_id bigint;
    v_ferengi_cluster_id bigint;
    v_cluster_id bigint;
    v_center_sector int;
    v_align int;
    v_total_target int;
    v_per_cluster_target int;
    v_cluster_count int;
    v_added int := 0;
    v_i int;
    v_attempts int;
    v_sectors_for_this_cluster int;
    rec record;
    rec_edge record;
BEGIN
    PERFORM bigbang_lock (v_lock_key);
    
    -- Truncate for idempotency
    TRUNCATE cluster_sectors;
    
    -- Get max sector
    SELECT MAX(sector_id) INTO v_max_sector FROM sectors;
    IF v_max_sector IS NULL OR v_max_sector <= 0 THEN
        PERFORM bigbang_unlock (v_lock_key);
        RETURN 0;
    END IF;
    
    -- Calculate target cluster count: random between 3 and 20 (scaled by universe)
    -- Formula: 3 + random portion = 3..20
    v_cluster_count := 3 + (floor(random() * 18))::int;
    v_cluster_count := GREATEST(3, LEAST(20, v_cluster_count));
    
    -- Target total sectors for random clusters (excludes Fed/special)
    v_total_target := (v_max_sector * target_coverage_pct) / 100;
    v_per_cluster_target := v_total_target / v_cluster_count;
    
    -- Create Federation cluster (fixed)
    INSERT INTO clusters (name, role, kind, center_sector, law_severity, alignment)
    VALUES ('Federation Core', 'FED', 'FACTION', 1, 3, 100)
    ON CONFLICT (name) DO UPDATE 
    SET role = EXCLUDED.role, kind = EXCLUDED.kind, 
        center_sector = EXCLUDED.center_sector,
        law_severity = EXCLUDED.law_severity,
        alignment = EXCLUDED.alignment;
    
    -- Get Federation cluster ID
    SELECT clusters_id INTO v_fed_cluster_id 
    FROM clusters WHERE name = 'Federation Core';
    
    -- Populate Federation cluster BASE: sectors 1-10 + all MSL sectors
    INSERT INTO cluster_sectors (cluster_id, sector_id)
    SELECT v_fed_cluster_id, generate_series(1, 10)
    ON CONFLICT DO NOTHING;
    
    INSERT INTO cluster_sectors (cluster_id, sector_id)
    SELECT v_fed_cluster_id, sector_id FROM msl_sectors
    ON CONFLICT DO NOTHING;
    
    -- Expand Federation cluster HALO: 2 hops outward from base sectors
    -- Add neighbors-of-neighbors but DO NOT modify MSL table
    INSERT INTO cluster_sectors (cluster_id, sector_id)
    WITH RECURSIVE fed_halo(sector_id, hops) AS (
        -- Start from base Fed sectors (1-10 + MSL)
        SELECT sector_id, 0
        FROM (SELECT generate_series(1, 10) as sector_id
              UNION
              SELECT sector_id FROM msl_sectors) base
        
        UNION ALL
        
        -- Expand 2 hops outward
        SELECT DISTINCT sw.to_sector::int, fh.hops + 1
        FROM fed_halo fh
        JOIN sector_warps sw ON fh.sector_id = sw.from_sector
        WHERE fh.hops < 2  -- Hard limit: 2 hops
          AND NOT EXISTS (
            -- Don't add sectors already in Fed cluster
            SELECT 1 FROM cluster_sectors 
            WHERE cluster_id = v_fed_cluster_id AND sector_id = sw.to_sector
          )
    )
    SELECT DISTINCT v_fed_cluster_id, sector_id
    FROM fed_halo
    WHERE hops > 0  -- Only add the halo, not the base (already added)
    ON CONFLICT DO NOTHING;
    
    -- Log sanity checks
    RAISE NOTICE 'MSL Sectors: %', (SELECT COUNT(*) FROM msl_sectors);
    RAISE NOTICE 'Federation cluster total sectors: %', (SELECT COUNT(*) FROM cluster_sectors WHERE cluster_id = v_fed_cluster_id);
    RAISE NOTICE 'Stardocks in MSL: %', (SELECT COUNT(*) FROM stardock_location sl WHERE EXISTS (SELECT 1 FROM msl_sectors WHERE sector_id = sl.sector_id));
    
    -- Create Orion cluster (special)
    -- Use Orion Hideout planet (planet_id 3) to find the center sector
    SELECT pl.sector_id INTO v_center_sector
    FROM planets pl
    WHERE pl.planet_id = 3
    LIMIT 1;
    
    INSERT INTO clusters (name, role, kind, center_sector, law_severity, alignment)
    VALUES ('Orion Syndicate', 'ORION', 'SYNDICATE', v_center_sector, 0, -80)
    ON CONFLICT (name) DO UPDATE 
    SET role = EXCLUDED.role, kind = EXCLUDED.kind,
        center_sector = EXCLUDED.center_sector,
        law_severity = EXCLUDED.law_severity,
        alignment = EXCLUDED.alignment;
    
    SELECT clusters_id INTO v_orion_cluster_id 
    FROM clusters WHERE name = 'Orion Syndicate';
    
    -- Find Orion homeworld and add small region around it
    FOR rec IN 
        SELECT pl.sector_id 
        FROM planets pl
        WHERE LOWER(pl.name) LIKE '%orion%' 
        LIMIT 5
    LOOP
        -- Add homeworld to Orion cluster
        INSERT INTO cluster_sectors (cluster_id, sector_id)
        VALUES (v_orion_cluster_id, rec.sector_id)
        ON CONFLICT DO NOTHING;
        
        -- Small bounded flood-fill (max 20 sectors, 5 hops)
        v_sectors_for_this_cluster := 0;
        INSERT INTO cluster_sectors (cluster_id, sector_id)
        WITH RECURSIVE ff(sector_id, hops) AS (
            SELECT rec.sector_id::int, 0
            
            UNION ALL
            
            SELECT DISTINCT sw.to_sector::int, ff.hops + 1
            FROM ff
            JOIN sector_warps sw ON ff.sector_id = sw.from_sector
            WHERE ff.hops < 5  -- Hard hop limit
              AND NOT EXISTS (
                SELECT 1 FROM cluster_sectors 
                WHERE sector_id = sw.to_sector
              )
        )
        SELECT DISTINCT v_orion_cluster_id, sector_id 
        FROM ff
        WHERE sector_id NOT IN (SELECT sector_id FROM cluster_sectors)
        LIMIT 20
        ON CONFLICT DO NOTHING;
    END LOOP;
    
    -- Create Ferengi cluster (special)
    -- Find Ferengi homeworld first to use as center_sector
    SELECT pl.sector_id INTO v_center_sector
    FROM planets pl
    WHERE LOWER(pl.name) LIKE '%ferengi%' OR LOWER(pl.name) LIKE '%ferring%'
    LIMIT 1;
    
    INSERT INTO clusters (name, role, kind, center_sector, law_severity, alignment)
    VALUES ('Ferengi Alliance', 'FERENGI', 'ALLIANCE', v_center_sector, 1, 50)
    ON CONFLICT (name) DO UPDATE 
    SET role = EXCLUDED.role, kind = EXCLUDED.kind,
        center_sector = EXCLUDED.center_sector,
        law_severity = EXCLUDED.law_severity,
        alignment = EXCLUDED.alignment;
    
    SELECT clusters_id INTO v_ferengi_cluster_id 
    FROM clusters WHERE name = 'Ferengi Alliance';
    
    -- Find Ferengi homeworld and add small region around it
    FOR rec IN 
        SELECT pl.sector_id 
        FROM planets pl
        WHERE LOWER(pl.name) LIKE '%ferengi%' OR LOWER(pl.name) LIKE '%ferring%'
        LIMIT 5
    LOOP
        -- Add homeworld to Ferengi cluster
        INSERT INTO cluster_sectors (cluster_id, sector_id)
        VALUES (v_ferengi_cluster_id, rec.sector_id)
        ON CONFLICT DO NOTHING;
        
        -- Small bounded flood-fill (max 20 sectors, 5 hops)
        INSERT INTO cluster_sectors (cluster_id, sector_id)
        WITH RECURSIVE ff(sector_id, hops) AS (
            SELECT rec.sector_id::int, 0
            
            UNION ALL
            
            SELECT DISTINCT sw.to_sector::int, ff.hops + 1
            FROM ff
            JOIN sector_warps sw ON ff.sector_id = sw.from_sector
            WHERE ff.hops < 5  -- Hard hop limit
              AND NOT EXISTS (
                SELECT 1 FROM cluster_sectors 
                WHERE sector_id = sw.to_sector
              )
        )
        SELECT DISTINCT v_ferengi_cluster_id, sector_id 
        FROM ff
        WHERE sector_id NOT IN (SELECT sector_id FROM cluster_sectors)
        LIMIT 20
        ON CONFLICT DO NOTHING;
    END LOOP;
    
    -- Generate random clusters with bounded flood-fill
    v_i := 0;
    WHILE v_i < v_cluster_count AND v_i < 100 LOOP
        -- Pick random unclaimed center
        v_attempts := 0;
        v_center_sector := 0;
        
        WHILE v_attempts < 50 AND v_center_sector = 0 LOOP
            SELECT 1 + floor(random() * (v_max_sector))::int INTO v_center_sector;
            
            -- Check if already claimed
            IF EXISTS (
                SELECT 1 FROM cluster_sectors WHERE sector_id = v_center_sector
            ) THEN
                v_center_sector := 0;
            END IF;
            
            v_attempts := v_attempts + 1;
        END LOOP;
        
        IF v_center_sector = 0 THEN
            -- Exhausted attempts, done
            EXIT;
        END IF;
        
        -- Create cluster with random name
        v_align := floor(random() * 200)::int - 100;
        INSERT INTO clusters (name, role, kind, center_sector, law_severity, alignment)
        VALUES (randomname(), 'RANDOM', 'REGION', v_center_sector, 1, v_align)
        RETURNING clusters_id INTO v_cluster_id;
        
        -- Bounded flood-fill: expand up to per_cluster_target with 10-hop limit
        INSERT INTO cluster_sectors (cluster_id, sector_id)
        WITH RECURSIVE flood_fill(sector_id, hops) AS (
            -- Start: just the center
            SELECT v_center_sector::int, 0
            
            UNION ALL
            
            -- Expand: follow warps
            SELECT DISTINCT sw.to_sector::int, ff.hops + 1
            FROM flood_fill ff
            JOIN sector_warps sw ON ff.sector_id = sw.from_sector
            WHERE ff.hops < 10  -- Hard hop limit prevents runaway
              AND NOT EXISTS (
                  SELECT 1 FROM cluster_sectors 
                  WHERE sector_id = sw.to_sector
              )
        )
        SELECT DISTINCT v_cluster_id, sector_id 
        FROM flood_fill
        WHERE sector_id NOT IN (SELECT sector_id FROM cluster_sectors)
        LIMIT v_per_cluster_target::int  -- Hard size limit
        ON CONFLICT DO NOTHING;
        
        v_i := v_i + 1;
    END LOOP;
    
    -- Count total sectors added
    SELECT COUNT(*) INTO v_added FROM cluster_sectors;
    
    PERFORM bigbang_unlock (v_lock_key);
    RETURN v_added;
END;
$$;

COMMIT;


